# t045-row-divisions — T-045: the keyed Time row, Type's display names, `time.div` on RC-7.4, §12.7 switch objects (D-050 A5)

- **Queue item:** T-045 (criteria rewritten in ROADMAP from D-050 A5).
- **Why:** horde answered `echo-003` and `echo-004` on 2026-11-15 (seq 3, ADR-399): one knob, two DAW lanes; `time.div` onto its standard table
  (SPEC-ENGINE RC-7.4); Echo's switch declarations in prose read Undeclared under §12.7 (ADR-309); the bank imports as it is. The human ruled
  D-050 A5 in four polls on 2026-11-24 (rulings 1 - 12).
- **Evidence consulted:** horde's `response-fixed-row-modes.md` at `6d0d6a15`; `specs/SPEC-ENGINE.md:764-789` (RC-7.4); the contract's §12.7
  and §12.9; D-050, A1 - A4; SPEC §3.1, §9, §16.2, §16.4; `params.h`, `state.h`, `presets.h`, `engine.h:972-983`; `tools/check_manifest.py`,
  `tools/manifest_lock.py`; the lab and `tools/lab_smoke.mjs`; FOUNDATIONS' `response-002` (transport delivered with each call).
- **Before the build (lead):**
  - `intake/` re-pinned to horde `6d0d6a15` (ruling 6): `rack-slot-contract.md` and `module-1.0-bar.md` byte for byte; `tempo-divisions.md`
    is now RC-7.4's text, replacing the 2026-10-19 `DIVS` list. The 1.0 bar's change since the last pin is horde's own budget rows (ADR-275).
  - `echo-005` filed (ruling 7, the human approved the text): horde PR #1521, `integrations/echo/brief-meter-switch-form.md`; our copy in
    `docs/filings/hypersaw-brief-meter-switch-form.md`. It accepts two lanes, RC-7.4 and the labels, and asks for the meter beside the tempo
    and a §12.7 form for a switch another key chooses. Respond by 2026-12-01.
- **What changed (implementer, two rounds, f46fa30 and cda8a11):**
  - `time.div` is RC-7.4: 22 rows, shortest first, RC-7.4's names, bar rows four quarters each (4/4 until a meter is delivered, SPEC §3.1).
    No aliases (ruling 12). Default still `1/4` (index 9). The 12 s fold (`engine.h:980`, a `while`) covers 4 bars at 20 bpm.
  - **The state parser reads a value as the rest of its line** (ruling 12): RC-7.4 names contain spaces. Stray spaces, tabs, CR and blanks are
    refused; save -> load -> save is byte-identical for all 44 labels (critic's probe). SPEC §16.2. This is outside the first brief's file
    list; the alternative failed A18.
  - The row: Time is `keyBy` on `time.mode` (`time.ms` in sec and hz, `time.div` in sync); "provisional" gone. The C++ copy of the row lives
    in `core/tests/fixed_row.h`, included only by the suite and `echo_manifest` (ruling 9), held equal to `params.json`.
  - Type: `display: ["Standard", "Ping-Pong", "Mid/Side"]` on `route.mode`, gated by `check_manifest` and mirrored in the lab (ruling 10).
  - §12.7 objects on nine flip keys; `time.mode` and `time.div` stay in prose, and prose is refused on any other key (critic M1).
  - The lock: `tools/manifest_lock.py write --decision D-050` (the tool takes a `## D-NNN` heading; D-050 A5 authorised this regeneration,
    on D-050 ruling 7). `module_version` `dfaf94fb0f74491d` -> `6690a4358094fb7d`; the presets file follows it. Bank: `3/16` -> `1/8D`,
    `3/8` -> `1/4D`.
  - Every suite, lab and smoke row that names a division keeps its musical length, now by name through `dv()` (the implementer's remap table,
    re-checked row by row by the critic). `static_assert` pins `dv("1/4") == 9`, `dv("1/8") == 6`, `dv("4 bars") == 21`.
- **Plants added:** `keyedRowSwapped` (A12 part 1), `stateDropsInactiveKey` (A12 part 2, the round trip across sec -> sync -> hz),
  `stateTrimsValue` (the stray-space refusals); in `check_manifest`, 11 switch, 5 display and 14 row faults; one lock drift (the map's sync
  entry). Suite: 113 checks, 159 core plants. Nothing deleted; the alias assertions became refusal assertions.
- **Readings:** factory presets bit-identical to `077de66` at 48 and 44.1 kHz (implementer's tool, a 120 -> 97 bpm step) and again by the
  critic (120, 93, 37, 200 bpm and a 90 -> 150 ramp). The worst-case state is now 621 B (was 618 B: the longest division name is now
  `4 bars T`), against D-051's 4,096 B.
- **Critic (opus), one round, FIX-THEN-SHIP, all taken:** M1 prose accepted on any key; M2 `display` ungated; M3 A12's round trip without a
  plant; M4 the row table in a public header (ruled 9); L1 - L3, L6 fixed. Recorded, not built: L4, horde's own lane test ("a lane on the
  second id moves `time.div` in every mode") has no Echo counterpart, since the lanes are horde's. L5, L7 are stated above.
- **Corrections:** the lead started one stray `./verify fast` in the background by mistake, could not stop it (the classifier refused
  `pkill`), and waited for it to end before the implementer built. The implementer's commits carry its own model's trailer.
- **Verify:** `./verify fast`; see the PR body. GCC, ASan and UBSan are CI's.
- **Open:** `echo-005` Ask 1 (the meter) and Ask 2 (the switch form), due 2026-12-01. Until then bar rows are 4/4 and two keys are Undeclared
  under A11. T-027's readiness notice carries `display`'s shape and the figure.
