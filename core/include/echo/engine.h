// echo/engine.h — Echo's stereo delay core (M1 + M2), conforming to FOUNDATIONS'
// FX-operator ABI where the vendored header declares it, a local shim elsewhere.
//
// THE ABI (M3 T-013). Echo models FOUNDATIONS' FX-operator concepts from the
// vendored, UNFROZEN header (core/third_party/foundations, D-036): the lifecycle
// prepare(double, int) / reset() and the parameter entry resolve(address) ->
// handle, setParam(handle, v), both static_assert'ed at the end of this file;
// properties() reports an OperatorProperties that passes validate() once
// prepared. What the header does NOT declare yet stays a DEGRADED, TEMPORARY
// SHIM (INTEGRATIONS rule 2), in the shape horde already admits from Bulwark's
// (contract §1.1), each a rename at the call site once FOUNDATIONS rules it:
//   - process(inL, inR, outL, outR, int n, double bpm): the signature is open
//     there (Echo's brief echo-002, question 1). Tempo is data, never a clock.
//   - the state entry: saveState / stageState / adoptStaged (T-015, below) and
//     the older loadState(span<KeyValue>) it lands through; the arguments are
//     open there (echo-002, question 2; horde RS-6.9).
//   - the action entry: resolveAction / fireAction (T-037, below), horde's
//     per-block context (contract §12.6) carried by this shim until FOUNDATIONS
//     rules an entry for it.
//   - latency()/tail()/minBlock()/channels(): the pre-ABI declarations, kept
//     for the tests; properties() is the ABI's view of the same values.
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
// M2 (ROADMAP M2): moving time per §3.5-3.8. A control move follows
// time.change: repitch by time.law (tape: a varispeed tape model whose write
// position is recorded at control rate; exponential: a one-pole on ln D), held
// to the 12 st cap, the hard read-speed invariant s in [0.25, 4] (a counted
// backstop) and the 3 s crossfade fallback; crossfade; jump (3 ms). A move the
// host tempo alone causes follows time.tempoMode: crossfade, glide (the repitch
// law), or follow (exact while the bend stays within 50 cents, a crossfade past
// it). Line modulation per §7: sine or seeded drift (two mulberry32 streams from
// mod.seed), depth limited by the curve's slope and the write head, the offset
// slew-limited to 0.25 sample/sample, 250 ms ramps, seeded switches.
//
// M3 (T-014): two meter taps (§16.1, B435), directly after Input Gain and after
// Output Gain, each the block's peak and a clip latch, published as relaxed
// atomics once per block.
//
// M3 (T-015): the saved state as text by name (§16.2, echo/state.h), staged off
// the audio thread and adopted at a block boundary (RS-6.6).
//
// M3 (T-037): one declared action, Clear (§16.7, RS-4.12): fired at a sample
// offset of the next block, it fades the repeats out over 20 ms and ends them,
// landing no value (the load's fade, D-020, with nothing to adopt).
//
// THREADING. One writer: every method is called from one thread at a time
// (horde's slot serialises them, RS-6.6), with two exceptions. stageState may run
// on another thread BESIDE process() (it touches only the staging slot, handed
// over by an atomic flag; T-015), and clearMeters() may be called from the GUI.
// The non-finite counters (RS-7.9) and the meter taps (§16.1) are atomics,
// readable from any other thread: the GUI reads published values, never working
// buffers (contract §8, RC-8). saveState stays serialised with process().
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "echo/ftz.h"
#include "echo/module_version.h"
#include "echo/params.h"
#include "echo/plants.h"
#include "echo/state.h"
#include "foundations/fx_operator.h"

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

// The engine's own "never ends" (RS-7.3). The engine counts its tail in 64 bits;
// tailDecl32 below maps it onto FOUNDATIONS' 32-bit TailDecl. TWO NAMES MEET in
// this header: echo::kInfiniteTail (int64 max, what tail() returns) and
// foundations::kInfiniteTail (uint32 max, what properties().tail holds). Inside
// namespace echo an unqualified kInfiniteTail is THIS one, so comparing a
// TailDecl against it is silently always false: qualify foundations:: there.
inline constexpr std::int64_t kInfiniteTail = std::numeric_limits<std::int64_t>::max();

// SPEC §16.5 (D-035): FOUNDATIONS' TailDecl counts samples in 32 bits. A tail at
// or past 2^32 - 1 samples (6.2 h at 192 kHz, reachable at feedback just under
// unity) declares foundations::kInfiniteTail: saturating never under-declares,
// and the gate still brings Echo to rest at exact zero. A plain narrowing would
// WRAP, and a host would plan for a short tail and cut a ringing loop (the
// planted tailWraps32). A negative count is a bug upstream; it saturates too,
// the conservative reading. Not constexpr: the plant is a runtime flag in
// echo_plants.
inline std::uint32_t tailDecl32(std::int64_t t) {
  if (plant::tailWraps32) return static_cast<std::uint32_t>(t);
  if (t < 0 || t >= static_cast<std::int64_t>(foundations::kInfiniteTail)) return foundations::kInfiniteTail;
  return static_cast<std::uint32_t>(t);
}

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
// M2: moving time and modulation (SPEC §3.6-3.8, §7, §13)
inline constexpr double kModRampS = 0.25;      // depth, spread and shape changes
inline constexpr double kModRateGlideS = 0.1;  // the rate glides in log-frequency
inline constexpr double kModSlopeMax = 0.25;   // |d offset / dt|, samples per sample
inline constexpr double kSMin = 0.25, kSMax = 4.0;  // the HARD read-speed invariant (D-006)
inline constexpr double kCapSt = 12.0;         // every repitch law's pitch cap, both ways
inline constexpr double kFallbackS = 3.0;      // longer than this at the cap's rate: crossfade
inline constexpr double kExpSlewS = 1e-4;      // the exponential law's safety bound: the speed moves at most 1 per this (SPEC §3.6, D-049 A1)
inline constexpr double kTempoCapCents = 50.0; // tempo "follow": exact within this bend
inline constexpr double kMinRead = 3.0;        // the 4-point kernel's distance from the write
inline constexpr int kTapeGrid = 32;           // the tape history's control rate (SPEC §11)
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
    // The tape law's write-position history, one point per 32 samples over the
    // whole line (SPEC §11: ~1.1 MiB at 192 kHz for both lines).
    tapeCap_ = lineLen_ / k::kTapeGrid + 8;
    for (auto& r : tapeRing_) r.assign(static_cast<std::size_t>(tapeCap_), 0.0);
    modRampN_ = std::max(1, static_cast<int>(std::lround(k::kModRampS * sr_)));
    rateA_ = 1.0 - std::exp(-1.0 / (k::kModRateGlideS * sr_));
    up_ = std::pow(2.0, k::kCapSt / 12.0) - 1.0;   // the fastest shortening (pitch up)
    dn_ = 1.0 - std::pow(2.0, -k::kCapSt / 12.0);  // the fastest lengthening (pitch down)
    capUp_ = std::pow(2.0, k::kTempoCapCents / 1200.0) - 1.0;
    capDn_ = 1.0 - std::pow(2.0, -k::kTempoCapCents / 1200.0);
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
    fadeLands_ = false;
    actionsN_ = 0;
    n_ = 0;
    paramMoved_ = false;
    fadeCount_[0] = fadeCount_[1] = 0;
    // A2's must-fail (resetKeepsModPhase): a reset that leaves one piece of running
    // state as it was. The contract's literal plant, an unsnapped smoother, cannot
    // reach the output here: a reset instance is at rest, and the rest landing
    // snaps every smoother on its first sample (D-043). The modulation phase is
    // what rest HOLDS, so a reset that skips it shows (D-019, §10).
    const double keptPhase = modPhase_;
    modPhase_ = 0.0;
    mods_[0] = mods_[1] = 0.0;
    reseed(false);
    if (plant::resetKeepsModPhase) modPhase_ = keptPhase;
    seedApplied_ = p_[kModSeed];
    resetStats();
    snapAll(true);
    dryPrev_[0] = dryPrev_[1] = 0.0;
    resolveBoth(bpm_);
    for (int k = 0; k < 2; ++k) { landHead(k); head_[k].lastPos = head_[k].D; head_[k].statsLive = false; }
    idle_ = true;
    restDirty_ = true;  // the first resting sample lands everything, the offsets included
    silentRun_ = quietRun_ = tailClock_ = 0;
    nonFinite_.store(0, std::memory_order_relaxed);
    nonFiniteLatch_.store(false, std::memory_order_relaxed);
    if (plant::resetClearsLatch) clearMeters();  // the meter latches are NEVER reset's (RS-6.13; critic T-014 LOW)
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
    fadeLands_ = true;  // this fade ends by adopting the patch (a Clear's fade alone lands nothing)
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
  // IN PLACE is allowed: inL == outL and inR == outR (tested, T-013). Any other
  // overlap (partial, or crossed: inL == outR) is not.
  void process(const double* inL, const double* inR, double* outL, double* outR, int n, double bpm) {
    if (!prepared_ || n <= 0) {
      for (int i = 0; i < n; ++i) outL[i] = outR[i] = 0.0;
      actionsN_ = actionsDone_ = 0;
      return;
    }
    const FtzScope ftz;
    // The planted block-state hazard (T-040): the exponential law's bound reads the speed as of the block's first
    // sample, so what it does depends on where the host cut the blocks.
    if (plant::expSlewBlockStart) blockV_[0] = head_[0].V, blockV_[1] = head_[1].V;
    // The planted in-place hazard (T-013): outputs cleared at the top of the
    // block, before the input is read. Out of place it changes nothing; in place
    // (inL == outL) it silences the input. The real loop reads sample i's input
    // before it writes sample i's output, and never reads input after that.
    if (plant::processReadsAfterWrite)
      for (int i = 0; i < n; ++i) outL[i] = outR[i] = 0.0;
    if (plant::allocInProcess) {
      std::vector<double> scratch(8, inL[0]);  // the planted audio-thread allocation
      outL[0] = scratch[0] * 0.0;
    }
    // The meters (§16.1): this block's peaks and overs, published once after the
    // loop. The latches are only ever SET here; clearMeters() is their one clear.
    if (plant::latchClears) { tapIn_.clip.store(false, std::memory_order_relaxed); tapOut_.clip.store(false, std::memory_order_relaxed); }
    double pkIn = 0.0, pkOut = 0.0;
    const double bpmEnd = (std::isfinite(bpm) && bpm > 1.0) ? bpm : 120.0;
    if (idle_ && !plant::tempoGlidesAtRest) bpmPrev_ = bpmEnd;  // at rest the tempo lands, never glides (§3.3)
    double bpmStart = bpmPrev_;  // re-landed if the instance rests and wakes inside this block
    const bool sync = p_[kSync] >= 0.5;

    for (int i = 0; i < n; ++i) {
      // Actions fired for this block land at their own sample (RS-4.12, A23),
      // before that sample is read. Offsets past the block land on its last
      // sample. The planted shim applies them all at the block's start.
      while (actionsDone_ < actionsN_ &&
             (plant::actionAtBlockStart || std::min(actions_[actionsDone_].offset, n - 1) <= i))
        applyAction(actions_[actionsDone_++].id);
      // Input guard (RS-7.9): non-finite input becomes 0, each repair counted.
      double xL = inL[i], xR = inR[i];
      if (!std::isfinite(xL)) { xL = 0.0; countRepair(); }
      if (!std::isfinite(xR)) { xR = 0.0; countRepair(); }

      if (idle_) {
        atRest(bpmEnd);
        if (!plant::tempoGlidesAtRest) bpmStart = bpmEnd;
        const bool wake = plant::wakeLeftOnly ? (xL != 0.0) : (xL != 0.0 || xR != 0.0);
        if (!wake) {
          // At rest the output is exact zero. The planted selfStart emits a
          // speck instead: in horde's feedback cycle (A14, loop gain 1.2) that
          // speck grows, a loop that starts itself from silence.
          outL[i] = outR[i] = plant::selfStart ? 1e-24 : 0.0;
          continue;
        }
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

      // Input Gain before every path (B435: the dry path, the loop and every
      // detector), then the input tap directly after it.
      const double rawPk = plant::tapReadsRaw ? std::max(std::fabs(inL[i]), std::fabs(inR[i])) : std::max(std::fabs(xL), std::fabs(xR));
      if (!plant::inGainAfterLoop) {
        xL *= gIn;
        xR *= gIn;
      }
      pkIn = std::max(pkIn, (plant::tapBeforeGain || plant::tapReadsRaw) ? rawPk : std::max(std::fabs(xL), std::fabs(xR)));
      const bool silentIn = xL == 0.0 && xR == 0.0;
      silentRun_ = silentIn ? silentRun_ + 1 : 0;
      // The tail clock (D-019): counts silent samples since the input stopped,
      // the loop last held (g >= 1) or the declared tail last changed, so a
      // full declared tail is always allowed after the last change.
      // It also restarts while any head moves (D-025, critic M2-H1): the
      // declared tail is built from the TARGET times, and a shortening glide's
      // head still sounds repeats from the old, longer time.
      if (plant::tailClockFromSilence) tailClock_ = silentRun_;
      else tailClock_ = (silentIn && g < 1.0 && fbTarget() < 1.0 && (plant::tailClockIgnoresMotion || !headsMoving())) ? tailClock_ + 1 : 0;

      // Encode into the lines (§4), then the input filter on what enters them.
      double u[2];
      for (int k = 0; k < 2; ++k) u[k] = enc_[k][0] * xL + enc_[k][1] * xR;
      if (plant::inGainDryOnly)  // the planted misplacement (critic T-014 H1): Input Gain reaches the dry path only
        for (int k = 0; k < 2; ++k) u[k] /= gIn;
      for (int k = 0; k < 2; ++k) u[k] = filterSample(fIn_, k, u[k]);

      // Time targets: the tempo moves per sample within the block (D-008).
      const double b = plant::tempoIgnored ? 120.0 : bpmStart + (bpmEnd - bpmStart) * (i + 1) / n;
      const bool tempoMoved = sync && b != bpm_;
      bpm_ = b;
      if (tempoMoved) resolveBoth(b);
      // A move's class (SPEC §3.5): a key moved it (1) or the tempo alone (2).
      const int cls = paramMoved_ ? 1 : (tempoMoved ? 2 : 0);
      paramMoved_ = false;

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
      // A load's fade holds the heads (they land on the new times at its end,
      // D-020). A Clear's fade lands nothing, so its heads keep moving exactly as
      // without it: a glide continues, a move keeps its mode (ruled by poll
      // 2026-10-23, D-040; freezing them broke "a moving time does what its mode
      // says": critic T-037 H1).
      if (!loading || (!fadeLands_ && !plant::clearFreezesHeads))
        for (int k = 0; k < 2; ++k) stepHead(k, cls);
      if (plant::blockSteppedD && i == 0)  // the plant: the read holds its position for the call, then steps
        for (int k = 0; k < 2; ++k) heldD_[k] = head_[k].D;
      modStep();  // the line modulation (§7), on every head of a line
      for (int k = 0; k < 2; ++k) {
        backstop(k);
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
      ++n_;

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
      // A Clear's fading wet follows the LIVE mix ("under the current settings",
      // ruled by poll 2026-10-23, D-040); only a load's fade keeps the old wet
      // level, because its new patch has already landed (D-020).
      const bool frozenWet = loading && (fadeLands_ || plant::clearFreezesWet);
      const double wg = frozenWet ? fadeWg_ : (mix <= 0.0 ? 0.0 : std::sin(mix * k::kPi / 2));
      double dL = plant::dryLimiter ? std::tanh(xL) : xL;
      double dR = plant::dryLimiter ? std::tanh(xR) : xR;
      if (plant::dryLate) {  // A4's must-fail: the dry path one sample late (a latency the module does not declare)
        std::swap(dL, dryPrev_[0]);
        std::swap(dR, dryPrev_[1]);
      }
      const double gLate = plant::inGainAfterLoop ? gIn : 1.0;  // the planted misplaced Input Gain
      double oL = (dg * dL + wg * wo[0]) * gOut * gLate;
      double oR = (dg * dR + wg * wo[1]) * gOut * gLate;
      if (plant::monoSum) oL = oR = 0.5 * (oL + oR);  // A8's must-fail: the image collapsed to mono
      if (!plant::noOutputGuard && (!std::isfinite(oL) || !std::isfinite(oR))) {
        oL = oR = 0.0;
        clearLoop();
        countRepair();
      }
      outL[i] = oL;
      outR[i] = oR;
      // The output tap: after Output Gain, every path and the tail. (The planted
      // outTapDryOnly reads the dry term alone, blind to the wet and the tail.)
      pkOut = std::max(pkOut, plant::outTapDryOnly ? std::max(std::fabs(dg * dL * gOut), std::fabs(dg * dR * gOut))
                                                   : std::max(std::fabs(oL), std::fabs(oR)));
      if (loadEnds) {
        if (fadeLands_ || plant::clearLandsValues) {
          adoptPatch();  // the old audio has faded: the new patch takes the wet too
          if (!plant::loadKeepsLine) clearLoop();
          for (int k = 0; k < 2; ++k) landHead(k);
        } else {
          clearLoop();  // a Clear's fade (§16.7): the repeats end, nothing lands, the heads stay
        }
        fadeLands_ = false;
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
    actionsN_ = actionsDone_ = 0;  // the block's actions are spent
    publishTap(tapIn_, pkIn);
    publishTap(tapOut_, pkOut);
  }

  // ---- declarations (SPEC §10) ----------------------------------------------
  int latency() const { return 0; }
  int minBlock() const { return 1; }
  int channels() const { return 2; }
  bool tailMayChange() const { return true; }
  // Samples until the instance is at rest once the input stops (§10, §12.1's
  // reading): (n+2)·T_max + 0.25 s for g < 1, kInfiniteTail for g >= 1.
  std::int64_t tail() const { return tailN_; }

  // FOUNDATIONS' view of the declarations for THIS prepared configuration
  // (fx_operator.h; horde row A1). Before prepare() every field is left
  // undeclared, so validate() refuses it: a forgotten declaration must be an
  // absence, never a confident 0 (FOUNDATIONS #129). Latency 0, exact (nothing
  // is buffered ahead, §10); the tail mapped by tailDecl32, may_change (it
  // follows feedback, time and depth); min_block 1 (bit-identical down to one
  // sample, §10); the load bound is the 20 ms fade-out at this rate (D-020),
  // the same count the engine fades over.
  foundations::OperatorProperties properties() const {
    foundations::OperatorProperties p;
    if (!prepared_) return p;
    p.latency = foundations::LatencyDecl{0, 0.0, foundations::ResidualSign::kNone};
    if (!plant::propsNoTail) p.tail = foundations::TailDecl{tailDecl32(tailN_), true};
    p.min_block = 1u;
    p.load = foundations::LoadDecl{static_cast<std::uint32_t>(plant::loadBoundShort ? rampN_ / 2 : rampN_)};
    return p;
  }

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

  // ---- saved state (SPEC §16.2, RS-6.6, RS-6.10 – 6.13; T-015) --------------
  // saveState: any thread that is not running process() concurrently (horde's
  // main thread). It writes every key's TARGET (never a value mid-ramp) and no
  // live state (meters, lines, phases: A20), into the caller's buffer, without
  // allocating. Returns the length, or 0 if cap is too small (state::kMaxBytes
  // always suffices).
  std::size_t saveState(char* out, std::size_t cap) const {
    state::Values v{};
    for (int i = 0; i < kNumParams; ++i) v[static_cast<std::size_t>(i)] = p_[i];
    const bool latch = tapIn_.clip.load(std::memory_order_relaxed) || tapOut_.clip.load(std::memory_order_relaxed);
    return state::write(v, out, cap, latch);
  }
  // The load in two steps (RS-6.6). stageState parses and validates OFF the
  // audio thread (all or nothing; a text over 2 KiB refused first) into one
  // staging slot; adoptStaged lands it ON the audio thread, between process()
  // calls, with no allocation. One slot, handed over by an atomic flag: a second
  // stage while one is pending is refused rather than racing the adopt.
  bool stageState(std::string_view text, const char** why = nullptr) {
    if (stagedReady_.load(std::memory_order_acquire)) {
      if (why) *why = "a staged state is pending";
      return false;
    }
    state::Values v{};
    if (!state::parse(text, v, why)) return false;
    staged_ = v;
    stagedReady_.store(true, std::memory_order_release);
    return true;
  }
  // The patch keys land as a load (D-016, D-020: no ramp-in, the old repeats fade
  // under their old settings; at rest, straight on). The Device keys are the
  // INSTANCE's (RS-4.5): restored here, but through setParam, so a gain restored
  // mid-play ramps its 20 ms like any gain move (B435) and lands at rest.
  bool adoptStaged() {
    if (!stagedReady_.load(std::memory_order_acquire)) return false;
    std::array<KeyValue, kNumParams> kv{};
    std::size_t n = 0;
    for (int i = 0; i < kNumParams; ++i)
      if (kParams[static_cast<std::size_t>(i)].morph != Morph::Device) kv[n++] = {kParams[static_cast<std::size_t>(i)].key, staged_[static_cast<std::size_t>(i)]};
    if (plant::adoptResets) reset();  // the planted hard cut: the ringing repeats end at the load (critic T-015 H1)
    loadState(std::span<const KeyValue>(kv.data(), n));
    for (int i = 0; i < kNumParams; ++i)
      if (kParams[static_cast<std::size_t>(i)].morph == Morph::Device) setParam(i, staged_[static_cast<std::size_t>(i)]);
    stagedReady_.store(false, std::memory_order_release);
    return true;
  }

  // ---- actions (SPEC §16.7, contract §12.6 RS-4.12, row A23; T-037) ---------
  // One action: Clear. The key resolves OFF the audio thread (no string on the
  // audio thread, FOUNDATIONS #131's rule for parameters); fireAction queues it
  // for the NEXT process() call at a sample offset within that block, in the
  // per-block context horde builds before process. Up to kMaxActions per block,
  // preallocated; a further one is refused, never dropped silently. Same events
  // at the same offsets give the same output; no event, no change (A23). An
  // action is not a parameter: not automated, not saved, no history (ADR-267 A1).
  static constexpr int kActionClear = 0;
  static constexpr int kMaxActions = 8;
  static std::optional<int> resolveAction(std::string_view key) {
    for (std::size_t i = 0; i < kActionKeys.size(); ++i)
      if (kActionKeys[i] == key) return static_cast<int>(i);
    return std::nullopt;
  }
  // Refused: an unknown id, a full queue, or an offset outside [0, maxBlock)
  // (critic T-037 L1: never relocated silently). An offset at or past the
  // NEXT block's n (when that block is shorter than maxBlock) lands on its last
  // sample. Actions queued before reset(), an unprepared process() or n <= 0 are
  // discarded with that call.
  bool fireAction(int id, int offset) {
    if (id != kActionClear || actionsN_ >= kMaxActions || offset < 0 || offset >= maxBlock_) return false;
    int j = actionsN_++;  // insertion keeps offset order; equal offsets keep firing order
    while (j > 0 && actions_[j - 1].offset > offset) { actions_[j] = actions_[j - 1]; --j; }
    actions_[j] = {id, offset};
    return true;
  }

  // ---- meters (SPEC §16.1, B435, contract RS-4.7; T-014) ---------------------
  // Two taps: directly after Input Gain (before every path and detector) and
  // directly after Output Gain (after every path and the tail). Each holds the
  // last block's peak |x| over both channels, and a clip latch that sets on a
  // sample with |x| > 1.0 (over 0 dBFS; exactly 1.0 is full scale, not a clip)
  // and holds until clearMeters(): never cleared by process(), reset() or a load
  // (B225; RS-6.13's table: a meter latch is live state, never saved).
  // Readable from any thread; relaxed is enough, as each value stands alone.
  // M4's T-033 adds its status word and ceiling count beside these (D-030).
  struct Meter {
    double peak;
    bool clip;
  };
  Meter inputMeter() const { return readTap(tapIn_); }
  Meter outputMeter() const { return readTap(tapOut_); }
  // clearMeters() is the one call meant for ANOTHER thread too (horde's mixer
  // clears from the GUI): two atomic stores. A block in flight publishes at its
  // end, so an over earlier in that block can re-set a latch just after a clear;
  // it was a real over, and the next clear removes it.
  void clearMeters() {
    tapIn_.clip.store(false, std::memory_order_relaxed);
    tapOut_.clip.store(false, std::memory_order_relaxed);
  }
  // How many time crossfades line k has started (test read: the retrigger queue).
  std::uint64_t fadeCount(int k) const { return fadeCount_[k]; }
  // The sounding head's delay in samples when no fade runs (test read).
  double headDelay(int k) const { return head_[k].D; }
  // The read position the line sounds now, delay + modulation (test read).
  double readPosition(int k) const { return (head_[k].fade ? head_[k].dA : head_[k].D) + mods_[k]; }
  double modOffset(int k) const { return mods_[k]; }
  bool fading(int k) const { return head_[k].fade; }
  // Read-speed statistics on the sounding position since reset / resetStats
  // (SPEC §3.6's invariant: s = 1 - d(position)/dt in [0.25, 4], never <= 0).
  struct SpeedStats {
    double sMin = 1e300, sMax = -1e300;
    std::uint64_t reverse = 0, clamps = 0, fallbacks = 0, overruns = 0, floors = 0;
    // The exponential law's 0.1 ms bound (SPEC §3.6, D-049 A1): how many samples it cut, and the sample index of the
    // last one. A test reads them to see that the bound binds only just after a retarget, never in a relaxation.
    std::uint64_t slewBinds = 0;
    std::int64_t lastSlewBind = -1;
  };
  SpeedStats speedStats(int k) const { return stats_[k]; }
  void resetStats() {
    for (auto& st : stats_) st = SpeedStats{};
  }
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
  // ---- actions (§16.7) ------------------------------------------------------
  struct ActionEvent {
    int id = 0;
    int offset = 0;
  };
  // Clear: the repeats fade out over 20 ms under the current settings and end,
  // through the load's own fade (§10, D-020) with nothing to adopt. At rest
  // nothing rings: it does nothing. During a fade (a load's or a Clear's) it
  // never restarts it, as a second load never does (D-020).
  void applyAction(int id) {
    if (id != kActionClear || idle_) return;
    if (plant::clearWritesState) { p_[kFb] = 0.0; applied(kFb); }  // the planted Clear that edits the patch
    if (plant::clearCuts) { clearLoop(); return; }                   // the planted hard cut, no fade
    if (loadLeft_ > 0) {
      if (plant::clearRestartsFade) loadLeft_ = rampN_;
      return;
    }
    fadeWg_ = rMix_.cur <= 0.0 ? 0.0 : std::sin(rMix_.cur * k::kPi / 2);  // the current wet level, frozen for the fade
    loadLeft_ = rampN_;
    fadeLands_ = false;
  }

  // ---- meter taps (§16.1) ---------------------------------------------------
  struct Tap {
    std::atomic<double> peak{0.0};
    std::atomic<bool> clip{false};
  };
  // A tap that could lock would put a lock on the audio thread (charter §Domain).
  static_assert(std::atomic<double>::is_always_lock_free && std::atomic<bool>::is_always_lock_free,
                "the meter taps must be lock-free atomics");
  static void publishTap(Tap& t, double pk) {
    t.peak.store(pk, std::memory_order_relaxed);
    if (pk > 1.0) t.clip.store(true, std::memory_order_relaxed);
  }
  static Meter readTap(const Tap& t) {
    return {t.peak.load(std::memory_order_relaxed), t.clip.load(std::memory_order_relaxed)};
  }

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
        return {{{{0.5, 0.5}, plant::pingpongBothLines ? std::array<double, 2>{0.5, 0.5} : std::array<double, 2>{0.0, 0.0}}},
                plant::pingpongNoCross ? M2{{{1.0, 0.0}, {0.0, 1.0}}} : M2{{{0.0, 1.0}, {1.0, 0.0}}},
                {{{1.0, 0.0}, plant::pingpongInvertsR ? std::array<double, 2>{0.0, -1.0} : std::array<double, 2>{0.0, 1.0}}}};
      case 2:  // mid/side: M = (L+R)/2, S = (L-R)/2; out L = M + S, R = M - S
        return {{{{0.5, 0.5}, plant::msSideGain2 ? std::array<double, 2>{1.0, -1.0} : std::array<double, 2>{0.5, -0.5}}},
                {{{1.0, 0.0}, {0.0, 1.0}}},
                {{{1.0, 1.0}, plant::msSign ? std::array<double, 2>{1.0, 1.0} : std::array<double, 2>{1.0, -1.0}}}};
      default:  // stereo
        return {{{{1.0, 0.0}, {0.0, 1.0}}}, {{{1.0, 0.0}, {0.0, 1.0}}},
                {{{1.0, 0.0}, plant::stereoInvertsR ? std::array<double, 2>{0.0, -1.0} : std::array<double, 2>{0.0, 1.0}}}};
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
  // Base time x time.scale, x (1 + offset), synced times halved together past
  // 12 s (the L/R ratio and the grid survive), free times clamped, 1 ms floor.
  // The scale comes BEFORE the fold and the clamp (§3.4): a clamp on the
  // unscaled times, scaled afterwards, would let a scaled time past the line
  // or under the floor. Also refreshes the declared tail, which depends on the
  // times, the feedback and the depth.
  void resolveBoth(double bpm) {
    const bool sync = p_[kSync] >= 0.5;
    const double b = (bpm > 1.0) ? bpm : 120.0;
    const int div = static_cast<int>(p_[kDiv]);
    const double base = sync ? kDivisions[static_cast<std::size_t>(div)].beats * 60.0 / b : p_[kTimeMs] / 1000.0;
    const double sc = timeScale(sync);
    const double scaled = plant::scaleAfterClamp ? base : base * sc;  // the plant defers the scale to after the clamp
    double tL = scaled * (1.0 + p_[kOffL] / 100.0), tR = scaled * (1.0 + p_[kOffR] / 100.0);
    if (plant::scaleOffsetRewrite) {  // the plant: the offset written distributively, a different rounding even at x1
      tL = scaled + scaled * (p_[kOffL] / 100.0);
      tR = scaled + scaled * (p_[kOffR] / 100.0);
    }
    folded_ = false;
    if (sync && !plant::clampTruncates)
      while (std::max(tL, tR) > k::kMaxDelayS) { tL *= 0.5; tR *= 0.5; folded_ = true; }
    const double lo = plant::noMinFloor ? 0.0 : k::kMinDelayS;
    tL = std::clamp(tL, lo, k::kMaxDelayS);
    tR = std::clamp(tR, lo, k::kMaxDelayS);
    if (plant::scaleAfterClamp) {  // capped at the line so the planted fault cannot read outside it
      tL = std::min(tL * sc, k::kMaxDelayS);
      tR = std::min(tR * sc, k::kMaxDelayS);
    }
    tT_[0] = tL * sr_;
    tT_[1] = tR * sr_;
    setTail(computeTail());
  }

  // time.scale as it multiplies the base time (§3.1, D-048): the value itself
  // while free; while synced the nearest OCTAVE, so the repeats stay on the grid
  // (x1/4, 1/2, 1, 2, 4). The boundaries sit at 2^(k + 1/2), which is irrational:
  // NO double lies exactly on one, so there is no tie to own. A double is up or
  // down by which side of the true boundary it is on, and the double nearest each
  // boundary (sqrt2 x 2^(k-1), exact) is just ABOVE it (1.4142135623730951 >
  // sqrt 2), so it snaps UP. The snap is therefore a mantissa compare against that
  // one double: s = m x 2^e with m in [0.5, 1), the boundary at m = sqrt2 / 2
  // whatever the octave. It needs no log2, whose last bit differs between libms
  // (log2 of the nearest double to 2^-1.5 returns exactly -1.5, which rounded to
  // the WRONG octave). std::ldexp(1, n) is exact.
  double timeScale(bool sync) const {
    const double s = p_[kTimeScale];
    if (!sync) return s;
    if (plant::scaleIgnoredSynced) return 1.0;
    if (plant::scaleNotSnapped) return s;
    if (plant::scaleSnapRounds) return std::ldexp(1.0, static_cast<int>(std::lround(std::log2(s))));  // the plant: round(log2 s), wrong at the boundary's double
    int e = 0;
    const double m = std::frexp(s, &e);  // s = m x 2^e, so s lies in [2^(e-1), 2^e)
    return std::ldexp(1.0, m >= std::numbers::sqrt2 * 0.5 ? e : e - 1);
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
    // The planted fault builds the tail from the unscaled times (scaleTailUnscaled).
    const double t01 = std::max(tT_[0], tT_[1]) / (plant::scaleTailUnscaled ? timeScale(p_[kSync] >= 0.5) : 1.0);
    const double tMax = t01 + p_[kModDepth] / 1000.0 * sr_ + 4.0;
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

  // A head: the sounding delay D (moving by V a sample under a repitch law),
  // or a crossfade from dA to dB. Each move is latched to the time.change /
  // time.law / time.tempoMode in force when it starts (§3.5 deferral).
  enum Mode : int { kRepitch = 0, kCrossfade = 1, kJump = 2, kFollow = 4 };
  struct Head {
    double D = 0, V = 0, T = 0;  // delay, its per-sample velocity, the target
    bool fade = false;
    double dA = 0, dB = 0;
    int fi = 0, fn = 1;
    int mode = kRepitch;         // latched per move
    int law = 0;                 // latched per move: 0 tape, 1 exponential
    bool tempo = false;          // the move's cause: the tempo alone
    double lastPos = 0;          // the sounding read position one sample ago
    bool statsLive = false;      // false for one sample after a handover (no speed measured)
    double lnT = 0;              // ln T, cached per target (the exponential law)
  };

  // The tape law's state (§3.6): a tape at speed v (tape units a sample), the
  // write head at tape position p, the read head one tape unit behind it. The
  // write position's history is a line through (baseN, baseP) with slope baseV
  // (a steady tape) followed by the points recorded every 32 samples since; a
  // repeat recorded at v(t') plays at v(t) because the read finds t' where the
  // tape that is under it now was written. pNow and the history restart from 0
  // whenever the tape law starts after D has been steady (exact, and it keeps
  // the numbers small over hours).
  struct Tape {
    bool active = false;
    double v = 0, p = 0;          // speed, write position at sample n_
    std::int64_t baseN = 0;       // the steady segment's end (a sample index)
    double baseP = 0, baseV = 0;
    std::int64_t g0 = 0;          // the grid index of the oldest recorded point
    int head = 0, count = 0;      // the ring of recorded points
    double c = 0;                 // the read cursor, a sample time
  };

  void landHead(int k) {
    Head& h = head_[k];
    h.D = h.dA = h.dB = h.T = tT_[k];
    h.V = 0;
    h.fade = false;
    h.fi = 0;
    h.fn = 1;
    h.lnT = std::log(h.T);
    h.statsLive = false;  // a landing is a handover: no speed is measured across it
    tape_[k].active = false;
  }

  int fadeLength(int mode) const {
    const double xf = plant::xfadeIgnored ? 0.030 : p_[kXfadeMs] / 1000.0;
    const double s = (mode == kJump && !plant::jumpUsesXfade) ? k::kJumpS : xf;
    return std::max(1, static_cast<int>(std::lround(s * sr_)));
  }

  void startFade(int k, double to, int mode) {
    Head& h = head_[k];
    h.dA = h.D;
    h.dB = to;
    h.fi = 0;
    h.fn = fadeLength(mode);
    h.fade = true;
    h.V = 0;
    ++fadeCount_[k];
  }

  // The behaviour a NEW move gets (§3.5): time.change for a key move,
  // time.tempoMode for a move the tempo alone caused; and the law.
  void latch(Head& h) const {
    h.law = static_cast<int>(p_[kLaw]);
    if (h.tempo) {
      const int tm = plant::tempoModeIgnored ? 0 : static_cast<int>(p_[kTempoMode]);  // 0 crossfade, 1 glide, 2 follow
      h.mode = tm == 0 ? kCrossfade : tm == 2 ? kFollow : kRepitch;
    } else {
      const int ch = static_cast<int>(p_[kChange]);  // 0 repitch, 1 crossfade, 2 jump
      h.mode = ch == 0 ? kRepitch : ch == 1 ? kCrossfade : kJump;
    }
  }

  // Advance one head by one sample (§3.5-3.8).
  void stepHead(int k, int cls) {
    Head& h = head_[k];
    // The planted spurious retarget (T-040): for 100 samples early on, the target drops to a quarter, then returns: a
    // move nobody asked for, whose bound-binding the census must see as "a bind before any move".
    const double T = (plant::expSpuriousRetarget && n_ >= 4800 && n_ < 4900) ? tT_[k] * 0.25 : tT_[k];
    const double Tprev = h.T;
    const double D0 = h.D;
    bool tapeRan = false;
    if (T != Tprev) {  // a new move, or a retarget of the one in flight
      h.T = T;
      h.lnT = std::log(T);
      // A move a KEY caused always follows time.change (§3.8), even when the
      // tempo moves while it is still in flight: only a settled head hands the
      // move to time.tempoMode (critic M2-H4).
      const bool keyInFlight = !h.tempo && (h.fade || std::fabs(h.D - Tprev) >= 1.0);
      if (cls == 1) h.tempo = false;
      else if (cls == 2 && (plant::tempoStealsKeyMove || !keyInFlight)) h.tempo = true;
      latch(h);
    }
    // Follow (§3.8): while a follow crossfade runs, its destination keeps
    // tracking the grid within the 50-cent cap, so the head is on the grid
    // when the fade ends and following resumes (critic M2-M1).
    if (h.fade && h.mode == kFollow && cls == 2 && !plant::followNoTrackInFade) {
      const double dT = T - Tprev;
      if (dT >= -capUp_ && dT <= capDn_) h.dB += dT;
    }
    if (h.fade && plant::retargetMidFade && std::fabs(T - h.dB) >= 0.5) h.dB = T;
    if (h.fade) {
      if (plant::deferralBroken) h.fn = std::min(h.fn, fadeLength(static_cast<int>(p_[kChange]) == 2 ? kJump : kCrossfade));
      if (++h.fi < h.fn) { finishStep(k, D0, false); return; }
      h.fade = false;  // the fade has sounded its last (weight-1) sample
      h.D = h.dB;
      h.V = 0;
      h.statsLive = false;  // a handover: dA's position gives way to dB's
      tape_[k].active = false;
      // The queue (§3.5): a target that moved during the fade is faded to
      // next; the latest one wins, the ones between are dropped.
      if (h.mode != kRepitch && std::fabs(T - h.D) >= 0.5) {
        startFade(k, T, h.mode == kJump ? kJump : kCrossfade);
        finishStep(k, D0, false);
        return;
      }
    }
    if (h.mode == kJump && plant::bareJump) {  // the click metric's planted control
      h.D = T;
      finishStep(k, D0, false);
      return;
    }
    if (h.mode == kCrossfade || h.mode == kJump) {
      if (std::fabs(T - h.D) >= 0.5) startFade(k, T, h.mode);
      finishStep(k, D0, false);
      return;
    }
    if (h.mode == kFollow) {  // tempo "follow" (§3.8): exact within 50 cents, a crossfade past it
      const double dT = T - Tprev;
      const bool settled = std::fabs(h.D - Tprev) < 1.0;
      if (settled && (plant::followNoCap || (dT >= -capUp_ && dT <= capDn_))) {
        // Move toward T by at most the cap: follows the grid exactly and
        // absorbs any residual a previous move left (critic M2-M2).
        h.D += plant::followNoCap ? dT : (plant::followKeepsResidual ? dT : std::clamp(T - h.D, -capUp_, capDn_));
        finishStep(k, D0, false);
        return;
      }
      if (settled) {
        if (std::fabs(T - h.D) >= 0.5) startFade(k, T, kCrossfade);
        finishStep(k, D0, false);
        return;
      }
      // else a glide from a key move is still in flight: the law finishes it
    }
    // ---- repitch: the law moves D toward T ------------------------------------
    const double gap = T - h.D;
    if (gap == 0.0 && h.V == 0.0) { finishStep(k, D0, false); return; }
    if (!plant::noFallback && std::fabs(gap) / (gap < 0 ? up_ : dn_) > k::kFallbackS * sr_) {
      ++stats_[k].fallbacks;  // D-006's hybrid: a 12 s -> 10 ms move crossfades, never dives for 12 s
      startFade(k, T, kCrossfade);
      finishStep(k, D0, false);
      return;
    }
    const double tau = std::max(1.0, p_[kGlide] * (plant::glideFixedRate ? 48000.0 : sr_));
    const double vPrev = plant::expSlewBlockStart ? blockV_[k] : h.V;
    bool expLaw = false;
    if (h.law == 1 || plant::expLawLinear) {
      // "exponential" (§3.6): a one-pole on ln D, time constant time.glide.
      h.V = plant::expLawLinear ? gap / tau : h.D * (h.lnT - std::log(h.D)) / tau;
      expLaw = !plant::expLawLinear;
    } else {
      h.V = tapeStep(k, T, tau) - h.D;  // "tape": the varispeed model's read delay
      tapeRan = true;
    }
    // The law's pitch cap (|pitch| <= 12 st, so dD/dt in [-1, +0.5]).
    if (!plant::noLawCap && !plant::expLawLinear) h.V = std::clamp(h.V, -up_, dn_);
    // The exponential law's safety bound (D-049 A1): its V is a memoryless function of (D, T), so a
    // retarget (an onset, or a move reversed mid-glide) steps the speed by up to 1.5 in one sample, which
    // reads as a click (M2 up to 5.1). Bound the step per second, not per sample, so it holds at any
    // host rate (a step defect reads in proportion to the rate). It binds only where the law's V jumps;
    // elsewhere the one-pole on ln D is exact. Applied after the cap: both bounds hold together, since
    // the result lies between two values the cap already allowed.
    if (expLaw && !plant::expSlewOff) {
      const double a = 1.0 / ((plant::expSlewLong ? 0.010 : k::kExpSlewS) * sr_);
      const double bound = std::clamp(h.V, vPrev - a, vPrev + a);
      if (bound != h.V) { ++stats_[k].slewBinds; stats_[k].lastSlewBind = n_; }
      h.V = bound;
    }
    if (std::fabs(T - (h.D + h.V)) < 1e-9 && std::fabs(h.V) <= std::fabs(gap)) { h.D = T; h.V = 0; }
    else h.D += h.V;
    if (tapeRan) tape_[k].c = static_cast<double>(n_) - h.D;  // the cursor follows a capped head
    finishStep(k, D0, tapeRan);
  }

  // After a head step: the tape model lives only while the tape law moves the
  // head. Once a tape glide has snapped to its target, its history over the
  // line is steady to ~1e-9 (D only converges once the speeds under the read
  // are steady), so the next tape move rebuilds an exact steady tape. (A
  // keep-alive that ran the tape on after a glide changed no result: critic L2.)
  void finishStep(int k, double /*D0*/, bool tapeRan) {
    if (!tapeRan) tape_[k].active = false;
  }

  // The tape law (§3.6): the tape speed lags its target 1/T with time constant
  // tau; the read sits where the tape now under it was written. Returns the
  // read delay the model implies (the caller caps it).
  double tapeStep(int k, double T, double tau) {
    Tape& tp = tape_[k];
    const Head& h = head_[k];
    if (!tp.active) tapeRebuild(k, h.D);
    if (tau != tapeTau_) { tapeTau_ = tau; tapeA_ = 1.0 - std::exp(-1.0 / tau); }
    tp.v += (1.0 / T - tp.v) * tapeA_;
    if (plant::tapeNoHistory) {
      tapeAdvance(k);
      return 1.0 / tp.v;  // a speed with no memory: the planted fault
    }
    tapeAdvance(k);
    const double target = tp.p - 1.0;  // one tape unit behind the write head
    const double c = tapeSolve(k, target, tp.c);
    return static_cast<double>(n_) - c;
  }

  // The steady tape that matches the current delay: v = 1/D, history a line.
  void tapeRebuild(int k, double D) {
    Tape& tp = tape_[k];
    tp.active = true;
    tp.v = 1.0 / D;
    tp.p = 0.0;
    tp.baseN = n_ - 1;  // the write position one sample ago; this sample's advance follows
    tp.baseP = 0.0;
    tp.baseV = tp.v;
    tp.g0 = n_ / k::kTapeGrid + 1;
    tp.head = tp.count = 0;
    tp.c = static_cast<double>(n_) - D;
  }

  // One sample of tape travel: p moves by v; every 32 samples the write
  // position is recorded (the history the read searches).
  void tapeAdvance(int k) {
    Tape& tp = tape_[k];
    tp.p += tp.v;
    if (n_ % k::kTapeGrid == 0 && n_ > tp.baseN) {
      const std::int64_t g = n_ / k::kTapeGrid;
      if (tp.count == tapeCap_) { tp.head = (tp.head + 1) % tapeCap_; --tp.count; ++tp.g0; }
      if (tp.count == 0) tp.g0 = g;
      tapeRing_[k][static_cast<std::size_t>((tp.head + tp.count) % tapeCap_)] = tp.p;
      ++tp.count;
    }
  }

  // The history's breakpoints, in time order: j = -2 a synthetic point one
  // grid step before the base (the steady line), -1 the base, 0..count-1 the
  // recorded points (every 32 samples), then the present.
  std::pair<double, double> tapeBp(int k, std::int64_t j) const {
    const Tape& tp = tape_[k];
    if (j <= -2) return {static_cast<double>(tp.baseN - k::kTapeGrid), tp.baseP - k::kTapeGrid * tp.baseV};
    if (j == -1) return {static_cast<double>(tp.baseN), tp.baseP};
    if (j < tp.count) return {static_cast<double>((tp.g0 + j) * k::kTapeGrid), ringAt(k, tp.g0 + j)};
    return {static_cast<double>(n_), tp.p};
  }
  // The segment [bp(j), bp(j+1)) that holds time t (t in (baseN, now)).
  std::int64_t tapeSeg(int k, double t) const {
    const Tape& tp = tape_[k];
    if (tp.count == 0 || t < static_cast<double>(tp.g0 * k::kTapeGrid)) return -1;
    return std::min<std::int64_t>(static_cast<std::int64_t>(std::floor(t / k::kTapeGrid)) - tp.g0, tp.count - 1);
  }
  // The write position at sample time t: QUADRATIC through three breakpoints
  // (critic M2-M5: linear interpolation over 32 samples erred by up to 50 cents
  // in the read's speed at a 10 ms glide; quadratic cuts the error by ~h/tau).
  // Continuous: every stencil passes through both ends of its segment.
  double tapeP(int k, double t) const {
    const Tape& tp = tape_[k];
    const double now = static_cast<double>(n_);
    if (t >= now) return tp.p;
    if (t <= static_cast<double>(tp.baseN)) return tp.baseP - (static_cast<double>(tp.baseN) - t) * tp.baseV;
    const std::int64_t j = tapeSeg(k, t);
    const auto [t0, p0] = tapeBp(k, j - 1);
    const auto [t1, p1] = tapeBp(k, j);
    const auto [t2, p2] = tapeBp(k, j + 1);
    if (plant::tapeLinearHistory || !(t1 > t0 && t2 > t1)) return t2 > t1 ? p1 + (p2 - p1) * (t - t1) / (t2 - t1) : p1;
    const double l0 = (t - t1) * (t - t2) / ((t0 - t1) * (t0 - t2));
    const double l1 = (t - t0) * (t - t2) / ((t1 - t0) * (t1 - t2));
    const double l2 = (t - t0) * (t - t1) / ((t2 - t0) * (t2 - t1));
    return p0 * l0 + p1 * l1 + p2 * l2;
  }
  double ringAt(int k, std::int64_t g) const {
    const Tape& tp = tape_[k];
    if (tp.count == 0) return tp.baseP;
    const std::int64_t i = std::clamp<std::int64_t>(g - tp.g0, 0, tp.count - 1);
    return tapeRing_[k][static_cast<std::size_t>((tp.head + i) % tapeCap_)];
  }
  // The time t >= from where the write position equalled `target`: a forward
  // walk over breakpoints (the cursor never runs backward and cannot pass the
  // present), a linear estimate inside the segment, then two secant
  // refinements on the quadratic history. Bounded: a legitimate answer lies
  // within ~2 samples of the cursor (the read moves at most 4 samples a
  // sample), so 16 segments are never needed (critic: the cap only binds where
  // the backstop overrides the answer anyway).
  double tapeSolve(int k, double target, double from) const {
    const Tape& tp = tape_[k];
    const double now = static_cast<double>(n_);
    if (tapeP(k, from) >= target) return from;
    if (from < static_cast<double>(tp.baseN)) {
      if (tp.baseP >= target) return std::max(from, static_cast<double>(tp.baseN) - (tp.baseP - target) / tp.baseV);
      from = static_cast<double>(tp.baseN);
    }
    std::int64_t j = tapeSeg(k, from);
    for (int it = 0; it < 16; ++it) {
      const auto [t0, p0] = tapeBp(k, j);
      const auto [t1, p1] = tapeBp(k, j + 1);
      if (p1 >= target) {
        if (!(p1 > p0)) return std::max(from, t1);
        const double slope = (p1 - p0) / (t1 - t0);
        double c = t0 + (target - p0) / slope;
        for (int r = 0; r < 2; ++r) c = std::clamp(c - (tapeP(k, c) - target) / slope, t0, t1);
        return std::max(from, c);
      }
      if (t1 >= now) return now;
      ++j;
    }
    return std::max(from, tapeBp(k, j).first);
  }

  // ---- line modulation (§7) ----------------------------------------------------
  // mulberry32, bit for bit the lab's: the only randomness in Echo.
  struct Stream {
    std::uint32_t a = 0;
    double next() {
      a += 0x6D2B79F5u;
      std::uint32_t t = (a ^ (a >> 15)) * (1u | a);
      t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
      return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
    }
  };
  struct Drift {
    Stream r;
    double pts[4] = {0, 0, 0, 0};
    double u = 0;
  };

  // Catmull-Rom through the stream's points, clamped to [-1, 1]: unclamped it
  // overshoots to 1.25 and would read past the line's 20 ms margin.
  double driftVal(const Drift& d) const {
    const double p0 = d.pts[0], p1 = d.pts[1], p2 = d.pts[2], p3 = d.pts[3], u = d.u;
    const double m1 = 0.5 * (p2 - p0), m2 = 0.5 * (p3 - p1), u2 = u * u, u3 = u2 * u;
    const double v = (2 * u3 - 3 * u2 + 1) * p1 + (u3 - 2 * u2 + u) * m1 + (-2 * u3 + 3 * u2) * p2 + (u3 - u2) * m2;
    return plant::driftOvershoot ? v : std::clamp(v, -1.0, 1.0);
  }

  // Two streams derived from mod.seed alone. A SEEDED reseed (a mod.seed
  // change, contract §12.2) starts the new streams from the value being
  // output, so the modulation does not step; only its slope changes.
  void reseed(bool seeded) {
    const auto seed = static_cast<std::uint32_t>(p_[kModSeed]);
    for (int k = 0; k < 2; ++k) {
      Drift& d = drift_[k];
      const double cur = seeded ? driftVal(d) : 0.0;
      d.r.a = seed * 2u + 1u + static_cast<std::uint32_t>(k);
      if (seeded && !plant::unseededSeed) {
        d.pts[0] = cur; d.pts[1] = cur;
        d.pts[2] = d.r.next() * 2 - 1; d.pts[3] = d.r.next() * 2 - 1;
      } else {
        for (double& q : d.pts) q = d.r.next() * 2 - 1;
      }
      d.u = 0;
    }
  }

  // The shape mix in force (0 sine, 1 drift), blending from shapeFrom_ toward
  // shapeTarget_ over 250 ms after a switch.
  double shapeNow() const {
    return shapeW_ >= 1.0 ? shapeTarget_ : shapeFrom_ + (shapeTarget_ - shapeFrom_) * shapeW_;
  }

  // The offsets (samples) for both lines at this sample. The depth is limited
  // so the curve's own slope moves the read speed by at most 25 % and the read
  // stays 3 samples clear of the write head; and the OFFSET itself is
  // slew-limited to 0.25 sample a sample, so no rate glide, ramp, shape blend,
  // seed change or lifted depth limit can move the read faster (§7).
  void modStep() {
    if (rateL2_ != rateTargetL2_) {  // the rate glides in log-frequency (cached once settled)
      rateL2_ += (rateTargetL2_ - rateL2_) * rateA_;
      if (std::fabs(rateTargetL2_ - rateL2_) < 1e-12) rateL2_ = rateTargetL2_;
      rateF_ = std::exp2(rateL2_);
    }
    const double f = rateF_;
    rDepth_.tick();
    rSpread_.tick();
    modPhase_ += 2 * k::kPi * f / sr_;
    if (modPhase_ >= 2 * k::kPi) modPhase_ -= 2 * k::kPi;
    for (auto& d : drift_) {
      d.u += f / sr_;
      if (d.u >= 1.0) {
        d.u -= 1.0;
        d.pts[0] = d.pts[1]; d.pts[1] = d.pts[2]; d.pts[2] = d.pts[3];
        d.pts[3] = d.r.next() * 2 - 1;
      }
    }
    const double w = shapeNow();
    if (shapeW_ < 1.0) shapeW_ = std::min(1.0, shapeW_ + 1.0 / modRampN_);
    double t[2];
    modTargets(w, t);
    for (int k = 0; k < 2; ++k) {
      if (plant::modSlewOff) mods_[k] = t[k];
      else mods_[k] += std::clamp(t[k] - mods_[k], -k::kModSlopeMax, k::kModSlopeMax);
      // The read floor wins over the slew (critic M2-M3): a move to a short
      // time can leave a slewing offset that would read under 3 samples from
      // the write head, which the kernel cannot. Bounded here, per line.
      if (!plant::modNoFloorBound) {
        const auto [lo, hi] = headSpan(k);
        mods_[k] = std::clamp(mods_[k], k::kMinRead - lo, static_cast<double>(lineLen_ - k::kKernelMargin) - hi);
      }
    }
  }

  // The offsets the curve asks for now (no slew), each line limited by its OWN
  // heads (§7: A <= D - 3 samples) and by the curve's slope.
  void modTargets(double w, double* t) const {
    t[0] = t[1] = 0.0;
    const double depth = rDepth_.cur;
    if (depth <= 0.0) return;
    const double f = rateF_, sp = rSpread_.cur;
    const double s[2] = {std::sin(modPhase_), std::sin(modPhase_ + sp * k::kPi / 2)};
    const double a = driftVal(drift_[0]);
    const double dr[2] = {a, (1 - sp) * a + sp * driftVal(drift_[1])};
    // max |dm/dt|: 2 pi f for the sine; 3.5 f for Catmull-Rom through [-1, 1]
    const double slope = std::max(w < 1.0 ? 2 * k::kPi * f / sr_ : 0.0, w > 0.0 ? 3.5 * f / sr_ : 0.0);
    const double lim = plant::modDepthUnlimited ? depth : k::kModSlopeMax / slope;
    for (int k = 0; k < 2; ++k) {
      const double A = std::max(0.0, std::min({depth, lim, headSpan(k).first - k::kMinRead}));
      t[k] = A * ((1 - w) * s[k] + w * dr[k]);
    }
  }

  // The shortest and longest delay any head of line k reads at.
  std::pair<double, double> headSpan(int k) const {
    const Head& h = head_[k];
    if (!h.fade) return {h.D, h.D};
    return {std::min(h.dA, h.dB), std::max(h.dA, h.dB)};
  }

  bool headsMoving() const {
    for (const auto& h : head_)
      if (h.fade || h.D != h.T) return true;
    return false;
  }


  // The hard invariant (D-006): the sounding read speed s = 1 - d(pos)/dt
  // stays in [0.25, 4] at every sample. The law's cap and the modulation's
  // slew limit keep it inside [0.25, 2.25] by construction; this clamp is the
  // backstop, and every hit is counted (the suite requires zero). Also keeps
  // the speed statistics.
  void backstop(int k) {
    Head& h = head_[k];
    SpeedStats& st = stats_[k];
    double pos = (h.fade ? h.dA : h.D) + mods_[k];
    if (h.statsLive) {
      double dp = pos - h.lastPos;
      const double lo = 1.0 - k::kSMax, hi = 1.0 - k::kSMin;
      if (!h.fade && (dp > hi || dp < lo)) {
        const double D = h.lastPos + (dp > hi ? hi : lo) - mods_[k];
        h.V += D - h.D;
        h.D = D;
        tape_[k].c = static_cast<double>(n_) - D;
        ++st.clamps;
        pos = h.D + mods_[k];
        dp = pos - h.lastPos;
      }
      const double s = 1.0 - dp;
      st.sMin = std::min(st.sMin, s);
      st.sMax = std::max(st.sMax, s);
      if (s <= 0.0) ++st.reverse;
    }
    h.lastPos = pos;
    h.statsLive = true;
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
  // The modulation rides on every head of a line, both mid-fade (§7).
  double readHead(int k) {
    const Head& h = head_[k];
    const double m = mods_[k];
    if (!h.fade) return readAt(k, pos(k, (plant::blockSteppedD ? heldD_[k] : h.D) + m));
    const double wF = static_cast<double>(h.fi + 1) / h.fn;
    const double a = readAt(k, pos(k, h.dA + m)), b = readAt(k, pos(k, h.dB + m));
    if (plant::equalPower) return std::cos(wF * k::kPi / 2) * a + std::sin(wF * k::kPi / 2) * b;
    return (1.0 - wF) * a + wF * b;
  }
  // Every read position is held inside the line; an attempt past it is
  // counted (the suite requires zero: the depth limit and the clamp keep it in).
  double pos(int k, double p) {
    if (plant::integerRead) p = std::round(p);
    const double hi = static_cast<double>(lineLen_ - k::kKernelMargin);
    if (p > hi) { ++stats_[k].overruns; return hi; }
    if (p < k::kMinRead) { ++stats_[k].floors; return k::kMinRead; }  // counted: the suite requires zero
    return p;
  }

  std::int64_t readSpan() const {
    double m = 0;
    for (const auto& h : head_) m = std::max({m, h.D, h.fade ? std::max(h.dA, h.dB) : 0.0});
    m += std::max(std::fabs(mods_[0]), std::fabs(mods_[1]));
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
      case kSync: case kTimeMs: case kDiv: case kOffL: case kOffR:
        resolveBoth(bpm_);
        paramMoved_ = true;  // a key move: the next head step latches time.change (§3.5)
        break;
      case kTimeScale: {
        // A scale move is a control move like any time key (§3.5, D-048): the heads
        // follow time.change. But only a send that MOVES a time is a move: a re-sent
        // value, or a synced scale staying inside one octave's snap band, moves
        // nothing, and must not re-latch a tempo move in flight as a key move.
        const double a0 = tT_[0], a1 = tT_[1];
        resolveBoth(bpm_);
        if (plant::scaleBypassesChange) { landHead(0); landHead(1); }  // the plant: a bare jump, no mode consulted
        else if (!plant::scaleMoveSwallowed && (plant::scaleResendIsMove || tT_[0] != a0 || tT_[1] != a1)) paramMoved_ = true;
        break;
      }
      case kModDepth: rDepth_.to(p_[kModDepth] / 1000.0 * sr_, modRampN_); setTail(computeTail()); break;
      case kModSpread: rSpread_.to(p_[kModSpread] / 100.0, modRampN_); break;
      case kModRate: rateTargetL2_ = std::log2(p_[kModRate]); break;
      case kModShape:
        // Blend from wherever the shape stands now (a seeded switch, §9).
        if (p_[kModShape] != shapeTarget_) {
          shapeFrom_ = shapeNow();
          shapeTarget_ = p_[kModShape];
          shapeW_ = plant::shapeInstant ? 1.0 : 0.0;
        }
        break;
      case kModSeed:  // only a CHANGE reseeds: a host re-sending the value must not freeze the drift (critic M2-H2)
        if (plant::seedResendRestarts || p_[kModSeed] != seedApplied_) { reseed(true); seedApplied_ = p_[kModSeed]; }
        break;
      case kRoute: switchRouting(static_cast<int>(p_[kRoute])); break;
      case kInOn: switchFilter(fIn_, p_[kInOn]); break;
      case kLoopOn: switchFilter(fLoop_, p_[kLoopOn]); break;
      case kOutOn: switchFilter(fOut_, p_[kOutOn]); break;
      case kInHp: case kInLp: fIn_.settled = false; break;
      case kLoopHp: case kLoopLp: fLoop_.settled = false; break;
      case kOutHp: case kOutLp: fOut_.settled = false; break;
      default: break;  // time.change / law / tempoMode: deferred (§3.5); mod.rateHz glides in modStep
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
      if (plant::prepareRampsIn) {
        // B435 test 3's fault: the gains never OPEN at their value; every landing
        // (prepare -> reset, and rest) ramps them in from unity instead. Planted
        // here, not in prepare(): a fresh instance is at rest, and the rest
        // landing on its first sample would mask a ramp-in set up only there.
        rIn_.cur = rOut_.cur = 1.0;
        rIn_.to(gainTarget(kInGain), rampN_);
        rOut_.to(gainTarget(kOutGain), rampN_);
      } else {
        rIn_.target = gainTarget(kInGain); rIn_.snap();
        rOut_.target = gainTarget(kOutGain); rOut_.snap();
      }
    }
    rMix_.target = p_[kMix] / 100.0; rMix_.snap();
    rFb_.target = fbTarget(); rFb_.snap();
    rDepth_.target = p_[kModDepth] / 1000.0 * sr_; rDepth_.snap();
    rSpread_.target = p_[kModSpread] / 100.0; rSpread_.snap();
    rateL2_ = rateTargetL2_ = std::log2(p_[kModRate]);
    rateF_ = p_[kModRate];
    shapeW_ = 1.0;
    shapeFrom_ = shapeTarget_ = p_[kModShape];
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
  // The modulation restarts from the patch's seed and phase 0, so a loaded
  // preset's drift is derived from mod.seed alone whatever played before
  // (D-026; the old audio has faded, so nothing audible steps).
  void adoptPatch() {
    snapAll(false);
    resolveBoth(bpm_);
    if (!plant::loadKeepsModState) {
      modPhase_ = 0.0;
      reseed(false);
      seedApplied_ = p_[kModSeed];
    }
  }

  // One resting sample. Everything lands ONCE per change (critic M4: snapping
  // every sample made resting cost more CPU than running): on entering rest and
  // after any setParam / load while resting. The tempo and heads land too.
  void atRest(double bpmEnd) {
    bool land = false;
    if (plant::restRamps) { tickRamps(); tickFilters(); tickRoute(); }
    else if (restDirty_) { snapAll(true); restDirty_ = false; land = true; }
    if (!plant::tempoGlidesAtRest) {
      const bool moved = bpm_ != bpmEnd;
      bpm_ = bpmPrev_ = bpmEnd;
      if (moved) resolveBoth(bpmEnd);
    }
    for (int k = 0; k < 2; ++k) {
      if (head_[k].D != tT_[k] || head_[k].fade) { landHead(k); land = true; }
      head_[k].statsLive = false;  // nothing sounds at rest: no speed across a wake
    }
    // The phase and the drift HOLD (§10), and the offset LANDS on the curve
    // they define, so the first repeat after a wake plays at speed 1, not
    // slewing in from 0 (critic M2-M4: it played 39 ms at 0.75).
    if (land && !plant::wakeOffsetFromZero) modTargets(shapeNow(), mods_);
    else if (plant::wakeOffsetFromZero) mods_[0] = mods_[1] = 0.0;
    paramMoved_ = false;
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
  bool fadeLands_ = false;       // the running fade ends by adopting a patch (a load); false for a Clear alone
  double dryPrev_[2] = {0.0, 0.0};  // the dryLate plant's one-sample delay (test-only)
  std::array<ActionEvent, kMaxActions> actions_{};  // the next block's actions, in offset order
  int actionsN_ = 0, actionsDone_ = 0;
  bool idle_ = true;
  bool restDirty_ = true;        // a change arrived while resting: land it once
  std::int64_t silentRun_ = 0, quietRun_ = 0;
  std::int64_t tailClock_ = 0;   // D-019
  int clearW_ = 0;               // the write index at the last logical clear
  int sinceClear_ = 0;           // samples written since it, saturating at lineLen_
  std::uint64_t fadeCount_[2] = {0, 0};
  // M2
  std::int64_t n_ = 0;            // samples processed since reset (the tape's clock)
  bool paramMoved_ = false;
  double up_ = 1.0, dn_ = 0.5, capUp_ = 0.0, capDn_ = 0.0;
  Tape tape_[2];
  std::array<std::vector<double>, 2> tapeRing_;
  int tapeCap_ = 0;
  SpeedStats stats_[2];
  double blockV_[2] = {0.0, 0.0};  // expSlewBlockStart plant only: each head's speed at the block's first sample
  Ramp rDepth_, rSpread_;
  int modRampN_ = 12000;
  double rateA_ = 0.0, rateL2_ = 0.0, rateTargetL2_ = 0.0, rateF_ = 1.0;
  double tapeTau_ = -1.0, tapeA_ = 0.0;
  double seedApplied_ = -1.0;
  double heldD_[2] = {0.0, 0.0};  // BLOCK_STEPPED_D plant only
  double modPhase_ = 0.0;
  double shapeW_ = 1.0, shapeFrom_ = 0.0, shapeTarget_ = 0.0;
  Drift drift_[2];
  double mods_[2] = {0.0, 0.0};
  std::atomic<std::uint64_t> nonFinite_{0};
  std::atomic<bool> nonFiniteLatch_{false};
  Tap tapIn_, tapOut_;
  state::Values staged_{};
  std::atomic<bool> stagedReady_{false};
};

// The ABI's checkable half (T-013): a build that drifts from the vendored
// header's concepts fails here, in every includer, not in a test.
static_assert(foundations::FxOperatorLifecycle<Echo>, "echo::Echo must model FOUNDATIONS' FxOperatorLifecycle");
static_assert(foundations::FxOperatorParams<Echo>, "echo::Echo must model FOUNDATIONS' FxOperatorParams");

}  // namespace echo

#if !defined(ECHO_FP_PRAGMA_OFF)
#if defined(__clang__)
#pragma STDC FP_CONTRACT DEFAULT
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif
#endif
