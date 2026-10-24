// core_tests.cpp — Echo's invariant suite, M1 + M2 + M3 (ROADMAP M1 – M3; SPEC revision 3).
//
// ONE SOURCE, TWO BINARIES (core/CMakeLists.txt):
//   echo_tests   built WITHOUT ECHO_PLANTS: every plant is constexpr false, so
//                each check runs exactly the code horde links. Every check must
//                PASS.
//   echo_plants  built WITH ECHO_PLANTS: each check runs once clean (must pass,
//                proving no plant hook leaks when off), then once per planted
//                fault it guards, with that fault switched on (must FAIL). A
//                check whose plant does not make it fail is vacuous and the
//                binary exits non-zero.
// The lab is never the reference (manifest Q5): every expectation below comes
// from the spec or from closed-form arithmetic.
#include "alloc_probe.h"  // first: replaces operator new for this executable

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string_view>
#include <cstring>
#include <clocale>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "echo/engine.h"

using echo::Echo;
using Vec = std::vector<double>;

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr double kProbeHz = 431.7;  // incommensurate with the test times (SPEC §15 probe finding)

// ---- fixtures ----------------------------------------------------------------

std::uint32_t g_rng = 1;
double noise() {  // mulberry32, seeded per check: determinism of the TESTS too
  std::uint32_t t = (g_rng += 0x6D2B79F5u);
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + ((t ^ (t >> 7)) * (t | 61u));
  return static_cast<double>((t ^ (t >> 14))) / 4294967296.0 * 2.0 - 1.0;
}

Vec zeros(long n) { return Vec(static_cast<std::size_t>(n), 0.0); }
Vec impulse(long n, long at = 0, double a = 1.0) {
  Vec v = zeros(n);
  v[static_cast<std::size_t>(at)] = a;
  return v;
}
Vec sine(long n, double hz, double a, double sr = kSr, double phase = 0.0) {
  Vec v(static_cast<std::size_t>(n));
  for (long i = 0; i < n; ++i) v[static_cast<std::size_t>(i)] = a * std::sin(2 * kPi * hz * i / sr + phase);
  return v;
}
long S(double seconds, double sr = kSr) { return std::lround(seconds * sr); }

struct Event {
  long at;
  std::function<void(Echo&)> fn;
};
struct Out {
  Vec L, R;
};

// Render with events applied at exact sample indices: blocks are cut at every
// event, so the same events land at the same sample under any block pattern.
Out run(Echo& e, const Vec& inL, const Vec& inR, std::vector<Event> ev = {}, double bpm = 120.0,
        const std::function<int(long)>& blockAt = [](long) { return 512; }) {
  const long n = static_cast<long>(inL.size());
  Out o{zeros(n), zeros(n)};
  std::stable_sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
  std::size_t ei = 0;
  long pos = 0;
  while (pos < n) {
    while (ei < ev.size() && ev[ei].at <= pos) ev[ei++].fn(e);
    long len = std::min<long>(blockAt(pos), n - pos);
    if (ei < ev.size()) len = std::min(len, ev[ei].at - pos);
    len = std::max<long>(1, len);
    const auto u = static_cast<std::size_t>(pos);
    e.process(inL.data() + u, inR.data() + u, o.L.data() + u, o.R.data() + u, static_cast<int>(len), bpm);
    pos += len;
  }
  return o;
}

std::unique_ptr<Echo> mk(std::initializer_list<std::pair<const char*, double>> kv, double sr = kSr) {
  auto e = std::make_unique<Echo>();
  e->prepare(sr, 4096);
  for (const auto& [k, v] : kv)
    if (!e->setParam(k, v)) {
      std::fprintf(stderr, "fixture: setParam(%s, %g) refused\n", k, v);
      std::abort();
    }
  return e;
}
Event set(long at, const char* key, double v) {
  return {at, [key, v](Echo& e) { e.setParam(key, v); }};
}

double maxAbs(const Vec& v, long a = 0, long b = -1) {
  if (b < 0) b = static_cast<long>(v.size());
  double m = 0;
  for (long i = a; i < b; ++i) m = std::max(m, std::fabs(v[static_cast<std::size_t>(i)]));
  return m;
}

// Second-difference click reading: the worst |Δ²y| around a switch over the
// worst |Δ²y| in a steady stretch before it. A smooth blend reads near the
// level ratio of the two states; a step reads in the tens or hundreds.
double clickRatio(const Vec& y, long at, long len) {
  auto d2 = [&](long i) { return std::fabs(y[i + 1] - 2 * y[i] + y[i - 1]); };
  double steady = 0, win = 0;
  for (long i = at - S(0.2); i < at - 50; ++i) steady = std::max(steady, d2(i));
  for (long i = at - 10; i < at + len + 200; ++i) win = std::max(win, d2(i));
  return steady > 0 ? win / steady : 1e9;
}

// Where the final exact-zero run of a stereo render starts (n if none).
long finalZeroRun(const Out& o) {
  long z = static_cast<long>(o.L.size());
  while (z > 0 && o.L[z - 1] == 0.0 && o.R[z - 1] == 0.0) --z;
  return z;
}

// The sample index at which head 0 reaches `target` after an event at `at`
// (processing one sample at a time), or -1.
long fadeEnd(Echo& e, const Vec& in, long at, const char* key, double v, double target) {
  double o[2];
  for (long i = 0; i < static_cast<long>(in.size()); ++i) {
    if (i == at) e.setParam(key, v);
    e.process(&in[i], &in[i], &o[0], &o[1], 1, 120);
    if (i >= at && e.headDelay(0) == target) return i - at + 1;
  }
  return -1;
}

// Iterative radix-2 FFT (the loop-bound check needs whole spectra).
void fft(std::vector<std::complex<double>>& a) {
  const std::size_t n = a.size();
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const double ang = -2 * kPi / static_cast<double>(len);
    const std::complex<double> wl(std::cos(ang), std::sin(ang));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<double> w(1);
      for (std::size_t j = 0; j < len / 2; ++j) {
        const auto u = a[i + j], v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}

std::complex<double> dft(const Vec& y, long a, long b, double hz, double sr) {
  std::complex<double> s(0);
  for (long i = a; i < b; ++i) s += y[static_cast<std::size_t>(i)] * std::polar(1.0, -2 * kPi * hz * (i - a) / sr);
  return s;
}

using Detail = std::string;
#define REQUIRE(cond, ...)                                       \
  do {                                                           \
    if (!(cond)) {                                               \
      char b_[300];                                              \
      std::snprintf(b_, sizeof b_, __VA_ARGS__);                 \
      d = b_;                                                    \
      return false;                                              \
    }                                                            \
  } while (0)

// ---- the checks ---------------------------------------------------------------

// Time accuracy (SPEC §3, D-004): a whole-sample time lands every repeat on its
// sample; the read's centroid equals the delay, whole or fractional.
bool timeAccuracy(Detail& d) {
  auto e = mk({{"filt.loop.on", 0}, {"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 50}, {"mix", 100}});
  const auto o = run(*e, impulse(S(0.6), 0, 0.5), zeros(S(0.6)));
  for (int k = 1; k <= 5; ++k) {
    long arg = 0;
    double m = -1;
    for (long i = k * 4800 - 100; i < k * 4800 + 100; ++i)
      if (std::fabs(o.L[i]) > m) { m = std::fabs(o.L[i]); arg = i; }
    REQUIRE(arg == k * 4800, "repeat %d at sample %ld, expected %d", k, arg, k * 4800);
  }
  for (double D : {4800.0, 4800.25, 4800.5, 4800.75}) {
    auto f = mk({{"filt.loop.on", 0}, {"time.sync", 0}, {"time.ms", D / 48.0}, {"fb.amount", 0}, {"mix", 100}});
    const auto q = run(*f, impulse(S(0.12)), zeros(S(0.12)));
    double s0 = 0, s1 = 0;
    for (long i = 4790; i < 4812; ++i) { s0 += q.L[i]; s1 += i * q.L[i]; }
    REQUIRE(std::fabs(s1 / s0 - D) < 1e-6, "centroid %.6f, delay %.2f samples", s1 / s0, D);
  }
  return true;
}

// The generation ratio (SPEC §5.3, ruled S-1): gen 2 is g(1+R)/2 of gen 1
// exactly (float storage: 1e-6), every later ratio is g within 1e-3.
// At all four rates: the DC blocker's pole comes from 5 Hz at THAT rate.
bool generationRatio(Detail& d) {
  for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
    auto e = mk({{"filt.loop.on", 0}, {"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 50}, {"mix", 100}}, sr);
    const long D = std::lround(0.1 * sr);
    const auto o = run(*e, impulse(8 * D, 0, 0.5), zeros(8 * D));
    const double R = std::exp(-2 * kPi * 5.0 / sr), g = 0.5;
    const double r2 = o.L[2 * D] / o.L[D];
    REQUIRE(std::fabs(r2 - g * (1 + R) / 2) < 1e-6, "at %.0f Hz gen2/gen1 %.9f, expected %.9f", sr, r2, g * (1 + R) / 2);
    for (int k = 2; k <= 6; ++k) {
      const double r = o.L[(k + 1) * D] / o.L[k * D];
      REQUIRE(std::fabs(r - g) < 1e-3, "at %.0f Hz gen%d/gen%d %.6f, expected %.3f within 1e-3", sr, k + 1, k, r, g);
    }
  }
  return true;
}

// The ceiling's law (SPEC §5.1), exactly: identity to 0.9, then
// 0.9 + 0.1·tanh((|x| - 0.9)/0.1), sign kept, applied to g·x (the RETURNING
// term). Read off generation 2 = float(((1+R)/2)·ceiling(g·a)).
bool ceilingLaw(Detail& d) {
  const double R = std::exp(-2 * kPi * 5.0 / kSr), gn = (1 + R) / 2;
  auto law = [](double x) { const double a = std::fabs(x); const double y = a <= 0.9 ? a : 0.9 + 0.1 * std::tanh((a - 0.9) / 0.1); return x < 0 ? -y : y; };
  const double cases[][2] = {{0.5, 1.2}, {0.95, 1.0}, {1.0, 1.2}, {-1.5, 1.0}, {0.8, 1.1}};
  for (const auto& c : cases) {
    auto e = mk({{"filt.loop.on", 0}, {"time.sync", 0}, {"time.ms", 100}, {"fb.amount", c[1] * 100}, {"mix", 100}});
    const auto o = run(*e, impulse(S(0.25), 0, c[0]), zeros(S(0.25)));
    const double want = static_cast<float>(gn * law(c[1] * c[0]));
    REQUIRE(std::fabs(o.L[9600] - want) <= 1e-6 * std::fabs(want), "a=%.2f g=%.2f: generation 2 is %.7f, the law gives %.7f", c[0], c[1], o.L[9600], want);
  }
  return true;
}

// The ceiling (SPEC §5.1, D-005 as amended): past unity the loop parks. After
// the input stops the line holds < 2.0; under input, at most its peak + 2.0;
// and it does not die.
bool ceilingPastUnity(Detail& d) {
  {
    auto e = mk({{"time.sync", 0}, {"time.ms", 150}, {"fb.amount", 120}, {"mix", 100}});
    Vec in = zeros(S(6));
    for (long i = 0; i < S(0.1); ++i) in[i] = 0.9;
    run(*e, in, in);
    const double pk = e->linePeak();
    REQUIRE(pk < 2.0, "after the input stops the line holds %.3f (bound 2.0)", pk);
    REQUIRE(pk > 0.5, "the held loop died (line peak %.3f)", pk);
  }
  {
    auto e = mk({{"time.sync", 0}, {"time.ms", 150}, {"fb.amount", 120}, {"mix", 100}});
    Vec in(S(4));
    for (long i = 0; i < S(4); ++i) in[i] = ((i / 480) % 2) ? -0.9 : 0.9;  // a 50 Hz square
    run(*e, in, in);
    const double pk = e->linePeak();
    REQUIRE(pk <= 0.9 + 2.0, "under a +-0.9 square the line holds %.3f (bound 2.9)", pk);
  }
  return true;
}

// Nothing limits the dry path (ADR-142): at mix 0 the output is the input
// exactly, at any level and feedback.
bool dryPathExact(Detail& d) {
  auto e = mk({{"fb.amount", 120}, {"mix", 0}, {"time.sync", 0}, {"time.ms", 50}});
  Vec in = sine(S(1), 220, 3.0);
  in[100] = 7.5;
  const auto o = run(*e, in, in);
  for (long i = 0; i < S(1); ++i) REQUIRE(o.L[i] == in[i] && o.R[i] == in[i], "dry differs at %ld: %.17g vs %.17g", i, o.L[i], in[i]);
  return true;
}

// Mix 100 % is exactly the wet: nothing sounds before the first repeat (§8).
bool mixFullNoDry(Detail& d) {
  auto e = mk({{"mix", 100}, {"time.sync", 0}, {"time.ms", 100}});
  const auto o = run(*e, sine(S(0.2), 300, 0.5), sine(S(0.2), 300, 0.5));
  REQUIRE(maxAbs(o.L, 0, 4798) == 0.0, "dry leak at mix 100: %.3g before the first repeat", maxAbs(o.L, 0, 4798));
  return true;
}

// True bypass (ADR-142): settled-off filters are not in the circuit, whatever
// their cutoffs: the output is bit-identical.
bool trueBypass(Detail& d) {
  g_rng = 7;
  Vec in(S(1));
  for (auto& v : in) v = 0.5 * noise();
  auto a = mk({{"filt.in.on", 0}, {"filt.loop.on", 0}, {"filt.out.on", 0}, {"fb.amount", 60}, {"mix", 70}, {"time.sync", 0}});
  auto b = mk({{"filt.in.on", 0}, {"filt.loop.on", 0}, {"filt.out.on", 0}, {"fb.amount", 60}, {"mix", 70}, {"time.sync", 0},
               {"filt.in.hpHz", 2000}, {"filt.in.lpHz", 500}, {"filt.loop.hpHz", 2000}, {"filt.loop.lpHz", 500},
               {"filt.out.hpHz", 2000}, {"filt.out.lpHz", 500}});
  const auto oa = run(*a, in, in), ob = run(*b, in, in);
  for (long i = 0; i < S(1); ++i) REQUIRE(oa.L[i] == ob.L[i], "off filters differ with their cutoffs at %ld", i);
  return true;
}

// The loop bound (SPEC §5.2, D-005 as amended), MEASURED on the engine: with
// g = 1 and the ceiling idle, |gen2(f) / gen1(f)| = |H_loop(f)| over every FFT
// bin up to Nyquist, at four rates, across the loop filter's corners and whole
// and half-sample delays. It must not exceed 1 (tolerance 1e-5 for float
// storage). This asserts the effective loop, not the coefficient formulas.
bool loopBound(Detail& d) {
  struct Case { bool on; double hp, lp, frac; };
  const Case cases[] = {{false, 20, 20000, 0.0}, {false, 20, 20000, 0.5}, {true, 20, 20000, 0.0}, {true, 2000, 500, 0.5},
                        {true, 60, 9000, 0.25}, {true, 20, 500, 0.0}, {true, 2000, 20000, 0.5}};
  for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
    for (const auto& c : cases) {
      const double D = std::round(0.5 * sr) + c.frac;
      auto e = mk({{"time.sync", 0}, {"time.ms", D / sr * 1000.0}, {"fb.amount", 100}, {"mix", 100},
                   {"filt.loop.on", c.on ? 1.0 : 0.0}, {"filt.loop.hpHz", c.hp}, {"filt.loop.lpHz", c.lp}}, sr);
      const long n = static_cast<long>(3.2 * D);
      const auto o = run(*e, impulse(n, 0, 0.25), zeros(n));
      // Window = one period minus 8 samples: generation k spreads over 3k+1
      // taps, and gen 3's first tap otherwise lands on gen 2's last sample.
      const long L = static_cast<long>(std::floor(D)) - 8;
      std::size_t N = 1;
      while (N < static_cast<std::size_t>(L)) N <<= 1;
      std::vector<std::complex<double>> y1(N), y2(N);
      const long a1 = static_cast<long>(std::floor(D)) - 3, a2 = static_cast<long>(std::floor(2 * D)) - 3;
      for (long i = 0; i < L; ++i) { y1[i] = o.L[a1 + i]; y2[i] = o.L[a2 + i]; }
      fft(y1);
      fft(y2);
      double m1 = 0;
      for (std::size_t k = 0; k <= N / 2; ++k) m1 = std::max(m1, std::abs(y1[k]));
      double worst = 0, at = 0;
      for (std::size_t k = 0; k <= N / 2; ++k) {
        if (std::abs(y1[k]) < 1e-3 * m1) continue;
        const double r = std::abs(y2[k]) / std::abs(y1[k]);
        if (r > worst) { worst = r; at = k * sr / N; }
      }
      REQUIRE(worst <= 1.0 + 1e-5, "|H_loop| peaks at %.7f (%.0f Hz) at %.0f Hz rate, filter %s %g/%g, frac %.2f", worst, at, sr,
              c.on ? "on" : "off", c.hp, c.lp, c.frac);
    }
  }
  return true;
}

// Sample-rate independence (1.0 bar row 40): at 44.1/48/96/192 kHz a 100 ms
// time lands on 0.1·fs exactly, and the loop filter's magnitude at 100 Hz–5 kHz
// matches the 48 kHz figure within 0.5 dB (bilinear warping at 44.1 kHz is the
// stated tolerance's reason: 0.26 dB at 5 kHz for a 2 kHz corner).
bool srIndependence(Detail& d) {
  const double fs[] = {100, 1000, 2000, 5000};
  double ref[4] = {};
  for (double sr : {48000.0, 44100.0, 96000.0, 192000.0}) {
    const long n = S(0.25, sr);
    const long D = std::lround(0.1 * sr);
    {  // time: unfiltered, so the peak IS the delay (a low-pass moves a filtered peak)
      auto t = mk({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mix", 100}, {"filt.loop.on", 0}}, sr);
      const auto ot = run(*t, impulse(n), zeros(n));
      long arg = 0;
      double m = -1;
      for (long i = D - 50; i < D + 50; ++i)
        if (std::fabs(ot.L[i]) > m) { m = std::fabs(ot.L[i]); arg = i; }
      REQUIRE(arg == D && ot.L[D] == 1.0, "at %.0f Hz the repeat lands at %ld (%.6f), expected %ld", sr, arg, ot.L[arg], D);
    }
    auto e = mk({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mix", 100}, {"filt.loop.on", 1},
                 {"filt.loop.hpHz", 20}, {"filt.loop.lpHz", 2000}}, sr);
    const auto o = run(*e, impulse(n), zeros(n));
    for (int j = 0; j < 4; ++j) {
      const double db = 20 * std::log10(std::abs(dft(o.L, D - 3, D - 3 + S(0.1, sr), fs[j], sr)));
      if (sr == 48000.0) ref[j] = db;
      else REQUIRE(std::fabs(db - ref[j]) <= 0.5, "at %.0f Hz rate the loop filter reads %.2f dB at %.0f Hz, 48 kHz reads %.2f", sr, db, fs[j], ref[j]);
    }
  }
  return true;
}

// Block-size independence (A3, 1.0 bar row 40): with switches, ramps, time
// moves and filter changes scheduled at fixed samples, every block pattern
// from 1 to 4096 renders bit-identically.
bool blockIndependence(Detail& d) {
  g_rng = 11;
  const long n = S(1.5);
  Vec inL(n), inR(n);
  for (long i = 0; i < n; ++i) {
    inL[i] = i < S(0.2) ? 0.4 * noise() : 0.3 * std::sin(2 * kPi * 330 * i / kSr);
    inR[i] = i < S(0.2) ? 0.4 * noise() : 0.2 * std::sin(2 * kPi * 510 * i / kSr);
  }
  auto evs = [] {
    return std::vector<Event>{set(2400, "route.mode", 1), set(7200, "filt.out.on", 1), set(9000, "filt.out.lpHz", 3000),
                              set(12000, "time.ms", 250), set(20000, "fb.amount", 80), set(30000, "mix", 60),
                              set(40000, "time.change", 2), set(40001, "time.ms", 180), set(50000, "route.mode", 2),
                              set(52000, "filt.loop.hpHz", 400), set(60000, "io.outGain", -6), set(61000, "io.inGain", 3),
                              set(64000, "mod.shape", 1), set(66000, "mod.seed", 42), set(68000, "time.law", 1), set(68001, "time.ms", 300)};
  };
  // Modulation on (sine on line 0, spread to line 1) and a drift switch mid-render: M2's state must be block-size independent too.
  auto ref = mk({{"time.sync", 0}, {"time.ms", 120}, {"fb.amount", 40}, {"mod.depthMs", 3}, {"mod.rateHz", 1.7}, {"mod.spread", 60}});
  const auto o0 = run(*ref, inL, inR, evs(), 120.0, [](long) { return 4096; });
  std::uint32_t seed = 5;
  const std::vector<std::function<int(long)>> pats = {
      [](long) { return 1; }, [](long) { return 7; }, [](long) { return 64; }, [](long) { return 333; },
      [&seed](long) { seed = seed * 1664525u + 1013904223u; return 1 + static_cast<int>(seed % 4096u); }};
  for (std::size_t p = 0; p < pats.size(); ++p) {
    auto e = mk({{"time.sync", 0}, {"time.ms", 120}, {"fb.amount", 40}, {"mod.depthMs", 3}, {"mod.rateHz", 1.7}, {"mod.spread", 60}});
    const auto o = run(*e, inL, inR, evs(), 120.0, pats[p]);
    for (long i = 0; i < n; ++i)
      REQUIRE(o.L[i] == o0.L[i] && o.R[i] == o0.R[i], "block pattern %zu differs at sample %ld", p, i);
  }
  return true;
}

// Determinism (A2, RS-7.11): two instances driven alternately each equal a
// solo render; reset() from mid-state equals a fresh instance.
bool determinism(Detail& d) {
  g_rng = 3;
  const long n = S(1);
  Vec in(n);
  for (auto& v : in) v = 0.4 * noise();
  auto solo = mk({{"fb.amount", 60}, {"time.sync", 0}, {"time.ms", 90}, {"mod.depthMs", 4}, {"mod.shape", 1}});
  const auto os = run(*solo, in, in);
  auto a = mk({{"fb.amount", 60}, {"time.sync", 0}, {"time.ms", 90}, {"mod.depthMs", 4}, {"mod.shape", 1}});
  auto b = mk({{"fb.amount", 60}, {"time.sync", 0}, {"time.ms", 90}, {"mod.depthMs", 4}, {"mod.shape", 1}});
  Out oa{zeros(n), zeros(n)}, ob{zeros(n), zeros(n)};
  for (long p = 0; p < n; p += 256) {
    const int len = static_cast<int>(std::min<long>(256, n - p));
    a->process(in.data() + p, in.data() + p, oa.L.data() + p, oa.R.data() + p, len, 120);
    b->process(in.data() + p, in.data() + p, ob.L.data() + p, ob.R.data() + p, len, 120);
  }
  for (long i = 0; i < n; ++i) REQUIRE(oa.L[i] == os.L[i] && ob.L[i] == os.L[i], "interleaved instance differs from solo at %ld", i);
  auto r = mk({{"fb.amount", 60}, {"time.sync", 0}, {"time.ms", 90}});
  run(*r, in, in);
  r->reset();
  auto f = mk({{"fb.amount", 60}, {"time.sync", 0}, {"time.ms", 90}});
  const Vec in2 = sine(n, 440, 0.3);
  const auto orr = run(*r, in2, in2), of = run(*f, in2, in2);
  for (long i = 0; i < n; ++i) REQUIRE(orr.L[i] == of.L[i], "reset() differs from a fresh instance at %ld", i);
  return true;
}

// Silence in, silence out (RS-7.7): a fresh instance stays exact zero in every
// routing and time-change mode at 120 %; and an awake engine whose loop and
// filters are clean returns exact zero everywhere but on the repeat.
bool silence(Detail& d) {
  // A fresh instance is at rest: silence is exact zero by construction, even
  // with the loop held at 120 % (no DSP runs; this row checks the rest path).
  auto h = mk({{"fb.amount", 120}});
  const auto oh = run(*h, zeros(S(0.5)), zeros(S(0.5)));
  REQUIRE(maxAbs(oh.L) == 0.0 && h->atRest(), "a fresh instance at 120 %% gave %.3g on silence", maxAbs(oh.L));
  // In every routing x time-change mode the DSP runs a sound into rest; from
  // then on silence is exact zero.
  for (int route = 0; route < 3; ++route)
    for (int ch = 0; ch < 3; ++ch) {
      auto e = mk({{"fb.amount", 50}, {"route.mode", static_cast<double>(route)}, {"time.change", static_cast<double>(ch)},
                   {"time.sync", 0}, {"time.ms", 60}});
      const long n = S(4);
      const auto o = run(*e, impulse(n), impulse(n, 0, -0.5));
      const long z = finalZeroRun(o);
      REQUIRE(maxAbs(o.L, 0, z) + maxAbs(o.R, 0, z) > 0.1, "control: an impulse is inaudible (route %d, change %d)", route, ch);
      REQUIRE(e->atRest() && z < S(3.5), "route %d, change %d: no exact silence after the sound (from %ld)", route, ch, z);
    }
  auto e = mk({{"filt.loop.on", 0}, {"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mix", 100}});
  const auto o = run(*e, impulse(S(0.3)), zeros(S(0.3)));
  for (long i = 0; i < S(0.3); ++i)
    if (i != 4800) REQUIRE(o.L[i] == 0.0 && o.R[i] == 0.0, "residue %.3g at sample %ld (only 4800 may sound)", o.L[i], i);
  return true;
}

// The tail (RS-7.3–7.6, SPEC §10): exact zero from the declared tail on,
// audible before it; kInfiniteTail at g >= 1.
bool tailExactZero(Detail& d) {
  auto e = mk({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 50}, {"mix", 100}});
  const long T = static_cast<long>(e->tail());
  const auto o = run(*e, impulse(T + S(0.5)), zeros(T + S(0.5)));
  REQUIRE(maxAbs(o.L, 0, T) > 1e-3, "control: the tail is inaudible before it ends");
  REQUIRE(maxAbs(o.L, T + 1) == 0.0 && maxAbs(o.R, T + 1) == 0.0, "after the declared tail (%ld samples) the output is %.3g", T, maxAbs(o.L, T + 1));
  REQUIRE(e->atRest(), "not at rest after the declared tail");
  auto h = mk({{"fb.amount", 100}});
  REQUIRE(h->tail() == echo::kInfiniteTail, "g = 1 must declare kInfiniteTail");
  REQUIRE(e->latency() == 0 && e->minBlock() == 1 && e->channels() == 2, "declarations: latency 0, min_block 1, 2 channels");
  return true;
}

// Rest never cuts a sound (SPEC §10 finding, M1): when the gate fires, the
// output it silences is already below -120 dBFS. With the default loop filter
// (60 Hz low cut) and a 20 Hz output low cut, the wet rings after the last loud
// write leaves the line; the gate must wait for it.
bool restNeverCuts(Detail& d) {
  for (double hp : {20.0, 60.0, 400.0}) {
    auto e = mk({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mix", 100}, {"filt.loop.hpHz", hp},
                 {"filt.out.on", 1}, {"filt.out.hpHz", 20}});
    const long n = S(1.5);
    const auto o = run(*e, impulse(n), zeros(n));
    long z = n;
    while (z > 0 && o.L[z - 1] == 0.0 && o.R[z - 1] == 0.0) --z;  // the final exact-zero run starts at z
    REQUIRE(z < n && e->atRest(), "never rested (loop low cut %g Hz)", hp);
    REQUIRE(std::fabs(o.L[z - 1]) < 1e-6, "the gate cut a sounding tail: %.3g just before rest (loop low cut %g Hz)", o.L[z - 1], hp);
  }
  return true;
}

// The tail formula (SPEC §10, D-019) is exercised, not bypassed: at feedback
// 50 / 90 / 99 %, filters on and off, the output just before rest is below
// -120 dBFS, so the declared tail never ended a sounding repeat.
bool tailFormula(Detail& d) {
  for (double fb : {50.0, 90.0, 99.0})
    for (double loopOn : {1.0, 0.0}) {
      auto e = mk({{"time.sync", 0}, {"time.ms", 50}, {"fb.amount", fb}, {"mix", 100}, {"filt.loop.on", loopOn}});
      const long T = static_cast<long>(e->tail());
      const auto o = run(*e, impulse(T + S(0.5)), zeros(T + S(0.5)));
      const long z = finalZeroRun(o);
      REQUIRE(e->atRest() && z <= T + 1, "fb %.0f %%: not at rest by the declared tail", fb);
      REQUIRE(std::fabs(o.L[z - 1]) < 1e-6, "fb %.0f %%, loop filter %s: rest cut a sounding tail (%.3g)", fb, loopOn ? "on" : "off", o.L[z - 1]);
    }
  return true;
}

// Lowering feedback mid-tail, or releasing a held loop, never cuts the repeats
// (D-019): the tail clock restarts at the change.
bool tailAfterChange(Detail& d) {
  struct Case { double from, to; };
  for (const Case c : {Case{120, 35}, Case{90, 10}}) {
    auto e = mk({{"time.sync", 0}, {"time.ms", 150}, {"fb.amount", c.from}, {"mix", 100}});
    const long n = S(10), at = S(4);
    Vec in = zeros(n);
    for (long i = 0; i < S(0.1); ++i) in[i] = 0.8 * std::sin(2 * kPi * 220 * i / kSr);
    const auto o = run(*e, in, in, {set(at, "fb.amount", c.to)});
    const long z = finalZeroRun(o);
    REQUIRE(z > at && e->atRest(), "fb %.0f -> %.0f: never rested", c.from, c.to);
    REQUIRE(std::fabs(o.L[z - 1]) < 1e-6, "fb %.0f -> %.0f at 4 s: rest cut the repeats at %.3g", c.from, c.to, o.L[z - 1]);
  }
  return true;
}

// A feedback a hair under 100 % (a normalised host's round trip gives
// 99.99999999999999) declares an enormous or infinite tail, never a negative
// or small one, and the loop keeps sounding (critic H3).
bool tailNearUnity(Detail& d) {
  auto e = mk({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 99.99999999999999}, {"mix", 100}});
  const auto t = e->tail();
  REQUIRE(t == echo::kInfiniteTail || t > static_cast<std::int64_t>(1e15), "tail() = %lld at 99.99999999999999 %%", static_cast<long long>(t));
  const auto o = run(*e, impulse(S(1)), zeros(S(1)));
  REQUIRE(!e->atRest() && maxAbs(o.L, S(0.85), S(1)) > 0.1, "the near-unity loop died (peak %.3g in the last 0.15 s)", maxAbs(o.L, S(0.85), S(1)));
  return true;
}

// The read never leaves the line (critic H1): a delay a hair above a whole
// number (70 ms at 44.1 kHz is 3087.0000000000005 samples) reads the right
// sample across the line's wrap at 12.02 s, every lap.
bool readAcrossWrap(Detail& d) {
  for (double sr : {44100.0, 48000.0}) {
    auto e = mk({{"filt.loop.on", 0}, {"time.sync", 0}, {"time.ms", 70}, {"time.offL", sr == 48000.0 ? 10.0 : 0.0}, {"fb.amount", 0}, {"mix", 100}}, sr);
    const long n = S(12.6, sr);
    const Vec in = sine(n, 431.7, 0.5, sr);
    const auto o = run(*e, in, in);
    const double D = e->resolvedTimes()[0] * sr;
    const long Di = std::lround(D);
    double worst = 0;
    long at = 0;
    for (long i = S(11.9, sr); i < n; ++i) {
      const double err = std::fabs(o.L[i] - static_cast<float>(in[i - Di]));
      if (err > worst) { worst = err; at = i; }
    }
    REQUIRE(worst < 1e-6, "at %.0f Hz (delay %.13f samples) the read is off by %.3g at sample %ld, across the 12.02 s wrap", sr, D, worst, at);
  }
  return true;
}

// FTZ is SET inside process (RS-7.8), not merely restored: a subnormal input
// is flushed (at mix 0 the dry would otherwise carry it through exactly).
bool ftzSet(Detail& d) {
  auto e = mk({{"mix", 0}});
  const Vec in(64, 1e-310);
  const auto o = run(*e, in, in);
  REQUIRE(maxAbs(o.L) == 0.0, "a subnormal input came through (%.3g): FTZ/DAZ is not set inside process", maxAbs(o.L));
  return true;
}

// Non-finite input (RS-7.9, A9): repaired to 0, every repair counted, latched.
bool finiteInput(Detail& d) {
  auto e = mk({{"fb.amount", 50}, {"mix", 50}});
  Vec L = sine(S(0.5), 300, 0.3), R = L;
  L[1000] = std::nan("");
  R[2000] = INFINITY;
  L[3000] = -INFINITY;
  const auto o = run(*e, L, R);
  for (long i = 0; i < S(0.5); ++i) REQUIRE(std::isfinite(o.L[i]) && std::isfinite(o.R[i]), "non-finite output at %ld", i);
  REQUIRE(e->nonFiniteCount() == 3, "3 repairs made, %llu counted", static_cast<unsigned long long>(e->nonFiniteCount()));
  REQUIRE(e->nonFiniteLatched(), "repair latch not held");
  // The output guard: a finite input times +24 dB overflows the dry path.
  auto f = mk({{"mix", 0}, {"io.outGain", 24}});
  Vec big = sine(S(0.05), 300, 0.3);
  big[1000] = 1e308;
  const auto ob = run(*f, big, big);
  for (long i = 0; i < S(0.05); ++i) REQUIRE(std::isfinite(ob.L[i]) && std::isfinite(ob.R[i]), "non-finite output from an overflowing dry at %ld", i);
  REQUIRE(f->nonFiniteCount() >= 1, "the output repair was not counted");
  return true;
}

// A non-finite inside the loop clears ALL loop state (SPEC §10): the wet
// recovers at once. Zeroing one sample would leave the poison in the input
// filter's state and silence the wet for seconds.
bool loopRepair(Detail& d) {
  auto e = mk({{"filt.in.on", 1}, {"fb.amount", 50}, {"mix", 100}, {"time.sync", 0}, {"time.ms", 100}});
  Vec in = sine(S(0.8), 300, 0.5);
  in[S(0.2)] = 1e300;  // finite input; float storage overflows: a non-finite write
  const auto o = run(*e, in, in);
  for (long i = 0; i < S(0.8); ++i) REQUIRE(std::isfinite(o.L[i]), "non-finite output at %ld", i);
  REQUIRE(e->nonFiniteCount() >= 1, "the loop repair was not counted");
  REQUIRE(maxAbs(o.L, S(0.5), S(0.8)) > 0.1, "the wet did not recover after the repair (peak %.3g)", maxAbs(o.L, S(0.5), S(0.8)));
  return true;
}

// FTZ is set inside process() and the host's control word restored (RS-7.8).
bool ftzRestored(Detail& d) {
  auto e = mk({});
#if defined(__x86_64__)
  constexpr std::uint64_t kMask = ~std::uint64_t{0x3F};  // MXCSR bits 0-5 are sticky exception flags, not control
#else
  constexpr std::uint64_t kMask = ~std::uint64_t{0};
#endif
  const std::uint64_t before = echo::fpControlWord() & kMask;
  run(*e, sine(S(0.05), 300, 0.3), sine(S(0.05), 300, 0.3));
  REQUIRE((echo::fpControlWord() & kMask) == before, "the FP control word changed: %llx -> %llx", static_cast<unsigned long long>(before),
          static_cast<unsigned long long>(echo::fpControlWord()));
  return true;
}

// Nothing allocates in process / setParam / reset / loadState (RS-7.7).
bool noAlloc(Detail& d) {
  if (!ECHO_ALLOC_PROBE) {
    std::printf("  SKIP  (sanitizer build: the allocation probe stands down; echo_tests runs this check)\n");
    return true;
  }
  auto e = mk({});
  const long n = S(0.6);
  Vec in = sine(n, 300, 0.3), out(n);
  const std::array<echo::KeyValue, 3> patch{{{"fb.amount", 30}, {"time.ms", 200}, {"mix", 50}}};
  alloc_probe::arm();
  for (std::size_t i = 0; i < echo::kParams.size(); ++i) e->setParam(static_cast<int>(i), echo::kParams[i].def);
  e->setParam("route.mode", 2);
  e->setParam("filt.out.on", 1);
  e->setParam("time.sync", 0);
  e->setParam("time.ms", 250);
  e->setParam("mod.depthMs", 8);
  e->setParam("mod.shape", 1);
  e->setParam("time.law", 1);
  for (long p = 0; p < n; p += 480) e->process(in.data() + p, in.data() + p, out.data() + p, out.data() + p, 480, 120);
  e->loadState(patch);
  e->process(in.data(), in.data(), out.data(), out.data(), 480, 100);
  e->reset();
  alloc_probe::disarm();
  REQUIRE(alloc_probe::g_allocs.load() == 0, "%ld allocations on the audio path", alloc_probe::g_allocs.load());
  return true;
}

// The I/O gain law (B435): unity exactly at 0 dB, a 20 ms ramp linear in
// amplitude, exact at its end.
bool ioGainLaw(Detail& d) {
  // B435 test 2: a step from 0 to +6 dB is a linear AMPLITUDE ramp over 20 ms
  // (+/- one sample), on both trims, at 44.1, 48 and 96 kHz. Every ramp sample
  // is checked against the straight line, and the end lands exactly on sample
  // N - 1 (N = 20 ms at the rate), not before.
  const double G = std::pow(10.0, 6.0 / 20.0);
  for (const char* key : {"io.inGain", "io.outGain"})
    for (double sr : {44100.0, 48000.0, 96000.0}) {
      auto e = mk({{"mix", 0}}, sr);
      const long n = S(0.2, sr), at = S(0.1, sr), N = S(0.02, sr);
      const Vec in(n, 0.25);
      const auto o = run(*e, in, in, {set(at, key, 6)});
      REQUIRE(o.L[at - 1] == 0.25, "%s: unity at 0 dB: %.17g", key, o.L[at - 1]);
      for (long k = 0; k < N; ++k) {
        const double want = 1.0 + (G - 1.0) * static_cast<double>(k + 1) / static_cast<double>(N);
        REQUIRE(std::fabs(o.L[at + k] / 0.25 - want) < 1e-12, "%s at %.0f Hz: ramp sample %ld reads %.9f, the linear-in-amplitude 20 ms ramp %.9f",
                key, sr, k, o.L[at + k] / 0.25, want);
      }
      REQUIRE(o.L[at + N - 2] != 0.25 * G, "%s at %.0f Hz: the ramp ended early", key, sr);
      REQUIRE(o.L[at + N - 1] == 0.25 * G && o.L[at + N] == 0.25 * G, "%s at %.0f Hz: after 20 ms the gain is %.17g, expected %.17g", key, sr,
              o.L[at + N] / 0.25, G);
    }
  return true;
}

// Routing and filter switches are click-free (RS-4.3; 20 ms equal-gain).
bool switchesClickFree(Detail& d) {
  const long n = S(1.2), at = S(0.8);
  const Vec L = sine(n, kProbeHz, 0.5), R = sine(n, kProbeHz, 0.5, kSr, kPi / 2);
  {
    auto e = mk({{"mix", 100}, {"fb.amount", 0}, {"time.sync", 0}, {"time.ms", 100}});
    const auto o = run(*e, L, R, {set(at, "route.mode", 2)});
    const double c = std::max(clickRatio(o.L, at, 960), clickRatio(o.R, at, 960));
    REQUIRE(c <= 4.0, "routing switch reads %.1f (a step reads tens)", c);
  }
  {
    auto e = mk({{"mix", 100}, {"fb.amount", 0}, {"time.sync", 0}, {"time.ms", 100}, {"filt.out.hpHz", 1500}});
    const auto o = run(*e, L, R, {set(at, "filt.out.on", 1)});
    const double c = clickRatio(o.L, at, 960);
    REQUIRE(c <= 4.0, "filter switch reads %.1f", c);
  }
  return true;
}

// Crossfade time changes are equal-gain (D-007): two correlated heads never
// sum above the signal.
bool crossfadeEqualGain(Detail& d) {
  auto e = mk({{"filt.loop.on", 0}, {"fb.amount", 0}, {"mix", 100}, {"time.sync", 0}, {"time.ms", 100}, {"time.change", 1}});
  const long n = S(1);
  const Vec in(n, 0.5);
  const auto o = run(*e, in, in, {set(S(0.5), "time.ms", 60)});
  double dev = 0;
  for (long i = S(0.2); i < n; ++i) dev = std::max(dev, std::fabs(o.L[i] - 0.5));
  REQUIRE(e->fadeCount(0) == 1 && e->fadeCount(1) == 1, "expected one crossfade per line, saw %llu / %llu",
          static_cast<unsigned long long>(e->fadeCount(0)), static_cast<unsigned long long>(e->fadeCount(1)));
  REQUIRE(dev < 1e-12, "a correlated crossfade departs from the signal by %.4f", dev);
  return true;
}

// Jump is a 3 ms equal-gain crossfade, never a bare jump (Q-A5, D-016).
bool jumpIsShortFade(Detail& d) {
  auto e = mk({{"mix", 100}, {"fb.amount", 0}, {"time.sync", 0}, {"time.ms", 500}, {"time.change", 2}});
  const long n = S(1.4), at = S(1.0);
  const Vec in = sine(n, kProbeHz, 0.5);
  const auto o = run(*e, in, in, {set(at, "time.ms", 250)});
  const double c = clickRatio(o.L, at, 144);
  REQUIRE(c <= 4.0, "the 3 ms jump reads %.1f (a bare jump reads tens)", c);
  return true;
}

// Fade lengths (SPEC §3.5, §3.7): jump = 3 ms, crossfade = time.xfadeMs, at
// the RATE (48 and 96 kHz). The fade SOUNDS for N samples (the last at weight
// 1); the head's bookkeeping lands one sample later, hence N + 1 below.
bool fadeLengths(Detail& d) {
  for (double sr : {48000.0, 96000.0}) {
    const Vec in = sine(S(0.6, sr), 300, 0.3, sr);
    auto j = mk({{"time.sync", 0}, {"time.ms", 100}, {"time.change", 2}, {"time.xfadeMs", 40}, {"fb.amount", 0}}, sr);
    const long lj = fadeEnd(*j, in, S(0.3, sr), "time.ms", 200, 0.2 * sr);
    REQUIRE(lj - 1 == S(0.003, sr), "at %.0f Hz the jump sounded for %ld samples, 3 ms is %ld", sr, lj - 1, S(0.003, sr));
    auto c = mk({{"time.sync", 0}, {"time.ms", 100}, {"time.change", 1}, {"time.xfadeMs", 40}, {"fb.amount", 0}}, sr);
    const long lc = fadeEnd(*c, in, S(0.3, sr), "time.ms", 200, 0.2 * sr);
    REQUIRE(lc - 1 == S(0.040, sr), "at %.0f Hz the crossfade sounded for %ld samples, time.xfadeMs 40 is %ld", sr, lc - 1, S(0.040, sr));
  }
  return true;
}

// The retrigger queue (SPEC §3.5): targets arriving mid-fade wait; at the
// fade's end the head fades to the LATEST one (intermediates dropped).
bool retriggerQueue(Detail& d) {
  auto e = mk({{"fb.amount", 0}, {"time.sync", 0}, {"time.ms", 100}, {"time.change", 1}, {"time.xfadeMs", 60}});
  const long n = S(1.0);
  const Vec in = sine(n, 300, 0.3);
  run(*e, in, in, {set(S(0.5), "time.ms", 200), set(S(0.51), "time.ms", 300), set(S(0.52), "time.ms", 400)});
  REQUIRE(e->fadeCount(0) == 2, "expected 2 fades (to 200, then to the latest 400), saw %llu", static_cast<unsigned long long>(e->fadeCount(0)));
  REQUIRE(e->headDelay(0) == 0.4 * kSr, "the head ends at %.1f samples, expected %.1f", e->headDelay(0), 0.4 * kSr);
  return true;
}

// Deferral (SPEC §3.5): flipping time.change mid-fade changes nothing until the
// next move.
bool deferral(Detail& d) {
  const long n = S(1.2);
  const Vec in = sine(n, kProbeHz, 0.4);
  auto a = mk({{"fb.amount", 30}, {"time.sync", 0}, {"time.ms", 100}, {"time.change", 1}, {"time.xfadeMs", 200}});
  auto b = mk({{"fb.amount", 30}, {"time.sync", 0}, {"time.ms", 100}, {"time.change", 1}, {"time.xfadeMs", 200}});
  const auto oa = run(*a, in, in, {set(S(0.5), "time.ms", 300)});
  const auto ob = run(*b, in, in, {set(S(0.5), "time.ms", 300), set(S(0.55), "time.change", 2)});
  for (long i = 0; i < n; ++i) REQUIRE(oa.L[i] == ob.L[i], "a time.change flip mid-fade changed the output at %ld", i);
  return true;
}

// Synced time lands on the host tempo from the first block (SPEC §3.3).
bool tempoSync(Detail& d) {
  auto e = mk({{"filt.loop.on", 0}, {"time.sync", 1}, {"time.div", 9}, {"fb.amount", 0}, {"mix", 100}});
  const auto o = run(*e, impulse(S(1.2)), zeros(S(1.2)), {}, 60.0);
  long arg = 0;
  double m = -1;
  for (long i = 0; i < S(1.2); ++i)
    if (std::fabs(o.L[i]) > m) { m = std::fabs(o.L[i]); arg = i; }
  REQUIRE(arg == 48000, "1/4 at 60 bpm repeats at sample %ld, expected 48000", arg);
  REQUIRE(e->fadeCount(0) == 0 && e->headDelay(0) == 48000.0, "the tempo glided in from a stale value (%llu fades, head %.1f)",
          static_cast<unsigned long long>(e->fadeCount(0)), e->headDelay(0));
  return true;
}

// The 12 s clamp (SPEC §3.4): synced times fold by halving (both lines, ratio
// kept); free times clamp; the examples of §3.4.
bool clampFold(Detail& d) {
  auto at = [](std::initializer_list<std::pair<const char*, double>> kv, double bpm) {
    auto e = mk(kv);
    const Vec z = zeros(64);
    run(*e, z, z, {}, bpm);
    return std::make_pair(e->resolvedTimes(), e->folded());
  };
  auto [t1, f1] = at({{"time.sync", 1}, {"time.div", 0}}, 30.0);
  REQUIRE(std::fabs(t1[0] - 8.0) < 1e-9 && f1, "2/1 at 30 bpm plays at %.3f s (expected 8 s, folded)", t1[0]);
  auto [t2, f2] = at({{"time.sync", 1}, {"time.div", 0}, {"time.offR", 50}}, 40.0);
  REQUIRE(std::fabs(t2[0] - 6.0) < 1e-9 && std::fabs(t2[1] - 9.0) < 1e-9 && f2, "2/1 at 40 bpm, +50 %% R: %.3f / %.3f s (expected 6 / 9)", t2[0], t2[1]);
  auto [t3, f3] = at({{"time.sync", 0}, {"time.ms", 12000}, {"time.offR", 50}}, 120.0);
  REQUIRE(t3[1] == 12.0 && !f3, "free 12 s +50 %% R clamps to %.3f s (expected 12)", t3[1]);
  auto [t4, f4] = at({{"time.sync", 0}, {"time.ms", 1}, {"time.offL", -50}}, 120.0);
  REQUIRE(t4[0] == 0.001 && !f4, "1 ms -50 %% L must floor at 1 ms, resolved %.6f s", t4[0]);
  return true;
}

// Routing (SPEC §4): ping-pong alternates from the left; stereo lines never
// cross; mid/side folds to its M line exactly (§4.1, by construction).
bool routing(Detail& d) {
  {
    auto e = mk({{"route.mode", 1}, {"filt.loop.on", 0}, {"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 50}, {"mix", 100}});
    const auto o = run(*e, impulse(S(0.4)), zeros(S(0.4)));
    REQUIRE(std::fabs(o.L[4800]) > 0.4 && o.R[4800] == 0.0, "ping-pong repeat 1 must be on the left (L %.3f R %.3f)", o.L[4800], o.R[4800]);
    // The far channel may carry the DC blocker's decaying tail (~1e-5), never a repeat.
    REQUIRE(std::fabs(o.R[9600]) > 0.1 && std::fabs(o.L[9600]) < 1e-4, "ping-pong repeat 2 must be on the right (L %.3g R %.3g)", o.L[9600], o.R[9600]);
    REQUIRE(std::fabs(o.L[14400]) > 0.02 && std::fabs(o.R[14400]) < 1e-4, "ping-pong repeat 3 must be on the left (L %.3g R %.3g)", o.L[14400], o.R[14400]);
  }
  {
    auto e = mk({{"route.mode", 0}, {"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 70}, {"mix", 100}});
    const auto o = run(*e, impulse(S(1)), zeros(S(1)));
    REQUIRE(maxAbs(o.R) == 0.0, "stereo: a left impulse reached the right (%.3g)", maxAbs(o.R));
    auto r = mk({{"route.mode", 0}, {"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mix", 100}, {"filt.loop.on", 0}});
    const auto orr = run(*r, zeros(S(0.2)), impulse(S(0.2)));
    REQUIRE(orr.R[4800] == 1.0 && maxAbs(orr.L) == 0.0, "stereo: a right-only impulse must wake the instance and repeat on the right (R %.3f)", orr.R[4800]);
  }
  {  // mid/side carries the side at its level: pure S in, M + S / M - S out
    auto e = mk({{"route.mode", 2}, {"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mix", 100}, {"filt.loop.on", 0}});
    const auto o = run(*e, impulse(S(0.2)), impulse(S(0.2), 0, -1.0));
    REQUIRE(o.L[4800] == 1.0 && o.R[4800] == -1.0, "mid/side: pure side in gives L %.3f R %.3f, expected 1 / -1", o.L[4800], o.R[4800]);
  }
  {
    g_rng = 21;
    const long n = S(0.8);
    Vec L(n), R(n), M(n);
    for (long i = 0; i < n; ++i) { L[i] = 0.5 * noise(); R[i] = 0.5 * noise(); M[i] = (L[i] + R[i]) / 2; }
    auto a = mk({{"route.mode", 2}, {"time.sync", 0}, {"time.ms", 70}, {"time.offL", 10}, {"time.offR", -20}, {"fb.amount", 50}, {"mix", 60}});
    auto b = mk({{"route.mode", 2}, {"time.sync", 0}, {"time.ms", 70}, {"time.offL", 10}, {"time.offR", -20}, {"fb.amount", 50}, {"mix", 60}});
    const auto oa = run(*a, L, R), ob = run(*b, M, M);
    double diff = 0;
    for (long i = 0; i < n; ++i) diff = std::max(diff, std::fabs((oa.L[i] + oa.R[i]) / 2 - (ob.L[i] + ob.R[i]) / 2));
    REQUIRE(diff < 1e-9, "mid/side fold differs from its M line by %.3g", diff);
  }
  return true;
}

// Load (SPEC §10, D-016): the old repeats fade out over 20 ms and END; Device
// keys are left alone.
bool loadEnds(Detail& d) {
  auto e = mk({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 80}, {"mix", 100}});
  const long n = S(1.2), at = S(0.5);
  const std::array<echo::KeyValue, 3> patch{{{"fb.amount", 30}, {"time.ms", 200}, {"io.inGain", 6}}};
  const auto o = run(*e, impulse(n), zeros(n), {{at, [&patch](Echo& x) { x.loadState(patch); }}});
  REQUIRE(maxAbs(o.L, at - S(0.1), at) > 1e-3, "control: nothing rings before the load");
  REQUIRE(maxAbs(o.L, at + 960) == 0.0, "after the 20 ms load fade the old repeats still sound (%.3g)", maxAbs(o.L, at + 960));
  REQUIRE(e->getParam(echo::kInGain) == 0.0, "a load wrote the Device key io.inGain (%.1f)", e->getParam(echo::kInGain));
  REQUIRE(e->getParam(echo::kFb) == 30.0, "the patch value did not land");
  REQUIRE(maxAbs(o.L, at, at + 400) > 1e-3, "the old repeats were cut at the load, not faded");
  return true;
}

// A load fades the old audio out under its OLD settings (D-020): routing,
// filters and wet level change underneath nothing that sounds, so no click.
bool loadClickFree(Detail& d) {
  const long n = S(1.2), at = S(0.8);
  const Vec L = sine(n, kProbeHz, 0.5), R = sine(n, kProbeHz, 0.5, kSr, kPi / 2);
  auto e = mk({{"route.mode", 2}, {"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 60}, {"mix", 100}});
  const std::array<echo::KeyValue, 4> patch{{{"route.mode", 0}, {"filt.out.on", 1}, {"filt.out.hpHz", 1500}, {"time.ms", 200}}};
  const auto o = run(*e, L, R, {{at, [&patch](Echo& x) { x.loadState(patch); }}});
  const double c = std::max(clickRatio(o.L, at, 960), clickRatio(o.R, at, 960));
  REQUIRE(c <= 4.0, "the load reads %.1f across its fade (a step reads tens)", c);
  return true;
}

// A second load during the fade lands its values but never restarts the fade
// (D-020): the old audio still ends 20 ms after the FIRST load.
bool reloadNoRestart(Detail& d) {
  auto e = mk({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 80}, {"mix", 100}});
  const long n = S(1), at = S(0.5);
  const std::array<echo::KeyValue, 1> a{{{"fb.amount", 30}}}, b{{{"fb.amount", 40}}};
  const auto o = run(*e, impulse(n), zeros(n), {{at, [&a](Echo& x) { x.loadState(a); }}, {at + 480, [&b](Echo& x) { x.loadState(b); }}});
  REQUIRE(maxAbs(o.L, at + 960) == 0.0, "the old audio still sounds %.3g after the first load's 20 ms (the fade restarted)", maxAbs(o.L, at + 960));
  REQUIRE(e->getParam(echo::kFb) == 40.0, "the second load's value did not land");
  return true;
}

// A load never touches the Device gains' ramps (RS-4.5, critic H4): an
// io.outGain ramp in flight runs on through a load.
bool loadLeavesDevice(Detail& d) {
  auto e = mk({{"mix", 0}});
  const long n = S(0.2), at = 4800;
  const Vec in(n, 0.25);
  const std::array<echo::KeyValue, 1> patch{{{"fb.amount", 20}}};
  // The load lands 900 samples BEFORE the gain move, so its 20 ms fade ends
  // inside the gain's ramp: a load that snapped the Device ramps would land it.
  const auto o = run(*e, in, in, {{at - 900, [&patch](Echo& x) { x.loadState(patch); }}, set(at, "io.outGain", 6)});
  const double G = std::pow(10.0, 6.0 / 20.0), mid = o.L[at + 479] / 0.25;
  REQUIRE(std::fabs(mid - (1 + G) / 2) < 1e-9, "a load disturbed the io.outGain ramp: %.6f at mid-ramp, expected %.6f", mid, (1 + G) / 2);
  return true;
}

// Sleep and wake (RS-7.6, A16): an instance horde stopped calling equals its
// never-slept twin from the wake on, parameters changed while asleep landed.
bool sleepTwin(Detail& d) {
  auto a = mk({{"mix", 50}, {"fb.amount", 50}, {"time.sync", 0}, {"time.ms", 120}, {"mod.depthMs", 6}, {"mod.rateHz", 0.9}});
  auto b = mk({{"mix", 50}, {"fb.amount", 50}, {"time.sync", 0}, {"time.ms", 120}, {"mod.depthMs", 6}, {"mod.rateHz", 0.9}});
  Vec burst = zeros(S(4));
  for (long i = 0; i < S(0.2); ++i) burst[i] = 0.5 * std::sin(2 * kPi * 330 * i / kSr);
  run(*a, burst, burst);  // real audio runs into rest in both
  run(*b, burst, burst);
  REQUIRE(a->atRest() && b->atRest(), "the burst did not come to rest");
  const Vec z = zeros(S(1));
  run(*a, z, z);  // a keeps being called on silence; b sleeps
  for (Echo* x : {a.get(), b.get()}) {
    x->setParam("mix", 70);
    x->setParam("fb.amount", 60);
    x->setParam("time.ms", 250);
    x->setParam("io.outGain", -6);
    x->setParam("filt.out.on", 1);
  }
  run(*a, zeros(S(0.5)), zeros(S(0.5)));
  const Vec in = sine(S(1), 300, 0.4);
  const auto oa = run(*a, in, in), ob = run(*b, in, in);
  for (long i = 0; i < S(1); ++i) REQUIRE(oa.L[i] == ob.L[i] && oa.R[i] == ob.R[i], "the slept instance differs from its twin at %ld", i);
  return true;
}

// Parameter validation: unknown keys, non-finite, out-of-range and fractional
// stepped values are refused, not clamped; aliases resolve to the canonical
// division; a load reports refusals but lands what it accepted.
bool paramValidation(Detail& d) {
  auto e = mk({});
  REQUIRE(!e->setParam("nope", 1), "an unknown key was accepted");
  REQUIRE(!e->setParam("mix", 101), "an out-of-range value was accepted");
  REQUIRE(!e->setParam("mix", std::nan("")), "NaN was accepted");
  REQUIRE(!e->setParam("route.mode", 1.5), "a fractional stepped value was accepted");
  REQUIRE(e->setParam("time.div", 21) && e->resolve("fb.amount") == echo::kFb, "valid values and handles");
  REQUIRE(echo::divisionIndex("1/4D") == 7 && echo::divisionIndex("3/8") == 7 && !echo::divisionIndex("1/5"), "division aliases");
  const std::array<echo::KeyValue, 2> kv{{{"bogus", 1}, {"mix", 40}}};
  REQUIRE(!e->loadState(kv) && e->getParam(echo::kMix) == 40, "a load must report the refusal and land the rest");
  return true;
}

// ---- M2: moving time and modulation ---------------------------------------------

struct SpeedStatsView { double sMin, sMax; unsigned long long clamps, floors; };

// Process one sample at a time, calling `each(i, e)` after each, with events
// and a per-sample tempo (block = 1, so the tempo is exactly bpmAt(i)).
Out runEach(Echo& e, const Vec& inL, const Vec& inR, std::vector<Event> ev, const std::function<double(long)>& bpmAt,
            const std::function<void(long, Echo&)>& each) {
  const long n = static_cast<long>(inL.size());
  Out o{zeros(n), zeros(n)};
  std::stable_sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
  std::size_t ei = 0;
  for (long i = 0; i < n; ++i) {
    while (ei < ev.size() && ev[ei].at <= i) ev[ei++].fn(e);
    e.process(&inL[i], &inR[i], &o.L[i], &o.R[i], 1, bpmAt(i));
    each(i, e);
  }
  return o;
}
double cents(double ratio) { return 1200.0 * std::log2(ratio); }
Vec clickProbeUntil(long n, long until);  // the click probe (below), silent from `until` on

// The tape law (SPEC §3.6) against an INDEPENDENT full-rate model written here
// from the spec: tape speed v lags 1/T with time constant glide; the write
// position is integrated every sample; the read head sits one tape unit behind
// the write head, so the delay is how long ago the tape now under it was
// written; |pitch| <= 12 st. The engine keeps the history at control rate (one
// point per 32 samples); the two must agree within 1e-3 sample.
bool tapeLaw(Detail& d) {
  struct Move { double from, to; };
  for (double glide : {0.01, 0.03, 0.3})
    for (const Move mv : {Move{500, 250}, Move{250, 500}, Move{2, 1}}) {
      auto e = mk({{"time.sync", 0}, {"time.ms", mv.from}, {"time.change", 0}, {"time.law", 0}, {"time.glide", glide}, {"fb.amount", 0},
                   {"filt.loop.on", 0}});
      const long n = S(2.5), at = S(1.0);
      const Vec in(n, 0.1);
      const double D0 = mv.from / 1000 * kSr, T = mv.to / 1000 * kSr, tau = glide * kSr, a = 1 - std::exp(-1 / tau);
      std::vector<double> P;  // the reference write position, EVERY sample
      P.reserve(static_cast<std::size_t>(n));
      double v = 1 / D0, Dref = D0, c = 0, worst = 0, worstCents = 0, prevE = D0, prevR = D0;
      long wAt = 0;
      runEach(*e, in, in, {set(at, "time.ms", mv.to)}, [](long) { return 120.0; }, [&](long i, Echo& x) {
        if (i < at) { P.push_back(static_cast<double>(i) / D0); return; }  // a steady tape at v = 1/D0
        v += (1 / T - v) * a;
        P.push_back(P.back() + v);
        const double target = P.back() - 1.0;
        if (i == at) c = static_cast<double>(i) - D0;
        long j = static_cast<long>(std::floor(c));
        while (j + 1 <= i && P[static_cast<std::size_t>(j + 1)] <= target) ++j;
        const double pj = P[static_cast<std::size_t>(j)], pk = j + 1 <= i ? P[static_cast<std::size_t>(j + 1)] : pj;
        const double cs = pk > pj ? j + (target - pj) / (pk - pj) : static_cast<double>(j);
        const double V = std::clamp((static_cast<double>(i) - std::max(cs, c)) - Dref, -1.0, 0.5);
        Dref += V;
        c = static_cast<double>(i) - Dref;
        const double E = x.headDelay(0);
        const double err = std::fabs(E - Dref);
        if (err > worst) { worst = err; wAt = i; }
        if (i > at) worstCents = std::max(worstCents, std::fabs(cents((1 - (E - prevE)) / (1 - (Dref - prevR)))));
        prevE = E;
        prevR = Dref;
      });
      // The engine keeps the history one point per 32 samples, QUADRATIC
      // between them (SPEC §11): measured <= 0.008 sample and <= 2 cents of
      // read speed at the fastest glide (10 ms), 0.01 cent at 0.3 s. Linear
      // interpolation erred by ~50 cents at 10 ms (critic M2-M5).
      REQUIRE(worst < 0.01 && worstCents < 2.5, "glide %.2f, %g -> %g ms: off the full-rate model by %.4g samples (at %ld), %.2f cents of speed", glide,
              mv.from, mv.to, worst, wAt, worstCents);
    }
  return true;
}

// The exponential law (SPEC §3.6): a one-pole on ln D with time constant
// time.glide, D_{n+1} = D_n + D_n ln(T/D_n)/tau, uncapped for a small move; a
// big one runs at the cap (+12 st: read speed exactly 2) and never past it.
bool expLaw(Detail& d) {
  {
    auto e = mk({{"time.sync", 0}, {"time.ms", 500}, {"time.law", 1}, {"time.glide", 0.3}, {"fb.amount", 0}, {"filt.loop.on", 0}});
    const long n = S(2.0), at = S(0.5);
    const Vec in(n, 0.1);
    const double T = 0.45 * kSr, tau = 0.3 * kSr;
    double D = 0.5 * kSr, worst = 0;
    runEach(*e, in, in, {set(at, "time.ms", 450)}, [](long) { return 120.0; }, [&](long i, Echo& x) {
      if (i < at) return;
      D += D * (std::log(T) - std::log(D)) / tau;
      if (std::fabs(T - D) < 1e-9) D = T;
      worst = std::max(worst, std::fabs(x.headDelay(0) - D));
    });
    REQUIRE(worst < 1e-6, "500 -> 450 ms: the exponential law departs from its recurrence by %.3g samples", worst);
  }
  {
    auto e = mk({{"time.sync", 0}, {"time.ms", 500}, {"time.law", 1}, {"time.glide", 0.3}, {"fb.amount", 0}, {"filt.loop.on", 0}});
    const long n = S(2.0), at = S(0.5);
    const Vec in(n, 0.1);
    e->resetStats();
    run(*e, in, in, {set(at, "time.ms", 50)});
    const auto st = e->speedStats(0);
    REQUIRE(std::fabs(st.sMax - 2.0) < 1e-9 && st.clamps == 0, "500 -> 50 ms must run at the cap (speed 2): max speed %.6f, %llu backstop hits",
            st.sMax, static_cast<unsigned long long>(st.clamps));
  }
  return true;
}

// Repitch accuracy in cents (ROADMAP M2 gate): during a glide the output's
// pitch, measured from its zero crossings over 5 ms windows, equals the read
// speed the head's motion implies, s = 1 - dD/dt, within 1 cent.
bool repitchCents(Detail& d) {
  for (int law : {0, 1})
  for (double glide : {0.05, 1.0}) {
    auto e = mk({{"time.sync", 0}, {"time.ms", 500}, {"time.law", static_cast<double>(law)}, {"time.glide", glide}, {"fb.amount", 0},
                 {"filt.loop.on", 0}, {"mix", 100}});
    const long n = S(3.0), at = S(0.8);
    const Vec in = sine(n, kProbeHz, 0.5);
    Vec D(static_cast<std::size_t>(n));
    const auto o = runEach(*e, in, in, {set(at, "time.ms", 400)}, [](long) { return 120.0; }, [&](long i, Echo& x) { D[static_cast<std::size_t>(i)] = x.headDelay(0); });
    const long W = S(0.005);
    double worst = 0, wAt = 0;
    // from the move's own start (the onset, the cap, the settling), not after it
    for (long a = at + S(0.0005); a + W < at + S(1.6); a += W) {
      // zero crossings (upward), linearly interpolated
      std::vector<double> zc;
      for (long i = a; i < a + W; ++i)
        if (o.L[i - 1] < 0 && o.L[i] >= 0) zc.push_back(i - 1 + o.L[i - 1] / (o.L[i - 1] - o.L[i]));
      if (zc.size() < 2) continue;
      const double fMeas = (zc.size() - 1) / (zc.back() - zc.front()) * kSr;
      const long i0 = static_cast<long>(zc.front()), i1 = static_cast<long>(zc.back());
      const double sMean = 1.0 - (D[static_cast<std::size_t>(i1)] - D[static_cast<std::size_t>(i0)]) / (i1 - i0);
      const double err = std::fabs(cents(fMeas / (kProbeHz * sMean)));
      if (err > worst) { worst = err; wAt = a / kSr; }
    }
    REQUIRE(worst < 1.0, "law %s, glide %.2f: the output's pitch departs from 1 - dD/dt by %.2f cents at %.2f s", law ? "exponential" : "tape", glide, worst, wAt);
  }
  return true;
}

// The hard read-speed invariant (D-006), over laws x moves x modulation:
// s in [0.25, 2.25] by construction (so the [0.25, 4] backstop never acts),
// never reversed, never past the line; a move longer than 3 s at the cap
// crossfades (the fallback) instead of gliding.
bool speedInvariant(Detail& d) {
  struct Move { double from, to; bool fallback; };
  const Move moves[] = {{500, 250, false}, {250, 500, false}, {12000, 10, true}, {10, 12000, true}, {300, 300.5, false}};
  struct Mod { double depth, rate, shape, spread; };
  const Mod mods[] = {{0, 0.6, 0, 0}, {20, 10, 0, 0}, {20, 10, 1, 100}};
  for (int law : {0, 1, 2, 3})  // repitch tape, repitch exponential, crossfade, jump
    for (const Move& mv : moves)
      for (const Mod& md : mods) {
        if (law >= 2 && mv.fallback) continue;  // a crossfade has no fallback to show
        auto e = mk({{"time.sync", 0}, {"time.ms", mv.from}, {"time.law", law == 1 ? 1.0 : 0.0}, {"time.change", law >= 2 ? law - 1.0 : 0.0},
                     {"time.glide", 0.3}, {"fb.amount", 0},
                     {"mod.depthMs", md.depth}, {"mod.rateHz", md.rate}, {"mod.shape", md.shape}, {"mod.spread", md.spread}});
        const long n = S(mv.from > 1000 || mv.to > 1000 ? 13.5 : 3.0), at = n - S(1.4);
        const Vec in(n, 0.1);
        e->resetStats();
        run(*e, in, in, {set(at, "time.ms", mv.to)});
        for (int k = 0; k < 2; ++k) {
          const auto st = e->speedStats(k);
          REQUIRE(st.sMin >= 0.25 - 1e-12 && st.sMax <= 2.25 + 1e-9 && st.reverse == 0 && st.clamps == 0 && st.overruns == 0 && st.floors == 0,
                  "law %d, %g -> %g ms, mod %g ms @ %g Hz shape %g: speed [%.4f, %.4f], %llu reverse, %llu backstop, %llu overruns", law,
                  mv.from, mv.to, md.depth, md.rate, md.shape, st.sMin, st.sMax, static_cast<unsigned long long>(st.reverse),
                  static_cast<unsigned long long>(st.clamps), static_cast<unsigned long long>(st.overruns));
          REQUIRE((st.fallbacks > 0) == mv.fallback, "law %d, %g -> %g ms: fallback %s", law, mv.from, mv.to, mv.fallback ? "expected, not taken" : "taken, not expected");
        }
      }
  return true;
}

// The modulation (SPEC §7): a sine of depth A moves the read by exactly +-A
// (after the 250 ms ramp), and the output IS the input read at that moving
// position; the depth is limited by the curve's slope (10 Hz caps a sine at
// 0.25 / (2 pi f / fs) samples); drift stays inside its depth; the offset never
// moves more than 0.25 sample a sample; mod.spread puts the right line's sine
// 90 degrees later.
bool modulation(Detail& d) {
  {  // depth and read
    auto e = mk({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}, {"mod.depthMs", 5},
                 {"mod.rateHz", 2}, {"mod.spread", 100}});
    const long n = S(2.0);
    const Vec in = sine(n, 100, 0.5);
    double lo = 1e9, hi = -1e9, worst = 0, maxStep = 0, prev = 0, r1AtPeak = 0;
    runEach(*e, in, in, {}, [](long) { return 120.0; }, [&](long i, Echo& x) {
      const double m = x.modOffset(0);
      if (i > 0) maxStep = std::max(maxStep, std::fabs(m - prev));
      prev = m;
      if (i < S(0.6)) return;
      if (m > hi) { hi = m; r1AtPeak = x.modOffset(1); }
      lo = std::min(lo, m);
    });
    REQUIRE(std::fabs(hi - 240) < 1.2 && std::fabs(lo + 240) < 1.2, "a 5 ms sine moves the read [%.2f, %.2f] samples, expected +-240", lo, hi);
    REQUIRE(std::fabs(r1AtPeak) < 3.0, "spread 100 %%: the right line's offset at the left's peak is %.2f, expected ~0 (90 degrees later)", r1AtPeak);
    REQUIRE(maxStep <= 0.25 + 1e-12, "the offset moved %.4f samples in one sample (slew limit 0.25)", maxStep);
    // the output is the input read at the moving position (Hermite at 100 Hz: < 1e-4)
    auto g = mk({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}, {"mod.depthMs", 5}, {"mod.rateHz", 2}});
    Vec pos(static_cast<std::size_t>(n));
    const auto og = runEach(*g, in, in, {}, [](long) { return 120.0; }, [&](long i, Echo& x) { pos[static_cast<std::size_t>(i)] = x.readPosition(0); });
    for (long i = S(0.6); i < n; ++i) {
      const double want = 0.5 * std::sin(2 * kPi * 100 * (i - pos[static_cast<std::size_t>(i)]) / kSr);
      worst = std::max(worst, std::fabs(og.L[i] - want));
    }
    REQUIRE(worst < 1e-4, "the output departs from the input read at the reported position by %.3g", worst);
  }
  {  // the slope limit: 20 ms at 10 Hz is capped at 0.25 / (2 pi 10 / fs)
    auto e = mk({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mod.depthMs", 20}, {"mod.rateHz", 10}});
    const long n = S(1.5);
    const Vec in(n, 0.1);
    double hi = 0;
    runEach(*e, in, in, {}, [](long) { return 120.0; }, [&](long i, Echo& x) { if (i > S(0.6)) hi = std::max(hi, std::fabs(x.modOffset(0))); });
    const double A = 0.25 / (2 * kPi * 10 / kSr);
    REQUIRE(std::fabs(hi - A) < 0.01 * A, "a 20 ms sine at 10 Hz reaches %.1f samples, the slope limit is %.1f", hi, A);
  }
  {  // drift: inside its (slope-limited) depth, every sample
    auto e = mk({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mod.depthMs", 20}, {"mod.rateHz", 10}, {"mod.shape", 1}, {"mod.seed", 7}});
    const long n = S(3.0);
    const Vec in(n, 0.1);
    double hi = 0;
    runEach(*e, in, in, {}, [](long) { return 120.0; }, [&](long i, Echo& x) { if (i > S(0.3)) hi = std::max(hi, std::fabs(x.modOffset(0))); });
    const double A = 0.25 / (3.5 * 10 / kSr);
    REQUIRE(hi <= A + 1e-9, "drift reached %.2f samples, its depth is %.2f (the Catmull-Rom overshoot must be clamped)", hi, A);
  }
  {  // a lifted depth limit (10 ms -> 100 ms) is slewed in, never stepped
    auto e = mk({{"time.sync", 0}, {"time.ms", 10}, {"time.change", 1}, {"fb.amount", 0}, {"mod.depthMs", 20}, {"mod.rateHz", 0.05}, {"mod.shape", 1}});
    const long n = S(2.0);
    const Vec in(n, 0.1);
    double maxStep = 0, prev = 0;
    runEach(*e, in, in, {set(S(0.6), "time.ms", 100)}, [](long) { return 120.0; }, [&](long i, Echo& x) {
      if (i > 0) maxStep = std::max(maxStep, std::fabs(x.modOffset(0) - prev));
      prev = x.modOffset(0);
    });
    REQUIRE(maxStep <= 0.25 + 1e-12, "a lifted depth limit moved the offset %.3f samples in one sample", maxStep);
  }
  return true;
}

// Seeded modulation (SPEC §7, §9): the drift comes from mod.seed alone (two
// instances agree; another seed differs; reset reproduces), and a seed change
// or a shape switch starts from the value being output, so the offset never
// jumps (its step stays at the curve's own tiny slope, far under the 0.25 slew).
bool modSeeded(Detail& d) {
  // Depth 5 ms (240 samples): a 250 ms blend between the sine and the drift
  // moves the offset at most 2*240/12000 = 0.04 sample a sample; a switch with
  // no blend saturates the 0.25 slew instead.
  auto render = [](double seed, std::vector<Event> ev, double shape = 1) {
    auto e = mk({{"time.sync", 0}, {"time.ms", 200}, {"fb.amount", 0}, {"mod.depthMs", 5}, {"mod.rateHz", 0.05}, {"mod.shape", shape}, {"mod.seed", seed}});
    const long n = S(2.0);
    const Vec in(n, 0.1);
    Vec m(static_cast<std::size_t>(n));
    runEach(*e, in, in, std::move(ev), [](long) { return 120.0; }, [&](long i, Echo& x) { m[static_cast<std::size_t>(i)] = x.modOffset(0); });
    return m;
  };
  const Vec a = render(7, {}), b = render(7, {}), c = render(8, {});
  REQUIRE(a == b, "the same seed gave different drift");
  REQUIRE(a != c, "a different seed gave the same drift");
  auto stepAfter = [](const Vec& m, long at) {
    double s = 0;
    for (long i = at; i < at + S(0.3); ++i) s = std::max(s, std::fabs(m[static_cast<std::size_t>(i)] - m[static_cast<std::size_t>(i - 1)]));
    return s;
  };
  const Vec sc = render(7, {set(S(1.0), "mod.seed", 99)});
  REQUIRE(stepAfter(sc, S(1.0)) < 0.02, "a seed change moved the offset %.3f samples in one sample (a step, slewing)", stepAfter(sc, S(1.0)));
  const Vec sh = render(7, {set(S(1.0), "mod.shape", 0)});
  REQUIRE(stepAfter(sh, S(1.0)) < 0.1, "a shape switch moved the offset %.3f samples in one sample (no blend)", stepAfter(sh, S(1.0)));
  return true;
}

// Tempo moves (SPEC §3.8): on a 1.25 %/s ramp, "follow" tracks the grid time
// exactly with no crossfade, "glide" moves by the repitch law with no
// crossfade, "crossfade" crossfades and never bends the pitch; a 2x tempo step
// crossfades in "follow" (past its 50-cent cap). A KEY move under "follow"
// still obeys time.change (a repitch glide here).
bool tempoModes(Detail& d) {
  const long n = S(3.0), r0 = S(0.5), r1 = S(1.5);
  auto ramp = [=](long i) { return i < r0 ? 120.0 : i < r1 ? 120.0 + 1.5 * (i - r0) / static_cast<double>(r1 - r0) : 121.5; };
  const Vec in(n, 0.1);
  for (int mode : {0, 1, 2}) {  // crossfade, glide, follow
    auto e = mk({{"time.sync", 1}, {"time.div", 9}, {"time.tempoMode", static_cast<double>(mode)}, {"fb.amount", 0}, {"time.glide", 0.05}});
    double worstFollow = 0;
    e->resetStats();
    runEach(*e, in, in, {}, ramp, [&](long i, Echo& x) {
      if (mode == 2 && i > r0 && i < r1) worstFollow = std::max(worstFollow, std::fabs(x.headDelay(0) - x.resolvedTimes()[0] * kSr));
    });
    const auto st = e->speedStats(0);
    const auto fades = e->fadeCount(0);
    if (mode == 0) REQUIRE(fades > 10 && std::fabs(st.sMax - 1) < 1e-12 && std::fabs(st.sMin - 1) < 1e-12,
                           "crossfade: %llu fades, speed [%.6f, %.6f] (must crossfade and never bend)", static_cast<unsigned long long>(fades), st.sMin, st.sMax);
    if (mode == 1) REQUIRE(fades == 0 && st.sMax > 1.0, "glide: %llu fades, max speed %.6f (must glide by the law, no crossfade)", static_cast<unsigned long long>(fades), st.sMax);
    if (mode == 2) REQUIRE(fades == 0 && worstFollow < 1e-6, "follow: %llu fades, off the grid time by %.3g samples", static_cast<unsigned long long>(fades), worstFollow);
  }
  {  // follow: a 2x step crossfades; a key move glides
    auto e = mk({{"time.sync", 1}, {"time.div", 9}, {"time.tempoMode", 2}, {"fb.amount", 0}});
    e->resetStats();
    run(*e, in, in, {}, 120.0, [](long) { return 480; });
    run(*e, in, in, {}, 240.0, [](long) { return 480; });
    // The step arrives as a one-block ramp (the tempo is interpolated per
    // sample, §3.3), so the queue may fade twice (to an early target, then the
    // latest): what matters is that it crossfades, never follows past the cap.
    const auto f1 = e->fadeCount(0);
    REQUIRE(f1 >= 1 && e->speedStats(0).clamps == 0 && e->speedStats(0).sMax <= std::pow(2.0, 50.0 / 1200) + 1e-9 && e->headDelay(0) == 12000.0,
            "follow: a 2x tempo step must crossfade (%llu fades, %llu backstop hits, max speed %.4f, head %.1f)",
            static_cast<unsigned long long>(f1), static_cast<unsigned long long>(e->speedStats(0).clamps), e->speedStats(0).sMax, e->headDelay(0));
    run(*e, in, in, {set(S(0.5), "time.div", 12)}, 240.0, [](long) { return 480; });
    REQUIRE(e->fadeCount(0) == f1, "follow: a key move must obey time.change (repitch), not crossfade");
  }
  return true;
}

// The tail clock holds while a head moves (D-025, critic M2-H1): a shortening
// glide still sounds repeats from the old, longer time, and the declared tail
// is built from the target. At fb 0 a 2900 -> 300 ms move is never cut.
bool tailDuringGlide(Detail& d) {
  for (int law : {0, 1})
    for (double glide : {0.3, 2.0}) {
      auto e = mk({{"time.sync", 0}, {"time.ms", 2900}, {"time.law", static_cast<double>(law)}, {"time.glide", glide}, {"fb.amount", 0}, {"mix", 100}});
      const long n = S(10.0), at = S(3.0);
      Vec in = clickProbeUntil(n, at);
      const auto o = run(*e, in, in, {set(at, "time.ms", 300)});
      const long z = finalZeroRun(o);
      REQUIRE(z < n && std::fabs(o.L[z - 1]) < 1e-6, "law %d, glide %.1f: rest cut a sounding repeat at %.3g (sample %ld)", law, glide, o.L[z - 1], z);
    }
  return true;
}

// mod.seed (critic M2-H2): re-sending an unchanged seed changes nothing, and a
// load derives the drift from the patch's seed alone (D-026): two instances
// with different histories that load the same patch modulate identically.
bool seedRules(Detail& d) {
  auto offsets = [](double seed0, bool resend, bool load) {
    auto e = mk({{"time.sync", 0}, {"time.ms", 200}, {"fb.amount", 0}, {"mod.depthMs", 5}, {"mod.rateHz", 3}, {"mod.shape", 1}, {"mod.seed", seed0}});
    const long n = S(2.0);
    const Vec in(n, 0.1);
    std::vector<Event> ev;
    if (resend)
      for (long i = 0; i < n; i += 64) ev.push_back(set(i, "mod.seed", seed0));
    if (load) ev.push_back({S(0.5), [](Echo& x) { const std::array<echo::KeyValue, 1> kv{{{"mod.seed", 9}}}; x.loadState(kv); }});
    Vec m(static_cast<std::size_t>(n));
    runEach(*e, in, in, ev, [](long) { return 120.0; }, [&](long i, Echo& x) { m[static_cast<std::size_t>(i)] = x.modOffset(0); });
    return m;
  };
  REQUIRE(offsets(7, true, false) == offsets(7, false, false), "re-sending an unchanged mod.seed changed the drift");
  const Vec a = offsets(1, false, true), b = offsets(2, false, true);
  double diff = 0;
  for (long i = S(0.6); i < S(2.0); ++i) diff = std::max(diff, std::fabs(a[static_cast<std::size_t>(i)] - b[static_cast<std::size_t>(i)]));
  REQUIRE(diff < 1e-9, "two instances that loaded the same seed modulate %.1f samples apart (history leaked into the patch)", diff);
  return true;
}

// Key moves and follow (§3.8, critic M2-H4, M1, M2): a tempo ramp never turns a
// key move in flight into a tempo move; after a 10 bpm step, follow returns to
// exact following on a slow ramp (it crossfades the step, not the ramp); and
// it never keeps a residual off the grid.
bool keyAndFollow(Detail& d) {
  {  // a key glide in flight under tempoMode crossfade, then a tempo ramp: no crossfade
    auto e = mk({{"time.sync", 1}, {"time.div", 6}, {"time.tempoMode", 0}, {"time.glide", 0.5}, {"fb.amount", 0}});
    const long n = S(2.0);
    const Vec in(n, 0.1);
    runEach(*e, in, in, {set(S(0.3), "time.div", 9)}, [](long i) { return i < S(0.35) ? 120.0 : 120.0 + 2.0 * (i - S(0.35)) / kSr; },
            [](long, Echo&) {});
    REQUIRE(e->fadeCount(0) == 0, "a key glide in flight crossfaded when the tempo moved (%llu fades)", static_cast<unsigned long long>(e->fadeCount(0)));
  }
  {  // follow: a 120 -> 130 step, then a +1 bpm/s ramp inside the cap
    auto e = mk({{"time.sync", 1}, {"time.div", 9}, {"time.tempoMode", 2}, {"fb.amount", 0}});
    const long n = S(4.0);
    const Vec in(n, 0.1);
    double worst = 0;
    runEach(*e, in, in, {}, [](long i) { return i < S(0.5) ? 120.0 : 130.0 + 1.0 * (i - S(0.5)) / kSr; }, [&](long i, Echo& x) {
      if (i > S(1.5)) worst = std::max(worst, std::fabs(x.headDelay(0) - x.resolvedTimes()[0] * kSr));
    });
    REQUIRE(e->fadeCount(0) <= 2 && worst < 1e-6, "follow after a step: %llu fades, off the grid by %.3g samples (it must resume exact following)",
            static_cast<unsigned long long>(e->fadeCount(0)), worst);
  }
  {  // follow: a key glide, then a ramp, then a steady tempo: no residual left
    // glide 0.05: the key move's own law finishes it (D-025's key rule) well
    // before the steady stretch, so any offset left there is follow's.
    auto e = mk({{"time.sync", 1}, {"time.div", 9}, {"time.tempoMode", 2}, {"time.glide", 0.05}, {"fb.amount", 0}});
    const long n = S(5.0);
    const Vec in(n, 0.1);
    runEach(*e, in, in, {set(S(0.2), "time.div", 12)},
            [](long i) { return i < S(0.25) ? 120.0 : i < S(2.0) ? 120.0 + 1.0 * (i - S(0.25)) / kSr : 121.75; }, [](long, Echo&) {});
    const double off = std::fabs(e->headDelay(0) - e->resolvedTimes()[0] * kSr);
    REQUIRE(off < 1e-6, "follow left the head %.4f samples off the grid at a steady tempo", off);
  }
  return true;
}

// The read floor and the wake (critic M2-M3, M4): a move to 1 ms under 20 ms
// of modulation never reads under 3 samples from the write head (counted); and
// after a wake the first repeat plays at the modulation's own speed, not
// slewing in from 0 (the offset lands on the curve at rest).
bool floorAndWake(Detail& d) {
  for (double ch : {0.0, 1.0, 2.0}) {
    // The move lands at 2.5 s, where the 0.3 Hz sine's offset is at -A (the
    // read pulled TOWARD the write head): the only side the floor can bind.
    auto e = mk({{"time.sync", 0}, {"time.ms", 500}, {"time.change", ch}, {"fb.amount", 0}, {"mod.depthMs", 20}, {"mod.rateHz", 0.3}});
    const long n = S(3.5);
    const Vec in(n, 0.1);
    e->resetStats();
    run(*e, in, in, {set(S(2.5), "time.ms", 1)});
    const auto f = e->speedStats(0).floors + e->speedStats(1).floors;
    REQUIRE(f == 0, "time.change %g, 500 -> 1 ms under 20 ms modulation: %llu reads under the 3-sample floor", ch, static_cast<unsigned long long>(f));
  }
  {
    const long n = S(3.0);
    Vec in = zeros(n);
    for (long i = 0; i < S(0.3); ++i) in[i] = 0.1;
    for (long i = S(2.0); i < n; ++i) in[i] = 0.1;  // silence long enough to rest, then a wake
    // the speed right after the wake: from the speed statistics of a fresh window
    auto g = mk({{"time.sync", 0}, {"time.ms", 30}, {"fb.amount", 0}, {"mod.depthMs", 10}, {"mod.rateHz", 0.6}});
    const Vec pre(in.begin(), in.begin() + S(2.0)), post(in.begin() + S(2.0), in.end());
    run(*g, pre, pre);
    REQUIRE(g->atRest(), "the instance did not come to rest before the wake");
    g->resetStats();
    run(*g, Vec(post.begin(), post.begin() + S(0.05)), Vec(post.begin(), post.begin() + S(0.05)));
    const double lo = g->speedStats(0).sMin;
    REQUIRE(lo > 0.95, "after a wake the read played at speed %.3f: the offset slewed in from 0 instead of landing on the curve", lo);
  }
  return true;
}

// Sample-rate independence of moving time (1.0 bar row 40, critic M2-M6): the
// same exponential glide, tape glide and modulation at 48 and 96 kHz trace the
// same delay in SECONDS within a sample's worth.
bool movingTimeSr(Detail& d) {
  for (int law : {0, 1}) {
    std::vector<double> ref;
    for (double sr : {48000.0, 96000.0}) {
      auto e = mk({{"time.sync", 0}, {"time.ms", 500}, {"time.law", static_cast<double>(law)}, {"time.glide", 0.2}, {"fb.amount", 0}}, sr);
      const long n = S(2.0, sr);
      const Vec in(n, 0.1);
      std::vector<double> tr;
      runEach(*e, in, in, {set(S(0.5, sr), "time.ms", 300)}, [](long) { return 120.0; }, [&](long i, Echo& x) {
        if (i % S(0.01, sr) == 0) tr.push_back(x.headDelay(0) / sr);
      });
      if (ref.empty()) { ref = tr; continue; }
      double worst = 0;
      for (std::size_t i = 0; i < std::min(ref.size(), tr.size()); ++i) worst = std::max(worst, std::fabs(tr[i] - ref[i]));
      REQUIRE(worst < 2.0 / 48000, "law %d: the glide at 96 kHz departs from 48 kHz's by %.3g ms", law, worst * 1000);
    }
  }
  return true;
}

// The click metric (D-009 M2): max|d2y| / (A (omega s / fs)^2) over a render
// of the 431.7 Hz probe, with s FIXED at the declared bound 2.25 (critic
// M2-H3: normalising by the render's own measured top speed let a speed fault
// hide itself). A smooth sine read at any speed up to 2.25 reads <= 1.
constexpr double kSDecl = 2.25;
double clickM2(const Vec& y, long a, long b, double A) {
  const double w = 2 * kPi * kProbeHz / kSr * kSDecl;
  double m = 0;
  for (long i = std::max(a, 1L); i < b - 1; ++i) m = std::max(m, std::fabs(y[i + 1] - 2 * y[i] + y[i - 1]));
  return m / (A * w * w);
}
// The smallest single-sample click the metric sees at threshold th, in dBFS
// (an impulse of size a reads |d2y| = 2a).
double clickFloorDbfs(double th, double A) {
  const double w = 2 * kPi * kProbeHz / kSr * kSDecl;
  return 20 * std::log10(th * A * w * w / 2);
}

// The threshold: PROPOSED (D-023), ONE value for every mode, set between the
// measured clean and faulted readings (`echo_plants --click-report`, s fixed at
// 2.25): every clean scenario reads <= 1.91 (jump's back-to-back 3 ms
// crossfades under the full sweep 1.89, the tape law's cap kink 1.91); every
// fault reads >= 3.5 (integer-rounded reads 3.5, a planted 0.02 click 4.9, a
// read held per block 120). Its floor: a single-sample click of -40 dBFS.
constexpr double kClickProposed = 2.5;
double kClickMax = kClickProposed;      // every mode
double kClickMaxJump = kClickProposed;  // jump (the same value; kept separate for the record)
bool g_report = false;

// One click scenario: the reading, and the speed invariants during it.
struct ClickRun { double m2; SpeedStatsView st; };

// The probe fades in (raised cosine, 50 ms): a sine that starts abruptly
// writes a slope break into the line at its first sample, and any head that
// later reads across it plays that break back as a "click" the engine never
// made (it read 10.2 in the crossfade tempo mode before this ramp).
Vec clickProbe(long n) {
  Vec in = sine(n, kProbeHz, 0.5);
  for (long i = 0; i < S(0.05); ++i) in[i] *= 0.5 - 0.5 * std::cos(kPi * i / S(0.05));
  return in;
}

struct ClickCase {
  const char* name;
  bool jump;                                       // judged against the jump threshold
  std::vector<std::pair<const char*, double>> kv;  // owned: an initializer_list here would dangle
  std::function<std::vector<Event>(long)> events;  // given n
  std::function<double(long)> bpm;                 // per-sample tempo, or null
  double seconds;
};

ClickRun clickRun(const ClickCase& c) {
  const long n = S(c.seconds);
  const Vec in = clickProbe(n);
  auto e = mk({});
  for (const auto& [key, v] : c.kv)
    if (!e->setParam(key, v)) { std::fprintf(stderr, "click case %s: setParam(%s) refused\n", c.name, key); std::abort(); }
  e->resetStats();
  Out o;
  if (c.bpm) o = runEach(*e, in, in, c.events ? c.events(n) : std::vector<Event>{}, c.bpm, [](long, Echo&) {});
  else o = run(*e, in, in, c.events ? c.events(n) : std::vector<Event>{}, 120.0, [](long) { return 512; });
  const auto st = e->speedStats(0), st1 = e->speedStats(1);
  SpeedStatsView v{std::min(st.sMin, st1.sMin), std::max(st.sMax, st1.sMax), st.clamps + st1.clamps, st.floors + st1.floors + st.overruns + st1.overruns};
  return {clickM2(o.L, S(0.2), n, 0.5), v};
}

Vec clickProbeUntil(long n, long until) {
  Vec in = clickProbe(n);
  for (long i = until; i < n; ++i) in[i] = 0.0;
  return in;
}

std::vector<ClickCase> clickCases() {
  auto sweep = [](double lo, double hi) {
    return [lo, hi](long n) {
      std::vector<Event> ev;
      for (long i = 0; i < n; i += 64) {
        const double ph = std::sin(2 * kPi * 1.0 * i / kSr);
        ev.push_back(set(i, "time.ms", std::exp(std::log(lo) + (std::log(hi) - std::log(lo)) * (0.5 + 0.5 * ph))));
      }
      return ev;
    };
  };
  auto move = [](double at, double ms) { return [at, ms](long) { return std::vector<Event>{set(S(at), "time.ms", ms)}; }; };
  auto tempo = [](long i) { return 120.0 + 60.0 * std::sin(2 * kPi * i / kSr); };
  return {
      {"full sweep, repitch tape", false, {{"time.sync", 0}, {"time.ms", 500}, {"time.change", 0}, {"time.law", 0}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, sweep(1, 12000), nullptr, 3.0},
      {"full sweep, repitch exponential", false, {{"time.sync", 0}, {"time.ms", 500}, {"time.change", 0}, {"time.law", 1}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, sweep(1, 12000), nullptr, 3.0},
      {"full sweep, crossfade", false, {{"time.sync", 0}, {"time.ms", 500}, {"time.change", 1}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, sweep(1, 12000), nullptr, 3.0},
      {"full sweep, jump", true, {{"time.sync", 0}, {"time.ms", 500}, {"time.change", 2}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, sweep(1, 12000), nullptr, 3.0},
      {"200-400 ms sweep, tape, glide 0.05", false, {{"time.sync", 0}, {"time.ms", 300}, {"time.law", 0}, {"time.glide", 0.05}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, sweep(200, 400), nullptr, 3.0},
      {"200-400 ms sweep, exponential, glide 0.05", false, {{"time.sync", 0}, {"time.ms", 300}, {"time.law", 1}, {"time.glide", 0.05}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, sweep(200, 400), nullptr, 3.0},
      {"tempo sweep, crossfade", false, {{"time.sync", 1}, {"time.div", 9}, {"time.tempoMode", 0}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, nullptr, tempo, 3.0},
      {"tempo sweep, glide", false, {{"time.sync", 1}, {"time.div", 9}, {"time.tempoMode", 1}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, nullptr, tempo, 3.0},
      {"tempo sweep, follow", false, {{"time.sync", 1}, {"time.div", 9}, {"time.tempoMode", 2}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, nullptr, tempo, 3.0},
      {"move 250 -> 500 ms, exponential", false, {{"time.sync", 0}, {"time.ms", 250}, {"time.law", 1}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, move(1.0, 500), nullptr, 2.5},
      {"move 300 -> 200 ms, exponential, glide 0.05", false, {{"time.sync", 0}, {"time.ms", 300}, {"time.law", 1}, {"time.glide", 0.05}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, move(1.0, 200), nullptr, 2.0},
      {"move 500 -> 100 ms, tape, glide 0.05", false, {{"time.sync", 0}, {"time.ms", 500}, {"time.law", 0}, {"time.glide", 0.05}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, move(1.0, 100), nullptr, 2.0},
      {"move 500 -> 250 ms, tape", false, {{"time.sync", 0}, {"time.ms", 500}, {"time.law", 0}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, move(1.0, 250), nullptr, 2.5},
      {"move 500 -> 250 ms, jump", true, {{"time.sync", 0}, {"time.ms", 500}, {"time.change", 2}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, move(1.0, 250), nullptr, 1.5},
  };
}

// The ROADMAP M2 gate: every time-change mode under a 1 Hz full-range time
// sweep, the laws under a sweep they can follow, every tempo mode under a 1 Hz
// tempo sweep, and Q-A1's single moves, each under its threshold AND inside the
// speed invariants (no backstop hit, no floor or overrun, s <= 2.25: a bare
// jump is caught here, where the backstop turns it into a counted glide). The
// metric's own control: a planted click in a clean render must read over.
bool clickSweep(Detail& d) {
  for (const auto& c : clickCases()) {
    const ClickRun r = clickRun(c);
    if (g_report)
      std::printf("  REPORT click M2 %-45s %6.3f  (speed %.3f..%.3f, %llu backstop, %llu floor/overrun)\n", c.name, r.m2, r.st.sMin, r.st.sMax,
                  static_cast<unsigned long long>(r.st.clamps), static_cast<unsigned long long>(r.st.floors));
    REQUIRE(r.st.clamps == 0 && r.st.floors == 0 && r.st.sMax <= kSDecl + 1e-9 && r.st.sMin >= 0.25 - 1e-12,
            "%s: speed %.3f..%.3f, %llu backstop hits, %llu floor/overrun reads", c.name, r.st.sMin, r.st.sMax,
            static_cast<unsigned long long>(r.st.clamps), static_cast<unsigned long long>(r.st.floors));
    const double lim = c.jump ? kClickMaxJump : kClickMax;
    REQUIRE(r.m2 <= lim, "%s reads M2 %.3f (threshold %.3f)", c.name, r.m2, lim);
  }
  auto e = mk({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}});
  const long n = S(2.0);
  auto o = run(*e, clickProbe(n), clickProbe(n));
  o.L[S(1.0)] += 0.02;
  const double c = clickM2(o.L, S(0.2), n, 0.5);
  if (g_report)
    std::printf("  REPORT click M2 planted 0.02 click (control): %.3f; at the threshold %.2f the floor is a %.1f dBFS single-sample click\n", c,
                kClickProposed, clickFloorDbfs(kClickProposed, 0.5));
  REQUIRE(c > std::min(kClickMax, kClickProposed), "a planted 0.02 click reads only M2 %.3f: the metric cannot see clicks", c);
  return true;
}

// ---- the registry ---------------------------------------------------------------

// ---- M3 B2, T-013: FOUNDATIONS' FX-operator ABI and the declarations (row A1) ----

// properties() passes the vendored header's validate() once prepared, at every
// rate horde runs, and is refused before prepare(): a forgotten declaration is
// an absence (FOUNDATIONS #129). The values are the spec's (§10, §16.5).
bool abiProperties(Detail& d) {
  {
    Echo fresh;
    REQUIRE(!fresh.properties().validate(), "an unprepared instance's properties() validated");
  }
  for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
    auto e = mk({{"time.sync", 0}, {"time.ms", 250}, {"fb.amount", 50}}, sr);
    const auto p = e->properties();
    const auto v = p.validate();
    REQUIRE(static_cast<bool>(v), "validate() refused at %.0f Hz (violation %d)", sr, static_cast<int>(v.violation));
    REQUIRE(p.latency->samples == 0 && p.latency->residual == 0.0 && p.latency->sign == foundations::ResidualSign::kNone,
            "latency at %.0f Hz is not an exact 0", sr);
    REQUIRE(p.tail->may_change, "tail.may_change is false at %.0f Hz (the tail follows feedback, time and depth)", sr);
    REQUIRE(static_cast<std::int64_t>(p.tail->samples) == e->tail(), "tail %u != the engine's %lld at %.0f Hz", p.tail->samples,
            static_cast<long long>(e->tail()), sr);
    REQUIRE(p.min_block && *p.min_block == 1u, "min_block is not 1 at %.0f Hz", sr);
    // The bound against the REAL fade (critic T-013 MED-1): the formula above
    // cannot catch a fade that outlasts it. Ringing repeats, a load, then exact
    // zero from the declared bound on, and sound just before it.
    REQUIRE(p.load, "no load bound declared at %.0f Hz", sr);
    auto r = mk({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 80}, {"mix", 100}}, sr);
    const long n = S(1.0, sr), at = S(0.5, sr), b = static_cast<long>(p.load->transition_samples);
    const std::array<echo::KeyValue, 1> patch{{{"fb.amount", 30}}};
    const auto o = run(*r, impulse(n), zeros(n), {{at, [&patch](Echo& x) { x.loadState(patch); }}});
    REQUIRE(maxAbs(o.L, at + b) == 0.0, "at %.0f Hz the load's old audio still sounds %.3g after the declared %ld samples", sr,
            maxAbs(o.L, at + b), b);
    REQUIRE(maxAbs(o.L, at + b - S(0.002, sr), at + b) > 0.0, "at %.0f Hz the load ended well before its declared bound", sr);
    const auto fade = static_cast<std::uint32_t>(std::lround(0.020 * sr));
    REQUIRE(p.load && p.load->transition_samples == fade, "load bound %u, expected the 20 ms fade = %u at %.0f Hz",
            p.load ? p.load->transition_samples : 0u, fade, sr);
  }
  auto held = mk({{"fb.amount", 100}});
  REQUIRE(held->properties().tail->samples == foundations::kInfiniteTail, "a held loop (feedback 100 %%) does not declare kInfiniteTail");
  return true;
}

// SPEC §16.5: the 64-bit tail maps onto TailDecl's 32 bits by saturating. T-013's
// own case FIRST (critic T-013 MED-2): 99.99 % feedback, 12 s, 192 kHz, where the
// engine's tail is ~3.3e11 samples (n = 145 099 passes of ~12 s) and a narrowing
// would wrap to a short tail. Then the boundary and past it. (A plain narrowing
// maps 2^32 - 1 and INT64_MAX to 0xFFFFFFFF by coincidence, so those two cannot
// see the plant; the engine case and 2^32 + 5 can.)
bool abiTail32(Detail& d) {
  using foundations::kInfiniteTail;
  constexpr std::int64_t kMax32 = 0xFFFFFFFFLL;
  {
    auto e = mk({{"time.sync", 0}, {"time.ms", 12000}, {"fb.amount", 99.99}}, 192000.0);
    REQUIRE(e->tail() > kMax32, "the case is vacuous: the engine tail %lld fits 32 bits", static_cast<long long>(e->tail()));
    const auto p = e->properties();
    REQUIRE(p.tail && p.tail->samples == kInfiniteTail, "99.99 %%, 12 s, 192 kHz declares %u samples, not kInfiniteTail",
            p.tail ? p.tail->samples : 0u);
  }
  REQUIRE(echo::tailDecl32(0) == 0u && echo::tailDecl32(48000) == 48000u, "small tails are not passed through");
  REQUIRE(echo::tailDecl32(kMax32 - 1) == 0xFFFFFFFEu, "2^32 - 2 is not passed through");
  REQUIRE(echo::tailDecl32(kMax32) == kInfiniteTail, "2^32 - 1 does not declare kInfiniteTail");
  REQUIRE(echo::tailDecl32(kMax32 + 6) == kInfiniteTail, "2^32 + 5 maps to %u, not kInfiniteTail", echo::tailDecl32(kMax32 + 6));
  REQUIRE(echo::tailDecl32(echo::kInfiniteTail) == kInfiniteTail, "the engine's own infinity is not kInfiniteTail");
  return true;
}

// RS-2.2: every parameter has an OQ #33 class (both fields declared), and the
// local enum maps one to one: distinct local classes never share a pair.
bool abiParamClasses(Detail& d) {
  using K = echo::oq33::MorphKind;
  using M = echo::oq33::MorphParticipation;
  for (const auto& p : echo::kParams) {
    const auto c = echo::oq33Class(p.morph);
    REQUIRE(c.kind != K::kUndeclared && c.participation != M::kUndeclared, "%.*s has no OQ #33 class",
            static_cast<int>(p.key.size()), p.key.data());
    REQUIRE((c.participation == M::kExcluded) == (p.morph == echo::Morph::Device), "%.*s: excluded iff Device is broken",
            static_cast<int>(p.key.size()), p.key.data());
    // The mapped kind must match the key's own shape (critic T-013 LOW-1): Device
    // maps to kContinuous by the lead's reading (SPEC §9 rules only "excluded"),
    // which is right only while every Device key is a scalar.
    REQUIRE((c.kind == K::kStepped) == p.stepped, "%.*s: kind %s but the key is %s", static_cast<int>(p.key.size()), p.key.data(),
            c.kind == K::kStepped ? "stepped" : "continuous", p.stepped ? "stepped" : "a scalar");
  }
  const echo::Morph all[] = {echo::Morph::Continuous, echo::Morph::Jump, echo::Morph::Device};
  for (auto a : all)
    for (auto b : all) {
      const auto ca = echo::oq33Class(a), cb = echo::oq33Class(b);
      REQUIRE(a == b || ca.kind != cb.kind || ca.participation != cb.participation, "two local classes share one OQ #33 pair");
    }
  return true;
}

// process() is safe in place (inL == outL, inR == outR): bit-identical to the
// same render out of place, in every routing, at block sizes 1, 64 and 4096,
// through a wake from rest. Settles T-007's signature item.
bool abiInPlace(Detail& d) {
  for (int route = 0; route < 3; ++route)
    for (int block : {1, 64, 4096}) {
      g_rng = 7;
      const long n = S(0.5);
      Vec inL = zeros(n), inR = zeros(n);
      for (long i = S(0.05); i < S(0.3); ++i) { inL[i] = 0.5 * noise(); inR[i] = 0.5 * noise(); }
      auto a = mk({{"route.mode", static_cast<double>(route)}, {"time.sync", 0}, {"time.ms", 40}, {"fb.amount", 60}, {"mix", 50}});
      auto b = mk({{"route.mode", static_cast<double>(route)}, {"time.sync", 0}, {"time.ms", 40}, {"fb.amount", 60}, {"mix", 50}});
      const Out ref = run(*a, inL, inR, {}, 120.0, [block](long) { return block; });
      Vec L = inL, R = inR;
      for (long pos = 0; pos < n; pos += block) {
        const int len = static_cast<int>(std::min<long>(block, n - pos));
        b->process(L.data() + pos, R.data() + pos, L.data() + pos, R.data() + pos, len, 120.0);
      }
      REQUIRE(maxAbs(ref.L) > 0.01, "the case is vacuous: no output (route %d, block %d)", route, block);
      for (long i = 0; i < n; ++i)
        REQUIRE(L[i] == ref.L[i] && R[i] == ref.R[i], "in place differs at sample %ld (route %d, block %d): %.17g vs %.17g", i, route,
                block, L[i], ref.L[i]);
    }
  return true;
}

// ---- M3 B2, T-014: the meter taps and the I/O gain standard's tests 1-7 (row A6, B435) ----

// One block straight through process(), so a meter read after it is that block's.
void block(Echo& e, const Vec& L, const Vec& R) {
  Vec oL(L.size()), oR(R.size());
  e.process(L.data(), R.data(), oL.data(), oR.data(), static_cast<int>(L.size()), 120.0);
}

// Test 1: at 0 dB both trims are bit-identical to an unprocessed pass.
bool ioTest1Unity(Detail& d) {
  g_rng = 11;
  const long n = S(0.2);
  Vec L(n), R(n);
  for (long i = 0; i < n; ++i) { L[i] = 0.9 * noise(); R[i] = 1e-30 * noise(); }
  auto e = mk({{"mix", 0}});
  const auto o = run(*e, L, R);
  for (long i = 0; i < n; ++i)
    REQUIRE(o.L[i] == L[i] && o.R[i] == R[i], "at 0 dB sample %ld is %.17g / %.17g, not the input %.17g / %.17g", i, o.L[i], o.R[i], L[i], R[i]);
  return true;
}

// Test 3: after prepare at a saved +6 dB (set before prepare, as a host restores
// an instance), the very first sample is already at +6 dB: no ramp in.
bool ioTest3Opens(Detail& d) {
  for (double sr : {48000.0, 96000.0}) {
    Echo e;
    e.setParam("io.inGain", 6);
    e.setParam("io.outGain", 6);
    e.setParam("mix", 0);
    e.prepare(sr, 4096);
    const double G = std::pow(10.0, 6.0 / 20.0);
    double x = 0.25, y = 0;
    e.process(&x, &x, &y, &y, 1, 120.0);
    REQUIRE(y == 0.25 * G * G, "at %.0f Hz the first sample after prepare is %.17g, expected %.17g (+6 dB in and out)", sr, y, 0.25 * G * G);
  }
  return true;
}

// Test 4: a patch load and a corner flip (a second load, another corner's patch)
// leave both gains alone, a ramp in flight included. Bit-identical to the same
// run without the loads (mix 0 in every patch, so only the gains shape the output).
bool ioTest4Kept(Detail& d) {
  const long n = S(0.3), t = S(0.1);
  const Vec in(n, 0.25);
  const std::array<echo::KeyValue, 3> a{{{"mix", 0}, {"fb.amount", 30}, {"io.inGain", -12}}};
  const std::array<echo::KeyValue, 3> b{{{"mix", 0}, {"fb.amount", 70}, {"io.outGain", 12}}};
  auto ref = mk({{"mix", 0}, {"io.inGain", 6}, {"io.outGain", -6}});
  auto e = mk({{"mix", 0}, {"io.inGain", 6}, {"io.outGain", -6}});
  const auto r = run(*ref, in, in, {set(t, "io.outGain", 3)});
  // The first load starts before the gain ramp and its 20 ms fade ENDS mid-ramp
  // (where a load lands its patch); the corner flip lands inside the ramp too.
  const auto o = run(*e, in, in, {set(t, "io.outGain", 3), {t - 500, [&a](Echo& x) { x.loadState(a); }},
                                  {t + 200, [&b](Echo& x) { x.loadState(b); }}});
  for (long i = 0; i < n; ++i)
    REQUIRE(o.L[i] == r.L[i], "a load or corner flip moved the gains: sample %ld is %.17g, without the loads %.17g", i, o.L[i], r.L[i]);
  REQUIRE(e->getParam(echo::kInGain) == 6.0 && e->getParam(echo::kOutGain) == 3.0, "the Device keys changed across the loads");
  return true;
}

// Test 5: Input Gain sits before the loop, so +12 dB drives the returning term
// into the ceiling and changes the output's SHAPE; Output Gain at +12 dB scales
// the level only, exactly.
bool ioTest5Placement(Detail& d) {
  const long n = S(1.0);
  Vec in = zeros(n);
  for (long i = 0; i < n; i += S(0.1)) in[i] = 0.3;  // at +12 dB, 1.19: past the 0.9 knee
  const char* fb = "fb.amount";
  // The loop filter off, so the pulses return at full height: 0.9 x 1.19 = 1.07,
  // past the ceiling's 0.9 knee at +12 dB, while 0.9 x 0.3 stays linear at 0 dB.
  auto base = mk({{fb, 90}, {"mix", 50}, {"time.sync", 0}, {"time.ms", 50}, {"filt.loop.on", 0}});
  auto inG = mk({{fb, 90}, {"mix", 50}, {"time.sync", 0}, {"time.ms", 50}, {"filt.loop.on", 0}});
  auto outG = mk({{fb, 90}, {"mix", 50}, {"time.sync", 0}, {"time.ms", 50}, {"filt.loop.on", 0}});
  inG->setParam("io.inGain", 12);
  outG->setParam("io.outGain", 12);
  // Land the 20 ms ramps before the first pulse (they started at setParam).
  Vec pre = zeros(S(0.05)), sink = zeros(S(0.05));
  for (auto* x : {base.get(), inG.get(), outG.get()}) x->process(pre.data(), pre.data(), sink.data(), sink.data(), static_cast<int>(pre.size()), 120.0);
  const auto r0 = run(*base, in, in), ri = run(*inG, in, in), ro = run(*outG, in, in);
  const double G = std::pow(10.0, 12.0 / 20.0);
  for (long i = 0; i < n; ++i) REQUIRE(ro.L[i] == r0.L[i] * G, "Output Gain +12 dB is not a pure level change at sample %ld", i);
  double dev = 0, pk = 0;
  for (long i = 0; i < n; ++i) { dev = std::max(dev, std::fabs(ri.L[i] - r0.L[i] * G)); pk = std::max(pk, std::fabs(r0.L[i] * G)); }
  REQUIRE(pk > 0.1 && dev > 1e-3 * pk, "Input Gain +12 dB only scaled the level (shape change %.3g of peak %.3g): it does not drive the loop",
          dev, pk);
  // Input Gain is a TRUE trim before everything (critic T-014 H1): the render at
  // +12 dB equals a 0 dB instance fed the input pre-scaled by G, bit for bit
  // (x*G then *1.0 is the same product), so it reaches the loop, not only the dry.
  auto pre12 = mk({{fb, 90}, {"mix", 50}, {"time.sync", 0}, {"time.ms", 50}, {"filt.loop.on", 0}});
  pre12->process(pre.data(), pre.data(), sink.data(), sink.data(), static_cast<int>(pre.size()), 120.0);
  Vec inG12 = in;
  for (auto& v : inG12) v *= std::pow(10.0, 12.0 / 20.0);
  const auto rp = run(*pre12, inG12, inG12);
  for (long i = 0; i < n; ++i)
    REQUIRE(ri.L[i] == rp.L[i], "Input Gain +12 dB differs from a pre-scaled input at sample %ld (%.17g vs %.17g): not a trim before the loop",
            i, ri.L[i], rp.L[i]);
  return true;
}

// Test 6: Echo declares both keys, so horde's slot adds no gain of its own; a
// slot that adds a second pair is the planted double gain. The slot is simulated
// here by B435's rule: supply a gain only where the module declares none.
bool ioTest6Slot(Detail& d) {
  const auto in = echo::paramIndex("io.inGain"), out = echo::paramIndex("io.outGain");
  REQUIRE(in && out, "Echo does not declare both io.inGain and io.outGain");
  REQUIRE(echo::kParams[*in].morph == echo::Morph::Device && echo::kParams[*out].morph == echo::Morph::Device, "the I/O gains are not Device class");
  const bool slotIn = !in || echo::plant::doubleGain, slotOut = !out || echo::plant::doubleGain;
  const double dB = 6.0, G = std::pow(10.0, dB / 20.0);
  auto e = mk({{"mix", 0}, {"io.inGain", dB}, {"io.outGain", dB}});
  const long n = S(0.1);
  Vec x(n, 0.1);
  if (slotIn) for (auto& v : x) v *= G;  // the slot's own Input Gain, at the same mixer value
  auto o = run(*e, x, x);
  if (slotOut) for (auto& v : o.L) v *= G;
  const double got = o.L[n - 1], want = 0.1 * G * G;
  REQUIRE(std::fabs(got - want) < 1e-12, "through the slot +6 dB in and out reads %.6f, expected exactly one pair: %.6f", got, want);
  return true;
}

// The two taps (§16.1, RS-4.7): the input tap after Input Gain, the output tap
// after Output Gain, each the last block's peak. +6 dB in and -12 dB out make
// the two points distinguishable: an over at the input tap that the output
// tap does not see.
bool metersTaps(Detail& d) {
  // First (so its plant is caught HERE): the input tap reads the REPAIRED sample (§16.1, M1): +inf in is a counted
  // repair to 0, never a peak of inf or a latched over.
  // The +inf sits inside a live 0.1 signal: alone at rest it would be repaired to
  // 0 and never wake the instance, so no tap would see it either way (vacuous).
  auto r = mk({{"mix", 0}});
  Vec bad(64, 0.1);
  bad[10] = std::numeric_limits<double>::infinity();
  block(*r, bad, Vec(64, 0.1));
  REQUIRE(r->inputMeter().peak == 0.1 && !r->inputMeter().clip, "a +inf input reads peak %g, latch %d at the input tap (the raw sample)",
          r->inputMeter().peak, r->inputMeter().clip);
  auto e = mk({{"mix", 0}, {"io.inGain", 6}, {"io.outGain", -12}});
  const Vec z = zeros(S(0.05));
  block(*e, z, z);  // land the gain ramps
  const double Gi = std::pow(10.0, 6.0 / 20.0), Go = std::pow(10.0, -12.0 / 20.0);
  // The peak is on the RIGHT channel and NEGATIVE (critic T-014 H2): a tap that
  // read one channel, or a signed max, misses it.
  const Vec x(256, 0.3), xr(256, -0.6);
  block(*e, x, xr);
  const auto mi = e->inputMeter(), mo = e->outputMeter();
  REQUIRE(mi.peak == 0.6 * Gi, "input tap peak %.17g, expected 0.6 x Input Gain = %.17g (after the gain)", mi.peak, 0.6 * Gi);
  REQUIRE(mi.clip, "0.6 at +6 dB (%.3f) is over 0 dBFS at the input tap but did not latch", 0.6 * Gi);
  REQUIRE(std::fabs(mo.peak - 0.6 * Gi * Go) < 1e-15, "output tap peak %.17g, expected %.17g (after Output Gain)", mo.peak, 0.6 * Gi * Go);
  REQUIRE(!mo.clip, "the output tap latched at %.3f", mo.peak);
  block(*e, z, z);
  REQUIRE(e->inputMeter().peak == 0.0 && e->outputMeter().peak == 0.0, "a silent block left a peak: the peak is not per block");
  // The output tap is after EVERY path and the tail (H2): input stopped, the
  // repeats still sound, so the input tap reads 0 and the output tap does not.
  auto w = mk({{"mix", 100}, {"time.sync", 0}, {"time.ms", 10}, {"fb.amount", 50}});
  block(*w, impulse(256, 0, 0.5), zeros(256));
  block(*w, zeros(2048), zeros(2048));
  REQUIRE(w->inputMeter().peak == 0.0 && w->outputMeter().peak > 0.05, "after the input stops the output tap reads %.3g: blind to the wet tail",
          w->outputMeter().peak);
  return true;
}

// Test 7: both latches set on an over (|x| > 1.0; exactly 1.0 is full scale) and
// hold through silence, reset() and a load, until clearMeters() (B225, RS-6.13).
bool ioTest7Latches(Detail& d) {
  auto e = mk({{"mix", 0}});
  const Vec z(256, 0.0), one(256, 1.0);
  block(*e, one, one);
  REQUIRE(!e->inputMeter().clip && !e->outputMeter().clip, "a sample at exactly 1.0 latched a clip");
  Vec over = z;
  over[100] = 1.01;
  block(*e, over, z);
  REQUIRE(e->inputMeter().clip && e->outputMeter().clip, "an over of 1.01 did not latch both taps (in %d, out %d)", e->inputMeter().clip,
          e->outputMeter().clip);
  for (int k = 0; k < 40; ++k) block(*e, z, z);
  REQUIRE(e->inputMeter().clip && e->outputMeter().clip, "a latch cleared itself over 40 silent blocks");
  e->reset();
  const std::array<echo::KeyValue, 1> patch{{{"mix", 0}}};
  e->loadState(patch);
  block(*e, z, z);
  REQUIRE(e->inputMeter().clip && e->outputMeter().clip, "reset() or a load cleared a latch");
  e->clearMeters();
  REQUIRE(!e->inputMeter().clip && !e->outputMeter().clip, "clearMeters() did not clear");
  block(*e, z, z);
  REQUIRE(!e->inputMeter().clip && !e->outputMeter().clip, "a latch came back without an over");
  return true;
}

// ---- M3 B2, T-015: the saved state (SPEC §16.2; rows A17 - A20; echo/state.h) ----

std::string saveText(const Echo& e) {
  char b[echo::state::kMaxBytes];
  const std::size_t n = e.saveState(b, sizeof b);
  return std::string(b, n);
}
bool restore(Echo& e, std::string_view t, const char** why = nullptr) { return e.stageState(t, why) && e.adoptStaged(); }
std::string replace_once(std::string t, const std::string& from, const std::string& to) {
  const auto at = t.find(from);
  return at == std::string::npos ? std::string("<anchor lost>") : t.replace(at, from.size(), to);
}
bool g_reportedLocales = false;

// A patch that exercises every awkward value: thirds, a 0.1 sum, the extremes,
// the longest labels and a value a few ulps off a round number.
void awkward(Echo& e) {
  e.setParam("time.ms", 1000.0 / 3.0);
  e.setParam("mix", 100.0 / 3.0);
  e.setParam("fb.amount", 0.1 * 7.0);
  e.setParam("filt.loop.lpHz", std::nextafter(9000.0, 1e9));
  e.setParam("mod.rateHz", 0.05);
  e.setParam("time.glide", 0.01);
  e.setParam("time.offL", -50);
  e.setParam("time.offR", 49.999999999999993);
  e.setParam("mod.seed", 65535);
  e.setParam("time.div", 19);  // 3/128, the longest label
  e.setParam("route.mode", 2);
  e.setParam("time.tempoMode", 2);
  e.setParam("io.inGain", -24);
  e.setParam("io.outGain", 7.123456789012345);
}
// A random in-range patch from the test's own seeded stream.
void randomPatch(Echo& e) {
  for (int i = 0; i < echo::kNumParams; ++i) {
    const auto& p = echo::kParams[static_cast<std::size_t>(i)];
    const double u = 0.5 + 0.5 * noise();
    double v = p.min + (p.max - p.min) * u;
    if (p.stepped) v = std::floor(std::min(v, p.max));
    e.setParam(i, v);
  }
}

// The format (SPEC §16.2): version line, every patch key once in manifest order
// by name, then the [device] block.
bool stateFormat(Detail& d) {
  auto e = mk({});
  const std::string t = saveText(*e);
  REQUIRE(t.rfind("echo-state 1\n", 0) == 0, "the state does not open with its version line");
  for (const char* want : {"\ntime.sync sync\n", "\ntime.div 1/4\n", "\nroute.mode stereo\n", "\ntime.ms 375\n", "\nmix 30\n", "\nmod.seed 1\n",
                           "\nfilt.loop.on on\n"})
    REQUIRE(t.find(want) != std::string::npos, "the default state lacks the line `%.*s`", static_cast<int>(std::strlen(want)) - 2, want + 1);
  const std::string_view dev = "\n[device]\nio.inGain 0\nio.outGain 0\n";
  REQUIRE(t.size() > dev.size() && t.compare(t.size() - dev.size(), dev.size(), dev) == 0, "the [device] block is not last");
  std::size_t pos = t.find('\n') + 1;
  for (const auto& p : echo::kParams) {
    if (p.morph == echo::Morph::Device) continue;
    const std::size_t sp = t.find(' ', pos);
    REQUIRE(sp != std::string::npos && t.compare(pos, sp - pos, p.key) == 0, "key %.*s is not next in manifest order",
            static_cast<int>(p.key.size()), p.key.data());
    pos = t.find('\n', pos) + 1;
  }
  return true;
}

// A18: save -> load -> save is byte-identical, every value bit-equal, and the
// renders after each load are bit-identical; also in a comma locale.
bool stateExact(Detail& d) {
  g_rng = 99;
  for (int c = 0; c < 22; ++c) {
    auto a = mk({});
    if (c == 1) awkward(*a);
    if (c >= 2) randomPatch(*a);
    const std::string t1 = saveText(*a);
    auto b = mk({});
    const char* why = "";
    REQUIRE(restore(*b, t1, &why), "case %d: its own state was refused (%s)", c, why);
    const std::string t2 = saveText(*b);
    REQUIRE(t1 == t2, "case %d: save -> load -> save differs", c);
    for (int i = 0; i < echo::kNumParams; ++i)
      REQUIRE(a->getParam(i) == b->getParam(i), "case %d: %.*s read back %.17g, saved %.17g", c,
              static_cast<int>(echo::kParams[static_cast<std::size_t>(i)].key.size()), echo::kParams[static_cast<std::size_t>(i)].key.data(),
              b->getParam(i), a->getParam(i));
    if (c < 3) {
      auto x = mk({}), y = mk({});
      restore(*x, t1);
      restore(*y, t2);
      const Vec in = impulse(S(0.5));
      const auto ox = run(*x, in, in), oy = run(*y, in, in);
      for (long i = 0; i < S(0.5); ++i) REQUIRE(ox.L[i] == oy.L[i] && ox.R[i] == oy.R[i], "case %d: the renders after the two loads differ at %ld", c, i);
    }
    if (c == 1) {  // hosts in other locales: a comma, and a two-byte decimal point (critic T-015 M1)
      int ran = 0;
      for (const char* loc : {"de_DE.UTF-8", "ar_SA.UTF-8", "fa_IR.UTF-8"}) {
        if (!std::setlocale(LC_NUMERIC, loc)) continue;
        ++ran;
        const std::string tl = saveText(*a);
        auto z = mk({});
        const bool ok = restore(*z, tl);
        const std::string tz = saveText(*z);
        const bool comma = z->stageState(replace_once(t1, "\nmix ", "\nmix 3,5\nmix_ "));
        std::setlocale(LC_NUMERIC, "C");
        REQUIRE(tl == t1, "in %s the state text changed", loc);
        REQUIRE(ok && tz == t1, "in %s the state did not read back", loc);
        REQUIRE(!comma, "in %s a decimal comma was accepted", loc);
      }
      if (!g_reportedLocales) {
        std::printf("  REPORT state locales: %d of 3 test locales present here (the C-locale guard is still exercised by every save)\n", ran);
        g_reportedLocales = true;
      }
    }
    if (c == 2) {  // -0 is written as 0: one patch, one text (critic T-015 L1)
      auto m = mk({});
      m->setParam("mix", -0.0);
      REQUIRE(saveText(*m).find("\nmix 0\n") != std::string::npos, "-0 was not written as 0");
    }
  }
  return true;
}

// A17: a state saved mid-session, loaded into a fresh instance, renders
// bit-identically to the original after its reset(): nothing outside the saved
// state and reset() shapes the sound.
bool stateComplete(Detail& d) {
  g_rng = 5;
  auto a = mk({{"time.sync", 0}, {"time.ms", 80}, {"fb.amount", 70}, {"mod.depthMs", 2}, {"mod.shape", 1}, {"mod.seed", 77}});
  const long n = S(0.6);
  Vec in(n);
  for (auto& v : in) v = 0.3 * noise();
  run(*a, in, in, {set(S(0.1), "time.ms", 120), set(S(0.2), "fb.amount", 55), set(S(0.3), "io.outGain", -3), set(S(0.4), "route.mode", 1),
                   set(S(0.45), "filt.out.on", 1), set(S(0.5), "mod.seed", 4242)});
  const std::string t = saveText(*a);
  a->reset();
  auto b = mk({});
  const char* why = "";
  REQUIRE(restore(*b, t, &why), "the mid-session state was refused (%s)", why);
  const auto oa = run(*a, in, in), ob = run(*b, in, in);
  for (long i = 0; i < n; ++i) REQUIRE(oa.L[i] == ob.L[i] && oa.R[i] == ob.R[i], "the restored instance differs from the reset original at %ld", i);
  return true;
}

// A19: the worst case fits 2 KiB; a text of 2 KiB + 1 is refused FIRST, with no
// allocation; staging a valid state allocates nothing either.
bool stateSize(Detail& d) {
  auto w = mk({});
  for (int i = 0; i < echo::kNumParams; ++i) {  // the longest text per key: a negative value with 17 digits, or the longest label
    const auto& p = echo::kParams[static_cast<std::size_t>(i)];
    if (!p.stepped) w->setParam(i, p.min < 0 ? p.min + (p.max - p.min) * (1.0 / 3.0) * 1e-6 : p.min + (p.max - p.min) / 3.0);
  }
  w->setParam("time.div", 19);
  w->setParam("mod.seed", 65535);
  const std::string worst = saveText(*w);
  REQUIRE(!worst.empty() && worst.size() <= echo::state::kMaxBytes, "the worst-case state is %zu bytes (budget 2048)", worst.size());
  std::printf("  REPORT state size: defaults %zu B, worst case %zu B (budget 2048)\n", saveText(*mk({})).size(), worst.size());
  // No VALID state reaches 2 KiB (the worst case above is far under it), so the
  // boundary is shown on texts padded with leading zeros on time.ms (a 1.5 KiB
  // token, which the parser refuses on its own): at exactly 2048 bytes the text
  // must be refused for something OTHER than its size (the test is >, not >=);
  // at 2049 it must be refused for its size, FIRST, before any parsing.
  const std::string base = saveText(*mk({}));
  const std::size_t at = base.find("\ntime.ms ") + 9;
  auto padded = [&](std::size_t total) { return base.substr(0, at) + std::string(total - base.size(), '0') + base.substr(at); };
  auto e = mk({});
  const std::string at2048 = padded(2048), over = padded(2049);
  const char *why2048 = "", *whyOver = "", *whyOk = "";
  alloc_probe::arm();
  const bool okStaged = e->stageState(base, &whyOk);
  e->adoptStaged();
  const bool s2048 = e->stageState(at2048, &why2048);
  const bool sOver = e->stageState(over, &whyOver);
  alloc_probe::disarm();
  REQUIRE(at2048.size() == 2048 && over.size() == 2049, "the padding is wrong (%zu, %zu)", at2048.size(), over.size());
  REQUIRE(okStaged, "the default state was refused (%s)", whyOk);
  REQUIRE(!s2048 && std::string_view(why2048) != "over 2 KiB", "a 2048-byte text was refused as oversized: the budget is not inclusive");
  REQUIRE(!sOver && std::string_view(whyOver) == "over 2 KiB", "the 2049-byte text was refused for `%s`, not its size first", whyOver);
  REQUIRE(alloc_probe::g_allocs.load() == 0, "staging allocated %ld times", alloc_probe::g_allocs.load());
  return true;
}

// A20: a state saved during a ringing tail, ramps in flight and a set latch
// equals one saved at rest with the same parameters.
bool stateLiveExcluded(Detail& d) {
  auto x = mk({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 80}, {"mix", 50}});
  run(*x, impulse(S(0.3), 0, 1.5), zeros(S(0.3)));  // ringing, and an over at the input tap
  x->setParam("io.outGain", 6);
  x->setParam("mix", 60);
  block(*x, zeros(100), zeros(100));  // both 20 ms ramps mid-flight
  REQUIRE(x->inputMeter().clip && !x->atRest(), "the case is vacuous: no latch, or already at rest");
  auto y = mk({{"time.sync", 0}, {"time.ms", 100}, {"fb.amount", 80}, {"mix", 60}, {"io.outGain", 6}});
  REQUIRE(saveText(*x) == saveText(*y), "the state saved while live differs from one saved at rest");
  return true;
}

// Undo (RS-6.14 via horde's history): for every one of the 29 keys, restoring a
// snapshot taken before the key moved gives the byte-identical state and a
// render bit-identical to a twin that only loaded the snapshot.
bool stateUndo(Detail& d) {
  for (int i = 0; i < echo::kNumParams; ++i) {
    const auto& p = echo::kParams[static_cast<std::size_t>(i)];
    auto a = mk({{"time.sync", 0}, {"time.ms", 90}, {"fb.amount", 60}, {"mod.depthMs", 1}});
    const std::string s0 = saveText(*a);
    const double cur = a->getParam(i);
    double v = p.stepped ? (cur + 1 > p.max ? p.min : cur + 1) : p.min + (p.max - p.min) * 0.37;
    if (v == cur) v = p.max;
    a->setParam(i, v);
    REQUIRE(saveText(*a) != s0, "%.*s: the move left the state unchanged (vacuous)", static_cast<int>(p.key.size()), p.key.data());
    const char* why = "";
    REQUIRE(restore(*a, s0, &why), "%.*s: the snapshot was refused (%s)", static_cast<int>(p.key.size()), p.key.data(), why);
    REQUIRE(saveText(*a) == s0, "%.*s: undo does not give back the snapshot's bytes", static_cast<int>(p.key.size()), p.key.data());
    auto twin = mk({});
    restore(*twin, s0);
    const Vec in = impulse(S(0.3), 10, 0.5);
    const auto oa = run(*a, in, in), ot = run(*twin, in, in);
    for (long k = 0; k < S(0.3); ++k)
      REQUIRE(oa.L[k] == ot.L[k] && oa.R[k] == ot.R[k], "%.*s: the undone instance renders differently at %ld", static_cast<int>(p.key.size()),
              p.key.data(), k);
  }
  return true;
}

// All or nothing, and the alias rule: each malformed text is refused and leaves
// the instance and the staging slot untouched; an alias reads and is written
// back as the canonical name.
// A load DURING PLAY (critic T-015 H1): stage + adopt on a ringing instance is
// exactly a load (D-020: the old repeats fade under their old settings) plus the
// Device keys through setParam (they ramp), bit for bit, against a twin that
// does those two things by hand at the same block boundary.
bool stateAdoptLive(Detail& d) {
  g_rng = 31;
  const long n = S(0.8), at = S(0.3);
  Vec in(n);
  for (long i = 0; i < S(0.25); ++i) in[static_cast<std::size_t>(i)] = 0.4 * noise();
  auto a = mk({{"time.sync", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 50}});
  auto b = mk({{"time.sync", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 50}});
  auto target = mk({{"time.sync", 0}, {"time.ms", 200}, {"fb.amount", 30}, {"mix", 70}, {"io.outGain", -6}});
  const std::string t = saveText(*target);
  std::array<echo::KeyValue, echo::kNumParams> kv{};
  std::size_t nk = 0;
  for (int i = 0; i < echo::kNumParams; ++i)
    if (echo::kParams[static_cast<std::size_t>(i)].morph != echo::Morph::Device) kv[nk++] = {echo::kParams[static_cast<std::size_t>(i)].key, target->getParam(i)};
  const auto oa = run(*a, in, in, {{at, [&t](Echo& x) { x.stageState(t); x.adoptStaged(); }}});
  const auto ob = run(*b, in, in, {{at, [&](Echo& x) {
                                     x.loadState(std::span<const echo::KeyValue>(kv.data(), nk));
                                     x.setParam("io.inGain", target->getParam(echo::kInGain));
                                     x.setParam("io.outGain", target->getParam(echo::kOutGain));
                                   }}});
  REQUIRE(maxAbs(ob.L, at, at + S(0.01)) > 1e-3, "the case is vacuous: the twin has nothing ringing at the load");
  for (long i = 0; i < n; ++i)
    REQUIRE(oa.L[i] == ob.L[i] && oa.R[i] == ob.R[i], "an adopt mid-play differs from a load at sample %ld (%.17g vs %.17g)", i, oa.L[i], ob.L[i]);
  return true;
}

bool stateRefusals(Detail& d) {
  auto e = mk({{"fb.amount", 44}});
  const std::string s = saveText(*e);
  auto replace = [&](const std::string& from, const std::string& to) {
    std::string t = s;
    const auto at = t.find(from);
    return at == std::string::npos ? std::string("<anchor lost>") : t.replace(at, from.size(), to);
  };
  const std::string devIn = "io.inGain 0\n";
  std::string devUp = replace(devIn, "");
  devUp.insert(devUp.find('\n') + 1, devIn);
  const struct { const char* what; std::string text; } bad[] = {
      {"an unknown key", replace("mix 30\n", "mix 30\nwidth 50\n")},
      {"a duplicated key", replace("mix 30\n", "mix 30\nmix 30\n")},
      {"a missing key", replace("mix 30\n", "")},
      {"out of range", replace("time.ms 375\n", "time.ms 13000\n")},
      {"a bad label", replace("route.mode stereo\n", "route.mode sideways\n")},
      {"a division by index", replace("time.div 1/4\n", "time.div 9\n")},
      {"a fractional seed", replace("mod.seed 1\n", "mod.seed 1.5\n")},
      {"non-finite", replace("mix 30\n", "mix nan\n")},
      {"another version", replace("echo-state 1\n", "echo-state 2\n")},
      {"a Device key in the patch block", devUp},
      {"a patch key in the device block", replace("mix 30\n", "") + "mix 30\n"},
      {"no final newline", s.substr(0, s.size() - 1)},
      {"CRLF", replace("mix 30\n", "mix 30\r\n")},
      // What a libc's strtod tolerates but the writer never emits (critic T-015 M2):
      {"a leading +", replace("mix 30\n", "mix +30\n")},
      {"a tab before the value", replace("mix 30\n", "mix \t30\n")},
      {"a hex float", replace("mix 30\n", "mix 0x1Ep0\n")},
      {"a trailing point", replace("mix 30\n", "mix 30.\n")},
      {"a leading point", replace("mix 30\n", "mix .30e2\n")},
      {"an underflow", replace("mix 30\n", "mix 1e-400\n")},
      {"a decimal comma", replace("mix 30\n", "mix 30,5\n")},
      {"a seed with an exponent", replace("mod.seed 1\n", "mod.seed 1e0\n")},
      {"a seed with a +", replace("mod.seed 1\n", "mod.seed +1\n")},
      {"a hex seed", replace("mod.seed 1\n", "mod.seed 0x1\n")},
  };
  for (const auto& b : bad) {
    const char* why = "";
    REQUIRE(!e->stageState(b.text, &why), "%s was accepted", b.what);
    REQUIRE(!e->adoptStaged() && saveText(*e) == s, "%s changed the instance or staged something", b.what);
  }
  const char* why = "";
  REQUIRE(restore(*e, replace("time.div 1/4\n", "time.div 1/8D\n"), &why), "the alias 1/8D was refused (%s)", why);
  REQUIRE(saveText(*e).find("\ntime.div 3/16\n") != std::string::npos, "the alias was not written back as its name 3/16");
  REQUIRE(e->stageState(s) && !e->stageState(s, &why), "a second stage while one is pending was accepted");
  e->adoptStaged();
  return true;
}

// ---- M3 B2, T-037: the Clear action (SPEC §16.7; RS-4.12; row A23) ----

// Blocks of a fixed size, with actions fired at their TRUE offset inside the
// block (run() cuts a block at every event, which would put every action at
// offset 0 and hide A23's must-fail shim). Parameter sets land at the start of
// the block that contains them; the callers align them to the block size.
struct Fire {
  long at;
  int id;
};
struct SetAt {
  long at;
  const char* key;
  double v;
};
// Into a preallocated Out, so an allocation probe around it sees only the engine.
void runActionsInto(Echo& e, const Vec& L, const Vec& R, const std::vector<Fire>& fires, int bs, Out& o, const std::vector<SetAt>& sets = {}) {
  const long n = static_cast<long>(L.size());
  for (long pos = 0; pos < n; pos += bs) {
    const long len = std::min<long>(bs, n - pos);
    for (const auto& st : sets)
      if (st.at >= pos && st.at < pos + len) e.setParam(st.key, st.v);
    for (const auto& f : fires)
      if (f.at >= pos && f.at < pos + len) e.fireAction(f.id, static_cast<int>(f.at - pos));
    const auto u = static_cast<std::size_t>(pos);
    e.process(L.data() + u, R.data() + u, o.L.data() + u, o.R.data() + u, static_cast<int>(len), 120.0);
  }
}
Out runActions(Echo& e, const Vec& L, const Vec& R, const std::vector<Fire>& fires, int bs, const std::vector<SetAt>& sets = {}) {
  Out o{zeros(static_cast<long>(L.size())), zeros(static_cast<long>(L.size()))};
  runActionsInto(e, L, R, fires, bs, o, sets);
  return o;
}
constexpr int kClear = echo::Echo::kActionClear;

// Criterion 1: before the Clear, nothing changes; from its sample the repeats
// fade over 20 ms under the current settings (the read scaled by the load's own
// ramp) and end; the heads stay.
bool clearFade(Detail& d) {
  for (double sr : {48000.0, 44100.0}) {  // "20 ms at the rate" (critic T-037 M2)
  g_rng = 41;
  const long n = S(0.6, sr), T = S(0.35, sr) + 37, N = S(0.02, sr);
  Vec in = zeros(n);
  for (long i = 0; i < S(0.25, sr); ++i) in[static_cast<std::size_t>(i)] = 0.3 * noise();
  auto a = mk({{"time.sync", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 50}}, sr);
  auto b = mk({{"time.sync", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 50}}, sr);
  const auto oa = runActions(*a, in, in, {{T, kClear}}, 512), ob = runActions(*b, in, in, {}, 512);
  for (long i = 0; i < T; ++i) REQUIRE(oa.L[i] == ob.L[i] && oa.R[i] == ob.R[i], "the Clear changed sample %ld, before its offset %ld", i, T);
  REQUIRE(maxAbs(ob.L, T, T + N) > 1e-3, "the case is vacuous: nothing rings at the Clear");
  for (long j = 0; j < N; ++j) {
    const double g = static_cast<double>(N - 1 - j) / static_cast<double>(N);
    REQUIRE(std::fabs(oa.L[T + j] - ob.L[T + j] * g) <= 1e-12, "at %.0f Hz, %ld after the Clear the output is %.9g, the 20 ms fade expects %.9g",
            sr, j, oa.L[T + j], ob.L[T + j] * g);
  }
  REQUIRE(maxAbs(oa.L, T + N) == 0.0 && maxAbs(oa.R, T + N) == 0.0, "at %.0f Hz the repeats still sound %.3g after the 20 ms fade", sr,
          maxAbs(oa.L, T + N));
  }
  return true;
}

// Criterion 1, "the heads stay" (ruled 2026-10-23, D-040: they keep moving, not
// landed; critic T-037 H1, M1): a Clear during a tape glide, and during a tempo
// move in crossfade mode, leaves every head exactly where a run without the
// Clear has it, sample for sample, and each move in its own mode.
bool clearMovingHeads(Detail& d) {
  {  // a key move gliding by the tape law
    auto a = mk({{"time.sync", 0}, {"time.ms", 20}, {"time.change", 0}, {"time.law", 0}, {"time.glide", 1}, {"fb.amount", 60}});
    auto b = mk({{"time.sync", 0}, {"time.ms", 20}, {"time.change", 0}, {"time.law", 0}, {"time.glide", 1}, {"fb.amount", 60}});
    const Vec in(S(0.8), 0.05);
    const std::vector<SetAt> sets{{64 * 300, "time.ms", 200}};
    Out oa{zeros(S(0.8)), zeros(S(0.8))}, ob{zeros(S(0.8)), zeros(S(0.8))};
    runActionsInto(*a, in, in, {{64 * 310 + 5, kClear}}, 64, oa, sets);
    runActionsInto(*b, in, in, {}, 64, ob, sets);
    REQUIRE(b->headDelay(0) != 200.0 * 48.0 && b->headDelay(0) > 20.0 * 48.0, "the case is vacuous: the glide is not in flight at the end");
    REQUIRE(a->headDelay(0) == b->headDelay(0) && a->headDelay(1) == b->headDelay(1),
            "a Clear moved a gliding head: %.6f vs %.6f samples without it", a->headDelay(0), b->headDelay(0));
  }
  {  // a tempo move in tempoMode crossfade, while time.change is repitch
    auto mkS = [] { return mk({{"time.sync", 1}, {"time.div", 9}, {"time.change", 0}, {"time.tempoMode", 0}, {"fb.amount", 60}}); };
    auto a = mkS(), b = mkS();
    double x[64], y[64], z[64];
    for (double& v : x) v = 0.05;
    for (int blk = 0; blk < 900; ++blk) {
      const double bpm = blk < 400 ? 120.0 : 130.0;  // the tempo moves inside the Clear's fade
      if (blk == 395) a->fireAction(kClear, 3);
      a->process(x, x, y, z, 64, bpm);
      b->process(x, x, y, z, 64, bpm);
      REQUIRE(a->headDelay(0) == b->headDelay(0), "block %d: a Clear changed a tempo move's head (%.4f vs %.4f): wrong mode", blk,
              a->headDelay(0), b->headDelay(0));
    }
    REQUIRE(b->fadeCount(0) >= 1, "the case is vacuous: the tempo move did not crossfade");
  }
  return true;
}

// "Under the current settings" (ruled 2026-10-23, D-040): with a mix ramp
// running through the fade, the fading wet follows the LIVE mix, so the output
// is the no-Clear output scaled by the fade, sample for sample.
bool clearLiveWet(Detail& d) {
  g_rng = 59;
  const long n = S(0.5), T0 = 64 * 260, T = T0 + 5, N = S(0.02);
  Vec in = zeros(n);
  for (long i = 0; i < S(0.25); ++i) in[static_cast<std::size_t>(i)] = 0.3 * noise();
  const std::vector<SetAt> sets{{T0, "mix", 90}};  // the 20 ms mix ramp starts 5 samples before the Clear
  auto a = mk({{"time.sync", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 30}});
  auto b = mk({{"time.sync", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 30}});
  const auto oa = runActions(*a, in, in, {{T, kClear}}, 64, sets), ob = runActions(*b, in, in, {}, 64, sets);
  REQUIRE(maxAbs(ob.L, T, T + N) > 1e-3, "the case is vacuous: nothing rings at the Clear");
  for (long j = 0; j < N; ++j) {
    const double g = static_cast<double>(N - 1 - j) / static_cast<double>(N);
    REQUIRE(std::fabs(oa.L[T + j] - ob.L[T + j] * g) <= 1e-12, "%ld into the fade the wet is %.9g, the live mix expects %.9g", j, oa.L[T + j],
            ob.L[T + j] * g);
  }
  return true;
}

// Criterion 1, the dry path and "no value lands": with live input and a mix ramp
// running through the fade's end, the output (all dry here: the first repeat is
// 2 s away) is bit-identical to a run without the Clear.
bool clearDryNoLand(Detail& d) {
  g_rng = 43;
  const long n = S(0.5), T = 64 * 300 + 17;
  Vec L(n), R(n);
  for (long i = 0; i < n; ++i) { L[i] = 0.4 * noise(); R[i] = 0.4 * noise(); }
  const std::vector<SetAt> sets{{64 * 302, "mix", 80}};  // a 20 ms mix ramp, still running when the Clear's fade ends
  auto a = mk({{"time.sync", 0}, {"time.ms", 2000}, {"fb.amount", 50}, {"mix", 40}});
  auto b = mk({{"time.sync", 0}, {"time.ms", 2000}, {"fb.amount", 50}, {"mix", 40}});
  const auto oa = runActions(*a, L, R, {{T, kClear}}, 64, sets), ob = runActions(*b, L, R, {}, 64, sets);
  for (long i = 0; i < n; ++i)
    REQUIRE(oa.L[i] == ob.L[i] && oa.R[i] == ob.R[i], "the dry path differs at sample %ld (%.17g vs %.17g): a Clear touched it", i, oa.L[i], ob.L[i]);
  return true;
}

// Criterion 2: a second Clear, or a load, during the fade never restarts it; a
// Clear at rest changes nothing.
bool clearNoRestart(Detail& d) {
  g_rng = 47;
  const long n = S(0.6), T = S(0.35) + 11, N = S(0.02);
  Vec in = zeros(n);
  for (long i = 0; i < S(0.25); ++i) in[static_cast<std::size_t>(i)] = 0.3 * noise();
  auto mkRing = [] { return mk({{"time.sync", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 50}}); };
  auto one = mkRing(), two = mkRing();
  const auto o1 = runActions(*one, in, in, {{T, kClear}}, 64), o2 = runActions(*two, in, in, {{T, kClear}, {T + 300, kClear}}, 64);
  for (long i = 0; i < n; ++i) REQUIRE(o1.L[i] == o2.L[i], "a second Clear mid-fade changed sample %ld: it restarted the fade", i);
  auto ld = mkRing();
  const std::array<echo::KeyValue, 2> patch{{{"fb.amount", 30}, {"time.ms", 200}}};
  Out ol{zeros(n), zeros(n)};
  for (long pos = 0; pos < n; pos += 64) {
    if (pos == (T / 64) * 64) ld->fireAction(kClear, static_cast<int>(T - pos));
    if (pos == ((T + 300) / 64) * 64) ld->loadState(patch);
    const auto u = static_cast<std::size_t>(pos);
    ld->process(in.data() + u, in.data() + u, ol.L.data() + u, ol.R.data() + u, 64, 120.0);
  }
  REQUIRE(maxAbs(ol.L, T + N) == 0.0, "a load mid-Clear restarted the fade: sound %.3g after the Clear's 20 ms", maxAbs(ol.L, T + N));
  REQUIRE(ld->getParam(echo::kFb) == 30.0 && ld->headDelay(0) == 0.2 * kSr, "the load that joined the Clear's fade did not land its patch");
  auto rest = mk({{"time.sync", 0}, {"time.ms", 90}, {"fb.amount", 75}}), twin = mk({{"time.sync", 0}, {"time.ms", 90}, {"fb.amount", 75}});
  const Vec pulse = impulse(S(0.3), 100, 0.5);
  const auto orest = runActions(*rest, pulse, pulse, {{0, kClear}}, 64), otwin = runActions(*twin, pulse, pulse, {}, 64);
  for (long i = 0; i < S(0.3); ++i) REQUIRE(orest.L[i] == otwin.L[i], "a Clear at rest changed sample %ld", i);
  return true;
}

// Criterion 3: a performance event: the saved state is byte-identical before
// and after a Clear (ADR-267 A1).
bool clearState(Detail& d) {
  auto e = mk({{"time.sync", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 50}});
  run(*e, impulse(S(0.2), 0, 0.5), zeros(S(0.2)));
  const std::string before = saveText(*e);
  runActions(*e, zeros(S(0.1)), zeros(S(0.1)), {{5, kClear}}, 256);
  REQUIRE(saveText(*e) == before, "a Clear changed the saved state");
  return true;
}

// Criterion 4 (A23) and 5: the same actions at the same offsets give bit-identical
// output on every run and at block sizes 1, 64 and 4096; firing allocates
// nothing; an unknown action is refused and changes nothing; the queue's cap
// refuses rather than drops.
bool clearA23(Detail& d) {
  g_rng = 53;
  const long n = S(0.5);
  Vec L(n), R(n);
  for (long i = 0; i < S(0.4); ++i) { L[i] = 0.3 * noise(); R[i] = 0.3 * noise(); }
  // Two Clears in one 4096 block, the second after the first's fade has ended
  // (critic T-037 L5), and one on a block's last sample.
  const std::vector<Fire> fires{{3001, kClear}, {9000, kClear}, {10200, kClear}, {15000 + 4095, kClear}};
  auto mkE = [] { return mk({{"time.sync", 0}, {"time.ms", 45}, {"fb.amount", 70}, {"mix", 50}}); };
  REQUIRE(echo::Echo::resolveAction("clear") == kClear && !echo::Echo::resolveAction("freeze"), "the action keys resolve wrongly");
  auto ref = mkE();
  const auto oref = runActions(*ref, L, R, fires, 1);
  for (int bs : {64, 4096, 1}) {
    auto e = mkE();
    Out o{zeros(n), zeros(n)};
    alloc_probe::arm();
    runActionsInto(*e, L, R, fires, bs, o);
    alloc_probe::disarm();
    REQUIRE(alloc_probe::g_allocs.load() == 0, "firing and processing actions allocated %ld times (block %d)", alloc_probe::g_allocs.load(), bs);
    for (long i = 0; i < n; ++i)
      REQUIRE(o.L[i] == oref.L[i] && o.R[i] == oref.R[i], "block %d differs from block 1 at sample %ld: an action is not at its offset", bs, i);
  }
  auto plain = mkE(), unk = mkE();
  const auto op = runActions(*plain, L, R, {}, 64);
  REQUIRE(!unk->fireAction(7, 10), "an unknown action id was accepted");
  const auto ou = runActions(*unk, L, R, {}, 64);
  for (long i = 0; i < n; ++i) REQUIRE(op.L[i] == ou.L[i], "a refused action changed sample %ld", i);
  auto cap = mkE();
  for (int k = 0; k < echo::Echo::kMaxActions; ++k) REQUIRE(cap->fireAction(kClear, k), "action %d of the cap was refused", k);
  REQUIRE(!cap->fireAction(kClear, 9), "an action past the cap of %d was accepted", echo::Echo::kMaxActions);
  auto off = mkE();
  REQUIRE(!off->fireAction(kClear, -1) && !off->fireAction(kClear, off->maxBlock()), "an offset outside [0, maxBlock) was accepted");
  return true;
}

struct Check {
  const char* name;
  bool (*fn)(Detail&);
  std::vector<const char*> plants;  // the faults it must see (echo_plants)
};

const std::vector<Check> kChecks = {
    {"time accuracy: repeats on their sample; read centroid = delay (whole and fractional)", timeAccuracy, {"timeHalfSample"}},
    {"generation ratio at 4 rates: gen2 = g(1+R)/2 exactly, then g within 1e-3 (S-1)", generationRatio, {"loopFilterInCircuit", "dcFixedRate"}},
    {"ceiling law: identity to 0.9, then 0.9 + 0.1 tanh, on g·x (the returning term)", ceilingLaw, {"noCeiling", "ceilingBeforeGain", "ceilingHardClip"}},
    {"ceiling: past unity the loop parks below 2.0 (input peak + 2 under input) and does not die", ceilingPastUnity, {"noCeiling"}},
    {"dry path: at mix 0 the output is the input exactly, at 120 % feedback (ADR-142)", dryPathExact, {"dryLimiter"}},
    {"mix 100 %: nothing before the first repeat (no cos(pi/2) leak)", mixFullNoDry, {"mixLeak"}},
    {"true bypass: settled-off filters are bit-identical whatever their cutoffs", trueBypass, {"loopFilterInCircuit"}},
    {"loop bound: measured |H_loop| <= 1 at 4 rates x 7 loop settings (D-005)", loopBound, {"loopGainOverOne", "dcTextbook"}},
    {"sample-rate independence: time exact, loop filter within 0.5 dB at 44.1-192 kHz", srIndependence, {"fixedCoefRate"}},
    {"block-size independence: blocks 1, 7, 64, 333, 4096 and random render bit-identically", blockIndependence, {"rampPerBlock", "blockSteppedD"}},
    {"determinism: interleaved instances = solo; reset() = fresh", determinism, {"sharedState", "resetKeepsLine"}},
    {"silence: exact zero in, exact zero out (9 routing x change modes at 120 %; awake engine)", silence, {"antiDenormalDc"}},
    {"tail: exact zero from the declared tail on; kInfiniteTail at g >= 1", tailExactZero, {"noTailGate"}},
    {"tail formula: at 50 / 90 / 99 %, rest never cuts a sounding repeat (D-019)", tailFormula, {"tailFormulaShort"}},
    {"tail clock: lowering feedback or releasing a held loop never cuts the repeats (D-019)", tailAfterChange, {"tailClockFromSilence"}},
    {"tail near unity: 99.99999999999999 % declares a huge or infinite tail and keeps sounding", tailNearUnity, {"tailInt32"}},
    {"read across the line's wrap: a near-whole delay reads the right sample every lap", readAcrossWrap, {"oldReadWrap"}},
    {"FTZ is set inside process: a subnormal input is flushed", ftzSet, {"ftzNeverSet"}},
    {"rest never cuts a sound: the gate waits for the filters' ring below -120 dBFS", restNeverCuts, {"gateLineOnly"}},
    {"non-finite input: repaired, counted (3 of 3), latched; the output guard too", finiteInput, {"guardUncounted", "noOutputGuard"}},
    {"loop repair: a non-finite write clears all loop state; the wet recovers", loopRepair, {"loopRepairPartial"}},
    {"FTZ: the host's FP control word is restored", ftzRestored, {"ftzNotRestored"}},
    {"real time: no allocation in process / setParam / reset / loadState", noAlloc, {"allocInProcess"}},
    {"I/O gain test 2: unity at 0 dB; a 20 ms ramp linear in amplitude on both trims, at 44.1, 48 and 96 kHz (B435)", ioGainLaw,
     {"dbRamp", "rampFixedSamples"}},
    {"switches: routing and filter switches are click-free (20 ms equal-gain)", switchesClickFree, {"instantSwitch"}},
    {"crossfade: equal-gain; correlated heads never exceed the signal (D-007)", crossfadeEqualGain, {"equalPower"}},
    {"jump: a 3 ms crossfade, never a bare jump (D-016)", jumpIsShortFade, {"bareJump"}},
    {"fade lengths: jump 3 ms, crossfade time.xfadeMs, at the rate (48 and 96 kHz)", fadeLengths, {"jumpUsesXfade", "xfadeIgnored"}},
    {"retrigger: latest target wins, intermediates dropped (SPEC §3.5)", retriggerQueue, {"retargetMidFade"}},
    {"deferral: a time.change flip mid-fade changes nothing", deferral, {"deferralBroken"}},
    {"tempo: synced 1/4 at 60 bpm repeats at exactly 1 s from the first block, no glide in", tempoSync, {"tempoIgnored", "tempoGlidesAtRest"}},
    {"clamp: synced times fold by halving, free times clamp, 1 ms floor (SPEC §3.4)", clampFold, {"clampTruncates", "noMinFloor"}},
    {"routing: ping-pong alternates; stereo never crosses; right wakes; M/S levels; M/S folds to M", routing,
     {"msSign", "pingpongNoCross", "wakeLeftOnly", "msSideGain2"}},
    {"load: old repeats fade (not cut) and end within 20 ms; Device keys untouched (D-016)", loadEnds,
     {"loadKeepsLine", "loadWritesDevice", "loadClearsInstantly"}},
    {"load: the old audio fades under its old settings, click-free (D-020)", loadClickFree, {"loadNewSettingsOnOld"}},
    {"load: a second load mid-fade never restarts the fade (D-020)", reloadNoRestart, {"loadRestartsFade"}},
    {"load: Device gain ramps run on through a load (RS-4.5)", loadLeavesDevice, {"loadSnapsDevice"}},
    {"sleep: a slept instance equals its never-slept twin from the wake on", sleepTwin, {"restRamps"}},
    {"parameters: unknown / non-finite / out-of-range / fractional values refused", paramValidation, {"rangeClamps"}},
    // ---- M2 ----
    {"tape law = an independent full-rate tape model: <= 0.01 sample, <= 2.5 cents of speed (3 glides x 3 moves)", tapeLaw,
     {"tapeNoHistory", "tapeLinearHistory"}},
    {"exponential law = its ln-D recurrence; a big move runs at the 12 st cap", expLaw, {"expLawLinear", "noLawCap"}},
    {"repitch accuracy: output pitch = 1 - dD/dt within 1 cent from the move's start, both laws, fast and slow glides", repitchCents,
     {"integerRead"}},
    {"read speed in [0.25, 2.25], no reverse, backstop, overrun or floor: 4 modes x 5 moves x 3 mods; fallback past 3 s", speedInvariant,
     {"noLawCap", "noFallback"}},
    {"tail clock holds while a head moves: a 2900 -> 300 ms glide at fb 0 is never cut (D-025)", tailDuringGlide, {"tailClockIgnoresMotion"}},
    {"mod.seed: an unchanged re-send changes nothing; a load derives the drift from the patch's seed (D-026)", seedRules,
     {"seedResendRestarts", "loadKeepsModState"}},
    {"key moves keep time.change through a tempo ramp; follow resumes after a step and leaves no residual", keyAndFollow,
     {"tempoStealsKeyMove", "followNoTrackInFade", "followKeepsResidual"}},
    {"read floor under modulation (counted, zero); the offset lands on the curve at rest (no slew-in after a wake)", floorAndWake,
     {"modNoFloorBound", "wakeOffsetFromZero"}},
    {"moving time is sample-rate independent: both laws trace the same delay in seconds at 48 and 96 kHz", movingTimeSr, {"glideFixedRate"}},
    {"modulation: depth exact, read = input at the moving position, slope limit, drift clamp, slew 0.25, spread 90 deg", modulation,
     {"modDepthUnlimited", "driftOvershoot", "modSlewOff"}},
    {"seeded modulation: reproducible from mod.seed; seed change and shape switch never step", modSeeded, {"unseededSeed", "shapeInstant"}},
    {"tempo modes: follow exact, glide by the law, crossfade never bends; 2x step crossfades; key moves obey time.change", tempoModes,
     {"tempoModeIgnored", "followNoCap"}},
    {"click metric (M2 gate, s fixed at 2.25): sweeps, tempo sweeps and single moves under 2.5 and inside the speed invariants; a planted click over",
     clickSweep, {"integerRead", "blockSteppedD", "bareJump", "noLawCap"}},
    // ---- M3 B2, T-013 ----
    {"ABI: properties() validates after prepare at 44.1/48/96/192 kHz (latency 0, tail, min_block 1, load 20 ms) and is refused before",
     abiProperties, {"propsNoTail", "loadBoundShort"}},
    {"ABI: the tail saturates onto TailDecl's 32 bits (kInfiniteTail at 2^32 - 1 and past; 99.99 %, 12 s, 192 kHz)", abiTail32, {"tailWraps32"}},
    {"ABI: every parameter maps one to one onto OQ #33's MorphKind + MorphParticipation (RS-2.2)", abiParamClasses, {"paramNoClass"}},
    {"ABI: process() in place is bit-identical to out of place (3 routings, blocks 1/64/4096)", abiInPlace, {"processReadsAfterWrite"}},
    // ---- M3 B2, T-014 ----
    {"I/O gain test 1: at 0 dB both trims are bit-identical to an unprocessed pass", ioTest1Unity, {"dryLimiter"}},
    {"I/O gain test 3: after prepare at a saved +6 dB the first sample is already +6 dB (48, 96 kHz)", ioTest3Opens, {"prepareRampsIn"}},
    {"I/O gain test 4: a load and a corner flip leave both gains and a ramp in flight alone (bit-identical)", ioTest4Kept,
     {"loadWritesDevice", "loadSnapsDevice"}},
    {"I/O gain test 5: Input +12 dB is a trim before the loop (= pre-scaled input) and drives the ceiling; Output +12 dB is level only",
     ioTest5Placement, {"inGainAfterLoop", "inGainDryOnly"}},
    {"I/O gain test 6: Echo declares both keys; the slot adds no second pair", ioTest6Slot, {"doubleGain"}},
    {"meters: input tap after Input Gain (repaired, both channels, |x|), output tap after Output Gain and the tail; per-block peaks",
     metersTaps, {"tapBeforeGain", "outTapDryOnly", "tapReadsRaw"}},
    {"I/O gain test 7: both latches set on an over (> 1.0, not at 1.0) and hold through silence, reset and a load until cleared",
     ioTest7Latches, {"latchClears", "resetClearsLatch"}},
    // ---- M3 B2, T-015 ----
    {"state: version line, every patch key once in manifest order by name, then the [device] block (SPEC §16.2)", stateFormat,
     {"stateDivByIndex", "stateKeyMissing"}},
    {"state A18: save -> load -> save byte-identical, values bit-equal, renders identical; also in a comma locale", stateExact, {"stateFloat6g"}},
    {"state A17: a mid-session state in a fresh instance renders as the original after reset()", stateComplete, {"stateKeyMissing"}},
    {"state A19: the worst case fits 2 KiB; 2 KiB + 1 refused first; staging allocates nothing", stateSize, {"stateOversizedAccepted"}},
    {"state A20: saved while ringing, mid-ramp and latched equals saved at rest", stateLiveExcluded, {"stateLatchSerialised"}},
    {"state undo: for each of the 29 keys, the snapshot restores byte- and bit-identically", stateUndo, {"stateKeyMissing"}},
    {"state: every malformed text refused whole (strict number grammar), nothing staged; an alias reads and is written back by name",
     stateRefusals, {"stateAcceptsUnknown"}},
    {"state: an adopt mid-play is a D-020 load plus the Device keys ramping, bit for bit", stateAdoptLive, {"adoptResets"}},
    // ---- M3 B2, T-037 ----
    {"Clear: nothing changes before its sample; from it the repeats fade over 20 ms (48, 44.1 kHz) and end", clearFade, {"clearCuts"}},
    {"Clear: the heads keep moving as without it (a tape glide; a tempo move keeps its crossfade mode)", clearMovingHeads, {"clearFreezesHeads"}},
    {"Clear: the fading wet follows the live mix (a mix ramp through the fade)", clearLiveWet, {"clearFreezesWet"}},
    {"Clear: the dry path is bit-identical to no Clear, and no value lands (a mix ramp runs on through the fade's end)", clearDryNoLand,
     {"clearLandsValues"}},
    {"Clear: a second Clear or a load mid-fade never restarts it; a Clear at rest changes nothing", clearNoRestart, {"clearRestartsFade"}},
    {"Clear: a performance event, the saved state byte-identical before and after", clearState, {"clearWritesState"}},
    {"Clear A23: same actions, same offsets, bit-identical at blocks 1/64/4096; no allocation; unknown refused; the cap refuses", clearA23,
     {"actionAtBlockStart"}},
};

#ifdef ECHO_PLANTS
bool* plantFlag(std::string_view n) {
  using namespace echo::plant;
  struct P { const char* n; bool* f; };
  static const P table[] = {
      {"noCeiling", &noCeiling}, {"dryLimiter", &dryLimiter}, {"mixLeak", &mixLeak}, {"loopFilterInCircuit", &loopFilterInCircuit},
      {"timeHalfSample", &timeHalfSample}, {"fixedCoefRate", &fixedCoefRate}, {"tempoIgnored", &tempoIgnored},
      {"clampTruncates", &clampTruncates}, {"antiDenormalDc", &antiDenormalDc}, {"noTailGate", &noTailGate},
      {"sharedState", &sharedState}, {"resetKeepsLine", &resetKeepsLine}, {"guardUncounted", &guardUncounted},
      {"loopRepairPartial", &loopRepairPartial}, {"ftzNotRestored", &ftzNotRestored}, {"allocInProcess", &allocInProcess},
      {"dbRamp", &dbRamp}, {"rampPerBlock", &rampPerBlock}, {"instantSwitch", &instantSwitch}, {"equalPower", &equalPower},
      {"bareJump", &bareJump}, {"deferralBroken", &deferralBroken}, {"loadKeepsLine", &loadKeepsLine},
      {"loadWritesDevice", &loadWritesDevice}, {"restRamps", &restRamps}, {"msSign", &msSign},
      {"pingpongNoCross", &pingpongNoCross}, {"loopGainOverOne", &loopGainOverOne}, {"dcTextbook", &dcTextbook},
      {"retargetMidFade", &retargetMidFade}, {"rangeClamps", &rangeClamps}, {"gateLineOnly", &gateLineOnly},
      {"oldReadWrap", &oldReadWrap}, {"tailClockFromSilence", &tailClockFromSilence}, {"tailInt32", &tailInt32},
      {"loadRestartsFade", &loadRestartsFade}, {"loadSnapsDevice", &loadSnapsDevice}, {"loadClearsInstantly", &loadClearsInstantly},
      {"loadNewSettingsOnOld", &loadNewSettingsOnOld}, {"tempoGlidesAtRest", &tempoGlidesAtRest}, {"jumpUsesXfade", &jumpUsesXfade},
      {"xfadeIgnored", &xfadeIgnored}, {"ceilingBeforeGain", &ceilingBeforeGain}, {"ceilingHardClip", &ceilingHardClip},
      {"dcFixedRate", &dcFixedRate}, {"rampFixedSamples", &rampFixedSamples}, {"ftzNeverSet", &ftzNeverSet},
      {"msSideGain2", &msSideGain2}, {"noMinFloor", &noMinFloor}, {"wakeLeftOnly", &wakeLeftOnly},
      {"noOutputGuard", &noOutputGuard}, {"tailFormulaShort", &tailFormulaShort},
      {"modSlewOff", &modSlewOff}, {"driftOvershoot", &driftOvershoot}, {"unseededSeed", &unseededSeed}, {"shapeInstant", &shapeInstant},
      {"modDepthUnlimited", &modDepthUnlimited}, {"noLawCap", &noLawCap}, {"noFallback", &noFallback}, {"tapeNoHistory", &tapeNoHistory},
      {"expLawLinear", &expLawLinear}, {"tempoModeIgnored", &tempoModeIgnored}, {"followNoCap", &followNoCap},
      {"integerRead", &integerRead}, {"blockSteppedD", &blockSteppedD},
      {"tailClockIgnoresMotion", &tailClockIgnoresMotion}, {"seedResendRestarts", &seedResendRestarts}, {"loadKeepsModState", &loadKeepsModState},
      {"tempoStealsKeyMove", &tempoStealsKeyMove}, {"followNoTrackInFade", &followNoTrackInFade}, {"followKeepsResidual", &followKeepsResidual},
      {"modNoFloorBound", &modNoFloorBound}, {"wakeOffsetFromZero", &wakeOffsetFromZero}, {"tapeLinearHistory", &tapeLinearHistory},
      {"glideFixedRate", &glideFixedRate},
      {"propsNoTail", &propsNoTail}, {"tailWraps32", &tailWraps32}, {"paramNoClass", &paramNoClass},
      {"processReadsAfterWrite", &processReadsAfterWrite}, {"loadBoundShort", &loadBoundShort},
      {"latchClears", &latchClears}, {"tapBeforeGain", &tapBeforeGain}, {"prepareRampsIn", &prepareRampsIn},
      {"inGainAfterLoop", &inGainAfterLoop}, {"doubleGain", &doubleGain},
      {"inGainDryOnly", &inGainDryOnly}, {"outTapDryOnly", &outTapDryOnly}, {"tapReadsRaw", &tapReadsRaw}, {"resetClearsLatch", &resetClearsLatch},

      {"stateFloat6g", &stateFloat6g}, {"stateKeyMissing", &stateKeyMissing}, {"stateLatchSerialised", &stateLatchSerialised},
      {"stateOversizedAccepted", &stateOversizedAccepted}, {"stateDivByIndex", &stateDivByIndex}, {"stateAcceptsUnknown", &stateAcceptsUnknown},
      {"adoptResets", &adoptResets},
      {"actionAtBlockStart", &actionAtBlockStart}, {"clearCuts", &clearCuts}, {"clearLandsValues", &clearLandsValues},
      {"clearWritesState", &clearWritesState}, {"clearRestartsFade", &clearRestartsFade},
      {"clearFreezesHeads", &clearFreezesHeads}, {"clearFreezesWet", &clearFreezesWet},
  };
  for (const auto& p : table)
    if (n == p.n) return p.f;
  return nullptr;
}
#endif

// The per-instance memory figure (1.0 bar MUST, SPEC §11), measured: every
// byte allocated by prepare() at each rate. Reported, and bounded by the spec.
// Run by the clean binary only (the plant binary has nothing to prove here).
[[maybe_unused]] bool memoryReport() {
  bool ok = true;
  for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
    Echo e;
    alloc_probe::arm();
    e.prepare(sr, 4096);
    alloc_probe::disarm();
    const double mib = static_cast<double>(alloc_probe::g_bytes.load()) / (1024.0 * 1024.0);
    const double lines = 2.0 * e.lineLength() * sizeof(float) / (1024.0 * 1024.0);
    std::printf("  REPORT memory at %6.0f Hz: %.2f MiB allocated at prepare (lines %.2f MiB, tape history %.2f MiB); sizeof(Echo) %zu B\n", sr,
                mib, lines, mib - lines, sizeof(Echo));
    if (sr == 192000.0 && mib > 18.75) ok = false;  // SPEC §11: 17.61 MiB of lines + ~1.1 MiB of tape history
  }
  return ok;
}

}  // namespace

int main(int argc, char** argv) {
  // --click-report: print every click reading (threshold lifted), clean and,
  // in echo_plants, under each click plant. The numbers the threshold is set on.
  if (argc > 1 && std::string_view(argv[1]) == "--click-report") {
    kClickMax = kClickMaxJump = 1e300;
    g_report = true;
    Detail d;
    clickSweep(d);
#ifdef ECHO_PLANTS
    for (const char* p : {"bareJump", "integerRead", "blockSteppedD", "noLawCap"}) {
      std::printf("  -- under plant %s\n", p);
      *plantFlag(p) = true;
      clickSweep(d);
      *plantFlag(p) = false;
    }
#endif
    return 0;
  }
  int fails = 0;
#ifdef ECHO_PLANTS
  // The plant table and the checks must cover each other: an unguarded plant is
  // a fault nobody looks for, an unknown name a check that guards nothing.
  for (const char* n : {"noCeiling", "dryLimiter", "mixLeak", "loopFilterInCircuit", "timeHalfSample", "fixedCoefRate", "tempoIgnored",
                        "clampTruncates", "antiDenormalDc", "noTailGate", "sharedState", "resetKeepsLine", "guardUncounted",
                        "loopRepairPartial", "ftzNotRestored", "allocInProcess", "dbRamp", "rampPerBlock", "instantSwitch", "equalPower",
                        "bareJump", "deferralBroken", "loadKeepsLine", "loadWritesDevice", "restRamps", "msSign", "pingpongNoCross",
                        "loopGainOverOne", "dcTextbook", "retargetMidFade", "rangeClamps", "gateLineOnly", "oldReadWrap",
                        "tailClockFromSilence", "tailInt32", "loadRestartsFade", "loadSnapsDevice", "loadClearsInstantly",
                        "loadNewSettingsOnOld", "tempoGlidesAtRest", "jumpUsesXfade", "xfadeIgnored", "ceilingBeforeGain",
                        "ceilingHardClip", "dcFixedRate", "rampFixedSamples", "ftzNeverSet", "msSideGain2", "noMinFloor",
                        "wakeLeftOnly", "noOutputGuard", "tailFormulaShort", "modSlewOff", "driftOvershoot", "unseededSeed",
                        "shapeInstant", "modDepthUnlimited", "noLawCap", "noFallback", "tapeNoHistory", "expLawLinear",
                        "tempoModeIgnored", "followNoCap", "integerRead", "blockSteppedD", "tailClockIgnoresMotion",
                        "seedResendRestarts", "loadKeepsModState", "tempoStealsKeyMove", "followNoTrackInFade",
                        "followKeepsResidual", "modNoFloorBound", "wakeOffsetFromZero", "tapeLinearHistory", "glideFixedRate",
                        "propsNoTail", "tailWraps32", "paramNoClass", "processReadsAfterWrite", "loadBoundShort",
                        "latchClears", "tapBeforeGain", "prepareRampsIn", "inGainAfterLoop", "doubleGain",
                        "inGainDryOnly", "outTapDryOnly", "tapReadsRaw", "resetClearsLatch",
                        "stateFloat6g", "stateKeyMissing", "stateLatchSerialised", "stateOversizedAccepted", "stateDivByIndex",
                        "stateAcceptsUnknown", "adoptResets",
                        "actionAtBlockStart", "clearCuts", "clearLandsValues", "clearWritesState", "clearRestartsFade",
                        "clearFreezesHeads", "clearFreezesWet"}) {
    bool guarded = false;
    for (const auto& c : kChecks)
      for (const char* p : c.plants) guarded = guarded || std::string_view(p) == n;
    if (!plantFlag(n) || !guarded) { std::printf("  FAIL  plant %s is %s\n", n, plantFlag(n) ? "guarded by no check" : "not in the plant table"); ++fails; }
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
      if (!f) { std::printf("  FAIL  unknown plant %s\n", p); ++fails; continue; }
      *f = true;
      Detail pd;
      const bool fired = !c.fn(pd);
      *f = false;
      std::printf("  %s  plant fires: %s -> %s\n", fired ? "PASS" : "FAIL", p, fired ? pd.c_str() : "NOT SEEN (vacuous check)");
      if (!fired) ++fails;
    }
#endif
  }
#ifndef ECHO_PLANTS
  if (ECHO_ALLOC_PROBE && !memoryReport()) { std::printf("  FAIL  memory at 192 kHz exceeds the spec's 18.75 MiB (lines + tape history)\n"); ++fails; }
#endif
  std::printf("%s: %s (%d failing)\n",
#ifdef ECHO_PLANTS
              "echo_plants",
#else
              "echo_tests",
#endif
              fails ? "RED" : "OK", fails);
  return fails ? 1 : 0;
}
