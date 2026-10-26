// b265.h — horde's B265 click metric, ported EXACTLY (TEST-ONLY; ROADMAP T-018,
// D-043; contract row A11 as amended by §12.1: the click is REPORTED, never judged).
//
//   source: horde docs/design/fx-chain-morph-lab-r3.html at commit 54caca8
//           (its last change; the metric's lines are unchanged since e97b9ff,
//           the commit Bulwark ported). Lines 1802 - 1834: CLK_*, sliceRms,
//           impulsiveness, clickOf.
//
// WHY EXACT. A reading is a number ON THIS METRIC: a "similar" detector (another
// slice length, neighbourhood or median rule) would make Echo's readings mean
// something else. Constants, loop order and the empty-neighbourhood fallback are
// the lab's, line for line. The proof is the b265Port check: horde's own
// JavaScript, run under node on five seeded vectors (traces/2026-10-26-t018-
// admission.md), gave the readings pinned there, and this port must match them
// to 1e-9 dB. Echo's oracle never reads horde's tree at run time.
//
// THE METRIC. The RMS of the second difference per 2 ms slice; each slice in dB
// against the median of its +-16 neighbours; a transition's click is the worst
// slice in the 1 s after the change, less the worst steady slice in the 0.25 s
// before it and after the span, floored at 0.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

#include "echo/plants.h"

namespace b265 {

inline constexpr double kSliceS = 0.002, kSteadyS = 0.25, kSpanS = 1.0;
inline int hood() { return echo::plant::b265HoodHalved ? 8 : 16; }  // CLK_HOOD (the plant halves it)

struct Slices {
  std::vector<double> e;
  long W = 0;
};

// sliceRms: the first two samples have no second difference: skipped, but still
// divided by W, as the lab does. JS Math.round and std::lround agree on positives.
inline Slices sliceRms(const std::vector<double>& y, double sr) {
  Slices s;
  s.W = std::max<long>(4, std::lround(kSliceS * sr));
  const long n = static_cast<long>(y.size()) / s.W;
  s.e.assign(static_cast<std::size_t>(n), 0.0);
  for (long k = 0; k < n; ++k) {
    double sum = 0;
    for (long i = k * s.W; i < (k + 1) * s.W; ++i) {
      if (i < 2) continue;
      const double d = y[static_cast<std::size_t>(i)] - 2 * y[static_cast<std::size_t>(i - 1)] + y[static_cast<std::size_t>(i - 2)];
      sum += d * d;
    }
    s.e[static_cast<std::size_t>(k)] = std::sqrt(sum / static_cast<double>(s.W));
  }
  return s;
}

inline std::vector<double> impulsiveness(const std::vector<double>& e) {
  const long n = static_cast<long>(e.size());
  std::vector<double> r(static_cast<std::size_t>(n), 0.0);
  double gmax = 0;
  for (double v : e)
    if (v > gmax) gmax = v;
  const double tiny = std::max(1e-12, 1e-6 * gmax);
  std::vector<double> buf;
  for (long k = 0; k < n; ++k) {
    buf.clear();
    for (long j = std::max<long>(0, k - hood()); j <= std::min<long>(n - 1, k + hood()); ++j)
      if (j != k) buf.push_back(e[static_cast<std::size_t>(j)]);
    std::sort(buf.begin(), buf.end());
    const double med = buf.empty() ? e[static_cast<std::size_t>(k)] : buf[buf.size() >> 1];
    r[static_cast<std::size_t>(k)] = 20 * std::log10(std::max(tiny, e[static_cast<std::size_t>(k)]) / std::max(tiny, med));
  }
  return r;
}

struct Click {
  double db = 0, at = 0, floor = 0;
};

inline Click clickOf(const std::vector<double>& y, double sr, long t0) {
  const Slices s = sliceRms(y, sr);
  const std::vector<double> r = impulsiveness(s.e);
  const long n = static_cast<long>(r.size()), k0 = t0 / s.W;
  const long ks = std::lround(kSteadyS * sr / static_cast<double>(s.W)), kspan = std::lround(kSpanS * sr / static_cast<double>(s.W));
  double pre = 0, post = 0, peak = -HUGE_VAL, at = 0;
  for (long k = std::max<long>(0, k0 - ks); k < k0; ++k) pre = std::max(pre, r[static_cast<std::size_t>(k)]);
  for (long k = k0 + kspan; k < std::min(n, k0 + kspan + ks); ++k) post = std::max(post, r[static_cast<std::size_t>(k)]);
  for (long k = k0; k < std::min(n, k0 + kspan); ++k)
    if (r[static_cast<std::size_t>(k)] > peak) {
      peak = r[static_cast<std::size_t>(k)];
      at = static_cast<double>((k - k0) * s.W) / sr;
    }
  return {std::max(0.0, peak - std::max(pre, post)), at, std::max(pre, post)};
}

// horde's two pinned probes (§12.1), at -6 dBFS: the lab's chord (sines at 55,
// 110, 130.81 and 164.81 Hz, equal amplitude, the sum normalised to peak at
// -6 dBFS) and a 1 kHz sine.
inline std::vector<double> chordProbe(long n, double sr) {
  const double A = std::pow(10.0, -6.0 / 20.0), kPi = 3.14159265358979323846;
  std::vector<double> y(static_cast<std::size_t>(n), 0.0);
  for (double f : {55.0, 110.0, 130.81, 164.81})
    for (long i = 0; i < n; ++i) y[static_cast<std::size_t>(i)] += std::sin(2 * kPi * f * static_cast<double>(i) / sr);
  double pk = 0;
  for (double v : y) pk = std::max(pk, std::fabs(v));
  for (double& v : y) v = v * A / pk;
  return y;
}
inline std::vector<double> sineProbe(long n, double sr, double hz = 1000.0) {
  const double A = std::pow(10.0, -6.0 / 20.0), kPi = 3.14159265358979323846;
  std::vector<double> y(static_cast<std::size_t>(n));
  for (long i = 0; i < n; ++i) y[static_cast<std::size_t>(i)] = A * std::sin(2 * kPi * hz * static_cast<double>(i) / sr);
  return y;
}

}  // namespace b265
