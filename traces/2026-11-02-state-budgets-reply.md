# state-budgets-reply — answer horde's seq 3 on state budgets; T-043 opened

- **Queue item:** none (a mailbox reply); opens T-043.
- **Why:** horde's notice (`integrations/hypersaw/notice-state-budgets.md`, ADR-351) asks for two numbers by 2026-11-20. The human approved the reply by poll, 2026-11-02.
- **Evidence consulted:** `REPORT factory state sizes` and `REPORT state size` from a `./verify fast` log (largest factory 478 B, suite worst case 631 B, defaults 473 B; the state format and bank are the same on main); `core/include/echo/state.h` `write` (fixed keys and two header lines); `spec/params.json` (30 keys, 10 labelled). The 910 B ceiling is arithmetic: key + space + longest value + newline over the 30 keys, plus the headers, taking 24 characters as the longest shortest-round-trip text of a finite double. Not measured as a state.
- **Alternatives rejected:** an adversarial search over values before answering (its first attempt was blocked at the permission prompt, and the ceiling does not depend on a search; the human chose to file as drafted).
- **Verify:** see the PR body.
- **Later:** the reply now says D-050 (2026-11-03) drops the key count to 29, so the ceiling can only fall.
- **Open questions:** none; the ball is horde's.
