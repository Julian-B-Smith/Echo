# Echo

horde's standard delay module: a stereo delay library hosted in horde's FX rack, with a lab twin for listening.
"Echo" is a working name; the module's name will be cleared before release.

**Status (2026-10-22): M1 and M2 closed. M3 (rack admission, the 1.0 bar, due 2026-12-11) is planned and approved (D-028; queue T-010 – T-027), its first batch not started. M4 (the impulse-response display and Echo's own screen, aimed at 1.0 beside M3, D-027) is under way: the renderer, the render service and the display model are built (T-030 – T-032); its next batch waits for M3's meters (T-014).** Phase 0 closed 2026-10-20 (D-013). The spec is ratified at revision 2 ([`spec/SPEC.md`](spec/SPEC.md),
[`spec/params.json`](spec/params.json), 29 keys) and the listening lab is built ([`lab/echo-lab.html`](lab/echo-lab.html)).
The listening choices are ruled: two repitch laws (tape, exponential), a per-patch tempo policy, jump as a 3 ms
crossfade, and a load that ends the old repeats. S-1 and the defaults were ruled
with the spec's ratification (D-017). The C++ core (`core/`) implements the spec's lines, time, routing, feedback,
filters, mix, I/O gain, tail, rest and load (M1), and repitch by both laws, the tempo modes and line modulation (M2). horde's module cutoff is **2026-12-11**. Phase state lives in
[`ROADMAP.md`](ROADMAP.md).

## What it will do

Free time (ms) or tempo sync by horde's division names, with left and right offsets as a percentage of the base time
(up to 12 s per channel). Stereo, ping-pong and mid/side routing. Feedback to 120 % with a ceiling on the returning
term only. Filters at the input, in the loop and at the output, each truly bypassed when off. Time changes that
repitch, crossfade or jump, and a sine or seeded-drift modulation of the line. Visualizers, the impulse response
first. **Status: specified (spec revision 2 with its amendments), playable in the lab, and built in C++ (M1, M2);
horde's ABI, meters, state and presets are M3; the impulse-response renderer, service and display model are built
(M4), the screen is not.**

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
| `docs/design/echo-plan-map.html` | The plan map: every phase with its gate and state, the queue, the open questions and horde's cutoff, **parsed from `ROADMAP.md` on load** (no copy of its own). Serve the repo root (`python3 -m http.server`, or the `plan-map` entry in `.claude/launch.json`) and open `/docs/design/echo-plan-map.html`; `?check=1` runs its self-checks | live |
| `spec/` | The module spec and its parameter manifest (29 keys) | ratified 2026-10-20 (D-017), protected |
| `lab/echo-lab.html` | The listening twin: A/B panels and a blind bench for the Phase A choices; free play with every key; an IR echogram. Open it in Chrome or Safari, or serve it: `python3 -m http.server -d lab` | built |
| `tools/lab_smoke.mjs` | The lab smoke in `./verify fast`, every detector with a planted control | green |
| `core/` | The C++ core: `include/echo/engine.h` (header-only, C++20; M1 + M2), its invariant suite `tests/core_tests.cpp` (52 checks), run twice by `./verify` (clean, and under each of the 75 planted faults it guards); CI adds ASan + UBSan | M1, M2 closed; M3 planned |
| `core/include/echo/ir_render.h`, `ir_service.h`, `ir_view.h` | The one offline impulse-response renderer (D-011, D-030): what M4's display draws, what the tail oracle and M3's mono-fold metric measure; the render service (worker, cache, head-first frames) and the display model (columns, tempo grid, spectrum strip, EDC). Their suite `core/tests/ir_tests.cpp` runs in `./verify fast` as `echo_ir_tests` / `echo_ir_plants` | M4: T-030 done; T-031, T-032 built |
| `core/third_party/foundations/` | FOUNDATIONS' FX-operator header `fx_operator.h`, vendored verbatim at `301834b` with its `MANIFEST`; `vendor_gate` pins its bytes and proves itself on two plants each run (D-036). Not yet included by the core (T-013) | pinned |
| `core/manifest.lock.json`, `core/include/echo/module_version.h` | The manifest lock (B428): every key's identity, the fixed row, the actions and SPEC §1's hash, and the generated `kModuleVersion` presets carry. Held by `params_lock_gate` (`tools/manifest_lock.py`); regenerated only with `--decision D-NNN` (D-041). Protected | locked |
| `core/include/echo/presets.h`, `presets/echo-factory.json` | Module presets and macros (SPEC §16.3; D-042): the model, validator, both macro laws, and the bank file generated from C++ (`echo_presets --json`); `./verify` fails if the file drifts. The init preset now; the factory seven are T-021 | init only |
| `docs/admission.md` | horde's admission rows A1 – A23, each with the checks that evidence it, their planted faults and this run's result (D-043). GENERATED by `echo_tests --admission-out`; `./verify` fails if it is stale | 20 pass, A7 horde's, A21/A22 not declared |
| `tools/check_manifest.py` | The C++ parameter table checked against `spec/params.json` | green |
| `tools/plan_map_smoke.mjs` | The plan map's parser run headless against `ROADMAP.md`: its self-checks, all seven phases found, a pure model; each with a planted control. In `./verify fast` | green |
| `index.html` | The repo root's landing page: sends a served root (the `plan-map` preview) to the plan map, and links the lab | live |
| `verify` | The oracle: `./verify fast` / `full` / `report` | live |

## Neighbours

- **horde (HYPERSAW)** hosts Echo. Echo's filings go to horde's `integrations/echo/`; horde's to Echo's
  `integrations/hypersaw/`.
- **FOUNDATIONS** provides the FX-operator ABI (the brief and the vendored header are M3's T-012).
- **Scape** (reverb) owns diffusion. **Sluice** owns graph morphing. **Echo** owns discrete repeats.

## Run the oracle

```bash
./verify fast
```

_Last verified: 2026-10-22 (main at 369edb1; `./verify fast` green: both core suites, both IR suites, the lab and plan-map smokes)._
