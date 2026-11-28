# fx-glass-answer — the sonar scope accepted as Echo's glass; seq 5 closed (D-052)

- **Queue item:** none of its own. It changes T-034's criteria 0 and 4 and T-035's scope (D-052), and closes horde's seq 5 (stacked on PR #43).
- **Why:** horde filed `integrations/hypersaw/brief-fx-glass.md` on 2026-11-25 (B712, ADR-409; first answer due 2026-12-09). The lead summarised it for the human and recommended accepting. The human agreed on 2026-11-27 and ruled by poll (D-052). Separately, PR #43 closes horde's seq 5 (`notice-state-budgets-ruled`), which T-043 had answered but which still read `ball: Echo`.
- **Evidence consulted:** the brief (sections 1 to 6); D-011, D-027, D-030 (Q-M4G); ROADMAP T-022, T-034, T-035; `spec/params.json` (29 keys, `time.ms` 1 to 12,000 ms, `time.div` to `4 bars`); SPEC §1 (the visualiser) and §3 (mid/side lines); autonomous `doctrine/INTEGRATIONS.md` (a terminal `status:` closes a thread; `ball: none` does not); `governor/ball_scan.py` `scan_repo`.
- **What changed:**
  - `integrations/hypersaw/brief-fx-glass.md` committed as horde wrote it (a resident act).
  - `integrations/hypersaw/response-fx-glass.md`: seq 2, `ball: HYPERSAW`. Accepts the sonar scope; the echogram, spectrum strip and EDC become pages; one reading (contrast in every scheme); one risk (afterglow read as decay: brightness is level, the fade is the wedge's); four questions; the hide conditions with T-022.
  - DECISIONS D-052; ROADMAP T-034 criteria 0 and 4, T-035's scope, and the Status block; `integrations/README.md` gets the thread's row.
  - On PR #43, beneath this one: `integrations/hypersaw/close-state-budgets.md` (seq 6, `status: closed`) and seq 5's ledger row.
- **Readings:** with the brief untracked, the ball scan read `hypersaw-notice-state-budgets` as Echo's and `hypersaw-brief-fx-glass` as Echo's. After seq 6 only `fx-glass` was. After seq 2, neither is Echo's.
- **Critic:** none. No code, gate or interface changes; the answer and the criteria changes were the human's, ruled by poll.
- **Verify:** `./verify fast` green; see the PR body.
- **Open:** horde's answers to the four questions and to the data form for hide conditions. The hide conditions themselves come with T-022, the design with T-034.
