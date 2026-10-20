# ROADMAP — Echo

Single source of truth. Only the lead session (or the human) edits this file.
State lives here; conversations are ephemeral. The *why* behind ratified choices
lives in `DECISIONS.md`; horde's shipping bar is `intake/module-1.0-bar.md`.

## Status

- **Phase:** Phase 0 CLOSED 2026-10-20 (D-013). **Phase A IN PROGRESS** (T-003):
  `spec/` revision 2, the lab, the lab smoke in `./verify fast`; Q-A1, Q-A2, Q-A4,
  Q-A5, the offset model and load ruled (D-015, D-016); S-1, the two new keys'
  defaults and the spec's ratification await the human.
- **Oracle:** `./verify fast` = kit integrity, leak gate, private-name gate,
  structure, manifest, intake pin, legal gate, **lab smoke** (`tools/lab_smoke.mjs`,
  every detector with a planted must-fail control, ~7 s). The C++ invariant suite arrives at
  M1. `full` = `fast` for now.
- **Hard date:** horde's module cutoff **2026-12-11**. Every MUST row of the 1.0
  bar met by then, or Echo ships after 1.0.
- **Last human ratification:** 2026-10-20 — Phase A's listening polls (D-016):
  Q-A1 (two laws, a key), Q-A2 (a key), Q-A5 (3 ms crossfade), load ends the tail.

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

### M2 — Modulation modes and moving time
- Repitch (per Q-A1), crossfade jump, and wobble/chorus on the line; what a
  moving time does under automation and morph; tempo changes (Q-A2).
- **Gate:** repitch accuracy in cents and modulation depth tested; the click
  metric (with a planted-click control) passes under a 1 Hz full-range time
  sweep in every mode.

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
- The impulse response drawn as the repeats will sound; Echo's own screen style
  plus the backup UI in horde's house style (horde's FX screens workshop). The
  1.0 bar's "face and expanded view" MUST row (B432/ADR-203) is met in M3 with
  the house-style UI if Echo's own screen is not ready.
- **Gate:** defined at Phase A close, with the human.

### Phase N (pre-ship) — Prior-art & IP re-scan + name clearance
- Re-run the swarm; a patent/IP landscape pass; horde's B433 knockout screen
  for the user-visible name.
- **Gate:** dated re-scan appended to `docs/prior-art.md`; conflicts surfaced to
  the human with a go/no-go; name cleared before any public release.

## Queue

### T-001 — Phase 0 prior-art swarm
- **Status:** done 2026-10-20 (trace: `traces/2026-10-20-phase0-rulings.md`; PR #1):
  `docs/prior-art.md`; D-004–D-011 RATIFIED (D-005 as amended, D-013), D-012 RATIFIED.
- **Scope:** `docs/prior-art.md`, DECISIONS
- **Acceptance criteria:** the Phase 0 gate above.
- **Out of scope:** any spec or code.

### T-003 — Phase A: spec + lab twin
- **Status:** in progress 2026-10-20 (D-015, D-016). `spec/SPEC.md` revision 2,
  `spec/params.json` (29 keys), `lab/echo-lab.html` (A/B panels, a blind bench,
  free play with every key, an IR echogram), `tools/lab_smoke.mjs` in `./verify
  fast`. An Opus critic reviewed revision 1 (REWORK, 7 HIGH; all fixed, D-016).
  Awaiting: S-1, the new keys' defaults, the spec ratification.
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

### T-002 — Spin-up filings
- **Status:** open (drafts in `docs/filings/`)
- **Scope:** horde's `integrations/echo/notice-spinup.md`; autonomous's
  `integrations/echo/brief.md` (ecosystem-track registration).
- **Acceptance criteria:** each filed by a PR in the provider's repo touching
  only that path, with the human's go-ahead; the outbound table in
  `integrations/README.md` updated.
- **Out of scope:** the FOUNDATIONS brief (M3, when the ABI is first touched).

## Open questions (blocking — ask the human)

- **S-1 — The generation ratio against the DC blocker.** D-000 Q4 says
  "= feedback exactly"; D-005's DC blocker makes it g within 10⁻³ (spec §5.3).
  Ruled with the spec, with the defaults of `time.law` and `time.tempoMode`.
- **Q-A3 — Per-module CPU budget** for 1.0 is the human's to set (1.0 bar row 51);
  the appendix's ~2 % default / 4 % worst of a min-spec core is the working target.

## Closed in Phase A

- **Q-A1** — both the tape and exponential laws ship, as `time.law`; **Q-A2** — a
  key, `time.tempoMode`; **Q-A5** — jump is a 3 ms crossfade (amends D-007);
  **load** — the ringing repeats end, spill-over is horde's. RULED by poll
  2026-10-20 (D-016; spec §3.6–3.8, §10).
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
