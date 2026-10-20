# legal-local — legal/IP analysis moved to gitignored local/legal/, enforced by legal_gate

- **Queue item:** unqueued: the human's cross-project rule, relayed by horde's
  lead (session message, 2026-10-20) and confirmed here by poll (D-014).
- **Why:** Echo's repo is public; legal and IP readings must not sit in tracked
  files, PRs or public posts.
- **Evidence consulted:** `git grep` over tracked files (outside intake/) for
  legal shapes; docs/prior-art.md §1, §8, §9 and references; DECISIONS D-004,
  D-012; traces/2026-10-19-prior-art.md.
- **Changed:** `local/` gitignored; the readings copied verbatim into
  `local/legal/` (untracked); neutral pointers left in prior-art §8, §9, the
  reference list, D-004, D-012 and the Phase 0 trace (a recorded exception to
  append-only, D-014); ROADMAP Phase N reworded; CLAUDE.md §Domain legal note;
  `./verify` gains `legal_gate`.
- **Gate proven:** on a scratch copy, the pre-move prior-art.md and DECISIONS.md
  (from 5505dc5) → red; office name, registration number, risk rating (en-dash
  and hyphen) → red; plain words ("a patent pass", "B433 clearance", "a
  medium-high shelf", "High-pass") → quiet; a file under local/legal/ → quiet
  (ignored); restored → green. The rating branch first used `[–-]` and stayed
  silent on its plant; fixed to `(–|-)` (LIBRARY L0005).
- **Alternatives rejected:** leaving the trace lines; a prose-only rule.
- **Verify:** `./verify fast` exit 0 before commit.
- **Open questions:** history is not rewritten (the human's ruling), so commits
  3186292 and 5505dc5 still carry the text.
