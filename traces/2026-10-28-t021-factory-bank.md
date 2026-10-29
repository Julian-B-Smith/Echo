# t021-factory-bank — the factory bank: the init plus seven presets, and A12/A19 over the bank

- **Queue item:** T-021 (M3 batch 4). D-028 (init + 7), D-042 (C++ is the one preset source), D-039 (A19).
- **Why:** the bank held only the init. The seven are the lead's voicing (slap, dotted ping-pong, dub loop, chorus
  line, wide M/S, tape throw, runaway), each from the init's values with only the listed keys changed, all modify,
  all lin, every slot's home chosen so its offset is exactly 0. The human picks the final set by listening (T-023).
  Second pass after the T-021 critic (FIX-THEN-SHIP): the checks now prove the loaded state is the preset's, every
  new detector has a planted control, and runaway / dub-loop / wide-ms were revoiced.
- **What changed:**
  - `core/include/echo/presets.h`: `step()` (a stepped key's index by label; -1 so `validate()` refuses a typo),
    `voiced()`, `factory()` returning the init plus seven; `jsonValue` is bounds-safe (outside the label range it
    writes the number, which `readBank` refuses; before, a -1 index was UB); three product-side plants
    (`presetPatchOfDropsKey` in `patchOf`, `factoryHomeOffset` and `factoryLabelTypo` in `factory()`).
  - `core/include/echo/plants.h`: those three plus `presetStateOversized` (TEST-SIDE). All constexpr false in
    production.
  - `core/tests/core_tests.cpp`: `presetFactoryBank` (A12: eight presets, ids and order, `validate()`, modify law,
    1 - 8 slots, every binding's offset at its home exactly 0, the load accepted, and the engine's SAVED state parsed
    back with `state::parse` and bit-equal to the preset's values on every patch key); `presetFactoryStateSize`
    (A19: each saved text at most `kMaxBytes`, REPORT of every size); `presetFactoryStereo` (B408 for the bank,
    REPORT only: line times, colouring flag, worst 1/3-octave fold on a centred source, through `fold::take`).
    Plant table and cross-check list; the A12 row lists A12's controls, the A19 row A19's (the one check had put
    `presetStateOversized` under A12).
  - GENERATED, not hand-edited: `presets/echo-factory.json` (`echo_presets --json`), `docs/admission.md`
    (`echo_tests --admission-out`).
- **The size bound is bounded, not fixed-width.** The seven's saved states are 460 - 465 B (they vary with the value
  text); `stateSize`'s 616 B worst case bounds any preset, and the budget is 2048.
- **Planted controls and what each fails on** (`echo_plants`, direct run):
  - `presetPatchOfDropsKey` -> "slap: the saved state's fb.amount is 35, the preset's is 8"
  - `factoryHomeOffset` -> "dub-loop, slot `Wobble`: the offset at its home is 2.399999999846969e-07, not 0"
  - `factoryLabelTypo` -> "pingpong-dotted was refused: internal value of time.div out of range"
  - `presetStateOversized` -> "runaway: the saved state is 2049 bytes (budget 2048)"
  The typo path was run under UBSan + bounds (hardened libc++; ASan does not start on this host): `validate` refuses,
  `writeBank` writes the number, `readBank` refuses, no UB.
- **Revoicings, measured (48 kHz, probe copied from the critic's into the scratchpad, at the homes):**
  - `wide-ms`: voicing kept; description now says a mono source stays centred (S = 0, Width moves nothing there, by
    D-044's design).
  - `dub-loop`: fb 92 -> 96, loop hp 250 -> 120, Feedback slot [-24, +8] home 0.75 -> [-28, +4] home 0.875.
    Per-repeat ratio (peak of successive repeats, steady): 150 Hz 0.746 (was 0.472), 700 Hz 0.882 (was 0.808). The
    target (>= 0.9 at both, < 1) is NOT met; both < 1 (a finite tail). Not tuned further, per the brief.
  - `runaway`: loop hp 300 -> 120, lp 3500 -> 6000, then fb 110 -> 115 and the Feedback slot [-30, +10] home 0.75 ->
    [-35, +5] home 0.875 (the brief's second step). A 0.9 220 Hz 50 ms burst, rms over 1 s windows: at fb 110 the
    level FALLS for ~7 s (0.096 at 1 s, 0.059 at 5 s) then climbs; at fb 115 it rises from 1 s on:
    1 s 0.117, 2 s 0.121, 5 s 0.134, 10 s 0.147, 20 s 0.168, 30 s 0.188, 45 s 0.214, 60 s 0.280, 75 s 0.359,
    120 s 0.365, 299 s 0.373; max 0.624, no non-finite. "Rising from 1 s on" is met; "within 10 % of the plateau by
    20 s" is NOT (0.168 vs 0.373; the plateau is reached at ~75 s). Stopped there, per the brief.
- **B408 for the bank (REPORT, centred source):** slap tL 0.1100 / tR 0.1166 s, colouring YES, worst 1/3 oct
  -18.91 dB at 77 Hz; runaway 0.2500 / 0.2200 s, colouring YES, -5.80 dB at 50 Hz; chorus-line -13.11 dB at 132 Hz;
  tape-throw -12.66 dB at 1943 Hz; pingpong-dotted -3.09 dB; init, dub-loop, wide-ms 0.00 dB. Two presets sit in
  the colouring zone by voicing (their time offsets are under 40 ms).
- **Evidence consulted:** CLAUDE.md §Domain; `core/include/echo/{presets,params,state,plants}.h`; core_tests.cpp
  `stateSize`, `presetA12`, `presetA13`, `presetValidate`, the stereo-report check, the check and plant tables;
  `core/tests/fold.h`; `./verify`'s presets and admission gates; the critic's probes (read, not edited).
- **Alternatives rejected:** a plant inside `state.h`'s save (protected format); one check serving A12 and A19 (the
  admission row would list the other row's controls), so it is two.
- **Critic (Opus): FIX-THEN-SHIP.** M1, M2, M5, L1 – L3, L7 fixed; L4 read into ROADMAP T-021; M3 (wide-ms on a
  centred source), M4 (runaway), L5 (dub-loop) revoiced or restated above; L6 (chorus-line's +3.2 dB coherent peak)
  noted for the listen. D-047.
- **The lead's ruling on the two unmet aims (D-047):** they were voicing aims, not gates. Both presets keep the
  measured voicing and their descriptions now say what they do: dub-loop "with long repeats; its Feedback macro
  takes it to unity", runaway "climbs slowly to a steady roar". The human's listen (T-023) decides the set.
- **Verify:** `./verify fast` exit 0 on the final tree (base `dc79341`, before commit); see the commit.
