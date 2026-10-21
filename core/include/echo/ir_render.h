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

namespace echo {

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
  bool render(const IrRequest& r, std::span<double> outL, std::span<double> outR, IrInfo* info = nullptr) {
    if (!preparedFor(r.sampleRate) || r.length < 0 || static_cast<std::int64_t>(outL.size()) < r.length ||
        static_cast<std::int64_t>(outR.size()) < r.length)
      return false;
    if (plant::irAllocsInRender) {  // the planted allocation (the service, T-031, preallocates everything)
      std::vector<double> scratch(8, 0.0);
      if (!outL.empty()) outL[0] = scratch[0];
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
    const double a = plant::irLevelIgnored ? 1.0 : std::pow(10.0, r.levelDb / 20.0);
    std::uint32_t rng = r.noiseSeed;
    // The input for sample i of this render. Impulses sit at sample 0.
    auto input = [&](std::int64_t i, double& l, double& rr) {
      l = rr = 0.0;
      switch (r.input) {
        case IrInput::Mono: if (i == 0) l = rr = a; break;
        case IrInput::Left: if (i == 0) l = a; break;
        case IrInput::Right: if (i == 0) rr = a; break;
        case IrInput::SideOnly: if (i == 0) { l = a; rr = -a; } break;
        case IrInput::Noise:
          if (i < r.noiseLength) { l = a * mulberry(rng); rr = a * mulberry(rng); }  // decorrelated: alternate draws
          break;
      }
    };
    IrInfo inf;
    std::array<double, kIrBlock> inL{}, inR{};
    for (std::int64_t pos = 0; pos < r.length;) {
      const int n = static_cast<int>(std::min<std::int64_t>(kIrBlock, r.length - pos));
      bool any = false;
      for (int i = 0; i < n; ++i) {
        input(pos + i, inL[static_cast<std::size_t>(i)], inR[static_cast<std::size_t>(i)]);
        any = any || inL[static_cast<std::size_t>(i)] != 0.0 || inR[static_cast<std::size_t>(i)] != 0.0;
      }
      // Once the clone rests and no input remains, every later sample is exact
      // zero by construction (SPEC §10: silence in, silence out at rest), so the
      // render stops early and zero-fills. The output equals the full render.
      if (!any && e_.atRest() && pos > 0 && (r.input != IrInput::Noise || pos >= r.noiseLength)) {
        if (inf.restAt < 0) inf.restAt = pos;
        for (std::int64_t i = pos; i < r.length; ++i) outL[static_cast<std::size_t>(i)] = outR[static_cast<std::size_t>(i)] = 0.0;
        break;
      }
      e_.process(inL.data(), inR.data(), outL.data() + pos, outR.data() + pos, n, r.bpm);
      if (pos == 0) inf.tail = e_.tail();  // the patch has landed by the end of the first block
      pos += n;
    }
    if (!plant::irRestAtMissesEnd && inf.restAt < 0 && e_.atRest()) inf.restAt = r.length;  // rested inside the last block (critic T-030 M1)
    if (info) *info = inf;
    return true;
  }

 private:
  // mulberry32 (the project's one generator), here for the noise input only.
  // This file sits outside engine.h's fp-contract pragma: `q * 2.0 - 1.0` is
  // safe under contraction only because x2 is exact (fused or not, one
  // rounding). Keep every scale here a power of two, or scope the pragma.
  static double mulberry(std::uint32_t& s) {
    std::uint32_t t = (s += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + ((t ^ (t >> 7)) * (t | 61u));
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0 * 2.0 - 1.0;
  }

  Echo e_;
  double sr_ = 0.0;
  bool prepared_ = false;
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
