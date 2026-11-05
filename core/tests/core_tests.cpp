// core_tests.cpp — Echo's invariant suite, M1 + M2 + M3 (ROADMAP M1 – M3; SPEC revision 4).
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
#include <tuple>
#include <array>
#include <atomic>
#include <thread>
#include <cstring>
#include <clocale>
#include <deque>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "echo/engine.h"
#include "echo/ir_render.h"
#include "echo/presets.h"
#include "b265.h"
#include "fold.h"

using echo::Echo;
using Vec = std::vector<double>;

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr double kProbeHz = 431.7;  // incommensurate with the test times (SPEC §15 probe finding)

// The fidelity thresholds (1.0 bar row 10), named once: each check below reads its
// own, and docs/admission.md's table is printed from these same constants, so the
// table can never state a threshold a check does not use (critic T-018 H1).
namespace tol {
constexpr double kCentroidSamples = 1e-6;  // timeAccuracy: read centroid vs delay, samples
constexpr double kGen2Ratio = 1e-6;        // generationRatio: gen2/gen1 vs g(1 + R)/2 (float storage)
constexpr double kGenLater = 1e-3;         // generationRatio: later generations vs g
constexpr double kLoopGainSlack = 1e-5;    // loopBound: max |H_loop| <= 1 + this
constexpr double kSrLoopDb = 0.5;          // srIndependence: loop filter vs 48 kHz, dB
constexpr double kTapeSamples = 0.01;      // tapeLaw: vs the full-rate tape model, samples
constexpr double kTapeCents = 2.5;         // tapeLaw: speed, cents
constexpr double kRepitchCents = 1.0;      // repitchCents: output pitch vs 1 - dD/dt
constexpr double kClearFade = 1e-12;       // clearFade, clearLiveWet: vs the no-Clear output x the ramp
}  // namespace tol

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
  auto e = mk({{"filt.loop.on", 0}, {"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 50}, {"mix", 100}});
  const auto o = run(*e, impulse(S(0.6), 0, 0.5), zeros(S(0.6)));
  for (int k = 1; k <= 5; ++k) {
    long arg = 0;
    double m = -1;
    for (long i = k * 4800 - 100; i < k * 4800 + 100; ++i)
      if (std::fabs(o.L[i]) > m) { m = std::fabs(o.L[i]); arg = i; }
    REQUIRE(arg == k * 4800, "repeat %d at sample %ld, expected %d", k, arg, k * 4800);
  }
  for (double D : {4800.0, 4800.25, 4800.5, 4800.75}) {
    auto f = mk({{"filt.loop.on", 0}, {"time.mode", 0}, {"time.ms", D / 48.0}, {"fb.amount", 0}, {"mix", 100}});
    const auto q = run(*f, impulse(S(0.12)), zeros(S(0.12)));
    double s0 = 0, s1 = 0;
    for (long i = 4790; i < 4812; ++i) { s0 += q.L[i]; s1 += i * q.L[i]; }
    REQUIRE(std::fabs(s1 / s0 - D) < tol::kCentroidSamples, "centroid %.6f, delay %.2f samples", s1 / s0, D);
  }
  return true;
}

// The generation ratio (SPEC §5.3, ruled S-1): gen 2 is g(1+R)/2 of gen 1
// exactly (float storage: 1e-6), every later ratio is g within 1e-3.
// At all four rates: the DC blocker's pole comes from 5 Hz at THAT rate.
bool generationRatio(Detail& d) {
  for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
    auto e = mk({{"filt.loop.on", 0}, {"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 50}, {"mix", 100}}, sr);
    const long D = std::lround(0.1 * sr);
    const auto o = run(*e, impulse(8 * D, 0, 0.5), zeros(8 * D));
    const double R = std::exp(-2 * kPi * 5.0 / sr), g = 0.5;
    const double r2 = o.L[2 * D] / o.L[D];
    REQUIRE(std::fabs(r2 - g * (1 + R) / 2) < tol::kGen2Ratio, "at %.0f Hz gen2/gen1 %.9f, expected %.9f", sr, r2, g * (1 + R) / 2);
    for (int k = 2; k <= 6; ++k) {
      const double r = o.L[(k + 1) * D] / o.L[k * D];
      REQUIRE(std::fabs(r - g) < tol::kGenLater, "at %.0f Hz gen%d/gen%d %.6f, expected %.3f within 1e-3", sr, k + 1, k, r, g);
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
    auto e = mk({{"filt.loop.on", 0}, {"time.mode", 0}, {"time.ms", 100}, {"fb.amount", c[1] * 100}, {"mix", 100}});
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
    auto e = mk({{"time.mode", 0}, {"time.ms", 150}, {"fb.amount", 120}, {"mix", 100}});
    Vec in = zeros(S(6));
    for (long i = 0; i < S(0.1); ++i) in[i] = 0.9;
    run(*e, in, in);
    const double pk = e->linePeak();
    REQUIRE(pk < 2.0, "after the input stops the line holds %.3f (bound 2.0)", pk);
    REQUIRE(pk > 0.5, "the held loop died (line peak %.3f)", pk);
  }
  {
    auto e = mk({{"time.mode", 0}, {"time.ms", 150}, {"fb.amount", 120}, {"mix", 100}});
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
  auto e = mk({{"fb.amount", 120}, {"mix", 0}, {"time.mode", 0}, {"time.ms", 50}});
  Vec in = sine(S(1), 220, 3.0);
  in[100] = 7.5;
  const auto o = run(*e, in, in);
  for (long i = 0; i < S(1); ++i) REQUIRE(o.L[i] == in[i] && o.R[i] == in[i], "dry differs at %ld: %.17g vs %.17g", i, o.L[i], in[i]);
  return true;
}

// Mix 100 % is exactly the wet: nothing sounds before the first repeat (§8).
bool mixFullNoDry(Detail& d) {
  auto e = mk({{"mix", 100}, {"time.mode", 0}, {"time.ms", 100}});
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
  auto a = mk({{"filt.in.on", 0}, {"filt.loop.on", 0}, {"filt.out.on", 0}, {"fb.amount", 60}, {"mix", 70}, {"time.mode", 0}});
  auto b = mk({{"filt.in.on", 0}, {"filt.loop.on", 0}, {"filt.out.on", 0}, {"fb.amount", 60}, {"mix", 70}, {"time.mode", 0},
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
      auto e = mk({{"time.mode", 0}, {"time.ms", D / sr * 1000.0}, {"fb.amount", 100}, {"mix", 100},
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
      REQUIRE(worst <= 1.0 + tol::kLoopGainSlack, "|H_loop| peaks at %.7f (%.0f Hz) at %.0f Hz rate, filter %s %g/%g, frac %.2f", worst, at, sr,
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
      auto t = mk({{"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mix", 100}, {"filt.loop.on", 0}}, sr);
      const auto ot = run(*t, impulse(n), zeros(n));
      long arg = 0;
      double m = -1;
      for (long i = D - 50; i < D + 50; ++i)
        if (std::fabs(ot.L[i]) > m) { m = std::fabs(ot.L[i]); arg = i; }
      REQUIRE(arg == D && ot.L[D] == 1.0, "at %.0f Hz the repeat lands at %ld (%.6f), expected %ld", sr, arg, ot.L[arg], D);
    }
    auto e = mk({{"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mix", 100}, {"filt.loop.on", 1},
                 {"filt.loop.hpHz", 20}, {"filt.loop.lpHz", 2000}}, sr);
    const auto o = run(*e, impulse(n), zeros(n));
    for (int j = 0; j < 4; ++j) {
      const double db = 20 * std::log10(std::abs(dft(o.L, D - 3, D - 3 + S(0.1, sr), fs[j], sr)));
      if (sr == 48000.0) ref[j] = db;
      else REQUIRE(std::fabs(db - ref[j]) <= tol::kSrLoopDb, "at %.0f Hz rate the loop filter reads %.2f dB at %.0f Hz, 48 kHz reads %.2f", sr, db, fs[j], ref[j]);
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
                              set(64000, "mod.shape", 1), set(66000, "mod.seed", 42), set(68000, "time.law", 1), set(68001, "time.ms", 300),
                              // The 0.1 ms bound of the exponential law (k::kExpSlewS) must bind INSIDE a block pattern's block.
                              // From the live exponential move above: time.ms 1500 pulls the speed to the 0.5 cap, then 21 ms later
                              // (blocks are cut at every event, so the event itself starts a block) time.ms 20 turns it over cap to
                              // cap, 1.5 units = ~7 bound samples at 48 kHz: patterns 1 and 7 cut inside that span, the random ones too.
                              set(68600, "time.ms", 1500), set(69300, "time.ms", 20),
                              // time.mode (T-044, D-050): sec <-> hz flips move nothing (14000, 36000), a flip into sync moves
                              // the time (56000), a division change while synced moves it (57000, 62500), and a flip out of sync
                              // into hz moves it again (64500); hz -> sec at 67000 moves nothing
                              set(14000, "time.mode", 1), set(36000, "time.mode", 0), set(56000, "time.mode", 2),
                              set(57000, "time.div", 6), set(62500, "time.div", 12), set(64500, "time.mode", 1),
                              // back to repitch, so the 68000 / 68001 pair is a live exponential-law move
                              // (synced from 56000 it moved nothing; under jump from 40000 the law never mattered)
                              set(67000, "time.mode", 0), set(67500, "time.change", 0)};
  };
  // Modulation on (sine on line 0, spread to line 1) and a drift switch mid-render: M2's state must be block-size independent too.
  auto ref = mk({{"time.mode", 0}, {"time.ms", 120}, {"fb.amount", 40}, {"mod.depthMs", 3}, {"mod.rateHz", 1.7}, {"mod.spread", 60}});
  const auto o0 = run(*ref, inL, inR, evs(), 120.0, [](long) { return 4096; });
  std::uint32_t seed = 5;
  const std::vector<std::function<int(long)>> pats = {
      [](long) { return 1; }, [](long) { return 7; }, [](long) { return 64; }, [](long) { return 256; }, [](long) { return 333; },
      [&seed](long) { seed = seed * 1664525u + 1013904223u; return 1 + static_cast<int>(seed % 4096u); }};
  for (std::size_t p = 0; p < pats.size(); ++p) {
    auto e = mk({{"time.mode", 0}, {"time.ms", 120}, {"fb.amount", 40}, {"mod.depthMs", 3}, {"mod.rateHz", 1.7}, {"mod.spread", 60}});
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
  auto solo = mk({{"fb.amount", 60}, {"time.mode", 0}, {"time.ms", 90}, {"mod.depthMs", 4}, {"mod.shape", 1}});
  const auto os = run(*solo, in, in);
  auto a = mk({{"fb.amount", 60}, {"time.mode", 0}, {"time.ms", 90}, {"mod.depthMs", 4}, {"mod.shape", 1}});
  auto b = mk({{"fb.amount", 60}, {"time.mode", 0}, {"time.ms", 90}, {"mod.depthMs", 4}, {"mod.shape", 1}});
  Out oa{zeros(n), zeros(n)}, ob{zeros(n), zeros(n)};
  for (long p = 0; p < n; p += 256) {
    const int len = static_cast<int>(std::min<long>(256, n - p));
    a->process(in.data() + p, in.data() + p, oa.L.data() + p, oa.R.data() + p, len, 120);
    b->process(in.data() + p, in.data() + p, ob.L.data() + p, ob.R.data() + p, len, 120);
  }
  for (long i = 0; i < n; ++i) REQUIRE(oa.L[i] == os.L[i] && ob.L[i] == os.L[i], "interleaved instance differs from solo at %ld", i);
  auto r = mk({{"fb.amount", 60}, {"time.mode", 0}, {"time.ms", 90}});
  run(*r, in, in);
  r->reset();
  auto f = mk({{"fb.amount", 60}, {"time.mode", 0}, {"time.ms", 90}});
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
                   {"time.mode", 0}, {"time.ms", 60}});
      const long n = S(4);
      const auto o = run(*e, impulse(n), impulse(n, 0, -0.5));
      const long z = finalZeroRun(o);
      REQUIRE(maxAbs(o.L, 0, z) + maxAbs(o.R, 0, z) > 0.1, "control: an impulse is inaudible (route %d, change %d)", route, ch);
      REQUIRE(e->atRest() && z < S(3.5), "route %d, change %d: no exact silence after the sound (from %ld)", route, ch, z);
    }
  auto e = mk({{"filt.loop.on", 0}, {"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mix", 100}});
  const auto o = run(*e, impulse(S(0.3)), zeros(S(0.3)));
  for (long i = 0; i < S(0.3); ++i)
    if (i != 4800) REQUIRE(o.L[i] == 0.0 && o.R[i] == 0.0, "residue %.3g at sample %ld (only 4800 may sound)", o.L[i], i);
  return true;
}

// The tail (RS-7.3–7.6, SPEC §10): exact zero from the declared tail on,
// audible before it; kInfiniteTail at g >= 1.
bool tailExactZero(Detail& d) {
  auto e = mk({{"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 50}, {"mix", 100}});
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
    auto e = mk({{"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mix", 100}, {"filt.loop.hpHz", hp},
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
      auto e = mk({{"time.mode", 0}, {"time.ms", 50}, {"fb.amount", fb}, {"mix", 100}, {"filt.loop.on", loopOn}});
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
    auto e = mk({{"time.mode", 0}, {"time.ms", 150}, {"fb.amount", c.from}, {"mix", 100}});
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
  auto e = mk({{"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 99.99999999999999}, {"mix", 100}});
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
    auto e = mk({{"filt.loop.on", 0}, {"time.mode", 0}, {"time.ms", 70}, {"time.offL", sr == 48000.0 ? 10.0 : 0.0}, {"fb.amount", 0}, {"mix", 100}}, sr);
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
  auto e = mk({{"filt.in.on", 1}, {"fb.amount", 50}, {"mix", 100}, {"time.mode", 0}, {"time.ms", 100}});
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
  e->setParam("time.mode", 0);
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
    auto e = mk({{"mix", 100}, {"fb.amount", 0}, {"time.mode", 0}, {"time.ms", 100}});
    const auto o = run(*e, L, R, {set(at, "route.mode", 2)});
    const double c = std::max(clickRatio(o.L, at, 960), clickRatio(o.R, at, 960));
    REQUIRE(c <= 4.0, "routing switch reads %.1f (a step reads tens)", c);
  }
  {
    auto e = mk({{"mix", 100}, {"fb.amount", 0}, {"time.mode", 0}, {"time.ms", 100}, {"filt.out.hpHz", 1500}});
    const auto o = run(*e, L, R, {set(at, "filt.out.on", 1)});
    const double c = clickRatio(o.L, at, 960);
    REQUIRE(c <= 4.0, "filter switch reads %.1f", c);
  }
  return true;
}

// Crossfade time changes are equal-gain (D-007): two correlated heads never
// sum above the signal.
bool crossfadeEqualGain(Detail& d) {
  auto e = mk({{"filt.loop.on", 0}, {"fb.amount", 0}, {"mix", 100}, {"time.mode", 0}, {"time.ms", 100}, {"time.change", 1}});
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
  auto e = mk({{"mix", 100}, {"fb.amount", 0}, {"time.mode", 0}, {"time.ms", 500}, {"time.change", 2}});
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
    auto j = mk({{"time.mode", 0}, {"time.ms", 100}, {"time.change", 2}, {"time.xfadeMs", 40}, {"fb.amount", 0}}, sr);
    const long lj = fadeEnd(*j, in, S(0.3, sr), "time.ms", 200, 0.2 * sr);
    REQUIRE(lj - 1 == S(0.003, sr), "at %.0f Hz the jump sounded for %ld samples, 3 ms is %ld", sr, lj - 1, S(0.003, sr));
    auto c = mk({{"time.mode", 0}, {"time.ms", 100}, {"time.change", 1}, {"time.xfadeMs", 40}, {"fb.amount", 0}}, sr);
    const long lc = fadeEnd(*c, in, S(0.3, sr), "time.ms", 200, 0.2 * sr);
    REQUIRE(lc - 1 == S(0.040, sr), "at %.0f Hz the crossfade sounded for %ld samples, time.xfadeMs 40 is %ld", sr, lc - 1, S(0.040, sr));
  }
  return true;
}

// The retrigger queue (SPEC §3.5): targets arriving mid-fade wait; at the
// fade's end the head fades to the LATEST one (intermediates dropped).
bool retriggerQueue(Detail& d) {
  auto e = mk({{"fb.amount", 0}, {"time.mode", 0}, {"time.ms", 100}, {"time.change", 1}, {"time.xfadeMs", 60}});
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
  auto a = mk({{"fb.amount", 30}, {"time.mode", 0}, {"time.ms", 100}, {"time.change", 1}, {"time.xfadeMs", 200}});
  auto b = mk({{"fb.amount", 30}, {"time.mode", 0}, {"time.ms", 100}, {"time.change", 1}, {"time.xfadeMs", 200}});
  const auto oa = run(*a, in, in, {set(S(0.5), "time.ms", 300)});
  const auto ob = run(*b, in, in, {set(S(0.5), "time.ms", 300), set(S(0.55), "time.change", 2)});
  for (long i = 0; i < n; ++i) REQUIRE(oa.L[i] == ob.L[i], "a time.change flip mid-fade changed the output at %ld", i);
  return true;
}

// Synced time lands on the host tempo from the first block (SPEC §3.3).
bool tempoSync(Detail& d) {
  auto e = mk({{"filt.loop.on", 0}, {"time.mode", 2}, {"time.div", 9}, {"fb.amount", 0}, {"mix", 100}});
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
  auto [t1, f1] = at({{"time.mode", 2}, {"time.div", 0}}, 30.0);
  REQUIRE(std::fabs(t1[0] - 8.0) < 1e-9 && f1, "2/1 at 30 bpm plays at %.3f s (expected 8 s, folded)", t1[0]);
  auto [t2, f2] = at({{"time.mode", 2}, {"time.div", 0}, {"time.offR", 50}}, 40.0);
  REQUIRE(std::fabs(t2[0] - 6.0) < 1e-9 && std::fabs(t2[1] - 9.0) < 1e-9 && f2, "2/1 at 40 bpm, +50 %% R: %.3f / %.3f s (expected 6 / 9)", t2[0], t2[1]);
  auto [t3, f3] = at({{"time.mode", 0}, {"time.ms", 12000}, {"time.offR", 50}}, 120.0);
  REQUIRE(t3[1] == 12.0 && !f3, "free 12 s +50 %% R clamps to %.3f s (expected 12)", t3[1]);
  auto [t4, f4] = at({{"time.mode", 0}, {"time.ms", 1}, {"time.offL", -50}}, 120.0);
  REQUIRE(t4[0] == 0.001 && !f4, "1 ms -50 %% L must floor at 1 ms, resolved %.6f s", t4[0]);
  return true;
}

// Routing (SPEC §4): ping-pong alternates from the left; standard lines never
// cross; mid/side folds to its M line exactly (§4.1, by construction).
bool routing(Detail& d) {
  {
    auto e = mk({{"route.mode", 1}, {"filt.loop.on", 0}, {"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 50}, {"mix", 100}});
    const auto o = run(*e, impulse(S(0.4)), zeros(S(0.4)));
    REQUIRE(std::fabs(o.L[4800]) > 0.4 && o.R[4800] == 0.0, "ping-pong repeat 1 must be on the left (L %.3f R %.3f)", o.L[4800], o.R[4800]);
    // The far channel may carry the DC blocker's decaying tail (~1e-5), never a repeat.
    REQUIRE(std::fabs(o.R[9600]) > 0.1 && std::fabs(o.L[9600]) < 1e-4, "ping-pong repeat 2 must be on the right (L %.3g R %.3g)", o.L[9600], o.R[9600]);
    REQUIRE(std::fabs(o.L[14400]) > 0.02 && std::fabs(o.R[14400]) < 1e-4, "ping-pong repeat 3 must be on the left (L %.3g R %.3g)", o.L[14400], o.R[14400]);
  }
  {
    auto e = mk({{"route.mode", 0}, {"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 70}, {"mix", 100}});
    const auto o = run(*e, impulse(S(1)), zeros(S(1)));
    REQUIRE(maxAbs(o.R) == 0.0, "standard: a left impulse reached the right (%.3g)", maxAbs(o.R));
    auto r = mk({{"route.mode", 0}, {"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mix", 100}, {"filt.loop.on", 0}});
    const auto orr = run(*r, zeros(S(0.2)), impulse(S(0.2)));
    REQUIRE(orr.R[4800] == 1.0 && maxAbs(orr.L) == 0.0, "standard: a right-only impulse must wake the instance and repeat on the right (R %.3f)", orr.R[4800]);
  }
  {  // mid/side carries the side at its level: pure S in, M + S / M - S out
    auto e = mk({{"route.mode", 2}, {"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mix", 100}, {"filt.loop.on", 0}});
    const auto o = run(*e, impulse(S(0.2)), impulse(S(0.2), 0, -1.0));
    REQUIRE(o.L[4800] == 1.0 && o.R[4800] == -1.0, "mid/side: pure side in gives L %.3f R %.3f, expected 1 / -1", o.L[4800], o.R[4800]);
  }
  {
    g_rng = 21;
    const long n = S(0.8);
    Vec L(n), R(n), M(n);
    for (long i = 0; i < n; ++i) { L[i] = 0.5 * noise(); R[i] = 0.5 * noise(); M[i] = (L[i] + R[i]) / 2; }
    auto a = mk({{"route.mode", 2}, {"time.mode", 0}, {"time.ms", 70}, {"time.offL", 10}, {"time.offR", -20}, {"fb.amount", 50}, {"mix", 60}});
    auto b = mk({{"route.mode", 2}, {"time.mode", 0}, {"time.ms", 70}, {"time.offL", 10}, {"time.offR", -20}, {"fb.amount", 50}, {"mix", 60}});
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
  auto e = mk({{"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 80}, {"mix", 100}});
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
  auto e = mk({{"route.mode", 2}, {"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 60}, {"mix", 100}});
  const std::array<echo::KeyValue, 4> patch{{{"route.mode", 0}, {"filt.out.on", 1}, {"filt.out.hpHz", 1500}, {"time.ms", 200}}};
  const auto o = run(*e, L, R, {{at, [&patch](Echo& x) { x.loadState(patch); }}});
  const double c = std::max(clickRatio(o.L, at, 960), clickRatio(o.R, at, 960));
  REQUIRE(c <= 4.0, "the load reads %.1f across its fade (a step reads tens)", c);
  return true;
}

// A second load during the fade lands its values but never restarts the fade
// (D-020): the old audio still ends 20 ms after the FIRST load.
bool reloadNoRestart(Detail& d) {
  auto e = mk({{"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 80}, {"mix", 100}});
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
  auto a = mk({{"mix", 50}, {"fb.amount", 50}, {"time.mode", 0}, {"time.ms", 120}, {"mod.depthMs", 6}, {"mod.rateHz", 0.9}});
  auto b = mk({{"mix", 50}, {"fb.amount", 50}, {"time.mode", 0}, {"time.ms", 120}, {"mod.depthMs", 6}, {"mod.rateHz", 0.9}});
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
      auto e = mk({{"time.mode", 0}, {"time.ms", mv.from}, {"time.change", 0}, {"time.law", 0}, {"time.glide", glide}, {"fb.amount", 0},
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
      REQUIRE(worst < tol::kTapeSamples && worstCents < tol::kTapeCents, "glide %.2f, %g -> %g ms: off the full-rate model by %.4g samples (at %ld), %.2f cents of speed", glide,
              mv.from, mv.to, worst, wAt, worstCents);
    }
  return true;
}

// The exponential law (SPEC §3.6): a one-pole on ln D with time constant
// time.glide, D_{n+1} = D_n + D_n ln(T/D_n)/tau, uncapped for a small move; a
// big one runs at the cap (+12 st: read speed exactly 2) and never past it.
bool expLaw(Detail& d) {
  {
    auto e = mk({{"time.mode", 0}, {"time.ms", 500}, {"time.law", 1}, {"time.glide", 0.3}, {"fb.amount", 0}, {"filt.loop.on", 0}});
    const long n = S(2.0), at = S(0.5);
    const Vec in(n, 0.1);
    // The recurrence is EXACT here only because this move's first speed step (|V| = 0.176 per sample) is under the
    // law's 0.1 ms safety bound (k::kExpSlewS: 1/(1e-4 * 48000) = 0.208 per sample, SPEC §3.6, D-049 A1), which
    // therefore never binds. That is specific to 48 kHz: the bound is per second, so at 96 kHz it is 0.104 per
    // sample and the same move binds (it departs from the bare recurrence by 0.071 samples there). A bigger move
    // binds for its first few samples by design (reversalClick and onsetClick gate those); a smaller bound would
    // need this reference to carry it.
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
    auto e = mk({{"time.mode", 0}, {"time.ms", 500}, {"time.law", 1}, {"time.glide", 0.3}, {"fb.amount", 0}, {"filt.loop.on", 0}});
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
    auto e = mk({{"time.mode", 0}, {"time.ms", 500}, {"time.law", static_cast<double>(law)}, {"time.glide", glide}, {"fb.amount", 0},
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
    REQUIRE(worst < tol::kRepitchCents, "law %s, glide %.2f: the output's pitch departs from 1 - dD/dt by %.2f cents at %.2f s", law ? "exponential" : "tape", glide, worst, wAt);
  }
  return true;
}

// The hard read-speed invariant (D-006), over laws x moves x modulation:
// s in [0.25, 2.25] by construction (so the [0.25, 4] backstop never acts),
// never reversed, never past the line; a move longer than 3 s at the cap
// crossfades (the fallback) instead of gliding.
bool speedStress(Detail& d);  // T-042's seeded gestures, defined with the click gates (it renders on their pool)
bool speedInvariant(Detail& d) {
  struct Move { double from, to; bool fallback; };
  const Move moves[] = {{500, 250, false}, {250, 500, false}, {12000, 10, true}, {10, 12000, true}, {300, 300.5, false}};
  struct Mod { double depth, rate, shape, spread; };
  const Mod mods[] = {{0, 0.6, 0, 0}, {20, 10, 0, 0}, {20, 10, 1, 100}};
  for (int law : {0, 1, 2, 3})  // repitch tape, repitch exponential, crossfade, jump
    for (const Move& mv : moves)
      for (const Mod& md : mods) {
        if (law >= 2 && mv.fallback) continue;  // a crossfade has no fallback to show
        auto e = mk({{"time.mode", 0}, {"time.ms", mv.from}, {"time.law", law == 1 ? 1.0 : 0.0}, {"time.change", law >= 2 ? law - 1.0 : 0.0},
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
  return speedStress(d);
}

// The modulation (SPEC §7): a sine of depth A moves the read by exactly +-A
// (after the 250 ms ramp), and the output IS the input read at that moving
// position; the depth is limited by the curve's slope (10 Hz caps a sine at
// 0.25 / (2 pi f / fs) samples); drift stays inside its depth; the offset never
// moves more than 0.25 sample a sample; mod.spread puts the right line's sine
// 90 degrees later.
bool modulation(Detail& d) {
  {  // depth and read
    auto e = mk({{"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}, {"mod.depthMs", 5},
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
    auto g = mk({{"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}, {"mod.depthMs", 5}, {"mod.rateHz", 2}});
    Vec pos(static_cast<std::size_t>(n));
    const auto og = runEach(*g, in, in, {}, [](long) { return 120.0; }, [&](long i, Echo& x) { pos[static_cast<std::size_t>(i)] = x.readPosition(0); });
    for (long i = S(0.6); i < n; ++i) {
      const double want = 0.5 * std::sin(2 * kPi * 100 * (i - pos[static_cast<std::size_t>(i)]) / kSr);
      worst = std::max(worst, std::fabs(og.L[i] - want));
    }
    REQUIRE(worst < 1e-4, "the output departs from the input read at the reported position by %.3g", worst);
  }
  {  // the slope limit: 20 ms at 10 Hz is capped at 0.25 / (2 pi 10 / fs)
    auto e = mk({{"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mod.depthMs", 20}, {"mod.rateHz", 10}});
    const long n = S(1.5);
    const Vec in(n, 0.1);
    double hi = 0;
    runEach(*e, in, in, {}, [](long) { return 120.0; }, [&](long i, Echo& x) { if (i > S(0.6)) hi = std::max(hi, std::fabs(x.modOffset(0))); });
    const double A = 0.25 / (2 * kPi * 10 / kSr);
    REQUIRE(std::fabs(hi - A) < 0.01 * A, "a 20 ms sine at 10 Hz reaches %.1f samples, the slope limit is %.1f", hi, A);
  }
  {  // drift: inside its (slope-limited) depth, every sample
    auto e = mk({{"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"mod.depthMs", 20}, {"mod.rateHz", 10}, {"mod.shape", 1}, {"mod.seed", 7}});
    const long n = S(3.0);
    const Vec in(n, 0.1);
    double hi = 0;
    runEach(*e, in, in, {}, [](long) { return 120.0; }, [&](long i, Echo& x) { if (i > S(0.3)) hi = std::max(hi, std::fabs(x.modOffset(0))); });
    const double A = 0.25 / (3.5 * 10 / kSr);
    REQUIRE(hi <= A + 1e-9, "drift reached %.2f samples, its depth is %.2f (the Catmull-Rom overshoot must be clamped)", hi, A);
  }
  {  // a lifted depth limit (10 ms -> 100 ms) is slewed in, never stepped
    auto e = mk({{"time.mode", 0}, {"time.ms", 10}, {"time.change", 1}, {"fb.amount", 0}, {"mod.depthMs", 20}, {"mod.rateHz", 0.05}, {"mod.shape", 1}});
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
    auto e = mk({{"time.mode", 0}, {"time.ms", 200}, {"fb.amount", 0}, {"mod.depthMs", 5}, {"mod.rateHz", 0.05}, {"mod.shape", shape}, {"mod.seed", seed}});
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
    auto e = mk({{"time.mode", 2}, {"time.div", 9}, {"time.tempoMode", static_cast<double>(mode)}, {"fb.amount", 0}, {"time.glide", 0.05}});
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
    auto e = mk({{"time.mode", 2}, {"time.div", 9}, {"time.tempoMode", 2}, {"fb.amount", 0}});
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
      auto e = mk({{"time.mode", 0}, {"time.ms", 2900}, {"time.law", static_cast<double>(law)}, {"time.glide", glide}, {"fb.amount", 0}, {"mix", 100}});
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
    auto e = mk({{"time.mode", 0}, {"time.ms", 200}, {"fb.amount", 0}, {"mod.depthMs", 5}, {"mod.rateHz", 3}, {"mod.shape", 1}, {"mod.seed", seed0}});
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
    auto e = mk({{"time.mode", 2}, {"time.div", 6}, {"time.tempoMode", 0}, {"time.glide", 0.5}, {"fb.amount", 0}});
    const long n = S(2.0);
    const Vec in(n, 0.1);
    runEach(*e, in, in, {set(S(0.3), "time.div", 9)}, [](long i) { return i < S(0.35) ? 120.0 : 120.0 + 2.0 * (i - S(0.35)) / kSr; },
            [](long, Echo&) {});
    REQUIRE(e->fadeCount(0) == 0, "a key glide in flight crossfaded when the tempo moved (%llu fades)", static_cast<unsigned long long>(e->fadeCount(0)));
  }
  {  // follow: a 120 -> 130 step, then a +1 bpm/s ramp inside the cap
    auto e = mk({{"time.mode", 2}, {"time.div", 9}, {"time.tempoMode", 2}, {"fb.amount", 0}});
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
    auto e = mk({{"time.mode", 2}, {"time.div", 9}, {"time.tempoMode", 2}, {"time.glide", 0.05}, {"fb.amount", 0}});
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
    auto e = mk({{"time.mode", 0}, {"time.ms", 500}, {"time.change", ch}, {"fb.amount", 0}, {"mod.depthMs", 20}, {"mod.rateHz", 0.3}});
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
    auto g = mk({{"time.mode", 0}, {"time.ms", 30}, {"fb.amount", 0}, {"mod.depthMs", 10}, {"mod.rateHz", 0.6}});
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
      auto e = mk({{"time.mode", 0}, {"time.ms", 500}, {"time.law", static_cast<double>(law)}, {"time.glide", 0.2}, {"fb.amount", 0}}, sr);
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
// At another sample rate the same normalisation holds with the probe's phase increment taken at that rate.
double clickM2At(const Vec& y, long a, long b, double A, double sr) {
  const double w = 2 * kPi * kProbeHz / sr * kSDecl;
  double m = 0;
  for (long i = std::max(a, 1L); i < b - 1; ++i) m = std::max(m, std::fabs(y[i + 1] - 2 * y[i] + y[i - 1]));
  return m / (A * w * w);
}
double clickM2(const Vec& y, long a, long b, double A) { return clickM2At(y, a, b, A, kSr); }
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
Vec clickProbeAt(long n, double sr) {
  Vec in = sine(n, kProbeHz, 0.5, sr);
  for (long i = 0; i < S(0.05, sr); ++i) in[i] *= 0.5 - 0.5 * std::cos(kPi * i / S(0.05, sr));
  return in;
}
Vec clickProbe(long n) { return clickProbeAt(n, kSr); }

struct ClickCase {
  const char* name;
  bool jump;                                       // judged against the jump threshold
  std::vector<std::pair<const char*, double>> kv;  // owned: an initializer_list here would dangle
  std::function<std::vector<Event>(long)> events;  // given n
  std::function<double(double)> bpm;               // tempo at a (fractional) 48 kHz sample index, or null
  double seconds;
};

// `shift` moves every event, and the tempo's clock, later by that many samples: the click reading depends on the probe's phase at
// a speed step (up to 3.49 x the step), so a gate that must not pass by luck renders a case at several
// shifts and takes the worst (reversalClick and onsetClick, D-049 A1/A2). `sr` runs the case at another
// rate: the case is defined in seconds at 48 kHz, so its events are placed at the same instants.
ClickRun clickRun(const ClickCase& c, long shift = 0, double sr = kSr) {
  // A tempo case is defined in seconds like the rest: c.bpm takes a 48 kHz sample index, so at another rate it is
  // asked at the SAME instant, (i - shift) * 48000 / sr, fractional (a rounded index would step the tempo every
  // few samples at 192 kHz, a defect of the probe, not the engine). At 48 kHz and shift 0 this is bpm(i), exactly.
  const long n = S(c.seconds, sr);
  const Vec in = clickProbeAt(n, sr);
  auto e = mk({}, sr);
  for (const auto& [key, v] : c.kv)
    if (!e->setParam(key, v)) { std::fprintf(stderr, "click case %s: setParam(%s) refused\n", c.name, key); std::abort(); }
  e->resetStats();
  Out o;
  std::vector<Event> ev = c.events ? c.events(S(c.seconds)) : std::vector<Event>{};
  for (auto& x : ev) x.at = std::lround(static_cast<double>(x.at) * sr / kSr) + shift;
  if (c.bpm) o = runEach(*e, in, in, ev, [&](long i) { return c.bpm(static_cast<double>(i - shift) * kSr / sr); }, [](long, Echo&) {});
  else o = run(*e, in, in, ev, 120.0, [](long) { return 512; });
  const auto st = e->speedStats(0), st1 = e->speedStats(1);
  SpeedStatsView v{std::min(st.sMin, st1.sMin), std::max(st.sMax, st1.sMax), st.clamps + st1.clamps, st.floors + st1.floors + st.overruns + st1.overruns};
  return {clickM2At(o.L, S(0.2, sr), n, 0.5, sr), v};
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
  auto tempo = [](double i) { return 120.0 + 60.0 * std::sin(2 * kPi * i / kSr); };
  // The gestures T-038 gated through time.scale, kept through the keys (D-050 removed the scale; its move was bit-identical to the same
  // move through the time key, so the same time gestures stay reachable): a 1 Hz log sweep over x1/4 .. x4 of 500 ms (125 ms - 2 s) sent
  // every 64 samples, and single octave steps of a synced 1/4 (1/2, back, 1/8, back) at the old times, each given time to settle.
  auto octaveSweep = [](long n) {
    std::vector<Event> ev;
    for (long i = 0; i < n; i += 64) ev.push_back(set(i, "time.ms", 500.0 * std::exp2(2.0 * std::sin(2 * kPi * 1.0 * i / kSr))));
    return ev;
  };
  auto divSteps = [](long) {
    return std::vector<Event>{set(S(0.7), "time.div", 6), set(S(1.7), "time.div", 9), set(S(2.7), "time.div", 12), set(S(3.7), "time.div", 9)};
  };
  return {
      {"full sweep, repitch tape", false, {{"time.mode", 0}, {"time.ms", 500}, {"time.change", 0}, {"time.law", 0}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, sweep(1, 12000), nullptr, 3.0},
      {"full sweep, repitch exponential", false, {{"time.mode", 0}, {"time.ms", 500}, {"time.change", 0}, {"time.law", 1}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, sweep(1, 12000), nullptr, 3.0},
      {"full sweep, crossfade", false, {{"time.mode", 0}, {"time.ms", 500}, {"time.change", 1}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, sweep(1, 12000), nullptr, 3.0},
      {"full sweep, jump", true, {{"time.mode", 0}, {"time.ms", 500}, {"time.change", 2}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, sweep(1, 12000), nullptr, 3.0},
      {"200-400 ms sweep, tape, glide 0.05", false, {{"time.mode", 0}, {"time.ms", 300}, {"time.law", 0}, {"time.glide", 0.05}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, sweep(200, 400), nullptr, 3.0},
      {"200-400 ms sweep, exponential, glide 0.05", false, {{"time.mode", 0}, {"time.ms", 300}, {"time.law", 1}, {"time.glide", 0.05}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, sweep(200, 400), nullptr, 3.0},
      {"tempo sweep, crossfade", false, {{"time.mode", 2}, {"time.div", 9}, {"time.tempoMode", 0}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, nullptr, tempo, 3.0},
      {"tempo sweep, glide", false, {{"time.mode", 2}, {"time.div", 9}, {"time.tempoMode", 1}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, nullptr, tempo, 3.0},
      {"tempo sweep, follow", false, {{"time.mode", 2}, {"time.div", 9}, {"time.tempoMode", 2}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, nullptr, tempo, 3.0},
      {"move 250 -> 500 ms, exponential", false, {{"time.mode", 0}, {"time.ms", 250}, {"time.law", 1}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, move(1.0, 500), nullptr, 2.5},
      {"move 300 -> 200 ms, exponential, glide 0.05", false, {{"time.mode", 0}, {"time.ms", 300}, {"time.law", 1}, {"time.glide", 0.05}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, move(1.0, 200), nullptr, 2.0},
      {"move 500 -> 100 ms, tape, glide 0.05", false, {{"time.mode", 0}, {"time.ms", 500}, {"time.law", 0}, {"time.glide", 0.05}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, move(1.0, 100), nullptr, 2.0},
      {"move 500 -> 250 ms, tape", false, {{"time.mode", 0}, {"time.ms", 500}, {"time.law", 0}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, move(1.0, 250), nullptr, 2.5},
      {"move 500 -> 250 ms, jump", true, {{"time.mode", 0}, {"time.ms", 500}, {"time.change", 2}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, move(1.0, 250), nullptr, 1.5},
      {"1 Hz sweep 125 ms - 2 s through time.ms, repitch tape", false, {{"time.mode", 0}, {"time.ms", 500}, {"time.change", 0}, {"time.law", 0}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, octaveSweep, nullptr, 3.0},
      {"1 Hz sweep 125 ms - 2 s through time.ms, repitch exponential", false, {{"time.mode", 0}, {"time.ms", 500}, {"time.change", 0}, {"time.law", 1}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, octaveSweep, nullptr, 3.0},
      {"1 Hz sweep 125 ms - 2 s through time.ms, crossfade", false, {{"time.mode", 0}, {"time.ms", 500}, {"time.change", 1}, {"time.law", 0}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, octaveSweep, nullptr, 3.0},
      {"1 Hz sweep 125 ms - 2 s through time.ms, jump", true, {{"time.mode", 0}, {"time.ms", 500}, {"time.change", 2}, {"time.law", 0}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, octaveSweep, nullptr, 3.0},
      {"division steps, synced 1/4 -> 1/2 -> 1/4 -> 1/8 -> 1/4, repitch tape", false, {{"time.mode", 2}, {"time.div", 9}, {"time.change", 0}, {"time.law", 0}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, divSteps, nullptr, 4.7},
      {"division steps, synced 1/4 -> 1/2 -> 1/4 -> 1/8 -> 1/4, repitch exponential", false, {{"time.mode", 2}, {"time.div", 9}, {"time.change", 0}, {"time.law", 1}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, divSteps, nullptr, 4.7},
      {"division steps, synced 1/4 -> 1/2 -> 1/4 -> 1/8 -> 1/4, crossfade", false, {{"time.mode", 2}, {"time.div", 9}, {"time.change", 1}, {"time.law", 0}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, divSteps, nullptr, 4.7},
      {"division steps, synced 1/4 -> 1/2 -> 1/4 -> 1/8 -> 1/4, jump", true, {{"time.mode", 2}, {"time.div", 9}, {"time.change", 2}, {"time.law", 0}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, divSteps, nullptr, 4.7},
  };
}

// ---- M3 B4, T-040: the exponential law's onset and mid-glide reversal (SPEC §3.6; D-049, D-049 A1, A2) ----

// The click reading of a speed STEP depends on the probe's phase where it lands (M2 = 3.49 x step x |cos
// phase|, at 48 kHz): the exponential law's plain onset read 2.299 at the suite's fixed event time and 3.509
// at the worst phase, so the first gate passed it by luck. The gates therefore render every case at kPhases
// shifts and take the worst. |cos| has a HALF-cycle period, so the shifts span half a probe period: spread over
// a whole period, 8 shifts would give only 4 distinct readings (critic T-040 M1; D-049 A2). A step reading also
// grows in proportion to the sample rate, which is why the bound the law carries is per second
// (k::kExpSlewS) and why the gates run at 44.1, 48, 96 and 192 kHz (D-049 A4: T-041 fixed the tape law's cap kink and
// the fade's frozen head, the two steps that kept 96 and 192 kHz out of the gates).
constexpr int kPhases = 8;
long phaseShift(int j, double sr) {
  if (echo::plant::phaseSweepCollapsed) return 0;  // the planted control: a sweep that does not sweep
  return std::lround(j * (sr / kProbeHz) / (2 * kPhases));
}

// One reversal gesture: a time move from A to B, reversed `revMs` into its glide (B back to A), through one
// of the two ways a time moves. Octaves are exact (500 / 250 / 125 ms at 120 bpm: divisions 1/4, 1/8, 1/16).
enum class RevKey { Ms, Div };
const char* revKeyName(RevKey k) {
  return k == RevKey::Ms ? "time.ms" : "time.div";
}
struct RevCase { int law; RevKey key; int oct; bool up; double revMs; double sr; };

ClickRun reversalRun(const RevCase& c, long shift) {
  const double sr = c.sr, ratio = std::ldexp(1.0, c.oct);
  constexpr int kDiv[] = {9, 12, 15};  // 1/4, 1/8, 1/16
  std::vector<std::pair<const char*, double>> kv = {{"time.change", 0}, {"time.law", static_cast<double>(c.law)}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}};
  const char* key = "time.ms";
  double first = 0, back = 0;  // the key's value for the move, and for the reversal
  switch (c.key) {
    case RevKey::Ms: {
      const double A = c.up ? 500.0 / ratio : 500.0, B = c.up ? 500.0 : 500.0 / ratio;
      kv.insert(kv.end(), {{"time.mode", 0}, {"time.ms", A}});
      key = "time.ms"; first = B; back = A;
      break;
    }
    case RevKey::Div: {
      const int ia = c.up ? kDiv[c.oct] : kDiv[0], ib = c.up ? kDiv[0] : kDiv[c.oct];
      kv.insert(kv.end(), {{"time.mode", 2}, {"time.div", static_cast<double>(ia)}});
      key = "time.div"; first = ib; back = ia;
      break;
    }
  }
  // The first move waits 0.7 s: the line holds the probe by then even at 500 ms (output starts at 0.5 s),
  // and 0.5 s after the reversal covers its cap entries and the settling. Shorter than the suite's older
  // cases on purpose: this check renders 768 cases (runtime).
  const long n = S(0.7 + c.revMs / 1000.0 + 0.5, sr);
  const long t0 = S(0.7, sr) + shift, t1 = t0 + S(c.revMs / 1000.0, sr);
  const Vec in = clickProbeAt(n, sr);
  auto e = mk({}, sr);
  for (const auto& [k, v] : kv)
    if (!e->setParam(k, v)) { std::fprintf(stderr, "reversal case: setParam(%s) refused\n", k); std::abort(); }
  e->resetStats();
  const Out o = run(*e, in, in, {set(t0, key, first), set(t1, key, back)});
  const auto st = e->speedStats(0), st1 = e->speedStats(1);
  SpeedStatsView v{std::min(st.sMin, st1.sMin), std::max(st.sMax, st1.sMax), st.clamps + st1.clamps, st.floors + st1.floors + st.overruns + st1.overruns};
  return {clickM2At(o.L, S(0.2, sr), n, 0.5, sr), v};
}

// Folds the worst reading and the speed invariants over the phases of one case.
struct PhaseWorst { double m2 = 0; SpeedStatsView st{1e300, -1e300, 0, 0}; };
void foldPhase(PhaseWorst& w, const ClickRun& r) {
  w.m2 = std::max(w.m2, r.m2);
  w.st.sMin = std::min(w.st.sMin, r.st.sMin);
  w.st.sMax = std::max(w.st.sMax, r.st.sMax);
  w.st.clamps += r.st.clamps;
  w.st.floors += r.st.floors;
}

// The pool the click gates render on (D-049 A3). Each work item owns its engine and buffers and writes only its
// own result slot; the REQUIREs then run on the main thread in the fixed case order, so the verdict and every
// message are byte-identical to a serial run (`--click-report --threads 1` against `--threads N` is diffed).
// Fixed size: at most hardware_concurrency() and at least 1; `--threads N` lowers it (never above the hardware).
int g_threads = 0;  // 0: the hardware's
template <class F>
void parallelFor(std::size_t n, F&& f) {
  const std::size_t hw = std::max<std::size_t>(1, std::thread::hardware_concurrency());
  const std::size_t want = g_threads > 0 ? std::min<std::size_t>(static_cast<std::size_t>(g_threads), hw) : hw;
  const std::size_t nt = std::max<std::size_t>(1, std::min(want, n));
  if (nt == 1) {
    for (std::size_t i = 0; i < n; ++i) f(i);
    return;
  }
  std::atomic<std::size_t> next{0};  // the only shared mutable state: which item to take
  auto worker = [&] {
    for (;;) {
      const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
      if (i >= n) return;
      f(i);
    }
  };
  std::vector<std::thread> pool;
  for (std::size_t t = 1; t < nt; ++t) pool.emplace_back(worker);
  worker();
  for (auto& t : pool) t.join();  // join() orders every slot write before the reads that follow
}

// One case at the four rates and kPhases phases: 32 renders on the pool, folded to the worst of the phases per rate.
// The gates render ONE CASE AT A TIME and judge it before the next, in a fixed order: the verdict and its message are
// those of a serial run, and a planted fault stops the gate at its first reading over the bar instead of rendering the
// whole matrix first (echo_plants runs every click gate under each plant that guards it).
template <class F>
std::array<PhaseWorst, 4> worstAtRates(F&& render) {  // render(rate index, phase) -> ClickRun
  std::vector<ClickRun> res(4 * kPhases);
  parallelFor(res.size(), [&](std::size_t i) { res[i] = render(i / kPhases, static_cast<int>(i % kPhases)); });
  std::array<PhaseWorst, 4> w;
  for (std::size_t ri = 0; ri < 4; ++ri)
    for (int j = 0; j < kPhases; ++j) foldPhase(w[ri], res[ri * kPhases + static_cast<std::size_t>(j)]);
  return w;
}

// The existing click cases that run the exponential law (sweeps, single moves, octave steps): the ones the
// phase gate re-renders. Selected by their key, not their name.
bool usesExpLaw(const ClickCase& c) {
  for (const auto& [key, v] : c.kv)
    if (std::string_view(key) == "time.law" && v == 1.0) return true;
  return false;
}

// The ROADMAP M2 gate: every time-change mode under a 1 Hz full-range time
// sweep, the laws under a sweep they can follow, every tempo mode under a 1 Hz
// tempo sweep, and Q-A1's single moves, each under its threshold AND inside the
// speed invariants (no backstop hit, no floor or overrun, s <= 2.25: a bare
// jump is caught here, where the backstop turns it into a counted glide). The
// metric's own control: a planted click in a clean render must read over.
// D-049 A4: every case at 44.1, 48, 96 and 192 kHz and the worst of kPhases probe phases (a speed step, or a fade
// weight's kink, reads in proportion to the rate and to the probe's phase: the tape law's cap kink read 4.1 at
// 48 kHz and 18.4 at 192; the 3 ms jump 1.9 and 7.0). Tempo cases run at every rate too (clickRun).
constexpr double kClickRates[4] = {44100.0, 48000.0, 96000.0, 192000.0};
bool clickSweep(Detail& d) {
  for (const auto& c : clickCases()) {
    const auto w4 = worstAtRates([&](std::size_t ri, int j) { return clickRun(c, phaseShift(j, kClickRates[ri]), kClickRates[ri]); });
    for (std::size_t ri = 0; ri < 4; ++ri) {
      const PhaseWorst& w = w4[ri];
      char nm[160];
      std::snprintf(nm, sizeof nm, "%s, %g Hz", c.name, kClickRates[ri]);
      if (g_report)
        std::printf("  REPORT click M2 %-60s %6.3f  (speed %.3f..%.3f, %llu backstop, %llu floor/overrun)\n", nm, w.m2, w.st.sMin, w.st.sMax,
                    static_cast<unsigned long long>(w.st.clamps), static_cast<unsigned long long>(w.st.floors));
      REQUIRE(w.st.clamps == 0 && w.st.floors == 0 && w.st.sMax <= kSDecl + 1e-9 && w.st.sMin >= 0.25 - 1e-12,
              "%s: speed %.3f..%.3f, %llu backstop hits, %llu floor/overrun reads", nm, w.st.sMin, w.st.sMax,
              static_cast<unsigned long long>(w.st.clamps), static_cast<unsigned long long>(w.st.floors));
      const double lim = c.jump ? kClickMaxJump : kClickMax;
      REQUIRE(w.m2 <= lim, "%s reads M2 %.3f at its worst of %d probe phases (threshold %.3f)", nm, w.m2, kPhases, lim);
    }
  }
  auto e = mk({{"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}});
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

// The phase sweep's own control (critic T-040 H1, L1): a sweep that renders every case at one phase (the planted
// phaseSweepCollapsed) would pass every gate by luck, again. Its own check, called once. Three detectors (the
// plant is caught by the first, the structural one; the other two guard the shift paths of clickRun and reversalRun):
//  1. STRUCTURE: the 8 shifts are distinct and increasing, and the last is 7/16 of a probe period (within one
//     sample): spread over half a period, so a whole-period spacing (4 distinct readings) cannot come back unseen.
//  2. clickRun's shift path: the exponential 250 -> 500 ms move steps the speed by 0.5 (bounded to 0.208 per
//     sample, so the fix does not hide it): 0.197 .. 0.741 over the 8 shifts (spread 0.543); required > 0.3.
//  3. reversalRun's shift path (its own code): the tape law's 2 octave down move reversed at 400 ms, time.ms, 48 kHz,
//     with the catch-up unbounded (the plant tapeKinkUnbounded, echo_plants only): 0.789 .. 2.440 (spread 1.65);
//     required > 1.0. A collapsed sweep spreads by exactly 0 in both.
// The margins are about half the measured spreads: far above rounding, far below a working sweep.
constexpr double kSweepSpreadMin = 0.3;
[[maybe_unused]] constexpr double kReversalSpreadMin = 1.0;  // used by echo_plants only (the plant is the step source)
#ifdef ECHO_PLANTS
bool* plantFlag(std::string_view n);
#endif
bool phaseSweepWorks(Detail& d) {
  for (const double sr : {48000.0, 44100.0}) {
    const double period = sr / kProbeHz;
    for (int j = 1; j < kPhases; ++j)
      REQUIRE(phaseShift(j, sr) > phaseShift(j - 1, sr), "the probe-phase sweep is not distinct: shift %d is %ld samples, shift %d is %ld at %g Hz (every phase is the same phase)",
              j - 1, phaseShift(j - 1, sr), j, phaseShift(j, sr), sr);
    REQUIRE(std::fabs(phaseShift(kPhases - 1, sr) - (kPhases - 1) * period / (2.0 * kPhases)) <= 1.0,
            "the last probe-phase shift is %ld samples at %g Hz, expected %.1f (7/16 of a probe period: the sweep spans half a period)", phaseShift(kPhases - 1, sr), sr,
            (kPhases - 1) * period / (2.0 * kPhases));
  }
  const auto cases = clickCases();
  const ClickCase* probe = nullptr;
  for (const auto& c : cases)
    if (std::string_view(c.name) == "move 250 -> 500 ms, exponential") probe = &c;
  REQUIRE(probe != nullptr, "the phase-sweep control case (move 250 -> 500 ms, exponential) is gone from the click cases");
  [[maybe_unused]] const RevCase tape{0, RevKey::Ms, 2, false, 400.0, kSr};
  for (const double sr : {48000.0, 44100.0}) {
    std::vector<double> viaClick(kPhases);
    parallelFor(kPhases, [&](std::size_t j) { viaClick[j] = clickRun(*probe, phaseShift(static_cast<int>(j), sr), sr).m2; });
    double lo = 1e300, hi = -1e300;
    for (const double m : viaClick) { lo = std::min(lo, m); hi = std::max(hi, m); }
    if (g_report) std::printf("  REPORT phase-sweep control, 250 -> 500 ms exponential at %g Hz: readings %.3f .. %.3f over %d phases (spread %.3f, needs > %.2f)\n", sr, lo, hi, kPhases, hi - lo, kSweepSpreadMin);
    REQUIRE(hi - lo > kSweepSpreadMin, "the probe-phase sweep does not move the reading: the 250 -> 500 ms exponential move reads %.3f .. %.3f over %d phases at %g Hz (spread %.3f, needs > %.2f): every phase is the same phase",
            lo, hi, kPhases, sr, hi - lo, kSweepSpreadMin);
  }
#ifdef ECHO_PLANTS
  // Detector 3 needs a speed step to spread over the phases, and with T-041's braking catch-up the tape reversal has
  // none (it reads 0.79 .. 1.0): the step source is the planted tapeKinkUnbounded, switched on for this render only
  // and off again BEFORE any REQUIRE can return (a leaked plant would fail every later check). In the clean binary the
  // flag is constexpr false, so reversalRun's shift path is proven by echo_plants alone.
  std::vector<double> viaRev(kPhases);
  bool* const kink = plantFlag("tapeKinkUnbounded");
  REQUIRE(kink != nullptr, "the plant tapeKinkUnbounded is not in the plant table: the reversal control has no step source");
  const bool kinkWas = *kink;  // restored to what it was, not to false: a caller may have it on
  *kink = true;
  parallelFor(kPhases, [&](std::size_t j) { RevCase c = tape; viaRev[j] = reversalRun(c, phaseShift(static_cast<int>(j), kSr)).m2; });
  *kink = kinkWas;
  double lo = 1e300, hi = -1e300;
  for (const double m : viaRev) { lo = std::min(lo, m); hi = std::max(hi, m); }
  if (g_report) std::printf("  REPORT phase-sweep control, tape 2 oct down reversed at 400 ms, unbounded catch-up, at 48000 Hz: readings %.3f .. %.3f over %d phases (spread %.3f, needs > %.2f)\n", lo, hi, kPhases, hi - lo, kReversalSpreadMin);
  REQUIRE(hi - lo > kReversalSpreadMin, "the probe-phase sweep does not move a reversal's reading: the tape 2 oct down move reversed at 400 ms (unbounded catch-up) reads %.3f .. %.3f over %d phases (spread %.3f, needs > %.2f): every phase is the same phase",
          lo, hi, kPhases, hi - lo, kReversalSpreadMin);
#endif
  return true;
}

// SPEC §3.6 (D-049): a move reversed mid-glide reads under the click bar at EVERY probe phase (worst of
// kPhases), with zero backstop hits and the speed invariants. The unfixed exponential law steps its speed by
// 1.5 in one sample at a reversal and reads up to 5.1; the unbounded law is the plant expSlewOff. The FULL
// matrix (D-049 A2): both laws x time.ms / time.div x 1 and 2 octaves x
// down and up x reversed at 100 / 200 / 400 ms x 44.1, 48, 96 and 192 kHz (D-049 A4) = 192 cases x 8 phases (the tape
// law's reversals are gated too: T-041's braking catch-up is what keeps them under the bar at 96 and 192 kHz). The exponential
// law goes first, so the plant expSlewOff fails after one law's renders, not both.
constexpr int kReversalCases = 2 * 2 * 2 * 2 * 3 * 4;
bool reversalClick(Detail& d) {
  int cases = 0;
  for (const int law : {1, 0})
    for (const RevKey key : {RevKey::Ms, RevKey::Div})
      for (const int oct : {1, 2})
        for (const bool up : {false, true})
          for (const double rv : {100.0, 200.0, 400.0}) {
            const auto w4 = worstAtRates([&](std::size_t ri, int j) { return reversalRun(RevCase{law, key, oct, up, rv, kClickRates[ri]}, phaseShift(j, kClickRates[ri])); });
            for (std::size_t ri = 0; ri < 4; ++ri) {
              const PhaseWorst& w = w4[ri];
              ++cases;
              char nm[128];
              std::snprintf(nm, sizeof nm, "%s law, %s, %d oct %s, reversed at %g ms, %g Hz", law ? "exponential" : "tape", revKeyName(key), oct, up ? "up" : "down", rv, kClickRates[ri]);
              if (g_report)
                std::printf("  REPORT reversal M2 %-78s %6.3f  (speed %.3f..%.3f, %llu backstop, %llu floor/overrun)\n", nm, w.m2, w.st.sMin, w.st.sMax,
                            static_cast<unsigned long long>(w.st.clamps), static_cast<unsigned long long>(w.st.floors));
              REQUIRE(w.st.clamps == 0 && w.st.floors == 0 && w.st.sMax <= kSDecl + 1e-9 && w.st.sMin >= 0.25 - 1e-12,
                      "%s: speed %.3f..%.3f, %llu backstop hits, %llu floor/overrun reads", nm, w.st.sMin, w.st.sMax,
                      static_cast<unsigned long long>(w.st.clamps), static_cast<unsigned long long>(w.st.floors));
              REQUIRE(w.m2 <= kClickMax, "%s reads M2 %.3f at its worst of %d probe phases (threshold %.3f)", nm, w.m2, kPhases, kClickMax);
            }
          }
  REQUIRE(cases == kReversalCases, "the reversal matrix ran %d cases, expected %d: a loop went empty", cases, kReversalCases);
  return true;
}

// The exponential law's onsets (D-049 A2): every existing click case that runs the law, and two plain onsets
// (500 -> 250 ms down, 125 -> 500 ms up; the other glides and sizes read identically to 500 -> 250), each at
// 8 half-period phases and at 44.1, 48, 96 and 192 kHz (D-049 A4). A separate check from the reversals so that the plant
// expSlewOff fails it for ITS OWN reason too, and a count guard so an emptied selection cannot pass.
constexpr int kExpClickCases = 6, kPlainOnsets = 2;
bool onsetClick(Detail& d) {
  struct Onset { double from, to, glide; };
  std::vector<ClickCase> cases;
  for (const auto& c : clickCases())
    if (usesExpLaw(c)) cases.push_back(c);
  REQUIRE(static_cast<int>(cases.size()) == kExpClickCases, "%zu exponential-law click cases selected, expected %d", cases.size(), kExpClickCases);
  for (const Onset m : {Onset{500, 250, 0.3}, Onset{125, 500, 0.3}})
    cases.push_back(ClickCase{m.from == 500 ? "plain onset 500 -> 250 ms" : "plain onset 125 -> 500 ms", false,
                              {{"time.mode", 0}, {"time.ms", m.from}, {"time.law", 1}, {"time.glide", m.glide}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}},
                              [m](long) { return std::vector<Event>{set(S(1.0), "time.ms", m.to)}; }, nullptr, 3.0});
  REQUIRE(static_cast<int>(cases.size()) == kExpClickCases + kPlainOnsets, "%zu onset cases built", cases.size());
  int ran = 0;
  for (const ClickCase& c : cases) {
    const auto w4 = worstAtRates([&](std::size_t ri, int j) { return clickRun(c, phaseShift(j, kClickRates[ri]), kClickRates[ri]); });
    for (std::size_t ri = 0; ri < 4; ++ri) {
      const PhaseWorst& w = w4[ri];
      ++ran;
      char nm[128];
      std::snprintf(nm, sizeof nm, "%s, %g Hz", c.name, kClickRates[ri]);
      if (g_report)
        std::printf("  REPORT onset M2 %-75s %6.3f  (speed %.3f..%.3f, %llu backstop, %llu floor/overrun)\n", nm, w.m2, w.st.sMin, w.st.sMax,
                    static_cast<unsigned long long>(w.st.clamps), static_cast<unsigned long long>(w.st.floors));
      REQUIRE(w.st.clamps == 0 && w.st.floors == 0 && w.st.sMax <= kSDecl + 1e-9 && w.st.sMin >= 0.25 - 1e-12,
              "%s: speed %.3f..%.3f, %llu backstop hits, %llu floor/overrun reads", nm, w.st.sMin, w.st.sMax,
              static_cast<unsigned long long>(w.st.clamps), static_cast<unsigned long long>(w.st.floors));
      REQUIRE(w.m2 <= kClickMax, "exponential onset %s reads M2 %.3f at its worst of %d probe phases (threshold %.3f)", nm, w.m2, kPhases, kClickMax);
    }
  }
  REQUIRE(ran == 4 * (kExpClickCases + kPlainOnsets), "the onset gate ran %d cases, expected %d", ran, 4 * (kExpClickCases + kPlainOnsets));
  return true;
}


// ---- M3 B4, T-041 (D-049 A4): the other one-sample speed steps, and the tape law's catch-up ----

// A case's name must outlive the case (ClickCase holds a const char*): the built-up ones live here.
const char* keepName(std::string s) {
  static std::deque<std::string> names;  // a deque: growing it never moves the strings already handed out
  names.push_back(std::move(s));
  return names.back().c_str();
}

struct StepEv { double at; const char* key; double v; };
ClickCase stepCase(const std::string& what, int law, int change, double from, std::vector<StepEv> ev, double seconds, bool jump = false) {
  return ClickCase{keepName(std::string(law ? "exponential: " : "tape: ") + what), jump,
                   {{"time.mode", 0}, {"time.ms", from}, {"time.law", static_cast<double>(law)}, {"time.change", static_cast<double>(change)}, {"time.glide", 0.3},
                    {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}},
                   [ev](long) { std::vector<Event> v; for (const StepEv& e : ev) v.push_back(set(S(e.at), e.key, e.v)); return v; }, nullptr, seconds};
}

// T-042 (D-049 A8, A9): the same bar under MODULATION, where the offset's own speed steps read. A speed step reads in proportion
// to the offset's slope and to where it meets the read floor, so each gesture is gated at four lfoAts of the LFO, the crest, the trough
// and both zero crossings (the first build gated the crest only and missed a case of the criterion's own family, the outgoing clamp
// reading last sample's offset: M2 4.27 at 192 kHz, found near the trough). `at` is the time of the gesture's first move, chosen from the
// LFO's period (a sine at phase 0 from the wake, sin = 1 at a quarter period): lfoLands reads the offset there and requires it to be where
// the label says. They are the lean set of a 252-case census (both laws, drift and sine, moves, fades, reversals, limit lifts: worst M2
// 105 / 124 / 495 / 1971 at 44.1 - 192 kHz before the fix, 1.19 / 1.43 / 1.48 / 1.48 after) and of the critic's sweeps over the LFO
// phase: every mechanism found has a row here that reads over the bar without its fix (the plants).
struct LfoPhase { const char* name; double frac; int slopeSign; };  // frac: the fraction of the period at which the label holds
constexpr LfoPhase kLfoPhases[4] = {{"crest", 0.25, 0}, {"down zero crossing", 0.5, -1}, {"trough", 0.75, 0}, {"up zero crossing", 1.0, +1}};
double lfoMoveTime(double hz, double frac) { return (std::ceil(1.0 * hz - frac - 1e-9) + frac) / hz; }  // the first such place at or after 1 s
// phase: an index into kLfoPhases, or -1 for a row placed by hand, which states the offset it expects there (a fraction of the depth, falling)
struct ModRow { ClickCase c; double hz, at; int phase; double expect = 0; };
struct RelEv { double dt; const char* key; double v; };    // dt after the gesture's first move
ModRow modRow(const std::string& what, int law, int change, double from, double hz, int phase, double atHand, std::vector<RelEv> ev, double tail, bool jump,
              std::vector<std::pair<const char*, double>> extra) {
  const double at = phase >= 0 ? lfoMoveTime(hz, kLfoPhases[phase].frac) : atHand;
  std::vector<StepEv> abs;
  for (const RelEv& e : ev) abs.push_back({at + e.dt, e.key, e.v});
  const std::string nm = what + (phase >= 0 ? std::string(" at the ") + kLfoPhases[phase].name + " of the LFO" : std::string(" at ") + std::to_string(static_cast<int>(std::lround(at * 1000))) + " ms") +
                         ", 20 ms at " + (hz == 1.25 ? "1.25" : "5") + " Hz";
  ClickCase c = stepCase(nm, law, change, from, abs, at + tail, jump);
  c.kv.push_back({"mod.depthMs", 20});
  c.kv.push_back({"mod.rateHz", hz});
  for (const auto& kv : extra) c.kv.push_back(kv);  // a later key overrides an earlier one (clickRun sets them in order)
  return {c, hz, at, phase};
}
// Eight gestures, each gated at the four lfoAts of the LFO, and two placed by hand where the critic found what the four lfoAts miss: the
// aim branch (1.3 s: between the down crossing and the trough) and the outgoing clamp (1.38 s, just before the trough). A plant stops the
// gate at its first row over the bar, so the rows are ordered with the one row each mechanism reads over first, then the rest.
std::vector<ModRow> modRows() {
  using KV = std::vector<std::pair<const char*, double>>;
  struct Gesture { const char* what; int law, change; double from, hz; std::vector<RelEv> ev; double tail; bool jump; KV extra; };
  const std::vector<RelEv> retarget6 = {{0, "time.ms", 2}, {0.006, "time.change", 1}, {0.006, "time.ms", 400}};
  const std::vector<RelEv> retarget12 = {{0, "time.ms", 2}, {0.012, "time.change", 1}, {0.012, "time.ms", 400}};
  const std::vector<Gesture> g = {
      // criterion 2 of T-042: a 20 -> 2 ms glide retargeted to a crossfade 6 ms in. The offset's slew stops abruptly when its depth limit
      // lifts at the retarget (M2 2.28 / 4.55 at 96 / 192 kHz in both laws before the slope bound).
      {"20 -> 2 ms glide, crossfade to 400 ms 6 ms in", 0, 0, 20, 1.25, retarget6, 0.6, false, {}},
      {"20 -> 2 ms glide, crossfade to 400 ms 6 ms in", 1, 0, 20, 1.25, retarget6, 0.6, false, {}},
      // a fade to a short time: the outgoing head sounds and the offset answers to it alone (the floor clamp); the incoming head rides
      // the floor and the offset leaves it braking (the ride). 20 ms asked at 5 Hz is limited to ~8 ms by its slope.
      {"crossfade 100 -> 3 ms", 0, 1, 100, 5, {{0, "time.ms", 3}}, 0.6, false, {}},
      {"jump 100 -> 3 ms", 0, 2, 100, 5, {{0, "time.ms", 3}}, 0.6, true, {}},
      // the aim branch: a LONG fade to a short time, the offset aims at the incoming head's floor while it is under it (D-049 A9)
      {"crossfade 100 -> 5 ms over 40 ms", 0, 1, 100, 1.25, {{0, "time.ms", 5}}, 0.6, false, {{"time.xfadeMs", 40}}},
      // a deep repitch dive to the floor: the head's closing speed meets the offset's retreat (the floor brake). The exponential law at
      // glide 0.01 s: at its default 0.3 s it dives slowly enough that the brake changes nothing (it read the same without it), at 0.01
      // it reads 1.46 with the brake and 9.8 without, at 192 kHz.
      {"repitch 100 -> 2 ms", 0, 0, 100, 1.25, {{0, "time.ms", 2}}, 0.8, false, {}},
      {"repitch 100 -> 2 ms, glide 0.01", 1, 0, 100, 1.25, {{0, "time.ms", 2}}, 0.8, false, {{"time.glide", 0.01}}},
      // H1 (D-049 A9): the same retarget 12 ms in. The outgoing head's floor clamp must look one sample ahead of the offset (the plant
      // fbClampStale): the rows at the four lfoAts read under 1.5; the hand-placed one, 4.06 at 192 kHz without it.
      {"20 -> 2 ms glide, crossfade to 400 ms 12 ms in", 0, 0, 20, 1.25, retarget12, 0.6, false, {}},
  };
  std::vector<ModRow> r;
  std::vector<std::pair<int, int>> done;
  auto add = [&](int gi, int p) {
    for (const auto& dn : done) if (dn.first == gi && dn.second == p) return;
    done.push_back({gi, p});
    const Gesture& x = g[static_cast<std::size_t>(gi)];
    r.push_back(modRow(x.what, x.law, x.change, x.from, x.hz, p, 0, x.ev, x.tail, x.jump, x.extra));
  };
  const auto hand = [&](int gi, double at, double expect) {
    const Gesture& x = g[static_cast<std::size_t>(gi)];
    r.push_back(modRow(x.what, x.law, x.change, x.from, x.hz, -1, at, x.ev, x.tail, x.jump, x.extra));
    r.back().expect = expect;
  };
  add(0, 0); add(1, 0);          // criterion 2 at the crest
  add(2, 2); add(3, 2);          // the fades at the trough
  hand(4, 1.3, -0.7071);         // the aim branch: sin(225 deg), falling toward the trough
  hand(7, 1.38, -0.9877);        // the outgoing clamp: sin(261 deg), falling, just before the trough (slope still negative: the clamp looks ahead by it)
  add(5, 2); add(6, 2);          // the dives at the trough
  for (int p = 0; p < 4; ++p)
    for (int gi = 0; gi < 8; ++gi) add(gi, p);
  return r;
}
constexpr int kModRows = 8 * 4 + 2;
// The speed steps T-041 found beyond the exponential law's: the tape law's cap release (plain onsets, and a time.glide
// change mid-move, which sends the model past the cap again), the fade's frozen outgoing head (D-006's fallback taken
// from a MOVING head, and a crossfade-mode retarget of a glide in flight), and a retarget of a fade in flight; and, from T-042, the
// moves under deep modulation (modRows above, first in the list).
std::vector<ClickCase> stepCases() {
  std::vector<ClickCase> cs;
  for (const ModRow& m : modRows()) cs.push_back(m.c);
  cs.push_back(stepCase("plain onset 500 -> 125 ms", 0, 0, 500, {{1.0, "time.ms", 125}}, 3.0));
  cs.push_back(stepCase("long glide 2000 -> 20 ms", 0, 0, 2000, {{1.0, "time.ms", 20}}, 3.0));
  for (const int law : {0, 1}) {
    cs.push_back(stepCase("time.glide 0.3 -> 0.01, 100 ms into a 500 -> 125 ms move", law, 0, 500, {{1.0, "time.ms", 125}, {1.1, "time.glide", 0.01}}, 3.0));
    cs.push_back(stepCase("time.glide 0.01 -> 1, 100 ms into a 500 -> 125 ms move", law, 0, 500, {{0.9, "time.glide", 0.01}, {1.0, "time.ms", 125}, {1.1, "time.glide", 1.0}}, 3.0));
    // D-006's fallback from a head that is moving: a 12 s target (or 1 ms from 5 s) taken while the glide is at speed 2.0 or 0.5.
    cs.push_back(stepCase("fallback: 500 -> 125 ms, then 12000 ms 60 ms in", law, 0, 500, {{1.0, "time.ms", 125}, {1.06, "time.ms", 12000}}, 3.0));
    cs.push_back(stepCase("fallback: 500 -> 125 ms, then 12000 ms 200 ms in", law, 0, 500, {{1.0, "time.ms", 125}, {1.2, "time.ms", 12000}}, 3.0));
    cs.push_back(stepCase("fallback: 4000 -> 5000 ms, then 1 ms 100 ms in", law, 0, 4000, {{1.0, "time.ms", 5000}, {1.1, "time.ms", 1}}, 6.0));
    cs.push_back(stepCase("fallback: 4000 -> 5000 ms, then 1 ms 300 ms in", law, 0, 4000, {{1.0, "time.ms", 5000}, {1.3, "time.ms", 1}}, 6.0));
    // a crossfade-mode retarget of a repitch glide in flight (the same freeze, from startFade's other call site)
    cs.push_back(stepCase("glide in flight, time.change flipped to crossfade, retargeted", law, 0, 500, {{1.0, "time.ms", 125}, {1.1, "time.change", 1}, {1.12, "time.ms", 400}}, 3.0));
  }
  cs.push_back(stepCase("crossfade retargeted mid-fade", 0, 1, 500, {{1.0, "time.ms", 250}, {1.010, "time.ms", 1000}}, 3.0));
  cs.push_back(stepCase("jump retargeted mid-fade", 0, 2, 500, {{1.0, "time.ms", 250}, {1.001, "time.ms", 1000}}, 3.0, true));
  // T-044 (D-050): a flip into or out of sync is a time move that follows time.change, and reads under the bar like any other, at
  // the four rates (the old sync key was only ever covered by switchesClickFree, at 48 kHz). time.ms 500 and the division 1/8 (250 ms at
  // 120 bpm) differ, so every flip moves the time: sec / hz -> sync shortens it (the head runs at speed 2 under repitch),
  // sync -> sec / hz lengthens it (speed 0.5). Last in the list, so every older plant still stops the gate at its own row.
  struct Flip { const char* what; double from, to; };
  const Flip flips[] = {{"sec -> sync", 0, 2}, {"sync -> sec", 2, 0}, {"hz -> sync", 1, 2}, {"sync -> hz", 2, 1}};
  struct Mode { int law, change; };
  for (const Mode m : {Mode{0, 0}, Mode{1, 0}, Mode{0, 1}, Mode{0, 2}})
    for (const Flip& f : flips) {
      const char* const changeName[3] = {"repitch", "crossfade", "jump"};
      ClickCase c = stepCase(std::string("time.mode ") + f.what + ", 500 ms / 1/8 (250 ms), " + changeName[m.change], m.law, m.change, 500, {{1.0, "time.mode", f.to}}, 3.0, m.change == 2);
      for (auto& kv : c.kv)
        if (std::string_view(kv.first) == "time.mode") kv.second = f.from;
      c.kv.push_back({"time.div", 12});
      cs.push_back(c);
    }
  return cs;
}
constexpr int kStepCases = kModRows + 2 + 2 * 7 + 2 + 4 * 4;

// Each modulated row's LFO label is a claim about the offset at the row's first move: read it there, at every rate, on the engine itself
// (one fresh engine per distinct LFO rate and place: the offset does not depend on the gesture). A crest is +A, a trough -A, a crossing is 0
// with the slope's sign, A being the depth the slope limit allows (20 ms, or 0.25 / (2 pi f / fs) samples at 5 Hz).
bool lfoLands(Detail& d) {
  struct LfoAt { double hz, at; int phase; double expect; };  // phase -1: a hand-placed row, with the offset it states
  std::vector<LfoAt> lfoAts;
  for (const ModRow& m : modRows()) {
    bool seen = false;
    for (const LfoAt& p : lfoAts) seen = seen || (p.hz == m.hz && p.phase == m.phase && (m.phase >= 0 || p.at == m.at));
    if (!seen) lfoAts.push_back({m.hz, m.at, m.phase, m.expect});
  }
  struct Read { double m0 = 0, m1 = 0, A = 0; };
  std::vector<Read> res(4 * lfoAts.size());
  parallelFor(res.size(), [&](std::size_t i) {
    const double sr = kClickRates[i / lfoAts.size()];
    const LfoAt& p = lfoAts[i % lfoAts.size()];
    auto e = mk({{"time.mode", 0}, {"time.ms", 100}, {"time.change", 0}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}, {"mod.depthMs", 20}, {"mod.rateHz", p.hz}}, sr);
    const long at = std::lround(p.at * sr);
    const Vec in = clickProbeAt(at + 2, sr);
    Vec oL(64), oR(64);
    for (long n = 0; n < at + 1;) {  // 64-sample blocks, then single samples for the last few
      const int len = n < at - 70 ? 64 : 1;
      e->process(in.data() + n, in.data() + n, oL.data(), oR.data(), len, 120.0);
      if (n == at - 1) res[i].m0 = e->modOffset(0);
      if (n == at) res[i].m1 = e->modOffset(0);
      n += len;
    }
    res[i].A = std::min(0.020 * sr, 0.25 / (2 * kPi * p.hz / sr));
  });
  for (std::size_t i = 0; i < res.size(); ++i) {
    const LfoAt& p = lfoAts[i % lfoAts.size()];
    const double sr = kClickRates[i / lfoAts.size()], m = res[i].m1 / res[i].A;
    if (p.phase < 0) {  // a hand-placed row: its stated offset, falling. A timing change that detunes it fails here, not silently.
      if (g_report) std::printf("  REPORT LFO hand-placed row %g Hz at %.4f s, %6.0f Hz: offset %+.4f of its depth (states %+.4f)\n", p.hz, p.at, sr, m, p.expect);
      REQUIRE(std::fabs(m - p.expect) <= 0.02 && res[i].m1 - res[i].m0 < 0,
              "the hand-placed row at %g Hz and %g Hz moved off its LFO place: the offset reads %+.4f of its depth (slope %+.4f), it states %+.4f and falling", p.hz, sr, m, res[i].m1 - res[i].m0, p.expect);
      continue;
    }
    const LfoPhase& ph = kLfoPhases[p.phase];
    if (g_report) std::printf("  REPORT LFO %-20s %g Hz at %.4f s, %6.0f Hz: offset %+.4f of its depth (%.1f samples)\n", ph.name, p.hz, p.at, sr, m, res[i].m1);
    const bool crossing = ph.slopeSign != 0;
    const double want = crossing ? 0.0 : (ph.frac == 0.25 ? 1.0 : -1.0);
    REQUIRE(std::fabs(m - want) <= 0.03 && (!crossing || (res[i].m1 - res[i].m0) * ph.slopeSign > 0),
            "the %s row at %g Hz and %g Hz does not land on the LFO's %s: the offset reads %+.4f of its depth there (slope %+.4f)", ph.name, p.hz, sr, ph.name, m, res[i].m1 - res[i].m0);
  }
  return true;
}

// D-049 A4: the cases above at 44.1 - 192 kHz, worst of 8 phases, under the bar with zero backstop hits. The plants
// tapeKinkUnbounded (the onsets and glide changes read 2.4 .. 12) and fbFrozenOutgoing (the fallbacks and the retarget
// read 3.5 at 48 kHz, 14 at 192) each break their own rows and not the other's. T-042's six plants break the modulated rows, each its own,
// at 192 kHz (all readings in the T-042 trace): modAccelUnbounded the 20 -> 2 ms retarget at the crest (4.5, both laws; it lifts every fade
// and dive row too, 3 - 7, since the slope bound is part of what keeps them smooth); modFloorClampsBoth the crossfade at the trough (the speed
// reverses, to -954; M2 868); modBoundKeepsSlope the jump at the trough (3.5); modRideKink the hand-placed aim row only (3.40; the rows
// at the four lfoAts read clean without it); modNoFloorBrake the tape dive at the trough (8.4, exponential 9.8) and nothing else;
// fbClampStale the hand-placed H1 row only (4.06). A count guard so an emptied list cannot pass.
bool stepClick(Detail& d) {
  const auto cases = stepCases();
  const std::size_t nc = cases.size();
  REQUIRE(static_cast<int>(nc) == kStepCases, "%zu speed-step cases built, expected %d", nc, kStepCases);
  if (!lfoLands(d)) return false;
  int ran = 0;
  for (const ClickCase& c : cases) {
    const auto w4 = worstAtRates([&](std::size_t ri, int j) { return clickRun(c, phaseShift(j, kClickRates[ri]), kClickRates[ri]); });
    for (std::size_t ri = 0; ri < 4; ++ri) {
      const PhaseWorst& w = w4[ri];
      ++ran;
      char nm[192];
      std::snprintf(nm, sizeof nm, "%s, %g Hz", c.name, kClickRates[ri]);
      if (g_report)
        std::printf("  REPORT step M2 %-100s %6.3f  (speed %.3f..%.3f, %llu backstop, %llu floor/overrun)\n", nm, w.m2, w.st.sMin, w.st.sMax,
                    static_cast<unsigned long long>(w.st.clamps), static_cast<unsigned long long>(w.st.floors));
      REQUIRE(w.st.clamps == 0 && w.st.floors == 0 && w.st.sMax <= kSDecl + 1e-9 && w.st.sMin >= 0.25 - 1e-12,
              "%s: speed %.3f..%.3f, %llu backstop hits, %llu floor/overrun reads", nm, w.st.sMin, w.st.sMax,
              static_cast<unsigned long long>(w.st.clamps), static_cast<unsigned long long>(w.st.floors));
      REQUIRE(w.m2 <= kClickMax, "%s reads M2 %.3f at its worst of %d probe phases (threshold %.3f)", nm, w.m2, kPhases, kClickMax);
    }
  }
  REQUIRE(ran == 4 * kStepCases, "the speed-step gate ran %d cases, expected %d", ran, 4 * kStepCases);
  return true;
}

// T-042 (D-049 A8): random control gestures at deep modulation, read on the engine's own speed statistics. The depth-20 report
// (time.ms / time.change / time.law / time.glide gestures under mod.depthMs 20 read a speed of -870 with reverse > 0) is real and
// dates from M2: the offset's floor clamp bounded the offset by BOTH heads of a fade, so a fade to a short time moved it by up to
// its whole size in one sample under the head still sounding. It reads on SpeedStats and not on M2 alone: some of those cases read
// speed -2547 with M2 0.2, because the outgoing head sounds silence and a click metric over silence reads nothing. Four seeds at
// four rates at depths 5 and 20 (a 7 Hz sine), the gestures from a seeded mulberry32 stream (no wall clock): the speed stays in
// [0.25, 2.25], never reverses, and neither the backstop nor the read floor acts. A vacuity guard: the gestures start fades.
struct Mulberry {
  std::uint32_t s;
  double next() {  // [0, 1)
    std::uint32_t t = (s += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + ((t ^ (t >> 7)) * (t | 61u));
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  }
};
struct StressRun { Echo::SpeedStats st[2]; std::uint64_t fades = 0; double peak = 0; };  // peak: the largest offset reached, in samples
StressRun stressRun(double sr, double depth, double rateHz, int seed) {
  Mulberry r{0x9E3779B9u * static_cast<std::uint32_t>(seed) + static_cast<std::uint32_t>(sr) + static_cast<std::uint32_t>(depth) + static_cast<std::uint32_t>(rateHz * 100)};
  Echo e;
  e.prepare(sr, 4096);
  const std::pair<const char*, double> kv[] = {{"time.mode", 0}, {"time.ms", 400}, {"time.change", 0}, {"time.glide", 0.3}, {"fb.amount", 0}, {"mod.depthMs", depth},
                                               {"mod.rateHz", rateHz}, {"mod.shape", 0}, {"filt.loop.on", 0}, {"mix", 100}};
  for (const auto& [k, v] : kv)
    if (!e.setParam(k, v)) { std::fprintf(stderr, "speed stress: setParam(%s) refused\n", k); std::abort(); }
  const long n = S(8.0, sr);
  const Vec in = clickProbeAt(n, sr);
  Vec oL(64), oR(64);
  e.resetStats();
  long pos = 0, nextEv = 0;
  StressRun o;
  while (pos < n) {
    if (pos >= nextEv) {
      // Ten draws a gesture, always in this order (never inside an argument list: C++ leaves that order unspecified), whether or
      // not they are used, so a seed's stream is the same whatever branches it takes.
      const double dMs = r.next(), dSkew = r.next(), dChange = r.next(), dChangeV = r.next(), dLaw = r.next(), dLawV = r.next(), dGlide = r.next(), dGlideV = r.next();
      const double dGap = r.next(), dGap2 = r.next();
      e.setParam("time.ms", std::exp(std::log(12000.0) * std::pow(dMs, dSkew < 0.5 ? 1.0 : 2.5)));  // 1 ms .. 12 s, log-uniform or skewed short
      if (dChange < 0.3) e.setParam("time.change", std::floor(dChangeV * 3.0));
      if (dLaw < 0.3) e.setParam("time.law", std::floor(dLawV * 2.0));
      if (dGlide < 0.2) e.setParam("time.glide", std::exp(std::log(0.01) + (std::log(5.0) - std::log(0.01)) * dGlideV));
      nextEv = pos + S(0.005 + 0.4 * dGap * dGap2, sr);
    }
    const int len = static_cast<int>(std::min<long>(64, n - pos));
    const auto u = static_cast<std::size_t>(pos);
    e.process(in.data() + u, in.data() + u, oL.data(), oR.data(), len, 120.0);
    pos += len;
    o.peak = std::max({o.peak, std::fabs(e.modOffset(0)), std::fabs(e.modOffset(1))});
  }
  for (int k = 0; k < 2; ++k) { o.st[k] = e.speedStats(k); o.fades += e.fadeCount(k); }
  return o;
}
bool speedStress(Detail& d) {
  constexpr int kSeeds = 4;
  // The depth is asked for in ms and LIMITED by the curve's slope (§7: A <= 0.25 / (2 pi f / fs) samples), so a label can go false: a 7 Hz sine
  // caps at 5.68 ms whatever is asked (the first build's "depth 20" was that). The 1.25 Hz row is where 20 ms is real; every row asserts the
  // offset it reached against what its label allows.
  struct Cfg { double depthMs, hz; };
  const Cfg cfgs[] = {{5.0, 7.0}, {20.0, 7.0}, {20.0, 1.25}};
  constexpr std::size_t nc = 3, nj = 4 * nc * kSeeds;
  std::vector<StressRun> res(nj);  // slot = (rate * nc + config) * kSeeds + seed - 1
  parallelFor(nj, [&](std::size_t i) {
    const Cfg& c = cfgs[(i / kSeeds) % nc];
    res[i] = stressRun(kClickRates[i / (nc * kSeeds)], c.depthMs, c.hz, static_cast<int>(i % kSeeds) + 1);
  });
  for (std::size_t i = 0; i < nj; ++i) {
    const Cfg& c = cfgs[(i / kSeeds) % nc];
    const double sr = kClickRates[i / (nc * kSeeds)];
    for (int k = 0; k < 2; ++k) {
      const auto& st = res[i].st[k];
      REQUIRE(st.sMin >= 0.25 - 1e-12 && st.sMax <= 2.25 + 1e-9 && st.reverse == 0 && st.clamps == 0 && st.overruns == 0 && st.floors == 0,
              "gestures at %g Hz, depth %g ms at %g Hz, seed %zu, line %d: speed [%.4f, %.4f], %llu reverse, %llu backstop, %llu overruns, %llu floors", sr, c.depthMs, c.hz,
              i % kSeeds + 1, k, st.sMin, st.sMax, static_cast<unsigned long long>(st.reverse), static_cast<unsigned long long>(st.clamps),
              static_cast<unsigned long long>(st.overruns), static_cast<unsigned long long>(st.floors));
    }
    REQUIRE(res[i].fades >= 20, "gestures at %g Hz, depth %g ms at %g Hz, seed %zu started only %llu fades: the stress no longer reaches the fades it guards (vacuous)", sr, c.depthMs, c.hz,
            i % kSeeds + 1, static_cast<unsigned long long>(res[i].fades));
    const double reach = std::min(c.depthMs * 1e-3 * sr, 0.25 / (2 * kPi * c.hz / sr));  // the offset the label allows, in samples
    if (g_report) std::printf("  REPORT stress %g Hz, depth %g ms at %g Hz, seed %zu: offset reached %.2f of %.2f samples (%.1f%%), %llu fades\n", sr, c.depthMs, c.hz, i % kSeeds + 1, res[i].peak, reach,
                              100.0 * res[i].peak / reach, static_cast<unsigned long long>(res[i].fades));
    REQUIRE(res[i].peak >= 0.9 * reach, "gestures at %g Hz, depth %g ms at %g Hz, seed %zu: the offset reached only %.2f of the %.2f samples the label allows: the depth is not what it says (vacuous)", sr, c.depthMs, c.hz,
            i % kSeeds + 1, res[i].peak, reach);
  }
  return true;
}

// The outgoing head's bound (D-049 A6). A fade's outgoing head slews its speed to rest under the bound a = 1/(k::kExpSlewS fs)
// per sample, and is held inside the line by a clamp that must not itself step the speed: the first form (kMinRead + |offset|)
// bound under a MODULATED fade from a shortening glide and stepped it by up to 27 in one sample. M2 cannot be the gate for the
// modulated case: under 20 ms of modulation at 1.25 Hz the OFFSET's own slew limit (0.25 a sample) steps the read speed and the
// click reads 2.3 at 96 kHz and 4.5 at 192 even with the head right (the modulation's, not the fade's). So this gate reads the head
// alone, the read position without the offset, and requires its speed to change by at most a from one sample to the next across
// the whole move, at 44.1 - 192 kHz, both laws, a crossfade retarget 1 - 12 ms into a 20 -> 2 ms glide under 20 ms of modulation.
// A vacuity guard, read only on steps whose last two samples are fading (Echo::fading), so the step is the outgoing head's
// speed against the speed just before it (the slew to rest takes a sample or two: the glide's speed here is ~0.15 under
// the exponential law at 44.1 kHz, under a): the worst there must reach 0.5 a, which only a fade whose outgoing head
// actually slews can do. Read over the whole move it would
// be vacuous, since the law's own onset step reaches a with no fade at all (critic re-check L1).
struct OutgoingRun { double worst = 0, worstInFade = 0; };
OutgoingRun outgoingWorstStep(double sr, int law, double x) {
  Echo e;
  e.prepare(sr, 4096);
  const std::pair<const char*, double> kv[] = {{"time.mode", 0}, {"time.ms", 20}, {"time.law", static_cast<double>(law)}, {"time.change", 0}, {"time.glide", 0.3}, {"fb.amount", 0},
                                               {"filt.loop.on", 0}, {"mix", 100}, {"mod.depthMs", 20}, {"mod.rateHz", 1.25}};
  for (const auto& [k, v] : kv)
    if (!e.setParam(k, v)) { std::fprintf(stderr, "outgoing bound: setParam(%s) refused\n", k); std::abort(); }
  const long n = S(1.6, sr), at = S(1.0, sr), at2 = at + std::lround(x * sr);
  std::vector<double> h(static_cast<std::size_t>(n));
  std::vector<char> f(static_cast<std::size_t>(n));
  double oL = 0, oR = 0;
  for (long i = 0; i < n; ++i) {
    if (i == at) e.setParam("time.ms", 2);
    if (i == at2) { e.setParam("time.change", 1); e.setParam("time.ms", 400); }
    const double s = 0.1 * std::sin(0.05 * static_cast<double>(i));
    e.process(&s, &s, &oL, &oR, 1, 120.0);
    h[static_cast<std::size_t>(i)] = e.readPosition(0) - e.modOffset(0);  // the head's own position: the fade's outgoing head while it fades
    f[static_cast<std::size_t>(i)] = e.fading(0);
  }
  OutgoingRun r;
  for (long i = at - 50; i < n; ++i) {
    const std::size_t u = static_cast<std::size_t>(i);
    const double a = h[u], b = h[u - 1], c = h[u - 2];
    if (std::fabs(a - b) >= 100 || std::fabs(b - c) >= 100) continue;  // a handover (a fade's end) is not a speed
    const double step = std::fabs(a - 2 * b + c);
    r.worst = std::max(r.worst, step);
    if (f[u] && f[u - 1]) r.worstInFade = std::max(r.worstInFade, step);  // the outgoing head's speed against the one before it
  }
  return r;
}
// D-049 A9, A10: the head's bound above holds AWAY from the read floor. For the READ, the head and the offset each move by at most a, so its
// speed changes by at most 2 a in this gesture (the head alone can change by more at contact: the floor brake, below, is an exception, up to
// 5.8 a at 192 kHz, cancelled in the read by the offset). That is NOT a general limit: a search reads 2.000 a far from the floor and 2.955 a
// when the floor stops the read, and the M2 bar governs (SPEC §3.6). So the contact case reads the read position itself, dA + the offset, from the first move
// to the fade's end (the handover to the incoming head is a position change by design, and not read): the critic's gesture, a 20 -> 2 ms
// glide at 1.38 s, near the LFO's trough, retargeted to a crossfade 12 ms in, at the worst of the 8 probe phases (the timing of the retarget
// against the offset's slope decides whether the clamp binds, as M2's rows are timed). Its bound is 2 a, the bound for this gesture and no general limit, and applies to this case only; the cases
// above keep a (1 + 1e-6). The plant fbClampStale (the clamp reads last sample's offset) reads far above it (5.7 a at 192 kHz, M2 4.06).
double contactWorstStep(double sr, long shift) {
  Echo e;
  e.prepare(sr, 4096);
  const std::pair<const char*, double> kv[] = {{"time.mode", 0}, {"time.ms", 20}, {"time.law", 0}, {"time.change", 0}, {"time.glide", 0.3}, {"fb.amount", 0},
                                               {"filt.loop.on", 0}, {"mix", 100}, {"mod.depthMs", 20}, {"mod.rateHz", 1.25}};
  for (const auto& [k, v] : kv)
    if (!e.setParam(k, v)) { std::fprintf(stderr, "contact bound: setParam(%s) refused\n", k); std::abort(); }
  const long at = S(1.38, sr) + shift, at2 = at + std::lround(0.012 * sr), n = at2 + S(0.2, sr);
  double worst = 0, pP = 0, vP = 0, oL = 0, oR = 0;
  for (long i = 0; i < n; ++i) {
    if (i == at) e.setParam("time.ms", 2);
    if (i == at2) { e.setParam("time.change", 1); e.setParam("time.ms", 400); }
    const double s = 0.1 * std::sin(0.05 * static_cast<double>(i));
    e.process(&s, &s, &oL, &oR, 1, 120.0);
    const double p = e.readPosition(0), v = p - pP;
    if (i > at && !(!e.fading(0) && i >= at2)) worst = std::max(worst, std::fabs(v - vP));
    pP = p; vP = v;
  }
  return worst;
}
bool outgoingBound(Detail& d) {
  const double xs[] = {0.001, 0.002, 0.003, 0.004, 0.006, 0.008, 0.010, 0.012};
  constexpr std::size_t nx = sizeof xs / sizeof xs[0];
  std::vector<OutgoingRun> res(4 * 2 * nx);  // slot = (rate * 2 + law) * nx + x
  std::vector<double> contact(4 * kPhases);  // slot = rate * kPhases + phase (the tape law: the exponential law's 20 -> 2 ms glide barely moves in 12 ms, so it reads nothing there)
  parallelFor(res.size() + contact.size(), [&](std::size_t i) {
    if (i < res.size()) res[i] = outgoingWorstStep(kClickRates[i / (2 * nx)], static_cast<int>((i / nx) % 2), xs[i % nx]);
    else { const std::size_t c = i - res.size(); contact[c] = contactWorstStep(kClickRates[c / kPhases], phaseShift(static_cast<int>(c % kPhases), kClickRates[c / kPhases])); }
  });
  if (g_report)  // every rate's contact reading before the first REQUIRE can stop the gate
    for (std::size_t ri = 0; ri < 4; ++ri) {
      double c = 0;
      for (int j = 0; j < kPhases; ++j) c = std::max(c, contact[ri * kPhases + static_cast<std::size_t>(j)]);
      const double a = 1.0 / (echo::k::kExpSlewS * kClickRates[ri]);
      std::printf("  REPORT contact at %g Hz: the read's speed changes by %.4f = %.4f a at most (contact bound 2 a = %.4f)\n", kClickRates[ri], c, c / a, 2.0 * a);
    }
  for (std::size_t ri = 0; ri < 4; ++ri) {
    const double a = 1.0 / (echo::k::kExpSlewS * kClickRates[ri]);
    double worst = 0, inFade = 0;
    for (std::size_t j = 0; j < 2 * nx; ++j) {
      worst = std::max(worst, res[ri * 2 * nx + j].worst);
      inFade = std::max(inFade, res[ri * 2 * nx + j].worstInFade);
    }
    if (g_report) std::printf("  REPORT outgoing bound at %g Hz: worst speed change a sample %.4f (inside fades %.4f), the bound %.4f\n", kClickRates[ri], worst, inFade, a);
    REQUIRE(worst <= a * (1.0 + 1e-6), "outgoing head at %g Hz: the speed changed by %.4f in one sample (bound %.4f): its clamp to the line steps it", kClickRates[ri], worst, a);
    REQUIRE(inFade >= 0.5 * a, "outgoing head at %g Hz: the worst step inside a fade %.4f is under half the bound %.4f: the cases no longer slew a fading head (vacuous)", kClickRates[ri], inFade, a);
    double c = 0;
    for (int j = 0; j < kPhases; ++j) c = std::max(c, contact[ri * kPhases + static_cast<std::size_t>(j)]);
    REQUIRE(c <= 2.0 * a * (1.0 + 1e-6), "the read at %g Hz at floor contact: its speed changed by %.4f = %.2f a in one sample (contact bound 2 a = %.4f for this gesture): the offset's slope bound or the outgoing head's clamp is broken", kClickRates[ri], c, c / a, 2.0 * a);
    REQUIRE(c >= 0.5 * a, "the read at %g Hz at floor contact: the worst step %.4f is under half the bound %.4f: the case no longer moves the read (vacuous)", kClickRates[ri], c, a);
  }
  return true;
}

// The catch-up gate (D-049 A4 and A5, ROADMAP T-041 criterion 4): after the tape law's cap releases, the head lands on
// the model, and on the target. M2 cannot judge HOW: a bound alone (the plant tapeNoBrake) keeps every step under the
// bar (M2 <= 1.06) while the head overshoots the model by up to 1.8 samples, rings, and dips to speed 0.5 (a wobble in
// pitch, not a click). So the gate reads the head against the model's own delay (Echo::tapeModelDelay) and against
// the target, by direction (D-049 A5, A6), in the 20 ms after the cap lets go, at 44.1 - 192 kHz:
//   shortening (held at +12 st): the speed never falls below 0.9, and the head passes neither the model nor the target
//     by more than 0.1 sample;
//   lengthening (held at -12 st, whose speed rises from 0.5 to 1: the mirror of "never falls below 0.9" is the only
//     form its release can satisfy): the speed never rises above 1.1, and the head passes the target by at most 0.1
//     sample. The model half is shortening-only: during a lengthening hold the model's read is pinned to the head, so
//     there is no model to pass (the target term of the catch-up is the brake there).
// READINGS are in the T-041 trace (traces/2026-10-30-t041-speed-steps.md) and in `--click-report`. A case that never
// reaches the cap would make the gate vacuous, so every capped case must first hold it for at least kCatchCapMin samples
// and must have made the catch-up act.
// Second half: on the model the law is untouched. A tape move that stays inside the cap must read 0 samples braked or
// bound (SpeedStats::catchUps), which is what "bit-identical to the unbraked law" means here: V is the law's own,
// sample for sample. The SPEC's 0.5 <-> 0.25 s moves are among them.
constexpr double kCatchSpeedMin = 0.9, kCatchSpeedMax = 1.1, kCatchOvershootMax = 0.1;
constexpr long kCatchCapMin = 50;
struct CatchCase { double from, to, glide; };
struct CatchRun { long capSamples = 0; double speed = 0, overshoot = 0; std::uint64_t catchUps = 0; };  // speed: the min (shortening) or max (lengthening) after the release
CatchRun catchUpRun(const CatchCase& c, double sr) {
  auto e = mk({{"time.mode", 0}, {"time.ms", c.from}, {"time.change", 0}, {"time.law", 0}, {"time.glide", c.glide}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100}}, sr);
  const long n = S(3.0, sr), at = S(1.0, sr);
  const Vec in(n, 0.1);  // not silent: a silent engine rests, and a resting head lands
  std::vector<double> D(static_cast<std::size_t>(n)), M(static_cast<std::size_t>(n), 0.0);
  runEach(*e, in, in, {set(at, "time.ms", c.to)}, [](long) { return 120.0; }, [&](long i, Echo& x) {
    D[static_cast<std::size_t>(i)] = x.headDelay(0);
    M[static_cast<std::size_t>(i)] = x.tapeModelDelay(0);
  });
  const bool shorten = c.to < c.from;
  const double T = c.to / 1000.0 * sr;
  // The cap holds the speed at exactly 2.0 (shortening) or 0.5 (lengthening); the longest such run is the one gated, and
  // the sample after it is the release.
  long run = 0, best = 0, end = -1;
  for (long i = at + 1; i < n; ++i) {
    const double sp = 1.0 - (D[static_cast<std::size_t>(i)] - D[static_cast<std::size_t>(i - 1)]);
    run = (shorten ? sp >= 2.0 - 1e-9 : sp <= 0.5 + 1e-9) ? run + 1 : 0;
    if (run > best) { best = run; end = i; }
  }
  CatchRun r;
  r.catchUps = e->speedStats(0).catchUps;
  r.capSamples = best;
  r.speed = best > 0 ? (shorten ? 1e300 : -1e300) : 0.0;  // no cap run: nothing to read (the gate then demands none of it)
  for (long i = end + 1; best > 0 && i <= std::min(n - 1, end + S(0.02, sr)); ++i) {
    const double sp = 1.0 - (D[static_cast<std::size_t>(i)] - D[static_cast<std::size_t>(i - 1)]);
    r.speed = shorten ? std::min(r.speed, sp) : std::max(r.speed, sp);
    const double Di = D[static_cast<std::size_t>(i)], Mi = M[static_cast<std::size_t>(i)];
    // ahead = past the target (D below it when shortening, above it when lengthening); a SHORTENING head is also gated
    // against the model. A lengthening head is not: while it lags the model the tape search only moves forward from the
    // head's own cursor, so the model's read is pinned to the head (tapeModelDelay = D + 1) and there is no model to
    // overshoot; the target is the only reference (D-049 A6).
    const double ahead = shorten ? std::max(Mi != 0.0 ? Mi - Di : 0.0, T - Di) : Di - T;
    r.overshoot = std::max(r.overshoot, ahead);
  }
  return r;
}
bool tapeCatchUp(Detail& d) {
  // 2 octave shortening moves at three glides, lengthening moves held at the lower cap (-12 st) to their landing; then
  // moves that stay inside the cap
  const CatchCase cases[] = {{500, 125, 0.3}, {500, 100, 0.05}, {500, 60, 0.01}, {125, 1000, 0.01}, {125, 500, 0.01}, {125, 750, 0.01},
                             {500, 250, 0.01}, {500, 250, 0.03}, {500, 250, 0.3}, {250, 500, 0.01}, {250, 500, 0.03}, {250, 500, 0.3}, {125, 500, 0.3}, {2, 1, 0.01}, {2, 1, 0.3}};
  constexpr std::size_t nc = sizeof cases / sizeof cases[0], nCapped = 6;
  std::vector<CatchRun> res(4 * nc);
  parallelFor(res.size(), [&](std::size_t i) { res[i] = catchUpRun(cases[i % nc], kClickRates[i / nc]); });
  if (g_report)  // every reading, before the first REQUIRE can return
    for (std::size_t ri = 0; ri < 4; ++ri)
      for (std::size_t k = 0; k < nc; ++k)
        std::printf("  REPORT catch-up %g -> %g ms glide %g, %g Hz: cap held %ld samples; speed after release %s %.3f; overshoot of the %s %.4f sample; %llu samples braked or bound\n",
                    cases[k].from, cases[k].to, cases[k].glide, kClickRates[ri], res[ri * nc + k].capSamples, cases[k].to < cases[k].from ? "min" : "max", res[ri * nc + k].speed,
                    cases[k].to < cases[k].from ? "model or target" : "target", res[ri * nc + k].overshoot, static_cast<unsigned long long>(res[ri * nc + k].catchUps));
  for (std::size_t ri = 0; ri < 4; ++ri)
    for (std::size_t k = 0; k < nc; ++k) {
      const CatchRun& r = res[ri * nc + k];
      const CatchCase& c = cases[k];
      if (k >= nCapped) {  // inside the cap the catch-up never acts: the law's own V, bit for bit (a braked V would differ in the last digits at least)
        REQUIRE(r.catchUps == 0, "tape move %g -> %g ms glide %g at %g Hz stays inside the cap, yet the catch-up changed the law's V on %llu samples", c.from, c.to, c.glide, kClickRates[ri],
                static_cast<unsigned long long>(r.catchUps));
        continue;
      }
      const bool shorten = c.to < c.from;
      REQUIRE(r.catchUps > 0, "catch-up %g -> %g ms glide %g at %g Hz: the catch-up never acted on a capped move (vacuous)", c.from, c.to, c.glide, kClickRates[ri]);
      REQUIRE(r.capSamples >= kCatchCapMin, "catch-up %g -> %g ms glide %g at %g Hz: the cap was held for %ld samples (< %ld): the case does not reach the cap, the gate is vacuous",
              c.from, c.to, c.glide, kClickRates[ri], r.capSamples, kCatchCapMin);
      REQUIRE(shorten ? r.speed >= kCatchSpeedMin : r.speed <= kCatchSpeedMax,
              "catch-up %g -> %g ms glide %g at %g Hz: after the cap released the speed %s to %.3f (limit %s %.1f): the head overshoots and rings", c.from, c.to, c.glide, kClickRates[ri],
              shorten ? "fell" : "rose", r.speed, shorten ? "min" : "max", shorten ? kCatchSpeedMin : kCatchSpeedMax);
      REQUIRE(r.overshoot <= kCatchOvershootMax, "catch-up %g -> %g ms glide %g at %g Hz: the head overshoots %s by %.3f sample (limit %.1f)", c.from, c.to, c.glide,
              kClickRates[ri], shorten ? "the model or the target" : "the target", r.overshoot, kCatchOvershootMax);
    }
  return true;
}

// SPEC §3.6: "the bound binds only where the law's rate would step" (an onset, a retarget, a time.glide change)
// and "never in a relaxation". The engine counts its binds and the sample of the last one; a render is cut at
// every event, and after each segment the last bind must lie within 0.15 ms (1.5 x k::kExpSlewS) of the segment's
// first sample (or there be none in it); before the first event there must be none. Across 44.1, 48, 96 and
// 192 kHz, glides 0.01 / 0.3 / 1.0 s, onsets down and up, a move reversed 20 and 100 ms in, 1 and 2 ms targets, a
// time.glide change mid-move, with and without 20 ms / 10 Hz modulation. Three planted controls, one per guard:
// expSlewLong (a 10 ms bound keeps binding for ~10 ms after an event: "binds in a relaxation"),
// expSpuriousRetarget (a move nobody asked for before the first event: "a bind before any move"), and
// expSlewOff (no bound, so no bind anywhere: "no run bound at all", the census's vacuity guard).
//
// The longest a bound of one speed unit per 0.1 ms can honestly bind after a move: the speed turning over from one
// cap to the other (s = 2 to s = 0.5) is 1.5 units = 0.15 ms (SPEC §3.6). A tighter limit of 0.1 ms flags a
// reversal that is simply doing its job (measured 0.104 ms at 48 kHz, glide 0.01, reversed 20 ms in).
constexpr double kCapToCap = 1.5;
struct BindScn { const char* what; double startMs; std::vector<std::tuple<double, const char*, double>> ev; };
struct BindJob { double sr, glide; bool mod; BindScn scn; };
struct BindSeg { long start, last; };  // a segment's first sample (-1: before any event) and the engine's last bind so far
struct BindRun { std::vector<BindSeg> segs; };

BindRun renderBinds(const BindJob& j) {
  auto e = mk({{"time.mode", 0}, {"time.ms", j.scn.startMs}, {"time.change", 0}, {"time.law", 1}, {"time.glide", j.glide}, {"fb.amount", 0}, {"filt.loop.on", 0}, {"mix", 100},
               {"mod.depthMs", j.mod ? 20 : 0}, {"mod.rateHz", 10}}, j.sr);
  const long n = S(1.6, j.sr);
  const Vec in = clickProbeAt(n, j.sr);
  Vec oL(512), oR(512);
  e->resetStats();
  BindRun r;
  long segStart = -1, pos = 0;
  for (std::size_t i = 0; i <= j.scn.ev.size(); ++i) {
    const long end = i < j.scn.ev.size() ? S(std::get<0>(j.scn.ev[i]), j.sr) : n;
    while (pos < end) {
      const int len = static_cast<int>(std::min<long>(512, end - pos));
      e->process(in.data() + pos, in.data() + pos, oL.data(), oR.data(), len, 120.0);
      pos += len;
    }
    r.segs.push_back({segStart, std::max(e->speedStats(0).lastSlewBind, e->speedStats(1).lastSlewBind)});
    if (i < j.scn.ev.size()) {
      e->setParam(std::get<1>(j.scn.ev[i]), std::get<2>(j.scn.ev[i]));
      segStart = end;
    }
  }
  return r;
}

bool expSlewBinds(Detail& d) {
  std::vector<BindJob> jobs;
  for (const double sr : {44100.0, 48000.0, 96000.0, 192000.0})
    for (const double glide : {0.01, 0.3, 1.0})
      for (const bool mod : {false, true}) {
        const double otherGlide = glide == 0.01 ? 0.3 : 0.01;
        for (const BindScn& sc : std::vector<BindScn>{
                 {"onset down 500 -> 125 ms", 500, {{0.3, "time.ms", 125}}},
                 {"onset up 125 -> 500 ms", 125, {{0.3, "time.ms", 500}}},
                 {"reversed 20 ms in", 500, {{0.3, "time.ms", 125}, {0.32, "time.ms", 500}}},
                 {"reversed 100 ms in, twice", 500, {{0.3, "time.ms", 125}, {0.4, "time.ms", 500}, {0.5, "time.ms", 125}}},
                 {"targets 2 ms and 1 ms, then back", 500, {{0.3, "time.ms", 2}, {0.34, "time.ms", 1}, {0.6, "time.ms", 500}}},
                 {"time.glide changed mid-move", 500, {{0.3, "time.ms", 125}, {0.34, "time.glide", otherGlide}, {0.4, "time.glide", glide}}},
             })
          jobs.push_back({sr, glide, mod, sc});
      }
  std::vector<BindRun> res(jobs.size());
  parallelFor(jobs.size(), [&](std::size_t i) { res[i] = renderBinds(jobs[i]); });
  double maxLateMs = 0;
  int binding = 0;
  for (std::size_t ji = 0; ji < jobs.size(); ++ji) {
    const BindJob& j = jobs[ji];
    for (const BindSeg& g : res[ji].segs) {
      const long late = g.last - g.start;
      if (g.start < 0) {
        REQUIRE(g.last < 0, "%s at %g Hz, glide %g, %s: a bind (sample %ld) before any move", j.scn.what, j.sr, j.glide, j.mod ? "modulated" : "unmodulated", g.last);
      } else if (g.last >= g.start) {
        maxLateMs = std::max(maxLateMs, 1000.0 * static_cast<double>(late) / j.sr);
        ++binding;
        REQUIRE(static_cast<double>(late) <= kCapToCap * echo::k::kExpSlewS * j.sr,
                "%s at %g Hz, glide %g, %s: the bound still binds %ld samples = %.3f ms after the last event (limit %.2f ms): it binds in a relaxation", j.scn.what, j.sr, j.glide,
                j.mod ? "modulated" : "unmodulated", late, 1000.0 * static_cast<double>(late) / j.sr, 1000.0 * kCapToCap * echo::k::kExpSlewS);
      }
    }
  }
  if (g_report) std::printf("  REPORT expSlewBinds: %zu runs, %d segments with a bind, the latest %.4f ms after its event (limit %.2f ms)\n", jobs.size(), binding, maxLateMs, 1000.0 * kCapToCap * echo::k::kExpSlewS);
  REQUIRE(binding > 0, "no run bound at all: the census sees nothing (vacuous)");
  return true;
}

// ---- M3 B4, T-044: time.mode, Time in Sec, Hz or Sync (SPEC §3.1, §3.4, §3.5; D-050) ----------------------

// An independent reading of SPEC §3, written here from the spec and not from the engine: the base time (time.ms / 1000
// in sec and hz, §3.1: Hz is only how time.ms is read; the division's beats at the host tempo in sync), x (1 + offset),
// then §3.4 (synced: halve both until the longer fits; free: 12 s; 1 ms below).
struct RefTimes { double tL, tR; bool folded; };
RefTimes refTimes(int mode, double ms, int div, double bpm, double offL, double offR) {
  const bool sync = mode == 2;
  const double base = sync ? echo::kDivisions[static_cast<std::size_t>(div)].beats * 60.0 / bpm : ms / 1000.0;
  RefTimes r{base * (1.0 + offL / 100.0), base * (1.0 + offR / 100.0), false};
  if (sync)
    while (std::max(r.tL, r.tR) > 12.0) { r.tL *= 0.5; r.tR *= 0.5; r.folded = true; }
  r.tL = std::clamp(r.tL, 0.001, 12.0);
  r.tR = std::clamp(r.tR, 0.001, 12.0);
  return r;
}

// SPEC §3.1 / §3.4 (D-050): the resolved times in all three modes against the independent reading above: the offsets,
// then the 12 s fold (synced) or clamp (free) and the 1 ms floor. Sec and hz read one time.ms; sync reads the division
// at the host tempo. Every case sets time.ms AND the division so that they differ, so a mode that reads the wrong one
// (the plant hzCountsAsSync plays the division in hz) resolves a different time. At 44.1 and 96 kHz.
bool timeResolution(Detail& d) {
  struct Cfg { int mode; double ms; int div; double bpm, offL, offR; const char* what; };
  static const char* const kModeName[3] = {"sec", "hz", "sync"};
  auto engineTimes = [](const Cfg& c, double sr) {
    auto e = mk({{"time.mode", static_cast<double>(c.mode)}, {"time.ms", c.ms}, {"time.div", static_cast<double>(c.div)}, {"time.offL", c.offL},
                 {"time.offR", c.offR}}, sr);
    const Vec z = zeros(64);
    run(*e, z, z, {}, c.bpm);
    return std::make_pair(e->resolvedTimes(), e->folded());
  };
  auto same = [](double a, double b) { return std::fabs(a - b) <= 1e-12 * std::max(1.0, std::fabs(b)); };
  std::vector<Cfg> cases;
  // 1. sec and hz read time.ms: the knob's Hz reading (Hz = 1000 / ms) over its range, 1000 Hz to 0.083 Hz, at a tempo and a division
  //    that give another time (1/8 at 97 bpm is 309.28 ms)
  for (const int mode : {0, 1})
    for (const double ms : {1.0, 2.0, 1000.0 / 440.0, 100.0, 1000.0 / 3.0, 375.0, 1000.0, 5000.0, 12000.0})
      cases.push_back({mode, ms, 12, 97, 0, 0, "time.ms with the offsets at zero"});
  // 2. offsets come first; the clamp and the floor come after them
  for (const int mode : {0, 1}) {
    cases.push_back({mode, 12000, 9, 120, 0, 50, "12 s, +50 % R (clamped to 12 s)"});
    cases.push_back({mode, 8000, 9, 120, -25, 40, "8 s, -25 / +40 % (the right line clamped)"});
    cases.push_back({mode, 1, 9, 120, -50, 0, "1 ms, -50 % L (floored at 1 ms)"});
    cases.push_back({mode, 100, 9, 120, 10, -20, "100 ms, +10 / -20 %"});
  }
  // 3. sync reads the division at the host tempo, every division, and folds past 12 s (never clamps)
  for (int div = 0; div < 22; ++div) cases.push_back({2, 123, div, 120, 0, 0, "every division at 120 bpm"});
  for (const double bpm : {30.0, 40.0, 61.0, 133.3, 240.0}) {
    cases.push_back({2, 123, 0, bpm, 0, 0, "2/1 across the fold's edge"});
    cases.push_back({2, 123, 3, bpm, 0, 50, "1/1, +50 % R"});
    cases.push_back({2, 123, 12, bpm, -25, 40, "1/8, -25 / +40 %"});
  }
  cases.push_back({2, 123, 0, 40, 0, 50, "2/1 at 40 bpm, +50 % R (18 s, halved once)"});
  for (double sr : {44100.0, 96000.0})
    for (const auto& c : cases) {
      const auto [t, folded] = engineTimes(c, sr);
      const RefTimes r = refTimes(c.mode, c.ms, c.div, c.bpm, c.offL, c.offR);
      REQUIRE(same(t[0], r.tL) && same(t[1], r.tR) && folded == r.folded,
              "%s, %s (time.ms %.9g, division %d, %g bpm, offsets %g / %g %%) at %.0f Hz: resolved %.9f / %.9f s%s, SPEC gives %.9f / %.9f s%s", kModeName[c.mode],
              c.what, c.ms, c.div, c.bpm, c.offL, c.offR, sr, t[0], t[1], folded ? " folded" : "", r.tL, r.tR, r.folded ? " folded" : "");
    }
  // By hand (the independent reading above could share a slip with the engine).
  {
    const auto [t, f] = engineTimes({0, 12000, 9, 120, 0, 50, ""}, 48000);
    REQUIRE(same(t[0], 12.0) && same(t[1], 12.0) && !f, "sec 12 s with +50 %% R plays %.6f / %.6f s, expected 12 / 12", t[0], t[1]);
    const auto [u, g] = engineTimes({2, 100, 0, 30, 0, 0, ""}, 48000);
    REQUIRE(same(u[0], 8.0) && g, "2/1 at 30 bpm (16 s) plays %.6f s, expected 8 (halved once)", u[0]);
    const auto [v, h] = engineTimes({0, 1, 9, 120, -50, 0, ""}, 48000);
    REQUIRE(v[0] == 0.001 && !h, "1 ms with -50 %% L (0.5 ms) must floor at 1 ms, resolved %.6f s", v[0]);
    const auto [w, k] = engineTimes({1, 1000, 12, 97, 0, 0, ""}, 48000);  // hz: 1 s is 1 Hz, whatever the division and the tempo
    REQUIRE(same(w[0], 1.0) && !k, "hz with time.ms 1000 (1 Hz) plays %.9f s, expected 1 (the division and the tempo are not read)", w[0]);
  }
  return true;
}

// SPEC §3.5 (D-050): a mode flip that moves a time is a control move. It follows time.change (repitch by the law,
// crossfade over time.xfadeMs, jump over 3 ms) and is latched like any time move. Proven by twins: the same move made
// through time.ms (from sec or hz) or time.div (from sync) to the same resulting time must be bit-identical, and a
// time.change flip while the move is in flight changes nothing. The sync -> sec / hz flips run twice: at a steady tempo,
// and after a tempo STEP, with a time.tempoMode that differs from the time.change under test. At a steady tempo the
// head's last move is never a tempo move, so a flip that never marks itself a control move (never clears the head's tempo
// flag: the plant modeFlipSwallowed) is invisible; after a tempo step the head is latched to tempoMode and only a real
// control move re-latches it to time.change.
bool modeFlipControlMove(Detail& d) {
  struct Mode { int change, law; const char* name; };
  // A flip, with the tempo it runs at: from / to are time.mode values. Into sync the flip lands on division 9 (1/4: 500 ms
  // at 120 bpm, 400 ms at 150) and the twin's time.ms move lands on the same instant; out of sync the flip lands on a
  // time.ms of that instant and the twin's time.div move on division 9. The start is division 12 (1/8) or time.ms 100.
  struct Flip { int from, to; bool stepped; const char* what; };
  const Flip flips[] = {{0, 2, false, "sec -> sync"},  {1, 2, false, "hz -> sync"},  {2, 1, false, "sync -> hz"},
                        {0, 2, true, "sec -> sync after a tempo step"},  {2, 1, true, "sync -> hz after a tempo step"},
                        {2, 0, true, "sync -> sec after a tempo step"}};
  const long n = S(2.0);
  for (const Flip& f : flips) {
    for (const Mode m : {Mode{0, 0, "repitch tape"}, Mode{0, 1, "repitch exponential"}, Mode{1, 0, "crossfade"}, Mode{2, 0, "jump"}}) {
      g_rng = 31 + static_cast<std::uint32_t>(m.change * 3 + m.law);
      Vec in = zeros(n);
      for (long i = 0; i < S(0.8); ++i) in[static_cast<std::size_t>(i)] = 0.4 * noise();
      // tempoMode: 0 crossfade, 1 glide. Never the behaviour time.change is set to (crossfade or jump under change 1 / 2, repitch under 0).
      const double tempoMode = m.change == 1 ? 1.0 : 0.0;
      const double landMs = f.stepped ? 400.0 : 500.0;  // division 9 at the tempo the flip runs at
      const bool fromSync = f.from == 2;
      auto make = [&] {
        return mk({{"time.mode", static_cast<double>(f.from)}, {"time.div", fromSync ? 12.0 : 9.0}, {"time.ms", fromSync ? landMs : 100.0},
                   {"time.change", static_cast<double>(m.change)}, {"time.law", static_cast<double>(m.law)}, {"time.tempoMode", tempoMode},
                   {"time.glide", 0.2}, {"fb.amount", 50}, {"mix", 100}});
      };
      const long at = f.stepped ? S(1.2) : S(0.9);
      auto bpmAt = [stepped = f.stepped](long i) { return stepped && i >= S(0.5) ? 150.0 : 120.0; };
      auto go = [&](Echo& e, std::vector<Event> ev) { return runEach(e, in, in, std::move(ev), bpmAt, [](long, Echo&) {}); };
      auto viaFlip = make(), viaKey = make(), flipped = make(), still = make();
      const auto oFlip = go(*viaFlip, {set(at, "time.mode", static_cast<double>(f.to))});
      // The same resulting time through a key that is already known to be a time move.
      const auto oKey = go(*viaKey, {fromSync ? set(at, "time.div", 9) : set(at, "time.ms", landMs)});
      const auto oChg = go(*flipped, {set(at, "time.mode", static_cast<double>(f.to)), set(at + S(0.02), "time.change", m.change == 2 ? 1.0 : 2.0)});
      const auto oStill = go(*still, {});
      for (long i = 0; i < n; ++i)
        REQUIRE(oFlip.L[i] == oKey.L[i] && oFlip.R[i] == oKey.R[i],
                "%s, %s: a time.mode flip differs from the same move through the time key at sample %ld (%.17g vs %.17g): it does not follow time.change",
                f.what, m.name, i, oFlip.L[i], oKey.L[i]);
      for (long i = 0; i < n; ++i)
        REQUIRE(oFlip.L[i] == oChg.L[i] && oFlip.R[i] == oChg.R[i],
                "%s, %s: flipping time.change 20 ms into a mode flip changed sample %ld: the move was not latched at its start", f.what, m.name, i);
      double moved = 0;
      for (long i = at; i < n; ++i) moved = std::max(moved, std::fabs(oFlip.L[i] - oStill.L[i]));
      REQUIRE(moved > 1e-3, "%s, %s: the case is vacuous: the flip leaves the render unchanged (%.3g)", f.what, m.name, moved);
    }
  }
  // A flip that lands on the time already playing moves nothing (SPEC §3.5), so in the sample a tempo step lands it must not re-latch the
  // tempo move to time.change: sec / hz -> sync with time.ms 500 = 1/4 at 120 bpm, the tempo stepping to 150 in that very sample, is
  // bit-identical to a patch synced all along. (The plant modeFlipAlwaysMove marks every flip a move.)
  for (const int from : {0, 1})
    for (const Mode m : {Mode{0, 0, "repitch tape"}, Mode{0, 1, "repitch exponential"}, Mode{1, 0, "crossfade"}, Mode{2, 0, "jump"}}) {
      g_rng = 91 + static_cast<std::uint32_t>(m.change * 3 + m.law);
      Vec in = zeros(n);
      for (long i = 0; i < S(0.8); ++i) in[static_cast<std::size_t>(i)] = 0.4 * noise();
      const double tempoMode = m.change == 1 ? 1.0 : 0.0;
      auto make = [&](int mode) {
        return mk({{"time.mode", static_cast<double>(mode)}, {"time.div", 9}, {"time.ms", 500}, {"time.change", static_cast<double>(m.change)},
                   {"time.law", static_cast<double>(m.law)}, {"time.tempoMode", tempoMode}, {"time.glide", 0.2}, {"fb.amount", 50}, {"mix", 100}});
      };
      const long at = S(0.9);
      auto bpmAt = [at](long i) { return i >= at ? 150.0 : 120.0; };
      auto go = [&](Echo& e, std::vector<Event> ev) { return runEach(e, in, in, std::move(ev), bpmAt, [](long, Echo&) {}); };
      auto flip = make(from), synced = make(2), steady = make(2);
      const auto oFlip = go(*flip, {set(at, "time.mode", 2)}), oSync = go(*synced, {});
      const auto oSteady = runEach(*steady, in, in, {}, [](long) { return 120.0; }, [](long, Echo&) {});
      for (long i = 0; i < n; ++i)
        REQUIRE(oFlip.L[i] == oSync.L[i] && oFlip.R[i] == oSync.R[i],
                "%s -> sync at the same time (500 ms) in the sample a tempo step lands, %s: differs from a patch synced all along at sample %ld (%.17g vs %.17g): the flip was counted as a move",
                from ? "hz" : "sec", m.name, i, oFlip.L[i], oSync.L[i]);
      double stepped = 0;
      for (long i = at; i < n; ++i) stepped = std::max(stepped, std::fabs(oSync.L[i] - oSteady.L[i]));
      REQUIRE(stepped > 1e-3, "%s: the case is vacuous: the tempo step leaves the render unchanged (%.3g)", m.name, stepped);
    }
  // The stale latch: a synced head latched to time.tempoMode by a tempo step keeps that latch through a same-time flip to sec (a
  // non-move) and through a tempo move nobody follows, so the flip back INTO sync is a real move that must follow time.change. Against
  // the twin that stays in sec and makes the same move through time.ms. sec never sets the tempo latch, so the row "sec -> sync after
  // a tempo step" above cannot see a flip into sync that goes unmarked (the plant modeFlipIntoSyncSwallowed); this one can.
  for (const Mode m : {Mode{0, 0, "repitch tape"}, Mode{0, 1, "repitch exponential"}, Mode{1, 0, "crossfade"}, Mode{2, 0, "jump"}}) {
    g_rng = 111 + static_cast<std::uint32_t>(m.change * 3 + m.law);
    const long nn = S(2.6);
    Vec in = zeros(nn);
    for (long i = 0; i < S(0.8); ++i) in[static_cast<std::size_t>(i)] = 0.4 * noise();
    const double tempoMode = m.change == 1 ? 1.0 : 0.0;
    auto make = [&] {
      return mk({{"time.mode", 2}, {"time.div", 9}, {"time.ms", 400}, {"time.change", static_cast<double>(m.change)}, {"time.law", static_cast<double>(m.law)},
                 {"time.tempoMode", tempoMode}, {"time.glide", 0.2}, {"fb.amount", 50}, {"mix", 100}});
    };
    // 120 -> 150 bpm at 0.5 s (1/4 = 400 ms), the flip to sec at 1.2 s lands on that same 400 ms, the tempo falls to 100 bpm at 1.5 s
    // (unheard in sec), and at 1.8 s the flip into sync lands on 1/4 at 100 bpm = 600 ms.
    auto bpmAt = [](long i) { return i < S(0.5) ? 120.0 : i < S(1.5) ? 150.0 : 100.0; };
    auto go = [&](Echo& e, std::vector<Event> ev) { return runEach(e, in, in, std::move(ev), bpmAt, [](long, Echo&) {}); };
    auto viaFlip = make(), viaKey = make(), still = make();
    const auto oFlip = go(*viaFlip, {set(S(1.2), "time.mode", 0), set(S(1.8), "time.mode", 2)});
    const auto oKey = go(*viaKey, {set(S(1.2), "time.mode", 0), set(S(1.8), "time.ms", 600)});
    const auto oStill = go(*still, {set(S(1.2), "time.mode", 0)});
    for (long i = 0; i < nn; ++i)
      REQUIRE(oFlip.L[i] == oKey.L[i] && oFlip.R[i] == oKey.R[i],
              "stale latch, %s: the flip into sync after a tempo step, a same-time flip to sec and a tempo move differs from the same move through time.ms at sample %ld (%.17g vs %.17g): it did not follow time.change",
              m.name, i, oFlip.L[i], oKey.L[i]);
    double moved = 0;
    for (long i = S(1.8); i < nn; ++i) moved = std::max(moved, std::fabs(oFlip.L[i] - oStill.L[i]));
    REQUIRE(moved > 1e-3, "stale latch, %s: the case is vacuous: the flip into sync leaves the render unchanged (%.3g)", m.name, moved);
  }
  return true;
}

// SPEC §3.5 (D-050): a key send that moves no time is not a move. time.ms while synced is stored, not read; a re-sent value, and a
// division or an offset that lands on the time already playing, move nothing. A move landing in the same sample (a tempo step) must
// keep its time.tempoMode latch instead of following time.change. (1) In the sample a tempo step lands, on a synced patch under time.change
// jump and each time.tempoMode: time.ms moved or re-sent, time.div and time.offL re-sent, each bit-identical to no send. (2) The realistic
// host: 512-sample blocks, a tempo ramp, time.ms automated every block: bit-identical to no automation, at every time.tempoMode.
// (The plant keyResendIsMove restores the unconditional mark.)
bool resendIsNoMove(Detail& d) {
  struct Send { const char* what; const char* key; double v; };
  const Send sends[] = {{"time.ms moved (100 -> 250)", "time.ms", 250}, {"time.ms re-sent (100)", "time.ms", 100}, {"time.div re-sent (1/4)", "time.div", 9},
                        {"time.offL re-sent (0)", "time.offL", 0}, {"time.offR re-sent (0)", "time.offR", 0}};
  const long n = S(2.0), at = S(1.0);
  for (int tempoMode = 0; tempoMode < 3; ++tempoMode) {
    g_rng = 121 + static_cast<std::uint32_t>(tempoMode);
    Vec in = zeros(n);
    for (long i = 0; i < S(0.8); ++i) in[static_cast<std::size_t>(i)] = 0.4 * noise();
    auto make = [&] {
      return mk({{"time.mode", 2}, {"time.div", 9}, {"time.ms", 100}, {"time.change", 2}, {"time.tempoMode", static_cast<double>(tempoMode)},
                 {"time.glide", 0.2}, {"fb.amount", 50}, {"mix", 100}});
    };
    auto bpmAt = [at](long i) { return i >= at ? 150.0 : 120.0; };
    auto go = [&](Echo& e, std::vector<Event> ev) { return runEach(e, in, in, std::move(ev), bpmAt, [](long, Echo&) {}); };
    auto base = make(), steady = make();
    const auto o0 = go(*base, {});
    const auto oSteady = runEach(*steady, in, in, {}, [](long) { return 120.0; }, [](long, Echo&) {});
    double stepped = 0;
    for (long i = at; i < n; ++i) stepped = std::max(stepped, std::fabs(o0.L[i] - oSteady.L[i]));
    REQUIRE(stepped > 1e-3, "tempoMode %d: the case is vacuous: the tempo step leaves the render unchanged (%.3g)", tempoMode, stepped);
    for (const Send& sd : sends) {
      auto e = make();
      const auto o = go(*e, {set(at, sd.key, sd.v)});
      for (long i = 0; i < n; ++i)
        REQUIRE(o.L[i] == o0.L[i] && o.R[i] == o0.R[i],
                "tempoMode %d, %s in the sample a tempo step lands: differs from no send at sample %ld (%.17g vs %.17g): the send was counted as a move and the tempo move followed time.change",
                tempoMode, sd.what, i, o.L[i], o0.L[i]);
    }
    // (2) the host: blocks of 512, the tempo ramping 120 -> 150 bpm over 0.5 - 1.5 s, time.ms automated at every block start
    auto host = [&](bool automate) {
      auto e = make();
      Out o{zeros(n), zeros(n)};
      for (long pos = 0, blk = 0; pos < n; pos += 512, ++blk) {
        const long len = std::min<long>(512, n - pos);
        if (automate) e->setParam("time.ms", 100.0 + 50.0 * std::sin(static_cast<double>(blk)));
        const double t = static_cast<double>(pos) / kSr, bpm = 120.0 + 30.0 * std::clamp((t - 0.5) / 1.0, 0.0, 1.0);
        const auto u = static_cast<std::size_t>(pos);
        e->process(in.data() + u, in.data() + u, o.L.data() + u, o.R.data() + u, static_cast<int>(len), bpm);
      }
      return o;
    };
    const Out h0 = host(false), h1 = host(true);
    for (long i = 0; i < n; ++i)
      REQUIRE(h1.L[i] == h0.L[i] && h1.R[i] == h0.R[i],
              "tempoMode %d: time.ms automated every 512-sample block on a synced patch under a tempo ramp differs from no automation at sample %ld (%.17g vs %.17g)",
              tempoMode, i, h1.L[i], h0.L[i]);
  }
  // (3) and the other side of it: an offset that DOES move a time is a move, whichever line it moves. After a tempo step the heads are
  // latched to the tempo's mode (glide: no fade), so a move that goes unmarked keeps gliding, where a marked one follows time.change
  // (jump: one fade on the line that moved).
  for (const int line : {0, 1}) {
    auto e = mk({{"time.mode", 2}, {"time.div", 9}, {"time.change", 2}, {"time.tempoMode", 1}, {"time.glide", 0.2}, {"fb.amount", 0}, {"mix", 100}});
    g_rng = 131;
    Vec in = zeros(S(2.0));
    for (auto& x : in) x = 0.4 * noise();  // sounding throughout: an instance at rest lands its heads, and nothing fades
    (void)runEach(*e, in, in, {set(S(1.5), line == 0 ? "time.offL" : "time.offR", 10)}, [](long i) { return i >= S(1.0) ? 150.0 : 120.0; }, [](long, Echo&) {});
    const auto moved = e->fadeCount(line), other = e->fadeCount(1 - line);
    REQUIRE(moved == 1 && other == 0, "a move of the offset on line %d after a tempo step fades %llu times on that line and %llu on the other, expected 1 and 0: it did not follow time.change",
            line, static_cast<unsigned long long>(moved), static_cast<unsigned long long>(other));
  }
  return true;
}

// SPEC §3.1, §3.5 (D-050): sec and hz are two readings of one time.ms, so a flip between them moves nothing: a run that
// flips sec -> hz -> sec every 2500 samples is bit-identical, on both channels, to a run that never flips. Each routing, each
// time.change (and both repitch laws), steady and moving: the move starts a glide / fade, and 20 ms in time.change is flipped
// to ANOTHER mode (deferred, §3.5: it must change nothing for the move in flight), then the mode flips: a flip that re-latched
// the move in flight (the plant hzFlipIsMove) would start it again under the new time.change. Modulation runs throughout, and
// the host tempo sweeps, which neither sec nor hz follows (a patch set to hz from the start equals the same patch in sec, too).
bool timeModeFlip(Detail& d) {
  struct Mode { int change, law; const char* name; };
  const long n = S(1.6);
  Vec inL(static_cast<std::size_t>(n), 0.0), inR = inL;
  for (const Mode m : {Mode{0, 0, "repitch tape"}, Mode{0, 1, "repitch exponential"}, Mode{1, 0, "crossfade"}, Mode{2, 0, "jump"}})
    for (int route = 0; route < 3; ++route)
      for (int moving = 0; moving < 2; ++moving) {
        g_rng = 71 + static_cast<std::uint32_t>(route * 7 + m.change * 3 + m.law);
        for (long i = 0; i < S(0.3); ++i) { inL[static_cast<std::size_t>(i)] = 0.4 * noise(); inR[static_cast<std::size_t>(i)] = 0.4 * noise(); }
        auto bpmAt = [](long i) { return 120.0 + 30.0 * std::sin(2 * kPi * static_cast<double>(i) / kSr); };
        auto render = [&](int variant) {  // 0: never flips, 1: flips sec <-> hz, 2: hz throughout
          auto e = mk({{"route.mode", static_cast<double>(route)}, {"time.mode", variant == 2 ? 1.0 : 0.0}, {"time.change", static_cast<double>(m.change)},
                       {"time.law", static_cast<double>(m.law)}, {"time.ms", 400}, {"time.div", 12}, {"fb.amount", 60}, {"mix", 60}, {"time.offR", 12},
                       {"time.glide", 0.15}, {"mod.depthMs", 3}, {"mod.rateHz", 1.7}, {"mod.shape", 1}});
          std::vector<Event> ev;
          if (moving) {
            ev.push_back(set(S(0.4), "time.ms", 130));
            ev.push_back(set(S(0.4) + S(0.02), "time.change", m.change == 2 ? 1.0 : m.change + 1.0));
          }
          if (variant == 1) {
            double mode = 1.0;
            for (long i = 1000; i < n - 1; i += 2500, mode = 1.0 - mode) ev.push_back(set(i, "time.mode", mode));
            ev.push_back(set(S(0.4) + S(0.02) + 1, "time.mode", 1.0));  // inside the move, right after the time.change flip
            ev.push_back(set(S(0.4) + S(0.02) + 2, "time.mode", 0.0));
            ev.push_back(set(S(0.4) + 50, "time.mode", 1.0));  // and inside the first 3 ms of a jump's fade
            ev.push_back(set(S(0.4) + 100, "time.mode", 0.0));
          }
          return runEach(*e, inL, inR, ev, bpmAt, [](long, Echo&) {});
        };
        const Out base = render(0), flip = render(1), hz = render(2);
        REQUIRE(maxAbs(base.L, S(0.4)) > 0.01, "the case is vacuous: no output (route %d, %s)", route, m.name);
        for (long i = 0; i < n; ++i) {
          REQUIRE(flip.L[i] == base.L[i] && flip.R[i] == base.R[i],
                  "route %d, %s, %s: the sec <-> hz flips changed sample %ld (%.17g vs %.17g without them): a flip between sec and hz moved something", route,
                  m.name, moving ? "moving" : "steady", i, flip.L[i], base.L[i]);
          REQUIRE(hz.L[i] == base.L[i] && hz.R[i] == base.R[i],
                  "route %d, %s, %s: hz differs from sec at sample %ld (%.17g vs %.17g): hz is not reading time.ms alone, or follows the tempo", route, m.name,
                  moving ? "moving" : "steady", i, hz.L[i], base.L[i]);
        }
      }
  return true;
}

// The resolved times are main's expression for the offset, bit for bit, with no libm in the argument (critic T-038 L2:
// a rewrite of base x (1 + offset / 100) as base + base x offset / 100 can round another way at a nonzero offset (at 0 both are exactly base), and
// nothing else would see it; kept after the scale key left, D-050). offsetTimes is resolveBoth as it was before the scale key (312d70e) written out: base, then
// base x (1 + offset / 100), the synced halving past 12 s, the clamp to [1 ms, 12 s], then x sr and back / sr as
// resolvedTimes() reads it. A grid of every division, tempi (40 bpm puts 2/1 on the fold's edge, 30 past it), offsets
// and free times (the 12 s clamp and the 1 ms floor included), in each of the three modes, at 44.1, 48 and 96 kHz.
std::array<double, 2> offsetTimes(bool sync, double ms, int div, double bpm, double offL, double offR, double sr) {
  const double b = (bpm > 1.0) ? bpm : 120.0;
  const double base = sync ? echo::kDivisions[static_cast<std::size_t>(div)].beats * 60.0 / b : ms / 1000.0;
  double tL = base * (1.0 + offL / 100.0), tR = base * (1.0 + offR / 100.0);
  if (sync)
    while (std::max(tL, tR) > 12.0) { tL *= 0.5; tR *= 0.5; }
  const double cL = std::clamp(tL, 0.001, 12.0) * sr, cR = std::clamp(tR, 0.001, 12.0) * sr;
  return {cL / sr, cR / sr};
}
bool offsetExpressionBits(Detail& d) {
  const double bpms[] = {30, 40, 60, 90, 120, 133.3, 240};
  const double offs[] = {-50, -33.3, 0, 12, 50};
  const double mss[] = {1, 2, 17.5, 375, 5000, 12000};
  long cases = 0;
  for (double sr : {44100.0, 48000.0, 96000.0}) {
    auto e = mk({}, sr);
    const Vec z = zeros(1);
    double o[2];
    for (int mode = 0; mode < 3; ++mode) {
      const bool sync = mode == 2;
      for (int div = 0; div < (sync ? 22 : 1); ++div)
        for (double ms : mss) {
          if (sync && ms != mss[0]) continue;
          for (double bpm : bpms)
            for (double oL : offs)
              for (double oR : offs) {
                if (!sync && bpm != bpms[0]) continue;  // free time ignores the tempo
                e->setParam("time.mode", mode);
                e->setParam("time.div", div);
                e->setParam("time.ms", ms);
                e->setParam("time.offL", oL);
                e->setParam("time.offR", oR);
                e->process(z.data(), z.data(), &o[0], &o[1], 1, bpm);  // at rest: the tempo lands, the times resolve
                const auto got = e->resolvedTimes();
                const auto want = offsetTimes(sync, ms, div, bpm, oL, oR, sr);
                ++cases;
                REQUIRE(got[0] == want[0] && got[1] == want[1],
                        "%s at %.0f Hz (div %d, %g ms, %g bpm, offsets %g / %g %%): resolves %.17g / %.17g s, the offset expression gives %.17g / %.17g",
                        sync ? "synced" : mode == 1 ? "hz" : "sec", sr, div, ms, bpm, oL, oR, got[0], got[1], want[0], want[1]);
              }
        }
    }
  }
  REQUIRE(cases > 10000, "the grid is vacuous: %ld cases", cases);
  return true;
}

// D-050 (replaces T-038's scaleTail): the declared tail, the rest gate and the offline renderer in each mode. (a) A sec patch and the
// same patch in hz declare the identical tail, and a sync patch declares that of a sec twin with the same resulting time (1/4 at 120 bpm
// is 500 ms); each sounds until its tail, is at exact zero from it, and rests. The division is set to something else in sec and hz, so a
// tail built from the wrong time (the plant tailReadsDivInHz) differs. (b) A sec <-> hz flip while the tail rings changes nothing: the
// output is bit-identical to no flips and the rest comes at the same sample. Its control is hzFlipDelaysRest (the flip restarts the quiet
// count). A flip restarting the tail clock instead changes nothing observable: the clock is a backstop the quiet gate preempts in every
// ordinary case (measured: rest at 68 % of the declared tail).
// (c) The offline renderer renders hz as sec and sync at its division's time, and reports the matching times and tail.
bool timeModesTail(Detail& d) {
  struct Cfg { const char* what; int mode; double ms; int div; };
  const Cfg secP{"sec", 0, 100, 9}, hzP{"hz", 1, 100, 9}, syncP{"sync", 2, 375, 9}, twinP{"sec twin of 1/4 at 120 bpm", 0, 500, 12};
  for (const double sr : {44100.0, 96000.0}) {
    auto build = [&](const Cfg& c) {
      return mk({{"time.mode", static_cast<double>(c.mode)}, {"time.ms", c.ms}, {"time.div", static_cast<double>(c.div)}, {"fb.amount", 40}, {"mix", 100}}, sr);
    };
    // (a)
    for (const auto& [x, y] : {std::pair<Cfg, Cfg>{secP, hzP}, std::pair<Cfg, Cfg>{syncP, twinP}}) {
      auto ex = build(x), ey = build(y);
      REQUIRE(ex->tail() == ey->tail(), "%s declares a tail of %lld samples, %s declares %lld, at %.0f Hz", x.what, static_cast<long long>(ex->tail()), y.what,
              static_cast<long long>(ey->tail()), sr);
      const long T = static_cast<long>(ex->tail());
      const auto ox = run(*ex, impulse(T + S(0.5, sr)), zeros(T + S(0.5, sr))), oy = run(*ey, impulse(T + S(0.5, sr)), zeros(T + S(0.5, sr)));
      REQUIRE(maxAbs(ox.L, 0, T) > 1e-3, "%s at %.0f Hz: the case is vacuous: nothing sounds", x.what, sr);
      REQUIRE(maxAbs(ox.L, T + 1) == 0.0 && maxAbs(ox.R, T + 1) == 0.0 && ex->atRest(),
              "%s at %.0f Hz: after the declared tail (%ld samples) the output is %.3g, at rest %d", x.what, sr, T, maxAbs(ox.L, T + 1), ex->atRest() ? 1 : 0);
      for (long i = 0; i < static_cast<long>(ox.L.size()); ++i)
        REQUIRE(ox.L[i] == oy.L[i] && ox.R[i] == oy.R[i], "%s and %s render differently at sample %ld at %.0f Hz", x.what, y.what, i, sr);
    }
    // (b)
    {
      const long T = static_cast<long>(build(secP)->tail());
      const long n = T + S(0.5, sr);
      auto render = [&](bool flips, long& restAt) {
        auto e = build(secP);
        std::vector<Event> ev;
        double mode = 1.0;
        if (flips)
          for (long i = S(0.2, sr); i < T - S(0.05, sr); i += S(0.37, sr), mode = 1.0 - mode) ev.push_back(set(i, "time.mode", mode));
        restAt = -1;
        return runEach(*e, impulse(n), zeros(n), ev, [](long) { return 120.0; }, [&restAt](long i, Echo& en) { if (restAt < 0 && en.atRest()) restAt = i; });
      };
      long r0 = -1, r1 = -1;
      const Out base = render(false, r0), flip = render(true, r1);
      REQUIRE(r0 > 0 && r0 <= T + 2, "sec at %.0f Hz: the rest came at sample %ld, the declared tail is %ld", sr, r0, T);
      for (long i = 0; i < n; ++i)
        REQUIRE(flip.L[i] == base.L[i] && flip.R[i] == base.R[i], "at %.0f Hz the sec <-> hz flips changed sample %ld of a ringing tail (%.17g vs %.17g)", sr, i, flip.L[i], base.L[i]);
      REQUIRE(r1 == r0, "at %.0f Hz the sec <-> hz flips moved the rest from sample %ld to %ld: a flip restarted the tail clock", sr, r0, r1);
    }
    // (c)
    {
      auto request = [&](const Cfg& c) {
        echo::IrRequest r;
        r.sampleRate = sr;
        r.patch[echo::kTimeMode] = c.mode;
        r.patch[echo::kTimeMs] = c.ms;
        r.patch[echo::kDiv] = c.div;
        r.patch[echo::kFb] = 40;
        r.length = S(10.0, sr);  // the declared tails here are under 9 s
        return echo::renderIr(r);
      };
      const auto a = request(secP), b = request(hzP), c = request(syncP), t = request(twinP);
      REQUIRE(a.ok && b.ok && c.ok && t.ok, "the offline render of a time.mode patch was refused at %.0f Hz", sr);
      REQUIRE(b.info.tail == a.info.tail && b.info.times == a.info.times && b.L == a.L && b.R == a.R,
              "the offline render of hz differs from sec at %.0f Hz: times %.9f / %.9f s vs %.9f s, tail %lld vs %lld", sr, b.info.times[0], b.info.times[1],
              a.info.times[0], static_cast<long long>(b.info.tail), static_cast<long long>(a.info.tail));
      REQUIRE(std::fabs(c.info.times[0] - 0.5) < 1e-12 && c.info.tail == t.info.tail && c.L == t.L,
              "the offline render of sync (1/4 at 120 bpm) reports times %.9f s and tail %lld at %.0f Hz (expected 0.5 s and %lld)", c.info.times[0],
              static_cast<long long>(c.info.tail), sr, static_cast<long long>(t.info.tail));
      REQUIRE(a.info.restAt >= 0 && maxAbs(a.L, a.info.restAt) == 0.0 && c.info.restAt >= 0 && maxAbs(c.L, c.info.restAt) == 0.0,
              "an offline render of a time.mode patch never rests at %.0f Hz (restAt %lld / %lld)", sr, static_cast<long long>(a.info.restAt), static_cast<long long>(c.info.restAt));
    }
  }
  return true;
}

// D-050: a patch in each of the three modes is deterministic, block-size independent and sample-rate independent. All three
// patches play 100 ms: time.ms 100 in sec and hz (10 Hz), 1/16 at 150 bpm in sync, and the key the mode does not read is set
// to something else (so a mode that reads the wrong one, the plant hzCountsAsSync, lands elsewhere).
bool timeModesIndependence(Detail& d) {
  struct M { int mode; const char* name; };
  const double bpm = 150.0;
  for (const M m : {M{0, "sec"}, M{1, "hz"}, M{2, "sync"}}) {
    const double ms = m.mode == 2 ? 250.0 : 100.0, div = m.mode == 2 ? 15.0 : 12.0;  // 1/16 = 100 ms, 1/8 = 200 ms at 150 bpm
    // 1. determinism: two instances driven alternately each equal a solo render
    g_rng = 17 + static_cast<std::uint32_t>(m.mode);
    const long n = S(1.2);
    Vec in(static_cast<std::size_t>(n));
    for (auto& v : in) v = 0.3 * noise();
    auto patch = [&] {
      return mk({{"time.mode", static_cast<double>(m.mode)}, {"time.ms", ms}, {"time.div", div}, {"fb.amount", 60}, {"mix", 70}, {"mod.depthMs", 2},
                 {"mod.shape", 1}, {"mod.seed", 9}});
    };
    auto solo = patch(), a = patch(), b = patch();
    const auto os = run(*solo, in, in, {}, bpm);
    Out oa{zeros(n), zeros(n)}, ob = oa;
    for (long p = 0; p < n; p += 256) {
      const int len = static_cast<int>(std::min<long>(256, n - p));
      a->process(in.data() + p, in.data() + p, oa.L.data() + p, oa.R.data() + p, len, bpm);
      b->process(in.data() + p, in.data() + p, ob.L.data() + p, ob.R.data() + p, len, bpm);
    }
    for (long i = 0; i < n; ++i)
      REQUIRE(oa.L[i] == os.L[i] && ob.L[i] == os.L[i] && oa.R[i] == os.R[i] && ob.R[i] == os.R[i], "%s: an interleaved instance differs from solo at %ld", m.name, i);
    // reset identity (A2): the instance that just played, reset(), renders what a freshly built one does after its own reset() (as
    // a2ResetIdentity: the reset lands the ramps a fresh instance would still be gliding, the modulation depth's 250 ms among them)
    solo->reset();
    auto fresh = patch();
    fresh->reset();
    const auto orr = run(*solo, in, in, {}, bpm), orf = run(*fresh, in, in, {}, bpm);
    for (long i = 0; i < n; ++i)
      REQUIRE(orr.L[i] == orf.L[i] && orr.R[i] == orf.R[i], "%s: reset() differs from a fresh instance at %ld", m.name, i);
    // 2. block independence: a time move, then a time.change flip and a second move, under every block pattern
    auto evs = [&] {
      return m.mode == 2 ? std::vector<Event>{set(S(0.3), "time.div", 12), set(S(0.5), "time.change", 2), set(S(0.55), "time.div", 9)}
                         : std::vector<Event>{set(S(0.3), "time.ms", 250), set(S(0.5), "time.change", 2), set(S(0.55), "time.ms", 130)};
    };
    auto ref = patch();
    const auto o0 = run(*ref, in, in, evs(), bpm, [](long) { return 4096; });
    std::uint32_t seed = 5;
    const std::vector<std::function<int(long)>> pats = {
        [](long) { return 1; }, [](long) { return 7; }, [](long) { return 333; },
        [&seed](long) { seed = seed * 1664525u + 1013904223u; return 1 + static_cast<int>(seed % 4096u); }};
    for (std::size_t pi = 0; pi < pats.size(); ++pi) {
      auto e = patch();
      const auto o = run(*e, in, in, evs(), bpm, pats[pi]);
      for (long i = 0; i < n; ++i)
        REQUIRE(o.L[i] == o0.L[i] && o.R[i] == o0.R[i], "%s: block pattern %zu differs at sample %ld", m.name, pi, i);
    }
    // 3. sample-rate independence: the one repeat of an impulse lands at 100 ms whatever the rate
    for (const double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
      auto e = mk({{"time.mode", static_cast<double>(m.mode)}, {"time.ms", ms}, {"time.div", div}, {"fb.amount", 0}, {"mix", 100}, {"filt.loop.on", 0}}, sr);
      const long len = S(0.3, sr);
      const auto o = run(*e, impulse(len), zeros(len), {}, bpm);
      long at = 0;
      for (long i = 0; i < len; ++i)
        if (std::fabs(o.L[i]) > std::fabs(o.L[at])) at = i;
      REQUIRE(at == S(0.1, sr) && o.L[at] > 0.9, "%s at %.0f Hz: the repeat lands at sample %ld (%.6f s), expected %ld (0.1 s)", m.name, sr, at,
              static_cast<double>(at) / sr, S(0.1, sr));
    }
  }
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
    auto e = mk({{"time.mode", 0}, {"time.ms", 250}, {"fb.amount", 50}}, sr);
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
    auto r = mk({{"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 80}, {"mix", 100}}, sr);
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
    auto e = mk({{"time.mode", 0}, {"time.ms", 12000}, {"fb.amount", 99.99}}, 192000.0);
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
      auto a = mk({{"route.mode", static_cast<double>(route)}, {"time.mode", 0}, {"time.ms", 40}, {"fb.amount", 60}, {"mix", 50}});
      auto b = mk({{"route.mode", static_cast<double>(route)}, {"time.mode", 0}, {"time.ms", 40}, {"fb.amount", 60}, {"mix", 50}});
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
  auto base = mk({{fb, 90}, {"mix", 50}, {"time.mode", 0}, {"time.ms", 50}, {"filt.loop.on", 0}});
  auto inG = mk({{fb, 90}, {"mix", 50}, {"time.mode", 0}, {"time.ms", 50}, {"filt.loop.on", 0}});
  auto outG = mk({{fb, 90}, {"mix", 50}, {"time.mode", 0}, {"time.ms", 50}, {"filt.loop.on", 0}});
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
  auto pre12 = mk({{fb, 90}, {"mix", 50}, {"time.mode", 0}, {"time.ms", 50}, {"filt.loop.on", 0}});
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
  auto w = mk({{"mix", 100}, {"time.mode", 0}, {"time.ms", 10}, {"fb.amount", 50}});
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
  for (const char* want : {"\ntime.mode sync\n", "\ntime.div 1/4\n", "\nroute.mode standard\n", "\ntime.ms 375\n", "\nmix 30\n", "\nmod.seed 1\n",
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

// RS-6.11, SPEC §3.1 / §16.2 (D-050): a saved time.ms reads back bit for bit. The knob's Hz reading sets time.ms from
// 1000 / Hz, a double that is almost never a short decimal (1000 / 3 is 333.33333333333331), so a writer that rounds it
// (the plant stateFloat6g writes %.6g) would move a saved Time on every reload. Values a Hz reading produces, the range's
// ends and the double after 1 ms, in sec and in hz, through save -> load -> save and into a fresh instance.
bool stateTimeExact(Detail& d) {
  const double vs[] = {1000.0 / 3.0, 1000.0 / 0.0834, 1000.0 / 440.0, 1000.0 / 7.0, 1.0000000000000002, 11999.999999999998, 12000.0, 1.0, 375.0};
  for (const int mode : {0, 1})
    for (const double v : vs) {
      auto a = mk({{"time.mode", static_cast<double>(mode)}});
      REQUIRE(a->setParam("time.ms", v), "time.ms %.17g was refused", v);
      const std::string t1 = saveText(*a);
      auto b = mk({});
      const char* why = "";
      REQUIRE(restore(*b, t1, &why), "time.ms %.17g: its own state was refused (%s)", v, why);
      REQUIRE(b->getParam(echo::kTimeMs) == v, "time.ms %.17g in %s read back as %.17g: the saved text rounds the time", v, mode ? "hz" : "sec", b->getParam(echo::kTimeMs));
      REQUIRE(saveText(*b) == t1, "time.ms %.17g: save -> load -> save differs", v);
    }
  return true;
}

// A17: a state saved mid-session, loaded into a fresh instance, renders
// bit-identically to the original after its reset(): nothing outside the saved
// state and reset() shapes the sound.
bool stateComplete(Detail& d) {
  g_rng = 5;
  auto a = mk({{"time.mode", 0}, {"time.ms", 80}, {"fb.amount", 70}, {"mod.depthMs", 2}, {"mod.shape", 1}, {"mod.seed", 77}});
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
  auto x = mk({{"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 80}, {"mix", 50}});
  run(*x, impulse(S(0.3), 0, 1.5), zeros(S(0.3)));  // ringing, and an over at the input tap
  x->setParam("io.outGain", 6);
  x->setParam("mix", 60);
  block(*x, zeros(100), zeros(100));  // both 20 ms ramps mid-flight
  REQUIRE(x->inputMeter().clip && !x->atRest(), "the case is vacuous: no latch, or already at rest");
  auto y = mk({{"time.mode", 0}, {"time.ms", 100}, {"fb.amount", 80}, {"mix", 60}, {"io.outGain", 6}});
  REQUIRE(saveText(*x) == saveText(*y), "the state saved while live differs from one saved at rest");
  return true;
}

// Undo (RS-6.14 via horde's history): for every one of the 29 keys, restoring a
// snapshot taken before the key moved gives the byte-identical state and a
// render bit-identical to a twin that only loaded the snapshot.
bool stateUndo(Detail& d) {
  for (int i = 0; i < echo::kNumParams; ++i) {
    const auto& p = echo::kParams[static_cast<std::size_t>(i)];
    auto a = mk({{"time.mode", 0}, {"time.ms", 90}, {"fb.amount", 60}, {"mod.depthMs", 1}});
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
  auto a = mk({{"time.mode", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 50}});
  auto b = mk({{"time.mode", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 50}});
  auto target = mk({{"time.mode", 0}, {"time.ms", 200}, {"fb.amount", 30}, {"mix", 70}, {"io.outGain", -6}});
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
      {"a bad label", replace("route.mode standard\n", "route.mode sideways\n")},
      {"the retired label stereo (route.mode's first label is standard)", replace("route.mode standard\n", "route.mode stereo\n")},
      {"the retired key time.sync", replace("time.mode sync\n", "time.sync sync\n")},
      {"the retired key time.scale", replace("mix 30\n", "mix 30\ntime.scale 1\n")},
      {"the retired labels free and sync as time.mode's own (free)", replace("time.mode sync\n", "time.mode free\n")},
      {"a time mode by index", replace("time.mode sync\n", "time.mode 2\n")},
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
  // Every patch key, omitted in turn, refuses the whole text: all or nothing has no exception: the reader
  // does not default a key (D-048 A1, D-050 ruling 2; the plant stateMissingKeyDefaults). The line must be there to be removed, so a writer that leaves a
  // key out (stateKeyMissing) fails here too.
  for (int i = 0; i < echo::kNumParams; ++i) {
    const auto& pd = echo::kParams[static_cast<std::size_t>(i)];
    if (pd.morph == echo::Morph::Device) continue;
    const std::string key(pd.key);
    const std::size_t at = s.find("\n" + key + " ");
    REQUIRE(at != std::string::npos, "the saved text has no line for %s", key.c_str());
    const std::size_t end = s.find('\n', at + 1);
    const std::string without = s.substr(0, at + 1) + s.substr(end + 1);
    const char* whyMissing = "";
    REQUIRE(!e->stageState(without, &whyMissing) && !e->adoptStaged() && saveText(*e) == s, "a text without %s was accepted", key.c_str());
  }
  const char* why = "";
  REQUIRE(restore(*e, replace("time.div 1/4\n", "time.div 1/8D\n"), &why), "the alias 1/8D was refused (%s)", why);
  REQUIRE(saveText(*e).find("\ntime.div 3/16\n") != std::string::npos, "the alias was not written back as its name 3/16");
  for (const char* mode : {"sec", "hz", "sync"}) {  // each label reads, and is written back by name
    REQUIRE(restore(*e, replace("time.mode sync\n", std::string("time.mode ") + mode + "\n"), &why), "time.mode %s was refused (%s)", mode, why);
    REQUIRE(saveText(*e).find(std::string("\ntime.mode ") + mode + "\n") != std::string::npos, "time.mode %s was not written back by name", mode);
  }
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
  auto a = mk({{"time.mode", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 50}}, sr);
  auto b = mk({{"time.mode", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 50}}, sr);
  const auto oa = runActions(*a, in, in, {{T, kClear}}, 512), ob = runActions(*b, in, in, {}, 512);
  for (long i = 0; i < T; ++i) REQUIRE(oa.L[i] == ob.L[i] && oa.R[i] == ob.R[i], "the Clear changed sample %ld, before its offset %ld", i, T);
  REQUIRE(maxAbs(ob.L, T, T + N) > 1e-3, "the case is vacuous: nothing rings at the Clear");
  for (long j = 0; j < N; ++j) {
    const double g = static_cast<double>(N - 1 - j) / static_cast<double>(N);
    REQUIRE(std::fabs(oa.L[T + j] - ob.L[T + j] * g) <= tol::kClearFade, "at %.0f Hz, %ld after the Clear the output is %.9g, the 20 ms fade expects %.9g",
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
    auto a = mk({{"time.mode", 0}, {"time.ms", 20}, {"time.change", 0}, {"time.law", 0}, {"time.glide", 1}, {"fb.amount", 60}});
    auto b = mk({{"time.mode", 0}, {"time.ms", 20}, {"time.change", 0}, {"time.law", 0}, {"time.glide", 1}, {"fb.amount", 60}});
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
    auto mkS = [] { return mk({{"time.mode", 2}, {"time.div", 9}, {"time.change", 0}, {"time.tempoMode", 0}, {"fb.amount", 60}}); };
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
  auto a = mk({{"time.mode", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 30}});
  auto b = mk({{"time.mode", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 30}});
  const auto oa = runActions(*a, in, in, {{T, kClear}}, 64, sets), ob = runActions(*b, in, in, {}, 64, sets);
  REQUIRE(maxAbs(ob.L, T, T + N) > 1e-3, "the case is vacuous: nothing rings at the Clear");
  for (long j = 0; j < N; ++j) {
    const double g = static_cast<double>(N - 1 - j) / static_cast<double>(N);
    REQUIRE(std::fabs(oa.L[T + j] - ob.L[T + j] * g) <= tol::kClearFade, "%ld into the fade the wet is %.9g, the live mix expects %.9g", j, oa.L[T + j],
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
  auto a = mk({{"time.mode", 0}, {"time.ms", 2000}, {"fb.amount", 50}, {"mix", 40}});
  auto b = mk({{"time.mode", 0}, {"time.ms", 2000}, {"fb.amount", 50}, {"mix", 40}});
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
  auto mkRing = [] { return mk({{"time.mode", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 50}}); };
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
  auto rest = mk({{"time.mode", 0}, {"time.ms", 90}, {"fb.amount", 75}}), twin = mk({{"time.mode", 0}, {"time.ms", 90}, {"fb.amount", 75}});
  const Vec pulse = impulse(S(0.3), 100, 0.5);
  const auto orest = runActions(*rest, pulse, pulse, {{0, kClear}}, 64), otwin = runActions(*twin, pulse, pulse, {}, 64);
  for (long i = 0; i < S(0.3); ++i) REQUIRE(orest.L[i] == otwin.L[i], "a Clear at rest changed sample %ld", i);
  return true;
}

// Criterion 3: a performance event: the saved state is byte-identical before
// and after a Clear (ADR-267 A1).
bool clearState(Detail& d) {
  auto e = mk({{"time.mode", 0}, {"time.ms", 90}, {"fb.amount", 75}, {"mix", 50}});
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
  auto mkE = [] { return mk({{"time.mode", 0}, {"time.ms", 45}, {"fb.amount", 70}, {"mix", 50}}); };
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

// ---- M3 B3, T-017: module presets (SPEC §16.3; rows A12, A13; echo/presets.h) ----
// T-039 (D-050): Echo offers no macros, so the slot model, the resolver, the curves and the checks that
// went with them are gone; a preset is plain values and its file keeps `slots: []`.

namespace P = echo::presets;

// A fixture exercising the whole value set: the fixed row's keys (time.ms, fb.amount, mix, and route.mode,
// D-050's Type) and a few more all off their defaults, so A12 shows its values exact. It also sets time.change,
// time.law, time.glide, mod.depthMs and time.offR off their defaults: every bank preset is repitch / tape at
// the default glide, so without these A13's corners would not differ in those keys.
P::ModulePreset valuesFixture() {
  P::ModulePreset p = P::initPreset();
  p.id = "fx-values";
  p.label = "Values";
  p.description = "a test fixture: values off their defaults";
  p.defaults[echo::kTimeMs] = 250;
  p.defaults[echo::kFb] = 40;
  p.defaults[echo::kLoopLp] = 6000;
  p.defaults[echo::kModDepth] = 3;
  p.defaults[echo::kMix] = 45;
  p.defaults[echo::kRoute] = 1;
  p.defaults[echo::kChange] = 1;  // crossfade
  p.defaults[echo::kLaw] = 1;     // exponential
  p.defaults[echo::kGlide] = 1.2;
  p.defaults[echo::kOffR] = 15;
  return p;
}

// A13's four corners, and A10's corner loads: the fixture and three factory presets. They differ in the
// continuous keys and in sync, division, routing, mod.shape, and the fixture differs from the three in
// time.change and time.law (the bank is all repitch / tape).
std::vector<P::ModulePreset> fourCorners() {
  std::vector<P::ModulePreset> c{valuesFixture()};
  for (const auto& p : P::factory())
    if (p.id == "dub-loop" || p.id == "chorus-line" || p.id == "wide-ms") c.push_back(p);
  return c;
}

// Validation (T-017): every way validate() and readBank() refuse, each on its own against the fixture's
// own file (an anchor that drifted fails the check loudly, it never passes as "refused"), and the valid
// fixtures pass. Not covered here: the JSON grammar's own limits (nesting depth, escapes).
// A preset with a slot is refused BY NAME whatever else is wrong with it (D-050), including the old
// echo-factory.json shape (slot plus macroLaw), a slot with no id, and a map of slots.
bool presetValidate(Detail& d) {
  std::string why;
  for (const auto& p : {P::initPreset(), valuesFixture()}) REQUIRE(P::validate(p, &why), "the valid %s was refused: %s", p.id.c_str(), why.c_str());
  // validate() direct: strings the file's reader would already refuse as raw control characters.
  for (const char* what : {"id", "label", "description"}) {
    P::ModulePreset c = valuesFixture();
    (std::string_view(what) == "id" ? c.id : std::string_view(what) == "label" ? c.label : c.description) = "Va\nlues";
    REQUIRE(!P::validate(c), "a control character in the %s was accepted", what);
  }
  const std::string good = P::writeBank({valuesFixture()});
  std::vector<P::ModulePreset> got;
  REQUIRE(P::readBank(good, got, &why), "the fixture's own file was refused: %s", why.c_str());
  std::string lost;
  auto swap = [&](std::string t, const std::string& from, const std::string& to) {
    const auto at = t.find(from);
    if (at == std::string::npos) { lost += (lost.empty() ? "" : "; ") + from; return t; }
    return t.replace(at, from.size(), to);
  };
  auto bad = [&](const std::string& from, const std::string& to) { return swap(good, from, to); };
  const std::string slot = "{\"slot\": 1, \"label\": \"Time\", \"home\": 0.5, \"bind\": {\"time.ms\": [-1, 1]}}";
  const std::string oldShape = swap(bad("\"internal_defaults\"", "\"macroLaw\": \"modify\", \"internal_defaults\""), "\"slots\": []", "\"slots\": [" + slot + "]");
  const std::vector<std::pair<const char*, std::string>> cases{
      {"an unknown key", bad("\"mix\": 45", "\"mix\": 45, \"width\": 50")},
      {"a Device key in internal_defaults", bad("\"mix\": 45", "\"mix\": 45, \"io.inGain\": 0")},
      {"a missing key", bad("\"mix\": 45, ", "")},
      {"a duplicated key", bad("\"mix\": 45", "\"mix\": 45, \"mix\": 45")},
      {"a continuous value out of range", bad("\"mix\": 45", "\"mix\": 450")},
      {"a stepped value out of range", bad("\"mod.seed\": 1", "\"mod.seed\": 70000")},
      {"a stepped value with a fraction", bad("\"mod.seed\": 1", "\"mod.seed\": 1.5")},
      {"a number given as a string", bad("\"mix\": 45", "\"mix\": \"45\"")},
      {"a bad label", bad("\"route.mode\": \"pingpong\"", "\"route.mode\": \"sideways\"")},
      {"an empty id", bad("\"id\": \"fx-values\"", "\"id\": \"\"")},
      {"an empty label", bad("\"label\": \"Values\"", "\"label\": \"\"")},
      {"a description as a number", bad("\"description\": \"a test fixture: values off their defaults\"", "\"description\": 5")},
      {"an unknown preset field", bad("\"internal_defaults\"", "\"colour\": \"red\", \"internal_defaults\"")},
      {"a macroLaw (D-050: no longer written, so unknown)", bad("\"internal_defaults\"", "\"macroLaw\": \"modify\", \"internal_defaults\"")},
      {"slots missing", bad(",\n   \"slots\": []", "")},
      {"slots as an object", bad("\"slots\": []", "\"slots\": {}")},
      {"another schema", bad("echo-module-presets/1", "echo-module-presets/2")},
      {"another module", bad("\"module\": \"echo\"", "\"module\": \"scape\"")},
      {"another module_version", bad(std::string(echo::kModuleVersion), "0000000000000000")},
      {"an unknown field in the bank", bad("\"schema\"", "\"colour\": 1, \"schema\"")},
      {"text after the bank", good + "{}"},
      {"a slot", bad("\"slots\": []", "\"slots\": [" + slot + "]")},
      {"a slot and a macroLaw (the old file's shape)", oldShape},
      {"a slot with an empty id", swap(bad("\"id\": \"fx-values\"", "\"id\": \"\""), "\"slots\": []", "\"slots\": [" + slot + "]")},
      {"a slot with no id", swap(bad("\"id\": \"fx-values\",", ""), "\"slots\": []", "\"slots\": [" + slot + "]")},
      {"a map of slots", bad("\"slots\": []", "\"slots\": {\"1\": 1}")}};
  REQUIRE(lost.empty(), "the test's anchor(s) `%s` are not in the fixture's file", lost.c_str());
  for (const auto& [what, text] : cases) {
    why.clear();
    REQUIRE(!P::readBank(text, got, &why), "%s was accepted in a bank file", what);
    const std::string w = what;
    if (w.rfind("a slot", 0) == 0 || w == "a map of slots") {
      const bool noId = w.find("id") != std::string::npos;
      REQUIRE(why.find(noId ? "a preset with no id: a slot" : "fx-values: a slot") == 0, "`%s` was refused, but not for the slot by name: `%s`", what, why.c_str());
    }
  }
  // All or nothing: a second preset that is refused leaves the caller's bank untouched, the first preset's read discarded.
  std::string two = P::writeBank({valuesFixture(), P::initPreset()});
  const auto lastSlots = two.rfind("\"slots\": []");
  REQUIRE(lastSlots != std::string::npos, "the test's anchor for the second preset's slots is lost");
  two.replace(lastSlots, std::string("\"slots\": []").size(), "\"slots\": [" + slot + "]");
  std::vector<P::ModulePreset> keep{P::initPreset()};
  REQUIRE(!P::readBank(two, keep, &why) && keep.size() == 1 && keep[0].id == "init" && why.find("init: a slot") == 0,
          "a refused second preset changed the caller's bank (%zu presets) or was not refused by name: `%s`", keep.size(), why.c_str());
  return true;
}

// A12: Echo offers no macros (RS-5.1: a module may offer none; D-050), so every preset's `slots` is empty in the
// file. What the row still checks: every preset saves and loads with its id, label, description and every value exact
// (the fixed row's keys included, the fixture has them off their defaults); written again, byte-identical.
bool presetA12(Detail& d) {
  auto bank = P::factory();
  bank.push_back(valuesFixture());
  const std::string t1 = P::writeBank(bank);
  std::vector<P::ModulePreset> got;
  std::string why;
  REQUIRE(P::readBank(t1, got, &why), "the bank's own text was refused: %s", why.c_str());
  REQUIRE(got.size() == bank.size(), "%zu presets read back, %zu written", got.size(), bank.size());
  constexpr std::string_view kEmpty = "\"slots\": []", kAny = "\"slots\": [";
  std::size_t any = 0, empty = 0;
  for (auto at = t1.find(kAny); at != std::string::npos; at = t1.find(kAny, at + 1)) {
    ++any;
    if (t1.compare(at, kEmpty.size(), kEmpty) == 0) ++empty;
  }
  REQUIRE(any == bank.size() && empty == bank.size(), "%zu of %zu presets write empty slots (macros are not offered, RS-5.1)", empty, bank.size());
  for (std::size_t i = 0; i < bank.size(); ++i) {
    const auto &a = bank[i], &b = got[i];
    REQUIRE(a.id == b.id && a.label == b.label && a.description == b.description, "preset %zu's header changed", i);
    for (int k = 0; k < echo::kNumParams; ++k)
      if (echo::kParams[static_cast<std::size_t>(k)].morph != echo::Morph::Device)
        REQUIRE(a.defaults[static_cast<std::size_t>(k)] == b.defaults[static_cast<std::size_t>(k)], "%s: internal value %d changed", a.id.c_str(), k);
  }
  REQUIRE(P::writeBank(got) == t1, "written again, the bank text differs");
  return true;
}

// A13 (critic T-017 H2: one ENGINE taken through the corners): the corners' text round-trips first (readBank of
// writeBank); then for each corner c, one engine loads c, then every other corner in turn (each a load mid-play,
// with input running), then c again; once the last fade ends, its saved state equals a fresh engine's loaded at c,
// byte for byte, AND is the corner's own values (T-039: with the resolver gone, a load that drops a key is what a
// corner flip can still get wrong), and a fresh engine restored from it renders as that fresh one, both channels.
bool presetA13(Detail& d) {
  std::vector<P::ModulePreset> read;
  std::string why;
  REQUIRE(P::readBank(P::writeBank(fourCorners()), read, &why), "the corners' text was refused: %s", why.c_str());
  REQUIRE(read.size() == 4, "%zu corners read back, not 4", read.size());
  auto loadAt = [&](Echo& e, std::size_t c) {
    const auto kv = P::patchOf(read[c].defaults);
    e.loadState(std::span<const echo::KeyValue>(kv.data(), kv.size()));
  };
  g_rng = 61;
  Vec live(S(0.05));
  for (auto& v : live) v = 0.3 * noise();
  const Vec in = impulse(S(0.4), 10, 0.5);
  for (std::size_t c = 0; c < read.size(); ++c) {
    auto e = mk({});
    loadAt(*e, c);
    run(*e, live, live);
    for (std::size_t o = 0; o < read.size(); ++o)
      if (o != c) { loadAt(*e, o); run(*e, live, live); }  // a corner flip mid-play, its fade run out
    loadAt(*e, c);
    run(*e, live, live);
    auto ref = mk({});
    loadAt(*ref, c);
    const std::string se = saveText(*e), sr = saveText(*ref);
    REQUIRE(se == sr, "corner %zu: after flipping through the corners the engine's state differs from a fresh load", c);
    echo::state::Values saved{};
    const char* swhy = "";
    REQUIRE(echo::state::parse(se, saved, &swhy), "corner %zu: the engine's saved state does not parse (%s)", c, swhy);
    for (int k = 0; k < echo::kNumParams; ++k) {
      const auto u = static_cast<std::size_t>(k);
      if (echo::kParams[u].morph == echo::Morph::Device) continue;
      REQUIRE(saved[u] == read[c].defaults[u], "corner %zu: the saved %s is %.17g, the corner's value is %.17g", c, echo::kParams[u].key.data(), saved[u],
              read[c].defaults[u]);
    }
    auto t = mk({});
    REQUIRE(restore(*t, se, nullptr), "corner %zu: the state did not restore", c);
    const auto rt = run(*t, in, in), rr = run(*ref, in, in);
    for (long i = 0; i < S(0.4); ++i)
      REQUIRE(rt.L[i] == rr.L[i] && rt.R[i] == rr.R[i], "corner %zu: the round-tripped engine renders differently at %ld", c, i);
  }
  return true;
}

// The factory bank (T-021, D-028): exactly the init plus seven, in this order. The ids
// are the contract a dropped preset breaks.
const char* const kFactoryIds[] = {"init", "slap", "pingpong-dotted", "dub-loop", "chorus-line", "wide-ms", "tape-throw", "runaway"};

// A preset loaded into a prepared engine the way a host loads it (patchOf of its
// values), and the state that engine then saves (SPEC §16.2).
struct FactoryLoad {
  std::string text;
  bool loaded = false;
};
FactoryLoad loadFactory(const P::ModulePreset& p) {
  FactoryLoad f;
  auto e = mk({});
  const auto kv = P::patchOf(p.defaults);
  f.loaded = e->loadState(std::span<const echo::KeyValue>(kv.data(), kv.size()));
  f.text = saveText(*e);
  return f;
}

// A12 over the bank: each factory preset is valid, and a host that loads it gets the preset's OWN values
// (read back from the engine's saved state, not from the preset the load started from).
bool presetFactoryBank(Detail& d) {
  const auto bank = P::factory();
  REQUIRE(bank.size() == 8, "the factory bank holds %zu presets, not the init plus seven", bank.size());
  std::string why;
  for (std::size_t i = 0; i < bank.size(); ++i) {
    const auto& p = bank[i];
    REQUIRE(p.id == kFactoryIds[i], "factory preset %zu is `%s`, not `%s`", i, p.id.c_str(), kFactoryIds[i]);
    REQUIRE(P::validate(p, &why), "%s was refused: %s", p.id.c_str(), why.c_str());
    const auto f = loadFactory(p);
    REQUIRE(f.loaded, "%s: the engine refused the load", p.id.c_str());
    echo::state::Values saved{};
    const char* swhy = "";
    REQUIRE(echo::state::parse(f.text, saved, &swhy), "%s: the engine's saved state does not parse (%s)", p.id.c_str(), swhy);
    for (int k = 0; k < echo::kNumParams; ++k) {
      const auto u = static_cast<std::size_t>(k);
      if (echo::kParams[u].morph == echo::Morph::Device) continue;  // the Device keys are the host's, not the preset's (RS-4.5)
      REQUIRE(saved[u] == p.defaults[u], "%s: the saved state's %s is %.17g, the preset's is %.17g", p.id.c_str(), echo::kParams[u].key.data(), saved[u],
              p.defaults[u]);
    }
  }
  return true;
}

// A19 over the bank (D-039): every factory preset's saved state fits 2 KiB. The sizes vary
// with the value text (bounded, not fixed-width); stateSize's worst case bounds any preset.
bool presetFactoryStateSize(Detail& d) {
  const auto bank = P::factory();
  std::size_t largest = 0;
  std::string sizes;
  for (std::size_t i = 0; i < bank.size(); ++i) {
    const auto& p = bank[i];
    auto f = loadFactory(p);
    REQUIRE(f.loaded, "%s: the engine refused the load", p.id.c_str());
    // TEST-SIDE plant: the saved text grows past the budget, as a preset whose state outgrew it would.
    if (echo::plant::presetStateOversized && i + 1 == bank.size()) f.text.resize(echo::state::kMaxBytes + 1, ' ');
    REQUIRE(!f.text.empty() && f.text.size() <= echo::state::kMaxBytes, "%s: the saved state is %zu bytes (budget %zu)", p.id.c_str(), f.text.size(),
            echo::state::kMaxBytes);
    largest = std::max(largest, f.text.size());
    sizes += (i ? ", " : "") + p.id + " " + std::to_string(f.text.size());
  }
  std::printf("  REPORT factory state sizes (B, budget 2048): %s; largest %zu\n", sizes.c_str(), largest);
  return true;
}

// B408 for the bank, STATED not gated (M5): each preset's two line times as loaded, the
// colouring flag (D-044) and the fold's worst 1/3-octave point on a centred (mono)
// source, through the same renderer and metric the stereo report uses (fold.h).
bool presetFactoryStereo(Detail& d) {
  for (const auto& p : P::factory()) {
    echo::IrPatch patch{};
    const auto& at = p.defaults;
    for (std::size_t k = 0; k < patch.size(); ++k) patch[k] = at[k];
    const auto t = fold::take(patch, echo::IrInput::Mono, kSr, -1, std::int64_t{1} << 19);
    REQUIRE(t.ok, "%s: the renderer refused the patch", p.id.c_str());
    const int mode = static_cast<int>(at[echo::kRoute]);
    const auto w = fold::worstNotch(fold::spectra(t.L, t.R, kSr, t.info.restAt > 0 ? static_cast<std::size_t>(t.info.restAt) : 0));
    std::printf("  REPORT B408 %-15s %-8s tL %.4f s tR %.4f s colouring %-3s worst 1/3 oct %.2f dB at %.0f Hz (centred source%s)\n", p.id.c_str(),
                echo::labels::kRoute[static_cast<std::size_t>(mode)].data(), t.info.times[0], t.info.times[1],
                fold::colouringZone(mode, t.info.times[0], t.info.times[1]) ? "YES" : "no", fold::db(w.rho), w.hz,
                t.info.tail >= echo::kInfiniteTail ? ", tail capped at 2^19 samples" : "");
  }
  return true;
}

// ---- M3 B3, T-018: the admission self-test's new checks (contract §2.2 rows; D-043) ----

// A stress patch, harsher than the factory's edge patch `runaway` (deep fast mod, every filter on): past-unity
// feedback, ping-pong, the tape law, deep fast SINE modulation (its phase is
// the running state rest holds, so A2 can see a reset that skips it), every
// filter on.
std::unique_ptr<Echo> mkStress(double sr = kSr) {
  return mk({{"time.mode", 0}, {"time.ms", 37}, {"time.offR", 30}, {"fb.amount", 110}, {"route.mode", 1}, {"time.law", 0}, {"mod.depthMs", 20},
             {"mod.rateHz", 7}, {"mod.shape", 0}, {"mod.spread", 100}, {"filt.in.on", 1}, {"filt.out.on", 1}, {"filt.out.hpHz", 80}, {"mix", 70}}, sr);
}

// A2: seeded noise, 2 s, at the defaults and the stress patch (whose 20 ms of
// drift modulation makes the phase audible): after reset() the output is
// bit-identical to a freshly prepared instance, with a smoother mid-ramp at the
// reset and 2 s of modulation run before it.
bool a2ResetIdentity(Detail& d) {
  for (int stress = 0; stress < 2; ++stress) {
    g_rng = 71;
    Vec in(S(2.0));
    for (auto& v : in) v = 0.3 * noise();
    auto a = stress ? mkStress() : mk({});
    run(*a, in, in);
    a->setParam("mix", 80);  // a 20 ms mix ramp in flight when reset() lands (healed by the rest landing either way)
    a->reset();
    auto b = stress ? mkStress() : mk({});
    b->setParam("mix", 80);
    b->reset();
    const auto oa = run(*a, in, in), ob = run(*b, in, in);
    for (long i = 0; i < S(2.0); ++i)
      REQUIRE(oa.L[i] == ob.L[i] && oa.R[i] == ob.R[i], "%s: after reset() sample %ld differs from a fresh instance", stress ? "stress" : "defaults", i);
  }
  return true;
}

// A4: latency truth on an impulse and a swept sine: the dry path's delay is the
// declared 0 samples. Feedback 0 and a 500 ms time keep the window dry-only, so
// the output must be the input times the dry gain, sample for sample.
bool a4Latency(Detail& d) {
  REQUIRE(mk({})->properties().latency->samples == 0, "the declared latency is not 0");
  auto e = mk({{"time.mode", 0}, {"time.ms", 500}, {"fb.amount", 0}, {"mix", 30}});
  const double dg = std::cos(0.3 * kPi / 2);
  const long n = S(0.4);
  Vec imp = impulse(n, 1000, 1.0), sw(n);
  for (long i = 0; i < n; ++i) {  // a log sweep 50 Hz -> 15 kHz
    const double t = static_cast<double>(i) / kSr, T = 0.4, f0 = 50, f1 = 15000, k = std::log(f1 / f0);
    sw[i] = 0.4 * std::sin(2 * kPi * f0 * T / k * (std::exp(t / T * k) - 1));
  }
  const auto oi = run(*e, imp, imp);
  long first = -1;
  for (long i = 0; i < n && first < 0; ++i)
    if (oi.L[i] != 0.0) first = i;
  REQUIRE(first == 1000 && std::fabs(oi.L[1000] - dg) < 1e-15 && std::fabs(oi.R[1000] - dg) < 1e-15,
          "the impulse at 1000 comes out first at %ld (%.6f): the dry path is not on time", first, first >= 0 ? oi.L[first] : 0.0);
  auto f = mk({{"time.mode", 0}, {"time.ms", 500}, {"fb.amount", 0}, {"mix", 30}});
  const auto os = run(*f, sw, sw);
  for (long i = 0; i < n; ++i)
    REQUIRE(std::fabs(os.L[i] - dg * sw[i]) < 1e-15 && std::fabs(os.R[i] - dg * sw[i]) < 1e-15, "the swept sine's dry path is off at %ld (%.9g vs %.9g)",
            i, os.L[i], dg * sw[i]);
  return true;
}

// A8: L 220 Hz and R 330 Hz, decorrelated: in standard routing |L - R| does not
// collapse (Echo declares changes_image for ping-pong and mid/side only).
bool a8Image(Detail& d) {
  const long n = S(1.0);
  const Vec L = sine(n, 220, 0.4), R = sine(n, 330, 0.4);
  auto e = mk({{"route.mode", 0}});
  const auto o = run(*e, L, R);
  double din = 0, dout = 0;
  for (long i = S(0.2); i < n; ++i) {
    din += (L[i] - R[i]) * (L[i] - R[i]);
    dout += (o.L[i] - o.R[i]) * (o.L[i] - o.R[i]);
  }
  const double ratio = std::sqrt(dout / din);
  // Clean reads ~1.18 (the repeats add level); a 25 % crossfeed reads 0.59 (critic T-018 L), a mono sum 0.
  REQUIRE(ratio > 0.9, "standard routing narrows the image: rms |L - R| is %.3f of the input's", ratio);
  return true;
}

// A10: a scripted session on the audio thread: sweeps of every continuous key,
// preset-to-preset moves (every key set to a point between two presets' values), corner loads,
// a staged state adopted, Clear, tempo moves, sleep and wake, varied blocks.
// Zero allocations. (Locks and syscalls are checked by review only: no
// RealtimeSanitizer on this toolchain; the engine calls nothing that locks.)
bool a10Session(Detail& d) {
  auto e = mk({{"time.mode", 0}, {"mod.depthMs", 3}});
  const auto cs = fourCorners();
  std::vector<std::vector<echo::KeyValue>> corners;
  for (const auto& c : cs) corners.push_back(P::patchOf(c.defaults));  // patchOf allocates; the host's, off the audio thread: done before arming
  // 64 value sets at (s % 16) / 15 of the way from the fixture's values to tape-throw's: every continuous key the two presets differ in
  // moves (time.ms, fb.amount, mix, the filters, mod depth and rate, time.offR, glide, ...), the stepped and Device keys hold the
  // fixture's. Between two valid values every key stays in range.
  const auto& from = cs[0].defaults;
  const auto to = P::factory()[6].defaults;
  std::vector<echo::state::Values> moves;
  for (int s = 0; s < 64; ++s) {
    echo::state::Values v = from;
    const double w = (s % 16) / 15.0;
    for (int k = 0; k < echo::kNumParams; ++k) {
      const auto u = static_cast<std::size_t>(k);
      if (!echo::kParams[u].stepped && echo::kParams[u].morph != echo::Morph::Device) v[u] = from[u] + (to[u] - from[u]) * w;
    }
    moves.push_back(v);
  }
  auto other = mk({{"fb.amount", 70}, {"time.ms", 222}});
  const std::string staged = saveText(*other);
  e->stageState(staged);  // stageState runs off the audio thread too
  g_rng = 73;
  Vec in(4096), oL(4096), oR(4096), z(4096, 0.0);
  for (auto& v : in) v = 0.3 * noise();
  const int blocks[] = {1, 7, 64, 256, 4096, 33};
  alloc_probe::arm();
  int step = 0;
  for (int pass = 0; pass < 6; ++pass) {
    for (int k = 0; k < echo::kNumParams; ++k) {  // a sweep of every continuous key
      const auto& p = echo::kParams[static_cast<std::size_t>(k)];
      if (p.stepped || p.morph == echo::Morph::Device) continue;
      e->setParam(k, p.min + (p.max - p.min) * ((step * 7 + k) % 11) / 10.0);
      const int bs = blocks[step++ % 6];
      e->process(in.data(), in.data(), oL.data(), oR.data(), bs, 100.0 + pass);
    }
    for (int k = 0; k < echo::kNumParams; ++k) e->setParam(k, moves[static_cast<std::size_t>((step + k) % 64)][static_cast<std::size_t>(k)]);
    e->loadState(std::span<const echo::KeyValue>(corners[static_cast<std::size_t>(pass % 4)].data(), corners[static_cast<std::size_t>(pass % 4)].size()));
    e->fireAction(echo::Echo::kActionClear, 10);
    e->process(in.data(), in.data(), oL.data(), oR.data(), 512, 140.0);
    if (pass == 2) e->adoptStaged();
    for (int q = 0; q < 200; ++q) e->process(z.data(), z.data(), oL.data(), oR.data(), 4096, 120.0);  // toward sleep
    e->process(in.data(), in.data(), oL.data(), oR.data(), 64, 120.0);  // and the wake
  }
  alloc_probe::disarm();
  REQUIRE(alloc_probe::g_allocs.load() == 0, "%ld allocation(s) on the audio thread in the scripted session", alloc_probe::g_allocs.load());
  return true;
}

// A14: inside horde's cycle (one sample of feedback delay, the module called at
// n = 1) at loop gain 1.2: silence in gives silence out, exactly, for 2 s. A
// module that starts itself from silence would grow in the loop.
bool a14Cycle(Detail& d) {
  REQUIRE(*mk({})->properties().min_block == 1u, "min_block is not 1: Echo could not sit in a cycle");
  for (int stress = 0; stress < 2; ++stress) {
    auto e = stress ? mkStress() : mk({});
    double fbL = 0, fbR = 0, worst = 0;
    for (long i = 0; i < S(2.0); ++i) {
      double xL = 1.2 * fbL, xR = 1.2 * fbR, yL = 0, yR = 0;
      e->process(&xL, &xR, &yL, &yR, 1, 120.0);
      fbL = yL;
      fbR = yR;
      worst = std::max({worst, std::fabs(yL), std::fabs(yR)});
    }
    REQUIRE(worst == 0.0, "%s: from silence, in a 1.2 cycle the output reached %.3g: the loop started itself", stress ? "stress" : "defaults", worst);
  }
  return true;
}

// The B265 port (b265.h): horde's own JavaScript, run under node at horde
// 54caca8 on these eight vectors (the script and its output verbatim:
// traces/2026-10-26-t018-admission.md), gave these readings; the port must match
// all three numbers of each, the click (db), where it peaks (at) and the steady
// floor, to 1e-9. The vectors aim at every window edge (critic T-018 M1): a
// spike late in the span, one in the pre-window, a step just past the span.
bool b265Port(Detail& d) {
  const double sr = 48000.0;
  const long n = 96000, t0 = 24000 + 12;  // a quarter period of 1 kHz past a zero crossing: the step lands on the peak
  const auto sine = b265::sineProbe(n, sr), chord = b265::chordProbe(n, sr);
  const long N = std::lround(0.02 * sr);
  std::vector<double> step = sine, ramp = sine, past = sine, spike = chord, late = chord, pre = chord;  // copies, then each altered
  for (long i = t0; i < n; ++i) step[i] *= 0.5;
  for (long i = t0; i < n; ++i) ramp[i] *= (i >= t0 + N) ? 0.5 : 1 - 0.5 * static_cast<double>(i - t0 + 1) / static_cast<double>(N);
  for (long i = t0 + std::lround(1.01 * sr); i < n; ++i) past[i] *= 0.5;
  spike[t0] += 0.25;
  late[t0 + std::lround(0.8 * sr)] += 0.25;
  pre[t0 - std::lround(0.24 * sr)] += 0.25;
  struct R { const char* name; const std::vector<double>* y; double db, at, floor; };
  const R refs[] = {{"sine steady", &sine, 0.0, 0.97999999999999998, 5.3173016505731971e-12},
                    {"sine step to half", &step, 15.589173966224418, 0.0, 5.3173016505731971e-12},
                    {"sine 20 ms ramp", &ramp, 0.0, 0.97999999999999998, 5.3173016505731971e-12},
                    {"chord spike", &spike, 52.662978959637798, 0.0, 7.9671571291875996},
                    {"chord steady", &chord, 0.0, 0.17399999999999999, 7.9671571291875996},
                    {"chord spike late in the span", &late, 54.308744084174734, 0.80000000000000004, 7.9671571291875996},
                    {"chord spike in the pre-window", &pre, 0.0, 0.17399999999999999, 60.834654241877345},
                    {"sine step just past the span", &past, 0.0, 0.99199999999999999, 15.589173966237640}};
  for (const auto& r : refs) {
    const auto c = b265::clickOf(*r.y, sr, t0);
    REQUIRE(std::fabs(c.db - r.db) <= 1e-9 && std::fabs(c.at - r.at) <= 1e-9 && std::fabs(c.floor - r.floor) <= 1e-9,
            "%s reads %.12f dB at %.6f s, floor %.12f; horde's JS %.12f dB at %.6f s, floor %.12f", r.name, c.db, c.at, c.floor, r.db, r.at, r.floor);
  }
  return true;
}

// A11's REPORTED reading (§12.1: never judged): every flip-class key switched,
// both directions, on horde's two probes at 48 kHz, at every factory preset (the
// bank: init today, T-021 adds the seven) and at a modulated patch (at the
// defaults mod.depthMs is 0, so the seeded-jump keys mod.shape and mod.seed would
// read 0 by construction; critic T-018 M3). Beside each, an ideal offline
// equal-gain crossfade of the two steady renders over the key's DECLARED length
// (params.json): 20 ms for the routing and filter switches; for time.mode and
// time.div, per time.change mode: crossfade = time.xfadeMs, jump = 3 ms, repitch
// = none (it declares no length, so no ideal is printed); the deferred and
// seeded-jump keys declare none either. The worse of L and R is read.
[[maybe_unused]] void b265Report() {
  const double sr = 48000.0;
  // The switch at 2 s: the probe onset's own repeats (every 375 ms at 35 %) have
  // decayed to ~0.35^5 by then, so they no longer read inside the 1 s span.
  const long n = S(3.4), t0 = S(2.0) + 12;
  struct K { const char* key; double a, b; double change; double fadeS; };  // change < 0: leave time.change alone
  const K keys[] = {{"route.mode", 0, 1, -1, 0.02},  {"filt.in.on", 0, 1, -1, 0.02}, {"filt.loop.on", 1, 0, -1, 0.02}, {"filt.out.on", 0, 1, -1, 0.02},
                    {"time.mode", 2, 0, 0, -1},       {"time.div", 9, 12, 0, -1},     {"time.mode", 2, 0, 1, 0.06},    {"time.div", 9, 12, 1, 0.06},
                    {"time.mode", 2, 0, 2, 0.003},    {"time.div", 9, 12, 2, 0.003},  {"mod.shape", 0, 1, -1, -1},     {"mod.seed", 1, 2, -1, -1},
                    {"time.change", 0, 1, -1, -1},    {"time.law", 0, 1, -1, -1},     {"time.tempoMode", 1, 0, -1, -1}};
  const char* changeName[] = {"repitch", "crossfade", "jump"};
  struct Patch { std::string name; P::ModulePreset preset; bool modulated; };
  std::vector<Patch> patches;
  for (const auto& p : P::factory()) patches.push_back({p.id, p, false});
  patches.push_back({"modulated", P::initPreset(), true});
  for (const auto& pt : patches)
    for (const auto& probe : {std::string("chord"), std::string("1 kHz")}) {
      auto x = probe == "chord" ? b265::chordProbe(n, sr) : b265::sineProbe(n, sr);
      // Faded in over 100 ms (LIBRARY L0013): an abrupt probe onset echoes back
      // into the 1 s span and reads as a click of its own on every row.
      for (long i = 0; i < S(0.1, sr); ++i) x[static_cast<std::size_t>(i)] *= 0.5 - 0.5 * std::cos(kPi * static_cast<double>(i) / static_cast<double>(S(0.1, sr)));
      for (const auto& k : keys)
        for (int dir = 0; dir < 2; ++dir) {
          const double from = dir ? k.b : k.a, to = dir ? k.a : k.b;
          auto mkAt = [&](double v) {
            auto e = mk({}, sr);
            const auto kv = P::patchOf(pt.preset.defaults);
            e->loadState(std::span<const echo::KeyValue>(kv.data(), kv.size()));
            if (pt.modulated) { e->setParam("mod.depthMs", 3); e->setParam("mod.rateHz", 0.8); }
            if (k.change >= 0) e->setParam("time.change", k.change);
            e->setParam(k.key, v);
            return e;
          };
          auto e = mkAt(from), sa = mkAt(from), sb = mkAt(to);
          const auto o = run(*e, x, x, {set(t0, k.key, to)});
          const auto oa = run(*sa, x, x), ob = run(*sb, x, x);
          const double c = std::max(b265::clickOf(o.L, sr, t0).db, b265::clickOf(o.R, sr, t0).db);
          char ideal[64] = "no declared length";
          if (k.fadeS > 0) {
            const long L = std::max<long>(1, std::lround(k.fadeS * sr));
            std::vector<double> iL(static_cast<std::size_t>(n)), iR(static_cast<std::size_t>(n));
            for (long i = 0; i < n; ++i) {
              const double w = i < t0 ? 0.0 : std::min(1.0, static_cast<double>(i - t0 + 1) / static_cast<double>(L));
              iL[static_cast<std::size_t>(i)] = (1 - w) * oa.L[i] + w * ob.L[i];
              iR[static_cast<std::size_t>(i)] = (1 - w) * oa.R[i] + w * ob.R[i];
            }
            std::snprintf(ideal, sizeof ideal, "ideal equal-gain %g ms: %.2f dB", k.fadeS * 1000,
                          std::max(b265::clickOf(iL, sr, t0).db, b265::clickOf(iR, sr, t0).db));
          }
          std::printf("  REPORT b265 %-9s %-6s %-15s %-9s %5g -> %-5g %6.2f dB  (%s)\n", pt.name.c_str(), probe.c_str(), k.key,
                      k.change >= 0 ? changeName[static_cast<int>(k.change)] : "", from, to, c, ideal);
        }
    }
}

// ---- M3 B3, T-019: the B408 stereo report's Layer-0 (D-010 as ruled by D-044) ----
// The metric is core/tests/fold.h, the code echo_stereo_report prints
// docs/stereo-report.md from; these checks judge what the report states.

// The law, per repeat generation, on the report's equal-time patches in every
// mode, on exact inputs only (D-044 as re-ruled after critic T-019 H1): the mono
// impulse (rho_in = 1) and the left and right impulses (rho_in = 1/2). Standard and
// mid/side keep rho_out >= rho_in; ping-pong has no cross term (rho = 1/2:
// hard-panned by design). Then where ping-pong's sides overlap: its fold at 1 kHz
// (a whole multiple of 1/T) equals the closed form, so an inverted side is seen.
bool b408Gate(Detail& d) {
  for (const auto& np : fold::reportPatches()) {
    if (!np.gated) continue;
    for (int mode = 0; mode < 3; ++mode)
      for (auto in : {echo::IrInput::Mono, echo::IrInput::Left, echo::IrInput::Right}) {
        auto p = np.p;
        p[fold::keyIndex("route.mode")] = mode;
        const auto t = fold::take(p, in, kSr, 0);
        REQUIRE(t.ok, "%s mode %d: the renderer refused the request", np.name, mode);
        REQUIRE(t.info.times[0] == t.info.times[1], "%s mode %d: a gated patch has unequal times", np.name, mode);
        const double rin = fold::rho(fold::energy(t.inL, t.inR));
        REQUIRE(rin == (in == echo::IrInput::Mono ? 1.0 : 0.5), "%s mode %d input %d: rho_in %.17g is not exact", np.name, mode, static_cast<int>(in), rin);
        const auto g = fold::generations(t.L, t.R, t.info.times[0] * kSr);
        REQUIRE(g.size() == static_cast<std::size_t>(fold::kMaxGenerations), "%s mode %d: only %zu generations read (the gate would be thin)",
                np.name, mode, g.size());
        for (const auto& x : g)
          REQUIRE(fold::genPasses(mode, x, rin), "%s, mode %d, input %d: generation %d reads rho %.6f (r %.3g) against rho_in %.6f", np.name, mode,
                  static_cast<int>(in), x.n, x.rho, x.corr, rin);
      }
  }
  // Ping-pong's overlap: loop filter off (its phase would turn per pass), 500 ms,
  // 35 %: 1 kHz is 500 periods of 1/T. g is the feedback; the DC blocker's phase
  // at 1 kHz (~0.005 rad a pass) moves the reading by < 1e-4.
  const auto q = fold::take(fold::patchWith({{"route.mode", fold::kPingPong}, {"filt.loop.on", 0}}), echo::IrInput::Mono, kSr, -1);
  REQUIRE(q.ok, "ping-pong overlap: the renderer refused");
  const double got = fold::foldAt(q.L, q.R, 1000.0, kSr), want = fold::pingPongFoldClosedForm(0.35);
  REQUIRE(std::fabs(got - want) < 1e-3, "ping-pong at 1 kHz: the overlapping repeats fold to %.5f, the closed form %.5f (a side inverted?)", got, want);
  return true;
}

// The closed-form fold of a 1 ms equal-gain offset (L = d, R = d delayed by D
// samples): |M|^2 / mean(|L|^2, |R|^2) = cos^2(pi f D / sr), averaged over a band
// [lo, hi] as 1/2 + (sin 2x2 - sin 2x1) / (4 (x2 - x1)), x = pi f D / sr.
double cosSqAverage(double lo, double hi, double D, double sr) {
  const double x1 = kPi * lo * D / sr, x2 = kPi * hi * D / sr;
  return 0.5 + (std::sin(2 * x2) - std::sin(2 * x1)) / (4 * (x2 - x1));
}

// The metric's own controls: prior-art §5.3's P1 - P5, restated in rho's scale
// (D-044). P3 is held to the closed form at every point of both band views, so
// the smoothing, the band edges and the scale are each pinned.
bool b408Controls(Detail& d) {
  const long n = 1L << 18;  // 0.18 Hz bins: the band edges cost < 0.1 dB at the notch
  Vec L = zeros(n), R = zeros(n);
  L[0] = 1, R[0] = -1;  // P1
  const double p1 = fold::rho(fold::energy(L, R));
  REQUIRE(p1 == 0.0 && !fold::genPasses(fold::kStandard, {1, p1, -1.0, 2.0}, 1.0), "P1 (hL = d, hR = -d) reads rho %.6f and the law passes it", p1);
  R[0] = 1;  // P2
  const double p2 = fold::rho(fold::energy(L, R));
  REQUIRE(p2 == 1.0, "P2 (hL = hR = d) reads rho %.6f, not 1 (the scale)", p2);
  R[0] = 0, R[48] = 1;  // P3: 1 ms at 48 kHz
  const double p3 = fold::rho(fold::energy(L, R));
  REQUIRE(p3 == 0.5, "P3 (1 ms equal-gain offset) reads rho %.6f broadband, not 1/2 (no cross term)", p3);
  const auto sp = fold::spectra(L, R, kSr);
  double cfMin = 1e9, cfHz = 0;
  for (int j = 0; j < fold::gridPoints(); ++j) {
    const double f = fold::gridHz(j), cf = cosSqAverage(f * std::pow(2.0, -1.0 / 6), f * std::pow(2.0, 1.0 / 6), 48, kSr);
    const double got = fold::smoothedAt(sp, j);
    REQUIRE(std::fabs(fold::db(got) - fold::db(cf)) < 0.1, "P3 smoothed at %.1f Hz: %.3f dB, closed form %.3f dB", f, fold::db(got), fold::db(cf));
    if (cf < cfMin) cfMin = cf, cfHz = f;
  }
  const auto nn = fold::worstNotch(sp);
  REQUIRE(std::fabs(std::log2(nn.hz / cfHz)) <= 1.0 / fold::kGridPerOctave + 1e-9 && std::fabs(std::log2(nn.hz / 500.0)) < 1.0 / 3,
          "P3's notch at %.1f Hz: the closed form's is %.1f Hz (expected ~500 Hz)", nn.hz, cfHz);
  const auto oct = fold::octaves(sp);
  for (int k = 0; k < fold::kOctaves; ++k) {
    const double fc = fold::octaveCentre(k), cf = cosSqAverage(fc / std::sqrt(2.0), fc * std::sqrt(2.0), 48, kSr);
    REQUIRE(std::fabs(fold::db(oct[static_cast<std::size_t>(k)]) - fold::db(cf)) < 0.1, "P3 octave %.1f Hz: %.3f dB, closed form %.3f dB", fc,
            fold::db(oct[static_cast<std::size_t>(k)]), fold::db(cf));
  }
  // P4: the S-only impulse through mid/side: the repeats sound, and fold to exactly nothing.
  const auto s = fold::take(fold::patchWith({{"route.mode", fold::kMidSide}}), echo::IrInput::SideOnly, kSr, 0);
  const auto es = fold::energy(s.L, s.R);
  REQUIRE(s.ok && es.l > 1e-6 && es.m == 0.0, "P4 (S-only through mid/side): energy %.3g, fold %.3g (expected repeats folding to 0)", es.l, es.m);
  // P5: equal-time ping-pong, mono input: every repeat exactly 1/2, no cross term.
  const auto q = fold::take(fold::patchWith({{"route.mode", fold::kPingPong}}), echo::IrInput::Mono, kSr, 0);
  const auto g = fold::generations(q.L, q.R, q.info.times[0] * kSr);
  REQUIRE(q.ok && g.size() >= 4, "P5: %zu generations read", g.size());
  for (const auto& x : g)
    REQUIRE(std::fabs(x.rho - 0.5) <= fold::kExact && std::fabs(x.corr) <= fold::kExact, "P5: generation %d reads rho %.15f, r %.3g", x.n, x.rho, x.corr);
  return true;
}

// The colouring flag (SPEC §3.2 as ruled, D-044): standard routing, 0 < |tL - tR| <
// 40 ms in whole microseconds; and the facts the ruling rests on, measured: a
// stereo offset combs the fold (inside the zone AND at 50 ms: 40 ms is a warning
// convention, not a boundary this metric shows), while ping-pong and mid/side
// offsets do not comb at all.
bool b408Zone(Detail& d) {
  struct C { int mode; double tl, tr; bool want; };
  const C cases[] = {{fold::kStandard, 0.5, 0.5, false},   {fold::kStandard, 0.5, 0.501, true},    {fold::kStandard, 0.5, 0.5399, true},
                     {fold::kStandard, 0.5399, 0.5, true},   {fold::kStandard, 0.5, 0.5401, false},  {fold::kStandard, 0.25, 0.2901, false},
                     {fold::kStandard, 0.25, 0.29, false},   {fold::kStandard, 0.54, 0.5, false},  // exactly 40 ms both ways, though the doubles differ by an ulp
                     {fold::kPingPong, 0.5, 0.501, false}, {fold::kMidSide, 0.5, 0.501, false}, {fold::kMidSide, 0.5, 0.5, false}};
  for (const auto& c : cases)
    REQUIRE(fold::colouringZone(c.mode, c.tl, c.tr) == c.want, "mode %d, %.4f / %.4f s: the flag reads %d", c.mode, c.tl, c.tr, !c.want);
  // Through the engine's own resolved times, and the folds they make.
  struct E { int mode; double offR; bool want; double lo, hi; };  // the smoothed fold's worst point must lie in [lo, hi] dB
  const E es[] = {{fold::kStandard, 0.0, false, -0.01, 0.01},  {fold::kStandard, 0.2, true, -60, -6},  {fold::kStandard, 10.0, false, -60, -6},
                  {fold::kPingPong, 0.2, false, -3.6, -2.9}, {fold::kMidSide, 0.2, false, -0.01, 0.01}};
  for (const auto& e : es) {
    const auto t = fold::take(fold::patchWith({{"route.mode", e.mode}, {"time.offR", e.offR}}), echo::IrInput::Mono, kSr, -1);
    REQUIRE(t.ok, "mode %d, offR %g: the renderer refused", e.mode, e.offR);
    const bool z = fold::colouringZone(e.mode, t.info.times[0], t.info.times[1]);
    REQUIRE(z == e.want, "mode %d, offR %g %%: %.4f / %.4f s, the flag reads %d", e.mode, e.offR, t.info.times[0], t.info.times[1], z);
    const auto w = fold::worstNotch(fold::spectra(t.L, t.R, kSr, t.info.restAt > 0 ? static_cast<std::size_t>(t.info.restAt) : 0));
    REQUIRE(fold::db(w.rho) >= e.lo && fold::db(w.rho) <= e.hi, "mode %d, offR %g %%: the fold's worst 1/3 octave is %.2f dB at %.0f Hz, outside [%g, %g]",
            e.mode, e.offR, fold::db(w.rho), w.hz, e.lo, e.hi);
  }
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
    {"block-size independence: blocks 1, 7, 64, 256, 333, 4096 and random render bit-identically", blockIndependence, {"rampPerBlock", "blockSteppedD", "expSlewBlockStart"}},
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
    {"routing: ping-pong alternates; standard never crosses; right wakes; M/S levels; M/S folds to M", routing,
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
    {"read speed in [0.25, 2.25], no reverse, backstop, overrun or floor: 4 modes x 5 moves x 3 mods; fallback past 3 s; and seeded time.ms / change / law / glide gestures under modulation (5 ms and 20 ms at 7 Hz, 20 ms at 1.25 Hz, the offset's depth asserted; 4 seeds, 44.1 - 192 kHz)", speedInvariant,
     {"noLawCap", "noFallback", "modFloorClampsBoth"}},
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
    // ---- M3 B4, T-044 (D-050; the offset expression is T-038's, kept) ----
    {"time.mode resolution: sec and hz read time.ms (Hz = 1000 / ms), sync the division at the host tempo; the offsets, then the 12 s fold / clamp / 1 ms floor, against an independent reading of SPEC §3 (44.1, 96 kHz)",
     timeResolution, {"hzCountsAsSync"}},
    {"time.mode flip that moves a time is a control move: from sec, hz and sync it follows time.change (bit-identical to the same move through time.ms / time.div) and is latched at its start, also after a tempo step and on a stale tempo latch; a same-time flip into sync at a tempo step equals a patch synced all along",
     modeFlipControlMove, {"modeFlipSwallowed", "modeFlipIntoSyncSwallowed", "modeFlipAlwaysMove"}},
    {"time.mode sec <-> hz moves nothing: flips every 2500 samples, in a move in flight with time.change flipped, under modulation and a tempo sweep, are bit-identical to none; hz equals sec and follows no tempo (3 routings, each time.change)",
     timeModeFlip, {"hzFlipIsMove", "hzCountsAsSync"}},
    {"time.mode and the tail: sec and hz declare the identical tail, sync that of its sec twin; each sounds until it and is exact zero from it; a sec <-> hz flip while the tail rings changes nothing and the rest comes at the same sample; the offline renderer agrees (44.1, 96 kHz)",
     timeModesTail, {"tailReadsDivInHz", "hzFlipDelaysRest"}},
    {"a time key's send that moves no time is no move: time.ms (moved while synced, or re-sent), a re-sent time.div or time.offL in the sample a tempo step lands, and time.ms automated every 512-sample block under a tempo ramp, change jump at every tempoMode, are bit-identical to no send",
     resendIsNoMove, {"keyResendIsMove", "moveReadsOneLine"}},
    {"the resolved times equal main's offset expression bit for bit over every division, tempo, offset and free time, in each of sec, hz and sync (44.1, 48, 96 kHz)",
     offsetExpressionBits, {"offsetRewrite"}},
    {"a patch in each time.mode (sec, hz, sync): two instances alternately equal a solo render, every block pattern 1 - 4096 renders bit-identically, reset() equals a fresh reset instance, the first repeat lands on its time at 44.1 - 192 kHz",
     timeModesIndependence, {"hzCountsAsSync"}},
    {"click metric (M2 gate, s fixed at 2.25): sweeps, tempo sweeps and single moves under 2.5 and inside the speed invariants, at 44.1 / 48 / 96 / 192 kHz, worst of 8 probe phases; a planted click over",
     clickSweep, {"integerRead", "blockSteppedD", "bareJump", "noLawCap", "fadeLinear"}},
    {"click metric over the probe's phase: a move reversed mid-glide (both laws, time.ms / time.div, 1 and 2 octaves, down and up, 100/200/400 ms; 44.1 / 48 / 96 / 192 kHz) reads under 2.5 at the worst of 8 half-period phases, zero backstop hits",
     reversalClick, {"expSlewOff", "tapeKinkUnbounded"}},
    {"the click gates' probe-phase sweep sweeps: the 8 shifts are distinct and span half a probe period, and they move a step's reading (an onset's, and a reversal's)",
     phaseSweepWorks, {"phaseSweepCollapsed"}},
    {"click metric over the probe's phase: every exponential-law onset (sweeps, single moves, plain onsets; 44.1 / 48 / 96 / 192 kHz) reads under 2.5 at the worst of 8 half-period phases, zero backstop hits",
     onsetClick, {"expSlewOff"}},
    {"the exponential law's 0.1 ms bound binds only just after a move (44.1 - 192 kHz; glides 0.01 - 1 s; onsets, reversals, 1 and 2 ms targets, a glide change; modulated): never in a relaxation",
     expSlewBinds, {"expSlewLong", "expSpuriousRetarget", "expSlewOff"}},
    // ---- M3 B4, T-041 ----
    {"click metric over the probe's phase: the tape law's plain onsets, time.glide changes mid-move, D-006's fallback taken from a moving head, a crossfade-mode retarget of a glide in flight and a retarget mid-fade (both laws), and the modulated moves, each at the crest, the trough and both zero crossings of the LFO (20 -> 2 ms glide then crossfade 6 and 12 ms in, repitch 100 -> 2 ms in both laws, crossfade and jump 100 -> 3 ms at 5 Hz, a long crossfade 100 -> 5 ms; 44.1 / 48 / 96 / 192 kHz) read under 2.5 at the worst of 8 half-period phases, zero backstop hits",
     stepClick, {"tapeKinkUnbounded", "fbFrozenOutgoing", "modAccelUnbounded", "modFloorClampsBoth", "modNoFloorBrake", "modRideKink", "modBoundKeepsSlope", "fbClampStale"}},
    {"a fade's outgoing head: its speed changes by at most the 0.1 ms bound a sample across a modulated crossfade retarget of a shortening glide, both laws (44.1 - 192 kHz): the clamp to the line must not step it (floor contact, the H1 gesture: at most 2 a)",
     outgoingBound, {"fbClampUnsigned", "fbClampStale"}},
    {"the tape law's catch-up: after the cap releases, the speed does not ring (>= 0.9 shortening, <= 1.1 lengthening) and the head never overshoots the model or passes its target by more than 0.1 sample, at 44.1 - 192 kHz (M2 cannot see this)",
     tapeCatchUp, {"tapeNoBrake", "tapeNoTargetBrake"}},
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
    {"state RS-6.11: time.ms saved bit for bit at the values a Hz reading produces (1000 / 3, 1000 / 440, the range's ends), in sec and hz", stateTimeExact, {"stateFloat6g"}},
    {"state A17: a mid-session state in a fresh instance renders as the original after reset()", stateComplete, {"stateKeyMissing"}},
    {"state A19: the worst case fits 2 KiB; 2 KiB + 1 refused first; staging allocates nothing", stateSize, {"stateOversizedAccepted"}},
    {"state A20: saved while ringing, mid-ramp and latched equals saved at rest", stateLiveExcluded, {"stateLatchSerialised"}},
    {"state undo: for each of the 29 keys, the snapshot restores byte- and bit-identically", stateUndo, {"stateKeyMissing"}},
    {"state: every malformed text refused whole (strict number grammar), nothing staged; an alias reads and is written back by name",
     stateRefusals, {"stateAcceptsUnknown", "stateKeyMissing", "stateMissingKeyDefaults"}},
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
    // ---- M3 B3, T-017 ----
    {"presets: every validation refusal (control characters, an empty id or label, Device, unknown, missing, duplicated and out-of-range keys, wrong kinds, another schema, module or version, trailing text, a macroLaw); all or nothing; a slot is refused first, by naming the preset (D-050)",
     presetValidate, {"presetDeviceAccepted"}},
    {"presets A12: no macros, every preset's slots empty (RS-5.1); the bank saves and loads exactly (id, label, description, values incl. the fixed row's keys); re-written byte-identical",
     presetA12,
     {"factoryCarriesSlot"}},
    {"presets A13: one engine through four corners and back, mid-play, equals a fresh load and holds the corner's own values; restored, it renders the same (L and R)", presetA13,
     {"presetPatchOfDropsKey"}},
    // ---- M3 B4, T-021 ----
    {"presets A12: the factory bank is the init plus seven (ids, order), each valid; loaded, its saved state is the preset's own values",
     presetFactoryBank, {"presetPatchOfDropsKey", "factoryLabelTypo"}},
    {"presets A19: every factory preset's saved state is within 2 KiB (reported)", presetFactoryStateSize, {"presetStateOversized"}},
    {"B408 for the bank: each preset's line times, colouring flag and worst 1/3-octave fold, REPORTED never judged", presetFactoryStereo, {}},
    // ---- M3 B3, T-018 ----
    {"A2 reset identity: 2 s of seeded noise at the defaults and a stress patch; reset() mid-ramp, mid-modulation = fresh", a2ResetIdentity, {"resetKeepsModPhase"}},
    {"A4 latency truth: an impulse and a swept sine pass the dry path at the declared 0 samples", a4Latency, {"dryLate"}},
    {"A8 stereo image: L 220 / R 330 Hz, |L - R| kept in standard routing", a8Image, {"monoSum"}},
    {"A10 real time: a scripted session (sweeps, preset moves, corner loads, staged state, Clear, tempo, sleep and wake) allocates nothing", a10Session,
     {"allocInProcess"}},
    {"A14 feedback: in horde's n = 1 cycle at loop gain 1.2, silence in gives exact silence out (defaults, stress)", a14Cycle, {"selfStart"}},
    {"B265 port: horde's click metric matches horde's JS (54caca8) to 1e-9 on eight pinned vectors (click, peak time, floor)", b265Port,
     {"b265HoodHalved"}},
    // ---- M3 B3, T-019 ----
    {"B408 law: per repeat on mono, L and R impulses, standard and mid/side keep rho_out >= rho_in, ping-pong 1/2 with no cross term; ping-pong's overlap at 1 kHz = closed form (D-044)",
     b408Gate, {"stereoInvertsR", "msSideGain2", "pingpongBothLines", "pingpongInvertsR"}},
    {"B408 controls P1 - P5 in rho's scale; P3's 1/3-octave and octave views equal the closed form within 0.1 dB, its notch ~500 Hz",
     b408Controls, {"foldScaleSlip", "foldNotchUnsmoothed", "foldOctaveNarrow"}},
    {"B408 colouring flag: standard routing, 0 < |tL - tR| < 40 ms (whole us); standard-routing offsets comb, ping-pong and mid/side offsets do not", b408Zone,
     {"zoneAnyMode", "zoneAtEqual"}},
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
      {"glideFixedRate", &glideFixedRate}, {"expSlewOff", &expSlewOff}, {"expSlewLong", &expSlewLong}, {"phaseSweepCollapsed", &phaseSweepCollapsed}, {"expSlewBlockStart", &expSlewBlockStart}, {"expSpuriousRetarget", &expSpuriousRetarget},
      {"tapeKinkUnbounded", &tapeKinkUnbounded}, {"tapeNoBrake", &tapeNoBrake}, {"tapeNoTargetBrake", &tapeNoTargetBrake}, {"fbClampUnsigned", &fbClampUnsigned}, {"fbFrozenOutgoing", &fbFrozenOutgoing}, {"fadeLinear", &fadeLinear},
      {"modAccelUnbounded", &modAccelUnbounded}, {"modFloorClampsBoth", &modFloorClampsBoth}, {"modNoFloorBrake", &modNoFloorBrake}, {"modRideKink", &modRideKink}, {"modBoundKeepsSlope", &modBoundKeepsSlope}, {"fbClampStale", &fbClampStale},
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

      {"presetDeviceAccepted", &presetDeviceAccepted},
      {"resetKeepsModPhase", &resetKeepsModPhase}, {"dryLate", &dryLate}, {"monoSum", &monoSum}, {"selfStart", &selfStart}, {"b265HoodHalved", &b265HoodHalved},
      {"stereoInvertsR", &stereoInvertsR}, {"pingpongBothLines", &pingpongBothLines}, {"foldScaleSlip", &foldScaleSlip},
      {"foldOctaveNarrow", &foldOctaveNarrow}, {"foldNotchUnsmoothed", &foldNotchUnsmoothed}, {"zoneAnyMode", &zoneAnyMode}, {"zoneAtEqual", &zoneAtEqual},
      {"pingpongInvertsR", &pingpongInvertsR},
      {"presetStateOversized", &presetStateOversized}, {"presetPatchOfDropsKey", &presetPatchOfDropsKey},
      {"factoryLabelTypo", &factoryLabelTypo}, {"factoryCarriesSlot", &factoryCarriesSlot},
      {"offsetRewrite", &offsetRewrite}, {"stateMissingKeyDefaults", &stateMissingKeyDefaults},
      {"hzCountsAsSync", &hzCountsAsSync}, {"hzFlipIsMove", &hzFlipIsMove}, {"modeFlipSwallowed", &modeFlipSwallowed},
      {"tailReadsDivInHz", &tailReadsDivInHz}, {"modeFlipIntoSyncSwallowed", &modeFlipIntoSyncSwallowed},
      {"modeFlipAlwaysMove", &modeFlipAlwaysMove}, {"keyResendIsMove", &keyResendIsMove}, {"moveReadsOneLine", &moveReadsOneLine}, {"hzFlipDelaysRest", &hzFlipDelaysRest},
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

// ---- docs/admission.md (T-018, D-043): horde's admission rows A1 - A23, each
// mapped to the checks that evidence it in THIS suite, their plants, and the
// result of this run; or "horde's" (the slot's side) or "not declared". Written
// by `echo_tests --admission-out <file>`; ./verify fails if the committed file
// differs, so the doc can never claim a row a passing check does not back.
struct AdmissionRow {
  const char* id;
  const char* what;
  std::vector<bool (*)(Detail&)> checks;
  const char* also;  // other evidence, or the reason the row is not Echo's
};
const std::vector<AdmissionRow>& admissionRows() {
  static const std::vector<AdmissionRow> rows = {
      {"A1", "Declarations", {abiProperties, abiParamClasses}, "`check_manifest.py` (C++ table = params.json) and `manifest_lock.py` (the lock) in ./verify"},
      {"A2", "Reset identity", {a2ResetIdentity, determinism, timeModesIndependence}, ""},
      {"A3", "Block independence", {blockIndependence, timeModesIndependence}, ""},
      {"A4", "Latency truth", {a4Latency, abiProperties}, "latency 0, no residual (nothing is buffered ahead)"},
      {"A5", "Silence and tails", {silence, tailExactZero, tailFormula, tailAfterChange, tailNearUnity, restNeverCuts, timeModesTail}, ""},
      {"A6", "I/O gain (B435 tests 1-7)", {ioTest1Unity, ioGainLaw, ioTest3Opens, ioTest4Kept, ioTest5Placement, ioTest6Slot, ioTest7Latches, metersTaps},
       "no limiter face: the post-limiter test does not apply"},
      {"A7", "Off", {}, "horde's: \"off\" is the router's (RS-4.8); Echo has no bypass key"},
      {"A8", "Stereo image", {a8Image, routing}, "changes_image is declared (ping-pong and mid/side move the image)"},
      {"A9", "Non-finite", {finiteInput, loopRepair}, ""},
      {"A10", "Real-time", {noAlloc, a10Session, ftzSet, ftzRestored},
       "locks and syscalls by review only: no RealtimeSanitizer on this toolchain, and the engine calls nothing that locks"},
      {"A11", "Parameter classes",
       {switchesClickFree, crossfadeEqualGain, jumpIsShortFade, deferral, modSeeded, clickSweep, reversalClick, onsetClick, phaseSweepWorks, expSlewBinds, modeFlipControlMove, timeModeFlip, timeResolution, resendIsNoMove, stepClick, outgoingBound, tapeCatchUp, loadLeavesDevice, presetValidate, b265Port},
       "structural: every flip key's switch declaration in params.json (`check_manifest.py`, with planted bare and length-less switches); the B265 reading REPORTED, never judged (`.harness/b265-report.txt`)"},
      {"A12", "Macros", {presetA12, presetValidate, presetFactoryBank},
       "macros are not offered (RS-5.1; D-050): every preset's slots are empty and a slot is refused by name; the row still round-trips every preset's values, the fixed row's keys included, byte-identical, and the factory bank (the init plus seven) loads as its own values (T-021); B408 for the bank is stated in the run's REPORT lines"},
      {"A13", "Corner presets", {presetA13}, "the horde preset's own save and reload is horde's"},
      {"A14", "Feedback", {a14Cycle}, "refusing a min_block > 1 module is the slot's; Echo declares 1"},
      {"A15", "Determinism", {determinism, timeModesIndependence}, ""},
      {"A16", "Sleep and wake", {sleepTwin, floorAndWake}, ""},
      {"A17", "State completeness", {stateComplete}, ""},
      {"A18", "Restore exactness", {stateExact, stateTimeExact}, "time.ms bit for bit at the values a Hz reading produces (RS-6.11)"},
      {"A19", "State size", {stateSize, presetFactoryStateSize}, "every factory preset's saved state within 2 KiB (T-021)"},
      {"A20", "Live-state exclusion", {stateLiveExcluded}, ""},
      {"A21", "Morph position", {}, "not declared (RS-4.10; SPEC §16.6)"},
      {"A22", "Note context", {}, "not declared (RS-4.11; SPEC §16.6, D-034)"},
      {"A23", "Declared actions", {clearA23, clearFade, clearNoRestart, clearState}, "one action, Clear (SPEC §16.7)"},
  };
  return rows;
}

std::string mdCell(std::string t) {  // a check name inside a Markdown table cell: '|' would end the cell
  std::string o;
  for (char c : t) o += (c == '|') ? std::string("\\|") : std::string(1, c);
  return o;
}

std::string admissionDoc(const std::vector<bool>& passed) {
  std::string o =
      "# Echo — admission self-test (horde's rack-slot contract §2.2, rows A1 – A23)\n\n"
      "GENERATED by `echo_tests --admission-out docs/admission.md` (core/tests/core_tests.cpp, T-018, D-043); never edit by hand.\n"
      "`./verify` fails if this file differs from a fresh run's, so a row is only ever claimed by a check that passed in that run.\n"
      "Each check is proven on its planted faults by `echo_plants` (charter §Domain). \"horde's\" rows are the slot's side.\n\n"
      "| row | what | evidence (checks in `echo_tests`) | planted faults | result |\n|---|---|---|---|---|\n";
  for (const auto& r : admissionRows()) {
    std::string checks, plants;
    bool all = !r.checks.empty();
    for (auto fn : r.checks) {
      for (std::size_t i = 0; i < kChecks.size(); ++i)
        if (kChecks[i].fn == fn) {
          checks += std::string(checks.empty() ? "" : "<br>") + mdCell(kChecks[i].name);
          for (const char* pl : kChecks[i].plants)
            if (plants.find(std::string("`") + pl + "`") == std::string::npos) plants += std::string(plants.empty() ? "" : ", ") + "`" + pl + "`";
          all = all && passed[i];
        }
    }
    if (*r.also) checks += std::string(checks.empty() ? "" : "<br>") + "*" + mdCell(r.also) + "*";
    const std::string result = r.checks.empty() ? (std::string(r.also).rfind("not declared", 0) == 0 ? "not declared" : "horde's") : (all ? "PASS" : "**FAIL**");
    o += "| " + std::string(r.id) + " | " + r.what + " | " + checks + " | " + (plants.empty() ? "—" : plants) + " | " + result + " |\n";
  }
  char t[2048];
  std::snprintf(t, sizeof t,
                "\n## Fidelity thresholds (1.0 bar row 10: \"its own fidelity suite with measured thresholds\")\n\n"
                "Printed from the constants the checks read (core_tests.cpp, namespace tol): a changed tolerance changes this table.\n\n"
                "| claim | threshold | check |\n|---|---|---|\n"
                "| time accuracy | repeats on their sample; read centroid within %g sample of the delay, whole and fractional | timeAccuracy |\n"
                "| generation ratio | gen2/gen1 within %g of g(1 + R)/2; later generations within %g of g | generationRatio |\n"
                "| loop bound | measured max abs(H_loop) <= 1 + %g | loopBound |\n"
                "| sample-rate independence | time exact; loop filter within %g dB of 48 kHz, 44.1 – 192 kHz | srIndependence |\n"
                "| block-size independence | bit-identical, blocks 1 … 4096 | blockIndependence |\n"
                "| determinism, reset, state | bit-identical | determinism, a2ResetIdentity, stateComplete, stateExact |\n"
                "| repitch | output pitch within %g cent; the tape model within %g sample and %g cents | repitchCents, tapeLaw |\n"
                "| moving-time clicks | M2 metric <= %g at s = 2.25, zero backstop hits (D-023); reversals and exponential onsets at the worst of 8 probe phases (D-049) | clickSweep, reversalClick, onsetClick |\n"
                "| Clear's fade | within %g of the no-Clear output times the 20 ms ramp | clearFade, clearLiveWet |\n",
                tol::kCentroidSamples, tol::kGen2Ratio, tol::kGenLater, tol::kLoopGainSlack, tol::kSrLoopDb, tol::kRepitchCents, tol::kTapeSamples,
                tol::kTapeCents, kClickProposed, tol::kClearFade);
  o += t;
  return o;
}

int main(int argc, char** argv) {
  // --threads N: the click gates' render pool (at most the hardware's; 1 = serial). The results are the same for any N.
  for (int a = 1; a + 1 < argc; ++a)
    if (std::string_view(argv[a]) == "--threads") g_threads = std::atoi(argv[a + 1]);
  // --click-report: print every click reading (threshold lifted), clean and,
  // in echo_plants, under each click plant. The numbers the threshold is set on.
  if (argc > 1 && std::string_view(argv[1]) == "--click-report") {
    kClickMax = kClickMaxJump = 1e300;
    g_report = true;
    Detail d;
    clickSweep(d);
    phaseSweepWorks(d);
    reversalClick(d);
    onsetClick(d);
    stepClick(d);
    std::printf("  SPEED STRESS %s\n", speedStress(d) ? "clean" : d.c_str());
    outgoingBound(d);
    tapeCatchUp(d);
    expSlewBinds(d);
#ifdef ECHO_PLANTS
    for (const char* p : {"bareJump", "integerRead", "blockSteppedD", "noLawCap", "expSlewOff", "tapeKinkUnbounded", "tapeNoBrake", "tapeNoTargetBrake", "fbClampUnsigned", "fbFrozenOutgoing", "fadeLinear"}) {
      std::printf("  -- under plant %s\n", p);
      *plantFlag(p) = true;
      clickSweep(d);
      reversalClick(d);
      onsetClick(d);
      stepClick(d);
      outgoingBound(d);
      tapeCatchUp(d);
      *plantFlag(p) = false;
    }
    // T-042's plants act on the modulated rows alone (stepClick, and the stress, which reports as a pass or a fail)
    for (const char* p : {"modAccelUnbounded", "modFloorClampsBoth", "modNoFloorBrake", "modRideKink", "modBoundKeepsSlope", "fbClampStale"}) {
      std::printf("  -- under plant %s\n", p);
      *plantFlag(p) = true;
      stepClick(d);
      std::printf("  SPEED STRESS %s\n", speedStress(d) ? "clean" : d.c_str());
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
                        "clearFreezesHeads", "clearFreezesWet",
                        "presetDeviceAccepted",
                        "resetKeepsModPhase", "dryLate", "monoSum", "selfStart", "b265HoodHalved",
                        "stereoInvertsR", "pingpongBothLines", "foldScaleSlip", "foldOctaveNarrow", "foldNotchUnsmoothed",
                        "zoneAnyMode", "zoneAtEqual", "pingpongInvertsR", "presetStateOversized", "presetPatchOfDropsKey", "factoryLabelTypo", "factoryCarriesSlot",
                        "offsetRewrite", "stateMissingKeyDefaults", "hzCountsAsSync", "hzFlipIsMove", "modeFlipSwallowed", "tailReadsDivInHz", "moveReadsOneLine", "hzFlipDelaysRest", "modeFlipIntoSyncSwallowed", "modeFlipAlwaysMove", "keyResendIsMove", "expSlewOff", "expSlewLong", "phaseSweepCollapsed", "expSlewBlockStart", "expSpuriousRetarget",
                        "tapeKinkUnbounded", "tapeNoBrake", "tapeNoTargetBrake", "fbClampUnsigned", "fbFrozenOutgoing", "fadeLinear",
                        "modAccelUnbounded", "modFloorClampsBoth", "modNoFloorBrake", "modRideKink", "modBoundKeepsSlope", "fbClampStale"}) {
    bool guarded = false;
    for (const auto& c : kChecks)
      for (const char* p : c.plants) guarded = guarded || std::string_view(p) == n;
    if (!plantFlag(n) || !guarded) { std::printf("  FAIL  plant %s is %s\n", n, plantFlag(n) ? "guarded by no check" : "not in the plant table"); ++fails; }
  }
#endif
  std::vector<bool> passed(kChecks.size(), false);
  for (const auto& c : kChecks) {
    Detail d;
    const bool ok = c.fn(d);
    passed[static_cast<std::size_t>(&c - kChecks.data())] = ok;
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
  b265Report();
  for (int a = 1; a + 1 < argc; ++a)
    if (std::string_view(argv[a]) == "--admission-out") {
      if (std::FILE* f = std::fopen(argv[a + 1], "wb")) {
        const std::string doc = admissionDoc(passed);
        std::fwrite(doc.data(), 1, doc.size(), f);
        std::fclose(f);
      } else {
        std::printf("  FAIL  cannot write %s\n", argv[a + 1]);
        ++fails;
      }
    }
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
