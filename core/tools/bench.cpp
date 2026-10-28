// bench.cpp — Echo's cost bench (1.0 bar rows 13 - 15; ROADMAP T-020; D-045).
// REPORTED, NEVER A GATE (RS-8.3: CPU is Layer-E). ./verify only smoke-runs it
// (--smoke) and checks that every row is well formed, never what it says.
//
//   echo_bench                  44.1 kHz, the judged rate: LEDGER and MEMORY rows
//   echo_bench --sr 48000       the reported rate
//   echo_bench --md             both rates and the memory figure as docs/cost.md
//   echo_bench --smoke          one repeat of a tiny window, no load guard (./verify)
//   --reps R (the protocol is 5)   --force (skip the load guard; never for a ledger row)
//
// THE PROTOCOL is horde's B441-3 (horde docs/port/cpu-ledger.md §1, last changed at
// 271f275, read at 549b886), adapted from a synth to an FX module where it must be:
// - Release, -O3 -ffp-contract=off (the shipped arithmetic is the timed one);
// - 44.1 kHz judged, 48 kHz reported, 128-sample blocks;
// - 0.25 s rendered untimed (onset, the first ramps), then 2.0 s timed;
// - best of 5, INTERLEAVED: repeat r runs every cell once before repeat r + 1;
// - calibration: 1e8 dependent multiply-adds (x = x * 1.0000001 + 1e-9,
//   uncontracted), timed at the start of every repeat, the best kept;
//   ratio = (best seconds per second of audio) / (best calibration seconds);
// - load guard before each repeat: 1-minute load <= 3.0 AND no other process
//   at >= 50 % of a core in a `ps` snapshot; else wait 20 s, up to 10 min, then
//   refuse (exit 3). The lowest and highest loads, the waits and the top three
//   other processes (basename and %CPU, never a path) go into every row;
// - a volatile sink takes each timed block's first sample;
// - min-spec % = M3 % x 1.5, AN ASSUMPTION (module-1.0-bar.md), labelled.
// What an FX module needs and a synth does not, ruled by poll (D-045): the
// stimulus (seeded noise at -12 dBFS AND horde's B265 chord, both reported);
// the worst row (the largest of three variants: repitch moving, a crossfade
// always in flight, and the repitch patch under the morph sweep, added after the
// critic measured the plain morph row above the first two); the morph sweep
// (blended targets every block, x on a 1 Hz sine, continuous keys only).
// Every moving row prints engine-side EVIDENCE that it moved (the blocks whose
// stored value changed, the time fades, the fallbacks, the read-speed range),
// so a row that silently went static cannot pass ./verify's form check.
#include "alloc_probe.h"  // first: replaces operator new (the memory figure)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <unistd.h>

#include "b265.h"
#include "echo/engine.h"

namespace {

using Clock = std::chrono::steady_clock;
constexpr int kBlock = 128;
constexpr double kPi = 3.14159265358979323846;
constexpr double kMinSpec = 1.5;  // ASSUMPTION (module-1.0-bar.md appendix): M3 / 1.5
constexpr double kBudgetDefault = 2.0, kBudgetWorst = 4.0;  // RS-8.1, % of a min-spec core

volatile double g_sink = 0.0;

int handle(std::string_view k) {
  for (int i = 0; i < echo::kNumParams; ++i)
    if (echo::kParams[static_cast<std::size_t>(i)].key == k) return i;
  std::fprintf(stderr, "bench: no key %.*s\n", static_cast<int>(k.size()), k.data());
  std::exit(2);
}

// ---- the cells -----------------------------------------------------------------
struct KV {
  const char* key;
  double v;
};
// What a row moves at control rate, once per block, inside the timed region.
// Carried by the cell itself, never matched by name (critic T-020 M5: a renamed
// row whose move silently stopped would still print a well-formed row).
enum class Move { None, RepitchSine, XfadeRetarget, TimeFullRange, FbFullRange, Morph, RepitchSineMorph };
struct Cell {
  const char* row;    // default, worst-repitch, worst-crossfade, worst-morph, auto-time, auto-fb, morph
  const char* what;   // for docs/cost.md
  std::vector<KV> patch;
  Move move;
  double budget;
};
std::vector<Cell> cells() {
  const std::vector<KV> worst = {{"route.mode", 2},  {"filt.in.on", 1},   {"filt.loop.on", 1}, {"filt.out.on", 1}, {"mod.depthMs", 20},
                                 {"mod.shape", 1},   {"mod.spread", 100}, {"fb.amount", 120},  {"time.sync", 0},   {"time.ms", 550}};
  auto with = [&](std::vector<KV> extra) {
    auto p = worst;
    p.insert(p.end(), extra.begin(), extra.end());
    return p;
  };
  return {
      {"default", "the manifest defaults (1/4 at 120 bpm, 35 %, loop filter on); nothing moves", {}, Move::None, kBudgetDefault},
      {"worst-repitch",
       "mid/side, all three filters on, drift at 20 ms with spread 100, feedback 120 %; time.ms moving on a 0.5 Hz sine, 100 ms – 1 s, "
       "tape repitch",
       with({{"time.change", 0}, {"time.law", 0}}), Move::RepitchSine, kBudgetWorst},
      {"worst-crossfade", "the same patch in crossfade mode, time.ms re-targeted every 60 ms (250 ↔ 400 ms): a fade always in flight",
       with({{"time.change", 1}, {"time.xfadeMs", 60}}), Move::XfadeRetarget, kBudgetWorst},
      {"worst-morph",
       "worst-repitch with the morph sweep on top: every other continuous key blended each block toward 3/4 along its law, x on a 1 Hz sine",
       with({{"time.change", 0}, {"time.law", 0}}), Move::RepitchSineMorph, kBudgetWorst},
      {"auto-time",
       "the defaults, free time, time.ms under a 1 Hz full-range sine (1 ms – 12 s, linear), repitch: the head repitches while it can, and past "
       "the 12-semitone cap D-006's fallback crossfades instead (the fades and fallbacks columns)",
       {{"time.sync", 0}, {"time.change", 0}}, Move::TimeFullRange, kBudgetWorst},
      {"auto-fb", "the defaults, fb.amount under a 1 Hz full-range sine (0 – 120 %)", {}, Move::FbFullRange, kBudgetWorst},
      {"morph", "blended setParam targets every block between corner A (defaults) and corner B (every continuous key 3/4 along its law), "
                "x on a 1 Hz sine",
       {}, Move::Morph, kBudgetWorst},
  };
}

// Corner B for the morph row: every continuous (morphing) key three quarters of
// the way along its own law (lin, or geometric for log), stepped and Device keys
// left alone, so the row is pure blending with no flips (D-045).
double alongLaw(const echo::ParamDef& p, double x) {
  if (p.law == "log") return p.min * std::pow(p.max / p.min, x);
  return p.min + (p.max - p.min) * x;
}
double lawPos(const echo::ParamDef& p, double v) {
  if (p.law == "log") return std::log(v / p.min) / std::log(p.max / p.min);
  return (v - p.min) / (p.max - p.min);
}

struct Stim {
  const char* name;
  std::vector<double> L, R;
};
std::vector<Stim> stimuli(double sr, long n) {
  // Seeded decorrelated noise at -12 dBFS peak (mulberry32, the project's one generator).
  std::uint32_t s = 0xEC40u;
  auto mul = [&s] {
    std::uint32_t t = (s += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + ((t ^ (t >> 7)) * (t | 61u));
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0 * 2.0 - 1.0;
  };
  const double a = std::pow(10.0, -12.0 / 20.0);
  Stim noise{"noise", std::vector<double>(static_cast<std::size_t>(n)), std::vector<double>(static_cast<std::size_t>(n))};
  for (long i = 0; i < n; ++i) noise.L[static_cast<std::size_t>(i)] = a * mul(), noise.R[static_cast<std::size_t>(i)] = a * mul();
  Stim chord{"chord", b265::chordProbe(n, sr), {}};  // horde's B265 chord at -6 dBFS, both channels
  chord.R = chord.L;
  return {std::move(noise), std::move(chord)};
}

// What a cell's render showed beside its time: the engine-side evidence that the
// row's move happened (counted over the timed window; deterministic per cell).
struct Evidence {
  long moves = 0;               // blocks in which a moved key's stored value changed (getParam)
  std::uint64_t fades = 0, fallbacks = 0;  // line 0's time crossfades and D-006 fallbacks
  double sMin = 1, sMax = 1;    // line 0's read-speed range (1 = no repitch)
};

// One timed render of one cell. Returns the timed seconds. ONE clock pair around
// the whole timed region, as horde's (critic T-020 L3): the control-rate moves
// inside it are the row's cost; building, preparing and warming are outside.
double runCell(const Cell& c, const Stim& st, double sr, long warm, long timed, Evidence& ev) {
  auto e = std::make_unique<echo::Echo>();
  e->prepare(sr, kBlock);
  for (const auto& kv : c.patch) e->setParam(handle(kv.key), kv.v);
  e->reset();
  const int hT = handle("time.ms"), hF = handle("fb.amount");
  // The morph sweep: corner A is the cell's own patch, corner B every continuous
  // key 3/4 along its law. A key the row drives itself (time.ms under the repitch
  // sine) is left to that move.
  std::vector<std::pair<int, double>> morph;  // (handle, corner A's law position)
  if (c.move == Move::Morph || c.move == Move::RepitchSineMorph)
    for (int i = 0; i < echo::kNumParams; ++i) {
      const auto& p = echo::kParams[static_cast<std::size_t>(i)];
      if (p.morph != echo::Morph::Continuous || p.stepped || (c.move == Move::RepitchSineMorph && i == hT)) continue;
      morph.push_back({i, lawPos(p, e->getParam(i))});
    }
  std::vector<int> watched;  // the keys this row moves: their stored values are the evidence
  if (c.move == Move::RepitchSine || c.move == Move::XfadeRetarget || c.move == Move::TimeFullRange || c.move == Move::RepitchSineMorph)
    watched.push_back(hT);
  if (c.move == Move::FbFullRange) watched.push_back(hF);
  for (const auto& m : morph) watched.push_back(m.first);
  std::vector<double> last(watched.size());
  const long xfadeEvery = std::lround(0.06 * sr);
  std::array<double, kBlock> oL{}, oR{};
  bool alt = false;
  long nextFlip = 0;
  std::uint64_t fades0 = 0, falls0 = 0;
  Clock::time_point t0{};
  for (long pos = 0; pos < warm + timed; pos += kBlock) {
    const int n = static_cast<int>(std::min<long>(kBlock, warm + timed - pos));
    const double t = static_cast<double>(pos) / sr;
    if (pos == warm) {  // the timed region starts: evidence baselines, then the one clock read
      fades0 = e->fadeCount(0), falls0 = e->speedStats(0).fallbacks;
      e->resetStats();
      for (std::size_t k = 0; k < watched.size(); ++k) last[k] = e->getParam(watched[k]);
      t0 = Clock::now();
    }
    switch (c.move) {
      case Move::None: break;
      case Move::RepitchSine:
      case Move::RepitchSineMorph: e->setParam(hT, 550 + 450 * std::sin(2 * kPi * 0.5 * t)); break;
      case Move::XfadeRetarget:
        if (pos >= nextFlip) {
          e->setParam(hT, alt ? 400.0 : 250.0);
          alt = !alt;
          nextFlip += xfadeEvery;
        }
        break;
      case Move::TimeFullRange: e->setParam(hT, 1 + (12000.0 - 1) * (0.5 - 0.5 * std::cos(2 * kPi * t))); break;
      case Move::FbFullRange: e->setParam(hF, 120 * (0.5 - 0.5 * std::cos(2 * kPi * t))); break;
      case Move::Morph: break;
    }
    if (!morph.empty()) {
      const double x = 0.5 - 0.5 * std::cos(2 * kPi * t);
      for (const auto& [h, a] : morph) e->setParam(h, alongLaw(echo::kParams[static_cast<std::size_t>(h)], a + x * (0.75 - a)));
    }
    e->process(st.L.data() + pos, st.R.data() + pos, oL.data(), oR.data(), n, 120.0);
    if (pos >= warm) {
      g_sink = g_sink + oL[0];
      bool moved = false;
      for (std::size_t k = 0; k < watched.size(); ++k) {
        const double v = e->getParam(watched[k]);
        moved = moved || v != last[k];
        last[k] = v;
      }
      ev.moves += moved;
    }
  }
  const double secs = std::chrono::duration<double>(Clock::now() - t0).count();
  ev.fades = e->fadeCount(0) - fades0, ev.fallbacks = e->speedStats(0).fallbacks - falls0;
  const auto sp = e->speedStats(0);
  if (sp.sMin <= sp.sMax) ev.sMin = sp.sMin, ev.sMax = sp.sMax;
  return secs;
}

double calibrate() {
  volatile double seed = 1.0;
  double x = seed;
  const auto t0 = Clock::now();
  for (long i = 0; i < 100000000; ++i) x = x * 1.0000001 + 1e-9;  // dependent; -ffp-contract=off keeps it a multiply then an add
  const double s = std::chrono::duration<double>(Clock::now() - t0).count();
  g_sink = g_sink + x;
  return s;
}

// ---- the load guard (B441-3) ---------------------------------------------------------
struct Foreign {
  std::string comm;
  double pcpu;
};
std::vector<Foreign> foreignTop() {
  std::vector<Foreign> v;
  if (std::FILE* f = popen("ps -A -o pcpu=,pid=,comm=", "r")) {
    char line[4096];
    const long me = static_cast<long>(getpid());
    while (std::fgets(line, sizeof line, f)) {
      double pc = 0;
      long pid = 0;
      int used = 0;
      if (std::sscanf(line, " %lf %ld %n", &pc, &pid, &used) < 2 || pid == me) continue;
      std::string comm(line + used);
      while (!comm.empty() && (comm.back() == '\n' || comm.back() == ' ')) comm.pop_back();
      const auto slash = comm.rfind('/');
      if (slash != std::string::npos) comm = comm.substr(slash + 1);  // the basename, never a path
      v.push_back({comm, pc});
    }
    pclose(f);
  }
  std::sort(v.begin(), v.end(), [](const Foreign& a, const Foreign& b) { return a.pcpu > b.pcpu; });
  if (v.size() > 3) v.resize(3);
  return v;
}
double load1() {
  double l[3] = {0, 0, 0};
  return getloadavg(l, 3) > 0 ? l[0] : 1e9;
}
// What the guard saw when it let a repeat start: the load then and the snapshot
// it passed on (critic T-020 L1, L2: loads seen during refused waits are not
// loads any measurement ran under, and a second snapshot is not the one judged).
struct Passed {
  double load = 0;
  std::vector<Foreign> top;
  int waits = 0;
};
// Waits for a quiet machine; false after 10 minutes.
bool quiet(Passed& out, bool force) {
  for (int i = 0;; ++i) {
    const double l = load1();
    auto top = foreignTop();
    if (force || (l <= 3.0 && (top.empty() || top[0].pcpu < 50.0))) {
      out.load = l, out.top = std::move(top);
      return true;
    }
    if (i >= 30) return false;
    std::fprintf(stderr, "bench: waiting for a quiet machine (load %.2f, busiest other process at %.0f %%)\n", l, top.empty() ? 0.0 : top[0].pcpu);
    ++out.waits;
    std::this_thread::sleep_for(std::chrono::seconds(20));
  }
}

std::string jsonStr(const std::string& s) {
  std::string o = "\"";
  for (char ch : s) {
    if (ch == '"' || ch == '\\') o += '\\';
    if (static_cast<unsigned char>(ch) >= 0x20) o += ch;
  }
  return o + "\"";
}
std::string shell(const char* cmd) {
  std::string h = "unknown";
  if (std::FILE* f = popen(cmd, "r")) {
    char b[256] = {0};
    if (std::fgets(b, sizeof b, f)) {
      h = b;
      while (!h.empty() && (h.back() == '\n' || h.back() == ' ')) h.pop_back();
    }
    pclose(f);
  }
  return h;
}

struct Row {
  std::string row, input;
  double best = 1e300, budget = 0;
  Evidence ev;
  std::vector<Foreign> top;  // what else ran at the start of the best repeat
  std::string from;          // the derived worst: which variant
};

// One rate: every cell, interleaved, best of `reps`. Exit 3 if the machine never quietens.
struct RateRun {
  double sr = 0, cal = 1e300, timed = 0, loadLo = 1e9, loadHi = 0;
  int waits = 0;
  bool forced = false;
  std::vector<Row> rows;
  std::string head;
};
RateRun measure(double sr, int reps, bool force, double warmS, double timedS, const std::vector<std::string>& inputs) {
  RateRun rr;
  rr.sr = sr;
  rr.forced = force;
  rr.head = shell("git describe --always --dirty 2>/dev/null");
  const long warm = std::lround(warmS * sr / kBlock) * kBlock, timed = std::lround(timedS * sr / kBlock) * kBlock;
  rr.timed = static_cast<double>(timed) / sr;
  const auto cs = cells();
  std::vector<Stim> st;
  for (auto& s : stimuli(sr, warm + timed))
    if (std::find(inputs.begin(), inputs.end(), s.name) != inputs.end()) st.push_back(std::move(s));
  for (const auto& c : cs)
    for (const auto& s : st) rr.rows.push_back({c.row, s.name, 1e300, c.budget, {}, {}, ""});
  for (int r = 0; r < reps; ++r) {
    Passed g;
    if (!quiet(g, force)) {
      std::fprintf(stderr, "bench: the machine never went quiet in 10 minutes (load %.2f): refused, nothing measured (B441-3)\n", load1());
      std::exit(3);
    }
    rr.waits += g.waits;
    rr.loadLo = std::min(rr.loadLo, g.load), rr.loadHi = std::max(rr.loadHi, g.load);
    rr.cal = std::min(rr.cal, calibrate());
    std::size_t k = 0;
    for (const auto& c : cs)
      for (const auto& s : st) {
        Evidence ev;
        const double t = runCell(c, s, sr, warm, timed, ev);
        if (t < rr.rows[k].best) rr.rows[k].best = t, rr.rows[k].top = g.top, rr.rows[k].ev = ev;
        ++k;
      }
    rr.loadHi = std::max(rr.loadHi, load1());  // read at the end of a repeat, as horde's does
  }
  // The worst row (D-045): the largest of the three variants, per input.
  for (const auto& s : st) {
    const Row* w = nullptr;
    for (const auto& r : rr.rows)
      if (r.input == s.name && r.row.rfind("worst-", 0) == 0 && (!w || r.best > w->best)) w = &r;
    Row d = *w;
    d.from = d.row;
    d.row = "worst";
    rr.rows.push_back(d);
  }
  return rr;
}

double pctM3(const RateRun& rr, const Row& r) { return 100.0 * r.best / rr.timed; }

// One LEDGER row. `publish` (docs/cost.md, a PUBLIC file) keeps the other
// processes' %CPU and drops their names (critic T-020 M3: the names are the
// user's other running work); stdout keeps them, as horde's rows do.
std::string ledger(const RateRun& rr, const Row& r, bool publish) {
  const double pct = pctM3(rr, r), ratio = (r.best / rr.timed) / rr.cal, ms = pct * kMinSpec;
  char b[1400];
  std::snprintf(b, sizeof b,
                "{\"protocol\":\"B441-3\",\"module\":\"echo\",\"head\":%s,\"row\":%s,\"input\":%s,\"sr\":%.0f,\"block\":%d,\"timed_s\":%.4f,"
                "\"best_s\":%.6f,\"pct_m3\":%.4f,\"cal_ms\":%.2f,\"ratio\":%.5f,\"minspec_pct_assumed_x1.5\":%.4f,\"budget_pct\":%.1f,"
                "\"over_budget\":%s,\"forced\":%s,\"load_lo\":%.2f,\"load_hi\":%.2f,\"guard_waits\":%d,\"moves\":%ld,\"fades\":%llu,"
                "\"fallbacks\":%llu,\"speed_min\":%.4f,\"speed_max\":%.4f",
                jsonStr(rr.head).c_str(), jsonStr(r.row).c_str(), jsonStr(r.input).c_str(), rr.sr, kBlock, rr.timed, r.best, pct, rr.cal * 1000,
                ratio, ms, r.budget, ms > r.budget ? "true" : "false", rr.forced ? "true" : "false", rr.loadLo, rr.loadHi, rr.waits, r.ev.moves,
                static_cast<unsigned long long>(r.ev.fades), static_cast<unsigned long long>(r.ev.fallbacks), r.ev.sMin, r.ev.sMax);
  std::string o = b;
  if (!r.from.empty()) o += ",\"from\":" + jsonStr(r.from);
  o += publish ? ",\"foreign_top3_pcpu\":[" : ",\"foreign_top3\":[";
  for (std::size_t i = 0; i < r.top.size(); ++i) {
    if (publish) std::snprintf(b, sizeof b, "%s%.1f", i ? "," : "", r.top[i].pcpu);
    else std::snprintf(b, sizeof b, "%s{\"comm\":%s,\"pcpu\":%.1f}", i ? "," : "", jsonStr(r.top[i].comm).c_str(), r.top[i].pcpu);
    o += b;
  }
  return "LEDGER " + o + "]}";
}

// The memory figure (1.0 bar row 15; SPEC §11): every byte prepare() allocates,
// at each rate, with the largest block horde passes (4096; the block size
// allocates nothing). A HIGH-WATER MARK: the lines are never shrunk, so an
// instance re-prepared at a lower rate keeps its larger buffers (critic L4).
struct Mem {
  double sr, mib, linesMib;
};
std::vector<Mem> memory() {
  std::vector<Mem> m;
  for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
    echo::Echo e;
    alloc_probe::arm();
    e.prepare(sr, 4096);
    alloc_probe::disarm();
    m.push_back({sr, static_cast<double>(alloc_probe::g_bytes.load()) / (1024.0 * 1024.0),
                 2.0 * e.lineLength() * sizeof(float) / (1024.0 * 1024.0)});
  }
  return m;
}
std::string memoryRow(const Mem& m) {
  char b[256];
  std::snprintf(b, sizeof b, "MEMORY {\"module\":\"echo\",\"sr\":%.0f,\"max_block\":4096,\"mib\":%.2f,\"lines_mib\":%.2f}", m.sr, m.mib, m.linesMib);
  return b;
}

}  // namespace

int main(int argc, char** argv) {
  double sr = 44100.0;
  int reps = 5;
  bool force = false, md = false, smoke = false;
  for (int a = 1; a < argc; ++a) {
    const std::string_view s = argv[a];
    if (s == "--sr" && a + 1 < argc) sr = std::atof(argv[++a]);
    else if (s == "--reps" && a + 1 < argc) reps = std::max(1, std::atoi(argv[++a]));
    else if (s == "--force") force = true;
    else if (s == "--md") md = true;
    else if (s == "--smoke") smoke = true;
    else {
      std::fprintf(stderr, "usage: echo_bench [--sr HZ] [--reps R] [--force] [--md | --smoke]\n");
      return 2;
    }
  }
  const std::vector<std::string> both = {"noise", "chord"};
  if (smoke) {  // ./verify: the code path and the row form, not the numbers
    const auto rr = measure(sr, 1, true, 0.01, 0.2, both);
    for (const auto& r : rr.rows) std::printf("%s\n", ledger(rr, r, false).c_str());
    for (const auto& m : memory()) std::printf("%s\n", memoryRow(m).c_str());
    return 0;
  }
  if (!md) {
    const auto rr = measure(sr, reps, force, 0.25, 2.0, both);
    for (const auto& r : rr.rows) std::printf("%s\n", ledger(rr, r, false).c_str());
    for (const auto& m : memory()) std::printf("%s\n", memoryRow(m).c_str());
    return 0;
  }
  if (force) {  // critic T-020 M4: a published figure is always a guarded one
    std::fprintf(stderr, "bench: --md never runs with --force (a published row must have passed the load guard)\n");
    return 2;
  }
  // --md: docs/cost.md. 44.1 kHz (judged) and 48 kHz on both inputs; 96 and 192 kHz
  // informational, noise only (RS-8.2: higher rates are informational; the rack
  // runs at the host rate, ADR-237, so their cost is stated, critic T-020 M2).
  std::vector<RateRun> runs;
  runs.push_back(measure(44100.0, reps, false, 0.25, 2.0, both));
  runs.push_back(measure(48000.0, reps, false, 0.25, 2.0, both));
  runs.push_back(measure(96000.0, reps, false, 0.25, 2.0, {"noise"}));
  runs.push_back(measure(192000.0, reps, false, 0.25, 2.0, {"noise"}));
  const auto mem = memory();
  const std::string cpu = shell("sysctl -n machdep.cpu.brand_string 2>/dev/null");
  std::string o =
      "# Echo — cost (1.0 bar rows 13 – 15)\n\n"
      "GENERATED by `echo_bench --md > docs/cost.md` (core/tools/bench.cpp, T-020, D-045) on " + cpu + ". **CPU is Layer-E: measured and\n"
      "reported, never a gate (RS-8.3).** horde measures every module itself on its own bench (RS-8.2); these figures are indicative.\n\n"
      "**Protocol:** horde's B441-3 (`docs/port/cpu-ledger.md` §1, last changed at `271f275`), adapted to an FX module (D-045): Release\n"
      "`-O3 -ffp-contract=off`; 128-sample blocks; 0.25 s untimed, 2.0 s timed (one clock pair, control moves inside); best of 5\n"
      "interleaved; calibration 1e8 dependent multiply-adds; the load guard (1-minute load ≤ 3.0 and no other process ≥ 50 % of a core;\n"
      "`--md` refuses `--force`). **Min-spec = M3 × 1.5: AN ASSUMPTION** until measured once on an M1 base or the Intel min-spec.\n"
      "Budgets (RS-8.1; Q-A3 closed by D-028): ≤ 2 % of a min-spec core at the defaults, ≤ 4 % at the worst; the automated and morph\n"
      "rows are read against the 4 % (the lead's reading, D-045). Stimuli: seeded noise at −12 dBFS (L and R decorrelated) and horde's\n"
      "B265 chord at −6 dBFS. **Evidence** columns show each row's move happened in the timed window: the blocks whose moved key changed,\n"
      "the time fades and D-006 fallbacks on line 0, and its read-speed range.\n\n## Rows\n\n";
  char t[700];
  for (const auto& rr : runs) {
    std::snprintf(t, sizeof t,
                  "### %.1f kHz — %s (calibration %.1f ms; guard passed at load %.2f – %.2f, end of repeat ≤ %.2f; %d guard waits; head `%s`)\n\n"
                  "| row | input | %% of an M3 core | ratio | min-spec %% (×1.5, assumed) | budget | reading | moved blocks | fades | fallbacks | speed |\n"
                  "|---|---|---|---|---|---|---|---|---|---|---|\n",
                  rr.sr / 1000, rr.sr == 44100.0 ? "judged (E-6)" : rr.sr == 48000.0 ? "reported" : "informational", rr.cal * 1000, rr.loadLo,
                  rr.loadLo, rr.loadHi, rr.waits, rr.head.c_str());
    o += t;
    for (const auto& r : rr.rows) {
      const double pct = pctM3(rr, r), ms = pct * kMinSpec;
      const std::string name = r.row + (r.from.empty() ? "" : " (" + r.from + ")");
      std::snprintf(t, sizeof t, "| %s | %s | %.3f | %.4f | %.3f | %.0f %% | %s | %ld | %llu | %llu | %.2f – %.2f |\n", name.c_str(), r.input.c_str(), pct,
                    (r.best / rr.timed) / rr.cal, ms, r.budget, ms > r.budget ? "**over**" : "within", r.ev.moves,
                    static_cast<unsigned long long>(r.ev.fades), static_cast<unsigned long long>(r.ev.fallbacks), r.ev.sMin, r.ev.sMax);
      o += t;
    }
    o += "\n";
  }
  o += "### The rows\n\n| row | what |\n|---|---|\n";
  for (const auto& c : cells()) o += std::string("| ") + c.row + " | " + c.what + " |\n";
  o += "| worst | the largest of worst-repitch, worst-crossfade and worst-morph, per input (D-045) |\n";
  o += "\n## Memory per instance (row 15; SPEC §11)\n\nEvery byte `prepare()` allocates, with a 4096-sample maximum block (the block size "
       "allocates nothing); nothing is allocated after it. A **high-water mark**: the lines are never shrunk, so an instance re-prepared at a "
       "lower rate keeps the larger figure. Plus `sizeof(Echo)` (2.5 KiB).\n\n| rate | MiB | of which the two lines |\n|---|---|---|\n";
  for (const auto& m : mem) {
    std::snprintf(t, sizeof t, "| %.1f kHz | %.2f | %.2f |\n", m.sr / 1000, m.mib, m.linesMib);
    o += t;
  }
  o += "\n**Beyond one instance (RS-8.4).** Echo starts no second instance during a flip or a load: a load and a Clear fade the old repeats\n"
       "inside the one engine. Spill-over tails are horde's. **When M4's impulse-response display is open, its render service\n"
       "(`ir_service.h`) holds one more prepared `Echo`** for the offline clone: the same memory again, off the audio thread.\n";
  o += "\n## Raw rows\n\nAs `echo_bench` prints them, with the other processes' names dropped for this public file (their %CPU kept).\n\n```\n";
  for (const auto& rr : runs)
    for (const auto& r : rr.rows) o += ledger(rr, r, true) + "\n";
  for (const auto& m : mem) o += memoryRow(m) + "\n";
  o += "```\n";
  std::fwrite(o.data(), 1, o.size(), stdout);
  return 0;
}
