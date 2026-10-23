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

Name: placeholder until horde's B433 clearance; notes kept privately (D-014). The human
chose, by poll: **"Echo" remains the repo and working name only.** The
user-visible name is chosen at Phase N through B433.
**Rejected:**
- *a naming poll in Phase A:* the screens can be designed without the final
  name;
- *keeping "Echo" as the rack label.*

(Body reduced 2026-10-20 under D-014.)

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

## D-014 — Legal and IP analysis lives in gitignored local/legal/ (RATIFIED by poll, 2026-10-20)

The human's cross-project rule, relayed by horde's lead and confirmed here by
poll: legal and IP analysis (trademark readings, patent notes, clearance
assessments, Phase N's patent/IP pass and B433 screen outputs) never sits in a
tracked file, a PR or a public post. It lives in `local/legal/`, gitignored.
Tracked files keep a neutral pointer only.
- **Moved 2026-10-20:** prior-art §8 and its references; the patent mentions in
  prior-art §1, §9 and the reference list; D-004's patent bullet; D-012's
  reading of marks and risk; two open-question lines in the Phase 0 trace.
- **A recorded exception to append-only:** D-004's and D-012's bodies and that
  trace were edited in place, because leaving the text there defeats the rule.
  Their decisions are unchanged. D-004 still rejects allpass on moving reads on
  technical grounds, which excludes that structure anyway.
- **History is not rewritten** (the human's ruling for horde, 2026-10-17):
  commits before this change still carry the text.
- **Enforced, not just stated:** `./verify`'s `legal_gate` fails on patent and
  registration numbers, trademark-office names and risk ratings in tracked
  files outside `intake/` (horde's pinned copy), proven on a plant.
Rejected: leaving trace lines untouched; a prose-only rule.

## D-015 — Phase A: first rulings, and what the spec draft proposes (2026-10-20)

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
- **Findings recorded** (LIBRARY candidates L0006–L0008): the textbook DC blocker peaks at 2/(1+R) > 1
  at Nyquist, so it alone breaches D-005's bound — use the gain-normalised form; D-009's M2 reads a
  speed *kink* at a few (slew and log one-pole laws read 4.1), between a smooth glide (≤ 1) and a
  jump (tens); a click probe commensurate with the test times is blind (440 Hz × 0.25 s = 110 cycles,
  so a 0.5 → 0.25 s jump reads clean) — the lab probes at 431.7 Hz; a modulation ramp time is a read
  *speed* (a 20 ms ramp to 20 ms depth moves the head at 2×), so modulation ramps are 250 ms.

## D-016 — Phase A: the listening rulings, and the critic's rework of the spec draft (2026-10-20)

The human listened in the lab (`lab/echo-lab.html`; a bench of the four repitch laws was added at their request,
with a blind mode) and ruled by poll. An independent critic (Opus, fresh context, read-only; verdict REWORK, 7 HIGH,
no CRITICAL) reviewed revision 1 (dab6f9b) first; its findings are folded into revision 2 of the spec, the lab and
the smoke.
- **Q-A1: RULED — two laws ship, as a key.** The human: "I like the sound of varispeed and of one-pole; maybe both
  should be options." New stepped key `time.law` ∈ {tape (varispeed, D-006 d), exponential (one-pole on ln D,
  D-006 c)}; `time.glide` is each law's time constant. Default proposed: tape (ratified with the spec). Not chosen:
  the asymmetric slew (a) and the critically damped 2nd order (b); they stay in the lab as references.
- **Q-A2: RULED — (D), a key.** `time.tempoMode` ∈ {crossfade (A), glide (B), follow (C)}. Default proposed: glide,
  which is horde's tempo rule as written (`intake/tempo-divisions.md`: re-resolved values "glide through the
  parameters' own smoothers"); crossfade and follow amend that rule for the patches that pick them, and horde gets a
  notice (ROADMAP follow-up).
- **Q-A5: RULED — jump is a 3 ms equal-gain crossfade.** **Amends D-007's jump line** ("Jump mode's test asserts
  that the click metric flags it"): a bare jump cannot pass horde's A11 (no bare jump at a flip, no step under a
  morph). The bare jump survives only as the click metric's planted must-fail control, which D-009 already wants.
  Rejected: keeping the bare jump and declaring jump mode inadmissible under morph; dropping jump as a user mode.
- **Load: RULED — the ringing repeats end; spill-over is horde's.** The critic's one question for the human
  (RS-6.7, RS-6.13 and RS-6.5 give a load's old tail to horde's spill-over setting). A load fades the old audio out
  over 20 ms (LoadDecl: crossfade-out, 20 ms), clears the lines and lands the heads on the new times. Rejected:
  keeping the line (it would decide spill-over inside the module, and with horde's spill-over "on" play the old
  tail twice).
- **The critic's HIGH findings, each fixed and gated with a plant that fires:**
  1. modulation could breach D-006's speed bound (a rate glide, a spread ramp, a shape switch; the clamp only on the
     repitch path) → the modulation **offset is slew-limited** to ±0.25 sample/sample, so the bound holds by
     construction; the gate runs every law, move and modulation extreme with zero backstop hits;
  2. drift overshot [−1, 1] (to 1.25) and read past the line → the drift curve is clamped, every read is held inside
     the line and an attempt is counted;
  3. the tail declaration contradicted its own gate → a tail gate that also fires when the declared tail has elapsed,
     clearing lines, filters and DC, so "exact zero at the declared tail" holds by construction;
  4. load vs RS-6.7 → the ruling above; Device keys are no longer written by a load (RS-4.5);
  5. switch declarations outside A11's vocabulary → repitch is declared a seeded jump of the read position,
     crossfades with stated lengths, `time.change` / `time.law` / `time.tempoMode` deferred (each move is latched to
     the modes in force when it starts; flipping mid-glide no longer jumps the head), with plants for an eager flip
     and an unseeded reseed;
  6. the reset check had no plant and moved nothing → reset from mid-ramp, with a reset that keeps the old line as
     the plant (the canonical "unsnapped smoother" plant cannot fire by design: reset puts the instance at rest,
     and rest snaps everything);
  7. sleep and wake were unaddressed → a **rest state**: at rest (after the gate) everything that would drift is
     held and every parameter lands at its target, so a slept instance equals its never-slept twin (A16), and the
     first block after prepare or reset lands on the host tempo (the critic's M2).
- **MEDIUM and LOW findings folded in:** the past-unity level is stated as a bound (< 2 after the input stops;
  ≤ input peak + 2 while it plays), not a measured 1.25; real plants replace the empty ones (a residue at rest, both
  NaN-repair layers removed, a guard that zeroes without counting); new coverage with plants: equal-power
  crossfade, instant routing and filter switches, a dry-path limiter, a dry leak at mix 100, a frozen tempo, the
  I/O gain law, Input Gain before the loop; all loop state is repaired on a non-finite value; synced folding keeps
  the L/R ratio; depth ≤ D − 3 samples; the §3.8 figures corrected (+51 cents at 3 s, not a semitone); the
  `smoothing` labels fixed; the static ban covers AudioWorklet clocks; the Q-A2 panel lets (B) follow any law.
- **Found by the rework itself:** after the rest state landed, the speed checks fed by an impulse went to rest and
  measured nothing — a vacuous pass. They now run on a constant non-zero input and require that the gate never
  fired. And the tape law's cursor search was unbounded per sample when the cap held the head back (10.7 s for one
  lab render; an unbounded loop on the audio thread in the C++); it is now bounded at the speed limit.
- **Still open:** S-1 (spec §5.3) and the two new keys' defaults, both ratified with the spec. Follow-ups for horde
  (spec §14): the tempo-rule notice, the mode-dependent switch declaration, and the 1 kHz probe's blind spot.

## D-017 — The Phase A spec ratified, with S-1 and the new keys' defaults (RATIFIED by poll, 2026-10-20)

- **S-1: RATIFIED — the generation ratio is g within 10⁻³**, with generation 2 exactly g·(1+R)/2 (the DC
  blocker's impulse gain). D-005's 5 Hz blocker stays as ratified; D-000 Q4's "= feedback exactly" is read
  through it (spec §5.3). The lab's gate, provisional until now, stands. Rejected: a 1 Hz corner (still not
  exact, slower DC recovery); switching the blocker out while the ceiling is idle (a switched loop element).
- **Defaults: RATIFIED — `time.law` = tape, `time.tempoMode` = glide.** Glide is horde's tempo rule as written,
  so the default patch amends nothing. Rejected: exponential by default; follow by default.
- **`spec/` revision 2: RATIFIED** (spec/SPEC.md, spec/params.json at 29 keys) and now a protected path (charter
  §Domain). `lab/` is NOT ratified by this poll and stays unprotected; it is a listening twin that follows the
  spec. The Phase A gate is met when this lands on main: spec ratified, the lab smoke in `./verify fast`, Q-A1,
  Q-A2, Q-A4, Q-A5 and S-1 closed.
- Same poll: merge main into the Phase A branch (keeping main's `legal_gate` and D-014 beside this branch's
  `lab_smoke_gate`, D-015–D-017), push it and open a PR; the human merges.

## D-018 — Rest waits for the filters: "quiet" includes the wet (RATIFIED by poll, 2026-10-20; amends SPEC §10)

**Finding (M1).** SPEC §10's first rest condition read "the input silent for a whole read span and every line
write in that span below −120 dBFS". Building it in C++ showed that the condition fires one read span after the last
loud write, while the loop filter and the output filter are still ringing on what was just read: resting clears
their state mid-ring. Measured on the core (48 kHz, 100 ms, fb 0, a 20 Hz loop low cut): the gate cut a tail at
**0.0285**, an audible step. It also corrupted the M1 sample-rate check (a 100 Hz reading of −3.2 dB where the filter
gives −0.2 dB), which is how it surfaced.

**Ruled by poll:** "quiet" means the line writes **and** the wet — the read after the loop filter and the decoded
wet after the output filter — below −120 dBFS for the span. The second condition (the declared tail elapsed) is
unchanged and still bounds rest, so the declaration stays exact by construction: rest can only come later than
under the literal text, never after the declared tail. SPEC §10 is amended to cite this decision (a protected-path
edit, approved in the same poll).

**Enforced.** `core/tests/core_tests.cpp` "rest never cuts a sound" requires the last sample before the final
exact-zero run to be below −120 dBFS, at three loop low cuts; the planted fault `gateLineOnly` (the literal §10
text) fails it (0.0285).

**Follow-up.** The lab (`lab/echo-lab.html`) implements the literal text and has the same cut. It is a listening
twin, not a reference, so M1 does not touch it; fixing it is ROADMAP T-005.

Rejected: keeping the spec text and recording M1 as stricter (two texts for one behaviour drift; the spec would
describe a click).

## D-019 — The tail clock restarts at a hold or a tail change (RATIFIED by poll, 2026-10-20; amends SPEC §10)

**Finding (the M1 critic, H2).** SPEC §10's second rest condition, "the declared tail has elapsed since the input
went silent", measured silence from the input alone. The declared tail is recomputed whenever feedback, time or
modulation depth changes, so lowering feedback mid-tail shrank the bound under repeats already ringing. Measured on
the core: a loop held at 120 % for 10 s, then pulled to 35 %, rested 225 samples later and cut a wet of −0.70; 90 %
→ 10 % after 10 s cleared the line holding repeats at about 0.17. That is the standard dub gesture.

**Ruled by poll:** the condition runs on a **tail clock** that counts silent samples since the latest of: the input
stopping, the loop last held (g ≥ 1, current or target), or the declared tail last changing. A full declared tail is
always allowed after the last change, so rest can only come later than under the old text, and the declaration stays
exact by construction. SPEC §10 is amended to cite this decision.

**Also recorded (no behaviour change, spec text aligned):** the code's tail uses 6.03 dB for the returning term's
headroom (20·log₁₀ 2 = 6.0206, rounded up) and adds the read kernel's 4 samples to T_max. Both make the tail longer,
never shorter; §10 now states them. The tail is computed in double and saturates to `kInfiniteTail` past 4·10¹⁸
samples, because a feedback of 99.99999999999999 % (a normalised host's round trip) overflowed int64 and declared a
NEGATIVE tail on x86 (critic H3).

**Enforced:** checks "tail clock" (held → released; 90 → 10 %), "tail formula" (50 / 90 / 99 %, filters on and off:
the sample before rest is below −120 dBFS) and "tail near unity", with plants `tailClockFromSilence`,
`tailFormulaShort` and `tailInt32`.

Rejected: keeping §10 as written (lowering feedback could end a tail abruptly).

## D-020 — A load fades the old repeats under their old settings (RATIFIED by poll, 2026-10-20; amends SPEC §10)

**Finding (the M1 critic, H4).** At a load the core faded the old wet over 20 ms, but routing, filters and the wet
level switched instantly underneath that fade (measured: 17.9 times the steady per-sample slope on a mid/side →
stereo load with the output filter switched on); a second load mid-fade restarted the fade gain from about 0.5 back to
1; and the load snapped a Device gain ramp in flight (+22.8 dB on the dry path).

**Ruled by poll, read with the contract.** RS-6.7 says the new parameters land at the load's block boundary with no
ramp-in and the old audio ends by a crossfade out. So: **every patch value lands at the boundary** (`getParam` reads
the new values, and the dry path follows a new mix at once: a step, by RS-6.7's design, with spill-over horde's); **the
old repeats fade out over 20 ms under their OLD routing, filters, feedback and wet level**; then the lines and loop
state are cleared, the new settings take the wet, and the heads land. A second load during the fade lands its values
but never restarts the fade. Device keys, ramps included, are never touched by a load. A resting instance has nothing
sounding and adopts the patch at once. (The poll's wording was "old settings until the fade ends, then the new patch
lands"; RS-6.7's "at that boundary" moves the landing for everything except the fading old audio to the boundary. The
behaviour of the old audio is exactly what the poll chose.)

**Enforced:** checks "load: old repeats fade and end", "load: click-free", "load: no restart", "load: Device ramps
run on", with plants `loadClearsInstantly`, `loadNewSettingsOnOld`, `loadRestartsFade`, `loadSnapsDevice`.

Rejected: new settings applied to the fading old audio (the click above).

## D-021 — M4's gate is set at M2 close (RATIFIED by the human, 2026-10-20)

ROADMAP said M4's gate (the visualizers and Echo's own screens) would be "defined at Phase A close, with the human";
Phase A closed without it, which the plan map (PR #6) flagged. The human ruled: leave it until **M2 close** and carry
it as an open question (ROADMAP Q-M4). Nothing in 1.0 waits on it: the 1.0 bar's face-and-expanded-view MUST row is
met in M3 with horde's house-style UI if Echo's own screen is not ready (ROADMAP M4).

## D-022 — fp-contract: the header's pragma, and GCC's one caveat (lead's record, 2026-10-21)

The core scopes fp-contract off inside `echo/engine.h` instead of a CMake INTERFACE flag that would change horde's
own numerics (critic M7). Evidence from the probe builds (`core/tools/render_hash.cpp`, `./verify` core gate):
- **Apple clang 16 (dev Mac):** the reference and the pragma build render bit-identically; the pragma-less control
  differs. Fused ops: engine 0 / 0 / 118, caller 0 / 0 / 0. Clang applies the pragma per expression in the front end,
  so it survives inlining into any caller.
- **GCC 13, x86-64 with -mfma (CI):** the pragma build holds **0** fused ops in `echo::` functions (the pragma-less
  control: 153 in `process` alone), so the pragma holds for the engine's own functions. But **1** fused op appeared
  in the probe's `main`, most likely engine arithmetic inlined into a caller compiled without the pragma: GCC's
  `optimize` pragma travels with a function, not with its body once inlined.
- **Unexplained, recorded rather than explained away:** on GCC the pragma-less render is bit-identical to the
  reference despite those fused ops. The gate therefore proves GCC at the instruction level. ROADMAP T-007 carries it.

**Rule for consumers on GCC:** compile translation units that include `echo/engine.h` with `-ffp-contract=off`;
ISO `-std=c++20` already defaults to it, and only `-std=gnu++20` turns it on. On clang nothing is needed.

## D-023 — The M2 click gate: s fixed at 2.25, one threshold 2.5, zero backstop hits (RATIFIED by poll, 2026-10-21)

The ROADMAP M2 gate needs a pass criterion for D-009's click metric. The first version, which normalised by each render's
own measured top speed with thresholds 3 and 12, was rejected by the independent critic: a bare jump's backstop-made
4× speed normalised it down to 3.4, under the correct jump; the repitch sweep pinned both laws at the cap; and the
"controls" mostly did not fire (critic M2-H3). Measured after the redesign (`build-core/echo_tests --click-report`):
- **Normalise by the declared bound** s = 2.25, not the render's own maximum, so a speed fault cannot hide itself.
- **Every scenario must also hold the speed invariants:** zero backstop hits, zero floor or overrun reads, s in
  [0.25, 2.25]. A bare jump (which the backstop turns into a counted 4× glide) fails here.
- **Scenarios:** the four time-change modes under a 1 Hz full-range sweep (1 ms .. 12 s); both laws under a sweep they
  can follow (200-400 ms); the three tempo modes under a 1 Hz tempo sweep (60-180 bpm); and Q-A1's single moves.
- **One threshold, 2.5, for every mode.** Clean readings: max 1.91 (the tape law's cap kink on a 500 → 100 ms move at
  glide 0.05); jump's back-to-back 3 ms crossfades under the full sweep, 1.89. Faults: integer-rounded reads 3.5, a
  planted 0.02 click 4.9, a read held per block 120. The floor: a −40 dBFS single-sample click on the −6 dBFS probe.
- **The probe fades in (50 ms).** An abrupt sine writes a slope break into the line that heads later replay. It read 10.2
  and was mistaken for a design kink until it was traced (LIBRARY L0013).
- D-009's other two readings (M1 residual, M3 AR-whitened) are **not built** for M2: ROADMAP T-008.

Rejected: thresholds 3/12 on the render's own speed (the critic's finding); building M1/M3 before closing M2.

## D-024 — M2's engineering choices (lead's record, 2026-10-21)

Implementation choices inside ratified rules (D-006–D-008, D-016, §3, §7), recorded so they are not re-derived:
- **Tape history:** one point per 32 samples (§11: 1.10 MiB at 192 kHz), interpolated **quadratically** with a
  secant-refined inverse. Linear interpolation erred by about 50 cents of read speed at a 10 ms glide; quadratic
  measures ≤ 2 cents and ≤ 0.008 samples there, and 0.01 cent at 0.3 s (critic M2-M5). Before the first recorded point
  the history is a steady line, so starting the law is O(1). Once a glide snaps, the next tape move rebuilds a steady
  tape, which is exact (the keep-alive was dead code, critic L2).
- **Key moves keep time.change:** a tempo move never takes over a key move still gliding or crossfading; only a
  settled head hands a move to time.tempoMode (§3.8; critic M2-H4).
- **Follow:** a settled head moves toward the grid time by at most the 50-cent cap a sample, so it follows exactly
  and absorbs residuals (critic M2-M2). While a follow crossfade runs, its destination tracks the grid within the cap,
  so following resumes when it ends (critic M2-M1).
- **Modulation:** each line's depth is limited by its own heads (§7); and after the slew the offset is bounded so no
  read falls under the 3-sample floor (critic M2-M3). Floor reads are counted and must be zero.
- **Cost:** the modulation rate, the tape coefficient and ln T are cached, not recomputed every sample (critic L3).

## D-025 — The tail clock restarts while a head moves (RATIFIED by poll, 2026-10-21; amends SPEC §10)

The declared tail is built from the target times. A shortening glide (2900 → 300 ms at fb 0) still sounds repeats from
the old, longer time, and the tail clock rested it at 0.850 s with the output at full level (critic M2-H1). Ruled: the
tail clock also restarts while any head glides or crossfades. Rest can only come later than before, and the declaration
stays exact by construction. Guarded by "tail clock holds while a head moves" with the plant `tailClockIgnoresMotion`.
Rejected: building T_max from the live head times (a tighter tail, but a longer formula and a moving declaration).

## D-026 — mod.seed reseeds only on a change; a load restarts the modulation from its seed (RATIFIED by poll, 2026-10-21; amends SPEC §7, §10)

Two findings (critic M2-H2). First, re-sending an unchanged seed reseeded every time: a host re-sending stepped values
every 64 samples froze the drift into a narrow band. Second, a load never reseeded, so two instances that loaded the same
patch modulated 353 samples apart. Ruled: only a changed value reseeds, and a load restarts the modulation from the
patch's seed at phase 0, so a preset's drift is derived from its seed alone. Nothing audible steps, because the old
repeats have faded by then. Guarded by "mod.seed" with the plants `seedResendRestarts` and `loadKeepsModState`.
Rejected: keeping the modulation running through loads.

## D-027 — M4's scope, gate and timing (RATIFIED by poll, 2026-10-21; closes Q-M4)

Set at M2 close, as D-021 scheduled. By poll:
- **Scope:** D-011's impulse-response display (the stem echogram on a tempo grid and the per-generation spectrum
  strip) and Echo's own screen style. Rejected: the display only, with the screen past 1.0; adding live meters and a
  read-head scope.
- **Visualiser gate:** the drawn IR equals the engine's own offline render bit for bit (one renderer, D-011); the
  "snapshot" label shows exactly when the time is modulated, the ceiling engages or a move is in flight; rendering never
  touches the audio thread; the display redraws within 100 ms of a parameter change. Each is proven on a planted fault
  first. Rejected: the human's visual sign-off alone.
- **Screen gate:** the human's sign-off on Echo's design in horde's FX screens workshop, plus checkable rows:
  keyboard-reachable, every key display-named (ADR-203), legible in light and dark, no audio-thread work. Rejected:
  sign-off alone.
- **Timing: aim for 1.0**, built in parallel with M3 (the lead recommended after the cutoff; the human chose 1.0). M3
  keeps priority: nothing in M4 may put a 1.0-bar MUST row at risk before 2026-12-11. If M4 is not ready, Echo ships with
  the house-style UI (which meets the bar's screen row on its own) and M4 follows.
- **Coordination:** the offline renderer is a non-real-time clone of the engine (D-011). Its `core/` changes must not
  collide with M3's, so the M3 session is told, and M4's core work lands in its own files or after M3's batch that
  touches the same ones.

## D-028 — The M3 plan approved; the bank's size, the face and the CPU budget (RATIFIED by poll, 2026-10-21)

The M3 planning session wrote the plan into ROADMAP (M3's evidence map for the 21 MUST rows; queue T-010 – T-027;
batches B1–B5, closing by 2026-12-04) and polled the human. Each answer took the lead's recommendation; the
session said beforehand that it hoped for exactly that, so these rulings deserve a second look at each item's gate.
- **Q-M3P: the plan, approved as written.** Each item still asks its own human gates as it reaches them (the
  intake re-pin, spec revision 3, `./verify` edits, outward filings, goldens). Rejected: re-ordering first;
  rework.
- **Q-M3B: the factory bank is an init plus 7** (T-021): a slap, a synced dotted ping-pong, a dark dub loop near
  unity, a modulated chorus-like line, a mid/side wide, a tape-repitch throw, a past-unity edge patch. The final
  seven are picked by listening (T-023). Rejected: init + 5 (thin coverage of the range); init + 11 (more
  listening and goldens than the slack allows).
- **Q-M3F: Echo specifies the face; horde's workshop draws it** (T-022). Echo writes `docs/face.md` (face and
  expanded keys, groups, keyboard order) and an ADR-203 law check; horde's FX screens workshop renders the
  house-style UI and owns the GUI review. Rejected: Echo building a house-style face itself (more M3 work that
  would compete with M4's own screen, D-027).
- **Q-A3: closed on the bar's approved budget** (`intake/module-1.0-bar.md` appendix, "Budget approved",
  2026-10-04): ≤ 2 % of a min-spec core at defaults and ≤ 4 % at worst per FX module, with ADR-236's 4 % ceiling
  for the automated and morphing rows. Echo is reported against it and never gated (RS-8.3). Rejected: an
  Echo-specific figure.

## D-029 — T-019 builds the one offline IR renderer; M4 adopts it (RATIFIED by the human, 2026-10-21)

M4's gate 1 (D-027) says ONE renderer feeds the display, the tail oracle and D-010's mono-fold metric. M3's B408
report (T-019) needs that metric in B3, before M4 would exist. The M3 plan flagged the overlap rather than adjusting
either gate. The human ruled: **T-019 builds the offline impulse-response renderer in its own file,
`core/include/echo/ir_render.h`**, a non-real-time clone of the engine on the public API (D-011: deterministic,
never probed from a live line), and the stereo report measures through it. M4 adopts that file for the display,
so one renderer holds from M3 on. M4 keeps its own gate rows 2–4 (the snapshot label, off the audio thread, the
100 ms redraw) and whatever caching or worker thread they need. Rejected: T-019 measuring on a plain offline
instance and M4 re-pointing the metric later (two renderers until M4 closes).

## D-030 — The M4 plan approved; the renderer built early by M4 (amends D-029); WebAssembly; two glasses (RATIFIED by poll, 2026-10-21)

An M4 planning session wrote the plan into ROADMAP (M4: what it found, where things live, batches B4-1 – B4-5; queue
T-030 – T-035) and polled the human. Every answer took the lead's recommendation, so each deserves a second look at
its item's gate (the same caution D-028 records).
- **Q-M4P: the plan, approved as written.** Each item still asks its own human gates as it reaches them. Rejected:
  core only for now (the screen revisited after M3's B2); rework.
- **Q-M4R: M4 builds the one renderer early (amends D-029).** D-029, ruled in the M3 session the same day, gave
  `core/include/echo/ir_render.h` to M3's T-019 (B3, from 2026-11-09). This plan had M4 build it by 2026-11-06, and
  the two collided; the human ruled for the plan. M4 builds `ir_render.h` in B4-1, to T-019's own renderer criteria
  (bit-equal to a plain offline render; the plant that skips the output filter) plus M4's. T-019 then measures through
  it and adds only the stereo report. **M3 never waits:** if the file has not landed on main by 2026-11-09, T-019
  builds it itself, as D-029 said. Everything else in D-029 holds: one file, one renderer, from M3 on. Rejected:
  keeping D-029 as ruled (M4's service and display would start after ~11-20, compressing the screen against the
  cutoff).
- **Q-M4W: WebAssembly for the screen.** horde's screens are HTML and canvas with no C++ painter (horde's gui2;
  GUI 3 unbuilt). The page runs the same C++ (`ir_render.h`, `ir_view.h`) compiled to wasm in a Web Worker, so gate 1
  is proven in the page itself. This adds a toolchain dependency (LLVM's wasm target with lld and a wasi sysroot,
  locally and in CI), installed at B4-4 with the human told what is installed. In the product the renderer runs
  natively in horde's process. Rejected: dumped renders only (no dependency, but the screen is not live).
- **Q-M4G: audition two glasses.** horde's workshop already holds a horde-authored mock Echo glass, SONAR SCOPE (on
  trial, fed mock values). Echo brings both its own stem-echogram glass and SONAR SCOPE fed by the true IR, from one
  display model; the human picks in the workshop (horde's ADR-259 ruling 5). Rejected: either glass alone.
- **Q-M4T: separate test binaries.** M4's checks live in `core/tests/ir_tests.cpp`, built as `echo_ir_tests` and
  `echo_ir_plants`, so they stay clear of M3's edits to `core_tests.cpp`. The one-time `./verify` edit that runs them
  in the core gate is approved by this ruling. Rejected: folding them into `core_tests.cpp`.

## D-031 — The IR drawn is the patch's response as heard right after loading it (lead's record, 2026-10-21; for the human's review on B4-1's PR)

The critic of T-030 (an Opus subagent, fresh context) found that the first renderer sent the patch to a reset clone
by `setParam`. On that path a changed `mod.seed` blends from the current drift stream (D-026's knob path), so a drift
patch's drawing depended on what was rendered before. Two identical requests differed from sample 7460 on, and the
fresh renderer drew a curve no listener hears after loading the preset (critic H1). The critic asked which response
should be drawn: the one heard right after loading the patch, or the one reached by turning knobs.
- **Ruled by the lead, because criterion 1 already decides it:** the drawing must be a function of the request alone,
  or the cache (keyed by the request's hash) serves the wrong curve. Only the post-load response qualifies. D-026 makes
  a load restart the modulation from the patch's seed at phase 0. The knob-path curve depends on the history of seed
  changes, and no hash of the patch can name it. So the renderer stores every value and then resets the clone, which
  lands them all and restarts the modulation from the stored seed.
- **What the user sees:** the drawing equals what plays right after the preset loads. After live seed changes, the
  playing drift can differ from the drawing. Gate 2's snapshot label already says "modulated" whenever the time is
  modulated, so the drawing never claims to be the live curve then.
- **Guarded by:** `irSeedAfterReset` (the old order), caught by the load-path reference and by the reuse check.
- **Rejected:** drawing the knob-path curve (not a function of the patch, so it cannot be cached or reproduced).

## D-032 — B4-2's measured amendments: a head fixed in samples; columns; the spectrum strip's oracle (lead's record, 2026-10-21; for the human's review on B4-2's PR)

Building T-031 and T-032 turned up three choices that the plan's wording got wrong. Each is a narrowing of the plan's
wording, never of the gate (gate 4 stays 100 ms; gate 1 stays bit for bit), and is recorded here for review:
- **Frame 1 is the first 96 000 samples, not the first 2 s.** The renderer costs per sample. In the service, the
  worst of 20 requests on a slow patch (drift, mid/side, feedback 99 %, all filters) took **228 ms** for a 2 s head at
  192 kHz; the renderer alone takes 85 ms at best. A fixed sample budget costs the same at every rate: it measured a
  worst of 16–29 ms at 48 and 192 kHz on the dev Mac. That is 2 s at 48 kHz and 0.5 s at 192 kHz, and the service
  extends the same render by 0.5 s chunks after it. Rejected: a 2 s head (fails gate 4 at 192 kHz); a time-budgeted
  head (deterministic content, but how much frame 1 shows would then depend on the machine).
- **The echogram is drawn as columns, each the render's own peak sample.** A screen has hundreds of columns and a
  render millions of samples. A column carries one sample, the one of greatest magnitude in its range, by index and
  value, so every drawn stem is a render sample bit for bit and no peak is thinned (plant `viewDecimatesPeaks`).
  Rejected: means or resampling (they draw values the engine never produced).
- **The spectrum strip's oracle is the geometric step, not an analytic |F|.** The loop filter's response is private
  to `engine.h`; a test that re-derived it would be a second model of the engine, the thing D-011 rules out. With
  every filter off, each generation is flat and the next is exactly 20 log₁₀ g lower; with the loop filter on, the
  step from one generation to the next is the same at every n (|F|·g per pass, so the strip shows Fⁿ up to a
  constant). Both are checked within 0.1 dB from 100 Hz to 0.45 fs.
- **Open: the service's memory.** The cache holds 2 × slots × window doubles. With 8 slots and an 8 s window that is
  49 MiB at 48 kHz and 197 MiB at 192 kHz, plus the clone's 18.7 MiB. T-034 picks the window and the slot count.
  The lead's proposal: size the slots from a fixed budget (for example 64 MiB) rather than a fixed count. To be ruled
  with the screen.
- **Observed, not acted on:** the worker's renders ran about 2.7× slower than the same render alone. The likely cause
  is macOS scheduling the worker on an efficiency core. horde's host sets its own thread priorities, so the core
  requests none.

## D-033 — Frame 1 is 96 000 samples, ruled by poll; D-032 corrected; the strip's oracle strengthened (RATIFIED by poll, 2026-10-21; supersedes D-032's first and third points)

The B4-2 critic (an Opus subagent, fresh context) found two problems in D-032:
- **Its evidence for the head did not reproduce.** The critic measured a 2 s head at 192 kHz at 47–66 ms worst of
  20; the lead re-measured 64–77 ms. D-032's "228 ms" was a contended measurement, wrong as stated.
- **Its spectrum oracle was too weak** (critic H2). A constant step also passes a strip with the loop filter missing,
  or one generation off.

Polled twice:
- **First poll: "2 s at every rate"** (the lead recommended it on the corrected figures). Once built, every chunk is
  cancellable, the head's included, so a change mid-render waits at most one 0.1 s chunk (critic M5). The 2 s head
  then measured, worst of 20 at 192 kHz, **90–196 ms cold and 81–122 ms with a render in flight**, over four runs,
  with other sessions sharing the dev Mac. It fails gate 4. Asking macOS for interactive QoS on the worker did not
  clearly help, and was removed.
- **Second poll: "96 000 samples"**. It measured 15–28 ms worst at every rate. It is 2 s at 48 kHz and 0.5 s at
  192 kHz, and later frames extend it every chunk.

Rejected:
- 2 s capped at 192 000 samples (about 1–2× margin at 192 kHz);
- keeping 2 s and gating on the median (that weakens gate 4).

**The strip's oracle, strengthened (supersedes D-032's third point):**
- The reference is the engine's own first repeat, which carries the loop filter once (SPEC §2). Each generation step
  must equal p passes of the first repeat's shape plus g, within 0.1 dB. A control requires the 200 Hz low cut to
  show at 100 Hz.
- Every routing is covered, each channel at its own arrivals per SPEC §4:
  - ping-pong alternates, so a channel generation is two passes;
  - mid/side with a mono input is the M line;
  - mid/side with side content and unequal times is refused (critic H1).
- Plants: `viewGenWindowFromBase`, `viewGenIndexOffByOne`.

D-032's second point (columns) and its open memory question stand. Its memory figures are corrected to binary
units: 8 slots × 8 s is 46.9 MiB at 48 kHz and 187.5 MiB at 192 kHz (critic L1). `stop()` now frees them.

## D-034 — intake re-pinned to horde d34cd13: the rack-slot contract and the macros spec; what §12.5 and §12.6 mean for Echo (RATIFIED by poll, 2026-10-23; T-010)

horde's contract had moved past T-010's brief. Since Echo's pin (horde 4d2288f, 2026-10-19):
- `rack-slot-contract.md` gained **§12.5** (ADR-257: RS-5.8 fixed controls, RS-4.11 the note context, admission row
  A22, RS-5.3's modify-law base, A12's round trip with fixed controls) and **§12.6** (ADR-267: RS-4.12 declared
  actions, admission row A23). T-010 was written for §12.5 alone.
- `SPEC-MODULE-MACROS.md` gained ADR-251's **macro law**: a preset may carry `macroLaw: modify`, where bindings add
  contributions to a base (in octaves for a Hz key) instead of setting the value; a `log` curve is invalid under it.
- The bar, the I/O gain standard and the division list are unchanged (hashes compared; the `DIVS` block in horde's
  `docs/design/shape-lab-mod.html` is identical, only moved).

Polled:
- **Re-pin both files** at horde `d34cd13` (2026-10-23), byte-identical, rather than the contract alone. T-011's
  preset format and T-017's macros build on the macros spec; pinning only the contract would leave them on a
  superseded law. This widens T-010's scope by one file.
- **RS-4.12 (actions) is decided in T-011**, as RS-5.8 is: a delay may plausibly want one (a "clear repeats"), so
  it is not ruled out here.

What the re-pin means for Echo (the lead's record of the contract's text, no new choice):
- **RS-4.11, the note context: not declared.** Echo has no use for notes. **A22: not applicable.**
- **RS-5.8, fixed controls:** which, if any, ruled in T-011.
- **RS-4.12, actions:** which, if any, ruled in T-011. **A23** applies only if one is declared; then its check and
  its must-fail shim (an action applied at the block's start, not its offset) join T-018.
- **ADR-251's macro law:** which law the factory bank uses is T-011's; T-017's validator carries `macroLaw` exactly
  and refuses a `log` curve under `modify`.

Rejected: re-pinning the contract alone (as T-010 was briefed); declaring no actions now.

## D-035 — Spec revision 3: meters, state, presets and macros, the fixed row, the tail mapping, Clear (RATIFIED by poll, 2026-10-23; T-011; cites D-017, D-034)

T-011's seven rulings, put to the human as six polls (rulings 5 and 6 together) against horde's contract as
re-pinned by D-034. Written into `spec/SPEC.md` §16 (with pointers in §8, §10, §12) and `spec/params.json`
(`fixedControls`, `actions`). The 29 keys' ranges, defaults and classes are unchanged.

| # | ruling | chosen | rejected |
|---|---|---|---|
| 1 | meters | taps after Input Gain and after Output Gain; peak per block; the clip latch sets on \|x\| > 1.0 and holds until cleared | latch at \|x\| ≥ 1.0 (flags a clean full-scale sine) |
| 2 | state | text, `echo-state 1`, `key value` in manifest order, by name, at target, `[device]` block, ≤ 2 KiB refused before allocation, all or nothing; floats as the shortest decimal that reads back exactly (Bulwark's T-029 shape) | C99 hex floats (exact but unreadable) |
| 3 | presets and macros | `ModulePreset`; macros bind continuous keys only; the factory bank uses `macroLaw: modify` (Hz keys in octaves), so a macro offsets the preset and the fixed row instead of overriding them | continuous with `set`; any key with `set` |
| 4 | fixed controls | four: Feedback (`fb.amount`), Tone (`filt.loop.lpHz`), Mod Depth (`mod.depthMs`), Mix (`mix`), all always live | Time, Feedback, Tone, Mix (`time.ms` is dead while synced); none |
| 5 | the tail mapping | whole samples, rounded up; at or past 2³² − 1 samples, `kInfiniteTail` | clamping to 2³² − 2 (under-declares) |
| 6 | not declared | `noteContext` (A22 not applicable), `morphPosition` | — |
| 7 | actions | **declare Clear** (`clear`): at its sample offset the old repeats fade out over 20 ms under the current settings and end, as a load ends them (D-020), with no value landing; a performance event | none for 1.0 (the lead's recommendation, to protect the date while horde's action shape is PROPOSED) |

Consequences:
- **Ruling 7 went against the lead's recommendation.** Clear costs M3 one more `engine.h` item and a live A23 row,
  while horde's action delivery (contract §12.6, ADR-267) is PROPOSED and unbuilt in FOUNDATIONS, so Echo's local
  shim carries it. New queue item **T-037** (B2, after T-015, in sequence on `engine.h`); T-018 maps A23 to its
  check. If horde's shape changes before 1.0, the shim follows it; the behaviour (§16.7) does not.
- D-034's "A23 applies only if one is declared" now applies: A23 is live.
- The fixed row and Clear are new in `params.json`; neither is a parameter, so `check_manifest.py` and the lab's
  manifest check (which compare `params`) are unaffected. T-016's lock is amended to list them too: horde gives each
  fixed control a host parameter id by position, so a reordered row is a breaking change.
- §16.2 accepts §3.1's division aliases on load and writes the name back (T-015 already said so).

## D-036 — FOUNDATIONS' `fx_operator.h` vendored at `301834b`, pinned by `vendor_gate` (RATIFIED by the human, 2026-10-23; T-012)

The human's instruction: "Vendor fx_operator.h at FOUNDATIONS 301834b in Scape's shape and add vendor_gate to
./verify with its planted one-byte edit." It covers both human gates: the vendoring and the `./verify` edit.

- **What:** `core/third_party/foundations/include/foundations/fx_operator.h`, verbatim from FOUNDATIONS
  `301834b` (its `origin/main` on 2026-10-23), and only that file. A `MANIFEST` beside it records `source:`,
  `commit:` and the upstream and local sha256 (`d60b7eba…`, equal: no private sibling name occurs in it).
- **The same bytes Scape pins** at FOUNDATIONS `c8e4ccb`: the header has not changed since slice 3 (`1a5995e`).
  Two horde modules now build against one header, which is the comparison FOUNDATIONS asks for.
- **`vendor_gate`** (in `./verify fast`, after `intake_gate`): every listed file's sha256 equals its MANIFEST line,
  and no unlisted file sits beside it. Unlike Scape's gate, it **proves itself every run** on two planted copies in
  a temp dir: a one-byte edit at offset 2048 (with a `cmp` guard so the plant cannot go vacuous) and an unlisted
  header. Both must fail. Checked by hand as well: a one-byte edit of the real file turns the gate red.
- **The header is an unfrozen draft.** `process()` and `loadState()` are open there; Echo's shim carries both,
  marked DEGRADED (rule 2), and the brief `echo-002` asks about both. Nothing includes the header yet: T-013 (B2)
  wires the include path and the `static_assert`s.
- **Re-vendoring is a human gate:** a new commit, both hashes, and a decision citing this one.

Rejected: vendoring `engine_manifest.h` now (`echo-002`'s question 4 asks FOUNDATIONS first).

## D-037 — T-013: Echo conforms to FOUNDATIONS' FX-operator ABI where the vendored header declares it (lead's record, 2026-10-23; for the human's review on batch 2's PR; one ruling by poll)

**What T-013 built** (`engine.h`, `params.h`, `plants.h`, `core_tests.cpp`, `core/CMakeLists.txt`):
- `static_assert`s at the end of `engine.h` that `echo::Echo` models `FxOperatorLifecycle` and `FxOperatorParams`.
  They sit in the header, so every includer (horde included) fails at compile time on drift. Scape puts its asserts
  in a `.cpp`; the critic judged the header the better place.
- `Echo::properties()`: latency 0 exact, the tail through `tailDecl32` with `may_change`, `min_block` 1, and a load
  bound of the 20 ms fade at the rate. Every field is left undeclared before `prepare()`, so `validate()` refuses it
  (FOUNDATIONS #129).
- `echo::tailDecl32`: SPEC §16.5's saturation, so a tail at or past 2³² − 1 declares `foundations::kInfiniteTail`.
- `process()` is in place for `inL == outL`, `inR == outR`, proven bit-identical across routings and block sizes
  1, 64 and 4096. This settles **T-007's signature item**: the 4-pointer `process` accepts the in-place shape, so
  the swap is a rename, not a call-site change.
- The vendored include directory is on `echocore`'s INTERFACE, as Scape has it PUBLIC.

**Readings the lead made (for the human's review):**
- **OQ #33's two enums are mirrored in `params.h`** (`echo::oq33`), by name and order, `kUndeclared` included,
  because `engine_manifest.h` is not vendored (`echo-002` question 4). When it is vendored, the mirror becomes a
  using-declaration.
- **Device maps to `kContinuous` + `kExcluded`.** SPEC §9 rules only "device = excluded"; the kind is the lead's
  reading, true while every Device key is a scalar (the two I/O gains in dB). The check now requires each key's
  mapped kind to match its own `stepped` flag, so a future stepped Device key fails loudly (critic LOW-1). *Open for
  the human:* ratify `continuous` as Device's kind, or leave it to FOUNDATIONS.

**Ruled by poll, 2026-10-23:** `./verify`'s fast-math guard probe compiles `engine.h` by hand, so it gained
`-I core/third_party/foundations/include` on both of its compiler calls. Without it the probe could not compile,
and its "did NOT fire" lines meant nothing. The change restores the gate; it does not weaken it.

**Critic (Opus, fresh context): SHIP**, no HIGH. Fixed:
- **MED-1:** the load bound was checked only against its own formula. It is now measured against the real fade at
  four rates, with a new plant, `loadBoundShort`.
- **MED-2:** T-013's own tail case (99.99 %, 12 s, 192 kHz) now runs first and catches the wrap plant itself. A
  plain narrowing maps 2³² − 1 and INT64_MAX to `0xFFFFFFFF` by coincidence. The tail figure is about 3.3e11
  samples, not 1.9e14.
- **LOW-1** (above); **LOW-2:** the two `kInfiniteTail` names are documented where they meet; **LOW-3:** the
  in-place contract is stated on `process()`; **LOW-5:** the stale first line is fixed.

**Not acted on, reported:** LOW-4, a fleet item for horde. Echo and Scape each put a copy of
`foundations/fx_operator.h` on horde's include path. Today the sha256s match. If the two pins ever diverge, whichever
directory comes first wins in horde's build, and each module's own `vendor_gate` proves nothing there.

Plants added: `propsNoTail`, `tailWraps32`, `paramNoClass`, `processReadsAfterWrite`, `loadBoundShort`. The core
suite now has 56 checks guarding 80 distinct planted faults (M2's 75 plus these 5); `echo_plants` prints 87
"plant fires" lines, because some plants are guarded by more than one check.

## D-038 — T-014: the meter taps and the I/O gain standard's tests 1–7 (lead's record, 2026-10-23; for the human's review on batch 2's PR)

**What T-014 built** (`engine.h`, `plants.h`, `core_tests.cpp`), to SPEC §16.1 (D-035) and B435:
- **Two meter taps:**
  - the input tap reads directly after Input Gain, on the repaired sample;
  - the output tap reads directly after Output Gain, every path and the tail.
- **Each tap** holds the last block's peak |x| over both channels and a clip latch that sets on |x| > 1.0.
- **The latch is cleared only by `clearMeters()`,** never by `process()`, `reset()` or a load (RS-6.13).
- **Lock-free relaxed atomics,** with a `static_assert` that they are lock-free. They are published once per
  `process()` call, and `clearMeters()` may be called from another thread.
- **No audio path changed** in the clean build: the render hash is unchanged (`85f103b94528512e`, checked by the
  critic).
- **Tests 1–7 as checks.** Test 2, already in the suite at 48 and 96 kHz, now covers both trims at 44.1, 48 and
  96 kHz. It checks every ramp sample against the straight line, with the end landing on sample N − 1.

**Critic (Opus, fresh context): FIX-THEN-SHIP**, from mutants it built by hand that the suite passed. All fixed:
- **H1:** test 5 showed only that Input Gain is not applied at the output. Input Gain applied to the dry path only
  passed. Test 5 now requires the +12 dB render to equal a 0 dB instance fed the pre-scaled input, bit for bit. New
  plant: `inGainDryOnly`.
- **H2:** the meter checks could not see channel, sign, or the output tap's placement. A signed left-only input tap
  with a dry-only output tap passed. The checks now put the peak on the right channel and make it negative, and the
  output tap must read a wet tail after the input stops. New plant: `outTapDryOnly`.
- **M1:** "reads the repaired sample" had no check. One is added, with a `tapReadsRaw` plant. The first version was
  itself vacuous (an instance at rest never reaches the taps on a repaired zero), and the plant was caught by the
  wrong check. It now runs on a live signal and catches the plant itself.
- **LOW:** a `resetClearsLatch` plant for test 7's reset and load half, and the `clearMeters()` threading note.

**Stated limits** (the critic's LOWs, recorded rather than changed):
- **Test 6 is mostly by construction.** `doubleGain` is a test-side plant that would fire against any module; the
  content is that Echo declares both keys, Device class.
- **Test 4's corner flip lands inside the first load's fade,** so it exercises the second-load branch (D-020).
- **On an unprepared instance or n ≤ 0,** `process()` returns before publishing, so the last peak stays readable.

**Open for the human (the critic's question).** §16.1 says "the block's peak", which this builds. With small horde
blocks (down to n = 1) and a GUI polling at 30–60 Hz, the shown peak is close to one arbitrary block; the latch
still catches every over. The alternative is a peak held since the GUI last read it, which makes the reader a
writer: an atomic exchange to zero. That would be a spec change (§16.1, a protected path).
