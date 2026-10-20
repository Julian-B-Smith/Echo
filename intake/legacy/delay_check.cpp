/*
 * delay_check — the standard delay's oracle (ADR-142, B68/B73 increment A).
 *
 * THIS IS A SPEC, NOT A PARITY TEST, and that distinction is the point. Every
 * other DSP gate here compares C++ against an HTML prototype; DelayCore has no
 * prototype on purpose (the swarm delay's feedback law is the thing the human
 * rejected — see delay_core.h's header), so correctness is defined by
 * impulse-response invariants that a delay either satisfies or does not:
 *
 *   L0-D1  repeat SPACING equals the requested delay
 *   L0-D2  per-generation RATIO equals feedback (within the in-loop filters)
 *   L0-D3  feedback 0 yields exactly one repeat, then silence
 *   L0-D4  feedback past unity stays BOUNDED (the soft-ceiling law)
 *   L0-D5  crossfeed 1 ALTERNATES channels (ping-pong)
 *   L0-D6  sync arithmetic: beats x tempo -> the same spacing L0-D1 measures
 *   L0-D7  the NaN watchdog heals a poisoned loop
 *   L0-D8  silence in, silence out (no self-noise; there is no RNG here)
 *
 * Each number below is the CONTRACT, not an observation of today's build: a
 * change that moves one is a change to what this module is, and belongs in an
 * ADR before it belongs here.
 * WIRED: ./verify full.
 */
#include <cstdio>
#include <cmath>
#include <vector>
#include <memory>
#include <algorithm>

#include "../src/delay_core.h"

namespace
{
int failures = 0;

void check(bool ok, const char *what, const char *detail = "")
{
  std::printf("  %-56s %s%s%s\n", what, ok ? "ok" : "FAIL", *detail ? "  " : "", detail);
  if (!ok) failures++;
}

constexpr double kSR = 48000.0;

// Run an impulse through the core and return both channels.
struct Run
{
  std::vector<float> L, R;
};

Run impulse(hypersaw::DelayCore &d, int nSamples, double amp = 1.0, bool rightToo = false)
{
  Run r;
  r.L.assign((size_t)nSamples, 0.0f);
  r.R.assign((size_t)nSamples, 0.0f);
  r.L[0] = (float)amp;
  if (rightToo) r.R[0] = (float)amp;
  // Process in blocks to prove the core is block-size independent (ADR-086's
  // subdivision rule applied here: a delay whose repeats move with the buffer
  // size would be broken in exactly the way no single-block test can see).
  int off = 0;
  while (off < nSamples)
  {
    const int m = std::min(97, nSamples - off);   // deliberately not a power of two
    d.processStereo(r.L.data() + off, r.R.data() + off, m);
    off += m;
  }
  return r;
}

// Peak position and value of the largest sample inside [from, to).
std::pair<int, double> peakIn(const std::vector<float> &v, int from, int to)
{
  int best = from;
  double bv = -1;
  for (int i = from; i < to && i < (int)v.size(); i++)
  {
    const double a = std::fabs((double)v[i]);
    if (a > bv) { bv = a; best = i; }
  }
  return {best, bv};
}

/* SUMMED MAGNITUDE, AND THE PEAK WOULD LIE. A fractional-delay read splits a
   one-sample impulse across two samples by the interpolation fraction, so the
   PEAK of a repeat is (1-f) or f -- as low as half the energy that actually
   came round the loop, and it changes again every generation as the smear
   widens. Measured: peak ratios read 0.4735 where the loop gain was exactly
   0.5. Sum |x| over a window is invariant under that split (the lerp weights
   are non-negative and sum to 1), so it is the honest measure of "how much
   came back" -- and it stays honest once the in-loop filters spread a repeat
   over many samples. */
double energyIn(const std::vector<float> &v, int from, int to)
{
  double s = 0;
  for (int i = std::max(0, from); i < to && i < (int)v.size(); i++) s += std::fabs((double)v[i]);
  return s;
}
}  // namespace

int main()
{
  std::printf("delay_check — standard delay invariants (ADR-142)\n");

  /* L0-D1 / L0-D2 — spacing and per-generation ratio.
     The read head SLEWS to its target (tape retime), so the first repeat lands
     slightly early and converges; the window is +/-2 ms rather than exact, and
     the RATIO between successive repeats is what pins the feedback law. */
  {
    auto dp = std::make_unique<hypersaw::DelayCore>(kSR);
    auto &d = *dp;
    d.p.timeMs = 100; d.p.feedback = 0.5; d.p.damp = 0.0; d.p.loopHp = 0;
    d.p.crossfeed = 0; d.p.offsetR = 1.0;
    d.snapTime();   // a load, not a knob move — see DelayCore::snapTime
    const int per = (int)std::lround(0.100 * kSR);
    Run r = impulse(d, per * 4 + 400);
    const auto g1 = peakIn(r.L, per - 400, per + 400);
    const auto g2 = peakIn(r.L, 2 * per - 400, 2 * per + 400);
    const auto g3 = peakIn(r.L, 3 * per - 400, 3 * per + 400);
    char buf[128];
    std::snprintf(buf, sizeof buf, "(peaks @ %d / %d / %d, want ~%d / %d / %d)",
                  g1.first, g2.first, g3.first, per, 2 * per, 3 * per);
    check(std::abs(g1.first - per) < 120 && std::abs(g2.first - 2 * per) < 240
              && std::abs(g3.first - 3 * per) < 360,
          "L0-D1 repeats land at the requested spacing", buf);
    const double e1 = energyIn(r.L, per - 400, per + 400);
    const double e2 = energyIn(r.L, 2 * per - 400, 2 * per + 400);
    const double e3 = energyIn(r.L, 3 * per - 400, 3 * per + 400);
    const double r21 = e2 / e1, r32 = e3 / e2;
    std::snprintf(buf, sizeof buf, "(g2/g1 %.4f, g3/g2 %.4f, want 0.5)", r21, r32);
    check(std::fabs(r21 - 0.5) < 0.01 && std::fabs(r32 - 0.5) < 0.01,
          "L0-D2 each generation is feedback x the last", buf);
  }

  /* L0-D3 — feedback 0 is ONE repeat. The failure this catches is a feedback
     path that leaks when the knob says it is shut. */
  {
    auto dp = std::make_unique<hypersaw::DelayCore>(kSR);
    auto &d = *dp;
    d.p.timeMs = 50; d.p.feedback = 0.0; d.p.damp = 0.0; d.p.loopHp = 0;
    d.snapTime();
    const int per = (int)std::lround(0.050 * kSR);
    Run r = impulse(d, per * 3);
    const double first = energyIn(r.L, per - 300, per + 300);
    const auto after = peakIn(r.L, per + 400, per * 3);
    char buf[128];
    std::snprintf(buf, sizeof buf, "(repeat energy %.4f, anything after %.2e)", first, after.second);
    check(first > 0.95 && after.second < 1e-9,
          "L0-D3 feedback 0 leaves exactly one repeat", buf);
  }

  /* L0-D4 — the soft-ceiling law. Past unity the loop must self-oscillate into
     a bounded ceiling, never diverge. Ten seconds is long enough that an
     exponential with gain 1.05 would reach ~1e9 without the limiter. */
  {
    auto dp = std::make_unique<hypersaw::DelayCore>(kSR);
    auto &d = *dp;
    d.p.timeMs = 20; d.p.feedback = 1.08; d.p.damp = 0.2; d.p.loopHp = 20;
    d.snapTime();
    Run r = impulse(d, (int)(kSR * 10), 1.0);
    double mx = 0;
    for (float v : r.L) mx = std::max(mx, (double)std::fabs(v));
    bool finite = true;
    for (float v : r.L) if (!std::isfinite(v)) { finite = false; break; }
    char buf[96];
    std::snprintf(buf, sizeof buf, "(max |x| %.4f over 10 s at fb 1.08)", mx);
    check(finite && mx < 1.3, "L0-D4 past-unity feedback stays bounded", buf);
  }

  /* L0-D5 — ping-pong. An impulse on L only, crossfeed 1: generation 1 must be
     on R, generation 2 back on L. A delay that claims ping-pong while both
     channels carry every repeat is the failure here. */
  {
    auto dp = std::make_unique<hypersaw::DelayCore>(kSR);
    auto &d = *dp;
    d.p.timeMs = 60; d.p.feedback = 0.7; d.p.crossfeed = 1.0; d.p.damp = 0.0; d.p.loopHp = 0;
    d.snapTime();
    const int per = (int)std::lround(0.060 * kSR);
    Run r = impulse(d, per * 3 + 400);
    const auto l1 = peakIn(r.L, per - 300, per + 300);
    const auto r1 = peakIn(r.R, per - 300, per + 300);
    const auto l2 = peakIn(r.L, 2 * per - 400, 2 * per + 400);
    const auto r2 = peakIn(r.R, 2 * per - 400, 2 * per + 400);
    char buf[160];
    std::snprintf(buf, sizeof buf, "(gen1 L %.3f R %.3f | gen2 L %.3f R %.3f)",
                  l1.second, r1.second, l2.second, r2.second);
    check(l1.second > 0.5 && r1.second < 1e-6 && r2.second > 0.2 && l2.second < 1e-6,
          "L0-D5 crossfeed 1 alternates channels", buf);
  }

  /* L0-D6 — sync arithmetic. At 120 BPM a quarter note is 500 ms, so 0.25
     beats is 125 ms; the spacing test must agree with the free-running one. */
  {
    auto dp = std::make_unique<hypersaw::DelayCore>(kSR);
    auto &d = *dp;
    d.setTempo(120.0);
    d.p.sync = 1; d.p.timeBeats = 0.25; d.p.feedback = 0.4; d.p.damp = 0.0; d.p.loopHp = 0;
    const double want = 0.125 * kSR;
    char buf[128];
    std::snprintf(buf, sizeof buf, "(target %.1f samples, want %.1f)", d.delaySamplesL(), want);
    check(std::fabs(d.delaySamplesL() - want) < 0.5,
          "L0-D6 sync: 0.25 beat at 120 BPM is 125 ms", buf);
    // and the R offset rides the synced time, not the free one
    d.p.offsetR = 1.5;
    check(std::fabs(d.delaySamplesR() - want * 1.5) < 0.5,
          "L0-D6b the R offset applies to synced time too");
  }

  /* L0-D7 — the NaN watchdog. Poison the loop and prove one block heals it;
     without this a single NaN is permanent (ADR-032's scar). */
  {
    auto dp = std::make_unique<hypersaw::DelayCore>(kSR);
    auto &d = *dp;
    d.p.timeMs = 30; d.p.feedback = 0.6;
    std::vector<float> L(256, 0.0f), R(256, 0.0f);
    L[0] = std::nanf("");
    d.processStereo(L.data(), R.data(), 256);
    std::vector<float> L2(256, 0.0f), R2(256, 0.0f);
    L2[0] = 1.0f;
    d.processStereo(L2.data(), R2.data(), 256);
    bool clean = true;
    for (float v : L2) if (!std::isfinite(v)) { clean = false; break; }
    check(clean, "L0-D7 NaN watchdog heals the feedback loop");
  }

  /* L0-D8 — silence in, silence out. There is no noise source in this module
     and no RNG; a nonzero output from a zero input would mean state leaking. */
  {
    auto dp = std::make_unique<hypersaw::DelayCore>(kSR);
    auto &d = *dp;
    d.p.feedback = 0.9;
    std::vector<float> L(4096, 0.0f), R(4096, 0.0f);
    d.processStereo(L.data(), R.data(), 4096);
    double mx = 0;
    for (float v : L) mx = std::max(mx, (double)std::fabs(v));
    for (float v : R) mx = std::max(mx, (double)std::fabs(v));
    check(mx == 0.0, "L0-D8 silence in, silence out");
  }

  /* L0-D9 — tone OFF is arithmetic, not a gentler tone. This is the swarm
     delay's complaint turned into a gate: with damp 0 and loopHp 0 the loop
     reproduces feedback EXACTLY (which is what makes L0-D2 meaningful), and
     turning damping up must audibly darken.

     Darkening is measured as PEAK, not as summed magnitude: a one-pole lowpass
     has an all-positive impulse response with unity DC gain, so it preserves
     sum |x| exactly — the two cases measured an identical 0.6000 before this
     was corrected. A lowpass SPREADS a repeat, so its peak falls while its
     sum does not. Same trap as L0-D2's, arriving from the other direction:
     pick the statistic the physics actually moves. */
  {
    auto dryp = std::make_unique<hypersaw::DelayCore>(kSR);
    auto darkp = std::make_unique<hypersaw::DelayCore>(kSR);
    auto &dry = *dryp; auto &dark = *darkp;
    for (auto *d : {&dry, &dark})
    {
      d->p.timeMs = 40; d->p.feedback = 0.6; d->p.loopHp = 0;
    }
    dry.p.damp = 0.0;
    dark.p.damp = 0.8;
    dry.snapTime(); dark.snapTime();
    const int per = (int)std::lround(0.040 * kSR);
    Run a = impulse(dry, per * 3), b = impulse(dark, per * 3);
    // generation 2 sits at 2*per and carries fb^1 of the original energy
    const double aE = energyIn(a.L, 2 * per - 300, 2 * per + 300);
    const double bE = energyIn(b.L, 2 * per - 300, 2 * per + 300);
    const double aP = peakIn(a.L, 2 * per - 300, 2 * per + 300).second;
    const double bP = peakIn(b.L, 2 * per - 300, 2 * per + 300).second;
    char buf[160];
    std::snprintf(buf, sizeof buf, "(gen2 sum dry %.4f damped %.4f | peak dry %.4f damped %.4f)",
                  aE, bE, aP, bP);
    check(std::fabs(aE - 0.6) < 0.005 && bP < aP * 0.5,
          "L0-D9 damp 0 bypasses; damp 0.8 darkens", buf);
  }

  if (failures) std::printf("delay_check: RED (%d failure%s)\n", failures, failures == 1 ? "" : "s");
  else std::printf("delay_check: GREEN (0 failures)\n");
  return failures ? 1 : 0;
}
