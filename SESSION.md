# SESSION — Echo, closed 2026-10-22

The hot state the next session trusts. Written last, by `/breakdown`; the tree it describes is `main` at
`369edb1` plus this close's PR (README audit, two lessons, REFLECTIONS.md, this file).

## Where it stands

- **Phases:** Phase 0, Phase A, M1 and M2 are CLOSED (D-013, D-017, PR #7, PR #10). M3 (rack admission, the 1.0 bar,
  due **2026-12-11**) is planned and approved (D-028; queue T-010 – T-027, batches B1 – B5, closing by 2026-12-04), with
  no batch started. M4 (the impulse-response display and Echo's own screen) is aimed at 1.0 beside M3, which keeps
  priority (D-027). Its plan is D-030; T-030 (the renderer `core/include/echo/ir_render.h`) is done, and T-031 / T-032
  (the render service and display model) are built (PR #15, PR #16).
- **The core:** header-only C++20 `core/include/echo/engine.h`.
  - `echo_tests`: 52 checks, guarding 75 planted faults (`echo_plants`).
  - The IR suite `echo_ir_tests` / `echo_ir_plants`.
  - CI: green on Mac/clang and Linux/GCC, plus ASan + UBSan.
  - Memory: 18.71 MiB per instance at 192 kHz.
- **Plan map:** `docs/design/echo-plan-map.html` reads ROADMAP live. Keep ROADMAP's queue statuses in its vocabulary
  (done / in progress / open / blocked / dropped) and gates as `- **Gate:**` lines, or its smoke turns `./verify` red.

## Verify

`./verify fast` exit 0 on `main` at `369edb1` (2026-10-22): kit integrity, leak, private-name, legal, structure,
manifest, intake pin, lab smoke, core gate (both core suites, both IR suites, the manifest check, the fp-contract
probe, the fast-math guard), plan-map smoke. This close's PR re-runs it on its own tree.

## First move next session (the human's answer)

**M3 batch 1: T-010, then T-011.** T-010 re-pins `intake/` to horde's current rack-slot contract (a human gate: a
re-pin). T-011 is spec revision 3 (meters, state by name, the preset format, fixed controls, the tail mapping; a
protected-path poll). Everything else in M3 waits on these two. The M3 session is
[local_bc51f771](#local_bc51f771-9e91-4d53-9cca-20bffc09f1be) (its plan is in ROADMAP M3).

## Open threads

- **The horde filings: the loose thread most likely forgotten** (the human's answer). T-002 is Echo's spin-up notice
  to horde plus the autonomous registration brief, so Echo is not yet in the ecosystem tracks. T-004 is Phase A's
  three notices to horde. All are drafts in `docs/filings/`, and every filing is outward-facing (the human's go-ahead
  each time). horde has heard nothing from Echo since spin-up.
- **M4 B4-3 (T-033)** is blocked until M3's T-014 (meter taps) lands on main. B4-4 installs the wasm toolchain (LLVM's
  wasm target, lld, a wasi sysroot), and the human is told what is installed (D-030).
- **T-005:** the lab still rests on line writes alone (the cut D-018 fixed in C++).
- **T-007:** the cutoff-glide check, the manifest's rate/smoothing comparison, D-022's unexplained identical GCC render.
- **T-008:** D-009's residual and AR-whitened click readings, plus B265 beside them (D-023 gates on M2 only).
- **Phase N:** the user-visible name goes through horde's B433 screen before release (D-012); the legal notes stay in
  gitignored `local/legal/` (D-014).
- **Housekeeping:**
  - Child sessions' worktrees are still in `.claude/worktrees/` (`mystifying-einstein-f12100` detached at 3186292,
    `sweet-newton-4d72a8`).
  - The M3 and M4 sessions are idle with their context, and the Phase A and plan-map sessions are done.
  - Their branches are merged; removing the worktrees is the human's call.

## This session's traces

`traces/2026-10-19-spinup.md`, `2026-10-20-phase0-rulings.md`, `2026-10-20-legal-local.md`, `2026-10-20-m1-core.md`,
`2026-10-21-m2.md` (written here); `2026-10-19-prior-art.md`, `2026-10-20-phase-a.md`, `2026-10-20-plan-map.md`,
`2026-10-21-m3-plan.md`, `2026-10-21-t030-ir-renderer.md`, `2026-10-21-b4-2-service-view.md` (written by the sessions
this one started). Decisions D-000 – D-033; lessons L0001 – L0016 (L0015, L0016 added at this close).
