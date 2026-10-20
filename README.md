# Echo

horde's standard delay module: a stereo delay library hosted in horde's FX rack, with a lab twin for listening.
"Echo" is a working name; the module's name will be cleared before release.

**Status: Phase A in progress (2026-10-20).** The spec is drafted ([`spec/SPEC.md`](spec/SPEC.md),
[`spec/params.json`](spec/params.json)) and the listening lab is built ([`lab/echo-lab.html`](lab/echo-lab.html):
open it in a browser). The open choices — the repitch law (Q-A1), tempo changes (Q-A2), what "jump" means (Q-A5) —
are made by ear in the lab, then the human ratifies the spec. No C++ yet. horde's module cutoff is **2026-12-11**.
Phase state lives in [`ROADMAP.md`](ROADMAP.md).

## What it will do

Free time (ms) or tempo sync by horde's division names, with left and right offsets as a percentage of the base time
(up to 12 s per channel). Stereo, ping-pong and mid/side routing. Feedback to 120 % with a ceiling on the returning
term only. Filters at the input, in the loop and at the output, each truly bypassed when off. Time changes that
repitch, crossfade or jump, and a sine or seeded-drift modulation of the line. Visualizers, the impulse response
first. **Status: specified in the Phase A draft and playable in the lab; the C++ starts at M1.**

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
| `docs/prior-art.md` | Phase 0 landscape: interpolation, feedback, time changes, click and mono-fold metrics, IR display, competitors, name check | done 2026-10-19 |
| `docs/filings/` | Drafts of what Echo files in other repos | live |
| `spec/` | The module spec and its parameter manifest (27 keys) | draft, awaiting ratification |
| `lab/echo-lab.html` | The listening twin: A/B panels for Q-A1, Q-A2, Q-A5; free play with every key; an IR echogram | built |
| `tools/lab_smoke.mjs` | The lab smoke in `./verify fast`, every detector with a planted control | green |
| `core/` | The C++ core | M1, not started |
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

_Last verified: 2026-10-20 (Phase A draft; `./verify fast` green, lab smoke included)._
