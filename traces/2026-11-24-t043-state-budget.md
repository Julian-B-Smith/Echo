# t043-state-budget — T-043: the state budget moves to 4 KiB per corner (D-051)

- **Queue item:** T-043 (criteria in ROADMAP).
- **Why:** horde's human ruled the re-derived state budgets on 2026-11-15 (ADR-401): 4,096 B per corner for Echo, counting the text as Echo writes it (`integrations/hypersaw/notice-state-budgets-ruled.md`, seq 5, `ball: Echo` for the constant). The human gave the go-ahead to start T-043 on 2026-11-24.
- **Evidence consulted:** ROADMAP T-043; the seq 5 notice; Echo's seq 4 (`integrations/hypersaw/response-state-budgets.md`); `core/include/echo/state.h` (`kMaxBytes`, `parse`); `core_tests.cpp` `stateSize`, `presetFactoryStateSize`; SPEC §11, §16.2; a tracked-file grep for every live 2 KiB / 2048.
- **What changed:**
  - `state.h`: `kMaxBytes` 2048 -> 4096; the refusal "over 2 KiB" -> "over 4 KiB", still the first test, before any allocation (A19). Comments in `state.h`, `engine.h`, `plants.h`.
  - `core_tests.cpp`: `stateSize`'s boundary rows at 4,096 and 4,097 bytes, as a LITERAL (critic H1); reports print the budget from `kMaxBytes`; the check and row names say 4 KiB. `docs/admission.md` regenerated (A19's row text).
  - `spec/SPEC.md` §11, §16.2 and a header amendment line (T-043's scope names §16.2).
  - DECISIONS D-051; `integrations/README.md` seq 5's row; the seq 5 notice committed (horde wrote it into our mailbox uncommitted); ROADMAP T-043 done.
  - Not changed: records of earlier items (DECISIONS before D-051, done ROADMAP rows, traces, filings) and `intake/` (protected, pinned; RS-6.12 there still says 2 KiB, and SPEC §16.2 states that ADR-401 moved it).
- **Readings:** defaults 462 B, worst case 618 B, factory 461 - 467 B; budget 4,096 B. Criterion 2: the 910 B ceiling fits.
- **Plants:** `stateOversizedAccepted` reads red at the new figure (critic's probe: 4,097 B refused for its size when off, for grammar when on); `presetStateOversized` still fails its own check. The literal pin was proven by two mutants of the constant: 3000 fails "a 4096-byte text was refused as oversized", 5000 fails "the 4097-byte text was refused for ... not its size first". Both reverted.
- **Critic (opus), one round, FIX-THEN-SHIP, all taken:** H1, nothing pinned 4096 once the test read `kMaxBytes` (fixed with the literal, D-051 ruling 4); M1, the seq 5 notice untracked and the ledger stale (fixed); M2, no gate checks the ledger against the files (recorded, not built); L1 - L3, D-051's figures and wording and a hard figure in a comment (fixed).
- **Verify:** `./verify fast` green before and after the fixes; see the PR body. GCC, ASan and UBSan are CI's.
- **Open:** horde's ball on its side (RS-6.12's text, A19; their B700, B701). T-027's readiness notice reports the figure.
