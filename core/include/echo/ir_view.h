// echo/ir_view.h — the display model: what the screen draws, computed from a
// render of the one IR renderer (ROADMAP T-032; D-011's main view).
//
// PURE FUNCTIONS of a render (and the request that made it): no state, no
// clock, no drawing. The screen (T-034, canvas, the same C++ compiled to wasm)
// only maps these numbers to pixels, so what the oracles check here is what is
// drawn.
//
// GATE 1 LIVES IN irColumns. A screen has a few hundred columns and a render
// has up to millions of samples, so each column shows ONE sample: the one of
// greatest magnitude in its range, carried as the render's own value and
// index (never a mean, never a resample). So every drawn stem is a render
// sample, bit for bit, and no peak can be thinned away (plant
// viewDecimatesPeaks). When a column covers one sample, the columns ARE the
// render.
//
// Not real-time code: these allocate (the spectra) and run on the UI or the
// service's worker, never the audio thread.
#pragma once
#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstdint>
#include <span>
#include <vector>

#include "echo/ir_render.h"
#include "echo/params.h"
#include "echo/plants.h"

// No fused multiply-add here either (CLAUDE.md §Domain): scoped exactly as
// engine.h scopes it, and restored at the end of the file, so the spectra,
// the EDC and the renderer's own arithmetic round the same in every build
// (native and wasm alike, T-034) without touching the includer's numerics.
#if !defined(ECHO_FP_PRAGMA_OFF)
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif
#endif

namespace echo {

inline constexpr double kIrFloorDb = -120.0;  // the echogram's floor (SPEC §10's gate level)

// ---- the stem echogram ---------------------------------------------------------

// One display column: the render sample of greatest magnitude in its range.
struct IrColumn {
  std::int64_t index = -1;  // -1: every sample in the range is exact zero
  double value = 0.0;       // the render's own sample at `index`
};

// Columns of x over the sample range [x0, x1): column c covers
// [x0 + c*(x1-x0)/n, x0 + (c+1)*(x1-x0)/n), integer-divided, so the columns
// tile the range exactly with no gap and no overlap. Ties keep the earliest.
inline void irColumns(std::span<const double> x, std::int64_t x0, std::int64_t x1, std::span<IrColumn> cols) {
  const auto n = static_cast<std::int64_t>(cols.size());
  const auto len = static_cast<std::int64_t>(x.size());
  for (std::int64_t c = 0; c < n; ++c) {
    const std::int64_t a = std::clamp<std::int64_t>(x0 + c * (x1 - x0) / n, 0, len);
    const std::int64_t b = std::clamp<std::int64_t>(x0 + (c + 1) * (x1 - x0) / n, 0, len);
    IrColumn col;
    for (std::int64_t i = a; i < b; ++i) {
      const double v = x[static_cast<std::size_t>(i)];
      if (plant::viewDecimatesPeaks) {  // the plant: the column's first sample, peak or not
        if (v != 0.0) col = {i, v};
        break;
      }
      if (v != 0.0 && (col.index < 0 || std::fabs(v) > std::fabs(col.value))) col = {i, v};
    }
    cols[static_cast<std::size_t>(c)] = col;
  }
}

// A sample in dB relative to the render's stated input level, on the floor.
inline double irDb(double v, double levelDb) {
  if (v == 0.0) return kIrFloorDb;
  return std::max(kIrFloorDb, 20.0 * std::log10(std::fabs(v)) - levelDb);
}

// ---- the tempo grid ------------------------------------------------------------

// Sample positions of every beat (a quarter note at `bpm`) in [0, length).
// Beat k sits at k * 60 / bpm * sr, computed per beat (never accumulated, so
// beat 400 is as exact as beat 1). Returns the count written (<= out.size()).
inline std::size_t irBeats(double sr, double bpm, std::int64_t length, std::span<double> out) {
  const double b = plant::gridFixedTempo ? 120.0 : bpm;
  std::size_t n = 0;
  for (std::int64_t k = 0; n < out.size(); ++k) {
    const double pos = static_cast<double>(k) * 60.0 / b * sr;
    if (pos >= static_cast<double>(length)) break;
    out[n++] = pos;
  }
  return n;
}

// The same for multiples of the patch's synced division (time.div, horde's
// division list in quarter notes): where synced repeats land before offsets.
inline std::size_t irDivisionMarks(double sr, double bpm, int div, std::int64_t length, std::span<double> out) {
  if (div < 0 || div >= static_cast<int>(kDivisions.size())) return 0;
  const double b = plant::gridFixedTempo ? 120.0 : bpm;
  const double step = kDivisions[static_cast<std::size_t>(div)].beats * 60.0 / b * sr;
  std::size_t n = 0;
  for (std::int64_t k = 0; n < out.size(); ++k) {
    const double pos = static_cast<double>(k) * step;
    if (pos >= static_cast<double>(length)) break;
    out[n++] = pos;
  }
  return n;
}

// ---- the per-generation spectrum strip ------------------------------------------

namespace detail {
inline void fft(std::vector<std::complex<double>>& a) {  // iterative radix-2, size a power of two
  const std::size_t n = a.size();
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const double ang = -2.0 * 3.14159265358979323846 / static_cast<double>(len);
    for (std::size_t i = 0; i < n; i += len)
      for (std::size_t k = 0; k < len / 2; ++k) {
        const std::complex<double> w(std::cos(ang * static_cast<double>(k)), std::sin(ang * static_cast<double>(k)));
        const auto u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
      }
  }
}
}  // namespace detail

// The spacing of one channel's generations, in seconds: the clone's resolved
// time for that line (after offsets, the §3.4 clamp and synced folding).
// Ping-pong crosses the lines, so its generations alternate channels; the
// strip is defined for stereo and mid/side, where line k feeds channel k.
inline double irGenerationSpacing(const IrInfo& info, const IrPatch& patch, int channel) {
  if (plant::viewGenWindowFromBase && patch[kSync] < 0.5) return patch[kTimeMs] / 1000.0;  // the plant: the base time
  return info.times[static_cast<std::size_t>(channel & 1)];
}

// Magnitude spectra, in dB re the stated level, of generations 1..gens of one
// channel. Generation n is the rectangular window of one spacing T centred on
// its arrival n*T (so neighbours never share a sample), zero-padded to nfft (a
// power of two >= T*sr). out is gens x (nfft/2 + 1), row-major. Returns false
// if a window runs past the render or nfft is too small.
inline bool irGenerationSpectra(std::span<const double> x, double sr, double T, double levelDb, int gens, std::size_t nfft, std::span<double> out) {
  const auto w = static_cast<std::int64_t>(std::floor(T * sr));
  const std::size_t bins = nfft / 2 + 1;
  if (w < 2 || nfft < static_cast<std::size_t>(w) || !std::has_single_bit(nfft) || out.size() < bins * static_cast<std::size_t>(gens)) return false;
  std::vector<std::complex<double>> a(nfft);
  for (int g = 1; g <= gens; ++g) {
    const std::int64_t c = std::llround(static_cast<double>(g) * T * sr), from = c - w / 2;
    if (from < 0 || from + w > static_cast<std::int64_t>(x.size())) return false;
    std::fill(a.begin(), a.end(), std::complex<double>(0.0, 0.0));
    for (std::int64_t i = 0; i < w; ++i) a[static_cast<std::size_t>(i)] = x[static_cast<std::size_t>(from + i)];
    detail::fft(a);
    for (std::size_t k = 0; k < bins; ++k) {
      const double m = std::abs(a[k]);
      out[static_cast<std::size_t>(g - 1) * bins + k] = m > 0.0 ? 20.0 * std::log10(m) - levelDb : -400.0;
    }
  }
  return true;
}

// ---- the energy decay curve (secondary, D-011) ------------------------------------

// Schroeder's backward integral, in dB re the total energy: out[t] =
// 10 log10( sum_{u >= t} x[u]^2 / sum_u x[u]^2 ), -400 where nothing remains.
// A delay's EDC is a staircase: one step per generation.
inline void irEdc(std::span<const double> x, std::span<double> out) {
  const std::size_t n = std::min(x.size(), out.size());
  double total = 0.0;
  for (std::size_t i = 0; i < n; ++i) total += x[i] * x[i];
  double e = 0.0;
  if (plant::edcForward) {  // the plant: energy so far, not energy remaining
    for (std::size_t i = 0; i < n; ++i) {
      e += x[i] * x[i];
      out[i] = (e > 0.0 && total > 0.0) ? 10.0 * std::log10(e / total) : -400.0;
    }
    return;
  }
  for (std::size_t i = n; i-- > 0;) {
    e += x[i] * x[i];
    out[i] = (e > 0.0 && total > 0.0) ? 10.0 * std::log10(e / total) : -400.0;
  }
}

}  // namespace echo

#if !defined(ECHO_FP_PRAGMA_OFF)
#if defined(__clang__)
#pragma STDC FP_CONTRACT DEFAULT
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif
#endif
