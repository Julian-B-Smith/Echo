# Echo — prior-art landscape (Phase 0)

**Dated 2026-10-19.** ROADMAP T-001, the Phase 0 gate. Written by the lead session from a swarm of four research
agents (Sonnet, read-only, web sources), then checked by the lead: the derived formulas below were recomputed, and one
agent claim was corrected (§1.3). Every source was accessed **2026-10-19**. The design consequences are proposed in
DECISIONS D-004 to D-011 (status PROPOSED); nothing here is spec.

**Clean room.** Published literature, public manuals, product pages and reviews only. No commercial delay was
reverse-engineered. Products are described by capability in generic words; no mode name, preset name or UI text is
recorded or proposed for reuse.

**How to read the evidence tags.** **[R]** the source text was read. **[A]** only an abstract, a bibliographic record or
a search summary was reached; treat the detail as unverified. **[D]** derived here from first principles; it is not a
citation, and it is checkable by a Layer-0 test. Several primary texts are paywalled books (Zölzer's *DAFX*, Pirkle,
Godsill & Rayner, Blauert, Katz), so the quantitative claims below lean on [R] web texts and [D] algebra rather than on
them. That gap is listed in §9.

---

## 1. Fractional-delay interpolation for a modulated line

### 1.1 The options

| Interpolator | Cost | What it does to the repeats | Under a moving delay time |
|---|---|---|---|
| Linear | 2 taps | Lowpass at fractional offsets: \|H\| = \|cos(ω/2)\| at a half-sample offset, no loss at integer offsets [1][D] | FIR, so no switching transient [1] |
| First-order allpass | 2 taps + state | Unity magnitude; the error is in phase, exact only at DC [2] | Recursive state: switching delays gives a transient; users report clicks under even subtle vibrato [3][4] |
| Thiran (higher-order allpass) | N + state | Maximally flat group delay [5] | Same recursive-state problem as the first-order allpass [1] |
| Lagrange 3rd order / cubic Hermite (Catmull-Rom) | 4 taps | DC gain 1, HF rolloff worst at a half-sample offset [6] | FIR, so no switching transient [1]; benchmarked for tape-like modulation [7] |
| Windowed sinc / oversampled read | many | Near-transparent; polynomial order alone cannot reach transparency without oversampling [8] | Needed only for large upward speeds (aliasing) [7] |

### 1.2 Linear interpolation breaks sample-rate independence in the loop

A delay line re-applies the interpolator's response on every pass, so the loss compounds per repeat (J. O. Smith: the
amplitude error of linear interpolation "can be highly audible" in feedback loops [1]). The loss at 20 kHz and a
half-sample offset, recomputed by the lead [D]:

| Host rate | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| Loss per pass at 20 kHz | −16.7 dB | −11.7 dB | −2.0 dB | −0.47 dB |

The same patch would darken its repeats at very different rates on different hosts, and the darkening would also depend
on whether the delay time happens to land on a whole sample. That collides with the §Domain invariant "SR and
block-size independence are TESTED". → D-004.

### 1.3 A correction to one agent claim: the L1 norm is not the loop gain

An agent reported that 4-point Lagrange and Catmull-Rom have a "peak gain up to 1.25", so a loop near unity might grow.
The lead checked both kernels over offsets 0…1 [D]:

- the **frequency response** never exceeds 1 (max \|H(e^jω)\| = 1.000, at offset 0), so these kernels do not threaten
  small-gain loop stability (§2.1);
- the **sum of absolute tap values** reaches 1.25 at a half-sample offset (taps −1/16, 9/16, 9/16, −1/16). That bounds
  the worst-case **sample peak**, not the loop gain. It matters to the ceiling's headroom and to a peak-based test, not
  to stability.

ADR-142 already recorded the sibling trap: "peak amplitude is NOT loop gain" (`intake/legacy/ADR-142-standard-delay.md`).
Captured as LIBRARY L0002.

### 1.4 Reading faster than real time aliases

A shortening delay reads faster than 1 sample per sample. This is decimation, and ordinary interpolation does not
suppress the content it folds back; the remedy is a kernel stretched by the speed-up factor, at O(K) cost [7]. Dattorro
notes that aliasing "is worst for large pitch change upward" and that allpass interpolation suits only sub-semitone
changes [9][A for Part 1]. The legacy Echoplex model handles the same problem with a variable-cutoff anti-alias filter
[10][A].

### 1.5 Buffer sizing and position precision

Zavalishin & Parker give sizing rules: the line needs the interpolator's half-width beyond the maximum delay, and one
more sample if the write happens before the read (the loop's hidden unit delay). They keep the read position in fixed
point, because a running float sum loses precision [7]. At 12 s and 192 kHz the position reaches 2.3 M samples, so a
float32 position has about 0.25-sample resolution there [D]. The position must be double or fixed point.

**Design implications.**
- *Reuse:* a 4-point FIR kernel (Hermite or Lagrange) on every read. Size the buffer as max delay + kernel half-width +
  write/read margin, with the read/write order stated. Keep the read position in double or fixed point.
- *Avoid:* allpass and Thiran on any read whose delay can move. Avoid plain linear in the loop.
- *Differentiate:* state the per-repeat HF loss as a measured figure (dB at 5, 10 and 20 kHz, offsets 0 and 0.5) that
  holds within tolerance from 44.1 to 192 kHz. If the kernel's loss still varies with SR, oversample the read or
  compensate. → D-004.

## 2. Feedback-path filters and stability

### 2.1 The criterion

A feedback comb is stable only while its loop gain stays below 1 [12]. With filters in the loop, the small-gain
condition is \|g · H_loop(e^jω)\| < 1 at **every** frequency [12][13]. Zames' theorem also covers time-varying and
nonlinear loops [14], which matters here because the time can be modulated. A long delay sweeps the loop phase through
0 many times across the band. Any band where \|gH\| > 1 therefore holds a comb peak that grows [D]. For a resonant filter,
a shelf boost or a Q-peaked highpass, the **peak** gain is what counts, not the passband gain [D]. The interpolator is a
filter in the loop too, and §1.3 shows the 4-point kernels stay ≤ 1.

### 2.2 Placement

- **The ceiling on the returning term only** (ADR-142's fix) keeps the dry path clean and bounds the energy whatever
  the filters do. A filter boost placed *after* the ceiling can exceed the bound again [D].
- **DC.** Any asymmetric nonlinearity in the loop makes DC, which then circulates with gain 1/(1−g) [D]. Smith calls a
  DC blocker indispensable in delay-line loops [15]. Put it inside the loop, after the ceiling, and derive its pole from
  a cutoff in Hz. Smith's R ≈ 0.995 is a 44.1 kHz figure, and the corner moves if it is kept as a constant [15][D].
- **Denormals.** Feedback paths and recursive filters are the main source. A decaying tail can stick in the denormal
  range [16]. The anti-denormal tricks that add DC or noise are cancelled by DC blockers and highpasses [16], so they
  conflict with the blocker above. FTZ/DAZ set inside `process`, with the host's flags restored, is the remedy; the
  contract already requires it (RS-7.8). Hosts do not reliably set or restore these flags [17]. One unconfirmed June
  2026 report describes vectorised multiplies producing denormals on Apple Silicon despite a scoped guard [18][A].
- **Analog models** (tape echo [10], bucket-brigade [19]) put pre/post lowpasses and nonlinearity in the loop. They are
  references for colour, not for structure.

**Design implications.**
- *Reuse:* make stability a **computed invariant**: the peak of \|g · H_loop\| over a frequency grid, at the current SR,
  stays below 1 for every reachable parameter combination, filters' resonance and the kernel included. The ceiling is
  the backstop, not the mechanism. The must-fail plant is a loop shelf with gain above 1.
- *Reuse:* a DC blocker inside the loop after the ceiling, with every coefficient derived from Hz at the current SR.
- *Avoid:* anti-denormal DC or noise injection, and trusting the host's FTZ state. → D-005.

## 3. Moving delay time: tape-style repitch, crossfade, jump

### 3.1 The law every repitch test asserts

Reading at a delay D(t) gives a pitch ratio of **1 − dD/dt**. Smith (time-varying reads, Doppler) [20][21], Dattorro
§4.4 [9] and Puckette [22] state this. Dattorro also notes that a triangle LFO gives piecewise-constant pitch, which
sounds unnatural [9].

### 3.2 Smoothing laws compared [D]

No published comparison was found. The lead re-derived each line below. The worked example is a move from D₀ to a
target T, where the read speed is s = 1 − dD/dt.

| Law | Pitch trajectory | Pathology |
|---|---|---|
| One-pole on D (τ) | Steps at onset (peak \|dD/dt\| = \|ΔD\|/τ), then decays | 12 s → 10 ms with τ = 100 ms gives s ≈ 121 (≈ +83 semitones, deep aliasing). Any **lengthening** move faster than 1 s/s gives s < 0: the head plays **backward** |
| Slew limit (rate r) | Constant 1 − r during the move; kinks at start and end | Bounded pitch costs time: at ±12 semitones, shortening 12 s takes 12 s and lengthening takes 24 s |
| Critically damped 2nd order (τ) | Starts at unity pitch; smooth bump peaking at \|ΔD\|/(eτ) at t = τ | Slower than a slew for the same peak pitch |
| Varispeed (tape speed v(t) with motor lag; heads fixed) | A repeat recorded at v(t−D) and played at v(t) has pitch v(t)/v(t−D) | Each echo carries the motor's speed history; the physically motivated choice [10][A] |

The 12 s line makes every law pathological at the extremes. A bounded pitch implies seconds of glide, and aliasing
needs a lowpass near fs/(2s) (§1.4). Zavalishin & Parker add that the pitch transition lasts exactly the new delay time,
and that the shifted signal then sits in the loop until it decays [7].

**Design implications.**
- *Avoid:* one-pole smoothing of D with a fixed τ over the whole range: the pitch step at onset and the reverse read on
  lengthening moves.
- *Reuse:* in repitch mode the read speed s stays in [s_min > 0, s_max] as an **invariant** (never backward). Past
  s_max the mode falls back to a crossfade (a hybrid).
- *Q-A1 lab candidates:* (a) asymmetric slew limit; (b) critically damped 2nd order with τ scaled by \|ΔD\| to cap the
  peak pitch; (c) one-pole in the log-delay domain; (d) varispeed with motor lag. Each is offered with and without the
  crossfade fallback. The test moves are 0.5 ↔ 0.25 s, 3 → 0.5 s and 12 s → 10 ms, on a sine, a drum loop, HF noise
  and a voice. The report covers peak pitch deviation, glide time, aliasing floor, the click reading and reverse-read
  count (must be 0). → D-006.

### 3.3 Crossfade between two read heads

- **Mechanism:** one write head, two read heads, gains crossfaded. It costs double during the fade, and the new head's
  interpolator is pre-warmed [23][24].
- **Colouration:** mid-fade the output is a comb a·z^−t₁ + (1−a)·z^−t₂, with notches every 1/\|t₂ − t₁\| and infinite
  depth at a = 0.5. No intermediate delay is synthesised [23].
- **The law:** equal-power suits uncorrelated signals and equal-gain (linear) suits correlated ones [25]. Two taps of
  one signal close together in time are correlated at low frequencies and decorrelated at high ones, so neither law is
  exact [D]. horde's contract already **mandates equal-gain for correlated crossfades inside a module** (RS-3.6,
  `intake/rack-slot-contract.md`).
- **Inside the loop [D; no source found]:** a fade inside the loop carries both copies. With equal-power gains,
  correlated copies sum to about 1.41× the loop gain at mid-fade, which can push past unity at high feedback. Equal-gain
  keeps the sum at 1 for D → 0.
- **Retrigger mid-fade:** with two heads, retargeting the still-audible old head clicks [26][A]. The literature offers
  no rule. The safe options are a queue (finish, then start) or a third head.
- **Length:** practitioners report about 10 ms as near-instant and click-free, and 10–30 ms as a typical edit fade
  [25][26][A].

**Design implications.** Equal-gain in crossfade mode, per RS-3.6, never equal-power inside the loop. A deterministic
retrigger rule written in the spec. A fade length in ms, converted at the current SR. Tests with feedback ≥ 0.95. Jump
mode is a documented artefact: its test asserts that the click metric **flags** it. → D-007.

### 3.4 Synced time on a tempo change (Q-A2)

No literature was found. Public manuals document the glide, crossfade and jump choices for time changes, but not what
the audible delay does when the *tempo* changes [27]. A third-party changelog describes crossfading as the default for
tempo automation [28][A]. The ramp case is derived here [D]: if D = k · 60/BPM and the tempo ramps smoothly, the repeat
pitch ratio is **1 + D · (dBPM/dt)/BPM**. A 1 %/s ramp gives +17 cents at D = 1 s, but +1.65 semitones at D = 10 s. For
short delays, continuous following is honest and nearly pitch-neutral. Long delays and tempo *steps* need a policy.
Tempo should be interpolated per sample within a block; block-rate steps zipper [23][29].

**Q-A2 lab candidates:** (A) always crossfade or jump to the new grid time; (B) follow continuously (glide); (C) follow
with a pitch cap and a crossfade past the cap; (D) let the user choose. Test material: a slow ramp, a 2× step and a
sawtooth automation, each at D = 0.25 s and 3 s, with and without feedback. → D-008.

## 4. Click metrics

- **horde's B265 metric** is the RMS of the second difference against its local median, per transition, on a sine probe
  (RS-4.4). The contract's own §12 suspends its 3 dB ceiling because the reading depends on the probe's frequency and
  the sample rate. A sine's second difference scales with (ω/fs)².
- **Restoration literature:** AR prediction-error detection [30][31][A]; second-difference detectors beat first-difference
  ones [A]. **No usable perceptual dB threshold was found** [A], so Echo does not claim one.
- **Echo's proposal [D]:**
  - **M1** — a reference residual against the analytic x(t − D(t)) for a sine input.
  - **M2** — max\|Δ²y\| / (A·(ω·s_max/fs)²) ≤ 1 + margin. A sine at read speed s has a bounded second difference, so this
    form is **probe-frequency- and SR-normalised by construction**, the property B265 lacks.
  - **M3** — the AR(16–32) whitened residual against its pre-transition σ.
- **Must-fail controls:** a planted single-sample impulse, swept down in level to report the detection floor in dBFS
  and located to ±2 samples; a naive integer-rounding engine; a block-rate-stepped D (zipper); and a clean fixed delay,
  which must pass with margin. The floor is the guarantee and the readings are measurements, kept separate.
- **Note for horde:** M2 may answer the contract's suspended RS-4.4 (a ceiling independent of probe and rate). That is
  an observation for a later filing, not a change to horde's metric. → D-009.

## 5. Ping-pong, mid/side and mono-fold safety

### 5.1 The algebra [D]

With M = (L+R)/2 and S = (L−R)/2, the mono fold (L+R)/2 **is M**. The fold energy ratio is
ρ = E[M²] / mean(E[L²], E[R²]) = E_M / (E_M + E_S) = (1 + r)/2, where r = 2·ΣLR / (ΣL² + ΣR²). So decorrelated equal
channels lose 3 dB, and r = −1 is total loss. The lead re-derived this.

### 5.2 Where each routing breaks

- **Equal-time ping-pong** is exactly mono-safe in broadband energy, because no two taps overlap [D].
- **L/R offsets under about 40 ms**, or cross-feedback with tL ≠ tR so that taps nearly coincide, comb in mono, with
  the first null at 1/(2d): d = 1 ms puts it at 500 Hz [D]. Same-sign taps colour; opposite-sign taps (negative
  feedback, an inverting mode) cancel the low end. The precedence-effect literature [32][33][A] puts the fusion/echo
  threshold anywhere from about 2 to 50 ms depending on the material. Treat "under about 40 ms" as a colouring zone,
  not as a constant. Practitioner sources confirm that short Haas offsets change or vanish in mono [34].
- **Mid/side:** repeats that live only in S vanish in mono **by construction**. With a mono or decorrelated input that
  is a defect; with S-only *content* the user asked for, it is intended. B408's "no unintended cancellation at width 0"
  (`intake/module-1.0-bar.md`) needs this line drawn in the spec (open question, §9).
- **A broadband ratio cannot see a comb between time-separated taps** (the cross-term is zero) [D]. The metric needs
  per-band and smoothed-notch views.
- No normative text defining the correlation meter was reachable (IEC 60268-18 is unconfirmed [A]). The formula is
  derived and unit-tested in Echo, not cited.

### 5.3 The proposed measurement [D]

Render offline to the declared tail. Report: ρ broadband; ρ per repeat generation; ρ per octave band, 31.5 Hz to
16 kHz; the 1/3-octave-smoothed fold minimum and the frequency of its worst notch. Stimuli: a mono impulse and seeded
decorrelated noise for the **unintended** test; an L-only impulse; an S-only impulse for the **by-construction** row.
Planted controls:
- **P1** hL = δ, hR = −δ must read total loss and FAIL.
- **P2** hL = hR = δ must PASS.
- **P3** a 1 ms equal-gain offset reads ρ ≈ 1, but the notch view finds about 500 Hz.
- **P4** S-only repeats in M/S read 0 and are labelled by construction.
- **P5** equal-time ping-pong with a mono input reads 0 dB per repeat.

The result is stated per routing mode: B408's "measured and stated".

**Design implications.** Reuse ρ as the headline (with r = 2ρ − 1 for correlation-minded readers). Never gate on
correlation alone, or on a raw narrowband minimum. Warn when \|tL − tR\| (or a small combination of the two) falls
inside the colouring zone. → D-010.

## 6. Impulse-response visualisation

- **A delay's response is a sparse comb.** Reverb metrics such as T60 and EDT (ISO 3382 fits to Schroeder's backward
  integral [35][36][A]) do not suit it as headline numbers: the energy decay curve is a staircase. The natural view is a
  **stem echogram** (time × dB, a lane per channel, on a tempo grid). Per-generation filter darkening is exactly F^n for
  the loop filter F, so a **per-generation spectrum strip** is cheap and exact [D]. The energy decay curve is secondary,
  for the tail readout.
- **Render, don't probe.** The response comes from a non-real-time clone of the engine, with an impulse in and the
  current parameters, deterministic, on a worker thread, and cached by a hash of the parameters [D]. Probing the live
  line would disturb it and be non-deterministic.
- **State the snapshot.** A modulated time is time-varying and the ceiling is nonlinear, so the plot is a snapshot at
  one modulation phase and one stated input level. A log sweep [37][A] is the tool if the nonlinear stage is ever
  measured.
- **One renderer feeds three consumers:** the display, the tail oracle (RS-7.3–7.6) and the mono-fold metric (§5). What
  the user sees is what is gated. A fold overlay (M-only taps against S-only taps) shows which repeats vanish in mono.
- **The market:** displays of taps, rings or spikes exist (§7). One product advertises an "impulse response" display
  with no detail [A]. None was found that draws the true post-filter, post-routing response, or the darkening per
  generation. → D-011.

## 7. The landscape: public feature sets

Twenty-five delays were surveyed: the stock delays of six DAWs, eleven commercial plugins, four hardware or Eurorack
units, and the delays of three open-source synths. The capabilities below are summarised generically; per-product notes
and URLs are in the cited sources [27][38]–[46]. Most facts come from product pages and summaries; only the Ableton and
Bitwig manual pages were read in full.

- **Table stakes:** ms and tempo sync (straight, dotted, triplet), feedback, a ping-pong option, dry/wet, some
  filtering of the repeats, separate L/R times, and an LFO or wobble on the time.
- **Common:** filters inside the loop [38][39][40][41]; ducking; a documented choice between a pitch glide and a
  crossfade when the time changes (two DAWs' stock delays [27][39] and one commercial plugin [42]); freeze or infinite
  hold; multi-tap.
- **Rare:**
  - mid/side delay (one DAW stock device [27], one commercial plugin [43], and a third single-source [A]);
  - a repeats display showing taps, rings or spikes [27][44][A];
  - all three time-change behaviours (glide, crossfade and jump) documented publicly: one DAW device [27].
- **Not found anywhere:**
  - a stated mono-fold measurement;
  - filters at all three positions (input, loop, output) with true bypass. Known placements are fixed and single: one
    stock delay filters only *before* the line [27], others only in the loop;
  - deterministic, SR-independent rendering as a stated property. One product advertises timing randomisation, the
    opposite direction [45][A].
- **Max time** ranges from about 2.5 s to 20 s across the field [27][42][46]. Echo's 12 s sits in the upper middle.

Absence comes from public summaries, not proof; a second pass reading full manuals would firm it up (§9).

**Openings for Echo** (differentiate, not invent): a true IR display computed from the engine; a stated, measured
mono-fold claim; mid/side as first-class routing; three filter positions with true bypass; tested time-change modes
against a click metric; determinism as a stated property. All but the first are already in Echo's scope, so the
landscape confirms the scope rather than changing it. The finding that changes the design is that the IR display is
the visible differentiator, which argues for D-011's one-renderer architecture. → D-011.

## 8. Name

Name: "Echo" is a placeholder until horde's B433 clearance; notes kept privately (D-014). The human's
ruling is DECISIONS D-012.

## 9. Gaps, unverified items and open questions

- **Not read:** Zölzer *DAFX*, Pirkle, Godsill & Rayner, Vaseghi & Rayner, Blauert, Katz, Haas (1949/1951/1972), Plenge
  1971, Schroeder 1965, Farina 2000 (abstract only), and Arnardottir/Abel/Smith 2008 (abstract only, paywalled).
  Dattorro Part 2 was read by one agent and was an unextractable scan for the other. Laakso et al. 1996 was checked for
  its scope only. The Thiran stability conditions under varying delay are unconfirmed.
- **Open for Phase A (proposed to the human through DECISIONS, not decided here):**
  - Does B408's "no unintended cancellation" exempt S-only content in mid/side mode, stated as by construction (D-010)?
  - What is the deterministic retrigger rule for a crossfade arriving mid-fade (D-007)?
  - Does any reachable loop filter setting (resonance, shelf) have a peak gain above 1, and if so, clamp it or rely on
    the ceiling (D-005)?
- **Energy growth under a modulated fractional read inside the loop** was not quantified anywhere in the literature
  found. It is Echo's to measure, with a planted worst case (M2).
- **Phase N re-scan** (ROADMAP): full manuals for the rare-feature products; legal and IP checks, kept privately (D-014).

---

## References (all accessed 2026-10-19)

1. J. O. Smith, "Delay-Line Interpolation" / "Linear Interpolation", *Physical Audio Signal Processing* (CCRMA online);
   and "Delay Line Interpolation", CMJ companion text. https://ccrma.stanford.edu/~jos/pasp/Delay_Line_Interpolation.html ;
   https://ccrma.stanford.edu/~jos/cmj96/Delay_Line_Interpolation.html [R]
2. J. O. Smith, "First-Order Allpass Interpolation", PASP. https://ccrma.stanford.edu/~jos/pasp/First_Order_Allpass_Interpolation.html [R]
3. V. Välimäki, T. I. Laakso, "Suppression of transients in variable recursive digital filters with a novel and efficient
   cancellation method", *IEEE Trans. Signal Process.* 46(12):3408–3414, 1998. [A]
4. VCV Rack community thread on allpass-interpolation artefacts, 2022-01-12..15.
   https://community.vcvrack.com/t/how-to-remove-artifacts-from-all-pass-interpolation-completely/16088 [R, anecdotal]
5. MathWorks, `thiran` documentation (citing Laakso & Välimäki). https://www.mathworks.com/help/control/ref/thiran.html [R];
   T. I. Laakso, V. Välimäki, M. Karjalainen, U. K. Laine, "Splitting the Unit Delay", *IEEE Signal Process. Mag.*
   13(1):30–60, 1996, doi:10.1109/79.482137 [A]
6. J. O. Smith, "Explicit Lagrange Coefficient Formulas", PASP. https://ccrma.stanford.edu/~jos/pasp/Explicit_Lagrange_Coefficient_Formulas.html [R]
7. V. Zavalishin, J. D. Parker, "Efficient emulation of tape-like delay modulation behavior", DAFx-18, Aveiro, 2018.
   https://www.dafx.de/paper-archive/2018/papers/DAFx2018_paper_9.pdf [R]
8. O. Niemitalo, "Polynomial Interpolators for High-Quality Resampling of Oversampled Audio", 2001 (self-published, not
   peer reviewed). https://yehar.com/blog/wp-content/uploads/2009/08/deip.pdf [R]
9. J. Dattorro, "Effect Design Part 2: Delay-Line Modulation and Chorus", *JAES* 45(10), 1997.
   https://ccrma.stanford.edu/~dattorro/EffectDesignPart2.pdf [R by one agent, §4–5]
10. S. Arnardottir, J. S. Abel, J. O. Smith III, "A Digital Model of the Echoplex Tape Delay", AES 125th Conv., paper
    7649, 2008. https://aes2.org/publications/elibrary-page/?id=14800 [A]
11. — moved to a private note, 2026-10-20 (D-014).
12. J. O. Smith, "Feedback Comb Filters" and "Lowpass Feedback Comb Filter", PASP.
    https://ccrma.stanford.edu/~jos/pasp/Feedback_Comb_Filters.html ; https://ccrma.stanford.edu/~jos/pasp/Lowpass_Feedback_Comb_Filter.html [R]
13. J. O. Smith, passivity of reflectances, PASP. https://ccrma.stanford.edu/~jos/pasp05/Reactive_Terminations.html [A]
14. G. Zames, "On the input-output stability of time-varying nonlinear feedback systems, Part I", *IEEE Trans. Autom.
    Control* 11(2):228–238, 1966. [A, citation]
15. J. O. Smith, "DC Blocker", *Introduction to Digital Filters*. https://ccrma.stanford.edu/~jos/filters/DC_Blocker.html [R]
16. L. de Soras, "Denormal numbers in floating point signal processing applications", 2002.
    https://musicdsp.readthedocs.io/en/latest/_downloads/d81fc9af8a9fa63332b248772fbb4a54/denormal.pdf [R]
17. JUCE forum, "State of the Art Denormal Prevention", 2016. https://forum.juce.com/t/state-of-the-art-denormal-prevention/16802 [R, forum]
18. JUCE forum, FloatVectorOperations denormals on Apple Silicon, 2026-06.
    https://forum.juce.com/t/heads-up-floatvectoroperations-multiply-might-produce-denormals-on-apple-silicon/68983 [R, single anecdote]
19. C. Raffel, J. O. Smith, "Practical Modeling of Bucket-Brigade Device Circuits", DAFx-10, Graz, 2010.
    https://dafx.de/paper-archive/details/JhVfAOFXD1lAtctkMTUODg [A]
20. J. O. Smith, "Time-Varying Delay Line Reads", PASP. https://ccrma.stanford.edu/~jos/pasp/Time_Varying_Delay_Line_Reads.html [R]
21. J. O. Smith, "Doppler Simulation with Delay Lines", PASP. https://ccrma.stanford.edu/~jos/pasp/Doppler_Simulation_Delay_Lines.html [R]
22. M. Puckette, *The Theory and Technique of Electronic Music*, World Scientific, 2007, §7.
    https://msp.ucsd.edu/techniques/latest/book-html/node115.html [R]
23. T. Carpentier, "Implementation of a continuously variable delay line by crossfading between several tap delays",
    SMC 2024 (IRCAM). https://smcnetwork.org/smc2024/papers/SMC2024_paper_id118.pdf [R]
24. J. O. Smith, "Large Delay Changes", PASP. https://www.dsprelated.com/freebooks/pasp/Large_Delay_Changes.html [R]
25. M. Fink, M. Holters, U. Zölzer, "Signal-Matched Power-Complementary Cross-Fading and Dry-Wet Mixing", DAFx-16,
    Brno, 2016. https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2017/10/Fink-Holters-Z%C3%B6lzer-2016-Signal-matched-power-complementary-cross-fading-and-dry-wet-mixing.pdf [R]
26. UCI computer-music notes on `tapout~` crossfades. https://sites.uci.edu/computermusic/tag/tapout [A]
27. Ableton, *Live 12 Manual*, Audio Effect Reference (Delay; Echo). https://www.ableton.com/en/live-manual/12/live-audio-effect-reference/ [R]
28. Rekkerd, multi-tap delay v1.0.2 update notes. https://rekkerd.org/frctl-updates-dlay-multi-tap-delay-effect-plugin-to-v1-0-2/ [A]
29. HISE docs, smoother node. https://docs.hise.dev/scriptnode/list/core/smoother.html [A]
30. S. J. Godsill, P. J. W. Rayner, *Digital Audio Restoration*, Springer, 1998, doi:10.1007/978-1-4471-1561-8. [A]
31. S. V. Vaseghi, P. J. W. Rayner, "Detection and suppression of impulsive noise in speech communication systems",
    *IEE Proc. I* 137(1):38–46, 1990. [A]
32. H. Haas, "The influence of a single echo on the audibility of speech", *JAES* 20(2), 1972 (translation).
    https://aes2.org/publications/elibrary-page/?id=2093 [A]
33. J. Blauert, *Spatial Hearing*, rev. ed., MIT Press, 1997 (via citing papers, e.g. https://pmc.ncbi.nlm.nih.gov/articles/PMC4565656). [A]
34. Sound On Sound, "Q. Can Haas delays be mono-compatible?". https://www.soundonsound.com/sound-advice/q-can-haas-delays-be-mono-compatible [R, practitioner]
35. M. R. Schroeder, "New Method of Measuring Reverberation Time", *JASA* 37(3):409–412, 1965 [A]; J. O. Smith, "Energy
    Decay Curve", PASP. https://ccrma.stanford.edu/~jos/pasp04/Energy_Decay_Curve.html [R]
36. ISO 3382-1 (via pyrato documentation). https://pyrato.readthedocs.io/en/latest/_modules/pyrato/edc.html [A]
37. A. Farina, "Simultaneous Measurement of Impulse Response and Distortion with a Swept-Sine Technique", AES 108th
    Conv., preprint 5093, 2000. https://aes2.org/publications/elibrary-page/?id=10211 [A]
38. ValhallaDSP, "ValhallaDelay: the controls", 2019-04-16. https://valhalladsp.com/2019/04/16/valhalladelay-the-controls/ [A]
39. Bitwig, *User Guide*, Delay devices. https://www.bitwig.com/userguide/latest/delay [R]
40. Apple, *Logic Pro* user guide, Tape Delay. https://support.apple.com/guide/logicpro-ipad/lpipf03e8133/ipados [A]
41. Reason Studios, The Echo product page. https://www.reasonstudios.com/devices/the-echo [A]
42. FabFilter, *Timeless 3* help, delay controls and display. https://www.fabfilter.com/help/timeless/using/delaycontrols ;
    https://www.fabfilter.com/help/timeless/using/delaydisplay [A]
43. D16 Group Sigmund (MusicRadar news; KVR listing). https://musicradar.com/news/tech/d16-group-releases-sigmund-delay-unit-582526 [A]
44. Blue Cat Audio, Late Replies release note, 2017-08-24.
    https://bluecataudio.com/PressReleases/NewsItem_2017_08_24.Blue_Cat_Audio_Releases_Blue_Cat's_Late_Replies_Creative_Delay_Plug-In [A]
45. MusicTech, Baby Audio Comeback Kid. https://musictech.com/products/baby-audio-comeback-kid-delay-plugin [A]
46. Cockos, ReaPlugs (ReaDelay). https://www.reaper.fm/reaplugs/ [A]
47. — moved to a private note, 2026-10-20 (D-014).
48. — moved to a private note, 2026-10-20 (D-014).
49. — moved to a private note, 2026-10-20 (D-014).
50. — moved to a private note, 2026-10-20 (D-014).
51. — moved to a private note, 2026-10-20 (D-014).
52. — moved to a private note, 2026-10-20 (D-014).
53. — moved to a private note, 2026-10-20 (D-014).
