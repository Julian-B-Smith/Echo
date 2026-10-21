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
// THE REFERENCE IS INDEPENDENT OF THE RENDERER. `plain()` below is what the
// core suite does by hand: a fresh, separately prepared Echo, only the patch's
// non-default keys set by NAME, the stated input built here, rendered in
// 333-sample blocks (the renderer uses 512; block independence is the core
// suite's check). So a renderer that resets wrongly, drops a key, mis-scales
// the input or rearranges blocks cannot agree with it by sharing the fault.
#include "alloc_probe.h"  // first: replaces operator new for this executable

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "echo/engine.h"
#include "echo/ir_render.h"

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
  for (const auto& [k, v] : kv)
    if (!e.setParam(k, v)) {
      std::fprintf(stderr, "fixture: setParam(%s, %g) refused\n", k, v);
      std::abort();
    }
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
// long ringing patch nothing of it leaks into the next.
bool reuseIsFresh(Detail& d) {
  echo::IrRenderer ren;
  ren.prepare(48000.0);
  Vec L(96000), R(96000);
  for (std::size_t a : {3u, 4u, 10u})
    for (std::size_t b : {1u, 8u, 0u}) {
      REQUIRE(ren.render(req(kPatches[a].kv, 96000), L, R), "render refused");
      const auto r = req(kPatches[b].kv, 96000);
      REQUIRE(ren.render(r, L, R), "render refused");
      const auto fresh = echo::renderIr(r);
      REQUIRE(firstDiff(L, fresh.L) < 0 && firstDiff(R, fresh.R) < 0, "'%s' after '%s' differs from a fresh render at sample %ld", kPatches[b].name,
              kPatches[a].name, firstDiff(L, fresh.L));
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

struct Check {
  const char* name;
  bool (*fn)(Detail&);
  std::vector<const char*> plants;
};

const std::vector<Check> kChecks = {
    {"renderer = the plain offline render, bit for bit: 12 patches; 3 at 44.1 / 192 kHz; L, R, S-only and noise inputs", equalsPlainRender,
     {"irDropsDevice", "irLevelIgnored", "irSkipsOutputFilter"}},
    {"one renderer reused: every render equals a fresh one (nothing leaks from the last patch)", reuseIsFresh, {"irReusesState"}},
    {"cache hash: each of the 29 keys, the rate, tempo, level, length and input change it", hashCoversEveryInput, {"irHashSkipsKey"}},
    {"tail oracle through the renderer: exact zero from the declared tail on; held loop = kInfiniteTail", tailThroughRenderer, {"noTailGate"}},
    {"stated level: the response is rendered at the request's level (-20, -40 dBFS)", levelIsStated, {"irLevelIgnored"}},
    {"a prepared render allocates nothing", renderNoAlloc, {"irAllocsInRender"}},
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
  for (const char* n : {"irHashSkipsKey", "irReusesState", "irDropsDevice", "irLevelIgnored", "irSkipsOutputFilter", "irAllocsInRender"}) {
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
