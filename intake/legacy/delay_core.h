/*
 * delay_core.h — the STANDARD stereo delay (B68/B73 increment A, ADR-142).
 *
 * THIS CORE HAS NO HTML LAB, AND THAT IS DELIBERATE. Every other core here is
 * a port of a prototype (ADR-003 spec-in-code), and correctness means parity
 * with that prototype. There is no delay lab, and writing one first would
 * reproduce the exact thing the human rejected: `reference/swarmtime.html`'s feedback
 * law is what "didn't really work very well". So this file IS the spec, and
 * its oracle is `tools/delay_check.cpp` — impulse-response invariants
 * (spacing, per-generation ratio, boundedness, ping-pong alternation, sync
 * arithmetic) rather than sample-parity against a reference that would only
 * enshrine the defect. B73's roadmap entry is the standing license to diverge.
 *
 * WHY THE SWARM DELAY NEEDED A SIBLING (measured in src/time_core.h):
 *   - `fbSig = wetRaw / n` is ADR-031's worst-case-correlation norm. With the
 *     default 8 taps the audible loop gain at regen 0.9 is ~0.32: repeats die
 *     in two or three generations and the edge-of-oscillation zone — the
 *     musical heart of a delay — is unreachable at any knob setting.
 *   - Every tap averages into ONE write head, so each generation re-smears
 *     through the whole swarm. That is a wash, not a repeat.
 *   - `tanh` sits at unity in the loop and damping is always in circuit, so
 *     every pass ducks and dulls even when the patch asked for neither.
 * This core answers each: single-tap-per-channel feedback (loop gain IS the
 * knob), saturation only above a threshold, damping still in-loop because
 * that is what a delay's repeats are supposed to do — but a gentle one-pole
 * whose cutoff the player owns.
 *
 * REAL-TIME CONTRACT: buffers are preallocated at construct; processStereo()
 * allocates nothing, reads no clock, and calls nothing external. Seeded RNG
 * is not merely absent but unnecessary — there is no stochastic element here,
 * which is why this module carries none of the unseeded-RNG blocker that
 * parks CANTO/WARP/STATION.
 */
#pragma once

#include <cmath>
#include <cstring>

namespace hypersaw
{

class DelayCore
{
 public:
  /* 1<<18 at 96 kHz is 2.7 s — past the 2 s maximum the time knob offers, with
     room for the sync grid's slowest division at low tempo. FLOAT storage, and
     the reason is arithmetic rather than taste: a double pair of these is 4 MB,
     which put TWO cores over the 8 MB stack and segfaulted the oracle on the
     spot. Float halves it, the loop STATE stays double where the recursion
     lives, and 24 bits of mantissa is well past the noise floor of a delay
     line. Instances still belong on the heap (the rack holds unique_ptrs, as
     it already does for TimeCore) — a 2 MB member is not a stack object. */
  static constexpr int kBuf = 1 << 18;

  struct Params
  {
    double timeMs = 375;     // free-running delay, ms (L; R is timeMs * offsetR)
    double sync = 0;         // 0 = free (timeMs), 1 = tempo-locked (timeBeats)
    double timeBeats = 0.5;  // beats per repeat when sync != 0
    double offsetR = 1.0;    // R time as a ratio of L — 1.0 is dead centre
    double feedback = 0.35;  // 0..1.08; >1 self-oscillates into the soft ceiling
    double crossfeed = 0;    // 0 = independent channels, 1 = ping-pong
    double damp = 0.35;      // in-loop one-pole LP: 0 = OFF (bypassed), 1 dark
    double loopHp = 60;      // in-loop high-pass, Hz — the "in-line filter"; 0 = OFF
  };

  Params p;

  explicit DelayCore(double sampleRate) : sr(sampleRate > 0 ? sampleRate : 44100.0)
  {
    std::memset(bufL, 0, sizeof bufL);
    std::memset(bufR, 0, sizeof bufR);
    // Start the smoothers AT the target rather than at zero: a delay whose
    // first block slews up from 0 ms would chirp on every instantiation.
    snapTime();
  }

  void setSampleRate(double sampleRate)
  {
    if (sampleRate <= 0 || sampleRate == sr) return;
    sr = sampleRate;
    reset();
  }

  // Host tempo, pushed by the shell each block. Kept as data, never read from
  // a clock in here (SPEC §5.7: no wall-clock reads in a core).
  void setTempo(double bpm) { tempo = bpm > 1 ? bpm : 120.0; }

  /* GLIDE ON A KNOB MOVE, SNAP ON A LOAD. The tape retime is the point when
     a player turns the time knob, and a fault when a preset arrives: a patch
     saved at 100 ms would spend its first ~50 ms sliding down from whatever
     the previous patch held, so the first repeat lands late and pitched. The
     shell calls this when a slot is (re)selected or a state is applied; the
     oracle calls it after setting params, which is the same event. */
  void snapTime()
  {
    dSmL = targetSamples(false);
    dSmR = targetSamples(true);
  }

  void reset()
  {
    std::memset(bufL, 0, sizeof bufL);
    std::memset(bufR, 0, sizeof bufR);
    wL = wR = 0;
    lpL = lpR = hpL = hpR = 0;
    snapTime();
  }

  /* NaN watchdog (ADR-032, same shape as TimeCore's heal). A NaN reaching a
     feedback line renders as permanent silence-or-scream that survives every
     parameter change, because the NaN keeps circulating. Cheap to check once
     per block; impossible to recover from without it. */
  void heal()
  {
    if (std::isfinite(lpL + lpR + hpL + hpR + dSmL + dSmR)) return;
    reset();
  }

  void processStereo(float *L, float *R, int n)
  {
    heal();
    const double tgtL = targetSamples(false), tgtR = targetSamples(true);
    /* OFF MEANS OFF, and this is the whole complaint about the swarm delay
       written as code. There, damping is always in circuit, so every repeat
       dulls even when the patch asked for none. Here `damp = 0` and
       `loopHp = 0` BYPASS their filters rather than setting them to a
       nominally-open cutoff — "open" at 18 kHz is still a one-pole colouring
       every pass at 48 kHz, and it showed up as a measurable error in the
       per-generation ratio before this branch existed. A player who turns
       tone off gets arithmetic, not a gentler flavour of tone. */
    const bool useLp = p.damp > 1e-6;
    const bool useHp = p.loopHp > 1e-6;
    // Damping: just-above-0 -> ~18 kHz, 1 -> ~400 Hz.
    const double dampHz = 18000.0 * std::pow(0.022, clamp01(p.damp));
    const double aLp = 1.0 - std::exp(-6.283185307179586 * dampHz / sr);
    const double hpHz = p.loopHp < 5 ? 5 : (p.loopHp > 2000 ? 2000 : p.loopHp);
    const double aHp = 1.0 - std::exp(-6.283185307179586 * hpHz / sr);
    const double fb = p.feedback < 0 ? 0 : (p.feedback > 1.08 ? 1.08 : p.feedback);
    const double x = clamp01(p.crossfeed);

    for (int i = 0; i < n; i++)
    {
      /* TAPE-STYLE RETIME. The read head slews toward the new delay rather
         than jumping, so a time change (or a tempo change under sync) pitches
         smoothly instead of clicking. 0.0015 per sample is TimeCore's own
         slew, reused deliberately: two delay modules that retime at visibly
         different speeds would read as a bug in one of them. */
      dSmL += (tgtL - dSmL) * 0.0015;
      dSmR += (tgtR - dSmR) * 0.0015;

      const double rdL = readLerp(bufL, wL, dSmL);
      const double rdR = readLerp(bufR, wR, dSmR);

      // CROSSFEED, and ping-pong is simply its endpoint: at x = 1 each line
      // feeds the other entirely, so a repeat alternates channels. No separate
      // "mode" enum can disagree with the mix knob about what is happening.
      const double fbL = rdL * (1 - x) + rdR * x;
      const double fbR = rdR * (1 - x) + rdL * x;

      // In-loop tone: LP then HP, both one-pole, both INSIDE the feedback so
      // successive repeats darken and thin — the behaviour that makes a delay
      // sit behind the source instead of competing with it.
      double toneL = fbL, toneR = fbR;
      if (useLp)
      {
        lpL += aLp * (toneL - lpL);
        lpR += aLp * (toneR - lpR);
        toneL = lpL; toneR = lpR;
      }
      if (useHp)
      {
        hpL += aHp * (toneL - hpL);
        hpR += aHp * (toneR - hpR);
        toneL -= hpL; toneR -= hpR;
      }
      const double loopL = toneL;
      const double loopR = toneR;

      /* SOFT CEILING ON THE FEEDBACK ONLY — not on the sum, and the
         difference is measurable. Limiting the sum shapes the DRY signal on
         its way in: a full-scale impulse entering a delay with feedback 0 came
         back at 0.976 (measured), which is the delay quietly compressing
         material it was told not to touch. Limiting only the returning term
         leaves the input path arithmetically clean while still bounding the
         loop — the fixed point of x = limit(1.08x) is ~1.0, so past-unity
         feedback still parks at a ceiling instead of diverging. */
      const double inL = (double)L[i] + softLimit(loopL * fb);
      const double inR = (double)R[i] + softLimit(loopR * fb);

      bufL[wL] = (float)inL;
      bufR[wR] = (float)inR;
      wL = (wL + 1) & (kBuf - 1);
      wR = (wR + 1) & (kBuf - 1);

      // The WET output is the pre-feedback tap: the rack's own dry/wet does
      // all blending (the ADR-131 contract), so this core never mixes.
      L[i] = (float)rdL;
      R[i] = (float)rdR;
    }
  }

  // Exposed for the oracle: what the read head is currently seeking, in samples.
  double delaySamplesL() const { return targetSamples(false); }
  double delaySamplesR() const { return targetSamples(true); }

 private:
  static double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

  static double softLimit(double v)
  {
    // Linear below the knee; tanh-shaped above it, continuous at the knee.
    constexpr double kKnee = 0.9;
    const double a = v < 0 ? -v : v;
    if (a <= kKnee) return v;
    const double over = a - kKnee;
    const double shaped = kKnee + (1.0 - kKnee) * std::tanh(over / (1.0 - kKnee));
    return v < 0 ? -shaped : shaped;
  }

  double targetSamples(bool right) const
  {
    /* SYNC IS ARITHMETIC, NOT A TABLE. beats * (60/bpm) * sr is exact for any
       division the GUI offers, so adding a dotted or triplet value is a GUI
       change and never a core change. */
    double ms;
    if (p.sync >= 0.5)
    {
      const double beats = p.timeBeats < 0.01 ? 0.01 : p.timeBeats;
      ms = beats * (60000.0 / tempo);
    }
    else
    {
      ms = p.timeMs;
    }
    if (right)
    {
      const double r = p.offsetR < 0.05 ? 0.05 : (p.offsetR > 4.0 ? 4.0 : p.offsetR);
      ms *= r;
    }
    double s = ms * 0.001 * sr;
    if (s < 1.0) s = 1.0;
    if (s > (double)kBuf - 4.0) s = (double)kBuf - 4.0;
    return s;
  }

  static double readLerp(const float *buf, int w, double d)
  {
    /* GUARD BOTH ENDS OF THE WRAP. TimeCore records this trap from the mod-lab
       sweep: rp += kBuf on a tiny negative yields exactly kBuf under rounding,
       and i0 then indexes one past the end — an out-of-bounds read on the
       audio thread. kBuf is a power of two here, so the mask does the wrap and
       the failure mode cannot arise; the check stays as the statement of why. */
    double rp = (double)w - d;
    if (rp < 0) rp += (double)kBuf;
    if (rp >= (double)kBuf) rp -= (double)kBuf;
    const int i0 = ((int)rp) & (kBuf - 1);
    const double fr = rp - (double)(int)rp;
    const int i1 = (i0 + 1) & (kBuf - 1);
    return (double)buf[i0] * (1.0 - fr) + (double)buf[i1] * fr;
  }

  double sr = 44100.0;
  double tempo = 120.0;
  float bufL[kBuf];
  float bufR[kBuf];
  int wL = 0, wR = 0;
  double dSmL = 0, dSmR = 0;
  double lpL = 0, lpR = 0, hpL = 0, hpR = 0;
};

}  // namespace hypersaw
