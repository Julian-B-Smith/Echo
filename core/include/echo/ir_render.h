// echo/ir_render.h — THE offline impulse-response renderer (D-011; D-029 as
// amended by D-030; ROADMAP T-030).
//
// ONE RENDERER, THREE CONSUMERS. The visualiser draws what this renders, the
// tail oracle reads its exact-zero point, and the B408 mono-fold metric (M3's
// T-019, D-010) measures through it, so what is seen is what is gated. Nothing
// else in Echo may render an IR "its own way": a second path is how the drawing
// and the gate drift apart.
//
// A CLONE, NEVER A PROBE. The renderer owns a private echo::Echo, prepared at
// the host rate, and drives it through the PUBLIC API only (reset, setParam,
// process). It never reads a live instance's buffers: probing the live line
// would disturb it and is not deterministic (D-011's rejected option), and the
// GUI may only read published atomics anyway (rack-slot contract §8). The patch
// arrives as plain values copied on the UI side.
//
// WHY setParam THEN reset(), NOT A FRESH INSTANCE PER RENDER. prepare()
// allocates and clears ~18 MiB at 192 kHz; reset() is bit-identical to a fresh
// instance (contract A2), so one prepared clone serves every render at a rate.
// Every key, Device keys included, is STORED first (setParam), then reset()
// lands them all and restarts the modulation from the stored mod.seed at phase
// 0. The order is load-bearing (critic T-030 H1): setParam(mod.seed) AFTER a
// reset reseeds only on a change and blends from the CURRENT drift stream
// (D-026's knob path), so the drawing would depend on whatever was rendered
// before, the cache (keyed by the request alone) would serve a history-
// dependent curve, and two identical requests would differ.
// So the response drawn is the patch's response AS HEARD RIGHT AFTER LOADING IT
// (D-026: a load restarts the modulation from the patch's seed alone; D-031):
// the heads on their resolved times, every ramp landed, the modulation at
// phase 0 from its seed. Only that response is a function of the patch.
//
// THE STATED LEVEL. The delay is linear until the ceiling engages (knee 0.9 on
// the returning term, SPEC §5.1), so the response depends on the input level.
// Every render states its level; kIrLevelDb is the display's default
// (-20 dBFS: below unity feedback, |H_loop| <= 1 and the DC blocker's
// overshoot < 2 keep every returning term under 0.2, far from the knee).
// Whether the ceiling engaged in a render is M4's T-033 (an engine count), not
// guessed here.
//
// THREADING. Not real-time and not thread-safe: one IrRenderer per thread. It
// allocates in prepare() (and renderIr() allocates its result); it must never
// run on the audio thread (M4 gate 3, T-031's service owns the worker).
//
// PURE. No clock, no global state, no randomness but seeded mulberry32 (the
// noise input). Same request, same input -> bit-identical output.
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include "echo/engine.h"
#include "echo/params.h"
#include "echo/plants.h"

// No fused multiply-add here either (CLAUDE.md §Domain): scoped exactly as
// engine.h scopes it, and restored at the end of the file, so the spectra,
// the EDC and the renderer's own arithmetic contract no a*b+c into one
// rounding in any build. (libm's pow, log10, sin and cos may still differ
// between platforms: T-034 compares the wasm build's output with the native
// one rather than assuming it.) Clang's closing DEFAULT also resets an
// includer's own OFF: engine.h does the same.
#if !defined(ECHO_FP_PRAGMA_OFF)
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif
#endif

namespace echo {

// Where the planted allocation's pointer escapes (irAllocsInRender, test-only).
// An allocation whose memory is never observed may be ELIDED by the optimiser
// (C++14 [expr.new]; libstdc++'s allocator permits it): GCC did so on Linux CI
// once batch 2 changed the inlining, and the plant went vacuous (2026-10-24).
// A store through a volatile pointer is an observable side effect.
inline const double* volatile g_irPlantSink = nullptr;

inline constexpr double kIrLevelDb = -20.0;  // the display's stated input level
inline constexpr int kIrBlock = 512;          // render block (any block renders bit-identically)

// Every value the clone needs, copied from the host's parameter model: all 29
// keys in params.h order, Device keys (the I/O gains) included.
using IrPatch = std::array<double, kNumParams>;

// The manifest defaults, the starting point for a patch built key by key.
inline IrPatch irDefaults() {
  IrPatch p{};
  for (std::size_t i = 0; i < kParams.size(); ++i) p[i] = kParams[i].def;
  return p;
}

// What is fed in. The display draws the Mono impulse (a centred source); the
// mono-fold metric (D-010) also needs the S-only impulse and seeded,
// decorrelated noise; L and R alone show a routing's crossing.
enum class IrInput : std::uint8_t { Mono, Left, Right, SideOnly, Noise };

struct IrRequest {
  IrPatch patch = irDefaults();
  double sampleRate = 48000.0;
  double bpm = 120.0;              // the host tempo, held for the whole render
  double levelDb = kIrLevelDb;     // the impulse's peak (or the noise's peak), dBFS
  std::int64_t length = 0;         // samples to render
  IrInput input = IrInput::Mono;
  std::uint32_t noiseSeed = 1;     // IrInput::Noise only
  std::int64_t noiseLength = 0;    // IrInput::Noise only: the burst's length in samples
};

// What a render reports beside the samples.
struct IrInfo {
  std::int64_t tail = 0;           // the clone's declared tail(), in samples, once the patch landed
  // A block boundary (a multiple of kIrBlock, or the length) at which the
  // clone was found at rest: the output is exact zero from there on. Block-
  // quantised, so up to kIrBlock - 1 samples late; -1: not at rest by the end.
  std::int64_t restAt = -1;
  std::array<double, 2> times{};   // the clone's resolved L/R times in seconds (generation spacing, T-032)
};

// The cache key (T-031): FNV-1a 64 over the bit pattern of every input the
// render reads. A key the hash skips would serve a stale drawing after that key
// changes (plant irHashSkipsKey), so the walk is over the whole patch array.
// Bit patterns, not values: -0.0 and 0.0 hash apart, which only costs a miss.
inline std::uint64_t irHash(const IrRequest& r) {
  std::uint64_t h = 0xcbf29ce484222325ull;
  auto mix = [&h](std::uint64_t v) {
    for (int b = 0; b < 8; ++b) { h ^= (v >> (8 * b)) & 0xffu; h *= 0x100000001b3ull; }
  };
  auto bits = [](double d) { return std::bit_cast<std::uint64_t>(d); };
  const std::size_t keys = plant::irHashSkipsKey ? r.patch.size() - 1 : r.patch.size();
  for (std::size_t i = 0; i < keys; ++i) mix(bits(r.patch[i]));
  mix(bits(r.sampleRate));
  mix(bits(r.bpm));
  mix(bits(r.levelDb));
  mix(static_cast<std::uint64_t>(r.length));
  mix(static_cast<std::uint64_t>(r.input));
  if (r.input == IrInput::Noise) {  // seed and burst matter only to the noise input
    mix(r.noiseSeed);
    mix(static_cast<std::uint64_t>(r.noiseLength));
  }
  return h;
}

class IrRenderer {
 public:
  // Allocates the clone's lines for this rate (the only allocating call). Call
  // again only when the rate changes.
  void prepare(double sampleRate) {
    e_.prepare(sampleRate, kIrBlock);
    sr_ = sampleRate;
    prepared_ = true;
  }
  // The engine clamps the rate to 8 kHz .. 768 kHz (and a non-finite one to
  // 48 kHz); a request it did not keep is refused, not rendered at another
  // rate under the requested rate's hash (critic T-030 M3: a 4 kHz request
  // rendered at 8 kHz would put T-032's tempo grid off by 2x).
  bool preparedFor(double sampleRate) const { return prepared_ && sr_ == sampleRate && (plant::irAcceptsClampedRate || e_.sampleRate() == sampleRate); }

  // Renders r.length samples into outL / outR (each at least r.length long).
  // Allocation-free once prepared. Returns false, writing nothing, if the
  // renderer is unprepared for r.sampleRate, an output is too short, or a
  // patch value is refused by the engine (a host bug made visible).
  // The one-shot form of start() + renderNext(r.length).
  bool render(const IrRequest& r, std::span<double> outL, std::span<double> outR, IrInfo* info = nullptr) {
    if (static_cast<std::int64_t>(outL.size()) < r.length || static_cast<std::int64_t>(outR.size()) < r.length) return false;
    if (!start(r)) return false;
    renderNext(outL, outR, r.length);
    if (info) *info = info_;
    return true;
  }

  // ---- the resumable form (T-031: head-first frames extend ONE render) ----
  // start() lands the patch; each renderNext(outL, outR, upTo) renders the
  // samples [position(), min(upTo, length)) into outL / outR at their ABSOLUTE
  // indices (the buffers hold the whole render). Internal blocks never cross a
  // multiple of kIrBlock and the rest check runs only there, so ANY split
  // renders bit-identically to the one-shot render, restAt included (critic
  // T-030 M2: re-rendering the head for every frame would cost the 100 ms).
  // Allocation-free. Returns false if the request is refused.
  bool start(const IrRequest& r) {
    active_ = false;
    if (!preparedFor(r.sampleRate) || r.length < 0) return false;
    if (plant::irAllocsInRender) {  // the planted allocation (the service, T-031, preallocates everything)
      std::vector<double> scratch(8, 0.0);
      g_irPlantSink = scratch.data();
      a_ = scratch[0];
    }
    if (plant::irSeedAfterReset) e_.reset();  // the pre-critic order
    for (int k = 0; k < kNumParams; ++k) {
      const auto& def = kParams[static_cast<std::size_t>(k)];
      if (plant::irDropsDevice && def.morph == Morph::Device) continue;
      double v = r.patch[static_cast<std::size_t>(k)];
      if (plant::irSkipsOutputFilter && def.key == "filt.out.on") v = 0.0;
      if (!e_.setParam(k, v)) return false;
    }
    if (!plant::irReusesState && !plant::irSeedAfterReset) e_.reset();  // AFTER the values: see "WHY setParam THEN reset()" above
    r_ = r;
    a_ = plant::irLevelIgnored ? 1.0 : std::pow(10.0, r.levelDb / 20.0);
    rng_ = r.noiseSeed;
    pos_ = 0;
    resting_ = false;
    info_ = IrInfo{};
    active_ = true;
    return true;
  }
  std::int64_t position() const { return pos_; }
  bool done() const { return active_ && pos_ >= r_.length; }
  const IrInfo& info() const { return info_; }

  // Returns the new position.
  std::int64_t renderNext(std::span<double> outL, std::span<double> outR, std::int64_t upTo) {
    if (!active_) return pos_;
    const auto room = static_cast<std::int64_t>(std::min(outL.size(), outR.size()));
    const std::int64_t end = std::min({upTo, r_.length, room});
    if (plant::irChunkResetsNoise) rng_ = r_.noiseSeed;
    std::array<double, kIrBlock> inL{}, inR{};
    while (pos_ < end) {
      if (resting_) {  // at rest with no input left: exact zero by construction (SPEC §10)
        for (std::int64_t i = pos_; i < end; ++i) outL[static_cast<std::size_t>(i)] = outR[static_cast<std::size_t>(i)] = 0.0;
        pos_ = end;
        break;
      }
      const bool boundary = pos_ % kIrBlock == 0;
      const int n = static_cast<int>(std::min<std::int64_t>(kIrBlock - pos_ % kIrBlock, end - pos_));
      bool any = false;
      for (int i = 0; i < n; ++i) {
        input(pos_ + i, inL[static_cast<std::size_t>(i)], inR[static_cast<std::size_t>(i)]);
        any = any || inL[static_cast<std::size_t>(i)] != 0.0 || inR[static_cast<std::size_t>(i)] != 0.0;
      }
      // Once the clone rests and no input remains, every later sample is exact
      // zero by construction (SPEC §10: silence in, silence out at rest), so the
      // render stops processing and zero-fills. The output equals the full render.
      if (boundary && !any && e_.atRest() && pos_ > 0 && (r_.input != IrInput::Noise || pos_ >= r_.noiseLength)) {
        resting_ = true;
        if (info_.restAt < 0) info_.restAt = pos_;
        continue;
      }
      e_.process(inL.data(), inR.data(), outL.data() + pos_, outR.data() + pos_, n, r_.bpm);
      if (pos_ == 0) {  // the patch has landed by the end of the first block
        info_.tail = e_.tail();
        info_.times = e_.resolvedTimes();
      }
      pos_ += n;
    }
    if (pos_ >= r_.length && !plant::irRestAtMissesEnd && info_.restAt < 0 && e_.atRest())
      info_.restAt = r_.length;  // rested inside the last block (critic T-030 M1)
    return pos_;
  }

 private:
  // mulberry32 (the project's one generator), here for the noise input only.
  static double mulberry(std::uint32_t& s) {
    std::uint32_t t = (s += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + ((t ^ (t >> 7)) * (t | 61u));
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0 * 2.0 - 1.0;
  }

  // The input for sample i of the render. Impulses sit at sample 0; the noise
  // draws in order, so it is only ever asked for i = position() onward.
  void input(std::int64_t i, double& l, double& rr) {
    l = rr = 0.0;
    switch (r_.input) {
      case IrInput::Mono: if (i == 0) l = rr = a_; break;
      case IrInput::Left: if (i == 0) l = a_; break;
      case IrInput::Right: if (i == 0) rr = a_; break;
      case IrInput::SideOnly: if (i == 0) { l = a_; rr = -a_; } break;
      case IrInput::Noise:
        if (i < r_.noiseLength) { l = a_ * mulberry(rng_); rr = a_ * mulberry(rng_); }  // decorrelated: alternate draws
        break;
    }
  }

  Echo e_;
  double sr_ = 0.0;
  bool prepared_ = false;
  // The render in flight (start / renderNext).
  IrRequest r_;
  double a_ = 0.0;
  std::uint32_t rng_ = 1;
  std::int64_t pos_ = 0;
  bool resting_ = false, active_ = false;
  IrInfo info_;
};

// One-shot convenience for tools and tests (T-019's report, the oracles):
// prepares a renderer, renders, returns the buffers. Allocates; never on the
// audio thread. Empty buffers on a refused request.
struct IrResult {
  std::vector<double> L, R;
  IrInfo info;
  bool ok = false;
};
inline IrResult renderIr(const IrRequest& r) {
  IrResult o;
  if (r.length < 0) return o;
  o.L.assign(static_cast<std::size_t>(r.length), 0.0);
  o.R.assign(static_cast<std::size_t>(r.length), 0.0);
  IrRenderer ren;
  ren.prepare(r.sampleRate);
  o.ok = ren.render(r, o.L, o.R, &o.info);
  if (!o.ok) o.L.clear(), o.R.clear();
  return o;
}

}  // namespace echo

#if !defined(ECHO_FP_PRAGMA_OFF)
#if defined(__clang__)
#pragma STDC FP_CONTRACT DEFAULT
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif
#endif
