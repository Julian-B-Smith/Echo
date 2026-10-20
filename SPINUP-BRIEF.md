# Spin-up brief: ECHO (delay), a new project in synthetic-worlds

**Provenance.** Written by the HYPERSAW (horde) lead on 2026-10-19, at the human's request: "I feel like I should
start developing the delay module (we've been calling Echo, though that will probably change) so it can be on the
same schedule as everything else. Please prepare the directory and the contract packet and orientation details and
I'll spin it up and create a repo."

By poll the same day, the human reversed horde's B425 ruling of 2026-10-04 ("ECHO stays in horde, no sibling"):
"Yes, a sibling". The human also chose the folder and repository name "Echo", public.

Motivating records in HYPERSAW:
- ROADMAP B425 (ECHO, horde's standard delay), B68 and B73 (the delay against the swarm time engines, and feedback
  designed rather than ported), B386 (the legacy roundup), B393 (the FX roster), B408 (true stereo) and B433 (name
  clearance);
- ADR-142 (the standard Delay), ADR-188 (corner patches), ADR-190 A8 (ECHO confirmed in 1.0), ADR-201 (the rack slot
  contract) and ADR-237 (the rack runs at the host rate).

HYPERSAW was at commit 4d2288f when this directory was made.

## What ECHO is for

horde's standard delay: the module a producer reaches for first, hosted in horde's FX rack beside Shriek (saturation),
Sluice (an experimental FX network), Scape (reverb) and Bulwark (dynamics). The boundary horde has already ruled:
**diffusion makes it Scape, discrete repeats make it ECHO, morphing the graph makes it Sluice.**

**The human's scope, in the human's words (2026-10-19):** "the standard delay capabilities; sync vs time, L/R offsets,
ping pong vs mid/side vs standard, feedback; different modes for delay modulation including repitch, and
input/delay-line/output filters. Also interesting visualizers, possibly including impulse response."

Read as a first scope list, for the survey to confirm:
- **Time:** free time (ms) or tempo sync by horde's division names (`intake/tempo-divisions.md`), with left and right
  offsets.
- **Routing:** standard (dual mono or stereo), ping-pong, and mid/side.
- **Feedback,** with filters in three places: input, in the loop (the delay line), and output.
- **Modulation modes,** including **repitch**, the tape-style pitch glide when the time changes, against alternatives
  such as a crossfade jump or a wobble or chorus on the line.
- **Visualizers,** possibly including the **impulse response**: the repeats drawn as they will sound.

## What is in this directory

- **`intake/`: the contract packet.** These are copies of horde's own documents. horde's copies stay the record; yours
  evolve.
  - `rack-slot-contract.md`: the contract every hosted module meets (ADR-201, ratified 2026-10-09; amendments in §12).
    Read it first.
  - `module-1.0-bar.md`: the minimum a module meets to ship in horde 1.0, including the CPU and memory budgets.
  - `module-io-gain.md`: the I/O gain standard (input and output gain, meter taps, clip latches).
  - `SPEC-MODULE-MACROS.md`: macros. Up to 8, carrying each preset's own labels. (Its older four-role face is retired
    by ADR-169 A4; the contract and the 1.0 bar state the current rule.)
  - `tempo-divisions.md`: horde's sync vocabulary, the rule for storing divisions by name, and how tempo arrives.
- **`intake/legacy/`: what ECHO absorbs from horde Legacy** (B425). These are references, **never parity targets**.
  - `ADR-142-standard-delay.md`: why horde's legacy Delay was designed rather than ported. Its findings are the traps
    to avoid:
    - loop gain divided by the tap count kills the edge of oscillation;
    - a limiter on the dry path;
    - damping always in circuit;
    - a read head that glided when a preset loaded.
  - `delay_core.h`, `delay_check.cpp`: that delay and its oracle, ten impulse-response invariants. The oracle's shape
    (time accuracy, generation ratio = feedback exactly, the ceiling, filters truly bypassed) is a good start for
    ECHO's own.
  - `swarm-time-lab.html`: the legacy swarm time engines (echo and room on Kuramoto-coupled lines). B68 asked for an
    A/B of the plain delay against these "special" ones. A swarm-modulated line is a candidate modulation mode.

## What horde needs from ECHO (the consumer's side of the contract)

The contract is `intake/rack-slot-contract.md`, and the shipping bar is `intake/module-1.0-bar.md`. The points a
delay hits hardest:

- **Determinism.** The same input and seed give an identical output. Seeded mulberry32 streams only, no wall clock.
  Tempo arrives as data.
- **Real time.** Nothing allocates, locks or makes a system call on the audio thread. **Every delay buffer is sized at
  prepare** for the maximum delay time, so the maximum time is a design decision with a stated memory figure (a MUST
  row). Flush-to-zero is set inside `process`, saving and restoring the host's flags (RS-7.8).
- **Units.** Times in seconds or milliseconds, rates per second, converted at the current sample rate (horde ADR-009's
  trap). Sample-rate and block-size independence are tested, not assumed.
- **Tails.** Declare the tail (RS-7.3–7.6): finite at feedback below 1, `kInfiniteTail` where the loop is held. Reach
  **exact zero** when the declared tail ends. Silence in gives silence out.
- **Latency.** Zero, or declared in whole samples (RS-7.1).
- **The rack runs at the host rate** (ADR-237). Any oversampling is internal.
- **Morph and presets.**
  - A preset is a horde FX-slot preset. horde's morph corners can each hold an ECHO patch (ADR-188).
  - Declare each parameter's class (RS-4.1): continuous values glide, stepped ones flip with a click-free switch or a
    `gate` duck (RS-4.3), and Device-class settings never morph.
  - The optional morph-position call (RS-4.10) exists, if a costly design needs it.
- **Delay time under morph and automation** is the hard case. Decide what a moving time does (repitch, crossfade or
  jump) per modulation mode, and test it against the click metric.
- **I/O gain standard,** **macros** (up to 8), **undo of every control,** and **factory presets** (an init plus a
  small set).
- **Stereo and mono safety** (B408): mid/side and ping-pong must fold to mono without unintended cancellation. Measure
  and state it.
- **Budgets** (1.0 bar appendix): about **2 % of a min-spec core at defaults, 4 % at worst**, and **4 % with one
  parameter automated or under a morph sweep**. CPU is measured and reported, never a verify gate. Memory is stated,
  all of it allocated at prepare.
- **Screens.** The human wants each module to have its own screen style, plus a backup UI in horde's house style
  (horde's FX screens workshop). The visualizers live here.
- **The name.** "Echo" is a placeholder. Any user-visible name goes through horde's B433 clearance screen before 1.0.
- **Schedule.** horde's module cutoff is **2026-12-11**. A module meets every MUST row by then to ship in 1.0;
  otherwise it ships after.

## Your first act: the spin-up survey, not code

Run the autonomous kit's spin-up survey (`/spinup`; ONBOARDING.md Part 2), commit `project.manifest.json`, then
scaffold from it. Here are the nine questions, with the lead's PROPOSED answers for the human to keep or strike:

1. **What is it?** A stereo delay engine library with a lab twin for listening. horde consumes the library. A
   standalone plugin is optional, later.
2. **Architecture rung.** One thread plus a read-only verifier. A fleet is not earned.
3. **Domain core.**
   - Stereo delay lines with fractional reads; free or synced time, with L/R offsets.
   - Standard, ping-pong and mid/side routing.
   - Feedback with a ceiling on the returning term only (ADR-142's fix).
   - Input, loop and output filters, with true bypass when they are off.
   - Modulation modes, repitch among them.
   - Seeded and deterministic, with no model in the path.
4. **Oracle shape.** The spec and impulse-response invariants first (ADR-142's lesson, so parity never enshrines a
   defect):
   - time accuracy in samples;
   - the generation ratio equals the feedback exactly, with the filters bypassed;
   - the ceiling holds past unity;
   - sample-rate and block-size independence;
   - silence to exact zero;
   - modulation depth and repitch accuracy in cents;
   - mono-fold metrics.

   Every detector has a must-fail control. Goldens are pinned per preset once the C++ exists.
5. **Second implementation?** The lab is a listening twin, not a parity target. The C++ core is the reference from
   day one, judged by its invariants.
6. **Consumers.**
   - **Provider to HYPERSAW (horde).** Your inbound mailbox is `integrations/<sender>/` here. Your filings to horde go
     in horde's tree under `integrations/echo/`, through a PR touching only that path (writes stay home).
   - **Consumer of FOUNDATIONS**' FX operator ABI, if it fits (Scape vendors a pinned `fx_operator.h`).
7. **Parallel audit thread?** No.
8. **Knowledge-loop tags:** time accuracy, feedback stability, modulation and repitch, stereo and mono safety,
   sample-rate independence, the CPU and memory budget.
9. **Lifespan and autonomy.** A long-lived product. Medium autonomy: PR-per-batch, and the human merges. The spec and
   the lab become protected paths once the survey ratifies them.

## Open for the human, at the survey

- **The maximum delay time.** It sets the memory figure. For example, 2/1 at 40 bpm is 12 s per channel.
- **Extras beyond the scope list:** freeze or infinite hold, reverse, ducking, a diffusion or smear amount (careful:
  diffusion is Scape's), and a swarm-modulated line from the legacy time engines.
- **A standalone plugin,** or only the library horde hosts?
- **The repitch law,** and what a synced time does when the tempo changes mid-note: glide or jump.
