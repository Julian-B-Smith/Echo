// ir_tests.cpp — M4's suite: the one offline IR renderer (ROADMAP T-030; D-011,
// D-029 as amended by D-030). Later M4 items (the service, the display model,
// the snapshot label) add their checks here.
//
// ONE SOURCE, TWO BINARIES (core/CMakeLists.txt), the same shape as
// core_tests.cpp, kept in its own file so M4 never edits M3's suite (D-030):
//   echo_ir_tests   built WITHOUT ECHO_PLANTS: every check must PASS on exactly
//                   the code horde links.
//   echo_ir_plants  built WITH ECHO_PLANTS: each check runs clean (must pass),
//                   then once per planted fault it guards (must FAIL). The M4
//                   plants and the checks must cover each other.
//
// THE REFERENCE IS INDEPENDENT OF THE RENDERER. `plain()` below is what a host
// does when the user loads the patch: a fresh, separately prepared Echo, the
// patch keys by NAME through loadState (the D-026 load path), the Device keys
// (the I/O gains, which a load never writes) by setParam, the stated input
// built here, rendered in 333-sample blocks (the renderer uses 512; block
// independence is the core suite's check). The renderer reaches the same state
// by another route (setParam then reset), so a renderer that resets wrongly,
// drops a key, mis-scales the input or rearranges blocks cannot agree with it
// by sharing the fault. (The first reference set keys on a fresh instance by
// setParam, the renderer's own route, and so shared critic T-030 H1.)
#include "alloc_probe.h"  // first: replaces operator new for this executable

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <thread>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "echo/engine.h"
#include "echo/ir_render.h"
#include "echo/ir_service.h"
#include "echo/ir_view.h"

using echo::Echo;
using echo::IrInput;
using echo::IrRequest;
using Vec = std::vector<double>;

namespace {

using Detail = std::string;
#define REQUIRE(cond, ...)                       \
  do {                                           \
    if (!(cond)) {                               \
      char b_[300];                              \
      std::snprintf(b_, sizeof b_, __VA_ARGS__); \
      d = b_;                                    \
      return false;                              \
    }                                            \
  } while (0)

using Kv = std::vector<std::pair<const char*, double>>;

// A patch as the host's model holds it: the defaults, then the named keys.
echo::IrPatch patchOf(const Kv& kv) {
  auto p = echo::irDefaults();
  for (const auto& [k, v] : kv) p[static_cast<std::size_t>(*echo::paramIndex(k))] = v;
  return p;
}
IrRequest req(const Kv& kv, long n, double sr = 48000.0, IrInput in = IrInput::Mono, double bpm = 120.0) {
  IrRequest r;
  r.patch = patchOf(kv);
  r.sampleRate = sr;
  r.bpm = bpm;
  r.length = n;
  r.input = in;
  r.noiseSeed = 4242;
  r.noiseLength = in == IrInput::Noise ? std::lround(0.05 * sr) : 0;
  return r;
}

// mulberry32, written out again on purpose: the reference's noise must not
// borrow the renderer's generator.
double refNoise(std::uint32_t& s) {
  std::uint32_t t = (s += 0x6D2B79F5u);
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + ((t ^ (t >> 7)) * (t | 61u));
  return static_cast<double>(t ^ (t >> 14)) / 4294967296.0 * 2.0 - 1.0;
}

struct Out {
  Vec L, R;
};

// The independent reference render (see the header comment).
Out plain(const Kv& kv, const IrRequest& r) {
  Echo e;
  e.prepare(r.sampleRate, 4096);
  std::vector<echo::KeyValue> load;
  for (const auto& [k, v] : kv) {
    const auto i = *echo::paramIndex(k);
    if (echo::kParams[static_cast<std::size_t>(i)].morph == echo::Morph::Device) {
      if (!e.setParam(k, v)) { std::fprintf(stderr, "fixture: setParam(%s, %g) refused\n", k, v); std::abort(); }
    } else {
      load.push_back({k, v});
    }
  }
  if (!e.loadState(load)) { std::fprintf(stderr, "fixture: loadState refused\n"); std::abort(); }
  const long n = static_cast<long>(r.length);
  Vec inL(static_cast<std::size_t>(n), 0.0), inR(static_cast<std::size_t>(n), 0.0);
  const double a = std::pow(10.0, r.levelDb / 20.0);
  std::uint32_t s = r.noiseSeed;
  if (n > 0) switch (r.input) {
      case IrInput::Mono: inL[0] = inR[0] = a; break;
      case IrInput::Left: inL[0] = a; break;
      case IrInput::Right: inR[0] = a; break;
      case IrInput::SideOnly: inL[0] = a; inR[0] = -a; break;
      case IrInput::Noise:
        for (long i = 0; i < r.noiseLength && i < n; ++i) {
          inL[static_cast<std::size_t>(i)] = a * refNoise(s);
          inR[static_cast<std::size_t>(i)] = a * refNoise(s);
        }
        break;
    }
  Out o{Vec(static_cast<std::size_t>(n)), Vec(static_cast<std::size_t>(n))};
  for (long p = 0; p < n; p += 333) {
    const int len = static_cast<int>(std::min<long>(333, n - p));
    const auto u = static_cast<std::size_t>(p);
    e.process(inL.data() + u, inR.data() + u, o.L.data() + u, o.R.data() + u, len, r.bpm);
  }
  return o;
}

// The first sample where two renders differ (-1: bit-identical).
long firstDiff(const Vec& a, const Vec& b) {
  if (a.size() != b.size()) return 0;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (std::bit_cast<std::uint64_t>(a[i]) != std::bit_cast<std::uint64_t>(b[i])) return static_cast<long>(i);
  return -1;
}
double peak(const Vec& v, long from = 0) {
  double m = 0;
  for (std::size_t i = static_cast<std::size_t>(from); i < v.size(); ++i) m = std::max(m, std::fabs(v[i]));
  return m;
}

// The twelve patches T-030 names: every routing, every filter position,
// feedback 0 / 50 / 99 / 120 %, free and synced time, sine and drift
// modulation, the I/O gains.
struct Named {
  const char* name;
  Kv kv;
};
const std::vector<Named> kPatches = {
    {"defaults (synced, loop filter on)", {}},
    {"free 250 ms, fb 0, filters off", {{"time.sync", 0}, {"time.ms", 250}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}},
    {"ping-pong, fb 50, offsets", {{"time.sync", 0}, {"time.ms", 300}, {"fb.amount", 50}, {"route.mode", 1}, {"time.offL", 10}, {"time.offR", -20}}},
    {"mid/side, fb 99", {{"time.sync", 0}, {"time.ms", 180}, {"fb.amount", 99}, {"route.mode", 2}, {"mix", 100}}},
    {"past unity, fb 120", {{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 120}, {"mix", 100}}},
    {"input filter", {{"time.sync", 0}, {"time.ms", 200}, {"filt.in.on", 1}, {"filt.in.hpHz", 300}, {"filt.in.lpHz", 3000}}},
    {"output filter", {{"time.sync", 0}, {"time.ms", 200}, {"filt.out.on", 1}, {"filt.out.hpHz", 200}, {"filt.out.lpHz", 5000}}},
    {"loop filter, fb 90", {{"time.sync", 0}, {"time.ms", 120}, {"fb.amount", 90}, {"filt.loop.hpHz", 500}, {"filt.loop.lpHz", 2000}}},
    {"I/O gains +6 / -12 dB", {{"time.sync", 0}, {"time.ms", 150}, {"io.inGain", 6}, {"io.outGain", -12}, {"fb.amount", 50}}},
    {"sine modulation", {{"time.sync", 0}, {"time.ms", 220}, {"fb.amount", 60}, {"mod.depthMs", 3}, {"mod.rateHz", 2}, {"mod.spread", 50}}},
    {"drift modulation", {{"time.sync", 0}, {"time.ms", 160}, {"fb.amount", 70}, {"mod.shape", 1}, {"mod.depthMs", 5}, {"mod.seed", 777}}},
    {"synced 1/8 dotted at 93 bpm, exp law", {{"time.div", 10}, {"time.offR", 25}, {"time.change", 1}, {"time.law", 1}, {"fb.amount", 40}}},
};

// ---- the checks ---------------------------------------------------------------

// T-030 criterion 2 (and T-019's renderer criterion): the renderer's output IS
// the engine's offline render, bit for bit, for every patch; at three rates for
// a subset; and for the inputs the mono-fold metric needs (D-010).
bool equalsPlainRender(Detail& d) {
  for (const auto& p : kPatches) {
    const double bpm = std::string_view(p.name).find("93 bpm") != std::string_view::npos ? 93.0 : 120.0;
    const auto r = req(p.kv, 96000, 48000.0, IrInput::Mono, bpm);
    const auto got = echo::renderIr(r);
    REQUIRE(got.ok, "%s: the renderer refused the request", p.name);
    const auto ref = plain(p.kv, r);
    REQUIRE(peak(ref.L) > 1e-4, "control: %s renders silence", p.name);
    long i = firstDiff(got.L, ref.L), j = firstDiff(got.R, ref.R);
    REQUIRE(i < 0 && j < 0, "%s: differs from the plain render at sample %ld (L %.17g vs %.17g)", p.name, i < 0 ? j : i,
            got.L[static_cast<std::size_t>(i < 0 ? j : i)], ref.L[static_cast<std::size_t>(i < 0 ? j : i)]);
  }
  for (double sr : {44100.0, 192000.0})
    for (std::size_t k : {2u, 6u, 9u}) {
      const auto r = req(kPatches[k].kv, std::lround(0.6 * sr), sr);
      const auto got = echo::renderIr(r);
      const auto ref = plain(kPatches[k].kv, r);
      REQUIRE(got.ok && firstDiff(got.L, ref.L) < 0 && firstDiff(got.R, ref.R) < 0, "%s at %.0f Hz differs from the plain render", kPatches[k].name, sr);
    }
  for (IrInput in : {IrInput::Left, IrInput::Right, IrInput::SideOnly, IrInput::Noise})
    for (std::size_t k : {2u, 3u}) {
      const auto r = req(kPatches[k].kv, 48000, 48000.0, in);
      const auto got = echo::renderIr(r);
      const auto ref = plain(kPatches[k].kv, r);
      REQUIRE(got.ok && firstDiff(got.L, ref.L) < 0 && firstDiff(got.R, ref.R) < 0, "%s, input %d: differs from the plain render", kPatches[k].name,
              static_cast<int>(in));
    }
  return true;
}

// One prepared renderer serves every render at a rate (T-031 keeps one per
// worker): a render after any other equals a fresh renderer's, and after a
// long ringing patch nothing of it leaks into the next. The drift patches are
// rendered after themselves and after a drift patch with another seed (critic
// T-030 H1: with the values sent after the reset, the curve followed the last
// render's seed from sample 7460 on).
bool reuseIsFresh(Detail& d) {
  echo::IrRenderer ren;
  ren.prepare(48000.0);
  Vec L(96000), R(96000);
  const Kv otherSeed{{"time.sync", 0}, {"time.ms", 160}, {"fb.amount", 70}, {"mod.shape", 1}, {"mod.depthMs", 5}, {"mod.seed", 500}};
  for (const Kv& first : {kPatches[3].kv, kPatches[4].kv, kPatches[10].kv, otherSeed})
    for (std::size_t b : {1u, 8u, 0u, 9u, 10u}) {
      REQUIRE(ren.render(req(first, 96000), L, R), "render refused");
      const auto r = req(kPatches[b].kv, 96000);
      REQUIRE(ren.render(r, L, R), "render refused");
      const auto fresh = echo::renderIr(r);
      REQUIRE(firstDiff(L, fresh.L) < 0 && firstDiff(R, fresh.R) < 0, "'%s' after another patch differs from a fresh render at sample %ld",
              kPatches[b].name, firstDiff(L, fresh.L) < 0 ? firstDiff(R, fresh.R) : firstDiff(L, fresh.L));
    }
  return true;
}

// The cache key (T-030 criterion 3): changing any one of the 29 keys, the rate,
// the tempo, the level, the length or the input changes the hash; an equal
// request hashes equal.
bool hashCoversEveryInput(Detail& d) {
  const auto base = req({{"time.sync", 0}}, 48000);
  const auto h0 = echo::irHash(base);
  REQUIRE(echo::irHash(base) == h0, "the same request hashed twice differently");
  for (std::size_t i = 0; i < echo::kParams.size(); ++i) {
    auto r = base;
    const auto& def = echo::kParams[i];
    r.patch[i] = r.patch[i] == def.max ? def.min : def.max;
    REQUIRE(echo::irHash(r) != h0, "changing %.*s leaves the hash unchanged: a stale drawing would be served",
            static_cast<int>(def.key.size()), def.key.data());
  }
  auto r = base;
  r.sampleRate = 96000.0;
  REQUIRE(echo::irHash(r) != h0, "the rate is not in the hash");
  r = base, r.bpm = 121.0;
  REQUIRE(echo::irHash(r) != h0, "the tempo is not in the hash");
  r = base, r.levelDb = -30.0;
  REQUIRE(echo::irHash(r) != h0, "the level is not in the hash");
  r = base, r.length = 48001;
  REQUIRE(echo::irHash(r) != h0, "the length is not in the hash");
  r = base, r.input = IrInput::SideOnly;
  REQUIRE(echo::irHash(r) != h0, "the input is not in the hash");
  const auto noise = req({{"time.sync", 0}}, 48000, 48000.0, IrInput::Noise);
  const auto hn = echo::irHash(noise);
  r = noise, r.noiseSeed += 1;
  REQUIRE(echo::irHash(r) != hn, "the noise seed is not in the hash (critic T-030 M3)");
  r = noise, r.noiseLength += 1;
  REQUIRE(echo::irHash(r) != hn, "the noise length is not in the hash");
  return true;
}

// The tail oracle through the renderer (T-030 criterion 4; RS-7.3-7.6): from
// the declared tail on the render is exact zero, and it was audible before;
// a held loop declares kInfiniteTail and never rests.
bool tailThroughRenderer(Detail& d) {
  for (double fb : {0.0, 50.0, 90.0}) {
    const Kv kv{{"time.sync", 0}, {"time.ms", 50}, {"fb.amount", fb}, {"mix", 100}};
    auto probe = echo::renderIr(req(kv, 512));
    const long T = static_cast<long>(probe.info.tail);
    const auto got = echo::renderIr(req(kv, T + 24000));
    REQUIRE(got.ok && T > 0, "fb %.0f %%: no declared tail", fb);
    REQUIRE(peak(got.L) > 1e-3, "control: fb %.0f %% renders nothing audible", fb);
    REQUIRE(peak(got.L, T) == 0.0 && peak(got.R, T) == 0.0, "fb %.0f %%: %.3g after the declared tail (%ld samples)", fb, peak(got.L, T), T);
    REQUIRE(got.info.restAt >= 0 && got.info.restAt <= T + echo::kIrBlock, "fb %.0f %%: the clone rested at %ld, after the tail %ld", fb,
            static_cast<long>(got.info.restAt), T);
  }
  // restAt when the clone rests inside the LAST block (critic T-030 M1): a long
  // render finds rest at boundary B; a render of exactly B samples ends on the
  // block the clone rested in, with no later boundary to look at, and must
  // still report it.
  {
    const Kv kv{{"time.sync", 0}, {"time.ms", 50}, {"fb.amount", 0}, {"mix", 100}};
    const auto lng = echo::renderIr(req(kv, 48000));
    const long B = static_cast<long>(lng.info.restAt);
    REQUIRE(B > 0 && B < 48000, "control: the long render never rested (restAt %ld)", B);
    const auto got = echo::renderIr(req(kv, B));
    REQUIRE(got.info.restAt == B, "a render of %ld samples, resting in its last block, reports restAt %ld", B, static_cast<long>(got.info.restAt));
  }
  const auto held = echo::renderIr(req({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 100}}, 48000));
  REQUIRE(held.info.tail == echo::kInfiniteTail && held.info.restAt < 0, "a held loop must declare kInfiniteTail and never rest");
  return true;
}

// The stated level (D-011's "one stated input level"): the response is
// rendered at the request's level, not 0 dBFS. Checked against the closed form
// at feedback 0 with the filters off: the first repeat is exactly a * wet.
bool levelIsStated(Detail& d) {
  const Kv kv{{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}};
  for (double db : {-20.0, -40.0}) {
    auto r = req(kv, 9600);
    r.levelDb = db;
    const auto got = echo::renderIr(r);
    const double want = std::pow(10.0, db / 20.0);
    REQUIRE(std::fabs(peak(got.L) - want) < 1e-6 * want, "at %.0f dBFS the first repeat peaks at %.6g, want %.6g", db, peak(got.L), want);
  }
  return true;
}

// A rate the engine would clamp (below 8 kHz, above 768 kHz, non-finite) is
// refused, never rendered at another rate under this rate's hash (critic T-030
// M3); every rate in range is kept exactly.
bool rateIsKept(Detail& d) {
  for (double sr : {4000.0, 1e6}) {
    const auto got = echo::renderIr(req({}, 4800, sr));
    REQUIRE(!got.ok, "a %.0f Hz request was rendered (the engine runs at %.0f Hz)", sr, sr < 8000 ? 8000.0 : 768000.0);
  }
  for (double sr : {8000.0, 44100.0, 192000.0, 768000.0})
    REQUIRE(echo::renderIr(req({}, 4800, sr)).ok, "a %.0f Hz request was refused", sr);
  return true;
}

// Ready for a preallocated service (T-031, gate 3): once prepared, render()
// allocates nothing.
bool renderNoAlloc(Detail& d) {
  if (!ECHO_ALLOC_PROBE) {
    std::printf("  SKIP  (sanitizer build: the allocation probe stands down; echo_ir_tests runs this check)\n");
    return true;
  }
  echo::IrRenderer ren;
  ren.prepare(48000.0);
  Vec L(48000), R(48000);
  const auto r1 = req(kPatches[9].kv, 48000), r2 = req(kPatches[3].kv, 48000, 48000.0, IrInput::Noise);
  alloc_probe::arm();
  const bool ok = ren.render(r1, L, R) && ren.render(r2, L, R);
  alloc_probe::disarm();
  REQUIRE(ok, "render refused");
  REQUIRE(alloc_probe::g_allocs.load() == 0, "%ld allocations in a prepared render", alloc_probe::g_allocs.load());
  return true;
}

// ---- B4-2: the resumable render, the service (T-031), the display model (T-032)

// T-031 criterion 6 (critic T-030 M2): a render resumed at ANY split equals the
// one-shot render bit for bit, rest point included, so head-first frames
// extend one render instead of re-rendering the head.
bool resumableEqualsOneShot(Detail& d) {
  struct Case {
    Kv kv;
    IrInput in;
  };
  const std::vector<Case> cases = {{kPatches[10].kv, IrInput::Mono}, {kPatches[2].kv, IrInput::Mono}, {kPatches[3].kv, IrInput::Noise},
                                   {{{"time.sync", 0}, {"time.ms", 50}, {"fb.amount", 0}, {"mix", 100}}, IrInput::Mono}};
  std::uint32_t s = 99;
  for (const auto& c : cases) {
    const auto r = req(c.kv, 48000, 48000.0, c.in);
    const auto one = echo::renderIr(r);
    for (int split = 0; split < 8; ++split) {
      echo::IrRenderer ren;
      ren.prepare(48000.0);
      REQUIRE(ren.start(r), "start refused");
      Vec L(48000, -1.0), R(48000, -1.0);  // poisoned: every sample must be written
      std::int64_t at = 0;
      const long fixed[] = {1, 7, 511, 512, 513, 4096};
      while (!ren.done()) {
        const long step = split < 6 ? fixed[split] : 1 + static_cast<long>(refNoise(s) * 0.5 * 3000 + 1500);
        at = ren.renderNext(L, R, at + step);
      }
      REQUIRE(firstDiff(L, one.L) < 0 && firstDiff(R, one.R) < 0, "input %d, split rule %d: the resumed render differs at sample %ld", static_cast<int>(c.in),
              split, firstDiff(L, one.L) < 0 ? firstDiff(R, one.R) : firstDiff(L, one.L));
      REQUIRE(ren.info().restAt == one.info.restAt && ren.info().tail == one.info.tail, "split rule %d: restAt %ld vs %ld", split,
              static_cast<long>(ren.info().restAt), static_cast<long>(one.info.restAt));
    }
  }
  return true;
}

using Clock = std::chrono::steady_clock;  // tests only: no clock is read in a render or a model

// Waits (yielding) until pred() holds or `ms` pass. True if it held.
template <class P>
bool waitFor(P pred, double ms) {
  const auto t0 = Clock::now();
  while (!pred()) {
    if (std::chrono::duration<double, std::milli>(Clock::now() - t0).count() > ms) return false;
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
  return true;
}
// The service's current frame state, copied out.
struct Seen {
  std::uint64_t serial = 0, hash = 0;
  std::int64_t ready = 0;
  bool complete = false;
};
Seen seen(const echo::IrService& sv) {
  Seen o;
  sv.read([&](const echo::IrFrame& f) { o = {f.serial, f.hash, f.ready, f.complete}; });
  return o;
}
// Frame 1 covers the first 96 000 samples (D-033, ruled by poll): the render
// costs per sample, so a head fixed in samples costs the same at every rate
// (2 s at 48 kHz, 0.5 s at 192 kHz). A 2 s head at 192 kHz measured 90-196 ms.
long headOf(double /*sr*/) { return 96000; }
echo::IrServiceConfig svcConfig(double sr, double windowS) {
  echo::IrServiceConfig c;
  c.sampleRate = sr;
  c.maxLength = std::lround(windowS * sr);
  c.headLength = headOf(sr);
  c.chunk = std::lround(0.1 * sr);  // a newer request waits at most 0.1 s of rendering (~25 ms at 192 kHz, slow patch)
  c.slots = 8;
  return c;
}
// A distinct drift patch per k (the seed defeats the cache).
IrRequest driftReq(int k, double sr, double windowS) {
  return req({{"time.sync", 0}, {"time.ms", 230}, {"fb.amount", 99}, {"route.mode", 2}, {"mod.shape", 1}, {"mod.depthMs", 5},
              {"mod.seed", 100 + k}, {"filt.in.on", 1}, {"filt.out.on", 1}, {"filt.loop.hpHz", 120}},
             std::lround(windowS * sr), sr);
}

// Gate 1 through the service: every frame it publishes is a prefix of the
// one-shot render, bit for bit, and the finished frame is all of it.
bool serviceFramesAreTheRender(Detail& d) {
  echo::IrService sv;
  REQUIRE(sv.start(svcConfig(48000.0, 6.0)), "service refused to start");
  for (IrInput in : {IrInput::Mono, IrInput::Noise}) {
    auto r = req(kPatches[3].kv, 6 * 48000, 48000.0, in);
    if (in == IrInput::Noise) r.noiseLength = 3 * 48000;  // longer than the head: the burst spans chunks
    const auto one = echo::renderIr(r);
    const auto serial = sv.request(r);
    bool ok = true, done = false;
    long bad = -1;
    REQUIRE(waitFor([&] {
              sv.read([&](const echo::IrFrame& f) {
                if (f.serial != serial) return;
                for (std::size_t i = 0; i < f.L.size() && ok; ++i)
                  if (std::bit_cast<std::uint64_t>(f.L[i]) != std::bit_cast<std::uint64_t>(one.L[i]) ||
                      std::bit_cast<std::uint64_t>(f.R[i]) != std::bit_cast<std::uint64_t>(one.R[i]))
                    ok = false, bad = static_cast<long>(i);
                done = f.complete && f.ready == r.length;
              });
              return done || !ok;
            },
            5000),
            "input %d: the service never finished", static_cast<int>(in));
    REQUIRE(ok, "input %d: a published frame differs from the one-shot render at sample %ld", static_cast<int>(in), bad);
  }
  return true;
}

// Gate 3: rendering never touches the audio thread. A live Echo runs on its
// own "audio" thread while the UI fires requests and the worker renders flat
// out: nothing allocates anywhere in the process, and every render ran on the
// worker, never on the audio thread or the UI's.
bool serviceOffAudioThread(Detail& d) {
  echo::IrService sv;
  REQUIRE(sv.start(svcConfig(48000.0, 4.0)), "service refused to start");
  auto live = std::make_unique<Echo>();
  live->prepare(48000.0, 64);
  live->setParam("fb.amount", 60);
  std::atomic<bool> go{false}, quit{false};
  std::thread::id audioId;
  std::atomic<long> blocks{0};
  std::thread audio([&] {
    audioId = std::this_thread::get_id();
    std::array<double, 64> in{}, oL{}, oR{};
    long n = 0;
    while (!go.load()) std::this_thread::yield();
    while (!quit.load()) {
      for (std::size_t i = 0; i < in.size(); ++i) in[i] = 0.3 * std::sin(0.05 * static_cast<double>(n++));
      live->process(in.data(), in.data(), oL.data(), oR.data(), 64, 120);
      blocks.fetch_add(1);
    }
  });
  std::vector<IrRequest> reqs;  // built before arming: the test's own vectors are not the service's
  for (int k = 0; k < 6; ++k) reqs.push_back(driftReq(k, 48000.0, 4.0));
  if (ECHO_ALLOC_PROBE) alloc_probe::arm();
  go = true;
  std::uint64_t last = 0;
  for (int k = 0; k < 6; ++k) {
    last = sv.request(reqs[static_cast<std::size_t>(k)]);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
  }
  const bool finished = waitFor([&] { const auto s = seen(sv); return s.serial == last && s.complete; }, 5000);
  quit = true;
  audio.join();
  if (ECHO_ALLOC_PROBE) alloc_probe::disarm();
  REQUIRE(finished, "the last request never finished");
  REQUIRE(blocks.load() > 100, "control: the audio thread barely ran (%ld blocks)", blocks.load());
  if (ECHO_ALLOC_PROBE) REQUIRE(alloc_probe::g_allocs.load() == 0, "%ld allocations while the service rendered beside a live engine", alloc_probe::g_allocs.load());
  const auto st = sv.stats();
  REQUIRE(st.nThreads >= 1, "control: no render was seen");
  for (int i = 0; i < st.nThreads; ++i) {
    const auto id = st.threads[static_cast<std::size_t>(i)];
    REQUIRE(id != audioId, "a render ran on the audio thread");
    REQUIRE(id == sv.workerId(), "a render ran on a thread other than the worker (the UI's?)");
  }
  return true;
}

// Gate 4: the display redraws within 100 ms of a change. Structurally (every
// build, CI included): frame 1 is published covering exactly the head,
// not the whole window. By the clock (Release, the dev Mac; reported on CI,
// whose shared runners make wall-clock gates flaky): on a slow patch at 48 and
// 192 kHz, frame 1 arrives within 100 ms, worst of 20 cold requests and of 20
// made while another render is in flight (critic B4-2 M5): half during its
// head, half during its extension.
bool serviceKeepsUp(Detail& d) {
  const bool ci = std::getenv("CI") != nullptr;
  for (double sr : {48000.0, 192000.0}) {
    echo::IrService sv;
    REQUIRE(sv.start(svcConfig(sr, 8.0)), "service refused to start");
    std::vector<IrRequest> reqs;
    for (int k = 0; k < 60; ++k) reqs.push_back(driftReq(k, sr, 8.0));
    double worstCold = 0.0, worstBusy = 0.0;
    for (int k = 0; k < 40; ++k) {
      const bool busy = k >= 20;
      if (busy) {  // another render in flight: in its head (even k) or past frame 1 (odd k)
        const auto other = sv.request(reqs[static_cast<std::size_t>(20 + k)]);
        if (k % 2) waitFor([&] { const auto s = seen(sv); return s.serial == other && s.ready > 0; }, 5000);
        else std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      const auto t0 = Clock::now();
      const auto serial = sv.request(reqs[static_cast<std::size_t>(k)]);
      const bool got = waitFor([&] { const auto s = seen(sv); return s.serial == serial && s.ready > 0; }, 5000);
      const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
      REQUIRE(got, "%.0f kHz: no frame within 5 s", sr / 1000);
      (busy ? worstBusy : worstCold) = std::max(busy ? worstBusy : worstCold, ms);
      const auto first = sv.stats().firstReady;
      REQUIRE(first == headOf(sr), "%.0f kHz: frame 1 covered %ld samples, not the head (%ld)", sr / 1000, static_cast<long>(first), headOf(sr));
      waitFor([&] { return seen(sv).complete; }, 5000);  // let it finish, so the next request starts cold
    }
    std::printf("  REPORT frame 1 at %.0f kHz: worst of 20 cold = %.1f ms, of 20 in flight = %.1f ms (gate 100 ms%s)\n", sr / 1000, worstCold, worstBusy,
                ci ? ", reported only on CI" : "");
    if (!ci) REQUIRE(std::max(worstCold, worstBusy) <= 100.0, "%.0f kHz: frame 1 took %.1f ms cold, %.1f ms in flight (gate: 100 ms)", sr / 1000,
                     worstCold, worstBusy);
  }
  return true;
}

// The screen never blanks: through a drag of requests, whatever frame is on
// screen has samples (critic B4-2 M3: a request used to go on screen empty).
bool serviceNeverBlank(Detail& d) {
  echo::IrService sv;
  REQUIRE(sv.start(svcConfig(192000.0, 4.0)), "service refused to start");
  std::vector<IrRequest> reqs;
  for (int k = 0; k < 12; ++k) reqs.push_back(driftReq(200 + k, 192000.0, 4.0));
  const auto s0 = sv.request(reqs[0]);
  REQUIRE(waitFor([&] { const auto s = seen(sv); return s.serial == s0 && s.complete; }, 5000), "the first request never finished");
  bool blank = false;
  std::uint64_t last = 0;
  for (int k = 1; k < 12; ++k) {
    last = sv.request(reqs[static_cast<std::size_t>(k)]);
    const auto t0 = Clock::now();
    while (std::chrono::duration<double, std::milli>(Clock::now() - t0).count() < 16.7) {  // one display frame at 60 Hz
      const auto s = seen(sv);
      blank = blank || s.serial == 0 || s.ready == 0;
    }
  }
  REQUIRE(waitFor([&] {
            const auto s = seen(sv);
            blank = blank || s.ready == 0;
            return s.serial == last && s.complete;
          },
          5000),
          "the last request never finished");
  REQUIRE(!blank, "an empty frame was on screen during the drag");
  return true;
}

// The cache: a request seen before and rendered to its end is answered at once
// with no render; a render cut short is never a hit.
bool serviceCache(Detail& d) {
  echo::IrService sv;
  REQUIRE(sv.start(svcConfig(48000.0, 8.0)), "service refused to start");
  const auto a = driftReq(1, 48000.0, 8.0), b = driftReq(2, 48000.0, 8.0);
  auto sa = sv.request(a);
  REQUIRE(waitFor([&] { const auto s = seen(sv); return s.serial == sa && s.complete; }, 5000), "A never finished");
  auto sb = sv.request(b);
  REQUIRE(waitFor([&] { const auto s = seen(sv); return s.serial == sb && s.complete; }, 5000), "B never finished");
  const auto before = sv.stats();
  sa = sv.request(a);
  REQUIRE(waitFor([&] { return seen(sv).serial == sa; }, 2000), "A again: nothing published");
  const auto after = sv.stats();
  const auto s = seen(sv);
  REQUIRE(after.hits == before.hits + 1 && after.renders == before.renders, "A again was re-rendered (hits %llu -> %llu, renders %llu -> %llu)",
          static_cast<unsigned long long>(before.hits), static_cast<unsigned long long>(after.hits), static_cast<unsigned long long>(before.renders),
          static_cast<unsigned long long>(after.renders));
  REQUIRE(s.complete && s.hash == echo::irHash(a), "the hit is not A's complete render");
  // A drag (critic B4-2 M2): more cut renders than there are slots, then A
  // again. Cut renders are reused first, so A's complete render survives.
  // Each drag step waits for its frame 1 (so it goes on screen and A is no
  // longer the protected current frame), then the next cuts it: an 8 s window
  // takes far longer than a frame 1.
  std::vector<IrRequest> drag;
  for (int k = 0; k < 12; ++k) drag.push_back(driftReq(50 + k, 48000.0, 8.0));
  std::uint64_t last = 0;
  for (const auto& r : drag) {
    last = sv.request(r);
    waitFor([&] { const auto x = seen(sv); return x.serial == last && x.ready > 0; }, 5000);
  }
  REQUIRE(waitFor([&] { const auto x = seen(sv); return x.serial == last && x.complete; }, 5000), "the drag's last request never finished");
  const auto hits = sv.stats().hits;
  sa = sv.request(a);
  REQUIRE(waitFor([&] { return seen(sv).serial == sa; }, 2000), "A after the drag: nothing published");
  REQUIRE(sv.stats().hits == hits + 1, "A's complete render was pushed out of the cache by the drag's cut renders (%llu cancelled)",
          static_cast<unsigned long long>(sv.stats().cancelled));
  return true;
}

// A newer request cancels the render in flight at its next chunk; the
// cancelled render is not kept as a hit.
bool serviceCancels(Detail& d) {
  echo::IrService sv;
  REQUIRE(sv.start(svcConfig(192000.0, 8.0)), "service refused to start");
  const auto a = driftReq(7, 192000.0, 8.0), b = driftReq(8, 192000.0, 1.0);
  const auto sa = sv.request(a);
  REQUIRE(waitFor([&] { const auto s = seen(sv); return s.serial == sa && s.ready > 0; }, 5000), "A: no frame 1");
  const auto sb = sv.request(b);
  REQUIRE(waitFor([&] { const auto s = seen(sv); return s.serial == sb && s.complete; }, 5000), "B never finished");
  const auto st = sv.stats();
  REQUIRE(st.cancelled >= 1, "A ran to its end before B started (no cancellation)");
  const auto again = sv.request(a);
  REQUIRE(waitFor([&] { return seen(sv).serial == again; }, 5000), "A again: nothing published");
  REQUIRE(sv.stats().hits == st.hits, "the cancelled A was served as a cache hit");
  return true;
}

// Fed only while open (B432): paused, nothing renders, and a render in flight
// stops at its next chunk; resumed, the newest request is rendered.
bool servicePaused(Detail& d) {
  echo::IrService sv;
  REQUIRE(sv.start(svcConfig(192000.0, 8.0)), "service refused to start");
  sv.pause();
  const auto s1 = sv.request(driftReq(3, 192000.0, 8.0));
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  REQUIRE(sv.stats().chunks == 0, "rendered %llu chunks while paused", static_cast<unsigned long long>(sv.stats().chunks));
  sv.resume();
  REQUIRE(waitFor([&] { const auto s = seen(sv); return s.serial == s1 && s.ready > 0; }, 5000), "resumed: no frame");
  sv.pause();
  // The chunk in flight ends: wait until the count holds still for 50 ms
  // (critic B4-2 M6: a fixed sleep flakes on a slow shared runner).
  auto c0 = sv.stats().chunks;
  waitFor([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto c = sv.stats().chunks;
    const bool still = c == c0;
    c0 = c;
    return still;
  },
          5000);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  REQUIRE(sv.stats().chunks == c0, "paused mid-render: %llu more chunks", static_cast<unsigned long long>(sv.stats().chunks - c0));
  REQUIRE(!seen(sv).complete, "control: the render finished before the pause could cut it");
  sv.resume();
  REQUIRE(waitFor([&] { const auto s = seen(sv); return s.complete; }, 5000), "resumed: the cut render was never finished");
  return true;
}

// ---- the display model (T-032) ----

// Gate 1 in the drawing: each column is a render sample, bit for bit, and the
// peak of its range; at one sample per column the columns are the render.
bool columnsAreRenderSamples(Detail& d) {
  const auto got = echo::renderIr(req(kPatches[7].kv, 96000));
  for (std::size_t n : {std::size_t{100}, std::size_t{333}, std::size_t{997}, std::size_t{96000}}) {
    std::vector<echo::IrColumn> cols(n);
    echo::irColumns(got.L, 0, 96000, cols);
    for (std::size_t c = 0; c < n; ++c) {
      const std::int64_t a = static_cast<std::int64_t>(c) * 96000 / static_cast<std::int64_t>(n), b = static_cast<std::int64_t>(c + 1) * 96000 / static_cast<std::int64_t>(n);
      double m = 0;
      for (std::int64_t i = a; i < b; ++i) m = std::max(m, std::fabs(got.L[static_cast<std::size_t>(i)]));
      const auto& col = cols[c];
      if (m == 0.0) {
        REQUIRE(col.index < 0, "%zu columns: column %zu is drawn over an all-zero range", n, c);
        continue;
      }
      REQUIRE(col.index >= a && col.index < b, "%zu columns: column %zu's sample %ld lies outside its range", n, c, static_cast<long>(col.index));
      REQUIRE(std::bit_cast<std::uint64_t>(col.value) == std::bit_cast<std::uint64_t>(got.L[static_cast<std::size_t>(col.index)]),
              "%zu columns: column %zu is not the render's own sample", n, c);
      REQUIRE(std::fabs(col.value) == m, "%zu columns: column %zu shows %.6g, its peak is %.6g", n, c, std::fabs(col.value), m);
    }
  }
  // A partial frame (critic B4-2 M4): columns past what is rendered say so,
  // never "exact zero" (which would draw a tail that has ended).
  std::vector<echo::IrColumn> four(4);
  echo::irColumns(std::span<const double>(got.L.data(), 30000), 0, 120000, four);
  REQUIRE(four[0].index >= 0, "control: the rendered column is empty");
  for (int c = 1; c < 4; ++c)
    REQUIRE(four[static_cast<std::size_t>(c)].index == echo::kIrColumnNotRendered, "a column past the rendered 30000 samples reads %ld, not 'not rendered'",
            static_cast<long>(four[static_cast<std::size_t>(c)].index));
  REQUIRE(echo::irDb(0.1, -20.0) == 0.0 && echo::irDb(0.0, -20.0) == echo::kIrFloorDb, "dB re the stated level");
  return true;
}

// The tempo grid: beats at k * 60 / bpm * sr, division marks at multiples of
// the division, at the request's tempo.
bool tempoGrid(Detail& d) {
  std::vector<double> out(64);
  std::size_t n = echo::irBeats(48000.0, 120.0, 240000, out);
  REQUIRE(n == 10 && out[1] == 24000.0 && out[9] == 216000.0, "120 bpm: %zu beats, beat 1 at %.3f", n, out[1]);
  n = echo::irBeats(48000.0, 93.0, 480000, out);
  for (std::size_t k = 0; k < n; ++k)
    REQUIRE(std::fabs(out[k] - static_cast<double>(k) * 48000.0 * 60.0 / 93.0) < 1e-6, "93 bpm: beat %zu at %.4f", k, out[k]);
  REQUIRE(n == 16, "93 bpm: %zu beats in 10 s, want 16", n);
  n = echo::irDivisionMarks(48000.0, 120.0, 10, 96000, out);  // 1/8D = 0.75 beat = 18000 samples
  REQUIRE(n == 6 && out[1] == 18000.0 && out[5] == 90000.0, "1/8D marks: %zu, mark 1 at %.3f", n, out[1]);
  return true;
}

// The spectrum strip: generation n's spectrum, windowed where that channel's
// generation n arrives, for every routing (critic B4-2 H1). Its reference is
// the engine's own first repeat, which carries the loop filter once (SPEC §2):
// so with the loop filter on, the step from one channel generation to the
// next equals p passes of (the first repeat's shape + 20 log10 g), within
// 0.1 dB from 100 Hz to 0.45 fs where the generation stands above -100 dB
// (critic B4-2 H2: a constant step alone also passed a strip with the filter
// missing or the generations off by one). With every filter off, each
// generation is flat and the next exactly p * 20 log10 g lower. Control: with
// the loop filter on, the step at 100 Hz (under the 200 Hz low cut) is several
// dB off the filter-free step, so the filter is in the strip.
bool spectrumStrip(Detail& d) {
  const double sr = 48000.0, g = 0.7;
  const std::size_t nfft = 32768, bins = nfft / 2 + 1;
  const auto bin = [&](double hz) { return static_cast<std::size_t>(std::lround(hz / sr * static_cast<double>(nfft))); };
  for (int route : {0, 1, 2})
    for (int loopOn : {0, 1}) {
      const Kv kv{{"time.sync", 0}, {"time.ms", 250}, {"time.offR", -20}, {"fb.amount", 70}, {"mix", 100}, {"route.mode", route},
                  {"filt.loop.on", loopOn}, {"filt.loop.hpHz", 200}, {"filt.loop.lpHz", 4000}};
      const auto r = req(kv, std::lround(3.0 * sr));
      const auto got = echo::renderIr(r);
      for (int ch = 0; ch < 2; ++ch) {
        const auto w = echo::irGenerationWindows(got.info, r, ch);
        std::vector<double> S(bins * 4);
        REQUIRE(echo::irGenerationSpectra(ch ? got.R : got.L, sr, w, r.levelDb, 4, nfft, S), "route %d, ch %d: spectra refused (first %.3f s, spacing %.3f s)",
                route, ch, w.first, w.spacing);
        const double p = w.passes;
        // Passes of the loop filter in this channel's first generation: one,
        // except ping-pong's right channel, whose first repeat crossed twice.
        const double q = (route == 1 && ch == 1) ? 2.0 : 1.0;
        const auto at = [&](int n, std::size_t k) { return S[static_cast<std::size_t>(n - 1) * bins + k]; };
        const std::size_t k0 = bin(100.0), k1 = bin(0.45 * sr), kr = bin(1000.0);
        const double flat = p * 20.0 * std::log10(g);
        for (int n = 1; n < 4; ++n) {
          const double stepRef = at(n + 1, kr) - at(n, kr);
          for (std::size_t k = k0; k <= k1; ++k) {
            if (at(n + 1, k) < -100.0) continue;
            const double step = at(n + 1, k) - at(n, k);
            if (!loopOn) {
              REQUIRE(std::fabs(step - flat) < 0.1, "route %d, ch %d, filters off: gen %d -> %d steps %.3f dB at %.0f Hz, want %.3f", route, ch, n, n + 1,
                      step, k * sr / nfft, flat);
              REQUIRE(std::fabs(at(n, k) - at(n, kr)) < 0.1, "route %d, ch %d, filters off: gen %d is not flat (%.3f dB at %.0f Hz)", route, ch, n,
                      at(n, k) - at(n, kr), k * sr / nfft);
            } else {
              const double want = stepRef + (p / q) * (at(1, k) - at(1, kr));
              REQUIRE(std::fabs(step - want) < 0.1, "route %d, ch %d, loop filter: gen %d -> %d steps %.3f dB at %.0f Hz; the first repeat says %.3f", route,
                      ch, n, n + 1, step, k * sr / nfft, want);
            }
          }
          if (loopOn)
            REQUIRE(std::fabs((at(n + 1, k0) - at(n, k0)) - flat) > 3.0, "control, route %d, ch %d: the 200 Hz low cut is not in the strip at 100 Hz", route, ch);
        }
      }
    }
  // Undefined is refused, not drawn: mid/side with side content and unequal times.
  auto r = req({{"time.sync", 0}, {"time.ms", 250}, {"time.offR", -20}, {"route.mode", 2}}, 4800, sr, IrInput::Left);
  const auto got = echo::renderIr(r);
  REQUIRE(!echo::irGenerationWindows(got.info, r, 0).ok, "mid/side with side content and unequal times must have no strip");
  return true;
}

// The energy decay curve: Schroeder's backward integral, a staircase. It
// starts at 0 dB, never rises, and with every filter off falls by 20 log10 g
// from one generation to the next.
bool edcStaircase(Detail& d) {
  const double sr = 48000.0, T = 0.1, g = 0.5;
  const auto got = echo::renderIr(req({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 50}, {"mix", 100}, {"filt.loop.on", 0}}, 48000));
  std::vector<double> e(got.L.size());
  echo::irEdc(got.L, e);
  REQUIRE(std::fabs(e[0]) < 1e-12, "the EDC starts at %.3g dB, not 0", e[0]);
  for (std::size_t i = 1; i < e.size(); ++i) REQUIRE(e[i] <= e[i - 1], "the EDC rises at sample %zu (%.4f after %.4f dB)", i, e[i], e[i - 1]);
  for (int n = 1; n < 5; ++n) {
    const auto a = static_cast<std::size_t>(std::lround(n * T * sr)) + 1, b = static_cast<std::size_t>(std::lround((n + 1) * T * sr)) + 1;
    REQUIRE(std::fabs((e[b] - e[a]) - 20.0 * std::log10(g)) < 0.05, "stair %d -> %d falls %.4f dB, want %.4f", n, n + 1, e[b] - e[a], 20.0 * std::log10(g));
  }
  return true;
}

struct Check {
  const char* name;
  bool (*fn)(Detail&);
  std::vector<const char*> plants;
};

const std::vector<Check> kChecks = {
    {"renderer = the plain offline render, bit for bit: 12 patches; 3 at 44.1 / 192 kHz; L, R, S-only and noise inputs", equalsPlainRender,
     {"irDropsDevice", "irLevelIgnored", "irSkipsOutputFilter", "irSeedAfterReset"}},
    {"one renderer reused: every render equals a fresh one, drift seeds included (nothing leaks from the last patch)", reuseIsFresh,
     {"irReusesState", "irSeedAfterReset"}},
    {"rates: a rate the engine would clamp is refused, never rendered under the wrong hash", rateIsKept, {"irAcceptsClampedRate"}},
    {"cache hash: each of the 29 keys, the rate, tempo, level, length and input change it", hashCoversEveryInput, {"irHashSkipsKey"}},
    {"tail oracle through the renderer: exact zero from the declared tail on; rest reported; held loop = kInfiniteTail", tailThroughRenderer,
     {"noTailGate", "irRestAtMissesEnd"}},
    {"stated level: the response is rendered at the request's level (-20, -40 dBFS)", levelIsStated, {"irLevelIgnored"}},
    {"a prepared render allocates nothing", renderNoAlloc, {"irAllocsInRender"}},
    // ---- B4-2 ----
    {"resumable render: any split equals the one-shot render bit for bit, rest point included", resumableEqualsOneShot, {"irChunkResetsNoise"}},
    {"service frames are the render: every published frame is a bit-exact prefix of the one-shot render", serviceFramesAreTheRender,
     {"irChunkResetsNoise"}},
    {"gate 3: beside a live engine, nothing allocates and every render runs on the worker, never the audio or UI thread", serviceOffAudioThread,
     {"irRenderInline"}},
    {"gate 4: frame 1 is the 96 000-sample head, within 100 ms of the request, cold and with a render in flight (48 and 192 kHz; clock gated off CI)",
     serviceKeepsUp, {"irWholeTailFirst"}},
    {"the screen never blanks: through a drag, the frame on screen always has samples", serviceNeverBlank, {"irPublishesEmpty"}},
    {"cache: a finished request seen before is a hit with no render, even after a drag of cut renders", serviceCache,
     {"irCacheIgnored", "irCancelKeepsSlot"}},
    {"cancel: a newer request cuts the render in flight at its next chunk; a cut render is never a hit", serviceCancels, {"irNoCancel"}},
    {"editor closed: paused, nothing renders and a render in flight stops; resumed, it finishes", servicePaused, {"irRendersWhilePaused"}},
    {"display columns: each is the render's own sample and its range's peak; 1 per sample = the render; unrendered says so", columnsAreRenderSamples,
     {"viewDecimatesPeaks", "viewUnrenderedIsZero"}},
    {"tempo grid: beats and division marks at the request's tempo", tempoGrid, {"gridFixedTempo"}},
    {"spectrum strip, every routing: at each channel's own arrivals; flat and g-stepped with filters off; with the loop filter, each step = the first repeat's shape",
     spectrumStrip, {"viewGenWindowFromBase", "viewGenIndexOffByOne"}},
    {"energy decay curve: Schroeder backward, from 0 dB, never rising, 20 log10 g per stair", edcStaircase, {"edcForward"}},
};

#ifdef ECHO_PLANTS
bool* plantFlag(std::string_view n) {
  using namespace echo::plant;
  struct P {
    const char* n;
    bool* f;
  };
  static const P table[] = {
      {"irHashSkipsKey", &irHashSkipsKey}, {"irReusesState", &irReusesState}, {"irDropsDevice", &irDropsDevice},
      {"irLevelIgnored", &irLevelIgnored}, {"irSkipsOutputFilter", &irSkipsOutputFilter}, {"irAllocsInRender", &irAllocsInRender},
      {"irSeedAfterReset", &irSeedAfterReset}, {"irAcceptsClampedRate", &irAcceptsClampedRate}, {"irRestAtMissesEnd", &irRestAtMissesEnd},
      {"irChunkResetsNoise", &irChunkResetsNoise}, {"irRenderInline", &irRenderInline}, {"irWholeTailFirst", &irWholeTailFirst},
      {"irRendersWhilePaused", &irRendersWhilePaused}, {"irCacheIgnored", &irCacheIgnored}, {"irNoCancel", &irNoCancel},
      {"viewDecimatesPeaks", &viewDecimatesPeaks}, {"gridFixedTempo", &gridFixedTempo}, {"viewGenWindowFromBase", &viewGenWindowFromBase},
      {"edcForward", &edcForward}, {"viewGenIndexOffByOne", &viewGenIndexOffByOne}, {"irCancelKeepsSlot", &irCancelKeepsSlot},
      {"irPublishesEmpty", &irPublishesEmpty}, {"viewUnrenderedIsZero", &viewUnrenderedIsZero},
      {"noTailGate", &noTailGate},  // an engine plant this suite also guards (the tail oracle)
  };
  for (const auto& p : table)
    if (n == p.n) return p.f;
  return nullptr;
}
#endif

}  // namespace

int main() {
  int fails = 0;
#ifdef ECHO_PLANTS
  // Every M4 plant must be guarded by a check here (core_tests.cpp guards the
  // engine's; an M4 plant nobody looks for is a fault nobody would see).
  for (const char* n : {"irHashSkipsKey", "irReusesState", "irDropsDevice", "irLevelIgnored", "irSkipsOutputFilter", "irAllocsInRender",
                        "irSeedAfterReset", "irAcceptsClampedRate", "irRestAtMissesEnd", "irChunkResetsNoise", "irRenderInline",
                        "irWholeTailFirst", "irRendersWhilePaused", "irCacheIgnored", "irNoCancel", "viewDecimatesPeaks", "gridFixedTempo",
                        "viewGenWindowFromBase", "edcForward", "viewGenIndexOffByOne", "irCancelKeepsSlot", "irPublishesEmpty",
                        "viewUnrenderedIsZero"}) {
    bool guarded = false;
    for (const auto& c : kChecks)
      for (const char* p : c.plants) guarded = guarded || std::string_view(p) == n;
    if (!plantFlag(n) || !guarded) {
      std::printf("  FAIL  plant %s is %s\n", n, plantFlag(n) ? "guarded by no check" : "not in the plant table");
      ++fails;
    }
  }
#endif
  for (const auto& c : kChecks) {
    Detail d;
    const bool ok = c.fn(d);
    std::printf("  %s  %s%s%s\n", ok ? "PASS" : "FAIL", c.name, ok ? "" : " -- ", ok ? "" : d.c_str());
    if (!ok) ++fails;
#ifdef ECHO_PLANTS
    for (const char* p : c.plants) {
      bool* f = plantFlag(p);
      if (!f) {
        std::printf("  FAIL  unknown plant %s\n", p);
        ++fails;
        continue;
      }
      *f = true;
      Detail pd;
      const bool fired = !c.fn(pd);
      *f = false;
      std::printf("  %s  plant fires: %s -> %s\n", fired ? "PASS" : "FAIL", p, fired ? pd.c_str() : "NOT SEEN (vacuous check)");
      if (!fired) ++fails;
    }
#endif
  }
  std::printf("%s: %s (%d failing)\n",
#ifdef ECHO_PLANTS
              "echo_ir_plants",
#else
              "echo_ir_tests",
#endif
              fails ? "RED" : "OK", fails);
  return fails ? 1 : 0;
}
