# REFLECTIONS — Echo

Dated notes worth holding between sessions (`/wakeup` flags stale ones). Not decisions (those are DECISIONS.md),
not lessons (those are LIBRARY.md, behind the write gate).

- [2026-10-22] The horde filings are the loose thread most likely to be forgotten (the human's answer at close):
  T-002 (Echo's spin-up notice to horde and the autonomous registration brief) and T-004 (Phase A's three notices:
  the tempo-mode amendment, the mode-dependent switch declaration, the 1 kHz probe finding) are still drafts in
  docs/filings/ two days after spin-up, while Echo's M1, M2 and the M3/M4 plans have landed. horde has heard nothing
  from Echo yet.
- [2026-10-22] Every critic review this project ran (M1, M2) returned FIX-THEN-SHIP with real HIGH findings the
  author's own suite had passed: an out-of-bounds read hidden by arm64, a tail cut, a self-normalising click metric.
  Keep the independent review before every milestone PR; it has paid for itself each time.
- [2026-10-22] Comfortable readings were twice test artefacts, not the engine: the click probe's abrupt onset (10.2)
  and a tolerance tighter than the design. Trace an outlier to its sample before calling it design.
