// fold.h — the B408 mono-fold metric (D-010; SPEC §4.1 as ruled by D-044),
// TEST- AND TOOL-ONLY. echo_tests gates on it (b408Gate, b408Controls,
// b408Zone) and echo_stereo_report prints docs/stereo-report.md from it: one
// metric, so the report can never state a reading the gate does not compute.
//
// THE READING. With M = (L+R)/2 the mono fold,
//   rho = E_M / mean(E_L, E_R) = (1 + r)/2,   r = 2 sum(LR) / (sum L^2 + sum R^2).
// A centred source reads 1 (0 dB); a hard-panned or decorrelated one 1/2
// (-3 dB); total cancellation 0. THE SCALE IS LOAD-BEARING (D-044): prior-art
// §5.3 stated P3 ("rho ~ 1") and P5 ("0 dB per repeat") on the 1 + r scale, a
// factor of two off this one, and the ratified gate inherited the slip
// (ping-pong would have failed it by construction). The plant foldScaleSlip
// divides by the SUM of the channel energies; control P2 catches it.
//
// WHAT IT MEASURES THROUGH. Every Echo render comes from the one offline
// renderer (echo/ir_render.h, D-011): no second render path. Its output equals
// a plain Echo render bit for bit on every input used here (equalsPlainRender
// in echo_ir_tests; T-019's renderer criterion, M4's plant irSkipsOutputFilter).
//
// THREE VIEWS, because a broadband ratio cannot see a comb between time-
// separated taps (their cross term is zero; prior-art §5.2): per repeat
// generation (time windows), per octave band and 1/3-octave-smoothed (both
// from the spectrum, by Parseval: a band's energy is its bins' energy, so no
// band filter rings into the reading).
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <vector>

#include "echo/ir_render.h"
#include "echo/plants.h"

namespace fold {

using Vec = std::vector<double>;
inline constexpr double kPi = 3.14159265358979323846;

// The colouring zone (SPEC §3.2's "about 40 ms", ruled a strict 40 ms and
// STEREO MODE ONLY by D-044: measured, ping-pong's taps never coincide and
// mid/side's fold is exactly the M line, so neither combs under an offset).
// 40 ms is a WARNING CONVENTION (the precedence-effect range), not a boundary
// this metric shows: a stereo offset combs the fold at low frequency at any
// size (-7.9 dB at 31 Hz at 50 ms; critic T-019 M2, ruled by poll).
inline constexpr long kZoneUs = 40000;
// Octave centres 1000 * 2^k, 31.25 Hz to 16 kHz (prior-art §5.3's 31.5 - 16 k),
// edges at 2^(+-1/2); the notch search on a 1/24-octave grid over the same span,
// each point the energy ratio over its 1/3 octave, 2^(+-1/6).
inline constexpr int kOctaves = 10;
inline constexpr double kLowHz = 31.25, kHighHz = 16000.0;
inline constexpr int kGridPerOctave = 24;
inline constexpr int kMaxGenerations = 8;
inline constexpr double kGenFloor = 1e-12;  // a generation below gen 1 by 120 dB is not read

inline constexpr int kStereo = 0, kPingPong = 1, kMidSide = 2;  // route.mode's steps (SPEC §4)

// ---- the energies ----------------------------------------------------------
struct Energy {
  double l = 0, r = 0, m = 0, lr = 0;
};
inline Energy energy(const Vec& L, const Vec& R, long a, long b) {
  Energy e;
  a = std::max<long>(a, 0);
  b = std::min<long>(b, static_cast<long>(std::min(L.size(), R.size())));
  for (long i = a; i < b; ++i) {
    const double x = L[static_cast<std::size_t>(i)], y = R[static_cast<std::size_t>(i)], m = (x + y) / 2;
    e.l += x * x;
    e.r += y * y;
    e.m += m * m;
    e.lr += x * y;
  }
  return e;
}
inline Energy energy(const Vec& L, const Vec& R) { return energy(L, R, 0, static_cast<long>(L.size())); }
inline double rho(double em, double el, double er) {
  const double den = echo::plant::foldScaleSlip ? el + er : (el + er) / 2;
  return den > 0 ? em / den : std::numeric_limits<double>::quiet_NaN();  // NaN: silent (nothing to fold)
}
inline double rho(const Energy& e) { return rho(e.m, e.l, e.r); }
inline double corr(const Energy& e) {  // r, the normalised cross term
  return e.l + e.r > 0 ? 2 * e.lr / (e.l + e.r) : std::numeric_limits<double>::quiet_NaN();
}
inline double db(double x) { return 10 * std::log10(x); }

// ---- per repeat generation ---------------------------------------------------
// For an equal-time patch (tL == tR == T samples) generation n arrives at n*T;
// its window is [n*T - T/4, (n+1)*T - T/4): the impulse with its filter ring, or
// a noise burst of at most T/2, sits inside with a quarter period to spare.
struct Gen {
  int n = 0;
  double rho = 0, corr = 0, energy = 0;
};
inline std::vector<Gen> generations(const Vec& L, const Vec& R, double T) {
  std::vector<Gen> g;
  double first = 0;
  for (int n = 1; n <= kMaxGenerations; ++n) {
    const long a = std::lround(n * T - T / 4), b = std::lround((n + 1) * T - T / 4);
    if (a >= static_cast<long>(L.size())) break;
    const Energy e = energy(L, R, a, b);
    const double tot = e.l + e.r;
    if (n == 1) first = tot;
    if (!(tot > kGenFloor * first) || first <= 0) break;
    g.push_back({n, rho(e), corr(e), tot});
  }
  return g;
}

// ---- the spectrum --------------------------------------------------------------
// Iterative radix-2. Each twiddle is computed once, from its own exact angle
// (no recurrence, whose error grows with the transform), into a table every
// stage strides through, and the butterflies run block by block so a stage
// walks memory in order (a k-outer loop strides the whole array per twiddle:
// ~4x slower at 2^19). Deterministic; the tool builds with -ffp-contract=off so
// the committed report is the same on GCC and clang.
inline void fft(std::vector<std::complex<double>>& a) {
  const std::size_t n = a.size();
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  // The table for the last size, kept: the report's transforms nearly all share
  // one size, and 2^18 cos/sin pairs per transform cost more than the butterflies.
  // (Not thread-safe; nothing here runs on more than one thread.)
  static std::vector<std::complex<double>> w;
  if (w.size() != n / 2) {
    w.assign(n / 2, {});
    for (std::size_t k = 0; k < n / 2; ++k) {
      const double ang = -2 * kPi * static_cast<double>(k) / static_cast<double>(n);
      w[k] = {std::cos(ang), std::sin(ang)};
    }
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const std::size_t half = len / 2, step = n / len;
    for (std::size_t i = 0; i < n; i += len)
      for (std::size_t k = 0; k < half; ++k) {
        const std::complex<double> u = a[i + k], v = a[i + k + half] * w[k * step];
        a[i + k] = u + v;
        a[i + k + half] = u - v;
      }
  }
}

// The one-sided power spectra of L, R and the fold M over the first `len`
// samples (the rest must be zero: a render past its rest point is exact zero by
// construction, SPEC §10, so trimming there changes no reading), zero-padded to
// a power of two of at least 2^16 (0.73 Hz bins at 48 kHz, so the lowest
// 1/3 octave holds ~10 bins whatever the render's length). The fold's spectrum
// is (FFT L + FFT R)/2 by linearity: two transforms, not three.
struct Spectra {
  Vec l, r, m;
  double binHz = 0;
};
inline Spectra spectra(const Vec& L, const Vec& R, double sr, std::size_t len = 0) {
  if (len == 0 || len > L.size()) len = L.size();
  std::size_t n = std::size_t{1} << 16;
  while (n < len) n <<= 1;
  std::vector<std::complex<double>> a(n), b(n);
  for (std::size_t i = 0; i < len; ++i) a[i] = L[i], b[i] = R[i];
  fft(a), fft(b);
  Spectra s;
  s.binHz = sr / static_cast<double>(n);
  for (std::size_t k = 0; k <= n / 2; ++k) {
    s.l.push_back(std::norm(a[k]));
    s.r.push_back(std::norm(b[k]));
    s.m.push_back(std::norm((a[k] + b[k]) / 2.0));
  }
  return s;
}
// rho over the bins in [lo, hi] Hz (clipped to Nyquist).
inline double bandRho(const Spectra& s, double lo, double hi) {
  const auto top = static_cast<long>(s.m.size()) - 1;
  const long a = std::max<long>(1, static_cast<long>(std::ceil(lo / s.binHz))), b = std::min<long>(top, static_cast<long>(std::floor(hi / s.binHz)));
  double em = 0, el = 0, er = 0;
  for (long k = a; k <= b; ++k) em += s.m[static_cast<std::size_t>(k)], el += s.l[static_cast<std::size_t>(k)], er += s.r[static_cast<std::size_t>(k)];
  return rho(em, el, er);
}
inline double octaveCentre(int k) { return 1000.0 * std::pow(2.0, k - 5); }
inline std::vector<double> octaves(const Spectra& s) {
  const double half = echo::plant::foldOctaveNarrow ? 0.25 : 0.5;  // the plant measures half-octave bands
  std::vector<double> o;
  for (int k = 0; k < kOctaves; ++k) o.push_back(bandRho(s, octaveCentre(k) * std::pow(2.0, -half), octaveCentre(k) * std::pow(2.0, half)));
  return o;
}
inline double gridHz(int j) { return kLowHz * std::pow(2.0, static_cast<double>(j) / kGridPerOctave); }
inline int gridPoints() { return 9 * kGridPerOctave + 1; }  // 31.25 Hz .. 16 kHz, nine octaves
// The 1/3-octave-smoothed fold at grid point j. The plant reads the one bin
// nearest the point: an unsmoothed comb shows its raw null (D-010 rejected
// gating on "a raw narrowband minimum").
inline double smoothedAt(const Spectra& s, int j) {
  const double f = gridHz(j);
  if (echo::plant::foldNotchUnsmoothed) {
    const auto k = static_cast<std::size_t>(std::lround(f / s.binHz));
    return rho(s.m[k], s.l[k], s.r[k]);
  }
  return bandRho(s, f * std::pow(2.0, -1.0 / 6), f * std::pow(2.0, 1.0 / 6));
}
struct Notch {
  double rho = 0, hz = 0;
};
inline Notch worstNotch(const Spectra& s) {
  Notch w{std::numeric_limits<double>::infinity(), 0};
  for (int j = 0; j < gridPoints(); ++j) {
    const double v = smoothedAt(s, j);
    if (v < w.rho) w = {v, gridHz(j)};  // NaN (a silent band) never compares less
  }
  return w;
}

// ---- the law (D-044) -------------------------------------------------------------
// Unintended cancellation (Q-A4) is the output's fold losing more than the
// input's. Stereo and mid/side: rho_out >= rho_in per generation. Ping-pong
// sums the input to mono and hard-pans every repeat by design, so its law is
// exact instead: no cross term, rho = 1/2.
// GATED ON EXACT INPUTS ONLY (mono, left and right impulses: rho_in 1, 1/2, 1/2),
// never on noise (re-ruled by poll after critic T-019 H1): a finite noise burst's
// rho moves under the loop's filtering with no fault present, by more than any
// independent-sample margin (the filters shrink the effective sample count every
// pass; 15 of 80 seeds broke 3/(2 sqrt N)). Noise is reported, not judged.
inline constexpr double kExact = 1e-12;  // rounding only: these readings are exact by construction
// Whether generation g keeps the law for its mode.
inline bool genPasses(int mode, const Gen& g, double rhoIn) {
  if (mode == kPingPong) return std::fabs(g.corr) <= kExact;
  return g.rho >= rhoIn - kExact;
}

// The repeats' fold where they overlap. Per generation, ping-pong's sides never
// share a window, so a polarity fault on one side (r stays 0) is invisible to the
// law above (critic T-019 M1). On sustained material they do overlap: at any
// frequency f that is a whole multiple of 1/T, an equal-time ping-pong response is
// H_L = 1/(1 - g^2), H_R = g/(1 - g^2) (loop filter off, the DC blocker's phase
// aside), so the fold there is (1 + g)^2 / (2 (1 + g^2)); an inverted side reads
// (1 - g)^2 / (2 (1 + g^2)). One exact-frequency DFT bin of the rendered response.
inline std::complex<double> dftAt(const Vec& x, double f, double sr) {
  std::complex<double> acc = 0;
  for (std::size_t i = 0; i < x.size(); ++i) {
    if (x[i] == 0.0) continue;
    const double ang = -2 * kPi * f * static_cast<double>(i) / sr;
    acc += x[i] * std::complex<double>(std::cos(ang), std::sin(ang));
  }
  return acc;
}
inline double foldAt(const Vec& L, const Vec& R, double f, double sr) {
  const auto a = dftAt(L, f, sr), b = dftAt(R, f, sr);
  return rho(std::norm((a + b) / 2.0), std::norm(a), std::norm(b));
}
inline double pingPongFoldClosedForm(double g) { return (1 + g) * (1 + g) / (2 * (1 + g * g)); }

// The colouring flag (SPEC §3.2 as ruled, D-044): stereo mode, the two resolved
// times different and under 40 ms apart. Compared in whole microseconds: the
// difference of two times already rounded to doubles lands either side of 40 ms
// by an ulp (0.29 - 0.25 < 0.04 < 0.54 - 0.5; critic T-019 L1), so "exactly
// 40 ms" would flag or not by accident.
inline bool colouringZone(int mode, double tL, double tR) {
  const long us = std::lround(std::fabs(tL - tR) * 1e6);
  if (!echo::plant::zoneAnyMode && mode != kStereo) return false;
  return (echo::plant::zoneAtEqual || us > 0) && us < kZoneUs;
}

// ---- rendering through the one renderer ---------------------------------------------
inline echo::IrPatch patchWith(std::initializer_list<std::pair<const char*, double>> kv) {
  echo::IrPatch p = echo::irDefaults();
  for (const auto& [k, v] : kv)
    for (std::size_t i = 0; i < echo::kParams.size(); ++i)
      if (echo::kParams[i].key == k) p[i] = v;
  return p;
}
inline std::size_t keyIndex(const char* k) {
  for (std::size_t i = 0; i < echo::kParams.size(); ++i)
    if (echo::kParams[i].key == k) return i;
  return echo::kParams.size();
}
// What one stimulus does through one patch: the wet (mix forced to 100 %: the dry
// path is the input exactly, so the output's dry fold IS the input's) and the
// input itself (the same request at mix 0, which is exactly the dry, SPEC §8).
struct Take {
  Vec L, R, inL, inR;
  echo::IrInfo info;
  long noiseN = 0;
  bool ok = false;
};
// length > 0: that many samples; 0: just enough for the generation windows (the
// gate); < 0: to the declared tail (prior-art §5.3), capped at `cap`.
inline Take take(echo::IrPatch p, echo::IrInput in, double sr, std::int64_t length, std::int64_t cap = std::int64_t{1} << 20,
                 std::uint32_t seed = 1) {
  Take t;
  p[keyIndex("mix")] = 100;
  echo::IrRequest r;
  r.patch = p;
  r.sampleRate = sr;
  r.input = in;
  r.noiseSeed = seed;
  r.length = 1;
  const auto probe = echo::renderIr(r);  // lands the patch: the resolved times and the declared tail
  if (!probe.ok) return t;
  const double T = std::min(probe.info.times[0], probe.info.times[1]) * sr;
  t.noiseN = in == echo::IrInput::Noise ? std::lround(T / 2) : 0;
  r.noiseLength = t.noiseN;
  r.length = length > 0 ? length : length == 0 ? std::lround((kMaxGenerations + 1.5) * T) : std::min<std::int64_t>(probe.info.tail, cap);
  auto w = echo::renderIr(r);
  r.patch[keyIndex("mix")] = 0;
  r.length = std::max<std::int64_t>(1, t.noiseN);
  auto d = echo::renderIr(r);
  if (!w.ok || !d.ok) return t;
  t.L = std::move(w.L), t.R = std::move(w.R), t.inL = std::move(d.L), t.inR = std::move(d.R);
  t.info = w.info;
  t.ok = true;
  return t;
}

// The report's patches (docs/stereo-report.md); the gate runs on the gated ones.
// Here, not in the tool, so the suite gates exactly the patches the report shows.
struct NamedPatch {
  const char* name;  // short, for the tables
  const char* what;  // the legend
  echo::IrPatch p;
  bool gated;  // equal times: the law applies per generation (D-044)
};
inline std::vector<NamedPatch> reportPatches() {
  return {
      {"defaults", "the manifest defaults: 1/4 synced at 120 bpm (500 ms), feedback 35 %, loop filter 60 Hz – 9 kHz", patchWith({}), true},
      {"dark", "300 ms free, feedback 70 %, loop low-pass 3 kHz, output filter on at 200 Hz – 6 kHz, 2 ms of sine modulation (spread 0)",
       patchWith({{"time.sync", 0}, {"time.ms", 300}, {"fb.amount", 70}, {"filt.loop.lpHz", 3000}, {"filt.out.on", 1}, {"filt.out.hpHz", 200},
                  {"filt.out.lpHz", 6000}, {"mod.depthMs", 2}}),
       true},
      {"+1 ms", "the defaults with time.offR +0.2 %: the right line 1 ms later (inside the colouring zone)", patchWith({{"time.offR", 0.2}}), false},
      {"+20 ms", "the defaults with time.offR +4 %: 20 ms later (inside the zone)", patchWith({{"time.offR", 4}}), false},
      {"+50 ms", "the defaults with time.offR +10 %: 50 ms later (outside the zone)", patchWith({{"time.offR", 10}}), false},
  };
}

}  // namespace fold
