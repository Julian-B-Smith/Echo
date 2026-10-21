# ROADMAP — Echo

Single source of truth. Only the lead session (or the human) edits this file.
State lives here; conversations are ephemeral. The *why* behind ratified choices
lives in `DECISIONS.md`; horde's shipping bar is `intake/module-1.0-bar.md`.

## Status

- **Phase:** Phase 0 CLOSED 2026-10-20 (D-013). Phase A CLOSED 2026-10-20 (PR #4;
  D-015–D-017). **M1 CLOSED 2026-10-21** (PR #7 merged, 5c3e822; T-006;
  D-018–D-022): the C++ core and its 39-check suite, 52 planted faults caught,
  green on Mac/clang and Linux/GCC, ASan + UBSan clean in CI. **M2 CLOSED 2026-10-21**
  (PR #10 merged, 4c68da4; T-009; D-023–D-026): both repitch laws, the tempo modes and
  modulation; 52 checks, 75 planted faults caught. **M3 (rack admission, the 1.0 bar,
  due 2026-12-11) is next.**
- **Oracle:** `./verify fast` = kit integrity, leak gate, private-name gate,
  structure, manifest, intake pin, legal gate, **lab smoke** (`tools/lab_smoke.mjs`,
  ~7 s), and the **core gate**: a warning-free Release build of `core/`, the M1
  invariant suite on the clean build (`echo_tests`), the same suite under each of
  its 32 planted faults (`echo_plants`, each must be caught), the C++ table
  against `spec/params.json`, and the fast-math guard probe (~10 s warm, ~70 s
  from a cold build). `full` = `fast` for now (goldens and sweeps arrive with M3).
- **Hard date:** horde's module cutoff **2026-12-11**. Every MUST row of the 1.0
  bar met by then, or Echo ships after 1.0.
- **Last human ratification:** 2026-10-20 — the M1 rulings by poll: rest waits for
  the wet (D-018), the tail clock (D-019), a load fades the old repeats under their
  old settings (D-020); before them, the spec, revision 2 (D-017).

## Phases (each gate is a checkable condition)

### Phase 0 — Prior-art landscape (agent swarm)
Delay design literature (fractional delay interpolation, tape-style repitch,
crossfade time changes, ping-pong / mid-side mono safety, feedback-path
filters, impulse-response visualization), competing delays' public feature
sets, known failure modes, and a first name-collision check for "Echo".
Clean-room: published literature and listening only; never reverse-engineer a
commercial delay or borrow its mode names, presets or UI text.
- **Gate:** `docs/prior-art.md` exists, dated and cited; DECISIONS records what
  it changes (reuse / avoid / differentiate). Name collision findings go to the
  human.

### Phase A — Spec + lab twin
- `spec/` — the scope list (1.0 bar row 23) and the parameter manifest: every
  key, unit, range, default, morph class (RS-4.1) and rate class; routing
  (standard / ping-pong / mid-side); free vs synced time with horde's division
  names (`intake/tempo-divisions.md`) and L/R offsets; the 12 s line and its
  clamp; the three filter positions with true bypass; the modulation modes.
- `lab/` — single-file browser listening twin (not a parity target), seeded,
  wall-clock-free, run headless by a Node smoke in `tools/`.
- **Decide by listening, then ADR:** Q-A1 and Q-A2 below; rule Q-A4 in the spec.
- **Gate:** the human ratifies `spec/` (then it is protected); `./verify fast`
  runs the lab determinism smoke (seeded → bit-identical; silence → exact
  silence; finite); Q-A1, Q-A2, Q-A4, Q-A5 and S-1 closed in DECISIONS (Q-A5 and
  S-1 were raised by the spec draft, D-015; added here by the lead, 2026-10-20).

### M1 — C++ core: lines, time, routing, feedback, filters
- `core/` C++20 library; buffers sized at prepare for 12 s at the host rate;
  FTZ inside `process` with the host's flags restored.
- **Invariant suite**, each detector with a planted must-fail control:
  time accuracy in samples; generation ratio = feedback exactly (filters
  bypassed); ceiling holds past unity on the returning term only; filters truly
  bypassed; SR independence at 44.1/48/96/192 kHz; block sizes 1…4096;
  silence → exact zero; tail reaches exact zero when declared; determinism;
  no allocation in `process`/`setParam`/`reset`; finite under NaN/Inf injection.
- **Gate:** the suite runs in `./verify fast`, green; every planted control
  proven to fire (recorded in a trace); memory figure stated per instance at
  192 kHz worst case.
- **Status: CLOSED 2026-10-21** (PR #7, 5c3e822; T-006): 39 checks green,
  52 of 52 planted faults caught (`traces/2026-10-20-m1-core.md`), after an
  independent Opus critic review (FIX-THEN-SHIP: 4 HIGH, 8 MED, all fixed or
  recorded; D-018–D-020). Memory measured at prepare: **17.61 MiB per instance at
  192 kHz** (4.40 MiB at 48 kHz), all of it the two lines. M1 placeholders M2
  replaces: repitch acts as a crossfade of `time.xfadeMs`; every tempo move is a
  crossfade whatever `time.tempoMode`; `mod.*` keys are stored but inert. Meters,
  the ABI swap and state-by-name are M3.

### M2 — Modulation modes and moving time
- Repitch (per Q-A1), crossfade jump, and wobble/chorus on the line; what a
  moving time does under automation and morph; tempo changes (Q-A2).
- **Gate:** repitch accuracy in cents and modulation depth tested; the click
  metric (with a planted-click control) passes under a 1 Hz full-range time
  sweep in every mode.
- **Status: CLOSED 2026-10-21** (PR #10, 4c68da4; T-009): both repitch laws
  (tape: a varispeed model with a quadratic 32-sample history; exponential),
  the 12 st cap, the 3 s fallback, the speed backstop (zero hits required),
  the three tempo modes, sine and seeded-drift modulation. The suite is 52
  checks with 75 planted faults caught (`traces/2026-10-21-m2.md`), after an
  independent Opus critic review (FIX-THEN-SHIP: 4 HIGH, 6 MED, all fixed or
  recorded). Click gate (D-023): s fixed at 2.25, one threshold 2.5, clean ≤ 1.91,
  faults ≥ 3.5. Memory: 18.71 MiB per instance at 192 kHz (lines 17.61, tape
  history 1.10).

### M3 — Rack admission (the 1.0 bar)
- FOUNDATIONS FX-operator ABI (vendored, sha256-pinned; brief filed first);
  I/O gain standard (`intake/module-io-gain.md`, tests 1–7); macros (≤ 8,
  preset-labelled); per-corner patches and FX-slot presets; parameter classes;
  tail/latency declarations; undo of every control; init + small factory set;
  the mono-fold stereo report (B408); CPU in Release at default / worst /
  automated / morph-swept, as a ratio to the calibration loop (reported, never
  gated); the admission test rows (contract §2.2).
- Goldens cut per factory preset (human gate), then protected.
- **Gate:** every MUST row of `intake/module-1.0-bar.md` evidenced; an
  independent critic review (sourced separately from the author) with no open
  HIGH findings; the human's listening sign-off (presets + max-feedback stress
  case) recorded here; readiness notice filed in horde's `integrations/echo/`.
  **Due 2026-12-11.**

### M4 — Visualizers and screens
- **Scope (D-027):** D-011's impulse-response display (a stem echogram in dB on a
  tempo grid, plus the per-generation spectrum strip) and Echo's own screen
  style. The house-style backup UI is M3's, and meets the 1.0 bar's
  face-and-expanded-view MUST row on its own if Echo's screen is not ready.
- **Timing (D-027):** aimed at 1.0, built in parallel with M3. M3 keeps priority:
  nothing in M4 may put a 1.0-bar MUST row at risk before 2026-12-11. If M4 is not
  ready, Echo ships in 1.0 with the house-style UI and M4 follows.
- **Gate (D-027):**
  1. **The drawing is the engine's.** The drawn impulse response equals the
     engine's own offline render bit for bit: one renderer feeds the display,
     the tail oracle and the D-010 mono-fold metric (D-011).
  2. **It says when it is a snapshot.** The label shows exactly when the time
     is modulated, the ceiling engages, or a move is in flight, and at no
     other time.
  3. **It stays off the audio thread.** Rendering never allocates, locks or
     runs there (the no-allocation probe on `process` stays green).
  4. **It keeps up.** The display redraws within 100 ms of a parameter change.
  5. Each of 1–4 is proven on a planted fault first.
  6. **The screen** gets the human's sign-off on Echo's own design in horde's FX
     screens workshop, plus checkable rows: keyboard-reachable, every key
     display-named (ADR-203), legible in light and dark, no audio-thread work.

### Phase N (pre-ship) — Prior-art re-scan + name clearance
- Re-run the prior-art swarm; the legal and IP checks and horde's B433 screen
  for the user-visible name run privately (`local/legal/`, gitignored, D-014).
- **Gate:** dated technical re-scan appended to `docs/prior-art.md`; legal/IP
  findings in `local/legal/` only, surfaced to the human with a go/no-go; name
  cleared before any public release.

## Queue

### T-001 — Phase 0 prior-art swarm
- **Status:** done 2026-10-20 (trace: `traces/2026-10-20-phase0-rulings.md`; PR #1):
  `docs/prior-art.md`; D-004–D-011 RATIFIED (D-005 as amended, D-013), D-012 RATIFIED.
- **Scope:** `docs/prior-art.md`, DECISIONS
- **Acceptance criteria:** the Phase 0 gate above.
- **Out of scope:** any spec or code.

### T-003 — Phase A: spec + lab twin
- **Status:** done 2026-10-20 (PR #4, 8187aef; trace: `traces/2026-10-20-phase-a.md`; D-015–D-017). `spec/SPEC.md` revision 2,
  `spec/params.json` (29 keys), `lab/echo-lab.html` (A/B panels, a blind bench,
  free play with every key, an IR echogram), `tools/lab_smoke.mjs` in `./verify
  fast`. An Opus critic reviewed revision 1 (REWORK, 7 HIGH; all fixed, D-016).
  Spec ratified 2026-10-20 (D-017).
- **Scope:** `spec/`, `lab/`, `tools/`, the `./verify` lab gate, CI's node step.
- **Acceptance criteria:** the Phase A gate above.
- **Out of scope:** any C++; `intake/`; the manifest.

### T-004 — Phase A notices to horde (after the spec is ratified)
- **Status:** open (spec §14).
- **Scope:** horde's `integrations/echo/`: (1) `time.tempoMode`'s crossfade and
  follow modes amend horde's "glide through smoothers" tempo rule for the
  patches that pick them; (2) is a switch declaration that depends on another
  key's value (`time.div` by `time.change`) admissible under A11?; (3) A11's 1 kHz
  probe reads jumps between integer-ms times and 120-bpm divisions as clean.
- **Acceptance criteria:** filed by a PR in horde's tree touching only that path,
  with the human's go-ahead; the outbound table updated.
- **Out of scope:** the spin-up notice (T-002).

### T-006 — M1: the C++ core and its invariant suite
- **Status:** done 2026-10-21 (PR #7, 5c3e822; trace: `traces/2026-10-20-m1-core.md`).
- **Scope:** `core/`, `tools/check_manifest.py`, `./verify`'s core gate, CI's C++
  note; SPEC §10 amended by D-018.
- **Acceptance criteria:** the M1 gate above.
- **Out of scope:** M2's laws and modulation; M3's ABI swap, meters, presets, CPU.

### T-005 — Lab: rest waits for the wet (D-018)
- **Status:** open.
- **Scope:** `lab/echo-lab.html`, `tools/lab_smoke.mjs`.
- **Acceptance criteria:** the lab's tail gate counts the wet (after the loop and
  output filters) as well as the line writes, as SPEC §10 now says; a lab smoke
  check that the gate never cuts a sounding tail, proven on the literal-text plant.
- **Out of scope:** any other lab change.

### T-007 — M1 critic leftovers (recorded, not blocking)
- **Status:** open.
- **Scope:** `core/`, `tools/check_manifest.py`.
- **Items:** a check that cutoff glides take 10 ms (an instant glide survives the
  suite); `check_manifest.py` to compare `rate` and `smoothing` too (the C++ table
  has neither yet); the ABI shim's 4-pointer `process` vs the shims' in-place L/R
  (a call-site change, not a rename, at M3); why GCC's pragma-less probe render
  stays bit-identical despite 153 fused ops in `process` (D-022).
- **Acceptance criteria:** each item lands with a planted control, or is ruled out
  in DECISIONS.

### T-009 — M2: moving time and modulation
- **Status:** done 2026-10-21 (PR #10, 4c68da4; trace: `traces/2026-10-21-m2.md`).
- **Scope:** `core/include/echo/{engine,plants}.h`, `core/tests/core_tests.cpp`;
  SPEC §7 and §10 amended by D-025 and D-026.
- **Acceptance criteria:** the M2 gate above.
- **Out of scope:** M3's ABI swap, meters, presets, CPU bench; T-008.

### T-008 — D-009's other two click readings (M1 residual, M3 AR-whitened)
- **Status:** open.
- **Scope:** `core/tests/core_tests.cpp`.
- **Acceptance criteria:** both readings implemented as reports with planted
  controls; whether either gates is a human ruling (D-023 gates on M2 only). Also
  report horde's B265 reading beside them (contract §12 A11; critic M2-L5).
- **Out of scope:** engine changes.

### T-002 — Spin-up filings
- **Status:** open (drafts in `docs/filings/`)
- **Scope:** horde's `integrations/echo/notice-spinup.md`; autonomous's
  `integrations/echo/brief.md` (ecosystem-track registration).
- **Acceptance criteria:** each filed by a PR in the provider's repo touching
  only that path, with the human's go-ahead; the outbound table in
  `integrations/README.md` updated.
- **Out of scope:** the FOUNDATIONS brief (M3, when the ABI is first touched).

## Open questions (blocking — ask the human)

- **Q-A3 — Per-module CPU budget** for 1.0 is the human's to set (1.0 bar row 51);
  the appendix's ~2 % default / 4 % worst of a min-spec core is the working target.

## Closed since

- **Q-M4 — M4's gate:** set at M2 close, 2026-10-21, by poll (D-027; ROADMAP M4).

## Closed in Phase A

- **Q-A1** — both the tape and exponential laws ship, as `time.law`; **Q-A2** — a
  key, `time.tempoMode`; **Q-A5** — jump is a 3 ms crossfade (amends D-007);
  **load** — the ringing repeats end, spill-over is horde's. RULED by poll
  2026-10-20 (D-016; spec §3.6–3.8, §10).
- **S-1** — the generation ratio is g within 10⁻³ (generation 2 exactly
  g·(1+R)/2); defaults tape + glide; **the spec ratified**. RULED by poll
  2026-10-20 (D-017).
- **Q-A4** — mid/side S-only content cancels in mono by construction, not a B408
  defect; unintended cancellation = the output's fold losing more than the
  input's. RATIFIED by poll 2026-10-20 (D-015; spec §4.1).

## Deferred / demoted

- **Extras** — freeze / infinite hold, reverse, ducking, smear (diffusion is
  Scape's), a swarm-modulated line from `intake/legacy/swarm-time-lab.html`.
  Not in the core scope (survey Q3); revisit after M3, each by poll.
- **Standalone plugin** — no (survey Q1); a later, logged decision.

## Graduation criteria

Echo graduates from interactive design to autonomous queue work when the
remaining open questions are infrastructure, not listening. Still in the
judgment column: Q-A1, Q-A2, Q-A5, the factory presets, the screen style.
