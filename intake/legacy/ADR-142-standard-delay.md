## ADR-142 — the standard Delay: a module with no lab, whose oracle is its spec (2026-08-28)

**Status: ACCEPTED (B68 / B73 increment A).** Slot type 9, `src/delay_core.h`,
32 per-slot params (232–263, four blocks of 8 — ADR-131's arithmetic, not
cases). It is simultaneously the A/B baseline the human asked for in B68 and
the reference the swarm delays get judged against in B73.

**Why it exists, measured in `src/time_core.h` rather than asserted.** The
human's report was *"the feedback from their lab didn't really work very
well"*. Three compounding causes, all visible in the source:
1. `fbSig = wetRaw / n` is ADR-031's worst-case-correlation norm. At the
   default 8 taps the audible loop gain at regen 0.9 is ≈0.32 — repeats die
   in two or three generations and the edge-of-oscillation zone, the musical
   heart of a delay, is unreachable at any knob setting.
2. Every tap averages into ONE write head, so each generation re-smears
   through the whole swarm: a wash, not repeats.
3. `tanh` sits at unity in the loop and damping is always in circuit, so
   every pass ducks and dulls even when the patch asked for neither.

**THIS CORE HAS NO HTML LAB, DELIBERATELY**, and that is a real amendment to
ADR-003's spec-in-code rule for this one module. Writing a lab first would
have reproduced the rejected law; B73's roadmap entry is the standing license
to diverge. So the file IS the spec and `tools/delay_check.cpp` IS the
correctness definition — ten impulse-response invariants rather than parity
against a reference that would only enshrine the defect.

**Three bugs the oracle found in its own author's work, each fixed at the
source rather than papered over in the test:**
- **The limiter shaped the dry path.** `softLimit(in + loop*fb)` compressed a
  full-scale input entering a delay with feedback 0 — measured 0.976 where it
  should be 1.000. It now limits only the returning term, so the input path is
  arithmetically clean and past-unity feedback still parks at a ceiling (the
  fixed point of x = limit(1.08x) is ≈1.0; L0-D4 measures 1.0000 over 10 s).
- **Damping was always in circuit** — the exact complaint this module was
  written to answer, reproduced by its author. `damp = 0` and `loopHp = 0` now
  BYPASS their filters; "open at 18 kHz" is still a one-pole colouring every
  pass at 48 kHz, and it showed as a measurable error in the generation ratio.
  With true bypass the ratio is EXACTLY 0.5000 at feedback 0.5.
- **Nothing snapped the read head on load.** A knob move should glide (tape
  retime) but a preset arriving should not: repeats landed late and pitched
  until `snapTime()` existed. The rack calls it when a slot becomes a Delay.

**Two measurement traps recorded, because both would have passed a careless
test.** Peak amplitude is NOT loop gain — a fractional read splits an impulse
across two samples, so peaks read 0.4735 where the gain was exactly 0.5;
summed magnitude is invariant under that split. And summed magnitude is NOT
brightness — a one-pole lowpass has unity DC gain, so it preserves the sum
exactly (dry and damped both measured 0.6000); darkening shows in the PEAK
(0.6000 vs 0.0632). Pick the statistic the physics actually moves.

**Storage is float, on the heap.** Two double-buffered cores are 8 MB and
segfaulted the oracle on the stack. Float halves it, the loop state stays
double where the recursion lives, and the rack holds `unique_ptr`s as it
already does for TimeCore. rtsafety stays GREEN: allocation happens only at
construction.

**Deferred with reason:** M-S mode. The human listed "std/M-S/ping-pong";
crossfeed = 1 IS ping-pong, and M-S needs a mid/side seam the rack does not
have. Named here rather than silently dropped.

**The oracle is NOT yet in `./verify`** — adding a gate is a human decision
(charter). It builds and passes; the test row says `none` rather than naming
it, because a row claiming a gate that does not run is the false coverage the
table exists to prevent.

