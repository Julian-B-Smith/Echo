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
| `spec/` | _planned, Phase A_ — scope list + parameter manifest |
| `lab/` | _planned, Phase A_ — single-file listening twin |
| `tools/` | _planned_ — headless Node oracles |
| `core/` | _planned, M1_ — C++20 library (CMake, Unix Makefiles) |
