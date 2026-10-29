# SESSION — Echo, closed 2026-10-29

The hot state the next session trusts. Written last, by `/breakdown`. The tree it describes is `main` at `d7374cf`,
plus this close's PR (a README audit, L0017 and L0018, REFLECTIONS.md, this file). The unfinished T-022 work sits on
branch `m3-b4-face` (`ca60f4a`, pushed, no PR).

## Where it stands

- **Phases.** Phase 0, Phase A, M1 and M2 are CLOSED.
- **M3** (rack admission, the 1.0 bar, due **2026-12-11**) is in batch 4.
  - Done: T-010 – T-021 and T-037. That covers horde's ABI, the meters, state, presets and macros, Clear, the
    admission self-test, the stereo report and the cost bench.
  - T-021 landed this session as PR #29 (D-047). The factory bank (the init plus seven) is a *proposal* until the
    human's listen in T-023.
  - T-022 (the face) is in progress. T-023 (listening renders and sign-off) is open; its source set and format were
    ruled by poll 2026-10-28 and are recorded in ROADMAP T-023.
- **M4.** T-030 – T-032 are built. T-036 landed this session as PR #28 (D-046): gate 4 now judges frame 1 on
  process CPU time, with a 1 s wall ceiling off CI. T-033 is unblocked, since T-014 is on main; T-034 and T-035 are
  open.
- **The core.**
  - `echo_tests`: 95 checks guarding 127 planted faults (`echo_plants`), plus the IR suites.
  - Admission: 20 rows PASS, A7 is horde's, and A21/A22 are not declared.
  - CI runs Mac/clang and Linux/GCC plus ASan + UBSan.

## Verify

- `./verify fast` exit 0 on `main` at `d7374cf` (2026-10-29), and on `m3-b4-face` at `ca60f4a`.
- On the T-022 branch it adds the line `fp-contract (params.h): OK — echo::toNorm/fromNorm hold 0 fused ops with the
  pragma, 1 without`.
- This close's PR re-runs verify on its own tree.

## First move next session (the human's answer)

**Rule the fixed row by poll, then finish T-022.**

- The poll is a human gate on spec, manifest and lock. Choices: keep D-035's four (Feedback, Tone, Mod Depth, Mix)
  or the lead's proposal of two (Feedback, Mix).
  - Under two, Tone stays on the face and greys out while the loop filter is off. Mod Depth moves to the macros.
  - Going to two touches SPEC §16.4, `params.json` `fixedControls`, and the lock via
    `tools/manifest_lock.py write --decision D-NNN`. That moves `module_version` and so the presets JSON. horde was
    told "four" at spin-up, so T-027 carries the notice.
- Then T-022 part 2: the critic's outstanding findings.
  - `docs/face.md`: L6 and L8 (unit, seed and Clear gaps).
  - `tools/check_manifest.py`: M2 (the exact face set) and M3 (one plant per message, plus a label plant).
  - L7 (parser edges).
- Then record D-048 with T-022's rulings: the step law, the face composition, the `./verify` probe edit, NaN → the
  key's default, and the fixed row. Re-check with the critic, run verify, open the PR.
- The implementer that did part 1 can be resumed (agent `a024fa0c29e175352`) or replaced by a fresh one with a
  self-contained brief.

## Open threads

- **The fixed-row rethink** is the loose thread most likely to be forgotten (the human's answer; REFLECTIONS
  2026-10-29). T-022's face spec and face check are held on it.
- **T-022 criterion 3:** the human or horde's workshop accepts the face. This stays open after the PR.
- **T-023:**
  - `core/tools/render_presets.cpp`, a listening page in `docs/`, and gitignored WAVs.
  - Seeded synthetic sources rendered in stereo (so wide-ms is audible), plus the stress cases.
  - The human's sign-off decides the bank.
  - Things to listen for:
    - dub-loop's repeat ratio is below its 0.9 aim.
    - runaway reaches its plateau at about 75 s, not 20 s.
    - chorus-line has a +3.2 dB coherent peak.
    - slap and runaway sit in the B408 colouring zone.
- **Replies due 2026-11-06:** FOUNDATIONS (`echo-002`, its PR #133) and autonomous (its PR #61).
- **T-004:** Phase A's three notices to horde are still drafts in `docs/filings/`. T-002 was filed 2026-10-23.
- **Recorded for T-026 (the M3 critic):** T-019's M3/L5; 192 kHz over the cost budget (informational); the morph
  cost.
- **Older, unchanged:**
  - T-005: the lab's rest waits on line writes only.
  - T-007: M1's critic leftovers.
  - T-008: D-009's other two click readings.
  - Phase N: the name screen; legal notes stay in gitignored `local/legal/`.
  - The D-037/38/39/41/43 questions in ROADMAP.
- **Housekeeping:** three old worktrees remain in `.claude/worktrees/`: `mystifying-einstein-f12100` and
  `sweet-newton-4d72a8` (both detached), and `roadmap-viz`. Removing them is the human's call.

## This session's traces

- `traces/2026-10-28-t036-gate4.md` (T-036, PR #28)
- `traces/2026-10-28-t021-factory-bank.md` (T-021, PR #29)
- `traces/2026-10-28-t022-face.md` (T-022 part 1, on `m3-b4-face`, in progress)
