# CODEMAP — Echo

Where things are. Update in the same change that moves them.

| Path | Role |
|---|---|
| `verify` | Oracle dispatcher (project-owned gates: private-name, structure, manifest, intake pin) |
| `.kit/` | Kit-owned gates, vendored + sha256-pinned by `kit_sync.py` — never hand-edit |
| `.claude/` | Harness: settings (permissions), hooks (deny, dirty, stop gate), agents (critic, implementer, verifier), provenance skill |
| `intake/` | horde's contract packet, read-only, pinned by `intake/MANIFEST` |
| `intake/legacy/` | Legacy delay (`delay_core.h`), its 10-invariant oracle (`delay_check.cpp`), ADR-142, swarm time lab |
| `integrations/` | Inbound mailbox + outbound thread table |
| `docs/filings/` | Drafts of outbound filings |
| `traces/` | One provenance entry per merged change |
| `spec/SPEC.md` | The module spec: scope list, signal flow, time, routing, feedback, filters, modulation, declarations, memory (Phase A draft) |
| `spec/params.json` | The parameter manifest (B439/RS-2.1), one key per line; the lab must match it (smoke-checked) |
| `lab/echo-lab.html` | Single-file listening twin: DSP + ANALYSIS sections (sliced by the smoke, loaded into the AudioWorklet) then the UI |
| `tools/lab_smoke.mjs` | The lab smoke, in `./verify fast`: determinism, silence, finite, manifest, invariants — each with a planted control |
| `docs/design/echo-plan-map.html` | The plan map: MODEL section (pure ROADMAP.md parser + self-checks, sliced by the smoke) then the UI. Reads `../../ROADMAP.md` when the repo root is served |
| `tools/plan_map_smoke.mjs` | Runs the plan map's MODEL against ROADMAP.md headlessly, each check with a planted control |
| `.claude/launch.json` | `plan-map`: a static server on the repo root for the plan map |
| `core/` | _planned, M1_ — C++20 library (CMake, Unix Makefiles) |
