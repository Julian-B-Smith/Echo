// echo/params.h — the parameter table, the C++ mirror of spec/params.json.
//
// spec/params.json is authoritative (SPEC §9). This table is checked against it
// key by key by tools/check_manifest.py in ./verify (the tool prints this table
// as JSON with `echo_manifest`), so a drifted range, default, class or label
// goes red rather than shipping. Order is params.json's order; the index is the
// parameter handle.
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

#include "echo/plants.h"

namespace echo {

enum class Morph { Continuous, Jump, Device };

// RS-2.2 (horde row A1): Echo's local class maps one to one onto OQ #33's two
// per-parameter fields (SPEC §9). FOUNDATIONS' engine_manifest.h is NOT vendored
// (Echo's brief echo-002 asks whether it should be), so its two enums are
// mirrored here by name and order, kUndeclared included, in a namespace of their
// own. When the header is vendored this becomes a using-declaration.
namespace oq33 {
enum class MorphKind : std::uint8_t { kUndeclared, kContinuous, kStepped, kGate };
enum class MorphParticipation : std::uint8_t { kUndeclared, kMorphs, kExcluded };
struct Class {
  MorphKind kind;
  MorphParticipation participation;
};
}  // namespace oq33

// continuous -> continuous + morphs; jump (a flip) -> stepped + morphs; device ->
// continuous + excluded (the I/O gains are continuous dB that never morph,
// RS-4.5). Not constexpr: the paramNoClass plant is a runtime flag in echo_plants.
inline oq33::Class oq33Class(Morph m) {
  using K = oq33::MorphKind;
  using M = oq33::MorphParticipation;
  switch (m) {
    case Morph::Continuous: return {K::kContinuous, M::kMorphs};
    case Morph::Jump: return {K::kStepped, M::kMorphs};
    case Morph::Device:
      if (plant::paramNoClass) return {K::kUndeclared, M::kUndeclared};
      return {K::kContinuous, M::kExcluded};
  }
  return {K::kUndeclared, M::kUndeclared};
}

struct ParamDef {
  std::string_view key;
  std::string_view name;
  std::string_view unit;
  bool stepped;
  double min, max, def;
  Morph morph;
  std::string_view law;
  std::span<const std::string_view> labels;
  // FOUNDATIONS' ReadCadence and the smoothing family (SPEC §9), compared with
  // params.json by check_manifest.py (T-016 closes T-007's item: before, the C++
  // table carried neither, so a drift in either was invisible).
  std::string_view rate;
  std::string_view smoothing;
};

// Handles, in table order.
enum P : int {
  kInGain, kOutGain, kMix, kSync, kTimeMs, kDiv, kOffL, kOffR, kChange, kLaw, kTempoMode,
  kGlide, kXfadeMs, kFb, kRoute, kInOn, kInHp, kInLp, kLoopOn, kLoopHp, kLoopLp, kOutOn,
  kOutHp, kOutLp, kModDepth, kModRate, kModShape, kModSpread, kModSeed, kNumParams
};

// horde's division list (intake/tempo-divisions.md), slow to fast: name,
// accepted alias ("" = none), beats in quarter notes. Stored by NAME in state.
struct Division { std::string_view name, alias; double beats; };
inline constexpr std::array<Division, 22> kDivisions{{
    {"2/1", "", 8.0},           {"3/2", "1/1D", 6.0},        {"2/1T", "", 16.0 / 3.0},
    {"1/1", "", 4.0},           {"3/4", "1/2D", 3.0},        {"1/1T", "", 8.0 / 3.0},
    {"1/2", "", 2.0},           {"3/8", "1/4D", 1.5},        {"1/2T", "", 4.0 / 3.0},
    {"1/4", "", 1.0},           {"3/16", "1/8D", 0.75},      {"1/4T", "", 2.0 / 3.0},
    {"1/8", "", 0.5},           {"3/32", "1/16D", 0.375},    {"1/8T", "", 1.0 / 3.0},
    {"1/16", "", 0.25},         {"3/64", "1/32D", 0.1875},   {"1/16T", "", 1.0 / 6.0},
    {"1/32", "", 0.125},        {"3/128", "1/64D", 0.09375}, {"1/32T", "", 1.0 / 12.0},
    {"1/64", "", 0.0625},
}};

// The division index for a canonical name or an alias (aliases are accepted
// on load and stored as the canonical name, SPEC §3.1).
constexpr std::optional<int> divisionIndex(std::string_view name) {
  for (std::size_t i = 0; i < kDivisions.size(); ++i)
    if (kDivisions[i].name == name || (!kDivisions[i].alias.empty() && kDivisions[i].alias == name))
      return static_cast<int>(i);
  return std::nullopt;
}

namespace labels {
inline constexpr std::array<std::string_view, 2> kSync{"free", "sync"};
inline constexpr std::array<std::string_view, 22> kDiv{
    "2/1", "3/2", "2/1T", "1/1", "3/4", "1/1T", "1/2", "3/8", "1/2T", "1/4", "3/16",
    "1/4T", "1/8", "3/32", "1/8T", "1/16", "3/64", "1/16T", "1/32", "3/128", "1/32T", "1/64"};
inline constexpr std::array<std::string_view, 3> kChange{"repitch", "crossfade", "jump"};
inline constexpr std::array<std::string_view, 2> kLaw{"tape", "exponential"};
inline constexpr std::array<std::string_view, 3> kTempoMode{"crossfade", "glide", "follow"};
inline constexpr std::array<std::string_view, 3> kRoute{"stereo", "pingpong", "midside"};
inline constexpr std::array<std::string_view, 2> kOnOff{"off", "on"};
inline constexpr std::array<std::string_view, 2> kShape{"sine", "drift"};
}  // namespace labels

inline constexpr std::array<ParamDef, kNumParams> kParams{{
    {"io.inGain", "Input Gain", "dB", false, -24, 24, 0, Morph::Device, "io", {}, "continuous", "gain"},
    {"io.outGain", "Output Gain", "dB", false, -24, 24, 0, Morph::Device, "io", {}, "continuous", "gain"},
    {"mix", "Mix", "%", false, 0, 100, 30, Morph::Continuous, "lin", {}, "continuous", "gain"},
    {"time.sync", "Sync", "", true, 0, 1, 1, Morph::Jump, "step", labels::kSync, "continuous", "time"},
    {"time.ms", "Time", "ms", false, 1, 12000, 375, Morph::Continuous, "log", {}, "continuous", "time"},
    {"time.div", "Division", "", true, 0, 21, 9, Morph::Jump, "step", labels::kDiv, "continuous", "time"},
    {"time.offL", "Left Offset", "%", false, -50, 50, 0, Morph::Continuous, "lin", {}, "continuous", "time"},
    {"time.offR", "Right Offset", "%", false, -50, 50, 0, Morph::Continuous, "lin", {}, "continuous", "time"},
    {"time.change", "Time Change", "", true, 0, 2, 0, Morph::Jump, "step", labels::kChange, "continuous", "deferred"},
    {"time.law", "Repitch Law", "", true, 0, 1, 0, Morph::Jump, "step", labels::kLaw, "continuous", "deferred"},
    {"time.tempoMode", "Tempo Follow", "", true, 0, 2, 1, Morph::Jump, "step", labels::kTempoMode, "continuous", "deferred"},
    {"time.glide", "Glide", "s", false, 0.01, 5, 0.3, Morph::Continuous, "log", {}, "continuous", "law"},
    {"time.xfadeMs", "Crossfade", "ms", false, 2, 500, 60, Morph::Continuous, "log", {}, "continuous", "law"},
    {"fb.amount", "Feedback", "%", false, 0, 120, 35, Morph::Continuous, "lin", {}, "continuous", "gain"},
    {"route.mode", "Routing", "", true, 0, 2, 0, Morph::Jump, "step", labels::kRoute, "continuous", "crossfade"},
    {"filt.in.on", "Input Filter", "", true, 0, 1, 0, Morph::Jump, "step", labels::kOnOff, "continuous", "crossfade"},
    {"filt.in.hpHz", "Input Low Cut", "Hz", false, 20, 2000, 20, Morph::Continuous, "log", {}, "continuous", "filter"},
    {"filt.in.lpHz", "Input High Cut", "Hz", false, 500, 20000, 20000, Morph::Continuous, "log", {}, "continuous", "filter"},
    {"filt.loop.on", "Loop Filter", "", true, 0, 1, 1, Morph::Jump, "step", labels::kOnOff, "continuous", "crossfade"},
    {"filt.loop.hpHz", "Loop Low Cut", "Hz", false, 20, 2000, 60, Morph::Continuous, "log", {}, "continuous", "filter"},
    {"filt.loop.lpHz", "Loop High Cut", "Hz", false, 500, 20000, 9000, Morph::Continuous, "log", {}, "continuous", "filter"},
    {"filt.out.on", "Output Filter", "", true, 0, 1, 0, Morph::Jump, "step", labels::kOnOff, "continuous", "crossfade"},
    {"filt.out.hpHz", "Output Low Cut", "Hz", false, 20, 2000, 20, Morph::Continuous, "log", {}, "continuous", "filter"},
    {"filt.out.lpHz", "Output High Cut", "Hz", false, 500, 20000, 20000, Morph::Continuous, "log", {}, "continuous", "filter"},
    {"mod.depthMs", "Mod Depth", "ms", false, 0, 20, 0, Morph::Continuous, "lin", {}, "continuous", "mod"},
    {"mod.rateHz", "Mod Rate", "Hz", false, 0.05, 10, 0.6, Morph::Continuous, "log", {}, "continuous", "mod"},
    {"mod.shape", "Mod Shape", "", true, 0, 1, 0, Morph::Jump, "step", labels::kShape, "continuous", "mod"},
    {"mod.spread", "Mod Spread", "%", false, 0, 100, 0, Morph::Continuous, "lin", {}, "continuous", "mod"},
    {"mod.seed", "Mod Seed", "", true, 0, 65535, 1, Morph::Jump, "step", {}, "continuous", "mod"},
}};

// The declared actions (SPEC §16.7, params.json `actions`), by index = action id.
// Engine::resolveAction reads THIS table; check_manifest.py holds it to
// params.json, so a C++ rename of an action cannot drift silently (critic T-016 L2).
inline constexpr std::array<std::string_view, 1> kActionKeys{"clear"};

constexpr std::optional<int> paramIndex(std::string_view key) {
  for (std::size_t i = 0; i < kParams.size(); ++i)
    if (kParams[i].key == key) return static_cast<int>(i);
  return std::nullopt;
}

}  // namespace echo

// ---- the normalised laws (ADR-203, SPEC §9; T-022) ----------------------------
//
// Every key maps [0, 1] onto its range and back, so horde can expose any of them
// (module 1.0 bar row 16). NOTHING ON THE AUDIO THREAD CALLS THESE: they serve the
// host's parameter surface and the face (docs/face.md). They are pure, allocate
// nothing, and read only the table.
//   lin   x = min + (max - min) v
//   log   x = min (max/min)^v   (geometric; every log key has min > 0)
//   io    dB = 48 (v - 1/2), so v = 1/2 is EXACTLY 0 dB (B435, intake/module-io-gain.md)
//   step  v = index / (n - 1), back index = round(v (n - 1)), n = max - min + 1
//         (poll ruling 2026-10-28: both ends exact; a stepped key is integer-valued)
// Out of range clamps to the ends (+-inf too); NaN, in either direction, reads as the
// key's DEFAULT, not its minimum (poll ruling 2026-10-28).
// The ends are returned as the table's own min and max, not computed: min * pow(max/min, 1)
// or min + (max - min) * 1 can miss max by an ulp, and "v = 1 is max" is contractual.
// No fused multiply-add here either (the §Domain invariant); same scoping as engine.h,
// at global scope, restored at the end of the file. ./verify's fused-op scan holds
// echo::toNorm / echo::fromNorm to it (echo_presets_fpc vs _nopragma).
#if !defined(ECHO_FP_PRAGMA_OFF)
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif
#endif

namespace echo {

// The display name and law as the host reads them. The two ADR-203 plants substitute
// here, at the one read point, so production reads the constexpr table unchanged.
inline std::string_view nameOf(int key) {
  if (key < 0 || key >= kNumParams) return {};
  if (plant::paramNoName && key == kOutLp) return {};
  return kParams[static_cast<std::size_t>(key)].name;
}
inline std::string_view lawOf(int key) {
  if (key < 0 || key >= kNumParams) return {};
  if (plant::paramNoLaw && key == kXfadeMs) return {};
  return kParams[static_cast<std::size_t>(key)].law;
}

namespace detail {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
// v clamped into [0, 1] (NaN is handled by the callers, before this).
constexpr double unit(double v) { return !(v > 0.0) ? 0.0 : (v < 1.0 ? v : 1.0); }
constexpr double within(double x, const ParamDef& p) { return !(x > p.min) ? p.min : (x < p.max ? x : p.max); }
constexpr bool known(std::string_view law) { return law == "lin" || law == "log" || law == "io" || law == "step"; }
}  // namespace detail

// v in [0, 1] -> the key's value. An unknown law (or key) answers NaN, which no check passes.
inline double fromNorm(int key, double v) {
  if (key < 0 || key >= kNumParams) return detail::kNaN;
  const ParamDef& p = kParams[static_cast<std::size_t>(key)];
  const std::string_view law = lawOf(key);
  if (!detail::known(law)) return detail::kNaN;
  if (std::isnan(v)) return p.def;
  v = detail::unit(v);
  if (v <= 0.0) return p.min;
  if (v >= 1.0) return p.max;
  double x;
  if (law == "lin" || (plant::logLawLinear && law == "log")) {
    x = p.min + (p.max - p.min) * v;
  } else if (law == "log") {
    // normRoundTripOff: the exponent is nudged, so toNorm(fromNorm(v)) != v.
    x = p.min * std::pow(p.max / p.min, plant::normRoundTripOff ? v * 0.999 : v);
  } else if (law == "io") {
    x = 48.0 * (v - 0.5);  // v = 1/2 -> exactly 0.0
  } else if (plant::stepFloors) {
    x = p.min + std::floor(v * (p.max - p.min) + 1e-9);  // stepFloors: a truncating step
  } else {
    x = p.min + std::round(v * (p.max - p.min));
  }
  return detail::within(x, p);
}

// The key's value -> v in [0, 1] (the value is clamped to the range first). A stepped
// key's non-integer value is NOT snapped here: v is its fractional index, and fromNorm
// rounds it back to a whole step.
inline double toNorm(int key, double x) {
  if (key < 0 || key >= kNumParams) return detail::kNaN;
  const ParamDef& p = kParams[static_cast<std::size_t>(key)];
  const std::string_view law = lawOf(key);
  if (!detail::known(law)) return detail::kNaN;
  if (std::isnan(x)) x = p.def;
  x = detail::within(x, p);
  if (x <= p.min) return 0.0;
  if (x >= p.max) return 1.0;
  double v;
  if (law == "log" && !plant::logLawLinear) v = std::log(x / p.min) / std::log(p.max / p.min);
  else if (law == "io") v = x / 48.0 + 0.5;
  else v = (x - p.min) / (p.max - p.min);  // lin, and step: index / (n - 1)
  return detail::unit(v);
}

}  // namespace echo

#if !defined(ECHO_FP_PRAGMA_OFF)
#if defined(__clang__)
#pragma STDC FP_CONTRACT DEFAULT
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif
#endif
