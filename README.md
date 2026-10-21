# Echo

horde's standard delay module: a stereo delay library hosted in horde's FX rack, with a lab twin for listening.
"Echo" is a working name; the module's name will be cleared before release.

**Status: M1 (the C++ core) built 2026-10-20; it closes when its PR lands, and M2 is next.** Phase 0 closed 2026-10-20 (D-013). The spec is ratified at revision 2 ([`spec/SPEC.md`](spec/SPEC.md),
[`spec/params.json`](spec/params.json), 29 keys) and the listening lab is built ([`lab/echo-lab.html`](lab/echo-lab.html)).
The listening choices are ruled: two repitch laws (tape, exponential), a per-patch tempo policy, jump as a 3 ms
crossfade, and a load that ends the old repeats. S-1 and the defaults were ruled
with the spec's ratification (D-017). The C++ core (`core/`) implements the spec's lines, time, routing, feedback,
filters, mix, I/O gain, tail, rest and load; repitch, tempo following and line modulation are M2. horde's module cutoff is **2026-12-11**. Phase state lives in
[`ROADMAP.md`](ROADMAP.md).

## What it will do

Free time (ms) or tempo sync by horde's division names, with left and right offsets as a percentage of the base time
(up to 12 s per channel). Stereo, ping-pong and mid/side routing. Feedback to 120 % with a ceiling on the returning
term only. Filters at the input, in the loop and at the output, each truly bypassed when off. Time changes that
repitch, crossfade or jump, and a sine or seeded-drift modulation of the line. Visualizers, the impulse response
first. **Status: specified (spec revision 2), playable in the lab, and in C++ except repitch, tempo following and
modulation (M2) and horde's ABI, meters and presets (M3).**

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
| `spec/` | The module spec and its parameter manifest (29 keys) | ratified 2026-10-20 (D-017), protected |
| `lab/echo-lab.html` | The listening twin: A/B panels and a blind bench for the Phase A choices; free play with every key; an IR echogram. Open it in Chrome or Safari, or serve it: `python3 -m http.server -d lab` | built |
| `tools/lab_smoke.mjs` | The lab smoke in `./verify fast`, every detector with a planted control | green |
| `core/` | The C++ core: `include/echo/engine.h` (header-only, C++20), its invariant suite `tests/core_tests.cpp` (39 checks), run twice by `./verify` (clean, and under each of 52 planted faults); CI adds ASan + UBSan | M1 built; M2 next |
| `tools/check_manifest.py` | The C++ parameter table checked against `spec/params.json` | green |
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

_Last verified: 2026-10-20 (M1 on branch m1-core; `./verify fast` green, core gate included)._
