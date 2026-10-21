// core_tests.cpp — Echo's M1 invariant suite (ROADMAP M1; SPEC revision 2).
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
                              set(52000, "filt.loop.hpHz", 400), set(60000, "io.outGain", -6), set(61000, "io.inGain", 3)};
  };
  auto ref = mk({{"time.sync", 0}, {"time.ms", 120}, {"fb.amount", 40}});
  const auto o0 = run(*ref, inL, inR, evs(), 120.0, [](long) { return 4096; });
  std::uint32_t seed = 5;
  const std::vector<std::function<int(long)>> pats = {
      [](long) { return 1; }, [](long) { return 7; }, [](long) { return 64; }, [](long) { return 333; },
      [&seed](long) { seed = seed * 1664525u + 1013904223u; return 1 + static_cast<int>(seed % 4096u); }};
  for (std::size_t p = 0; p < pats.size(); ++p) {
    auto e = mk({{"time.sync", 0}, {"time.ms", 120}, {"fb.amount", 40}});
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
  auto solo = mk({{"fb.amount", 60}, {"time.sync", 0}, {"time.ms", 90}});
  const auto os = run(*solo, in, in);
  auto a = mk({{"fb.amount", 60}, {"time.sync", 0}, {"time.ms", 90}});
  auto b = mk({{"fb.amount", 60}, {"time.sync", 0}, {"time.ms", 90}});
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
  for (double sr : {48000.0, 96000.0}) {
    auto e = mk({{"mix", 0}}, sr);
    const long n = S(0.2, sr), at = S(0.1, sr), N = S(0.02, sr);
    const Vec in(n, 0.25);
    const auto o = run(*e, in, in, {set(at, "io.outGain", 6)});
    const double G = std::pow(10.0, 6.0 / 20.0);
    REQUIRE(o.L[at - 1] == 0.25, "unity at 0 dB: %.17g", o.L[at - 1]);
    const double mid = o.L[at + N / 2 - 1] / 0.25;
    REQUIRE(std::fabs(mid - (1 + G) / 2) < 1e-9, "at %.0f Hz the gain 10 ms in is %.6f, linear-in-amplitude over 20 ms expects %.6f", sr, mid, (1 + G) / 2);
    REQUIRE(o.L[at + N] == 0.25 * G, "after 20 ms the gain is %.17g, expected %.17g", o.L[at + N] / 0.25, G);
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
  auto a = mk({{"mix", 50}, {"fb.amount", 50}, {"time.sync", 0}, {"time.ms", 120}});
  auto b = mk({{"mix", 50}, {"fb.amount", 50}, {"time.sync", 0}, {"time.ms", 120}});
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

// ---- the registry ---------------------------------------------------------------

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
    {"block-size independence: blocks 1, 7, 64, 333, 4096 and random render bit-identically", blockIndependence, {"rampPerBlock"}},
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
    {"I/O gain: unity at 0 dB; a 20 ms ramp linear in amplitude, at 48 and 96 kHz (B435)", ioGainLaw, {"dbRamp", "rampFixedSamples"}},
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
    std::printf("  REPORT memory at %6.0f Hz: %.2f MiB allocated at prepare (lines %.2f MiB); sizeof(Echo) %zu B\n", sr, mib, lines,
                sizeof(Echo));
    if (sr == 192000.0 && mib > 17.7) ok = false;
  }
  return ok;
}

}  // namespace

int main() {
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
                        "wakeLeftOnly", "noOutputGuard", "tailFormulaShort"}) {
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
  if (ECHO_ALLOC_PROBE && !memoryReport()) { std::printf("  FAIL  memory at 192 kHz exceeds the spec's 17.7 MiB\n"); ++fails; }
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
