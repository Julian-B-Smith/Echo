# Echo

horde's standard delay module: a stereo delay library hosted in horde's FX rack, with a lab twin for listening.
"Echo" is a working name; the module's name will be cleared before release.

**Status: spun up 2026-10-19 — nothing built yet.** The survey is done and the harness is green; the next step is
Phase 0 (prior art), then the spec and the lab (Phase A). horde's module cutoff is **2026-12-11**. Phase state lives in
[`ROADMAP.md`](ROADMAP.md).

## What it will do

Free time (ms) or tempo sync by horde's division names, with left and right offsets (up to 12 s per channel).
Standard, ping-pong and mid/side routing. Feedback with filters at the input, in the loop and at the output. Modulation
modes, including repitch (the tape-style glide when the time changes). Visualizers, possibly the impulse response.
**Status: planned** — every item above is spec work in Phase A.

## The map

| Path | What it holds | Status |
|---|---|---|
| `SPINUP-BRIEF.md` | Provenance, scope, horde's contract needs, the survey's proposed answers | done |
| `project.manifest.json` | The survey answers (the machine-readable record) | ratified 2026-10-19 |
| `ROADMAP.md` | Phases, gates, queue, open questions — the single source of truth | live |
| `DECISIONS.md` | Why each settled choice was made (append-only) | live |
| `CLAUDE.md` | The agent charter: harness contract + Echo's domain invariants | live |
| `intake/` | horde's contract packet (rack slot contract, 1.0 bar, I/O gain, macros, tempo divisions). Read-only, hash-pinned | done |
| `intake/legacy/` | What Echo absorbs from horde Legacy: ADR-142's delay and its oracle, the swarm time lab. References only, never parity targets | done |
| `integrations/` | Inbound mailbox (`integrations/<sender>/`) and the outbound thread table | live |
| `docs/filings/` | Drafts of what Echo files in other repos | live |
| `spec/`, `lab/`, `tools/`, `core/` | Spec, listening twin, oracles, C++ core | not yet built |
| `verify` | The oracle: `./verify fast` / `full` / `report` | live |

## Neighbours

- **horde (HYPERSAW)** hosts Echo. Echo's filings go to horde's `integrations/echo/`; horde's to Echo's
  `integrations/hypersaw/`.
- **FOUNDATIONS** provides the FX-operator ABI (brief filed when the C++ first touches it).
- **Scape** (reverb) owns diffusion. **Sluice** owns graph morphing. **Echo** owns discrete repeats.

## Run the oracle

```bash
./verify fast
```

_Last verified: 2026-10-19 (spin-up; `./verify fast` green)._
