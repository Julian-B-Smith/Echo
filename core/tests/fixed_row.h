// fixed_row.h — Echo's C++ copy of the fixed row (RS-5.8), TEST AND TOOL ONLY.
//
// Not under core/include/echo/: horde links that directory, and D-050 ruling 7 dropped the C++ fixed list
// from the module (D-050 A5 ruling 9). Only the suite (core/tests/core_tests.cpp) and `echo_manifest --fixed`
// include this, so tools/check_manifest.py can hold it equal to params.json's `fixedControls`.
#pragma once
#include <array>
#include <span>
#include <string_view>

#include "echo/params.h"

namespace echo {

// The fixed row (RS-5.8, SPEC §16.4; params.json `fixedControls`, which check_manifest.py holds this table to).
// A plain entry names its `key`. A keyed entry (D-050 A5, horde ADR-399) names a stepped key, `keyBy`, and
// `map`, parallel to that key's labels, says which key the entry carries under each label: Time is `time.ms`
// in sec and hz and `time.div` in sync. At most one keyed entry, and at most two keys in its map.
struct FixedControl {
  int k;
  std::string_view label;
  std::string_view key;    // a plain entry's key; "" for a keyed one
  std::string_view keyBy;  // a keyed entry's selector key; "" for a plain one
  std::span<const std::string_view> map;
};
namespace fixedmap {
inline constexpr std::array<std::string_view, 3> kTime{"time.ms", "time.ms", "time.div"};  // sec, hz, sync
}
inline constexpr std::array<FixedControl, 4> kFixedControls{{
    {1, "Time", "", "time.mode", fixedmap::kTime},
    {2, "Feedback", "fb.amount", "", {}},
    {3, "Mix", "mix", "", {}},
    {4, "Type", "route.mode", "", {}},
}};

// The key a fixed entry carries while its selector stands at label index `at` (a plain entry: its key).
// The plant swaps the first and last label of the map, as a transcription slip would (sync <-> sec).
inline std::string_view fixedKey(const FixedControl& f, std::size_t at) {
  if (f.keyBy.empty()) return f.key;
  if (plant::keyedRowSwapped) at = f.map.size() - 1 - at;
  return f.map[at];
}

// A keyed entry's map is parallel to its selector's labels, and the selector is a stepped key.
static_assert([] {
  for (const auto& f : kFixedControls) {
    if (f.keyBy.empty()) continue;
    const auto sel = paramIndex(f.keyBy);
    if (!sel || !kParams[static_cast<std::size_t>(*sel)].stepped || kParams[static_cast<std::size_t>(*sel)].labels.size() != f.map.size()) return false;
  }
  return true;
}());

}  // namespace echo
