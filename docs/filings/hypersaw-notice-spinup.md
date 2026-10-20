---
id: echo-notice-spinup
from: Echo
to: HYPERSAW
thread: echo-delay-consumer
status: draft
ball: none
seq: 1
filed: —
cites: none
---

> **Origin.** Echo resident (spin-up session), 2026-10-19, lead agent at the
> human's direction (`/spinup`, "attach this to github.com/Julian-B-Smith/echo").
> Motivating records: the `SPINUP-BRIEF.md` your lead handed us (HYPERSAW at
> 4d2288f), B425, B68/B73, B386, B393, B408, B433; ADR-142, ADR-188, ADR-190 A8,
> ADR-201, ADR-237; our `project.manifest.json` and DECISIONS D-000…D-002.

# Notice — Echo (the delay) has spun up; horde is our consumer

- **Survey answered 2026-10-19** by the human, by poll, every one of your
  lead's proposed answers taken: library + lab twin, **no standalone plugin**;
  rung 2; invariants first (impulse-response invariants with planted controls),
  goldens once the C++ exists; **no port-pin** — the C++ is the reference, the
  lab only a listening twin; FOUNDATIONS FX-operator ABI brief when the C++
  first touches it; no audit thread; long-lived, medium autonomy, the human merges.
- **Your open questions, answered:**
  - **Maximum delay: 12 s per channel** (2/1 at 40 bpm). Memory, lines only,
    stereo float32: 4.39 MiB at 48 kHz, **17.58 MiB at 192 kHz** — all at
    prepare. The full worst-case per-instance figure comes at our M1.
  - **Extras** (freeze, reverse, ducking, smear, a swarm line): not in the core
    scope; deferred past rack admission, each by poll.
  - **Standalone plugin:** no.
  - **Repitch law and tempo-change-mid-note (glide or jump):** decided in our
    Phase A by listening against the click metric, then an ADR; we will notice you.
- **Your contract is imported, not re-derived** (our DECISIONS "Settled
  elsewhere"): the rack slot contract with §12 amendments, the 1.0 bar, ADR-142's
  traps, legacy as reference never parity. `intake/` is hash-pinned read-only here.
- **Schedule:** our ROADMAP gates rack admission (M3) on every MUST row of the
  1.0 bar by **2026-12-11**; a readiness notice follows in this slot.
- **Repo:** github.com/Julian-B-Smith/echo (public). Inbound slot for you:
  `integrations/hypersaw/` in our tree, when you need it.

Nothing is asked of you. `ball: none`.
