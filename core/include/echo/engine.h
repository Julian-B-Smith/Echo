// echo/engine.h — Echo's stereo delay core (M1), behind a local stand-in for
// FOUNDATIONS' FX-operator ABI.
//
// DEGRADED AND TEMPORARY SHIM. FOUNDATIONS has not frozen the ABI (OQ #32), so
// this mirrors the shape horde already admits from Bulwark's shim (contract
// §1.1): prepare(double, int), reset(), process(double* L/R, int n), setParam by
// key or by a resolved handle, loadState(span), latency/tail/minBlock/channels.
// process() also takes the host tempo for the block (Sluice's shape): tempo is
// data, never a clock. The swap to the real header is a rename at the call site.
//
// SCOPE (ROADMAP M1). Built to spec/SPEC.md revision 2: the two 12 s lines with
// 4-point Catmull-Rom reads (D-004), free and synced time with L/R offsets and
// the §3.4 clamp, per-sample tempo interpolation (§3.3), the three routings with
// a 20 ms equal-gain matrix crossfade (§4), feedback with the ceiling on the
// returning term and the gain-normalised DC blocker (§5), the three filter
// positions with true bypass, 20 ms switch blends and 10 ms log cutoff glides
// (§6), mix and the I/O gain pair (§8), the tail, rest, load and non-finite
// rules (§10), FTZ (RS-7.8). Time moves: crossfade (time.xfadeMs) and jump
// (3 ms) per §3.5, with the latest-target-wins queue and per-move latching.
//
// M1 PLACEHOLDERS (visibly degraded, replaced in M2; ROADMAP M2):
//   - time.change = repitch moves the head by a crossfade of time.xfadeMs, not
//     by the tape / exponential laws of §3.6; time.law and time.glide are stored
//     and latched but have no audible effect yet.
//   - every tempo move is handled like a control move (a crossfade), whatever
//     time.tempoMode says: §3.8's (A) behaviour for all three modes.
//   - mod.* keys are validated and stored, and mod.depthMs enters the declared
//     tail, but the line modulation of §7 is not built: reads are unmodulated.
//   - meters with clip latches (§8, B435) are M3; only the non-finite repair
//     count and latch exist here.
//
// THREADING. One writer: every method is called from one thread at a time
// (horde's slot serialises them, RS-6.6). Only the non-finite counters are
// atomics, readable from another thread (RS-7.9).
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "echo/ftz.h"
#include "echo/params.h"
#include "echo/plants.h"

// __FINITE_MATH_ONLY__ is always DEFINED by clang (as 0); test its value, or
// this guard fires on every clean build.
// No contraction of a*b+c into a fused multiply-add inside this header: a
// fused op rounds once where another build rounds twice, so determinism across
// builds and platforms depends on it (critic M7). Scoped HERE, not by a CMake
// INTERFACE flag, so it never changes the numerics of horde's own code; the
// state is restored at the end of the file. ./verify proves it: a render built
// with -ffp-contract=fast must hash equal to the default build's.
// (ECHO_FP_PRAGMA_OFF exists ONLY for that probe's planted control.)
#if !defined(ECHO_FP_PRAGMA_OFF)
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif
#endif

#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__)
#error "echo: -ffast-math / -ffinite-math-only are forbidden: they delete the non-finite guards (RS-7.9)"
#endif

namespace echo {

// FOUNDATIONS' TailDecl sentinel stand-in (RS-7.3): "never ends".
inline constexpr std::int64_t kInfiniteTail = std::numeric_limits<std::int64_t>::max();

struct KeyValue {
  std::string_view key;
  double value;
};

// Constants (SPEC §13). Every one is in seconds, Hz or a ratio, converted at
// the current rate: no per-sample constant (ADR-009).
namespace k {
inline constexpr double kMaxDelayS = 12.0;
inline constexpr double kModMarginS = 0.020;   // the modulation ceiling the line also holds (§3.4)
inline constexpr int kKernelMargin = 8;        // samples beyond the margin, for the 4-point read (§11)
inline constexpr double kMinDelayS = 0.001;
inline constexpr double kKnee = 0.9, kCeil = 1.0;  // the ceiling (§5.1)
inline constexpr double kDcHz = 5.0;
inline constexpr double kRampS = 0.020;        // gains, mix, feedback, switches, load fade-out
inline constexpr double kJumpS = 0.003;        // jump = a 3 ms equal-gain crossfade (§3.7)
inline constexpr double kCutoffTauS = 0.010;   // cutoff glide, log-frequency (§6)
inline constexpr double kGateFloor = 1e-6;     // -120 dBFS (§10)
inline constexpr double kGateDb = -120.0;
inline constexpr double kReturnHeadroomDb = 6.03;  // the DC blocker's L1 gain < 2 (§10 "why that tail")
inline constexpr double kTailMarginS = 0.25;
inline constexpr double kMinCutHz = 10.0, kMaxCutFrac = 0.45;
inline constexpr double kPi = 3.14159265358979323846;
}  // namespace k

class Echo {
 public:
  Echo() { initFilters(); }

  // ---- lifecycle ------------------------------------------------------------
  // The only allocating call (RS-7.7): both lines, for 12 s at THIS rate plus
  // the modulation margin and the kernel's margin (§11). Must precede process().
  void prepare(double sampleRate, int maxBlock) {
    // 8 kHz .. 768 kHz: below 8 kHz the 1 ms floor is under the 3-sample read
    // gap and the cutoff clamp's ceiling (0.45 fs) falls under its 10 Hz floor.
    sr_ = std::isfinite(sampleRate) ? std::clamp(sampleRate, 8000.0, 768000.0) : 48000.0;
    maxBlock_ = maxBlock > 0 ? maxBlock : 1;
    lineLen_ = static_cast<int>(std::ceil((k::kMaxDelayS + k::kModMarginS) * sr_)) + k::kKernelMargin;
    for (auto& l : line_) l.assign(static_cast<std::size_t>(lineLen_), 0.0f);
    rampN_ = plant::rampFixedSamples ? 960 : std::max(1, static_cast<int>(std::lround(k::kRampS * sr_)));
    dcR_ = std::exp(-2.0 * k::kPi * k::kDcHz / (plant::dcFixedRate ? 48000.0 : sr_));
    glideA_ = 1.0 - std::exp(-1.0 / (k::kCutoffTauS * sr_));
    prepared_ = true;
    reset();
  }

  // Bit-identical to a freshly prepared instance (contract row A2). A fresh
  // instance is AT REST, so its first block lands on the host's tempo (§3.3).
  void reset() {
    if (!prepared_) return;
    w_ = 0;  // before clearLoop(): the logical clear starts at the write index
    if (!plant::resetKeepsLine) clearLoop();
    bpm_ = bpmPrev_ = 120.0;
    loadLeft_ = 0;
    fadeCount_[0] = fadeCount_[1] = 0;
    snapAll(true);
    resolveBoth(bpm_);
    for (int k = 0; k < 2; ++k) landHead(k);
    idle_ = true;
    restDirty_ = false;
    silentRun_ = quietRun_ = tailClock_ = 0;
    nonFinite_.store(0, std::memory_order_relaxed);
    nonFiniteLatch_.store(false, std::memory_order_relaxed);
  }

  // ---- parameters -----------------------------------------------------------
  // Natural units (dB, %, ms, Hz, s, or a stepped index), as in params.json.
  // Refuses an unknown key, a non-finite value, an out-of-range value and a
  // non-integer stepped value: the slot validates at load, so a refusal here is
  // a host bug made visible rather than a silent clamp. Allocation-free.
  bool setParam(std::string_view key, double v) {
    const auto i = paramIndex(key);
    return i ? setParam(*i, v) : false;
  }
  std::optional<int> resolve(std::string_view key) const { return paramIndex(key); }
  bool setParam(int handle, double v) {
    if (plant::rangeClamps && handle >= 0 && handle < kNumParams && std::isfinite(v))
      v = std::clamp(v, kParams[static_cast<std::size_t>(handle)].min, kParams[static_cast<std::size_t>(handle)].max);
    if (!accept(handle, v)) return false;
    p_[handle] = v;
    applied(handle);
    restDirty_ = true;
    return true;
  }
  double getParam(int handle) const { return (handle >= 0 && handle < kNumParams) ? p_[handle] : 0.0; }

  // A preset or state load (RS-6.7; SPEC §10, ruled D-016, D-020). At this
  // boundary every patch value lands with no ramp-in (getParam reads the new
  // values, the dry level follows the new mix at once). The OLD repeats fade
  // out over 20 ms under their OLD routing, filters, feedback and wet level,
  // then the lines and loop state are cleared and the heads land on the new
  // times, never a glide (ADR-142). Device keys are not part of a patch and are
  // left alone, ramps included (RS-4.5). A second load during the fade lands
  // its values too but never restarts the fade upward. Spill-over of the old
  // tail is horde's (a second instance, RS-6.5). An instance at rest has
  // nothing sounding: it adopts the patch at once. Returns false if any entry
  // was refused; the accepted ones still land.
  bool loadState(std::span<const KeyValue> values) {
    bool ok = true;
    for (const auto& kv : values) {
      const auto i = paramIndex(kv.key);
      if (!i || !accept(*i, kv.value)) { ok = false; continue; }
      if (kParams[static_cast<std::size_t>(*i)].morph == Morph::Device && !plant::loadWritesDevice) continue;
      p_[static_cast<std::size_t>(*i)] = kv.value;
    }
    if (idle_) {
      adoptPatch();
      restDirty_ = true;  // the resting instance lands everything, Device ramps too
      return ok;
    }
    if (plant::loadClearsInstantly) {  // no fade: the old audio is cut at the boundary
      adoptPatch();
      clearLoop();
      for (int k = 0; k < 2; ++k) landHead(k);
      return ok;
    }
    if (plant::loadNewSettingsOnOld) {  // a fade, but under the NEW settings
      adoptPatch();
      fadeWg_ = rMix_.cur <= 0.0 ? 0.0 : std::sin(rMix_.cur * k::kPi / 2);
      loadLeft_ = rampN_;
      return ok;
    }
    rMix_.target = p_[kMix] / 100.0;  // the dry level takes the new mix at the boundary
    if (loadLeft_ == 0) {
      fadeWg_ = rMix_.cur <= 0.0 ? 0.0 : std::sin(rMix_.cur * k::kPi / 2);  // the old wet level, frozen
      loadLeft_ = rampN_;
    } else if (plant::loadRestartsFade) {
      loadLeft_ = rampN_;
    }
    rMix_.snap();
    return ok;
  }

  // ---- process --------------------------------------------------------------
  // bpm: the host tempo for THIS block; <= 1 falls back to 120. Within the block
  // it is interpolated per sample from the previous block's value (§3.3).
  void process(const double* inL, const double* inR, double* outL, double* outR, int n, double bpm) {
    if (!prepared_ || n <= 0) {
      for (int i = 0; i < n; ++i) outL[i] = outR[i] = 0.0;
      return;
    }
    const FtzScope ftz;
    if (plant::allocInProcess) {
      std::vector<double> scratch(8, inL[0]);  // the planted audio-thread allocation
      outL[0] = scratch[0] * 0.0;
    }
    const double bpmEnd = (std::isfinite(bpm) && bpm > 1.0) ? bpm : 120.0;
    if (idle_ && !plant::tempoGlidesAtRest) bpmPrev_ = bpmEnd;  // at rest the tempo lands, never glides (§3.3)
    double bpmStart = bpmPrev_;  // re-landed if the instance rests and wakes inside this block
    const bool sync = p_[kSync] >= 0.5;

    for (int i = 0; i < n; ++i) {
      // Input guard (RS-7.9): non-finite input becomes 0, each repair counted.
      double xL = inL[i], xR = inR[i];
      if (!std::isfinite(xL)) { xL = 0.0; countRepair(); }
      if (!std::isfinite(xR)) { xR = 0.0; countRepair(); }

      if (idle_) {
        atRest(bpmEnd);
        if (!plant::tempoGlidesAtRest) bpmStart = bpmEnd;
        const bool wake = plant::wakeLeftOnly ? (xL != 0.0) : (xL != 0.0 || xR != 0.0);
        if (!wake) { outL[i] = outR[i] = 0.0; continue; }
        idle_ = false;
        silentRun_ = quietRun_ = tailClock_ = 0;
      }

      // RAMP_PER_BLOCK plant: ramps advance once per call (an ADR-175 violation).
      if (!plant::rampPerBlock || i == 0) tickRamps();
      const double gIn = plant::dbRamp ? std::pow(10.0, rIn_.cur / 20.0) : rIn_.cur;
      const double gOut = plant::dbRamp ? std::pow(10.0, rOut_.cur / 20.0) : rOut_.cur;
      const double mix = rMix_.cur, g = rFb_.cur;
      const bool loading = loadLeft_ > 0;  // the old audio is fading out (D-020)
      tickFilters();
      tickRoute();

      xL *= gIn;
      xR *= gIn;
      const bool silentIn = xL == 0.0 && xR == 0.0;
      silentRun_ = silentIn ? silentRun_ + 1 : 0;
      // The tail clock (D-019): counts silent samples since the input stopped,
      // the loop last held (g >= 1) or the declared tail last changed, so a
      // full declared tail is always allowed after the last change.
      if (plant::tailClockFromSilence) tailClock_ = silentRun_;
      else tailClock_ = (silentIn && g < 1.0 && fbTarget() < 1.0) ? tailClock_ + 1 : 0;

      // Encode into the lines (§4), then the input filter on what enters them.
      double u[2];
      for (int k = 0; k < 2; ++k) u[k] = enc_[k][0] * xL + enc_[k][1] * xR;
      for (int k = 0; k < 2; ++k) u[k] = filterSample(fIn_, k, u[k]);

      // Time targets: the tempo moves per sample within the block (D-008).
      const double b = plant::tempoIgnored ? 120.0 : bpmStart + (bpmEnd - bpmStart) * (i + 1) / n;
      const bool tempoMoved = sync && b != bpm_;
      bpm_ = b;
      if (tempoMoved) resolveBoth(b);

      // A load fades the old audio out over 20 ms, then ends it (§10, D-020).
      double lg = 1.0;
      bool loadEnds = false;
      if (loading) {
        lg = static_cast<double>(--loadLeft_) / rampN_;
        loadEnds = loadLeft_ == 0;
      }

      // Read each line (a crossfade sounds both heads, equal-gain), then the
      // loop filter: the wet already carries it once (§2).
      double y[2];
      for (int k = 0; k < 2; ++k) {
        if (!loading) stepHead(k);
        y[k] = filterSample(fLoop_, k, readHead(k)) * lg;
      }

      // Feedback (§5.1): routing, gain, the ceiling on the RETURNING term only,
      // then the gain-normalised DC blocker, added to what the input wrote.
      bool loud = false, bad = false;
      float* L0 = line_[0].data();
      float* L1 = line_[1].data();
      for (int k = 0; k < 2; ++k) {
        const double f = fb_[k][0] * y[0] + fb_[k][1] * y[1];
        const double ret = dcBlock(k, plant::ceilingBeforeGain ? g * ceiling(f) : ceiling(g * f));
        double wv = u[k] + ret;
        if (plant::antiDenormalDc) wv += 1e-20;
        float wf = static_cast<float>(wv);
        if (!std::isfinite(wf)) { wf = 0.0f; bad = true; }
        if (std::fabs(wf) >= k::kGateFloor) loud = true;
        (k ? L1 : L0)[w_] = wf;
      }
      // Non-finite inside the loop (RS-7.9, A9): clear ALL loop state — lines,
      // filters, DC — and count it. Zeroing one sample would leave the poison in
      // a filter's state, re-poisoning every sample after.
      if (bad || !std::isfinite(y[0] + y[1])) {
        if (!plant::loopRepairPartial) clearLoop();
        y[0] = y[1] = 0.0;
        countRepair();
      }
      w_ = (w_ + 1 == lineLen_) ? 0 : w_ + 1;
      if (sinceClear_ < lineLen_) ++sinceClear_;

      // Decode, output filter, mix (equal-power, exact endpoints), output gain.
      double wo[2];
      for (int c = 0; c < 2; ++c) wo[c] = dec_[c][0] * y[0] + dec_[c][1] * y[1];
      for (int c = 0; c < 2; ++c) wo[c] = filterSample(fOut_, c, wo[c]);
      // Quiet = the line writes AND the wet (after the loop and output filters)
      // below -120 dBFS. SPEC §10 states the line writes only, but the filters
      // still ring after the last loud write leaves the line: resting on the
      // writes alone cut a 20 Hz low cut's tail at 0.0285, an audible click
      // (measured; the gateLineOnly plant keeps it caught). D-018. Stricter than the spec's condition A, so rest only
      // comes later, never after the declared tail (condition B bounds it).
      if (!plant::gateLineOnly)
        for (double v : {y[0], y[1], wo[0], wo[1]})
          if (std::fabs(v) >= k::kGateFloor) loud = true;
      quietRun_ = loud ? 0 : quietRun_ + 1;
      const double dg = (mix >= 1.0 && !plant::mixLeak) ? 0.0 : std::cos(mix * k::kPi / 2);
      // While a load fades, the old wet keeps the OLD mix's wet level (D-020).
      const double wg = loading ? fadeWg_ : (mix <= 0.0 ? 0.0 : std::sin(mix * k::kPi / 2));
      const double dL = plant::dryLimiter ? std::tanh(xL) : xL;
      const double dR = plant::dryLimiter ? std::tanh(xR) : xR;
      double oL = (dg * dL + wg * wo[0]) * gOut;
      double oR = (dg * dR + wg * wo[1]) * gOut;
      if (!plant::noOutputGuard && (!std::isfinite(oL) || !std::isfinite(oR))) {
        oL = oR = 0.0;
        clearLoop();
        countRepair();
      }
      outL[i] = oL;
      outR[i] = oR;
      if (loadEnds) {
        adoptPatch();  // the old audio has faded: the new patch takes the wet too
        if (!plant::loadKeepsLine) clearLoop();
        for (int k = 0; k < 2; ++k) landHead(k);
      }

      // The tail gate (§10, D-018, D-019): input silent and every write AND the
      // wet below -120 dBFS for a whole read span, OR the declared tail elapsed
      // on the tail clock. Either way the state is cleared, so the output is
      // exact zero from then on BY CONSTRUCTION. A held loop (g >= 1) never rests.
      if (!plant::noTailGate && !loading && g < 1.0 && fbTarget() < 1.0) {
        const std::int64_t span = readSpan();
        if ((silentRun_ >= span && quietRun_ >= span) || (tailN_ != kInfiniteTail && tailClock_ >= tailN_)) {
          clearLoop();
          idle_ = true;
          restDirty_ = true;  // land everything once, on the next resting sample
        }
      }
    }
    bpmPrev_ = bpmEnd;
  }

  // ---- declarations (SPEC §10) ----------------------------------------------
  int latency() const { return 0; }
  int minBlock() const { return 1; }
  int channels() const { return 2; }
  bool tailMayChange() const { return true; }
  // Samples until the instance is at rest once the input stops (§10, §12.1's
  // reading): (n+2)·T_max + 0.25 s for g < 1, kInfiniteTail for g >= 1.
  std::int64_t tail() const { return tailN_; }

  // ---- reads for the slot, the screen and the tests -------------------------
  bool atRest() const { return idle_; }
  double sampleRate() const { return sr_; }
  int lineLength() const { return lineLen_; }
  // The resolved per-line times in seconds after offsets and the §3.4 clamp.
  std::array<double, 2> resolvedTimes() const { return {tT_[0] / sr_, tT_[1] / sr_}; }
  bool folded() const { return folded_; }
  // Non-finite repairs (RS-7.9): counted and latched, readable from any thread.
  std::uint64_t nonFiniteCount() const { return nonFinite_.load(std::memory_order_relaxed); }
  bool nonFiniteLatched() const { return nonFiniteLatch_.load(std::memory_order_relaxed); }
  void clearLatches() { nonFiniteLatch_.store(false, std::memory_order_relaxed); }
  // How many time crossfades line k has started (test read: the retrigger queue).
  std::uint64_t fadeCount(int k) const { return fadeCount_[k]; }
  // The sounding head's delay in samples when no fade runs (test read).
  double headDelay(int k) const { return head_[k].D; }
  // The largest magnitude held in either line (test read, not real-time:
  // it scans 2 x 12 s). The ceiling's bound is stated on the line (§5.1).
  double linePeak() const {
    float m = 0.0f;
    for (int k = 0; k < 2; ++k)
      for (int i = 0; i < lineLen_; ++i) m = std::max(m, std::fabs(tap(line_[k].data(), i)));
    return m;
  }
  int maxBlock() const { return maxBlock_; }

 private:
  // ---- smoothing ------------------------------------------------------------
  // A linear ramp of a fixed length: per-sample, so block-size independent.
  struct Ramp {
    double cur = 0, target = 0, step = 0;
    int left = 0;
    void to(double t, int len) {
      target = t;
      left = len;
      step = (t - cur) / len;
      if (left <= 0) snap();
    }
    void tick() {
      if (left > 0) {
        cur += step;
        if (--left == 0) cur = target;
      }
    }
    void snap() { cur = target; left = 0; step = 0; }
  };

  // ---- filters (§6) ---------------------------------------------------------
  // One position: a low cut and a high cut behind one switch. kind 0 is the
  // 12 dB/oct Butterworth TPT state-variable pair (input, output); kind 1 is
  // the 6 dB/oct one-pole TPT pair (loop). No resonance anywhere, so |H| <= 1.
  struct FilterPos {
    int kind = 0;
    int onKey = 0, hpKey = 0, lpKey = 0;
    double w = 0, wT = 0, wStep = 0;   // the switch blend, 0 = bypassed
    double hl = 0, ll = 0;             // log2 cutoffs, gliding
    bool settled = false;
    double gH = 0, gL = 0;             // tan(pi fc / fs)
    double s[2][4] = {};               // per channel: hp s1,s2 then lp s1,s2 (one-pole uses [0],[2])
    void clear() {
      for (auto& c : s)
        for (auto& v : c) v = 0.0;
    }
  };

  void initFilters() {
    fIn_.kind = 0; fIn_.onKey = kInOn; fIn_.hpKey = kInHp; fIn_.lpKey = kInLp;
    fLoop_.kind = 1; fLoop_.onKey = kLoopOn; fLoop_.hpKey = kLoopHp; fLoop_.lpKey = kLoopLp;
    fOut_.kind = 0; fOut_.onKey = kOutOn; fOut_.hpKey = kOutHp; fOut_.lpKey = kOutLp;
  }

  double cutoffG(double log2f) const {
    const double rate = plant::fixedCoefRate ? 48000.0 : sr_;
    const double fc = std::clamp(std::exp2(log2f), k::kMinCutHz, k::kMaxCutFrac * sr_);
    return std::tan(k::kPi * fc / rate);
  }

  void tickFilter(FilterPos& f) {
    if (f.w != f.wT) {
      f.w += f.wStep;
      if ((f.wStep > 0 && f.w >= f.wT) || (f.wStep < 0 && f.w <= f.wT) || f.wStep == 0) {
        f.w = f.wT;
        if (f.w == 0.0) f.clear();
      }
    }
    if (f.w == 0.0 && f.wT == 0.0) return;  // off and settled: not computed (true bypass)
    if (!f.settled) {
      const double hT = std::log2(p_[f.hpKey]), lT = std::log2(p_[f.lpKey]);
      f.hl += (hT - f.hl) * glideA_;
      f.ll += (lT - f.ll) * glideA_;
      if (std::fabs(hT - f.hl) < 1e-9) f.hl = hT;
      if (std::fabs(lT - f.ll) < 1e-9) f.ll = lT;
      f.gH = cutoffG(f.hl);
      f.gL = cutoffG(f.ll);
      f.settled = (f.hl == hT && f.ll == lT);
    }
  }
  void tickFilters() {
    tickFilter(fIn_);
    tickFilter(fLoop_);
    tickFilter(fOut_);
  }

  double filterSample(FilterPos& f, int c, double x) {
    if (f.w == 0.0 && f.wT == 0.0) {
      // True bypass (ADR-142): an "off" filter is not in the circuit at all.
      if (!(plant::loopFilterInCircuit && &f == &fLoop_)) return x;
      return onePolePair(f, c, x);  // the plant: "off" but still filtering
    }
    const double v = f.kind == 0 ? svfPair(f, c, x) : onePolePair(f, c, x);
    const double out = f.w == 1.0 ? v : (1.0 - f.w) * x + f.w * v;
    return (plant::loopGainOverOne && &f == &fLoop_) ? out * 1.1220184543019633 : out;
  }

  // One-pole TPT: low cut = x - lp(x) at gH, then lp at gL.
  static double onePolePair(FilterPos& f, int c, double x) {
    double* s = f.s[c];
    const double GH = f.gH / (1.0 + f.gH);
    double z = (x - s[0]) * GH;
    double lp = z + s[0];
    s[0] = lp + z;
    const double hp = x - lp;
    const double GL = f.gL / (1.0 + f.gL);
    z = (hp - s[2]) * GL;
    lp = z + s[2];
    s[2] = lp + z;
    return lp;
  }

  // Butterworth (Q = 1/sqrt2) TPT state-variable filter (Zavalishin): high-pass
  // at gH, then low-pass at gL.
  static double svfPair(FilterPos& f, int c, double x) {
    constexpr double kR = 1.4142135623730951;  // 1/Q
    double* s = f.s[c];
    double g = f.gH;
    double h = 1.0 / (1.0 + kR * g + g * g);
    double hp = (x - (kR + g) * s[0] - s[1]) * h;
    double bp = g * hp + s[0];
    s[0] = g * hp + bp;
    double lp = g * bp + s[1];
    s[1] = g * bp + lp;
    const double v = hp;
    g = f.gL;
    h = 1.0 / (1.0 + kR * g + g * g);
    hp = (v - (kR + g) * s[2] - s[3]) * h;
    bp = g * hp + s[2];
    s[2] = g * hp + bp;
    lp = g * bp + s[3];
    s[3] = g * bp + lp;
    return lp;
  }

  void switchFilter(FilterPos& f, double on) {
    if (on != 0.0 && f.w == 0.0) {
      // Switched on from off: start from cleared state, cutoffs at their targets.
      f.clear();
      f.hl = std::log2(p_[f.hpKey]);
      f.ll = std::log2(p_[f.lpKey]);
      f.settled = false;
    }
    f.wT = on;
    if (plant::instantSwitch) {
      f.w = on;
      f.wStep = 0;
      if (on == 0.0) f.clear();
    } else {
      f.wStep = (on - f.w) / rampN_;
    }
  }

  // ---- routing (§4) ---------------------------------------------------------
  // Encode (line from input), feedback (line from wet line), decode (output
  // from wet line). A switch blends the CURRENT effective matrices into the new
  // mode's over 20 ms, equal-gain; a switch arriving mid-switch starts from
  // wherever the blend is, so nothing steps.
  using M2 = std::array<std::array<double, 2>, 2>;
  struct Routing { M2 enc, fb, dec; };
  Routing routingFor(int mode) const {
    switch (mode) {
      case 1:  // ping-pong: mono sum into line 0, crossed feedback
        return {{{{0.5, 0.5}, {0.0, 0.0}}},
                plant::pingpongNoCross ? M2{{{1.0, 0.0}, {0.0, 1.0}}} : M2{{{0.0, 1.0}, {1.0, 0.0}}},
                {{{1.0, 0.0}, {0.0, 1.0}}}};
      case 2:  // mid/side: M = (L+R)/2, S = (L-R)/2; out L = M + S, R = M - S
        return {{{{0.5, 0.5}, plant::msSideGain2 ? std::array<double, 2>{1.0, -1.0} : std::array<double, 2>{0.5, -0.5}}},
                {{{1.0, 0.0}, {0.0, 1.0}}},
                {{{1.0, 1.0}, plant::msSign ? std::array<double, 2>{1.0, 1.0} : std::array<double, 2>{1.0, -1.0}}}};
      default:  // stereo
        return {{{{1.0, 0.0}, {0.0, 1.0}}}, {{{1.0, 0.0}, {0.0, 1.0}}}, {{{1.0, 0.0}, {0.0, 1.0}}}};
    }
  }
  void setRoutingNow(int mode) {
    const Routing r = routingFor(mode);
    routeFrom_ = routeTo_ = r;
    routeW_ = 1.0;
    enc_ = r.enc; fb_ = r.fb; dec_ = r.dec;
  }
  void switchRouting(int mode) {
    routeFrom_ = {enc_, fb_, dec_};
    routeTo_ = routingFor(mode);
    routeW_ = plant::instantSwitch ? 1.0 : 0.0;
    if (routeW_ == 1.0) { enc_ = routeTo_.enc; fb_ = routeTo_.fb; dec_ = routeTo_.dec; }
  }
  void tickRoute() {
    if (routeW_ >= 1.0) return;
    routeW_ = std::min(1.0, routeW_ + 1.0 / rampN_);
    const double a = routeW_, b = 1.0 - routeW_;
    for (int r = 0; r < 2; ++r)
      for (int c = 0; c < 2; ++c) {
        enc_[r][c] = b * routeFrom_.enc[r][c] + a * routeTo_.enc[r][c];
        fb_[r][c] = b * routeFrom_.fb[r][c] + a * routeTo_.fb[r][c];
        dec_[r][c] = b * routeFrom_.dec[r][c] + a * routeTo_.dec[r][c];
      }
    if (routeW_ >= 1.0) { enc_ = routeTo_.enc; fb_ = routeTo_.fb; dec_ = routeTo_.dec; routeFrom_ = routeTo_; }
  }

  // ---- the loop's nonlinear and DC elements (§5.1) ---------------------------
  static double ceiling(double x) {
    if (plant::noCeiling) return x;
    if (plant::ceilingHardClip) return std::clamp(x, -1.0, 1.0);
    const double a = std::fabs(x);
    if (a <= k::kKnee) return x;
    const double sh = k::kKnee + (k::kCeil - k::kKnee) * std::tanh((a - k::kKnee) / (k::kCeil - k::kKnee));
    return x < 0 ? -sh : sh;
  }
  // Gain-normalised: H(z) = ((1+R)/2)(1 - z^-1)/(1 - R z^-1), |H| <= 1 to
  // Nyquist. The textbook form without (1+R)/2 peaks at 2/(1+R) > 1 there.
  double dcBlock(int k, double x) {
#ifdef ECHO_PLANTS
    static double shared[4] = {};  // SHARED_STATE plant only (the B230 fault); absent from production
    double* st = plant::sharedState ? &shared[2 * k] : &dc_[2 * k];
#else
    double* st = &dc_[2 * k];
#endif
    const double gn = plant::dcTextbook ? 1.0 : 0.5 * (1.0 + dcR_);
    const double y = gn * (x - st[0]) + dcR_ * st[1];
    st[0] = x;
    st[1] = y;
    return y;
  }

  // ---- time (§3) ------------------------------------------------------------
  // Base time, x (1 + offset), synced times halved together past 12 s (the L/R
  // ratio and the grid survive), free times clamped, 1 ms floor. Also refreshes
  // the declared tail, which depends on the times, the feedback and the depth.
  void resolveBoth(double bpm) {
    const bool sync = p_[kSync] >= 0.5;
    const double b = (bpm > 1.0) ? bpm : 120.0;
    const int div = static_cast<int>(p_[kDiv]);
    const double base = sync ? kDivisions[static_cast<std::size_t>(div)].beats * 60.0 / b : p_[kTimeMs] / 1000.0;
    double tL = base * (1.0 + p_[kOffL] / 100.0), tR = base * (1.0 + p_[kOffR] / 100.0);
    folded_ = false;
    if (sync && !plant::clampTruncates)
      while (std::max(tL, tR) > k::kMaxDelayS) { tL *= 0.5; tR *= 0.5; folded_ = true; }
    const double lo = plant::noMinFloor ? 0.0 : k::kMinDelayS;
    tT_[0] = std::clamp(tL, lo, k::kMaxDelayS) * sr_;
    tT_[1] = std::clamp(tR, lo, k::kMaxDelayS) * sr_;
    setTail(computeTail());
  }

  // (n + 2)·T_max + 0.25 s (§10, D-019): n passes take a write from the
  // returning term's bound (< 2, +6.03 dB rounded up) below -120 dBFS; T_max
  // includes the 4-sample kernel. Computed in double and saturated: a feedback
  // just under 100 % (99.99999999999999 from a normalised host's round trip)
  // gives n ~ 1e17 and a product past int64, which a plain cast would turn into
  // undefined behaviour and, on x86, a NEGATIVE tail (critic H3).
  std::int64_t computeTail() const {
    const double g = fbTarget();
    if (g >= 1.0) return kInfiniteTail;
    const double tMax = std::max(tT_[0], tT_[1]) + p_[kModDepth] / 1000.0 * sr_ + 4.0;
    double n = g <= 0.0 ? 0.0 : std::ceil((k::kGateDb - k::kReturnHeadroomDb) / (20.0 * std::log10(g)));
    if (plant::tailFormulaShort) n = std::floor(n / 7.0);
    const double v = std::ceil((n + 2.0) * tMax + k::kTailMarginS * sr_);
    if (plant::tailInt32)  // narrowed to 32 bits: C++20's conversion is modular, so defined
      return static_cast<std::int32_t>(static_cast<std::int64_t>(std::fmod(std::min(v, 4.0e18), 4294967296.0)) - 2147483648LL);
    if (!(v < 4.0e18)) return kInfiniteTail;
    return static_cast<std::int64_t>(v);
  }
  void setTail(std::int64_t t) {
    if (t != tailN_) { tailN_ = t; tailClock_ = 0; }
  }

  // A head: the sounding delay D, or a crossfade from dA to dB. Each move is
  // latched to the time.change in force when it starts (§3.5 deferral).
  struct Head {
    double D = 0;
    bool fade = false;
    double dA = 0, dB = 0;
    int fi = 0, fn = 1;
    int mode = 0;  // the latched time.change of the running fade
  };

  void landHead(int k) {
    Head& h = head_[k];
    h.D = h.dA = h.dB = tT_[k];
    h.fade = false;
    h.fi = 0;
    h.fn = 1;
  }

  int fadeLength(int mode) const {
    const double xf = plant::xfadeIgnored ? 0.030 : p_[kXfadeMs] / 1000.0;
    const double s = (mode == 2 && !plant::jumpUsesXfade) ? k::kJumpS : xf;  // repitch: crossfade (M1 placeholder)
    return std::max(1, static_cast<int>(std::lround(s * sr_)));
  }

  // Advance one sample. A fade runs to its end; then, if the target moved by
  // half a sample or more, the next fade starts toward the LATEST target
  // (queue, latest wins: intermediate targets are dropped, §3.5).
  void stepHead(int k) {
    Head& h = head_[k];
    if (h.fade && plant::retargetMidFade && std::fabs(tT_[k] - h.dB) >= 0.5) h.dB = tT_[k];
    if (h.fade) {
      if (plant::deferralBroken) h.fn = std::min(h.fn, fadeLength(static_cast<int>(p_[kChange])));
      if (++h.fi >= h.fn) { h.D = h.dB; h.fade = false; }
    }
    if (!h.fade && std::fabs(tT_[k] - h.D) >= 0.5) {
      h.mode = static_cast<int>(p_[kChange]);
      if (h.mode == 2 && plant::bareJump) { h.D = tT_[k]; return; }
      h.dA = h.D;
      h.dB = tT_[k];
      h.fi = 0;
      h.fn = fadeLength(h.mode);
      h.fade = true;
      ++fadeCount_[k];
    }
  }

  // The read (D-004): 4-point Catmull-Rom (cubic Hermite) at a fractional
  // delay. At a whole-sample delay it returns the stored sample EXACTLY (t = 0).
  // The read never sits closer than 3 samples to the write (1 ms floor).
  //
  // INDEX ARITHMETIC IS INTEGER (critic H1). The first version formed
  // p = w - D in double and wrapped by adding the line length: for a delay a
  // hair above a whole number (3087.0000000000005) p came out at -5e-13, and
  // p + N rounded to exactly N, reading one past the buffer: undefined
  // behaviour, harmless by luck on arm64, garbage on x86. Now D splits into its
  // whole part (an int) and its fraction, and only ints are wrapped.
  double readAt(int k, double D) const {
    if (plant::timeHalfSample) D += 0.5;
    const float* b = line_[k].data();
    const int N = lineLen_;
    int i;
    double t;
    if (plant::oldReadWrap) {
      // The pre-fix arithmetic, made memory-safe: the out-of-range index is
      // clamped to N - 1 (a wrong sample) instead of read past the end.
      double p = static_cast<double>(w_) - D;
      if (p < 0) p += N;
      const double fl = std::floor(p);
      t = p - fl;
      i = std::min(static_cast<int>(fl), N - 1);
    } else {
      const double di = std::floor(D);
      const double f = D - di;
      i = w_ - static_cast<int>(di);  // the sample written floor(D) samples ago
      t = 0.0;
      if (f > 0.0) { --i; t = 1.0 - f; }
      if (i < 0) i += N;              // D <= N - 8, so one wrap always suffices
    }
    const double x0 = tap(b, i == 0 ? N - 1 : i - 1);
    const double x1 = tap(b, i);
    const double x2 = tap(b, i + 1 >= N ? i + 1 - N : i + 1);
    const double x3 = tap(b, i + 2 >= N ? i + 2 - N : i + 2);
    const double c1 = 0.5 * (x2 - x0);
    const double c2 = x0 - 2.5 * x1 + 2.0 * x2 - 0.5 * x3;
    const double c3 = 0.5 * (x3 - x0) + 1.5 * (x1 - x2);
    return ((c3 * t + c2) * t + c1) * t + x1;
  }

  // A stored sample, or 0 if it predates the last clear (M3: the lines are
  // cleared LOGICALLY in O(1), never by a memset on the audio thread).
  float tap(const float* b, int idx) const {
    if (sinceClear_ < lineLen_) {
      int d = idx - clearW_;
      if (d < 0) d += lineLen_;
      if (d >= sinceClear_) return 0.0f;
    }
    return b[idx];
  }

  // Mid-fade both heads sound, equal-gain (D-007, RS-3.6): correlated copies
  // then never sum above the signal, so the loop bound holds through a fade.
  double readHead(int k) const {
    const Head& h = head_[k];
    if (!h.fade) return readAt(k, h.D);
    const double wF = static_cast<double>(h.fi + 1) / h.fn;
    if (plant::equalPower)
      return std::cos(wF * k::kPi / 2) * readAt(k, h.dA) + std::sin(wF * k::kPi / 2) * readAt(k, h.dB);
    return (1.0 - wF) * readAt(k, h.dA) + wF * readAt(k, h.dB);
  }

  std::int64_t readSpan() const {
    double m = 0;
    for (const auto& h : head_) m = std::max({m, h.D, h.fade ? std::max(h.dA, h.dB) : 0.0});
    if (m >= lineLen_) m = lineLen_;
    return static_cast<std::int64_t>(std::ceil(m)) + 4;
  }

  // ---- parameter plumbing ---------------------------------------------------
  bool accept(int handle, double v) const {
    if (handle < 0 || handle >= kNumParams || !std::isfinite(v)) return false;
    const ParamDef& d = kParams[static_cast<std::size_t>(handle)];
    if (v < d.min || v > d.max) return false;
    if (d.stepped && v != std::floor(v)) return false;
    return true;
  }

  double gainTarget(int key) const {
    // B435: the ramp is linear in AMPLITUDE (the DB_RAMP plant ramps the dB).
    return plant::dbRamp ? p_[key] : std::pow(10.0, p_[key] / 20.0);
  }
  double fbTarget() const { return p_[kFb] / 100.0; }

  void applied(int key) {
    switch (key) {
      case kInGain: rIn_.to(gainTarget(kInGain), rampN_); break;
      case kOutGain: rOut_.to(gainTarget(kOutGain), rampN_); break;
      case kMix: rMix_.to(p_[kMix] / 100.0, rampN_); break;
      case kFb: rFb_.to(fbTarget(), rampN_); setTail(computeTail()); break;
      case kSync: case kTimeMs: case kDiv: case kOffL: case kOffR: resolveBoth(bpm_); break;
      case kModDepth: setTail(computeTail()); break;
      case kRoute: switchRouting(static_cast<int>(p_[kRoute])); break;
      case kInOn: switchFilter(fIn_, p_[kInOn]); break;
      case kLoopOn: switchFilter(fLoop_, p_[kLoopOn]); break;
      case kOutOn: switchFilter(fOut_, p_[kOutOn]); break;
      case kInHp: case kInLp: fIn_.settled = false; break;
      case kLoopHp: case kLoopLp: fLoop_.settled = false; break;
      case kOutHp: case kOutLp: fOut_.settled = false; break;
      default: break;  // time.change / law / tempoMode: deferred (§3.5); mod.*: M2
    }
  }

  void tickRamps() {
    rIn_.tick(); rOut_.tick(); rMix_.tick(); rFb_.tick();
  }

  // Land every smoother and switch on its target: reset and rest (device =
  // true), and a load's end (device = false: Device ramps are never a patch's,
  // RS-4.5, critic H4).
  void snapAll(bool device) {
    if (device || plant::loadSnapsDevice) {
      rIn_.target = gainTarget(kInGain); rIn_.snap();
      rOut_.target = gainTarget(kOutGain); rOut_.snap();
    }
    rMix_.target = p_[kMix] / 100.0; rMix_.snap();
    rFb_.target = fbTarget(); rFb_.snap();
    for (FilterPos* f : {&fIn_, &fLoop_, &fOut_}) {
      f->w = f->wT = p_[f->onKey];
      f->wStep = 0;
      f->hl = std::log2(p_[f->hpKey]);
      f->ll = std::log2(p_[f->lpKey]);
      f->gH = cutoffG(f->hl);
      f->gL = cutoffG(f->ll);
      f->settled = true;
      if (f->w == 0.0) f->clear();
    }
    setRoutingNow(static_cast<int>(p_[kRoute]));
  }

  // The new patch takes everything: at a load's end, or at once when resting.
  void adoptPatch() {
    snapAll(false);
    resolveBoth(bpm_);
  }

  // One resting sample. Everything lands ONCE per change (critic M4: snapping
  // every sample made resting cost more CPU than running): on entering rest and
  // after any setParam / load while resting. The tempo and heads land too.
  void atRest(double bpmEnd) {
    if (plant::restRamps) { tickRamps(); tickFilters(); tickRoute(); }
    else if (restDirty_) { snapAll(true); restDirty_ = false; }
    if (!plant::tempoGlidesAtRest) {
      const bool moved = bpm_ != bpmEnd;
      bpm_ = bpmPrev_ = bpmEnd;
      if (moved) resolveBoth(bpmEnd);
    }
    for (int k = 0; k < 2; ++k)
      if (head_[k].D != tT_[k] || head_[k].fade) landHead(k);
    loadLeft_ = 0;
  }

  // O(1): the lines are cleared LOGICALLY (tap() reads anything written before
  // this point as 0), so a rest, a load's end or a burst of repairs never
  // memsets 2 x 12 s on the audio thread (critic M3: 17.6 MB at 192 kHz cost
  // 0.36 ms in one 64-sample block; repeated repairs cost 0.29 ms a sample).
  void clearLoop() {
    clearW_ = w_;
    sinceClear_ = 0;
    fIn_.clear();
    fLoop_.clear();
    fOut_.clear();
    for (double& d : dc_) d = 0.0;
  }

  void countRepair() {
    if (plant::guardUncounted) return;
    nonFinite_.fetch_add(1, std::memory_order_relaxed);
    nonFiniteLatch_.store(true, std::memory_order_relaxed);
  }

  // ---- state ----------------------------------------------------------------
  static std::array<double, kNumParams> defaults() {
    std::array<double, kNumParams> d{};
    for (std::size_t i = 0; i < kParams.size(); ++i) d[i] = kParams[i].def;
    return d;
  }

  bool prepared_ = false;
  double sr_ = 48000.0;
  int maxBlock_ = 1;
  int lineLen_ = 0;
  int rampN_ = 960;
  double dcR_ = 0.0, glideA_ = 0.0;
  std::array<double, kNumParams> p_ = defaults();
  std::array<std::vector<float>, 2> line_;
  int w_ = 0;
  Ramp rIn_, rOut_, rMix_, rFb_;
  FilterPos fIn_, fLoop_, fOut_;
  Routing routeFrom_{}, routeTo_{};
  double routeW_ = 1.0;
  M2 enc_{}, fb_{}, dec_{};
  double dc_[4] = {};
  double tT_[2] = {0, 0};
  bool folded_ = false;
  std::int64_t tailN_ = 0;
  Head head_[2];
  double bpm_ = 120.0, bpmPrev_ = 120.0;
  int loadLeft_ = 0;
  double fadeWg_ = 0.0;          // the old wet level, frozen while a load fades (D-020)
  bool idle_ = true;
  bool restDirty_ = true;        // a change arrived while resting: land it once
  std::int64_t silentRun_ = 0, quietRun_ = 0;
  std::int64_t tailClock_ = 0;   // D-019
  int clearW_ = 0;               // the write index at the last logical clear
  int sinceClear_ = 0;           // samples written since it, saturating at lineLen_
  std::uint64_t fadeCount_[2] = {0, 0};
  std::atomic<std::uint64_t> nonFinite_{0};
  std::atomic<bool> nonFiniteLatch_{false};
};

}  // namespace echo

#if !defined(ECHO_FP_PRAGMA_OFF)
#if defined(__clang__)
#pragma STDC FP_CONTRACT DEFAULT
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif
#endif
