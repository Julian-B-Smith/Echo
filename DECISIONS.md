# Echo — DECISIONS (append-only)

> The decision log. Append only: never edit or delete a prior decision;
> supersede it with a new one that cites it. ROADMAP.md holds task state,
> acceptance criteria and open questions; the *why* behind a ratified choice
> lives here. Status words: PROPOSED (lead's answer, awaiting the human) ·
> RATIFIED (human said yes, date) · SUPERSEDED (by which).

## Settled elsewhere — imported, not re-litigated (2026-10-19)

Ruled in HYPERSAW (horde) before the spin-out; they bind here, and re-opening
one is a brief to horde. Source: `SPINUP-BRIEF.md` and `intake/`.
- **Echo is a sibling, not part of horde** — the human reversed horde's B425
  "no sibling" ruling by poll, 2026-10-19.
- **The boundary:** diffusion is Scape's, graph morphing is Sluice's, discrete
  repeats are Echo's.
- **The rack slot contract** (ADR-201, amendments §12) and **the 1.0 bar**
  (`intake/module-1.0-bar.md`) are the consumer's terms: determinism, real time,
  units (ADR-009), tails, latency, the rack at the host rate (ADR-237), parameter
  classes, morph-corner patches (ADR-188), I/O gain (B435), macros (ADR-169 A4),
  mono safety (B408), CPU measured never gated, memory stated (ADR-236).
- **ADR-142's traps** (`intake/legacy/ADR-142-standard-delay.md`) are design
  constraints: ceiling on the returning term only, no loop-gain-by-tap-count,
  no dry-path limiter, filters out of circuit when off, no glide on preset load.
- **Legacy is reference, never parity** (B425): the legacy delay and the swarm
  time lab inform; nothing is pinned to them.

## D-000 — The spin-up survey (RATIFIED by poll, 2026-10-19 — see D-003)

Nine polls, 2026-10-19, every recommended option taken. The manifest is the
record; rejected alternatives are kept here so they are not re-proposed:
- **Q1** library + lab twin, no plugin. Rejected: + standalone plugin (JUCE
  build, auval/codesign, a second consumer with no demand yet); library only
  (loses the fast listening loop for repitch and modulation design).
- **Q2** rung 2 (thread + read-only verifier/critic). Rejected: rung 1 (no
  independent eye); rung 3 (one core, no seams, no bottleneck, ~15× tokens).
- **Q3** the brief's domain core, unnarrowed. Rejected: core + extras now;
  narrowing for 1.0 (mid/side, multi-mode modulation, in/out filters after 1.0).
- **Q3b** maximum delay **12 s per channel** (2/1 at 40 bpm). Rejected: 4 s
  (slow synced divisions clamp); 20 s (looper territory, ~2× memory).
  Memory, lines only, stereo float32: 4.39 MiB at 48 kHz, 17.58 MiB at 192 kHz.
  (The poll's option text said "~4.2 MiB"; that was an arithmetic slip by the
  lead, corrected here and in the manifest — see LIBRARY L0001.)
- **Q4** invariants first, goldens once the C++ exists. Rejected: invariants
  only (a silent tonal change in a preset would pass); goldens early (would
  enshrine defects, ADR-142's lesson).
- **Q5** no port-pin; the C++ is the reference from day one. Rejected: lab → C++
  pin (Bulwark shape; doubles DSP work for a twin that is only for listening).
- **Q6** provider to horde; consumer of FOUNDATIONS' FX-operator ABI (brief when
  the ABI is first touched). Rejected: horde only with a private ABI shim forever.
- **Q7** no audit thread. Rejected: a one-off legacy audit (nothing is ported).
- **Q8** six tags: time-accuracy, feedback-stability, modulation-repitch,
  stereo-mono-safety, sample-rate-independence, performance-budget. Rejected:
  a seventh `visualization` tag (no hard-won knowledge there yet).
- **Q9** long-lived, medium autonomy: PR per batch, the human merges.
  Rejected: high (long unattended runs); low (every batch live).
- **Open at survey, by poll:** the repitch law and tempo-change behaviour are
  decided in Phase A by listening (ROADMAP Q-A1, Q-A2), not now. Extras and a
  plugin are deferred (ROADMAP §Deferred).

## D-001 — Knowledge-loop forks (lead's default, 2026-10-19)

Candidate lessons inline in LIBRARY (`tier: candidate`), not a QUARANTINE file;
reflection enforced at session close via `/breakdown`. Matches the fleet
(Scape, Bulwark). Rejected: a quarantine file (no reviewer cadence to drain it).

## D-002 — intake/ is pinned by hash (lead's default, 2026-10-19)

`intake/MANIFEST` holds the sha256 of every handed-over file; `./verify`'s
`intake_gate` goes red on any edit or unlisted addition. The charter's "intake
is read-only" is enforced, not just stated (kit gate rule: assert the effective
state). A re-pin is a human gate. Rejected: prose-only protection.

## D-003 — Manifest ratified (RATIFIED by poll, 2026-10-19)

The human ratified `project.manifest.json` as surveyed, by poll, with no
changes; it is now a protected path. Same poll batch: the initial commit is
pushed straight to `main` on the empty remote (no base branch exists to PR
against; every later change is PR-per-batch), and the spin-up filings stay
drafts until ROADMAP T-002 (rejected: filing both now from this session).
