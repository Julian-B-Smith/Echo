# horde's standard tempo divisions (SPEC-ENGINE RC-7.4)

> **Origin.** Copied by Echo's lead session on 2026-11-24, byte for byte, from horde's `specs/SPEC-ENGINE.md`
> lines 764 – 789 (RC-7.4) at HYPERSAW `6d0d6a15`. It replaces the list horde's lead copied on 2026-10-19 from
> `docs/design/shape-lab-mod.html` (`DIVS`), which RC-7.4 superseded on 2026-10-30 (ADR-328). horde ruled that it binds
> a hosted module (ADR-399 ruling 2; `integrations/echo/response-fixed-row-modes.md` in horde's tree), and Echo's
> human adopted it (Echo D-050 A5). Echo stores a division by name; the toggles stay face gestures there (D-050 ruling 4).

  - **RC-7.4** **The standard tempo divisions.** Every part of the instrument that syncs to tempo (the morph's stepped hybrid, and the LFOs, the arpeggiator and the delays when they are specified) offers one set of divisions, the table below, and no part keeps a list of its own. **The table is fixed:** an index always names the same row, whatever the time signature, and the menu lists the rows in the table's order. The order is by length in 4/4. A note value's length is in whole notes; a bar value's length is in bars, a bar being the time signature's (RC-7). So in other meters a row's length may tie or cross its neighbour's (in 3/4 a dotted 1/2 equals 1 bar), and neither the index nor the order changes. **Base** rows are always offered. **Triplet** rows (⅔ of their base) are offered when the control's triplet toggle is on, and **dotted** rows (3/2 of their base) when its dotted toggle is on. The two toggles belong to each synced control and are saved with the patch (ADR-327). They filter the menu only, so a stored index, and the sound, never change with them.

    | index | name | kind | length |
    |---|---|---|---|
    | 0 | 1/32 | base | 1/32 whole note |
    | 1 | 1/16T | triplet | 1/24 whole note |
    | 2 | 1/32D | dotted | 3/64 whole note |
    | 3 | 1/16 | base | 1/16 whole note |
    | 4 | 1/8T | triplet | 1/12 whole note |
    | 5 | 1/16D | dotted | 3/32 whole note |
    | 6 | 1/8 | base | 1/8 whole note |
    | 7 | 1/4T | triplet | 1/6 whole note |
    | 8 | 1/8D | dotted | 3/16 whole note |
    | 9 | 1/4 | base | 1/4 whole note |
    | 10 | 1/2T | triplet | 1/3 whole note |
    | 11 | 1/4D | dotted | 3/8 whole note |
    | 12 | 1/2 | base | 1/2 whole note |
    | 13 | 1 bar T | triplet | 2/3 bar |
    | 14 | 1/2D | dotted | 3/4 whole note |
    | 15 | 1 bar | base | 1 bar |
    | 16 | 2 bars T | triplet | 4/3 bar |
    | 17 | 1 bar D | dotted | 3/2 bar |
    | 18 | 2 bars | base | 2 bars |
    | 19 | 4 bars T | triplet | 8/3 bar |
    | 20 | 2 bars D | dotted | 3 bars |
    | 21 | 4 bars | base | 4 bars |
