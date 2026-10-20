---
name: provenance
description: Decision-trace discipline. Defines the trace entry every merged change must produce and the evidence standard for claims about the codebase.
---

# Provenance

## Evidence standard

A claim about the codebase is either **evidenced** (file:line, a verify run,
a git hash, a ROADMAP entry) or a **hypothesis** (phrased as one). There is
no third category. "Verified" means an oracle checked it; "entailed" means it
follows from evidenced premises; neither implies the other — label which one
you mean.

## Trace entries

Every completed change set writes `traces/YYYY-MM-DD-<slug>.md`:

```markdown
# <slug> — <one-line what>

- **Queue item:** <ROADMAP id or "unqueued: reason">
- **Why:** <one or two sentences; the decision, not a diff narration>
- **Evidence consulted:** <files read, ROADMAP sections, prior traces>
- **Alternatives rejected:** <briefly, with the reason — or "none considered">
- **Verify:** <target, exit code, git hash from .harness/last-verify.json>
- **Open questions:** <anything unverified or deferred; "none" is a claim>
```

Traces are append-only. Never edit or delete a prior trace; correct the
record with a new entry that cites the old one.
