# CODEMAP — Echo

Where things are. Update in the same change that moves them.

| Path | Role |
|---|---|
| `verify` | Oracle dispatcher (project-owned gates: private-name, structure, manifest, intake pin, legal, lab smoke, core) |
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
| `core/include/echo/engine.h` | The engine, header-only C++20 behind a local ABI shim (Bulwark's shape). M1: lines, reads, time, routing, feedback, filters, mix, gains, tail/rest/load, non-finite guards. M2: `stepHead` (moves, §3.5-3.8), the tape law (`tapeStep`/`tapeP`/`tapeSolve`, a quadratic 32-sample history), the exponential law, `backstop` (the speed invariant), `modStep`/`modTargets`/`reseed` (§7) |
| `core/include/echo/params.h` | The 29-key table (handles = indices) and horde's division list; mirrors `spec/params.json` |
| `core/include/echo/ir_render.h` | THE offline IR renderer (D-011; D-029 as amended by D-030; T-030): `IrRenderer` drives a private, reset `Echo` through the public API (every key re-sent), an impulse / L / R / S-only / seeded-noise input at a stated level (default −20 dBFS); `irHash` is the cache key over every input; `renderIr` is the one-shot form for tools. Feeds the display (M4), the tail oracle and the mono-fold metric (M3's T-019) |
| `core/include/echo/ir_service.h` | The render service (T-031): the one renderer on ONE worker thread; preallocated cache slots keyed by `irHash`; head-first frames (frame 1 = 96 000 samples, D-033; then 0.1 s chunks of the same resumable render); every chunk cancellable, the head's too; a request goes on screen only at its first publication; cut renders are reused first; `pause()`/`resume()` for a closed editor. UI-thread API only (it locks): never the audio thread |
| `core/include/echo/ir_view.h` | The display model (T-032), pure: `irColumns` (each column the render's own peak sample: gate 1 in the drawing; `kIrColumnNotRendered` past a partial frame), `irDb`, `irBeats` / `irDivisionMarks` (the tempo grid), `irGenerationWindows` (each channel's generation arrivals per routing, SPEC §4) and `irGenerationSpectra` (the per-generation spectrum strip), `irEdc` (Schroeder's backward integral) |
| `core/tests/ir_tests.cpp` | M4's suite, built as `echo_ir_tests` (clean) and `echo_ir_plants` (each check under its M4 plants); its own file so M4 never edits `core_tests.cpp` (D-030). The reference `plain()` is an independent `Echo` set by key name, 333-sample blocks |
| `core/include/echo/plants.h` | The planted faults: constexpr false in production, switchable in `echo_plants` only |
| `core/include/echo/ftz.h` | FTZ/DAZ RAII scope (RS-7.8), restores the host's control word |
| `core/tests/core_tests.cpp` | The invariant suite (52 checks, M1 + M2); one source built as `echo_tests` (clean) and `echo_plants` (each check under its faults; every plant must be guarded). `--click-report` prints the click readings the D-023 threshold rests on |
| `core/tests/alloc_probe.h` | Allocation probe (operator new + Darwin malloc_logger); also measures the prepare() memory figure; stands down under ASan |
| `core/tools/manifest.cpp` | `echo_manifest`: prints the C++ table as JSON for `tools/check_manifest.py` |
| `core/tools/render_hash.cpp` | The fp-contract probe: one fixed render, hashed; built as `echo_hash_ref` / `echo_hash` / `echo_hash_nopragma` |
| `core/tests/guard_probe.cpp` | Compiled under -ffast-math / -ffinite-math-only by `./verify`: must hit the engine's `#error` |
| `tools/check_manifest.py` | C++ table == `spec/params.json`, proven on 4 planted drifts |
| `docs/design/echo-plan-map.html` | The plan map: MODEL section (pure ROADMAP.md parser + self-checks, sliced by the smoke) then the UI. Reads `../../ROADMAP.md` when the repo root is served |
| `tools/plan_map_smoke.mjs` | Runs the plan map's MODEL against ROADMAP.md headlessly, each check with a planted control |
| `.claude/launch.json` | `plan-map`: a static server on the repo root for the plan map |
