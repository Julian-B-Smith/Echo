# Agent Charter — Echo

Everything above §Domain is the invariant harness layer. Do not edit it
per-project. Project-specific facts live in §Domain and in ROADMAP.md.
**The global doctrine (imported via `~/.claude/CLAUDE.md`) applies on top of
this charter and is not restated here** — this file carries only what doctrine
doesn't: the operational contract of THIS harness. (Context budget: slimmed
2026-07-16, Decision 28.)

## Truth contract

- **ROADMAP.md is the single source of truth.** Task state, acceptance
  criteria, invariants, and open questions live there and only there. If the
  conversation and ROADMAP.md disagree, ROADMAP.md wins; if ROADMAP.md is
  wrong, fixing it is the first task.
- **Passing ≠ done.** Done = `./verify full` green AND the ROADMAP acceptance
  criteria satisfied AND a trace entry written in `traces/`. Never collapse
  these into each other.
- **Grounded refusal is a success class.** "I cannot do this within the brief
  because X" with evidence is a correct output. Guessing to appear productive
  is a failure.

## Provenance

- Every nontrivial claim about the codebase must cite its evidence: a file
  path and line, a verify run, or a ROADMAP entry. No provenance → phrase it
  as a hypothesis, not a fact.
- Every merged change gets an entry in `traces/` (see the provenance skill):
  what changed, why, evidence consulted, verify result + git hash.

## Delegation policy (lead session)

- The lead plans, delegates, integrates, and is the **only** writer of
  ROADMAP.md. Subagents never touch it.
- Delegation briefs are self-contained: subagents start with zero conversation
  history. Every brief states (1) files in scope, (2) acceptance criteria
  copied verbatim from ROADMAP.md, (3) the verify target, (4) what is
  explicitly out of scope.
- Use built-in Explore for codebase reconnaissance. Use `implementer` for
  scoped changes, `verifier` for oracle runs, `critic` (Opus) for adversarial
  review of anything architectural, irreversible, or touching an invariant.
- One queue item per implementer dispatch. Parallel dispatches only for items
  with disjoint file scopes.
- Do not start work on an item whose acceptance criteria are missing or
  ambiguous. Surface the gap to the human; that is the deliverable.

## Oracle discipline

- Run `./verify fast` after any change set; `./verify full` before declaring
  a queue item done. Report oracle output verbatim — never summarize a failure
  into vagueness.
- A red oracle halts forward work. Fix or revert; do not stack changes on red.
- Never weaken a gate (skip a test, relax a threshold, mark xfail) without an
  explicit human decision recorded in ROADMAP.md.

## Human gates

Stop and ask before: deleting files, changing the public interface of
anything, editing `./verify` or the gates it runs, adding a dependency,
any git operation beyond add/commit on the working branch, and anything §Domain
lists as protected.

---

## §Domain — Echo

**What this is.** Echo, horde's standard delay: a C++ stereo delay engine
library that HYPERSAW's horde hosts as one module in its FX rack, plus a browser
lab twin for listening (`lab/`, not yet built). Not a plugin of its own (survey
Q1). Boundary ruled by horde: diffusion is Scape's, graph morphing is Sluice's,
**discrete repeats are Echo's**. Provenance, scope and horde's contract needs:
`SPINUP-BRIEF.md`; the contract itself: `intake/rack-slot-contract.md` and
`intake/module-1.0-bar.md` (horde module cutoff **2026-12-11**).

**Stack & entrypoints.** C++20 header-only core `core/include/echo/engine.h`
(`echo::Echo`, behind a local ABI shim until FOUNDATIONS' FX-operator ABI lands
in M3), its suite `core/tests/core_tests.cpp` (built twice: `echo_tests` clean,
`echo_plants` with every planted fault in `plants.h` switchable). CMake
`-G "Unix Makefiles"`, ABSOLUTE build paths (`build-core/`), Release for any perf
number (global CLAUDE.md audio section). The single-file lab `lab/` is run
headless by plain Node oracles in `tools/`. Oracle: `./verify fast|full`.

**Domain invariants** (the critic checks against these):
- Deterministic: seeded mulberry32 streams only; no `Math.random`, no wall-clock
  read in any DSP path. Tempo arrives as data. Same seed + params + input →
  bit-identical output.
- Real time: nothing allocates, locks or makes a system call on the audio
  thread. **Every delay buffer is sized at prepare** for the 12 s maximum
  (manifest `max_delay`); flush-to-zero set inside `process`, host flags saved
  and restored (RS-7.8).
- Units: times in s/ms, rates per second, converted at the current sample rate
  (horde ADR-009). SR and block-size independence are TESTED, not assumed. The
  rack runs at the host rate (ADR-237); any oversampling is internal.
- Feedback (ADR-142's traps, `intake/legacy/ADR-142-standard-delay.md`): the
  ceiling acts on the returning term only; never divide loop gain by tap count;
  never limit the dry path; filters are truly out of circuit when off; a preset
  load never glides the read head.
- Tails: declared (RS-7.3–7.6) — finite below unity feedback, `kInfiniteTail`
  when held; exact zero when the declared tail ends; silence in → silence out.
  Latency zero or declared in whole samples (RS-7.1).
- Parameter classes declared (RS-4.1): continuous glides, stepped flips
  click-free or `gate`-ducks, Device class never morphs. A moving delay time
  does what its modulation mode says (repitch / crossfade / jump), tested
  against the click metric.
- Stereo is mono-safe (B408): ping-pong and mid/side fold to mono without
  unintended cancellation — measured and stated.
- No fused multiply-add in the core: the header scopes `fp-contract` off itself
  (never a CMake INTERFACE flag, which would leak into horde), proven by a hash probe.
- Every detector has a must-fail planted control before it gates anything; a
  plant no check guards is itself a failure (`echo_plants` cross-checks).
  Goldens are pinned only once the C++ exists (ADR-142: parity never enshrines
  a defect). The legacy delay and swarm lab are references, never parity targets.
- CPU is measured in Release as a ratio to a calibration loop, never gated.
  Memory is stated, all allocated at prepare.

**Alias note.** This repo is PUBLIC. Private siblings are named by alias in
every tracked file; the map lives in untracked `PRIVATE-NOTES.md`, enforced by
`private_name_gate` from untracked `.leakcheck-names`.

**Legal note.** Legal and IP analysis (marks, patents, clearance readings) is
never tracked: it lives in gitignored `local/legal/`, with a neutral pointer in
tracked files (D-014), enforced by `legal_gate`.

**Protected paths** (human gate to modify): `./verify` and `.kit/`;
`project.manifest.json`; `intake/` (horde's record, sha256-pinned by
`intake_gate` — annotate in docs/, never edit); `spec/` and `lab/` once the
human ratifies them; goldens once cut.

**Verify targets.** `fast` (~20 s warm, ~90 s from a cold build): kit integrity,
leak, private-name, structure, manifest, intake pin, legal gate, the lab smoke,
and the core gate (`-Werror` Release build of `core/`; the M1 suite clean and
under each of its planted faults; the C++ table against `spec/params.json`; the
fp-contract probe; the fast-math guard). `full` = `fast` until M3's goldens and
sweeps. CI runs `fast` on Linux/GCC, then the core suite under ASan + UBSan.

---

<!-- KNOWLEDGE-LOOP:START -->` and `<!-- KNOWLEDGE-LOOP:END -->
