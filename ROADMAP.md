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
- **Plan: APPROVED by poll 2026-10-21 (D-028)** (T-010 – T-027 below; each item still asks its
  own human gates). Written by an M3 planning session from the 1.0 bar, the
  rack-slot contract (§1, §2.2, §4–§8, §11–§12), the I/O gain standard, SPEC-MODULE-MACROS,
  this repo's spec, decisions, traces and `core/`, and a read of FOUNDATIONS, Scape and horde
  as they stand on 2026-10-21.
- **Found while planning (each changes the plan):**
  1. **The intake pin is stale.** horde amended the rack-slot contract twice on 2026-10-20
     (ADR-257 and its A1; horde's copy now hashes `09f41cc2…`, ours `945108a7…`). The new
     §12.5 adds RS-5.8 (up to four optional *fixed controls*, which A12's round trip then
     covers), RS-4.11 (an optional per-block note context) and admission row A22. The 1.0
     bar and the I/O gain standard are unchanged. A re-pin is a human gate (T-010).
  2. **FOUNDATIONS' `fx_operator.h` is still an unfrozen draft** (slices 1–3, DECISIONS
     #127/#129/#131; nothing after #136). It declares `LatencyDecl`, `TailDecl`, `LoadDecl`,
     `OperatorProperties`, `validate()` and the lifecycle and parameter concepts. It has **no**
     `process()`, no `loadState()` arguments, no channels and no meters. So the "swap" is
     partial: Echo conforms to what exists, and the local shim stays, visibly degraded, for
     `process()` and the state entry (T-013). `TailDecl::samples` is `uint32_t`
     (`kInfiniteTail` = 2³² − 1), but Echo's tail is int64 and can pass 2³² samples near unity
     at 192 kHz, so the mapping must saturate (T-013, and a question in the brief, T-012).
     FOUNDATIONS has no `integrations/echo/`, and Echo is not in its `correspondents.json`.
  3. **horde has no hosted-module admission test.** It is still "a specification, not code"
     (horde's module-cutoff audit, 2026-10-17). Modules self-admit row by row (Bulwark, Scape),
     and horde judges its own rows at hosting. Echo does the same (T-018).
  4. **horde's `integrations/echo/` does not exist yet.** horde's B526 waits for Echo's
     spin-up notice (T-002), so that notice goes first and the readiness notice (T-027) last.
  5. **The CPU budget is already approved.** The bar's appendix says "Budget approved"
     (2026-10-04): ≤ 2 % of a min-spec core at defaults and ≤ 4 % at worst per FX module, and a 4 % ceiling for
     the automated and morphing rows. Q-A3 can close on that reading (poll). horde's frozen
     protocol is `docs/port/cpu-ledger.md` §1 (a 1e8 multiply-add calibration, best of 5
     interleaved, 44.1 kHz at 128 samples, × 1.5 to min-spec, labelled an assumption). The
     "Echo" row in horde's B262 bench (0.337 / 0.405 %) is the legacy rack's built-in echo,
     not this module.
  6. **Eight of the 21 MUST rows already have most of their evidence** (rows 1, 2, 7–11, 15 below). The
     work is in plugging in (the ABI, meters, state, presets, macros), the reports (stereo,
     cost) and the sign-offs.
- **Evidence map: the 21 MUST rows** (bar line → what exists today → what closes it):

  | bar row | today | closes it |
  |---|---|---|
  | 1 scope list, frozen | SPEC §1 | T-016 (freeze, at horde's feature freeze) |
  | 2 manifest + lockfile diff gate | `spec/params.json`, `check_manifest.py` | T-016 (key lock gate) |
  | 3 rack-slot contract (admission) | about half of A1–A20 in the suite | T-013, T-015, T-017, T-018 |
  | 4 I/O gain standard, tests 1–7 | law, ramp at 48/96, Device on load, before the loop | T-014 (meters, latches, tests 1–7) |
  | 5 macros, round trip | SPEC §12 only | T-017 |
  | 6 morph + presets (corner flip, save/load) | Device keys skipped by `loadState` | T-015, T-017 |
  | 7 deterministic | interleaved = solo; reset = fresh | T-018 (A2 at a stress preset) |
  | 8 SR + block independent | 4 rates; blocks 1, 7, 64, 333, 4096, random | T-018 (add n = 256; state the tolerances) |
  | 9 real-time safe | allocation probe; FTZ; NaN/Inf | T-018 (A10 scripted session; locks and syscalls stated) |
  | 10 fidelity suite | 52 checks, 75 plants | T-018 (the thresholds, stated in one table) |
  | 11 silence, exact-zero tails | silence and tail checks | — (met; cited in T-018) |
  | 12 mono-safe (B408 report) | M/S folds to M; Q-A4 | T-019 |
  | 13 CPU cost, default / worst | informal ns figures (M1) | T-020 |
  | 14 automated + morphing cost | none | T-020 |
  | 15 memory figure | 18.71 MiB at 192 kHz (M2) | T-020 (restated after M3's additions) |
  | 16 face + expanded view | display names and laws in `params.json` | T-022 (Echo specifies, horde draws; D-028) |
  | 17 undo of every control | none | T-015 |
  | 18 factory presets | none | T-021 |
  | 19 critic review, no open HIGH | M1, M2 critics | T-026 |
  | 20 listening sign-off | Phase A lab listening only | T-023 |
  | 21 name cleared (B433 knockout) | D-012 placeholder | T-025 (private, `local/legal/`) |

  SHOULD rows: lab parity is met by the ruling that demoted the lab (survey Q5) once goldens
  are frozen (T-024). A manual page is not planned for 1.0 unless the human asks.
- **Batches** (one branch and PR each, the human merges; one implementer at a time on
  `engine.h`; M4's core work lands in its own files or after B2, D-027):
  - **B1, the gates (by 2026-10-24):** T-010, T-011, T-012, with T-002's filings under the
    same go-ahead.
  - **B2, plugging in (2026-10-26 → 11-06):** T-013 → T-014 → T-015 in sequence (all
    `engine.h`); T-016 beside them (`./verify`, tools).
  - **B3, presets and reports (11-09 → 11-20):** T-017, T-018, T-019, T-020 (tools and tests,
    disjoint files); the T-021 bank drafted.
  - **B4, listening (11-23 → 11-27):** T-021 finished, T-022, T-023.
  - **B5, close (11-30 → 12-04):** T-024, T-025, T-026, T-027. A week of slack before the
    2026-12-11 cutoff.
- **The `core/` files each batch touches** (so M4's offline renderer, D-011/D-027, lands in
  its own files or after the batch that changes the same ones; asked by the lead,
  2026-10-21):

  | batch | changes | adds |
  |---|---|---|
  | B1 | none | `core/third_party/foundations/` (T-012: the vendored header and its `MANIFEST`) |
  | B2 | `core/include/echo/engine.h`, `plants.h`, `params.h` (class mapping, T-013), `core/tests/core_tests.cpp`, `core/tests/alloc_probe.h`, `core/CMakeLists.txt` (the vendored include path) | `core/include/echo/state.h` (T-015) |
  | B3 | `core/tests/core_tests.cpp`, `plants.h`, `core/CMakeLists.txt` (new tool targets); `engine.h` **only** for a fault a check finds | `core/tools/bench.cpp` (T-020), `core/tools/stereo_report.cpp` (T-019; `ir_render.h` only under D-030's fallback), a preset validator and resolver (T-017) |
  | B4 | `core/CMakeLists.txt` | `core/tools/render_presets.cpp` (T-023) |
  | B5 | `core/CMakeLists.txt`, `./verify full` | `core/goldens/` (T-024) |

  `engine.h`'s public surface settles at the end of B2 (`properties()`, the meters,
  `saveState` / `stageState` / `adoptStaged`). After that, M4 can build on the public API in a
  file of its own (for example `core/include/echo/ir_render.h`) with no collision, except
  `core/CMakeLists.txt` and `core_tests.cpp`, which every batch appends to.
- **M3 vs M4's one-renderer gate: RULED (D-029, amended by D-030).** M4 builds the one
  offline IR renderer, `core/include/echo/ir_render.h`, in its batch B4-1. It must be on main
  by 2026-11-06 and meet T-019's renderer criteria. T-019 then measures through it. **M3
  never waits:** if the file is not on main by 2026-11-09, T-019 builds it itself, as D-029
  said. Either way there is one file and one renderer from M3 on (M4's gate 1, D-027).
  (D-030 is on M4's branch and lands with M4's plan PR.)
- **Never blocked on a provider:** if FOUNDATIONS or horde has not answered, Echo proceeds on
  the shim and the contract as pinned, and says so in the readiness notice (INTEGRATIONS
  rule 2).

### M4 — Visualizers and screens
- **Scope (D-027):** D-011's impulse-response display (a stem echogram in dB on a
  tempo grid, plus the per-generation spectrum strip) and Echo's own screen
  style. The house-style backup UI is M3's, and meets the 1.0 bar's
  face-and-expanded-view MUST row on its own if Echo's screen is not ready.
- **Timing (D-027):** aimed at 1.0, built in parallel with M3. M3 keeps priority:
  nothing in M4 may put a 1.0-bar MUST row at risk before 2026-12-11. If M4 is not
  ready, Echo ships in 1.0 with the house-style UI and M4 follows.
- **Gate:** (D-027)
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
- **Plan: APPROVED by poll 2026-10-21 (D-030)** (T-030 – T-035 below; each item still asks
  its own human gates as it reaches them: the `./verify` edits, the toolchain, the filing to horde). Written by an M4 planning session from D-011, D-027, SPEC §1
  and §15, prior-art §6–7, `core/`, the M3 planning session's draft (its T-010 – T-027,
  uncommitted on `m3-plan`), and a read of horde as it stands on 2026-10-21.
- **Found while planning (each changes the plan):**
  1. **horde's screens are HTML and canvas in a webview** (gui2 today; GUI 3, horde's
     stage S6, is specified but not built). No C++ painter exists. So the IR the screen
     draws must cross from the C++ renderer into a page.
  2. **The FX screens workshop is one horde-owned page** (`docs/design/fx-screens-workshop.html`
     in horde, last edited 2026-10-01), with no per-module slot. A module's glass is
     a 160 × 144 screen with four soft keys and four knobs, drawn by canvas and CSS only,
     deterministic, with must-fail self-checks. Modules get in when horde's lead edits
     them into the page. **Echo is already there, as a horde-authored mock:** a "SONAR
     SCOPE" glass, tagged ON TRIAL, written before Echo was a sibling and fed mock values.
     horde's ADR-259 ruling 5 says Echo auditions one of the remaining styles. The workshop
     has no sign-off procedure: the human picks, and the pick is recorded.
  3. **Sibling precedent:** Sluice's and Shriek's screens live in horde's tree, drawn
     from what the module rendered offline ("rendered, not live"). Scape and Bulwark ship
     no screen code.
  4. **horde's GUI rules bind the display:** the GUI reads only published atomics, never
     working buffers (contract §8, RC-8); no display feed while the editor is closed
     (B432; 0 % CPU); keyboard mode is opt-in with a visible lamp (ADR-259, B449).
  5. **The renderer is fast enough, but not for a whole tail at once.** Measured on the
     dev Mac (Release, 2026-10-21): 95–245 ns per sample, so a 10 s IR takes 40–120 ms
     at 48 kHz and 180–280 ms at 192 kHz. The display must draw the head of the
     response first and extend it, or the 100 ms gate fails at high rates.
  6. **The mono-fold metric is M3's** (its T-019, B3, from 2026-11-09) and D-011 says it
     uses this renderer. D-029 gave the renderer to T-019; D-030 amends that: M4 builds
     `core/include/echo/ir_render.h` first, by 2026-11-06, to T-019's own criteria, and
     T-019 measures through it. If it has not landed by 2026-11-09, T-019 builds it itself
     and does not wait.
  7. **The engine has no ceiling count and no live status.** Gate 2 needs both. Today
     `headsMoving()` is private, and nothing counts ceiling engagements. That is an
     `engine.h` change. It lands after M3's T-014 (meters, published atomics) and reuses
     T-014's publishing; one implementer at a time on `engine.h`.
  8. **The display names exist already.** All 29 keys have a display name, unit and law in
     `params.h` and `params.json`, so the ADR-203 row is a check, not new work.
- **Where things live:**
  - **Echo's repo:** the renderer and its service (C++, `core/include/echo/ir_render.h`, `ir_service.h`, `ir_view.h`), the
    display model (C++), Echo's screen as the source of truth (`screen/`), and every
    oracle.
  - **horde's repo:** only a filing in its `integrations/echo/` slot, asking horde's lead
    to edit Echo's glass into the workshop, with the human's go-ahead.
  - **In the product,** the renderer runs natively in horde's process, on a worker
    thread. The page receives finished frames.
  - **In the workshop and the screen page,** there is no native host, so the same C++ is
    compiled to WebAssembly and run in a Web Worker (if the poll rules so).
- **Batches** (one branch and PR each; the human merges; M3 keeps priority):
  - **B4-1, the renderer (to 2026-11-06):** T-030. New files only, plus an M4 section
    appended to `plants.h`, and the one-time `./verify` edit (approved by D-030).
  - **B4-2, the service and the display model (to 2026-11-20):** T-031, T-032. New files
    only.
  - **B4-3, the engine hooks (after M3's T-014 lands):** T-033. Small; in sequence with
    M3's implementer on `engine.h`.
  - **B4-4, the screen (to 2026-12-04):** T-034. `screen/`, the WebAssembly build, and its
    smoke.
  - **B4-5, the workshop and close:** T-035. The filing to horde, the human's sign-off,
    and an independent critic review. If it is not done by 2026-12-11, Echo ships with
    the house-style UI (D-027).

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

**The M3 queue, T-010 – T-027 (approved by poll 2026-10-21, D-028).**

Every item: its new checks each get a planted fault in `plants.h` that `echo_plants` must
catch (charter §Domain); `./verify fast` green; a trace in `traces/`. An edit to `./verify`,
`spec/`, `intake/` or the manifest is a human gate, asked in the batch's own poll. Outward
filings need the human's go-ahead each time.

### T-010 — M3: Re-pin intake to horde's current contract (ADR-257, §12.5)
- **Status:** open (human gate: a re-pin).
- **Scope:** `intake/rack-slot-contract.md`, `intake/MANIFEST`; DECISIONS.
- **Acceptance criteria:** the copy is byte-identical to horde's `docs/proposals/rack-slot-contract.md`
  at a named horde commit; `intake_gate` green on the new hash; a decision records what
  §12.5 means for Echo: RS-4.11 (the note context) **not declared** (Echo has no use for
  notes), A22 not applicable, and RS-5.8 (fixed controls) decided in T-011.
- **Out of scope:** any other intake file (the bar and the I/O gain standard are unchanged).

### T-011 — M3: Spec revision 3: what M3 adds to the spec
- **Status:** open (human gate: `spec/` is protected; ratified by poll).
- **Scope:** `spec/SPEC.md`, `spec/params.json` if fixed controls are declared; DECISIONS.
- **Acceptance criteria:** the spec states, each as a ruled line: (1) the meter taps (where they
  sit; peak per block; the clip latch sets on a sample with |x| > 1.0, i.e. over 0 dBFS, and
  holds until cleared); (2) the state format: text, a version line, then one `key value`
  per line in manifest order, stepped keys and divisions **by name** (SPEC §3.1), floats
  written so they read back exactly, Device keys in a separate instance block, ≤ 2 KiB,
  refused before allocation when larger; (3) the module preset format (SPEC-MODULE-MACROS
  §4's `ModulePreset`: values by name, slots 1–8 with label, bindings `[lo, hi, curve]` and
  `home`), and which keys a macro may bind (proposed: continuous keys only; a stepped key
  through a macro would flip at an arbitrary threshold); (4) fixed controls (RS-5.8): which,
  if any; (5) the `TailDecl` mapping: tails at or past 2³² − 1 samples saturate to
  `kInfiniteTail`; (6) `noteContext` and `morphPosition` not declared. Ratified by poll and
  cited by a decision.
- **Out of scope:** any change to the 29 keys' ranges, defaults or classes.

### T-012 — M3: FOUNDATIONS brief, then the vendored, pinned header
- **Status:** open (outward-facing: the human's go-ahead to file).
- **Scope:** FOUNDATIONS' `integrations/echo/brief.md` (a PR touching only that path);
  `core/third_party/foundations/` (Scape's shape: `fx_operator.h` verbatim from a reviewed
  commit, a `MANIFEST` with `commit:` and sha256); `./verify`'s `vendor_gate` (human gate);
  `integrations/README.md`'s outbound table.
- **Acceptance criteria:** the brief is filed with the protocol's frontmatter (`id`, `status`,
  `ball: FOUNDATIONS`, `seq: 1`, `filed`, `respond-by`), asks to register Echo as a
  correspondent, and asks: (1) the `process()` shape for a stereo module that takes the host
  tempo as data; (2) `loadState`'s arguments, offering Echo's staged by-name text; (3) the
  `uint32_t` tail for lines whose tail passes 2³² samples; (4) whether `engine_manifest.h`
  should be vendored so row A1 can validate the manifest in Echo's own tree. It offers
  Echo's contract tests. The header is vendored with its sha256 pinned; `vendor_gate` fails
  on a planted one-byte edit of the vendored file and passes clean.
- **Out of scope:** waiting for the answer: B2 proceeds on the draft (rule 2).

### T-013 — M3: ABI conformance and the declarations (row A1)
- **Status:** open (needs T-012's header and T-011's ruling on the tail mapping).
- **Scope:** `core/include/echo/engine.h`, `plants.h`, `core/tests/core_tests.cpp`.
- **Acceptance criteria:** `static_assert` that `echo::Echo` models FOUNDATIONS'
  `FxOperatorLifecycle` and `FxOperatorParams`; `properties()` returns an `OperatorProperties`
  (latency 0 with no residual, tail with `may_change`, `min_block` 1, `load` = 20 ms at
  the rate) that passes `validate()` after `prepare` at 44.1, 48, 96 and 192 kHz; the tail
  saturates to `kInfiniteTail` (checked at 99.99 % feedback, 12 s, 192 kHz); every parameter
  maps one to one onto `MorphKind` + `MorphParticipation` (RS-2.2); `process()` is proven safe
  in place (`inL == outL`, `inR == outR`), which settles T-007's signature item; the header's
  comment names what is still the local shim (`process`, the state entry) as DEGRADED, per
  INTEGRATIONS rule 2. Plants: a tail left undeclared; a tail that wraps at 2³²; a parameter
  with no class; a process that reads its input after writing its output.
- **Out of scope:** the state format (T-015); meters (T-014).

### T-014 — M3: Meter taps, clip latches and the I/O gain tests 1–7 (row A6)
- **Status:** open (needs T-011's meter ruling).
- **Scope:** `engine.h`, `plants.h`, `core_tests.cpp`.
- **Acceptance criteria:** two taps, directly after Input Gain and directly after Output
  Gain, each a peak and a clip latch, published as atomics readable from another thread,
  computed without allocation; a latch holds until `clearMeters()`, and a load or a reset
  never clears it (RS-6.13's table). The published-atomics mechanism leaves room for a
  status word (heads moving, modulated) and a ceiling count, which M4's T-033 adds after this
  item lands (D-030); this is not required here. Tests 1–7 of `intake/module-io-gain.md` as checks:
  1 bit-identical at 0 dB; 2 the 20 ms linear ramp at 44.1, 48 **and** 96 kHz; 3 the first
  sample after `prepare` at a saved +6 dB is already +6 dB; 4 a patch load and a corner flip
  (a load at another corner's patch) leave both gains and their ramps alone; 5 +12 dB of
  Input raises the loop into the ceiling (the returning term changes shape) while +12 dB of
  Output scales level only; 6 Echo declares both keys, and a test-side slot that adds a
  second pair reads red (the planted double gain); 7 both latches set on an over and hold.
  Plants: a latch that clears itself; a tap before its gain; a prepare that ramps in; a
  double gain.
- **Out of scope:** horde's mixer page and its feed gating.

### T-015 — M3: State by name, exact restore, and undo (rows A17–A20; bar row 17)
- **Status:** open (needs T-011's format ruling).
- **Scope:** `engine.h` (or a new `core/include/echo/state.h`), `plants.h`, `core_tests.cpp`.
- **Acceptance criteria:** `saveState` writes the T-011 format; `stageState` parses and
  validates it off the audio thread; `adoptStaged` adopts it at a block boundary without
  allocating (RS-6.6), with the D-020 load behaviour. A17: a state saved mid-session and loaded
  into a fresh instance renders bit-identically to the original after its `reset()`. A18:
  save → load → save is byte-identical and the renders after each load are bit-identical.
  A19: every factory preset's state and a worst-case state fit 2 KiB; a 2 KiB + 1 state is
  refused before anything allocates. A20: a state saved during a ringing tail, a ramp in
  flight and a set latch equals one saved at rest. **Undo:** for every one of the 29 keys,
  restoring a snapshot taken before the key moved gives a byte-identical state and a
  bit-identical render (Device keys through their instance block). Aliases are accepted
  on load and written back canonical (SPEC §3.1). Plants: a float written as `%.6g`
  (L0008); one key missing from the state; the meter latch serialised; an oversized state
  accepted; a division stored by index.
- **Out of scope:** horde's history tree and gesture brackets (RS-6.14 is the face's).

### T-016 — M3: Manifest lock and scope freeze (bar rows 1–2)
- **Status:** open (human gate: a `./verify` edit).
- **Scope:** a lock file beside the manifest (or under `core/`, as the human prefers, since
  `spec/` is protected), `./verify`, `tools/check_manifest.py`.
- **Acceptance criteria:** a lock lists every key with its unit, class and stepped labels; a
  gate fails on a renamed, removed or re-classed key and on a new key added without updating the lock, and
  passes clean; the lock's hash is the `module_version` written in every preset (contract
  §6.2). Also closes T-007's item to compare `rate` and `smoothing`. Plants: a renamed key; a
  dropped label.
- **Out of scope:** the date of horde's feature freeze (horde's; the scope list in SPEC §1 is
  frozen on it).

### T-017 — M3: Module presets and macros (rows A12, A13; bar rows 5–6)
- **Status:** open (needs T-011 and T-015).
- **Scope:** `presets/` (the bank file), a validator and reference resolver in `tools/` or
  `core/tools/`, `core_tests.cpp`.
- **Acceptance criteria:** the validator refuses: more than 8 slots, slots out of order, an
  unknown key, **a Device key in a preset**, a binding on a key T-011 rules out, a `log`
  curve with a zero or mixed-sign endpoint, a `home` outside [0, 1]. The reference
  resolver (SPEC-MODULE-MACROS §4–§5) hits `lo` and `hi` exactly at v = 0 and 1 for `lin`,
  `exp` and `log`, and matches the closed form to 1e-12 at v ∈ {0, ¼, ½, ¾, 1} with a
  decreasing range. A12: every factory preset saves and loads with slot order, labels,
  bindings, curves, `home` (and fixed controls, if declared) exact. A13: a preset loaded at each of four corners,
  flipped and round-tripped renders identically before and after. Plants: a load that
  reorders slots; a Device key accepted in a preset; an invalid `log` accepted; an endpoint
  that is not exact.
- **Out of scope:** horde's resolution at run time (RS-5.5: horde resolves Echo's macros).

### T-018 — M3: Admission self-test: rows A1–A22 mapped and the gaps filled
- **Status:** open (after T-013 – T-015).
- **Scope:** `core_tests.cpp`, `plants.h`; a generated `docs/admission.md`.
- **Acceptance criteria:** `docs/admission.md` lists each row A1–A22 with its check, its plant
  and the result, or "horde's" (A7 "off" is the router's: Echo has no bypass key) or "not
  declared" (A21, A22), and `./verify` fails if it is stale. New checks: A2 at a stress
  preset; A3 with n = 256 added; A4 latency truth (on an impulse and a swept sine, the
  measured delay of the dry path equals the declared 0 samples; plant: a dry path one
  sample late); A8 `|L − R|` kept in stereo routing (plant: a mono sum); A10 a
  scripted session (sweeps, macro-style moves, corner loads, staged state adoption, sleep and
  wake) with zero allocations, and a stated note that locks and syscalls are checked by
  review only (no RealtimeSanitizer on this toolchain); A11 a structural check that every flip
  key in `params.json` declares a crossfade with a length, `gate`, a seeded jump or
  deferral (plant: a switch with its crossfade removed), plus the **reported, never
  judged** B265 click reading on horde's two probes at 48 kHz, at the defaults and each
  factory preset, both directions, beside an ideal offline equal-gain crossfade (absorbs
  T-008's B265 item); A14 Echo inside an external one-sample cycle at loop gain 1.2: silence
  in gives silence out (plant: a loop that starts itself). The fidelity thresholds (row 10)
  stated in one table.
- **Out of scope:** horde's own admission harness (unbuilt; horde judges its rows at hosting).

### T-019 — M3: The B408 stereo report (D-010; bar row 12)
- **Status:** open.
- **Scope:** a report tool in `core/tools/`, `docs/stereo-report.md` (generated), checks in
  `core_tests.cpp`. The report measures through the one offline IR renderer,
  `core/include/echo/ir_render.h`. M4 builds it by 2026-11-06 (D-030, amending D-029); if it
  is not on main by 2026-11-09, T-019 builds it here, to the criteria below.
- **Acceptance criteria:** per routing mode, D-010's readings: ρ broadband, per repeat
  generation and per octave band; the ⅓-octave-smoothed worst notch and its frequency; on a
  mono impulse, seeded decorrelated noise, and the S-only impulse (labelled *by
  construction*, Q-A4). Layer-0: ρ_out ≥ ρ_in per generation on the mono and noise inputs in
  every mode; the colouring-zone flag raised exactly when the two times differ by less than
  ~40 ms and are not equal. The renderer's output equals a plain offline `Echo` render of the same impulse, bit for
  bit, on the mono, S-only and seeded-noise inputs too (M4's plant `irSkipsOutputFilter`, or
  this item's own if it builds the file). Plants: D-010's P1–P5 (prior-art §5.3).
- **Out of scope:** a stereo width control (post-1.0).

### T-020 — M3: Cost bench and memory figure (bar rows 13–15)
- **Status:** open.
- **Scope:** `core/tools/bench.cpp`, `docs/cost.md`. Reported, **never** a verify gate
  (RS-8.3).
- **Acceptance criteria:** Release; horde's frozen protocol (`docs/port/cpu-ledger.md` §1 at a
  cited horde commit); rows at 44.1 kHz / 128 (judged) and 48 kHz (reported): default;
  worst (proposed: mid/side, all three filters on, drift modulation at full depth, tape
  repitch moving, 120 % feedback); automated (one continuous key under a 1 Hz full-range
  sine: `time.ms` in repitch, and `fb.amount`); morph-swept (the slot's blended `setParam`
  targets at control rate between two corner patches). Each row as a ratio to the
  calibration loop and as % of an M3 core and of min-spec (× 1.5, labelled an assumption),
  against the approved 2 % / 4 % (Q-A3, closed by D-028). Memory per instance at 44.1–192 kHz, worst case,
  all allocated at prepare, restated after M3's additions.
- **Out of scope:** optimisation, unless a row is over budget (then a new item; ADR-236:
  over budget does not cut a module).

### T-021 — M3: The factory bank: an init plus a small set
- **Status:** open (init + 7, D-028; needs T-017).
- **Scope:** `presets/echo-factory.json`.
- **Acceptance criteria:** an init (the manifest defaults, no macros) and seven presets (D-028),
  chosen to show the range: a slap, a synced dotted ping-pong, a dark dub loop near
  unity, a modulated chorus-like line, a mid/side wide, a tape-repitch throw, a past-unity
  edge patch. Each passes T-017's validator and T-015's size check and carries up to 8
  labelled macros. The human picks the final set by listening (T-023).
- **Out of scope:** horde's presets lab and its library size (B395/B409).

### T-022 — M3: The face and expanded view (bar row 16)
- **Status:** open (Echo specifies, horde's workshop draws, D-028).
- **Scope:** `docs/face.md`: which keys sit on the face and which in the
  expanded view, their grouping and order, a keyboard order; the ADR-203 check that every
  key has a display name and a normalised law that maps [0, 1] onto its range and back
  (plant: a key with no law). horde's FX screens workshop draws the house-style UI from it
  (B336/B531), and the GUI review is horde's.
- **Acceptance criteria:** the face spec exists and is complete for all 29 keys; the law
  check is in `./verify`; the human or horde's workshop accepts the face.
- **Out of scope:** Echo's own screen (M4, D-027).

### T-023 — M3: Listening renders and the human's sign-off (bar row 20)
- **Status:** open (after T-021).
- **Scope:** `core/tools/render_presets.cpp` (WAVs from the C++ engine, never the lab), a
  small listening page in `lab/` or `docs/`, ROADMAP.
- **Acceptance criteria:** renders of every factory preset and of the stress cases (120 %
  feedback; 12 s at 1 ms flips; full-depth drift; a tape throw across the cap) on a fixed
  seeded source set; the human's sign-off recorded here, with any preset they reject
  replaced or dropped.
- **Out of scope:** the lab's own sound (the lab is not the module).

### T-024 — M3: Goldens per factory preset (human gate)
- **Status:** blocked (on T-021 and T-023).
- **Scope:** `core/goldens/` (render hashes), `./verify full`.
- **Acceptance criteria:** one golden per factory preset plus the init and the stress cases,
  cut only after the sign-off, at 48 kHz on the fixed source set; `./verify full` runs
  them on Mac/clang and Linux/GCC (or states why one platform's hashes differ); the
  goldens become a protected path; a planted one-ulp change in the engine turns `full`
  red.
- **Out of scope:** goldens before the sign-off (ADR-142: parity never enshrines a defect).

### T-025 — M3: The name: B433's knockout screen (bar row 21)
- **Status:** open (private).
- **Scope:** `local/legal/` only (D-014); a neutral pointer in tracked files.
- **Acceptance criteria:** the self-run knockout screen of B433 run for the user-visible
  name the human picks (D-012), recorded privately; the human's go/no-go recorded here as
  a neutral line. Phase N's broader clearance stays pre-commercial.
- **Out of scope:** counsel (ADR-236 item 5).

### T-026 — M3: Independent critic review of M3 (bar row 19)
- **Status:** blocked (on T-013 – T-020).
- **Scope:** read-only.
- **Acceptance criteria:** a fresh-context Opus critic with no shared conversation, an
  adversarial brief and planted faults it must catch (ADR-203's reading for 1.0) reviews
  the port and its oracles; no HIGH finding left open. (Before any binary ships outside
  the project, a second review from another model family or a human is still owed.)
- **Out of scope:** fixing in the review itself.

### T-027 — M3: Readiness notice to horde
- **Status:** blocked (on T-002's spin-up notice and every MUST row above).
- **Scope:** horde's `integrations/echo/` (a PR touching only that path; the human's
  go-ahead), `integrations/README.md`.
- **Acceptance criteria:** the notice lists each MUST row with its evidence (the admission
  report, the stereo report, the cost rows, the sign-offs), Echo's known differences (the
  shim's `process()` and state entry; anything FOUNDATIONS has not answered), and T-004's
  three notices folded in; filed by 2026-12-04.
- **Out of scope:** hosting Echo in horde (horde's work).

### T-030 — M4: the offline IR renderer (B4-1)
- **Status:** done 2026-10-21 (PR #15, 1f23fe7; trace `traces/2026-10-21-t030-ir-renderer.md`;
  critic FIX-THEN-SHIP, all fixed).
- **Scope:** new `core/include/echo/ir_render.h` (the one renderer, D-029 as amended by
  D-030), new `core/tests/ir_tests.cpp` (built as `echo_ir_tests` and `echo_ir_plants`),
  `core/CMakeLists.txt`, an M4 section appended to `plants.h`, the `./verify` core gate
  running both new binaries (approved by D-030), and CI's sanitizer step.
- **Amended 2026-10-21 by the lead, after the critic (D-031):** the API is
  `echo::renderIr(IrRequest)` and `IrRenderer::render`, not a five-argument call. The
  response drawn is the one heard right after loading the patch (D-026's restart from the
  seed), the only response that is a function of the patch. The reference is an `Echo`
  that loaded the patch. The plant `irRendersFromLive` cannot be planted (the API takes no
  live instance); `irReusesState` and `irSeedAfterReset` stand for it. The criteria below
  are read with those amendments.
- **Acceptance criteria:**
  1. `echo::renderIr(patch, sr, bpm, level, n)` renders through a private `echo::Echo`
     instance: prepared at the host rate, every patch and Device value landed at rest,
     an impulse at the stated level, a fixed tempo. It returns L/R float64 buffers and
     whether the render reached rest. It is pure: no clock, no global state, and the
     same inputs always give bit-identical output.
  2. The render equals, bit for bit, an independent `Echo` driven by the suite's own
     `run()` with the same impulse. This holds for 12 patches covering every routing,
     every filter position, feedback 0 / 50 / 99 / 120 %, synced and free time, and
     modulation.
  3. The cache key is a hash of every input the render reads: all 29 keys, the rate,
     the tempo, the level and the length. Changing any one key changes the hash.
  4. The tail oracle uses it: from the declared `tail()` on, the render is exact zero.
     The mono-fold metric (M3's T-019) takes its input from it: a mono, an S-only or a
     seeded-noise input renders through the same path as the impulse.
  5. Each criterion is proven on a plant first. The plants are `irHashSkipsKey`,
     `irRendersFromLive` (the clone shares state with a live instance),
     `irLevelIgnored`, `irDropsDevice` (the render ignores the I/O gains) and T-019's own
     `irSkipsOutputFilter`.
- **Out of scope:** threads, the display, `engine.h`.

### T-031 — M4: the render service (B4-2)
- **Status:** in progress (built on branch `m4-service`, B4-2's PR; trace
  `traces/2026-10-21-b4-2-service-view.md`).
- **Amended 2026-10-21 by poll (D-033):** frame 1 covers the first **96 000 samples** (2 s at
  48 kHz, 0.5 s at 192 kHz). The render costs per sample, and once built, a 2 s head at
  192 kHz measured 90–196 ms worst of 20. Every chunk is cancellable, the head's included,
  so a newer request waits at most one 0.1 s chunk.
- **Scope:** new `core/include/echo/ir_service.h`, `core/tests/ir_tests.cpp`, `plants.h`
  (M4 section).
- **Acceptance criteria:**
  1. **Worker.** A UI-thread `request(patch)` returns at once, and one worker thread
     renders. The cache is allocated at start, holds the last 8 renders and is keyed by
     the T-030 hash.
  2. **Head first.** Each response arrives head first: frame 1 covers the first 2 s (or
     the whole response, if that is shorter), and later frames extend it to the display
     window. A new request cancels the render in flight at its next chunk.
  3. **Off the audio thread (gate 3).** While the service renders flat out on another
     thread, a live `Echo` keeps its allocation probe green. Every render records its
     thread, and none is ever the thread calling `process()`. The plant
     `irRenderInline` renders inside `request()`.
  4. **It keeps up (gate 4).** Frame 1 is ready within 100 ms of `request()`. This is
     measured in Release on the dev Mac at 48 and 192 kHz, on the slowest patch, as the
     worst of 20 runs. The plant `irWholeTailFirst` withholds frame 1 until the whole
     tail is rendered, and must fail the check. CI reports the timing but does not gate
     on it, because shared runners make wall-clock gates flaky.
  5. **Fed only while open.** With the editor closed (`pause()`), no renders run.
  6. **No re-rendering of the head (critic T-030 M2).** Head-first frames extend one
     render. `IrRenderer` gains a resumable form (start, then render the next n
     samples) in `ir_render.h`, proven bit-equal to the one-shot render at every split.
- **Out of scope:** drawing; `engine.h`.

### T-032 — M4: the display model (B4-2)
- **Status:** in progress (built on branch `m4-service`, B4-2's PR).
- **Amended 2026-10-21 by the lead (D-032, for the human's review):**
  - Criterion 1: the model draws **columns**. Each column is the render's own sample of
    greatest magnitude in its range. A screen has hundreds of columns and a render millions
    of samples, so a stem per sample cannot be drawn. When a column covers one sample, the
    columns are the render.
  - Criterion 2: **beats and division marks**. Bar lines need the host's meter, which the
    rack-slot contract does not pass; T-034 raises it with the screen.
  - Criterion 3's reference is the engine's **own first repeat**, not an analytic |F|. That
    repeat carries the loop filter once (SPEC §2), and re-deriving |F| in a test would be a
    second model of the engine.
    - Every routing is covered, each channel at its own arrivals (SPEC §4).
    - With every filter off, each generation is flat and the next is exactly
      p·20 log₁₀ g lower (p passes per channel generation; 2 in ping-pong).
    - With the loop filter on, each step equals p passes of the first repeat's shape plus
      g.
    - Mid/side with side content and unequal times has no strip, and is refused.
    - (Critic B4-2 H1, H2; D-033.)
- **Scope:** new `core/include/echo/ir_view.h`, `core/tests/ir_tests.cpp`, `plants.h`
  (M4 section).
- **Acceptance criteria:**
  1. **Stems.** A pure function turns a render into what the screen draws: per channel,
     every non-zero sample as a stem at its sample index, in dB relative to the
     impulse, on a −120 dB floor. The model keeps the render's own values, so a drawn
     stem and the render sample read back equal (gate 1). The plant
     `viewDecimatesPeaks` thins the stems for the screen and must be caught.
  2. **Tempo grid.** Beats and bars are placed from the tempo and `time.div`.
  3. **Spectrum strip.** For each generation n, the magnitude spectrum is taken in a
     window centred on that generation's arrival time. With the loop filter alone in
     circuit and the other filters off, it matches |F|ⁿ·g within 0.1 dB to 0.45 fs,
     where F is the loop filter's analytic response.
  4. **Energy decay curve.** The EDC is the backward integral (Schroeder) of the render.
  5. Each is proven on its plant first.
- **Out of scope:** pixels, styling.

### T-033 — M4: the snapshot label's engine hooks (B4-3)
- **Status:** blocked (until M3's T-014 lands on main; approved 2026-10-21, D-030).
- **Scope:** `core/include/echo/engine.h` (additive only): a ceiling-engagement count, and
  a status word published once per block through T-014's mechanism (heads moving, time
  modulated). Also `ir_render.h`, `ir_tests.cpp` and `plants.h`.
- **Acceptance criteria:**
  1. The label (`snapshot = modulated || ceiling || moving`) is true exactly when the
     live engine's heads glide or crossfade, its modulation depth is non-zero or
     ramping, or the render's ceiling engaged. It is false at every other block of a
     scripted session that holds 20 moves, modulation on and off, and feedback from 50
     to 120 % at the stated level.
  2. Publishing allocates nothing and takes no lock. The `noAlloc` check stays green,
     and the clean suite's 52 checks render bit-identically.
  3. The plants are `statusLate` (published one block late), `ceilingUncounted` and
     `modFlagFromParamOnly` (it misses the 250 ms ramp-down).
- **Out of scope:** any other `engine.h` change.

### T-034 — M4: Echo's screen (B4-4)
- **Status:** open (approved 2026-10-21, D-030).
- **Scope:** new `screen/`. It holds `echo-screen.html` (the 160 × 144 glass on horde's
  chassis contract, plus an expanded view with the stem echogram on the tempo grid,
  the spectrum strip and the EDC), the WebAssembly build of `ir_render.h` and `ir_view.h` (D-030), and
  `tools/screen_smoke.mjs` in `./verify` (a human gate).
- **Acceptance criteria:**
  0. **Two glasses, one model (D-030):** Echo's stem-echogram glass and horde's SONAR SCOPE
     glass, both drawn from `ir_view.h`'s model, for the human to pick in the workshop.
  1. **The drawing is the engine's (gate 1).** The page's drawn stems equal, bit for bit,
     the native renderer's output for the T-030 patches. The WebAssembly render's hash
     equals the native one, or the difference is traced to libm and recorded in
     DECISIONS before anything is pinned.
  2. **The snapshot label** shows exactly as in T-033, driven from the status word.
  3. **It keeps up.** A redraw follows a key change within 100 ms in Chrome and Safari on
     the dev Mac. This is measured, and a planted whole-tail renderer must fail.
  4. **The screen's own rows.**
     - Keyboard: every control can be reached and changed by keyboard in horde's keyboard
       mode.
     - Names: every key shows its `params.json` display name.
     - Contrast: text and stems meet WCAG AA in the light and dark themes.
     - No feed while closed: nothing renders while the page is hidden.
     - Each is checked headlessly with a planted control.
  5. It follows horde's chassis rules: canvas and CSS only, no WebGL, no wheel
     listeners, deterministic.
- **Out of scope:** horde's tree; the house-style backup UI (M3's T-022).

### T-035 — M4: the workshop and close (B4-5)
- **Status:** open (approved 2026-10-21, D-030).
- **Scope:** a filing in horde's `integrations/echo/`, made with the human's go-ahead
  after T-002's spin-up notice, asking horde's lead to edit Echo's glass into the
  workshop. Also ROADMAP, DECISIONS and a trace.
- **Acceptance criteria:**
  1. The human's sign-off on Echo's design in the workshop, recorded here.
  2. An independent Opus critic review with no open HIGH findings.
  3. M4's gate rows 1–6, each evidenced in the trace.
- **Out of scope:** any edit inside horde's tree beyond that one path.

## Open questions (blocking — ask the human)

- (none open; the M3 plan's questions closed 2026-10-21, D-028)

## Closed since

- **Q-M3P — the M3 plan:** approved as written, by poll 2026-10-21 (D-028).
- **Q-M3B — the factory bank:** an init plus 7 (D-028; T-021).
- **Q-M3F — the face:** Echo specifies it, horde's workshop draws it (D-028; T-022).
- **Q-A3 — the CPU budget:** closed on the bar's approved 2 % / 4 % (D-028; T-020).
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
