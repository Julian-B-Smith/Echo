# tap-swarm-design — the legacy tap swarm as an opt-in option: a design note and a listening prototype, for the poll

- **Queue item:** unqueued. The human asked mid-T-046 (Echo lead session, 2026-11-05) to look at the legacy echo's tap
  distribution ("could make for an ethereal effect... consider appending the behavior as an option"). ROADMAP "Deferred /
  demoted" keeps the swarm-modulated line for after M3, by poll. This change prepares that poll and decides nothing.
- **Why:** the poll needs the legacy engine measured rather than recalled, and shapes that fit Echo's boundary (discrete
  repeats are Echo's; diffusion is Scape's). It needs their cost in keys, gates, CPU and the lock before horde's
  2026-12-11 cutoff, and a prototype to judge by ear.
- **What changed (new files only; no protected path touched):**
  - `docs/design/tap-swarm.md`: the note. It covers the legacy engine measured (§1), the one-line peak-versus-RMS
    constraint behind ADR-142's trap (§2), shapes (a) scatter, (b) smear (flagged as Scape's diffusion) and (c) wander,
    measured (§3), keys, determinism, CPU, clicks, mono, tail and lock costs (§4), a recommendation (§5) and the poll
    questions (§7).
  - `docs/design/tap-swarm-proto.html`: a self-contained, deterministic listening prototype. It has an Echo-like loop,
    the three shapes plus the legacy law, two A/B slots in one AudioWorklet, an impulse-response view, live
    measurements, and 14 self-checks, each with a planted fault.
- **Evidence consulted:**
  - `intake/legacy/swarm-time-lab.html` (lines 157–249, 276–367) and `intake/legacy/ADR-142-standard-delay.md`;
  - CLAUDE.md §Domain; SPEC §1 – §13 and §16.2; `spec/params.json` (via SPEC §9);
  - `core/include/echo/engine.h` (readAt, readHead); `core/manifest.lock.json` and `module_version.h`;
  - `docs/cost.md`; D-041, D-044, D-048 and T-038; ROADMAP Status and Deferred;
  - `intake/module-1.0-bar.md` (B428) and `intake/rack-slot-contract.md` (§6.2, ADR-197 rule E).
- **How the numbers were made:**
  - Every figure was made by scripts outside the repo, which slice the pages' engines and run them in Node (48 kHz).
  - The CPU figures come from a C++ kernel timed against `echo::Echo` at the defaults: Release `-O3 -ffp-contract=off`,
    single-threaded, interleaved, best of 7. **The machine was loaded** (load average 7–10, horde's suite on a core),
    so only ratios to Echo's own default are reported, and they are converted with `docs/cost.md`. That is Layer-E,
    measured.
  - The prototype's self-checks are guaranteed only for the prototype. They gate nothing in `./verify`, since adding
    a gate is a human decision.
- **Alternatives rejected:**
  - A 1/√n feedback normalisation: its peak is 2.73 for 8 taps, which breaks SPEC §5.2 above 37 % feedback.
  - An FDN or allpass loop: lossless, but it is diffusion, and n × 12 s lines do not fit Echo's memory.
  - A separate note per shape: the shapes share §2's constraint and §4's costs.
- **Verify:** `./verify fast` on the commit that lands this trace, run on a clean tree, so `.harness/last-verify.json`
  names it. It ran under background QoS (`taskpolicy -b`, the four efficiency cores) because a horde CPU run was due
  on this machine. The PR quotes the result.
- **Open questions:** everything in the note's §7 belongs to the human's poll. Two further questions are unverified:
  - whether "before" placement keeps enough of the legacy sound; that is for the ear, and the prototype has both;
  - the control-rate drift's CPU figure in Echo's real engine; the kernel models the work and is not the
    implementation.
