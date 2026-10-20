# horde's tempo division list (the sync vocabulary)

> **Origin.** Copied by the HYPERSAW (horde) lead on 2026-10-19 from horde's `docs/design/shape-lab-mod.html`
> (`DIVS`). Sluice uses the same list and stores divisions by **name** (horde `integrations/sluice/notice-tempo-sync.md`).
> Echo's tempo sync should use this list and store divisions by name too, so a horde preset means the same thing
> in every module. If horde's division ruling changes, horde files a notice to Echo's mailbox.

Slow to fast. Columns: name, accepted alias, beats in quarter notes (the host's bpm counts quarters).

| name | alias | beats |
|---|---|---|
| 2/1 | | 8 |
| 3/2 | 1/1D | 6 |
| 2/1T | | 16/3 |
| 1/1 | | 4 |
| 3/4 | 1/2D | 3 |
| 1/1T | | 8/3 |
| 1/2 | | 2 |
| 3/8 | 1/4D | 1.5 |
| 1/2T | | 4/3 |
| 1/4 | | 1 |
| 3/16 | 1/8D | 0.75 |
| 1/4T | | 2/3 |
| 1/8 | | 0.5 |
| 3/32 | 1/16D | 0.375 |
| 1/8T | | 1/3 |
| 1/16 | | 0.25 |
| 3/64 | 1/32D | 0.1875 |
| 1/16T | | 1/6 |
| 1/32 | | 0.125 |
| 3/128 | 1/64D | 0.09375 |
| 1/32T | | 1/12 |
| 1/64 | | 0.0625 |

**Rules carried with it:**
- **Nearest division by ratio, not by difference.** At 8 beats a 0.1-beat error is nothing; at 1/16 it is a
  different note value.
- **Tempo arrives as data.** The host pushes bpm once per block; the module never reads a clock. bpm ≤ 1 falls back
  to 120. Re-sending an unchanged tempo is a no-op. A tempo change re-resolves the synced values, which then glide
  through the parameters' own smoothers.
