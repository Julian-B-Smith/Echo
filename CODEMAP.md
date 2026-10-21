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
| `core/include/echo/engine.h` | The M1 engine, header-only C++20 behind a local ABI shim (Bulwark's shape): lines, reads, time, routing, feedback, filters, mix, gains, tail/rest/load, non-finite guards |
| `core/include/echo/params.h` | The 29-key table (handles = indices) and horde's division list; mirrors `spec/params.json` |
| `core/include/echo/plants.h` | The planted faults: constexpr false in production, switchable in `echo_plants` only |
| `core/include/echo/ftz.h` | FTZ/DAZ RAII scope (RS-7.8), restores the host's control word |
| `core/tests/core_tests.cpp` | The invariant suite (39 checks); one source built as `echo_tests` (clean) and `echo_plants` (each check under its faults; every plant must be guarded) |
| `core/tests/alloc_probe.h` | Allocation probe (operator new + Darwin malloc_logger); also measures the prepare() memory figure; stands down under ASan |
| `core/tools/manifest.cpp` | `echo_manifest`: prints the C++ table as JSON for `tools/check_manifest.py` |
| `core/tools/render_hash.cpp` | The fp-contract probe: one fixed render, hashed; built as `echo_hash_ref` / `echo_hash` / `echo_hash_nopragma` |
| `core/tests/guard_probe.cpp` | Compiled under -ffast-math / -ffinite-math-only by `./verify`: must hit the engine's `#error` |
| `tools/check_manifest.py` | C++ table == `spec/params.json`, proven on 4 planted drifts |
