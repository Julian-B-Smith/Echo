// echo/state.h — Echo's saved state as text (SPEC §16.2, D-035; M3 T-015; horde
// RS-6.10 – 6.12, rows A17 – A20).
//
//   echo-state 1
//   mix 30
//   time.sync sync
//   ...
//   [device]
//   io.inGain 0
//   io.outGain 0
//
// Every patch key, one per line, in manifest order (params.h, = params.json);
// then the Device keys in their own instance block. Stepped keys with labels are
// written BY NAME (divisions by horde's names; an alias is read and written back
// as the name, SPEC §3.1); mod.seed as a decimal integer; every other value as
// the SHORTEST decimal that this file's own parser reads back exactly.
//
// WHY NOT std::to_chars / std::from_chars: the dev Mac's libc++ deletes
// from_chars(double), so there would be no exact reader to prove the writer
// against. Instead: snprintf candidates, kept only when parseNumber() returns the
// same bits (17 significant digits always do, IEEE double).
//
// LOCALE (critic T-015 M1): snprintf and strtod follow LC_NUMERIC, and a host may
// set a comma locale, or one whose decimal point is two bytes ("٫", ar_SA). So
// both run under a "C" numeric locale made current for THIS thread only
// (uselocale; stageState may run beside process() on another thread), and every
// number token must first match a strict grammar of ASCII digits, '-', '.', 'e'
// and an exponent sign. The same bytes then read the same in every locale.
//
// Parsing is strict and ALL OR NOTHING: an unknown, missing or duplicated key, a
// value out of range or not a whole number for a stepped key, a label that is
// neither the key's nor a division alias, a key in the wrong block, a different
// version, or a text over 2 KiB refuses the whole text. The size is checked
// FIRST, before anything else is done with the text (A19). Numbers follow a
// strict grammar (no '+', hex, whitespace, "30." or ".5"; the seed is a plain
// decimal integer), so what is accepted does not depend on the libc's strtod.
// No operator new here: tokens are copied into stack buffers. (The libc's own
// strtod / printf may malloc internally on some platforms; these run OFF the
// audio thread, so that is allowed, but it is not "allocation-free" in the
// strictest sense.)
#pragma once
#include <array>
#include <cerrno>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "echo/params.h"
#include "echo/plants.h"

#if defined(__APPLE__)
#include <xlocale.h>
#endif
#include <locale.h>

namespace echo::state {

inline constexpr std::size_t kMaxBytes = 2048;  // horde RS-6.12: 2 KiB per corner, the whole text
inline constexpr std::string_view kVersion = "echo-state 1";
inline constexpr std::string_view kDeviceHeader = "[device]";

using Values = std::array<double, kNumParams>;

// The "C" numeric locale, current for this thread while the guard lives. The
// locale object is made once (newlocale allocates; on the first state call,
// never on the audio thread) and never freed. If it cannot be made, the guard
// does nothing, and the strict grammar below still refuses a comma or a
// multi-byte point, so a state can fail to load but never load differently.
class CNumeric {
 public:
  CNumeric() {
    static const locale_t c = newlocale(LC_NUMERIC_MASK, "C", static_cast<locale_t>(0));
    if (c) prev_ = uselocale(c);
  }
  ~CNumeric() {
    if (prev_) uselocale(prev_);
  }
  CNumeric(const CNumeric&) = delete;
  CNumeric& operator=(const CNumeric&) = delete;

 private:
  locale_t prev_ = static_cast<locale_t>(0);
};

// The number grammar: -?D+(.D+)?([eE][+-]?D+)?  (integer: -?D+). Exactly what
// the writer emits, nothing a libc's strtod merely tolerates.
inline bool numberGrammar(std::string_view t, bool integer) {
  std::size_t i = 0;
  auto digits = [&] {
    const std::size_t s = i;
    while (i < t.size() && t[i] >= '0' && t[i] <= '9') ++i;
    return i > s;
  };
  if (i < t.size() && t[i] == '-') ++i;
  if (!digits()) return false;
  if (integer) return i == t.size();
  if (i < t.size() && t[i] == '.') {
    ++i;
    if (!digits()) return false;
  }
  if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
    ++i;
    if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i;
    if (!digits()) return false;
  }
  return i == t.size();
}

// A number token (at most 63 chars) as a finite double, or false. Refuses an
// underflow or overflow (ERANGE): the writer never produces one. -0 reads as 0,
// so two byte-different texts never describe one patch (critic T-015 L1).
inline bool parseNumber(std::string_view tok, double& out, bool integer = false) {
  char b[64];
  if (tok.size() >= sizeof b || !numberGrammar(tok, integer)) return false;
  std::memcpy(b, tok.data(), tok.size());
  b[tok.size()] = '\0';
  const CNumeric c;
  char* end = nullptr;
  errno = 0;
  const double v = std::strtod(b, &end);
  if (end != b + tok.size() || errno == ERANGE || !std::isfinite(v)) return false;
  out = v + 0.0;
  return true;
}

// The SHORTEST text of v that parseNumber reads back bit for bit. Fewest
// significant digits is not shortest text: 30 round-trips at one digit as
// "3e+01". So both forms are tried, "%.*g" at 1..17 digits and "%.*f" at 0..17
// decimals, and the shortest exact one wins, fixed on a tie ("30", "375",
// "0.05"; "1e-05" over "0.00001"). Returns the length written into b
// (cap >= 48), 0 only if nothing fits.
inline std::size_t formatNumber(double v, char* b, std::size_t cap) {
  const CNumeric cn;
  v = v + 0.0;  // -0 is written as 0 (critic T-015 L1)
  if (plant::stateFloat6g) {  // the planted lossy %.6g (L0008)
    const int n = std::snprintf(b, cap, "%.6g", v);
    return n > 0 ? static_cast<std::size_t>(n) : 0;
  }
  char c[48];
  std::size_t best = 0;
  auto tryForm = [&](const char* fmt, int p) {
    const int n = std::snprintf(c, sizeof c, fmt, p, v);
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof c || static_cast<std::size_t>(n) >= cap) return;
    if (best != 0 && static_cast<std::size_t>(n) >= best) return;  // fixed is tried first, so a tie keeps fixed
    double back = 0;
    if (!parseNumber(std::string_view(c, static_cast<std::size_t>(n)), back) || back != v || std::signbit(back) != std::signbit(v)) return;
    std::memcpy(b, c, static_cast<std::size_t>(n));
    best = static_cast<std::size_t>(n);
  };
  for (int p = 0; p <= 17; ++p) tryForm("%.*f", p);
  for (int p = 1; p <= 17; ++p) tryForm("%.*g", p);
  return best;
}

// Parameter i's value as its text: a label, an integer or a number.
inline std::size_t formatValue(int i, double v, char* b, std::size_t cap) {
  const ParamDef& d = kParams[static_cast<std::size_t>(i)];
  if (d.stepped && !d.labels.empty()) {
    const auto k = static_cast<std::size_t>(v);
    if (plant::stateDivByIndex && i == kDiv) return static_cast<std::size_t>(std::snprintf(b, cap, "%zu", k));
    const std::string_view name = d.labels[k];
    if (name.size() >= cap) return 0;
    std::memcpy(b, name.data(), name.size());
    return name.size();
  }
  if (d.stepped) {
    const CNumeric cn;
    return static_cast<std::size_t>(std::snprintf(b, cap, "%d", static_cast<int>(v)));
  }
  return formatNumber(v, b, cap);
}

// Parameter i's text as its value, refused unless the engine would accept it.
inline bool parseValue(int i, std::string_view tok, double& out) {
  const ParamDef& d = kParams[static_cast<std::size_t>(i)];
  double v = 0;
  if (d.stepped && !d.labels.empty()) {
    bool found = false;
    for (std::size_t k = 0; k < d.labels.size(); ++k)
      if (d.labels[k] == tok) { v = static_cast<double>(k); found = true; }
    if (!found && i == kDiv)
      if (const auto k = divisionIndex(tok)) { v = *k; found = true; }
    if (!found) return false;
  } else {
    if (!parseNumber(tok, v, d.stepped)) return false;  // a stepped key without labels (mod.seed): a plain decimal integer
  }
  if (v < d.min || v > d.max) return false;
  out = v;
  return true;
}

// Write values as the state text into out. Returns the length, or 0 if it does
// not fit cap (a caller's buffer of kMaxBytes always holds a valid state:
// abiState's worst case is checked in the suite). `latch` is only for the
// planted serialised meter latch (A20's must-fail control).
inline std::size_t write(const Values& p, char* out, std::size_t cap, bool latch = false) {
  std::size_t n = 0;
  auto put = [&](std::string_view s) {
    if (n + s.size() > cap) return false;
    std::memcpy(out + n, s.data(), s.size());
    n += s.size();
    return true;
  };
  char b[48];
  auto line = [&](int i) {
    const std::size_t len = formatValue(i, p[static_cast<std::size_t>(i)], b, sizeof b);
    return len > 0 && put(kParams[static_cast<std::size_t>(i)].key) && put(" ") && put(std::string_view(b, len)) && put("\n");
  };
  if (!put(kVersion) || !put("\n")) return 0;
  for (int i = 0; i < kNumParams; ++i) {
    if (kParams[static_cast<std::size_t>(i)].morph == Morph::Device) continue;
    if (plant::stateKeyMissing && i == kFb) continue;  // the planted incomplete state (A17)
    if (!line(i)) return 0;
  }
  if (plant::stateLatchSerialised && !(put("meter.clip ") && put(latch ? "1" : "0") && put("\n"))) return 0;
  if (!put(kDeviceHeader) || !put("\n")) return 0;
  for (int i = 0; i < kNumParams; ++i)
    if (kParams[static_cast<std::size_t>(i)].morph == Morph::Device && !line(i)) return 0;
  return n;
}

// Parse a whole state text into out. On refusal returns false, sets *why, and
// leaves out untouched.
inline bool parse(std::string_view text, Values& out, const char** why = nullptr) {
  auto refuse = [why](const char* w) {
    if (why) *why = w;
    return false;
  };
  if (text.size() > kMaxBytes && !plant::stateOversizedAccepted) return refuse("over 2 KiB");  // FIRST (A19)
  Values v{};
  std::array<bool, kNumParams> seen{};
  bool device = false, first = true;
  std::size_t pos = 0;
  while (pos < text.size()) {
    const std::size_t nl = text.find('\n', pos);
    if (nl == std::string_view::npos) return refuse("the last line is not terminated");
    const std::string_view ln = text.substr(pos, nl - pos);
    pos = nl + 1;
    if (first) {
      if (ln != kVersion) return refuse("not an echo-state 1 text");
      first = false;
      continue;
    }
    if (ln == kDeviceHeader) {
      if (device) return refuse("two [device] blocks");
      device = true;
      continue;
    }
    const std::size_t sp = ln.find(' ');
    if (sp == std::string_view::npos || sp == 0 || ln.find(' ', sp + 1) != std::string_view::npos)
      return refuse("a line is not `key value`");
    const auto i = paramIndex(ln.substr(0, sp));
    if (!i) {
      if (plant::stateAcceptsUnknown) continue;  // the planted silent skip (FOUNDATIONS' note to Scape)
      return refuse("an unknown key");
    }
    const auto u = static_cast<std::size_t>(*i);
    if (seen[u]) return refuse("a duplicated key");
    if ((kParams[u].morph == Morph::Device) != device) return refuse("a key in the wrong block");
    if (!parseValue(*i, ln.substr(sp + 1), v[u])) return refuse("a value out of range or not the key's");
    seen[u] = true;
  }
  if (first) return refuse("empty");
  for (bool s : seen)
    if (!s) return refuse("a missing key");
  out = v;
  return true;
}

}  // namespace echo::state
