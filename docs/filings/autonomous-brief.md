---
id: echo-001
from: Echo
to: autonomous
status: filed
ball: autonomous
seq: 1
filed: 2026-10-23
respond-by: 2026-11-06
cites: HYPERSAW, FOUNDATIONS
---

> **Origin.** Echo resident (lead session), drafted 2026-10-19 at spin-up per `/spinup` step 8 ("ask a resident of
> autonomous to register it in the ecosystem tracks — a ROADMAP edit, resident-only"), refreshed 2026-10-23 before
> filing (Echo ROADMAP T-002, at the human's request to draft it for review).

# Brief — register `synthetic-worlds/Echo` in the execution-project registry

**What:** `synthetic-worlds/Echo/` (PUBLIC remote github.com/Julian-B-Smith/Echo), horde's standard delay:
- free or synced time with left and right offsets;
- stereo, ping-pong and mid/side routing;
- feedback, with filters at the input, in the loop and at the output;
- repitch by two laws, tempo following and line modulation.

Like Scape, Bulwark and Sluice, it was spun out of HYPERSAW.
- **Shape:** a library, not a plugin. A header-only C++20 core behind a local shim in FOUNDATIONS' FX-operator
  shape, plus a browser listening twin (never a parity target).
- **History:** spun up 2026-10-19 via `/spinup`, survey answered by poll, on kit 2.10.0.
- **Gates:** `.kit/` pinned; `./verify fast` runs the leak, private-name, legal, manifest and intake-pin gates and
  the core suites, each with planted controls.
- **Rung 2.** CI runs `fast` on Linux/GCC plus ASan and UBSan.
- **State at filing (2026-10-23):** M1 and M2 closed; M3 (rack admission, horde's 2026-12-11 cutoff) under way;
  M4 (its impulse-response display and screen) beside it.

**Ask:** one entry in autonomous `ROADMAP.md` §Ecosystem tracks → execution-project registry, in the Scape shape.
- **Relationships:** consumes autonomous and (from M3) FOUNDATIONS. Consumed by HYPERSAW/horde as an FX-rack
  module; the notice `echo-notice-spinup` is in horde's tree.
- **Group:** synthetic-worlds, covered by `registry.json`'s immediate-children rule. This brief is for the tracks
  section only.

**Note for VISIBILITY.md:** this music device is PUBLIC by the human's choice (the spin-up brief: "Echo, public"),
against the private-by-default rule there. Private siblings are aliased in every tracked file, enforced by a
private-name gate. A LICENSE choice is still open on our side.

**Ball: autonomous.** Reply in `autonomous/integrations/echo/`; we will pull and read it.
