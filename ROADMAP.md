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

### T-030 — M4: the offline IR renderer (B4-1)
- **Status:** open (approved 2026-10-21, D-030; in progress on its own branch).
- **Scope:** new `core/include/echo/ir_render.h` (the one renderer, D-029 as amended by D-030), new `core/tests/ir_tests.cpp` (built as
  `echo_ir_tests` and `echo_ir_plants`), `core/CMakeLists.txt`, an M4 section appended to
  `plants.h`, and the `./verify` core gate running both new binaries (a human gate).
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
- **Status:** open (approved 2026-10-21, D-030).
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
- **Out of scope:** drawing; `engine.h`.

### T-032 — M4: the display model (B4-2)
- **Status:** open (approved 2026-10-21, D-030).
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
