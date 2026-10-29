# REFLECTIONS — Echo

Dated notes worth holding between sessions (`/wakeup` flags stale ones). Not decisions (those are DECISIONS.md),
not lessons (those are LIBRARY.md, behind the write gate).

- [2026-10-22] The horde filings are the loose thread most likely to be forgotten (the human's answer at close):
  T-002 (Echo's spin-up notice to horde and the autonomous registration brief) and T-004 (Phase A's three notices:
  the tempo-mode amendment, the mode-dependent switch declaration, the 1 kHz probe finding) are still drafts in
  docs/filings/ two days after spin-up, while Echo's M1, M2 and the M3/M4 plans have landed. horde has heard nothing
  from Echo yet.
  _Stays until T-002 and T-004 are filed (confirmed at /wakeup, 2026-10-22)._
  _Half resolved 2026-10-29: T-002 was filed 2026-10-23 (horde PR #1201, autonomous PR #61); T-004's three
  notices are still drafts._

_Pruned at /wakeup 2026-10-22: the critic-review note graduated to ROADMAP's standing rule; the flaky clock gate
graduated to ROADMAP T-036; the outlier note dropped (already LIBRARY L0013, L0014)._

- [2026-10-29] The fixed-row rethink is the loose thread most likely to be forgotten (the human's answer at close).
  D-035 ruled four fixed controls (Feedback, Tone, Mod Depth, Mix), D-041 locked them, and the spin-up notice told
  horde "four"; the human now doubts Echo needs four, and the lead proposed two (Feedback, Mix), with Tone kept on
  the face and greyed while the loop filter is off. Changing it touches SPEC §16.4, `params.json` `fixedControls`,
  the manifest lock (`manifest_lock.py write --decision D-NNN`, so `module_version` and the presets JSON move) and
  needs a horde notice (T-027). T-022's face spec and face check are held on it.
