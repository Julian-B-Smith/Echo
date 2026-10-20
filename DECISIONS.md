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

## Phase 0 prior-art proposals (2026-10-19)

From `docs/prior-art.md` (T-001). Each entry is PROPOSED: it shapes the
Phase A spec and the lab, and the human ratifies it there or strikes it. The
section of the prior-art doc holding the evidence is cited in each entry.

## D-004 — FIR interpolation on every read; no allpass, no plain linear in the loop (RATIFIED by poll, 2026-10-20 — see D-013)

**Avoid / reuse.** Use a 4-point FIR kernel (cubic Hermite or 3rd-order
Lagrange) for every fractional read. Keep the read position in double or fixed
point: at 12 s × 192 kHz a float32 position resolves only 0.25 sample. Size
each line as max delay + kernel half-width + the read/write-order margin
(Zavalishin & Parker). State and test the per-repeat HF loss (dB at 5, 10 and
20 kHz, offsets 0 and 0.5) across 44.1–192 kHz. Evidence: prior-art §1.
**Rejected:**
- *allpass and Thiran on a moving read:* recursive state clicks when the delay
  moves;
- *plain linear in the loop:* per-pass loss at 20 kHz is −16.7 dB at 44.1 kHz
  against −0.47 dB at 192 kHz, which breaks the SR-independence invariant;
- *the two-allpass + crossfader structure of US 5,742,532:* expiry unconfirmed.

The choice between Hermite and Lagrange, and whether to oversample the read,
is left to the lab.

## D-005 — Loop stability is a computed invariant; DC blocker after the ceiling (RATIFIED by poll, 2026-10-20, AS AMENDED — see D-013)

**Reuse.** For every reachable parameter set, the peak over frequency of
|g · H_loop(e^jω)| stays below 1 at the current SR. The bound includes filter
resonance, shelf boosts and the interpolation kernel. The ceiling on the
returning term (ADR-142) is the backstop, not the mechanism. The must-fail
plant is a loop shelf with gain above 1. A DC blocker sits inside the loop,
after the ceiling, with its pole derived from a cutoff in Hz. Evidence:
prior-art §2. The kernels' L1 norm (1.25) bounds sample peaks, not loop gain
(prior-art §1.3, LIBRARY L0002).
**Rejected:**
- *relying on the ceiling alone;*
- *anti-denormal DC or noise injection:* a DC blocker cancels it (de Soras
  2002); FTZ per RS-7.8 is the remedy;
- *fixed coefficient constants:* the corner moves with SR.

## D-006 — Repitch: read speed bounded and never reversed; four lab candidates for Q-A1 (RATIFIED by poll, 2026-10-20 — see D-013)

**Avoid / differentiate.** In repitch mode the read speed s = 1 − dD/dt stays
within [s_min > 0, s_max] as a tested invariant (zero reverse reads). Past
s_max, the mode falls back to a crossfade. Every repitch test asserts the
measured instantaneous ratio against 1 − dD/dt. Q-A1 is decided by listening
among these candidates:
- an asymmetric slew limit;
- a critically damped 2nd order with τ ∝ |ΔD|;
- a one-pole in the log-delay domain;
- varispeed with motor lag.

Each is tested with and without the crossfade fallback. Evidence: prior-art
§3.2.
**Rejected:** *a one-pole on D with a fixed τ.* It steps the pitch at onset,
reaches about +83 semitones on a 12 s → 10 ms move at τ = 100 ms, and reads
backward on any lengthening move faster than 1 s/s.

## D-007 — Crossfade mode: equal-gain law, deterministic retrigger, tested near unity (RATIFIED by poll, 2026-10-20 — see D-013)

**Reuse.** Crossfade-mode time changes use two read heads with an equal-gain
fade. This is RS-3.6's law for correlated paths, and it keeps the loop sum at
1 for close taps. The fade length is in ms, converted at the current SR. The
retrigger rule is deterministic, either a queue or a third head; the spec
picks one. The tests run at feedback ≥ 0.95. Jump mode's test asserts that the
click metric flags it. Evidence: prior-art §3.3.
**Rejected:** *equal-power inside the loop.* Correlated copies sum to about
1.41× the loop gain at mid-fade, which can push past unity.

## D-008 — Q-A2 framing: following a tempo ramp is a pitch shift of D·BPM′/BPM (RATIFIED by poll, 2026-10-20 — see D-013)

**Differentiate.** The spec states what the audible delay does on a tempo
change. No product manual found does. The lab tests four candidates:
- (A) always crossfade or jump to the new grid time;
- (B) follow continuously;
- (C) follow with a pitch cap and a crossfade past it;
- (D) a user choice.

The material is a slow ramp, a 2× step and sawtooth automation, at D = 0.25 s
and 3 s. Tempo is interpolated per sample within a block. The framing behind
the candidates is the ramp result: following a ramp gives a pitch ratio of
1 + D·(dBPM/dt)/BPM, which is +17 cents at D = 1 s but +1.65 semitones at
D = 10 s, so long lines need a policy. Evidence: prior-art §3.4.

## D-009 — Echo's click metric is probe- and SR-normalised; B265 reported beside it (RATIFIED by poll, 2026-10-20 — see D-013)

**Differentiate.** Three readings:
- **M1:** a residual against the analytic x(t − D(t)).
- **M2:** max|Δ²y| / (A·(ω·s_max/fs)²), normalised by construction.
- **M3:** an AR-whitened residual.

The must-fail controls are a planted impulse (with the detection floor
reported in dBFS), a naive integer-rounding engine, a block-rate-stepped D and
a clean fixed delay. The B265 reading stays reported, as the contract requires
(§12 A11). No perceptual threshold is claimed. Evidence: prior-art §4.
**Candidate filing:** M2 may answer the contract's suspended RS-4.4 (a ceiling
independent of probe and rate). That is an observation for horde once
measured, not now.

## D-010 — Mono-fold metric ρ = E_M / mean(E_L, E_R), per repeat and per band (RATIFIED by poll, 2026-10-20 — see D-013)

**Differentiate.** The B408 stereo report states, per routing mode:
- ρ broadband, per repeat generation and per octave band;
- the 1/3-octave-smoothed worst notch and its frequency.

It is measured with a mono impulse and seeded decorrelated noise (the
*unintended* test) and an S-only impulse (the *by-construction* row). The
planted controls are P1–P5 (prior-art §5.3). Echo warns when an L/R offset or
a near-coincident cross-feedback tap falls inside the ~40 ms colouring zone.
**Rejected:**
- *gating on correlation alone:* it cannot see combs between time-separated
  taps;
- *gating on a raw narrowband minimum.*

**Open for the human:** whether S-only content in mid/side mode is stated as
cancelling by construction, rather than as a B408 defect.

## D-011 — One offline IR renderer feeds the display, the tail oracle and the mono-fold metric (RATIFIED by poll, 2026-10-20 — see D-013)

**Differentiate.** The visualiser draws the true post-filter, post-routing
impulse response. No surveyed product was found doing so; existing displays
show taps, rings or spikes. The response is rendered from a non-real-time
clone of the engine on a worker thread: deterministic, cached by a hash of the
parameters, never probed from the live line. The main view is a stem
echogram in dB on a tempo grid, plus a per-generation spectrum strip (F^n).
The energy decay curve is secondary. The plot labels itself a snapshot when
the time is modulated or the ceiling is engaged. One renderer serves the
display, RS-7.3–7.6 and D-010, so what is seen is what is gated. Evidence:
prior-art §6–7.
**Rejected:**
- *reverb metrics (T60, EDT) as headline numbers:* a delay's decay curve is a
  staircase;
- *live probing:* it disturbs the line and is not deterministic.

## D-012 — "Echo" stays a placeholder until horde's B433 screen (RATIFIED by poll, 2026-10-19)

Prior-art §8 found the same name in the same category: stock delays named Echo
in Ableton Live, Logic Pro and Reason Studios, plus smaller uses. Amazon's
ECHO-formative class-9 marks exist too. The risk reads Medium–High for branded
use and Medium as a rack label only. This came from a web check, not legal
clearance. The human chose, by poll: **"Echo" remains the repo and working
name only.** The user-visible name is chosen at Phase N through B433, starting
from the doc's three generic directions.
**Rejected:**
- *a naming poll in Phase A:* the screens can be designed without the final
  name;
- *keeping "Echo" as the rack label:* in-host confusion with three DAWs'
  stock devices.

## D-013 — Phase 0 rulings: D-004–D-011 ratified, D-005 amended (RATIFIED by poll, 2026-10-20)

The human ruled on the eight Phase 0 proposals by poll. Before polling, the
lead re-derived the numbers they rest on: float32 position resolution 0.25
sample at 12 s × 192 kHz; linear-interpolation loss −16.7 dB vs −0.47 dB at
20 kHz; +83 st for a fixed-τ one-pole on a 12 s → 10 ms move; 1.41× for
equal-power correlated copies; the ramp law 1 + D·BPM′/BPM. All held. (The
"+1.65 st at 10 s" in D-008 assumes a 1 %/s ramp; scaling the rounded "+17
cents at 1 s" gives 1.63 st, a rounding difference only.)
- **D-004, D-006, D-007, D-008, D-009, D-010, D-011:** RATIFIED as written.
  Rejected alternatives offered: "M2 only for 1.0" for D-009 (the human kept
  all three readings).
- **D-005: RATIFIED AS AMENDED.** As written, it said loop gain stays below 1
  at every reachable setting, which would forbid past-unity feedback. That
  contradicts ADR-142 (`intake/legacy/ADR-142-standard-delay.md:32`: past-unity
  feedback parks at the ceiling, the musical edge-of-oscillation zone) and the
  ratified oracle row "the ceiling holds past unity" (D-000 Q4). Ruled reading:
  **the filter path is bounded, not the knob.** The computed invariant is
  peak |H_loop(e^jω)| ≤ 1 over the frequency grid at the current SR: loop
  filters, shelves, resonance and the interpolation kernel never ADD gain, so
  loop gain is bounded by the feedback g. g > 1 is a deliberate regime that the
  ceiling on the returning term parks; tails there are declared `kInfiniteTail`
  (RS-7.3–7.6). The planted control is unchanged (a loop shelf with gain > 1
  must fail). The DC blocker after the ceiling stands. Rejected: "always < 1"
  (caps feedback below unity, drops the edge zone and an oracle row).
- **Q-A4** (mid/side S-only content in mono: by construction or a B408 defect?)
  stays OPEN for Phase A's spec, by the human's choice.

## D-014 — Phase A: first rulings, and what the spec draft proposes (2026-10-20)

Phase A lead session (ROADMAP Phase A, T-003). Three polls before building, then the spec draft
(`spec/SPEC.md`, `spec/params.json`) and the lab (`lab/echo-lab.html`) with its smoke.
- **`./verify` + CI wiring: RATIFIED by poll** (a human gate). `lab_smoke_gate` runs
  `node tools/lab_smoke.mjs` in `fast`; `need_node` fails loudly rather than skipping; `structure_gate`
  lists the spec, the lab and the smoke; CI gains `setup-node`. Rejected: `./verify` only (CI would
  skip or fail on the step); diff-first review (the human chose to proceed).
- **Q-A4: RATIFIED by poll — by construction.** Mid/side never crosses M and S, so the mono fold IS
  the M line's output. S-only repeats vanishing in mono are not a B408 defect: the S line carries only
  the input's own side content, which the dry also loses in mono. "Unintended cancellation" = the
  output's fold losing more than the input's (ρ_out ≥ ρ_in per generation), tested on mono and seeded
  decorrelated inputs; the S-only impulse row is labelled by construction (spec §4.1; the smoke proves
  fold(out(L,R)) = fold(out(M,M)) with a planted S→M leak). Rejected: leaking S into M (changes the
  mode's sound to cover a non-defect); deciding after listening.
- **L/R offsets: RATIFIED by poll — % of the base time,** −50 … +50 % per side (spec §3.2). A ratio
  survives tempo changes and reaches dotted (+50 %) and triplet (−33.3 %) patterns. Rejected: signed ms
  per side (synced patterns drift off the grid on a tempo change); independent L/R times with a link
  (more keys, and "offset" becomes implicit).
- **PROPOSED, ratified with the spec** (the lead's choices, each stated where it lives in the spec):
  routing conventions — ping-pong sums to mono into the left line, mid/side with M = (L+R)/2 so the
  ceiling meets a mono input at the same level as stereo mode (§4); a routing switch is a 20 ms
  equal-gain matrix crossfade that keeps and re-images the tail (§4); equal-power mix (§8); loop filter
  one-pole 6 dB/oct, input/output two-pole Butterworth, no resonance anywhere in 1.0 (§6); ceiling
  knee 0.9 → 1.0 (ADR-142's law, §5.1); DC blocker 5 Hz, gain-normalised (§5.1); D-007's retrigger
  rule = **a queue, latest target wins** (§3.5); a load crossfades the head, never glides (§3.5); hard
  read-speed bound s ∈ [0.25, 4] and a 3 s repitch fallback (§3.6); synced times past 12 s fold by
  halving, free times clamp (§3.4); modulation slope cap ±25 % with 250 ms modulation ramps (§7); the
  defaults in `params.json`.
- **New open items, raised by the draft:** **Q-A5** — a bare "jump" (D-007 as ratified) cannot pass
  horde's A11: no bare jump at a flip, no step under a morph (spec §3.7; the lab measures M2 ≈ 37 for
  a bare jump, 0.3 for a 3 ms crossfade). **S-1** — D-000 Q4's "generation ratio = feedback
  exactly" cannot hold with D-005's always-in-circuit DC blocker; measured 3 × 10⁻⁵ relative at
  100 ms (spec §5.3).
- **Findings recorded** (LIBRARY candidates L0005–L0007): the textbook DC blocker peaks at 2/(1+R) > 1
  at Nyquist, so it alone breaches D-005's bound — use the gain-normalised form; D-009's M2 reads a
  speed *kink* at a few (slew and log one-pole laws read 4.1), between a smooth glide (≤ 1) and a
  jump (tens); a click probe commensurate with the test times is blind (440 Hz × 0.25 s = 110 cycles,
  so a 0.5 → 0.25 s jump reads clean) — the lab probes at 431.7 Hz; a modulation ramp time is a read
  *speed* (a 20 ms ramp to 20 ms depth moves the head at 2×), so modulation ramps are 250 ms.
