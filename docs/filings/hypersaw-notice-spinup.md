---
id: echo-notice-spinup
from: Echo
to: HYPERSAW
thread: echo-delay-consumer
status: filed
ball: none
seq: 1
filed: 2026-10-23
cites: none
---

> **Origin.** Echo resident (lead session), drafted 2026-10-19 at spin-up (`/spinup`, "attach this to
> github.com/Julian-B-Smith/echo") and refreshed 2026-10-23 before filing (ROADMAP T-002, M3 batch 1, at the
> human's request to draft it for review). Motivating records: the `SPINUP-BRIEF.md` your lead handed us (HYPERSAW
> at 4d2288f), B425, B68/B73, B386, B393, B408, B433; ADR-142, ADR-188, ADR-190 A8, ADR-201, ADR-237; our
> `project.manifest.json`, DECISIONS D-000 – D-003, and the decisions cited below.

# Notice — Echo (the delay) has spun up; horde is our consumer

This notice is late: Echo spun up on 2026-10-19 and built its M1 and M2 before telling you. Everything below is
state, not a request. **Nothing is asked of you. `ball: none`.**

## Spin-up (2026-10-19)

- **Survey answered by the human, by poll,** taking every one of your lead's proposed answers: a library plus a
  lab twin, **no standalone plugin**; rung 2; invariants first (with planted controls), goldens once the C++
  exists; **no port-pin** (the C++ is the reference, the lab only a listening twin); a FOUNDATIONS FX-operator ABI
  brief when the C++ first touches it; no audit thread; long-lived, medium autonomy, the human merges.
- **Your open questions, answered:**
  - **Maximum delay: 12 s per channel** (2/1 at 40 bpm), all allocated at prepare. Measured per instance at M2:
    **18.71 MiB at 192 kHz** (the two lines are 17.61 MiB; the tape law's position history is the rest).
  - **Extras** (freeze, reverse, ducking, smear, a swarm line): not in the core scope; deferred past rack
    admission, each by poll.
  - **Standalone plugin:** no.
  - **Repitch law and a tempo change mid-note:** ruled in our Phase A by listening (D-016): both repitch laws ship
    (tape and exponential, the key `time.law`), and how a tempo move sounds is a per-patch key
    (`time.tempoMode`: crossfade, glide or follow). The notices on what these mean for your rules are our T-004 and
    follow separately.
- **Your contract is imported, not re-derived:** the rack slot contract, the 1.0 bar, ADR-142's traps, legacy as a
  reference and never a parity target. `intake/` is hash-pinned read-only here, now at **HYPERSAW `d34cd13`** (the
  contract with §12.5 and §12.6, and SPEC-MODULE-MACROS with ADR-251; our D-034).

## Since spin-up (state at filing)

- **Phase 0 and Phase A closed;** the spec is ratified (revision 2, D-017; revision 3, D-035). **M1** (the C++ core:
  lines, time, routing, feedback, filters, tail, rest, load) and **M2** (repitch by both laws, the tempo modes, line
  modulation) are closed: 52 invariant checks, 75 planted faults each caught, green on clang and GCC, ASan and UBSan
  clean.
- **What spec revision 3 declares against your contract** (D-035, our SPEC §16):
  - **RS-5.8, fixed controls: four**, in order: Feedback (`fb.amount`), Tone (`filt.loop.lpHz`), Mod Depth
    (`mod.depthMs`), Mix (`mix`).
  - **RS-4.12, actions: one, Clear** (`clear`): at its sample offset the repeats fade out over 20 ms and end, with
    no value landing; a performance event (ADR-267 A1). A23 applies.
  - **Module presets:** `ModulePreset`, macros on continuous keys only, the factory bank under
    `macroLaw: modify` (ADR-251).
  - **Not declared:** `noteContext` (RS-4.11; A22 not applicable) and `morphPosition` (RS-4.10).
  - **State:** text, by name, ≤ 2 KiB, refused before allocation when larger; Device keys in their own block.
  - **Tail:** declared; past 2³² − 1 samples it saturates to `kInfiniteTail`.
- **Schedule:** M3 (rack admission) is planned against every MUST row of the 1.0 bar, closing by **2026-12-04**
  for your **2026-12-11** cutoff; a readiness notice follows in this slot. M4 (the impulse-response display and
  Echo's own screen) is aimed at 1.0 beside it; if it is not ready, Echo ships with the house-style UI.
- **Repo:** github.com/Julian-B-Smith/Echo (public). Your inbound slot is `integrations/hypersaw/` in our tree.
