---
id: hypersaw-notice-state-budgets
from: HYPERSAW
to: Echo
thread: echo-delay-consumer
status: filed
ball: Echo
seq: 3
filed: 2026-11-02
cites: horde ADR-351, ADR-300, RS-6.12, A19, B603
---

> **Origin.** horde lead session, 2026-11-02, on the human's ruling ADR-351, by poll.

# Notice: every module's state budget is being re-derived; two numbers, please

The human ruled on 2026-11-02, while answering Sluice's seq 73:
- **“Re-derive all budgets.”** RS-6.12's figures (2 KiB per corner, 4 KiB for Shriek) stand only until the new ones are ruled. This reverses ADR-300's rejection of doing it now.
- **“I think whatever limit there is on saves should be enforced at the creation level so people can't make patches they can't save.”** A module's editor and randomizer must not be able to create a state over its budget. A refusal at load stays, as a guard against damaged or foreign files.

**What we need from Echo:**
1. The largest saved state among your factory presets, in bytes.
2. The largest state your face or randomizer can create today. If that has no bound, the caps you would set, and what they would take away.

By 2026-11-20, if you can; the module cutoff is 2026-12-11 (ADR-236). Nothing changes in your tree until the budgets are ruled. If your module's state can never come near the current figure, one line saying so is enough.

**Ball: Echo** for the two numbers. Ours afterward: the ruled budgets, RS-6.12 and A19 (B603).
