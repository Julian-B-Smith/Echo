// echo/presets.h — Echo's module presets and their macros (SPEC §16.3, D-035;
// M3 T-017; horde SPEC-MODULE-MACROS §4 - §5 with ADR-169 A3/A4/A5 and ADR-251;
// contract rows A12, A13). C++ is the ONE source (ruled by poll 2026-10-24,
// D-042): `echo_presets --json` writes presets/echo-factory.json from factory(),
// and ./verify fails if the file drifts.
//
// THE MODEL. A ModulePreset is an id, a label, a description, its macro law, the
// value of EVERY patch key (internal_defaults, by name in the file; Device keys
// never appear, RS-4.5) and up to 8 ORDERED slots (ADR-169 A4: ordered, keyed by
// position, each with the preset's own label). A slot binds continuous keys as
// [lo, hi] or [lo, hi, curve] (A3), and has a home in [0, 1] (A5).
//
// RESOLUTION (§5's last step), at slot values v[0..n):
//   set     key = shape(curve, lo, hi, v)
//   modify  key = base (+) sum of shape(...) over every slot bound to it, where
//           (+) is * 2^sum for a key in Hz with base > 0 (octaves), else + sum;
//           clamped to the key's range (ADR-251)
// Echo's factory bank uses modify (D-035 ruling 3).
//
// VALIDATION refuses (T-017): more than 8 slots; slots out of order (the file
// numbers them); an unknown key; a Device key anywhere; a binding on a stepped
// or Device key (§16.3); a log curve on a zero or mixed-sign endpoint, or under
// modify; a home outside [0, 1]; a set-law endpoint outside the key's range; an
// empty slot, an empty label, a key bound twice in one slot; an internal value
// out of range. Main thread only: this file allocates (strings, vectors).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "echo/engine.h"  // KeyValue, the load's input
#include "echo/module_version.h"
#include "echo/params.h"
#include "echo/plants.h"
#include "echo/state.h"

// No contraction of a*b+c into a fused multiply-add here either (the §Domain
// invariant; critic T-017 M1 measured 2 fused ops in resolve() without this).
// Same scoping as engine.h: on this file only, restored at its end; ./verify's
// fused-op scan holds echo::presets to it (echo_presets_fpc vs _nopragma).
#if !defined(ECHO_FP_PRAGMA_OFF)
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif
#endif

namespace echo::presets {

inline constexpr int kMaxSlots = 8;  // RS-5.1: "up to 8"
inline constexpr std::string_view kSchema = "echo-module-presets/1";

enum class Curve { Lin, Exp, Log };
enum class Law { Set, Modify };

struct Binding {
  int key = 0;
  double lo = 0, hi = 0;
  Curve curve = Curve::Lin;
};
struct Slot {
  std::string label;
  std::vector<Binding> bind;
  double home = 0.0;
};
struct ModulePreset {
  std::string id, label, description;
  Law law = Law::Modify;
  state::Values defaults{};  // every key; the Device entries are ignored and never written
  std::vector<Slot> slots;
};

inline const char* curveName(Curve c) { return c == Curve::Lin ? "lin" : c == Curve::Exp ? "exp" : "log"; }

// A3's curves. Every curve returns lo EXACTLY at v = 0 and hi EXACTLY at v = 1
// (special-cased: lo + (hi - lo)*1 need not round to hi, e.g. 0.1 + 0.2).
inline double shape(Curve c, double lo, double hi, double v) {
  if (!plant::presetEndpointInexact) {
    if (v <= 0.0) return lo;
    if (v >= 1.0) return hi;
  }
  switch (c) {
    case Curve::Lin: return lo + (hi - lo) * v;
    case Curve::Exp: return lo + (hi - lo) * v * v;
    case Curve::Log:
      // A host meeting an invalid log (zero or mixed-sign endpoints) resolves it
      // as lin and never produces a NaN (SPEC-MODULE-MACROS §4; critic T-017 M2).
      if (lo == 0.0 || hi == 0.0 || (lo < 0.0) != (hi < 0.0)) return lo + (hi - lo) * v;
      return lo * std::pow(hi / lo, v);
  }
  return lo;
}

inline bool isHz(int key) { return kParams[static_cast<std::size_t>(key)].unit == "Hz"; }

// §5's final step: the patch values at slot values v (one per slot, in order;
// a slot with no value given sits at its home, A5).
// The preset is read, never written (the planted presetResolveMutates writes the
// resolved values back into it, so a second resolve starts from the wrong base).
inline state::Values resolve(const ModulePreset& p, const std::vector<double>& v) {
  state::Values ip = p.defaults;
  auto at = [&](std::size_t s) { return s < v.size() ? v[s] : p.slots[s].home; };
  if (p.law == Law::Set) {
    for (std::size_t s = 0; s < p.slots.size(); ++s)
      for (const auto& b : p.slots[s].bind) ip[static_cast<std::size_t>(b.key)] = shape(b.curve, b.lo, b.hi, at(s));
  } else {
    state::Values sum{};
    std::array<bool, kNumParams> bound{};
    for (std::size_t s = 0; s < p.slots.size(); ++s)
      for (const auto& b : p.slots[s].bind) {
        sum[static_cast<std::size_t>(b.key)] += shape(b.curve, b.lo, b.hi, at(s));
        bound[static_cast<std::size_t>(b.key)] = true;
      }
    for (int k = 0; k < kNumParams; ++k) {
      const auto u = static_cast<std::size_t>(k);
      if (!bound[u]) continue;
      const double base = p.defaults[u];
      const double x = (isHz(k) && base > 0.0 && !plant::presetHzLinear) ? base * std::exp2(sum[u]) : base + sum[u];
      ip[u] = std::clamp(x, kParams[u].min, kParams[u].max);
    }
  }
  if (plant::presetResolveMutates) const_cast<ModulePreset&>(p).defaults = ip;
  return ip;
}

// The resolved values as the KeyValue list a load takes (patch keys only).
inline std::vector<KeyValue> patchOf(const state::Values& ip) {
  std::vector<KeyValue> kv;
  for (int k = 0; k < kNumParams; ++k)
    if (kParams[static_cast<std::size_t>(k)].morph != Morph::Device) kv.push_back({kParams[static_cast<std::size_t>(k)].key, ip[static_cast<std::size_t>(k)]});
  return kv;
}

// ---- validation -------------------------------------------------------------
inline bool validate(const ModulePreset& p, std::string* why = nullptr) {
  auto refuse = [why](std::string w) {
    if (why) *why = std::move(w);
    return false;
  };
  if (p.id.empty() || p.label.empty()) return refuse("a preset without an id or a label");
  auto plain = [](const std::string& t) {  // the file's strings carry no control characters (critic T-017 L2)
    for (unsigned char c : t)
      if (c < 0x20 || c == 0x7f) return false;
    return true;
  };
  if (!plain(p.id) || !plain(p.label) || !plain(p.description)) return refuse("a control character in " + p.id);
  for (const auto& s : p.slots)
    if (!plain(s.label)) return refuse("a control character in a slot label of " + p.id);
  if (static_cast<int>(p.slots.size()) > kMaxSlots) return refuse("more than 8 slots");
  for (int k = 0; k < kNumParams; ++k) {
    const auto& d = kParams[static_cast<std::size_t>(k)];
    if (d.morph == Morph::Device) continue;
    const double v = p.defaults[static_cast<std::size_t>(k)];
    if (!(v >= d.min && v <= d.max) || (d.stepped && v != std::floor(v)))
      return refuse("internal value of " + std::string(d.key) + " out of range");
  }
  for (const auto& s : p.slots) {
    if (s.label.empty()) return refuse("a slot without a label");
    if (s.bind.empty()) return refuse("slot `" + s.label + "` binds nothing");
    if (!(s.home >= 0.0 && s.home <= 1.0)) return refuse("slot `" + s.label + "`: home outside [0, 1]");
    std::array<bool, kNumParams> seen{};
    for (const auto& b : s.bind) {
      if (b.key < 0 || b.key >= kNumParams) return refuse("a binding on an unknown key");
      const auto& d = kParams[static_cast<std::size_t>(b.key)];
      const std::string key(d.key);
      if (d.morph == Morph::Device && !plant::presetDeviceAccepted) return refuse("a binding on the Device key " + key);
      if (d.stepped) return refuse("a binding on the stepped key " + key + " (SPEC §16.3: continuous keys only)");
      if (seen[static_cast<std::size_t>(b.key)]) return refuse(key + " bound twice in one slot");
      seen[static_cast<std::size_t>(b.key)] = true;
      if (!std::isfinite(b.lo) || !std::isfinite(b.hi)) return refuse("a non-finite endpoint on " + key);
      if (b.curve == Curve::Log) {
        if (p.law == Law::Modify && !plant::presetLogModifyAccepted) return refuse("a log curve under macroLaw modify (" + key + ", ADR-251)");
        if ((b.lo == 0.0 || b.hi == 0.0 || (b.lo < 0.0) != (b.hi < 0.0)) && !plant::presetLogAccepted)
          return refuse("a log curve on a zero or mixed-sign endpoint (" + key + ")");
      }
      if (p.law == Law::Set && (b.lo < d.min || b.lo > d.max || b.hi < d.min || b.hi > d.max))
        return refuse("a set-law endpoint outside " + key + "'s range");
    }
  }
  return true;
}

// ---- the file (one source: factory() below) -----------------------------------
inline std::string jsonString(std::string_view s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o + "\"";
}
inline std::string jsonNumber(double v) {
  char b[48];
  const std::size_t n = state::formatNumber(v, b, sizeof b);
  return std::string(b, n);
}
// A patch key's value in the file: a label for a labelled stepped key, else a number.
inline std::string jsonValue(int k, double v) {
  const auto& d = kParams[static_cast<std::size_t>(k)];
  if (d.stepped && !d.labels.empty()) return jsonString(d.labels[static_cast<std::size_t>(v)]);
  if (d.stepped) return std::to_string(static_cast<long long>(v));
  return jsonNumber(v);
}

inline std::string writeBank(const std::vector<ModulePreset>& bank) {
  std::string o = "{\n";
  o += " \"schema\": " + jsonString(kSchema) + ",\n";
  o += " \"module\": \"echo\",\n";
  o += " \"module_version\": " + jsonString(kModuleVersion) + ",\n";
  o += " \"note\": \"Echo's factory module presets. GENERATED by echo_presets --json from core/include/echo/presets.h "
       "(D-042); never edit by hand. Device keys never appear. Slots are ordered and numbered; a bind is [lo, hi] or "
       "[lo, hi, curve], no curve meaning lin; under macroLaw modify a binding is an offset from internal_defaults "
       "(octaves for a key in Hz).\",\n";
  o += " \"presets\": [\n";
  for (std::size_t i = 0; i < bank.size(); ++i) {
    const auto& p = bank[i];
    o += "  {\n";
    o += "   \"id\": " + jsonString(p.id) + ",\n";
    o += "   \"label\": " + jsonString(p.label) + ",\n";
    o += "   \"description\": " + jsonString(p.description) + ",\n";
    o += std::string("   \"macroLaw\": \"") + (p.law == Law::Modify ? "modify" : "set") + "\",\n";
    o += "   \"internal_defaults\": {";
    bool first = true;
    for (int k = 0; k < kNumParams; ++k) {
      if (kParams[static_cast<std::size_t>(k)].morph == Morph::Device) continue;
      o += (first ? "" : ", ") + jsonString(kParams[static_cast<std::size_t>(k)].key) + ": " + jsonValue(k, p.defaults[static_cast<std::size_t>(k)]);
      first = false;
    }
    o += "},\n   \"slots\": [";
    for (std::size_t s = 0; s < p.slots.size(); ++s) {
      const auto& sl = p.slots[s];
      o += std::string(s ? "," : "") + "\n    {\"slot\": " + std::to_string(s + 1) + ", \"label\": " + jsonString(sl.label) +
           ", \"home\": " + jsonNumber(sl.home) + ", \"bind\": {";
      for (std::size_t j = 0; j < sl.bind.size(); ++j) {
        const auto& b = sl.bind[j];
        o += std::string(j ? ", " : "") + jsonString(kParams[static_cast<std::size_t>(b.key)].key) + ": [" + jsonNumber(b.lo) + ", " + jsonNumber(b.hi);
        if (b.curve != Curve::Lin) o += std::string(", \"") + curveName(b.curve) + "\"";
        o += "]";
      }
      o += "}}";
    }
    o += std::string(p.slots.empty() ? "" : "\n   ") + "]\n  }" + (i + 1 < bank.size() ? "," : "") + "\n";
  }
  o += " ]\n}\n";
  return o;
}

// ---- reading the file: a strict reader for exactly this schema ------------------
// Objects, arrays, strings (\" and \\ only) and numbers (state.h's grammar); no
// true/false/null. Keys keep their order: slot and binding order is the contract.
struct Json {
  enum Kind { Obj, Arr, Str, Num } kind = Num;
  std::vector<std::pair<std::string, Json>> obj;
  std::vector<Json> arr;
  std::string str;
  double num = 0;
  const Json* get(std::string_view k) const {
    for (const auto& kv : obj)
      if (kv.first == k) return &kv.second;
    return nullptr;
  }
};

class Reader {
 public:
  explicit Reader(std::string_view t) : t_(t) {}
  bool parse(Json& out) {
    if (!value(out, 0)) return false;
    ws();
    return i_ == t_.size();
  }

 private:
  std::string_view t_;
  std::size_t i_ = 0;
  void ws() {
    while (i_ < t_.size() && (t_[i_] == ' ' || t_[i_] == '\n')) ++i_;
  }
  bool lit(char c) {
    ws();
    if (i_ < t_.size() && t_[i_] == c) { ++i_; return true; }
    return false;
  }
  bool string(std::string& s) {
    if (!lit('"')) return false;
    s.clear();
    while (i_ < t_.size() && t_[i_] != '"') {
      if (t_[i_] == '\\') {
        if (++i_ >= t_.size() || (t_[i_] != '"' && t_[i_] != '\\')) return false;
      } else if (static_cast<unsigned char>(t_[i_]) < 0x20) {
        return false;
      }
      s += t_[i_++];
    }
    return lit('"');
  }
  bool value(Json& j, int depth) {
    if (depth > 8) return false;
    ws();
    if (i_ >= t_.size()) return false;
    const char c = t_[i_];
    if (c == '{') {
      ++i_;
      j.kind = Json::Obj;
      if (lit('}')) return true;
      do {
        std::string k;
        Json v;
        if (!string(k) || !lit(':') || !value(v, depth + 1)) return false;
        for (const auto& kv : j.obj)
          if (kv.first == k) return false;  // a duplicated key
        j.obj.emplace_back(std::move(k), std::move(v));
      } while (lit(','));
      return lit('}');
    }
    if (c == '[') {
      ++i_;
      j.kind = Json::Arr;
      if (lit(']')) return true;
      do {
        Json v;
        if (!value(v, depth + 1)) return false;
        j.arr.push_back(std::move(v));
      } while (lit(','));
      return lit(']');
    }
    if (c == '"') {
      j.kind = Json::Str;
      return string(j.str);
    }
    const std::size_t s = i_;
    while (i_ < t_.size() && std::string_view("-0123456789.eE+").find(t_[i_]) != std::string_view::npos) ++i_;
    j.kind = Json::Num;
    j.str = std::string(t_.substr(s, i_ - s));  // the token as written: a slot number must be the writer's integer
    return state::parseNumber(t_.substr(s, i_ - s), j.num);
  }
};

// The whole bank file, all or nothing: the schema, the module, the version this
// build ships, and every preset whole and valid. `why` names the first refusal.
inline bool readBank(std::string_view text, std::vector<ModulePreset>& out, std::string* why = nullptr) {
  auto refuse = [why](std::string w) {
    if (why) *why = std::move(w);
    return false;
  };
  Json root;
  if (!Reader(text).parse(root) || root.kind != Json::Obj) return refuse("not a JSON object in the bank's subset");
  // Every field is checked for its KIND and unknown fields are refused: the
  // reader accepts exactly what writeBank produces (critic T-017 M4).
  auto onlyKeys = [](const Json& o, std::initializer_list<std::string_view> keys) {
    for (const auto& kv : o.obj) {
      bool known = false;
      for (auto k : keys) known = known || kv.first == k;
      if (!known) return false;
    }
    return true;
  };
  auto str = [](const Json* j) { return j && j->kind == Json::Str; };
  auto num = [](const Json* j) { return j && j->kind == Json::Num; };
  const Json* sc = root.get("schema");
  const Json* md = root.get("module");
  const Json* mv = root.get("module_version");
  const Json* ps = root.get("presets");
  if (!onlyKeys(root, {"schema", "module", "module_version", "note", "presets"})) return refuse("an unknown field in the bank");
  if (!str(sc) || sc->str != kSchema) return refuse("not an echo-module-presets/1 file");
  if (!str(md) || md->str != "echo") return refuse("not Echo's bank (module is not echo)");
  if (!str(mv) || mv->str != kModuleVersion) return refuse("module_version is not this build's");
  if (!ps || ps->kind != Json::Arr) return refuse("no presets array");
  std::vector<ModulePreset> bank;
  for (const Json& j : ps->arr) {
    ModulePreset p;
    const Json *id = j.get("id"), *lb = j.get("label"), *ds = j.get("description"), *law = j.get("macroLaw"),
               *idf = j.get("internal_defaults"), *sl = j.get("slots");
    if (j.kind != Json::Obj || !onlyKeys(j, {"id", "label", "description", "macroLaw", "internal_defaults", "slots"}))
      return refuse("a preset with an unknown field");
    if (!str(id) || !str(lb) || !str(ds) || !str(law) || !idf || !sl || idf->kind != Json::Obj || sl->kind != Json::Arr)
      return refuse("a preset is missing a field, or one has the wrong kind");
    p.id = id->str;
    p.label = lb->str;
    p.description = ds->str;
    if (law->str == "modify") p.law = Law::Modify;
    else if (law->str == "set") p.law = Law::Set;
    else return refuse("macroLaw is neither set nor modify");
    std::array<bool, kNumParams> seen{};
    for (const auto& [key, val] : idf->obj) {
      const auto k = paramIndex(key);
      if (!k) return refuse("an unknown key `" + key + "` in " + p.id);
      const auto& d = kParams[static_cast<std::size_t>(*k)];
      if (d.morph == Morph::Device && !plant::presetDeviceAccepted) return refuse("the Device key " + key + " in " + p.id);
      double v = 0;
      if (d.stepped && !d.labels.empty()) {
        if (val.kind != Json::Str || !state::parseValue(*k, val.str, v)) return refuse("a bad label for " + key + " in " + p.id);
      } else if (val.kind != Json::Num) {
        return refuse("a non-number for " + key + " in " + p.id);
      } else {
        v = val.num;
      }
      p.defaults[static_cast<std::size_t>(*k)] = v;
      seen[static_cast<std::size_t>(*k)] = true;
    }
    for (int k = 0; k < kNumParams; ++k)
      if (kParams[static_cast<std::size_t>(k)].morph != Morph::Device && !seen[static_cast<std::size_t>(k)])
        return refuse("a missing key " + std::string(kParams[static_cast<std::size_t>(k)].key) + " in " + p.id);
    int expect = 1;
    for (const Json& sj : sl->arr) {
      const Json *no = sj.get("slot"), *sl2 = sj.get("label"), *hm = sj.get("home"), *bd = sj.get("bind");
      if (sj.kind != Json::Obj || !onlyKeys(sj, {"slot", "label", "home", "bind"})) return refuse("a slot with an unknown field in " + p.id);
      if (!num(no) || !str(sl2) || !num(hm) || !bd || bd->kind != Json::Obj) return refuse("a slot is missing a field in " + p.id);
      if (no->str != std::to_string(expect++)) return refuse("slots out of order (or not numbered 1, 2, ...) in " + p.id);
      Slot s;
      s.label = sl2->str;
      s.home = hm->num;
      for (const auto& [key, arr] : bd->obj) {
        const auto k = paramIndex(key);
        if (!k) return refuse("a binding on an unknown key `" + key + "` in " + p.id);
        if (arr.kind != Json::Arr || arr.arr.size() < 2 || arr.arr.size() > 3 || !num(&arr.arr[0]) || !num(&arr.arr[1]))
          return refuse("a malformed binding on " + key);
        Binding b{*k, arr.arr[0].num, arr.arr[1].num, Curve::Lin};
        if (arr.arr.size() == 3) {
          if (!str(&arr.arr[2])) return refuse("a malformed curve on " + key);
          const std::string& c = arr.arr[2].str;
          if (c == "exp") b.curve = Curve::Exp;
          else if (c == "log") b.curve = Curve::Log;
          else return refuse("an unknown curve on " + key);  // lin is written by omission only
        }
        s.bind.push_back(b);
      }
      p.slots.push_back(std::move(s));
    }
    if (plant::presetReordersSlots && p.slots.size() > 1) std::swap(p.slots.front(), p.slots.back());
    std::string w;
    if (!validate(p, &w)) return refuse(p.id + ": " + w);
    bank.push_back(std::move(p));
  }
  out = std::move(bank);
  return true;
}

// ---- the factory bank (T-021 fills it; T-017 ships the init) ---------------------
// The manifest's defaults, no macros (D-028: "an init plus 7").
inline ModulePreset initPreset() {
  ModulePreset p;
  p.id = "init";
  p.label = "Init";
  p.description = "The manifest defaults (spec/params.json), no macros.";
  p.law = Law::Modify;
  for (int k = 0; k < kNumParams; ++k) p.defaults[static_cast<std::size_t>(k)] = kParams[static_cast<std::size_t>(k)].def;
  return p;
}
inline std::vector<ModulePreset> factory() { return {initPreset()}; }

}  // namespace echo::presets

#if !defined(ECHO_FP_PRAGMA_OFF)
#if defined(__clang__)
#pragma STDC FP_CONTRACT DEFAULT
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif
#endif
