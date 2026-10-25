// echo/params.h — the parameter table, the C++ mirror of spec/params.json.
//
// spec/params.json is authoritative (SPEC §9). This table is checked against it
// key by key by tools/check_manifest.py in ./verify (the tool prints this table
// as JSON with `echo_manifest`), so a drifted range, default, class or label
// goes red rather than shipping. Order is params.json's order; the index is the
// parameter handle.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
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
