# Echo — the module spec (Phase A)

**Status: RATIFIED 2026-10-20 by poll (revision 2; D-017). `spec/` is a protected path:** a change is a human
gate, made by a new decision that cites this one. Written by the Phase A lead session from the ratified Phase 0 decisions
(D-004–D-013), the Phase A rulings (D-015, D-016), the horde contract packet (`intake/`), an independent critic
review of revision 1 (folded in here), and measurements on the lab (`lab/echo-lab.html`). S-1 (§5.3) and the
defaults of the two keys added by the rulings were ratified with it (D-017). **Amended 2026-10-20 by D-018, D-019 and D-020**
(§10: rest waits for the wet to fall below −120 dBFS; the tail clock restarts at a hold or a tail change; a load fades
the old repeats under their old settings), and **2026-10-21 by D-025 and D-026** (§10: the tail clock also restarts
while a head moves; §7, §10: a seed reseeds only when it changes, and a load restarts the modulation from its seed); **2026-10-23 by D-040** (§16.7: during a Clear the heads keep moving and the wet follows the live mix).
**Revision 3, RATIFIED 2026-10-23 by poll (D-035):** what rack admission (M3) adds — the meter taps, the saved
state, module presets and macros, the fixed row, the tail declaration's mapping, the entries not declared, and one
action, Clear (§16). The 29 keys' ranges, defaults and classes are unchanged.
**Amended 2026-10-27 by D-044** (§3.2, §4.1: the mono-fold law per routing mode in ρ's own scale, gated on exact
impulses with noise reported, and the colouring flag raised in stereo mode only as a stated convention), from T-019's
measurements and its critic review.
**Revision 4, ratified 2026-10-29 by poll (D-048):** a 30th key, `time.scale` (§3.1, §3.4, §3.5, §9), appended so no
other key moves, and the fixed row is three: Time Scale, Feedback and Mix (§16.4). The other 29 keys' ranges, defaults
and classes are unchanged.

**What this is.** The behaviour Echo must have, stated so the C++ core (M1–M3) can be built and tested against it.
The machine-readable half is [`params.json`](params.json): every key, unit, range, default, morph class, rate
class and switch declaration (1.0 bar row 24, RS-2.1, A11). The lab implements both, and `./verify fast` checks the
lab against `params.json` key by key. The lab is a listening twin, never a parity target; where the lab and this
file disagree, this file wins and the lab has a bug.

**Units** (ADR-009). Times in s or ms, rates per second, frequencies in Hz, all converted at the current sample
rate. No per-sample constant appears anywhere in this spec.

---

## 1. Scope list (1.0 bar row 23)

**Ships in 1.0:**
- one delay line per channel, up to **12 s** each, with fractional reads (D-004);
- **free time** (ms) or **synced time** by horde's division names, with a **left and right offset** as a percentage
  of the base time (§3);
- **routing**: stereo, ping-pong, mid/side (§4);
- **feedback** to 120 %, with a ceiling on the returning term only and a DC blocker in the loop (§5);
- **filters at three positions** (input, loop, output), each with true bypass (§6);
- **time changes**: repitch with two laws (tape, exponential), crossfade, jump; and a per-patch policy for tempo
  changes (§3.5–3.8);
- **line modulation**: sine or seeded drift, with depth, rate, stereo spread and seed (§7);
- **mix** and horde's **I/O gain** pair with meter taps (§8);
- up to **8 macros**, resolved by horde (§12);
- the declarations horde reads: latency, tail, rest, load, memory, classes and switch methods (§9–11).

**Post-1.0, each by its own poll** (ROADMAP §Deferred): freeze / infinite hold, reverse, ducking, a smear amount
(diffusion is Scape's), a swarm-modulated line, a stereo width control, filter resonance, an anti-alias filter on
fast repitch reads.

**Not Echo's, by horde's boundary:** diffusion (Scape), graph morphing (Sluice). Echo makes discrete repeats.

**The visualiser** (M4) draws the true impulse response (D-011). The lab's echogram is a first sketch, not its
spec; M4's gate is set at Phase A close with the human (ROADMAP).

## 2. Signal flow

```
in L,R ─► io.inGain ─┬────────────────────────────────────────────── dry ──────────────┐
                     │                                                                  │
                     └► encode (§4) ─► input filter ─► (+) ─► line 0/1 ─► read (§3) ─►  │
                                                        ▲                     │        │
                                                        │              loop filter     │
                                                        │                     │        │
                         DC blocker ◄─ ceiling ◄─ × fb ◄┴─ feedback routing ◄─┤        │
                                                                              ▼        │
                                                              decode (§4) ─► output filter ─► mix ─► io.outGain ─► out
```

- The **wet** is the read after the loop filter, so the first repeat already carries the loop filter once, and
  generation *n* carries it *n* times (the per-generation darkening the visualiser draws, D-011).
- **Input Gain** sits before every path, the dry path and the feedback injection included (B435). **Output Gain**
  sits after everything, the tail included.
- Nothing limits the dry path (ADR-142): at mix 0 the output is the input exactly, at any level and feedback.

## 3. Time

### 3.1 Free and synced

| | free (`time.sync` = free) | synced (`time.sync` = sync) |
|---|---|---|
| base time | `time.ms`, 1 ms – 12 s | `time.div` beats × 60 / bpm |
| stored as | ms | the division's **name** (`intake/tempo-divisions.md`) |
| tempo | ignored | data, pushed by the host per block; never read from a clock |

The 22 division names are horde's list, slow to fast, `2/1` to `1/64`; aliases (`1/1D` for `3/2`, …) are accepted
on load and stored as the canonical name. bpm ≤ 1 falls back to 120; re-sending an unchanged tempo is a no-op.

**Time Scale** (`time.scale`, D-048) multiplies the base time: ×¼ to ×4 on a log law, default ×1, so the knob's centre
is the preset as saved. Free, it is the value itself and scales continuously. Synced, it is the **nearest octave**,
2^round(log2 scale), so the repeats stay on the grid: ×¼, ½, 1, 2 or 4 exactly (the snap boundaries are ×2^(k + ½),
0.354, 0.707, 1.414 and 2.828). A re-sent scale, or a synced one moving inside a single octave's band, moves no time.

### 3.2 Left and right offsets (ruled by poll, D-015)

Each side's time is `base × (1 + offset/100)`, with `time.offL` and `time.offR` in −50 … +50 %. A ratio, not
milliseconds, so a pattern survives a tempo change: +50 % turns 1/4 into 1/4D, −33.3 % into 1/4T, and a few percent
gives a Haas-style widening. Offsets apply in every routing; in mid/side they offset the M and S lines.

**Colouring zone (D-010; D-044).** In **stereo** mode, when the two resolved times differ by less than 40 ms and are
not equal, mono playback combs (a 1 ms offset puts the first ⅓-octave notch at 500 Hz, −9.4 dB at the defaults).
Echo's screen warns; the mono-fold report states it. The flag is exactly 0 < |tL − tR| < 40 ms, compared in whole
microseconds ("about 40 ms" in the literature, ruled a strict 40 ms). **40 ms is a warning convention** (the
precedence-effect range), not a boundary the fold metric shows: a stereo offset combs the fold at low frequency at
any size (−7.9 dB at 31 Hz at 50 ms; T-019). **Not in ping-pong or mid/side** (measured, T-019): ping-pong's taps
never coincide, so its fold stays flat at −3 dB under any offset; in mid/side the offsets move the M and S lines and
the fold is exactly the M line, a flat 0 dB.

### 3.3 Tempo

Tempo arrives once per block. Within a block it is **interpolated per sample** from the previous block's value to
this one's (D-008), so a ramp never zippers. (So block independence, A3, holds for a constant tempo; under a
changing tempo the host's per-block values are themselves the data, and different block splits are different
data.) **At rest (§10) the tempo lands without interpolation**, so a fresh instance, a reset at a transport jump
or a wake starts on the host's tempo rather than gliding from a stale or default one.

### 3.4 The 12 s line and its clamp

The line holds 12 s plus the modulation ceiling (20 ms) plus the kernel's margin (§11). Each time is the base
time × `time.scale` (§3.1) × (1 + offset); the scale comes **before** this clamp, never after it. Then:
- **synced** times longer than 12 s are **halved until the longer of the two fits** — both lines together, so the
  L/R ratio is kept and both stay on the grid (the screen marks them folded);
- a **free** time longer than 12 s is clamped to 12 s (only reachable through a positive offset);
- every time is clamped below at **1 ms**.

Example: `2/1` at 30 bpm is 16 s, which plays at 8 s; `2/1` at 40 bpm with a +50 % right offset is 12 s left and
18 s right, which play at 6 s and 9 s.

### 3.5 What a moving time does: `time.change`

A time moves when a knob, automation, a macro or a morph changes `time.ms`, `time.div`, `time.sync`, `time.scale` or
an offset (a **control move**), or when the host tempo changes a synced time (a **tempo move**, §3.8). For a control move,
`time.change` decides:

| mode | what the read head does |
|---|---|
| **repitch** | glides to the new time by the repitch law (`time.law`, §3.6); the repeats in the line bend in pitch |
| **crossfade** | a second head starts at the new time and the two are crossfaded, equal-gain, over `time.xfadeMs` |
| **jump** | the same, over **3 ms**: an instant retime with no glide, no comb and no click (Q-A5, ruled, D-016) |

**Crossfade** (D-007): equal-gain, never equal-power (correlated copies would sum to 1.41× mid-fade; RS-3.6).
**Retrigger rule: a queue, latest target wins** — a target that arrives mid-fade waits, and when the fade ends the
head fades to the most recent target (intermediate ones are dropped). Two heads at most, deterministic. A move of
less than half a sample starts no fade.

**Deferral.** Each move is **latched** to the `time.change`, `time.law` and `time.tempoMode` in force when it
starts. Flipping any of the three changes nothing at the flip; it applies from the next move (their declared
switch, §9).

### 3.6 Repitch (Q-A1, ruled: both laws ship, D-016)

Reading at a moving delay D(t) plays at speed s = 1 − dD/dt, a pitch ratio of s (prior-art §3.1).

**`time.law`** chooses the law; `time.glide` is its time constant:

| `time.law` | law | character, measured on the lab (0.5 ↔ 0.25 s, glide 0.3 s, 48 kHz) |
|---|---|---|
| **tape** (default) | varispeed: the tape speed lags its target with time constant `time.glide`; the heads stay put, and a repeat recorded at speed v(t−D) plays at v(t) | each repeat carries the speed history it was recorded under; peak +9.1 / −6.7 st; M2 0.72 |
| **exponential** | a one-pole on ln D with time constant `time.glide` | the rate steps at onset, then relaxes; reaches the cap; M2 4.1 (the onset kink) |

The other two candidates (asymmetric slew, critically damped 2nd order) stay in the lab as references only.

**Bounds.** Every law is held to **the cap**: |pitch| ≤ 12 st, so dD/dt ∈ [−1, +0.5]. The modulation adds at most
±0.25 to the read's speed (§7). Together, s ∈ [0.25, 2.25] **by construction**, inside the **hard invariant
(D-006): s ∈ [0.25, 4] for the sounding head at every sample**. The read never reverses and never runs faster than
4×; a clamp at the bound is kept as a backstop and its hits are counted (the lab's gate requires zero hits across
every law, move and modulation extreme, so the backstop never acts).

**Fallback (D-006's hybrid).** If a move would take longer than **3 s** at the cap's rate, the head crossfades
(§3.5) instead of gliding: a 12 s → 10 ms move crossfades rather than diving for 12 s.

**The tape law's real-time bound.** The tape model keeps a history of tape position; finding where the read sits
is a forward search that may advance at most 4 samples a sample (the speed bound), so its work per sample is
bounded. The history may be kept at control rate in the C++ (§11).

### 3.7 Jump (Q-A5, ruled, D-016)

D-007 ratified a jump mode whose test asserts that the click metric **flags** it — a bare jump of the read head.
horde cannot admit a bare jump (A11 as amended: no bare jump of signal-path state at a flip; no step of a morphed
continuous key). Measured on the lab (probe 431.7 Hz, 48 kHz): a bare jump reads **M2 ≈ 37** on a 0.5 → 0.25 s
move and ≈ 63 on a 1 Hz time sweep; a **3 ms equal-gain crossfade** reads 0.3 and 0.8. **Ruled: jump is a 3 ms
equal-gain crossfade.** The bare jump survives only as the click metric's planted must-fail control. This amends
D-007's jump line (D-016).

### 3.8 Tempo moves (Q-A2, ruled: a key, D-016)

Following a tempo ramp bends the repeats by 1 + D·BPM′/BPM (D-008). For a 1 %/s ramp that is +17 cents at
D = 1 s, +51 cents at 3 s and +1.65 st at 10 s. **`time.tempoMode`** lets each patch choose:

| `time.tempoMode` | policy (D-008's letter) | measured on the lab |
|---|---|---|
| **crossfade** | (A) crossfade to the new grid time on every tempo move | never bends; a ramp becomes a stream of crossfades (135 over 8 s at 1.25 %/s) |
| **glide** (default) | (B) glide through the repitch law, like a control move — **horde's tempo rule as written** ("re-resolved synced values glide through the parameters' own smoothers", `intake/tempo-divisions.md`) | follows a ramp within cents at 0.25 s; a 2× step at 3 s dives up to the cap |
| **follow** | (C) follow exactly while the move needs ≤ 50 cents, crossfade past it | follows ramps exactly; steps crossfade |

The policy owns a move caused by the tempo alone; a move caused by a key always follows `time.change`. The
crossfade and follow modes amend horde's rule for the patches that choose them; horde is told so in a notice (a
ROADMAP follow-up), not asked.

## 4. Routing (`route.mode`)

Line 0 and line 1, each with its own time (`offL` for line 0, `offR` for line 1):

| mode | into line 0 | into line 1 | feedback | wet L | wet R |
|---|---|---|---|---|---|
| **stereo** | L | R | 0 → 0, 1 → 1 | line 0 | line 1 |
| **ping-pong** | (L+R)/2 | — | 0 → 1, 1 → 0 | line 0 | line 1 |
| **mid/side** | M = (L+R)/2 | S = (L−R)/2 | 0 → 0, 1 → 1 | M + S | M − S |

- **Ping-pong** sums the input to mono into the left line, so the first repeat is on the left and the repeats
  alternate. An input only on the right still enters (at half amplitude).
- **Mid/side** uses M = (L+R)/2, so a mono input runs the M line at the same level as stereo mode does and meets the
  ceiling at the same input level. M and S never cross.
- **Switching** routing is a 20 ms equal-gain crossfade of the encode, feedback and decode matrices. The line is
  kept, so a switch into or out of mid/side re-images the ringing tail. Through the switch the loop matrix is a
  convex blend of two matrices of norm 1, so the loop bound (§5.2) holds.

### 4.1 Mono safety (B408) — Q-A4, ruled by poll (D-015)

The mono fold (L+R)/2 of each mode, stated:
- **stereo**: the fold of two independent lines; equal times fold without colouring. Offsets inside the colouring
  zone comb (§3.2).
- **ping-pong**: every repeat sits on one side, so each folds with **no cross term**: ρ = ½ (−3 dB, the fold of a
  hard-panned source) exactly, on any input. That is panning by design, not cancellation: no two taps overlap
  (D-010 P5, restated in ρ's scale by D-044; the "0 dB" first written there was the 1 + r scale).
- **mid/side**: **the fold is exactly the M line's output, by construction.** Repeats that live only in S vanish in
  mono, and that is **not** a B408 defect: the S line carries only the input's own side content, which the dry path
  also loses in mono. The S-only impulse row of D-010 reads 0 and is labelled *by construction*.

**The law (D-044).** ρ = E_M / mean(E_L, E_R), M = (L+R)/2: 1 for a centred source, ½ for a hard-panned or
decorrelated one, 0 for total cancellation. **Unintended cancellation** is the output's fold losing more than the
input's. Gated per repeat generation on equal-time patches (an L/R offset is a requested widening, reported, not
gated), on **exact inputs only**: the mono impulse (ρ_in = 1) and the left and right impulses (ρ_in = ½).
**Stereo and mid/side**: ρ_out ≥ ρ_in. **Ping-pong**: no cross term (ρ = ½); and where its repeats overlap
(sustained material) the fold at any whole multiple of 1/T equals (1 + g)² / (2 (1 + g²)) with the loop filter off,
so a side inverted cannot hide behind r = 0. **Seeded decorrelated noise is reported, never judged:** a finite
burst's ρ moves under the loop's filtering with no fault present by more than any independent-sample margin (the
filters shrink the effective sample count every pass; 15 of 80 seeds broke 3/(2√N), up to 0.051 below).

The B408 report (M3, T-019: `docs/stereo-report.md`, generated and gated) states, per mode, D-010's readings: ρ
broadband, per generation and per octave band, and the ⅓-octave-smoothed worst notch with its frequency.

## 5. Feedback

### 5.1 The loop

`fb.amount`, 0 – 120 %. Per line, each sample: the read (after the loop filter) is routed (§4), multiplied by the
feedback g, passed through the **ceiling**, then the **DC blocker**, and added to the line's input.

- **The ceiling acts on the returning term only** (ADR-142): identity for |x| ≤ 0.9, above that
  0.9 + 0.1·tanh((|x| − 0.9)/0.1) with the sign kept — continuous, slope 1 at the knee, never above 1.0. It never
  touches the dry path or the input. Loop gain is never divided by anything (ADR-142's trap).
- **DC blocker**, in the loop after the ceiling, corner **5 Hz** (pole R = e^(−2π·5/fs), from Hz at the current
  rate), in the **gain-normalised** form H(z) = ((1+R)/2)·(1 − z⁻¹)/(1 − R z⁻¹). *Finding:* the textbook form
  without (1+R)/2 peaks at 2/(1+R) > 1 at Nyquist, which would by itself breach the loop bound below.
- **Past unity** (g > 1) is a deliberate regime (D-005 as amended): the loop self-oscillates and parks. The bound
  is stated, not a measured figure: the returning term is at most the ceiling (1.0) times the DC blocker's L1 gain
  (1 + R < 2), so **once the input stops the line holds less than 2.0**, and while the input plays the line holds
  at most the input's peak + 2.0. Measured at 120 %, 150 ms: an impulse parks at 1.00, a 100 ms pulse at 1.36, a
  continuous ±0.9 square at 2.89 (≤ 0.9 + 2). The declared tail is then `kInfiniteTail`.

### 5.2 The loop bound (D-005 as amended)

**The filter path is bounded, not the knob:** peak |H_loop(e^jω)| ≤ 1 over the frequency grid at the current rate,
for every reachable setting. H_loop is the loop filter, the interpolation kernel (max |H| = 1, L0002), the DC
blocker and, during a switch, the equal-gain blends. So the loop gain is at most g. The planted control (M1) is a
loop shelf with gain above 1. No loop element has resonance; the loop filter is one-pole (§6).

### 5.3 The generation ratio — S-1, ruled (D-017)

D-000 Q4's oracle row says the generation ratio equals the feedback **exactly**, filters bypassed. D-005's DC
blocker is always in the loop, so it cannot be exact. Measured (48 kHz, 100 ms, g = 0.5, filters off): repeats land
on their samples exactly; generation 2 is g·(1+R)/2 = 0.499836 of generation 1 (the blocker's impulse gain); later
generations also carry the blocker's small negative tail from earlier passes, an error of order (1−R)·R^D,
3 × 10⁻⁵ relative at 100 ms and about 5 × 10⁻⁴ at 10 ms. **Ruled: the ratio is g within 10⁻³**, and generation 2
is g·(1+R)/2 exactly; D-005 stands. Rejected: a 1 Hz corner (five times smaller error, still inexact, slower DC
recovery); switching the blocker out while the ceiling is idle (exact, but a switched loop element).

## 6. Filters

Three positions, each a **low cut** and a **high cut** behind one on/off switch:

| position | where | slope | low cut | high cut | default |
|---|---|---|---|---|---|
| input (`filt.in.*`) | on what enters the line (not the dry) | 12 dB/oct, Butterworth | 20 – 2000 Hz | 0.5 – 20 kHz | off |
| loop (`filt.loop.*`) | after the read, inside the loop | 6 dB/oct, one-pole | 20 – 2000 Hz | 0.5 – 20 kHz | **on**, 60 Hz / 9 kHz |
| output (`filt.out.*`) | on the decoded wet | 12 dB/oct, Butterworth | 20 – 2000 Hz | 0.5 – 20 kHz | off |

- **True bypass** (ADR-142): a filter that is off and settled is **not computed** and its state is cleared, so the
  output is bit-identical to a module without it, whatever its cutoffs (tested, with an "open but on" plant).
- **Switching** on or off is a 20 ms equal-gain blend of the filtered and unfiltered signal; a filter switched on
  starts from cleared state. Both paths have |H| ≤ 1, so the blend does too.
- **Cutoffs** glide in log-frequency with a 10 ms time constant, coefficients computed at the current rate (TPT
  forms), clamped to 10 Hz … 0.45·fs. No filter has resonance in 1.0 (Butterworth Q = 0.707; one-pole), so none
  peaks above 1. In the loop the one-pole compounds per repeat.

## 7. Line modulation (`mod.*`)

A modulation offset is added to every read head of a line (both heads mid-crossfade): D_read = D + offset.
- `mod.depthMs` 0 – 20 ms (0 = off: the read is exactly D). `mod.rateHz` 0.05 – 10 Hz.
- `mod.shape`: **sine**, or **drift** — a Catmull-Rom curve through seeded random points in [−1, 1], one point per
  period, **clamped to [−1, 1]** (Catmull-Rom overshoots to 1.25, which would read past the line's 20 ms margin).
  Drift is the only randomness in Echo: two mulberry32 streams, derived from `mod.seed` alone.
- `mod.spread` 0 – 100 %: the right line's sine is phase-shifted by up to 90°; its drift is blended toward an
  independent stream.
- **Depth limits.** The depth is limited so the curve's own slope moves the read speed by at most ±25 %
  (A ≤ 0.25 / max|dm/dt|: 2πf for the sine, 3.5f for drift; at 10 Hz the sine's depth is at most 4 ms), and so the
  read stays 3 samples clear of the write head (A ≤ D − 3 samples).
- **The offset is slew-limited to ±0.25 samples a sample.** Rate glides, depth and spread ramps, a shape switch, a
  seed change, or a depth limit lifting when a short time moves to a long one can therefore never move the read
  faster than ±25 %: this is what makes the speed bound of §3.6 hold by construction. (Revision 1 relied on ramp
  lengths alone; the critic found four ways around them.)
- **Smoothing**: depth and spread ramp over 250 ms; a shape switch blends the two shapes' values over 250 ms; the
  rate glides in log-frequency (100 ms).
- **Seed change** is a declared **seeded jump** (contract §12.2): the new streams start from the value being
  output, so the modulation does not step; only its slope changes. Only a **changed** value reseeds: a host or morph
  re-sending the same seed changes nothing (D-026).
- **A load** restarts the modulation from the patch's `mod.seed` at phase 0, so a loaded preset's drift is derived from
  its seed alone whatever played before; the old repeats have faded by then, so nothing audible steps (D-026).
- **At rest** the phase and the drift hold, and the offset rests ON the curve they define, so a wake plays at the
  modulation's own speed rather than slewing in from 0.
- The modulated read is a snapshot for the visualiser (D-011).

## 8. Mix and I/O gain

- **`io.inGain`, `io.outGain`**: horde's standard exactly (B435, RS-4.6): dB −24 … +24, law 48·(v − ½), exactly unity
  at 0 dB, a 20 ms ramp linear in amplitude, opening at the saved gain, Device class (never in a preset, untouched by
  a load), meter taps with clip latches after each (§16.1). Echo owns them (the slot adds none).
- **`mix`** 0 – 100 %, **equal-power**: dry = cos(πm/2), wet = sin(πm/2), ramped over 20 ms. Mix 0 is exactly the dry
  signal; mix 100 % is exactly the wet (no cos(π/2) leak).

## 9. The parameter manifest and switch declarations

Authoritative: [`params.json`](params.json) (30 keys). Morph classes use the local enum mapped one to one onto
OQ #33 (RS-2.2): **continuous** = `continuous` + `morphs`; **jump** (flip) = `stepped` + `morphs`; **device** =
`excluded`. Rate class is FOUNDATIONS' `ReadCadence`: every key is `kContinuous`. Every key has a display name and
a normalised law (ADR-203): `lin`, `log` (geometric between min and max), `io` (B435), `step`.

| key | name | unit | range | default | class | law | smoothing / switch |
|---|---|---|---|---|---|---|---|
| `io.inGain` | Input Gain | dB | −24 … 24 | 0 | device | io | 20 ms amplitude ramp |
| `io.outGain` | Output Gain | dB | −24 … 24 | 0 | device | io | 20 ms amplitude ramp |
| `mix` | Mix | % | 0 … 100 | 30 | continuous | lin | 20 ms ramp |
| `time.sync` | Sync | free / sync | — | sync | flip | step | moves the time (below) |
| `time.ms` | Time | ms | 1 … 12000 | 375 | continuous | log | moves the time (§3.5) |
| `time.div` | Division | 22 names | 2/1 … 1/64 | 1/4 | flip | step | moves the time (below) |
| `time.offL` | Left Offset | % | −50 … 50 | 0 | continuous | lin | moves the time (§3.5) |
| `time.offR` | Right Offset | % | −50 … 50 | 0 | continuous | lin | moves the time (§3.5) |
| `time.change` | Time Change | repitch / crossfade / jump | — | repitch | flip | step | deferred |
| `time.law` | Repitch Law | tape / exponential | — | tape | flip | step | deferred |
| `time.tempoMode` | Tempo Follow | crossfade / glide / follow | — | glide | flip | step | deferred |
| `time.glide` | Glide | s | 0.01 … 5 | 0.3 | continuous | log | the law's time constant |
| `time.xfadeMs` | Crossfade | ms | 2 … 500 | 60 | continuous | log | latched per fade |
| `fb.amount` | Feedback | % | 0 … 120 | 35 | continuous | lin | 20 ms ramp |
| `route.mode` | Routing | stereo / pingpong / midside | — | stereo | flip | step | equal-gain crossfade, 20 ms |
| `filt.in.on` | Input Filter | off / on | — | off | flip | step | equal-gain crossfade, 20 ms |
| `filt.in.hpHz` | Input Low Cut | Hz | 20 … 2000 | 20 | continuous | log | 10 ms log glide |
| `filt.in.lpHz` | Input High Cut | Hz | 500 … 20000 | 20000 | continuous | log | 10 ms log glide |
| `filt.loop.on` | Loop Filter | off / on | — | on | flip | step | equal-gain crossfade, 20 ms |
| `filt.loop.hpHz` | Loop Low Cut | Hz | 20 … 2000 | 60 | continuous | log | 10 ms log glide |
| `filt.loop.lpHz` | Loop High Cut | Hz | 500 … 20000 | 9000 | continuous | log | 10 ms log glide |
| `filt.out.on` | Output Filter | off / on | — | off | flip | step | equal-gain crossfade, 20 ms |
| `filt.out.hpHz` | Output Low Cut | Hz | 20 … 2000 | 20 | continuous | log | 10 ms log glide |
| `filt.out.lpHz` | Output High Cut | Hz | 500 … 20000 | 20000 | continuous | log | 10 ms log glide |
| `mod.depthMs` | Mod Depth | ms | 0 … 20 | 0 | continuous | lin | 250 ms ramp (§7) |
| `mod.rateHz` | Mod Rate | Hz | 0.05 … 10 | 0.6 | continuous | log | 100 ms log glide |
| `mod.shape` | Mod Shape | sine / drift | — | sine | flip | step | seeded jump of the offset (below) |
| `mod.spread` | Mod Spread | % | 0 … 100 | 0 | continuous | lin | 250 ms ramp (§7) |
| `mod.seed` | Mod Seed | integer | 0 … 65535 | 1 | flip | step | seeded jump (§7) |
| `time.scale` | Time Scale | × | 0.25 … 4 | 1 | continuous | log | moves the time (§3.5); synced, snaps to octaves (§3.1) |

**Switch declarations, in A11's vocabulary as amended** (§12.1: an equal-gain crossfade of a stated length, or
`gate`; §12.2: a seeded jump — the state that starts running is seeded from the value being applied, so the value
does not step and only its slope changes; it needs a reported click reading and a planted unseeded-jump control):
- **`time.sync`, `time.div`** move the read head, by the active time-change mode: **repitch = a seeded jump of the
  read position** (the position is continuous; only its slope changes, within the cap); **crossfade = equal-gain,
  `time.xfadeMs`**; **jump = equal-gain, 3 ms**. A key whose method depends on another key's value is new to horde;
  Echo asks horde, in the readiness filing, whether a mode-dependent declaration is admissible (a ROADMAP
  follow-up). The planted bare jump is the unseeded control.
- **`time.change`, `time.law`, `time.tempoMode`: deferred** — no signal-path state switches at the flip (§3.5).
- **`route.mode`, `filt.*.on`: equal-gain crossfade, 20 ms.**
- **`mod.shape`, `mod.seed`: seeded jump of the modulation offset**, with a planted unseeded reseed as the control.

Keys freeze at the first release (B428). The defaults of `time.law` (tape) and `time.tempoMode` (glide: horde's
rule as written) were ratified with the spec (D-017).

## 10. Declarations, rest and load

| declaration | value |
|---|---|
| `latency()` | **0** samples. Nothing is buffered ahead. |
| `min_block` | **1**. Any block size 1 … 4096; parameters ramp per sample (A3; tempo, §3.3). |
| `channels()` | **2** in, 2 out (RS-2.8). |
| `tail()` | feedback g < 1: **(n + 2)·T_max + 0.25 s**, with n = ⌈(−120 dB − 6.03 dB) / (20·log₁₀ g)⌉ and T_max the longer resolved time plus the modulation depth plus the read kernel's 4 samples (g = 0: n = 0; D-019). **g ≥ 1: `kInfiniteTail`.** `may_change` = true. |
| `changes_image`, `changes_level` | both **true** (ping-pong and mid/side move the image; the wet adds level) (RS-2.4). |
| `LoadDecl` | **crossfade out, 20 ms** (below). |
| `morphPosition` | not declared (RS-4.10 is optional; Echo has no costly per-corner design). |
| `noteContext` | not declared (RS-4.11 is optional; Echo has no use for notes; §16.6). |
| `TailDecl` | the tail above in whole samples, rounded up; **saturates to `kInfiniteTail`** at or past 2³² − 1 (§16.5). |

**Why that tail.** After the input stops, one pass drains what the input wrote. After that the returning term is at
most the ceiling times the DC blocker's L1 gain (< 2, the +6 dB) and falls by g per pass (|H_loop| ≤ 1), so n
passes bring every write below −120 dBFS; one more pass is the gate's window; 0.25 s covers the filters and the DC
blocker settling.

**The tail gate and rest (A5, RS-7.3–7.6, A16 as amended).** When the input has been silent for a whole read span
and every line write **and every wet sample (after the loop and output filters; D-018)** in that span is below −120 dBFS — **or** when the declared tail has elapsed on the **tail clock** (D-019: it counts silent samples since the input stopped, the loop was last held at g ≥ 1, or the declared tail last changed, whichever is latest, and it restarts while any head still glides or crossfades,
D-025, because the tail is built from the target times and a shortening glide still sounds the old, longer time), since the input
went silent, whichever comes first — Echo clears its lines, filter states and DC state and is **at rest**. So the
output is exact zero at the end of the declared tail **by construction**, never as an estimate. At rest:
- the output is exact zero (silence in, silence out);
- every time-varying state holds: the modulation phase and drift, the tempo interpolation, the read heads;
- every parameter lands at its target with no ramp, the tempo lands on the host's value, and the heads land on the
  resolved times.

The first non-zero input sample wakes it. **Sleep** (RS-7.6): the declared tail is "the time until the instance is
at rest" (§12.1's reading). horde may stop calling an instance at rest; because rest holds everything that would
otherwise drift, an instance that slept and its never-slept twin are bit-identical from the wake on, with
parameters changed during sleep at their targets (A16's judge; the lab gate tests it with a planted modulation
that keeps running at rest). A held loop (g ≥ 1) never rests.

**Load (RS-6.7, ruled D-016, D-020).** A preset or state load lands every patch value with no ramp-in, at the load's block boundary (the dry path follows a new mix at once); **the old repeats fade out under their OLD routing, filters, feedback and wet level**, and a second load during the fade lands its values but never restarts the fade. **Device keys are
not part of a patch and are left alone**, a gain ramp in flight included (RS-4.5; B435 test 4). The old audio **fades out over 20 ms and ends**:
the lines and loop state are cleared and the heads land on the new times — never a glide (ADR-142). Whether the old
tail keeps ringing is horde's spill-over setting (a second instance, RS-6.3/6.5/6.13), never Echo's. (Input that
arrives during the 20 ms fade is lost from the line; the dry path is untouched.)

**Non-finite (RS-7.9, A9).** Input NaN/±Inf is replaced by 0, each repair counted and latched. A non-finite value
anywhere in the loop or at the output clears **all** loop state — lines, filters, DC — and is counted; zeroing one
sample would leave the poison in a filter's state.

## 11. Memory (1.0 bar MUST, RS-2.6)

All allocated at `prepare`, for the host rate (ADR-237). Lines: 2 × (⌈12.02 s × fs⌉ + 8) samples × 4 B (float32
storage; the loop state is double):

| rate | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| lines | 4.04 MiB | 4.40 MiB | 8.80 MiB | **17.61 MiB** |

(The manifest's 17.58 MiB is the bare 12 s; the 20 ms modulation margin adds 0.03 MiB. The drift clamp, §7, keeps
every read inside it.) **The tape law's position history**: one double per sample per line would be 35.2 MiB at
192 kHz, so the C++ keeps it at control rate (one value per 32 samples, interpolated: 1.1 MiB at 192 kHz) — a design
target, measured at M1 with the rest of the per-instance figure. **State size**: 30 keys, well inside the 2 KiB
budget per corner (RS-6.12).

## 12. Macros

Echo offers up to 8 macros per preset, ordered and labelled by the preset (ADR-169 A4). It does not resolve them
itself: horde resolves them through SPEC-MODULE-MACROS §5 (RS-5.5, second case). Nothing is driven by horde's
intents unless a preset binds it. What a preset holds, which keys a macro may bind and the factory bank's law:
§16.3. The fixed row beside the macros: §16.4.

## 13. Constants

| constant | value | where |
|---|---|---|
| maximum delay | 12 s per line | §3.4 |
| minimum delay | 1 ms; the read never closer than 3 samples to the write | §3.4, §7 |
| ceiling | knee 0.9, asymptote 1.0 | §5.1 |
| DC blocker | 5 Hz, gain-normalised | §5.1 |
| repitch cap | 12 st both directions | §3.6 |
| read speed | s ∈ [0.25, 4] (hard); [0.25, 2.25] by construction | §3.6 |
| repitch fallback | 3 s at the cap's rate | §3.6 |
| jump | 3 ms equal-gain crossfade | §3.7 |
| tempo follow cap | 50 cents | §3.8 |
| modulation | slope ±25 % of speed; offset slew ±0.25 sample/sample; drift clamped to [−1, 1] | §7 |
| ramps | 20 ms (gains, mix, feedback, routing and filter switches, load fade-out); 250 ms (modulation depth, spread, shape) | §4–10 |
| cutoff glide | 10 ms, log-frequency | §6 |
| tail gate | −120 dBFS; margin 0.25 s | §10 |
| crossfade queue | latest target wins | §3.5 |
| kernel | 4-point FIR (cubic Hermite in the lab; Hermite vs Lagrange left open by D-004, both in the lab) | D-004 |

## 14. Rulings and open items

| id | question | state |
|---|---|---|
| Q-A1 | the repitch law | **ruled** 2026-10-20 after a bench: tape and exponential both ship, as `time.law` (D-016) |
| Q-A2 | tempo moves | **ruled** 2026-10-20: (D), the `time.tempoMode` key (D-016) |
| Q-A5 | what "jump" means (raised by revision 1) | **ruled** 2026-10-20: a 3 ms crossfade (D-016) |
| load | what a load does to ringing repeats (raised by the critic) | **ruled** 2026-10-20: they end; spill-over is horde's (D-016) |
| Q-A4 | mid/side and B408 | **ruled** 2026-10-20: by construction (D-015) |
| B408 law | ping-pong's scale, the noise gate, the colouring flag (found by T-019 and its critic) | **ruled** 2026-10-27: ρ = ½ exact for ping-pong; exact impulses gated, noise reported; the flag stereo only, a convention (D-044) |
| offsets | L/R offset model | **ruled** 2026-10-20: % of base (D-015) |
| S-1 | the generation ratio against the DC blocker (§5.3) | **ruled** 2026-10-20: g within 10⁻³ (D-017) |
| defaults | `time.law` = tape, `time.tempoMode` = glide | **ruled** 2026-10-20 (D-017) |

**For horde (follow-ups, not blockers):** (1) a notice that two tempo modes amend the "glide through smoothers"
rule for the patches that choose them (§3.8); (2) the question whether a mode-dependent switch declaration is
admissible (§9); (3) the probe finding below, which applies to A11's pinned 1 kHz probe: every jump between
integer-millisecond times, and between 120-bpm divisions down to 1/16, is a whole number of 1 kHz cycles, so that
probe reads such jumps as clean and would under-report Echo's time-flip clicks.

## 15. The lab and the oracles

- The lab (`lab/echo-lab.html`) implements this spec, plus the unpicked candidates as lab-only references. Its
  smoke (`tools/lab_smoke.mjs`, in `./verify fast`, ~7 s) proves, each detector first proven on a planted fault:
  no clock and no `Math.random` in the engine (static and at run time); the lab's manifest, switch declarations
  included, equals `params.json`; two instances driven side by side each equal a solo render, and `reset()` from
  mid-ramp equals a fresh instance; silence gives exact silence in every routing and time-change mode at 120 %;
  the tail is exact zero from the declared tail on; the output stays finite under NaN/Inf at the input and in the
  loop state, every repair counted; the read speed stays in bounds with zero reverse reads, zero overruns and zero
  backstop hits across every law, move and modulation extreme; repeats land on their sample with the §5.3 ratio;
  synced time lands on the host tempo from the first block; settled-off filters are bit-identical bypasses; the dry
  path is untouched at mix 0 and absent at mix 100; the I/O gain law and ramp; routing and filter switches are
  click-free and the crossfade is equal-gain; deferred flips change nothing; the seed switch is seeded; a load ends
  the old repeats and leaves Device keys alone; mid/side folds to its M line; past unity the loop parks within its
  bound; the click reading separates the 3 ms jump from a bare one; a slept instance equals its twin.
- **Finding on D-009's M2**: M2 reads ≤ 1 for smooth speed changes, but a **speed kink** (a slew law's corners, a log
  one-pole's onset) reads a few (4.1 at 48 kHz on the 431.7 Hz probe), and a bare jump reads tens. M2 ranks clicks
  above kinks above smooth glides; its floor on a steady sine at s_max = 2 is 0.25. It is a measurement; no
  perceptual threshold is claimed (D-009).
- **Finding on probes**: a 440 Hz probe is blind to a 0.5 → 0.25 s jump (exactly 110 cycles, the same phase). Click
  probes must be incommensurate with the test times; the lab uses 431.7 Hz.
- **Finding on vacuous checks**: once Echo rests, a check driven by an impulse into silence can pass because nothing
  is running. The lab's speed checks are fed a constant non-zero input and require that the gate never fired.
- M1's C++ invariant suite tests this spec directly (ROADMAP M1); the lab is never its reference.

## 16. Rack admission additions (revision 3, D-035)

Ruled by poll 2026-10-23 (T-011), against horde's contract as re-pinned at `d34cd13` (D-034).

### 16.1 Meter taps (RS-4.6, RS-4.7; A6)

- **Two taps.** *Input*: right after `io.inGain`, before every path and detector. *Output*: right after
  `io.outGain`, after every path and the tail.
- **Each tap publishes, once per block:** the block's peak |x| over both channels' samples, and a **clip latch**.
- **The latch sets on a sample with |x| > 1.0** (over 0 dBFS; a sample at exactly 1.0 is full scale, not a clip) and
  **holds until horde clears it.**
- Publishing allocates nothing and takes no lock: the GUI reads published values, never working buffers (contract
  §8, RC-8). Non-finite input is repaired and counted by the guard (§10), separately; the input tap reads the
  repaired sample.
- Peaks and latches are **live state**: never saved (A20).

### 16.2 Saved state (RS-6.10 – 6.12; A17 – A20)

```
echo-state 1
mix 30
time.sync sync
time.ms 375
time.div 1/4
...
mod.seed 1
[device]
io.inGain 0
io.outGain 0
```

- **Text.** The first line is the version, `echo-state 1`. Then one `key value` per line, every patch key in
  manifest order (`params.json`), then a `[device]` line and the Device keys (`io.inGain`, `io.outGain`), saved once
  per instance, outside every corner (RS-4.5).
- **Values by name:** stepped keys with labels (`time.sync`, `time.div`, `time.change`, `time.law`,
  `time.tempoMode`, `route.mode`, `filt.*.on`, `mod.shape`) are written by label, divisions by horde's names (§3.1;
  an alias is accepted on load and written back as the name); `mod.seed` as a decimal integer.
- **Each value at its target**, never mid-ramp, so a state saved during a ramp, a ringing tail or a latched clip
  equals one saved at rest with the same parameters (A20).
- **Floats: the shortest decimal that reads back exactly**, proven by reading it back as it is written; never
  `%.6g` (RS-6.11). Save → load → save is byte-identical.
- **At most 2 KiB** (2048 bytes, the whole text). A larger text is **refused before anything is allocated** (A19).
- **Loading is all or nothing:** an unknown, missing, duplicated or out-of-range key, a label that is neither the key's
  label nor a division alias, or a different version refuses the whole text and changes nothing. Out of range is refused, never clamped.
- Nothing outside the saved state and `reset()` shapes the sound (A17): the 30 keys are the whole patch.

### 16.3 Module presets and macros (SPEC-MODULE-MACROS §4, ADR-251; RS-5.1 – 5.6)

- **Format:** SPEC-MODULE-MACROS §4's `ModulePreset`: `id`, `module: echo`, `internal_defaults` (every patch key, by
  name as in §16.2), `macroLaw`, and up to 8 `slots`, ordered, each with its `label`, its `bind` map of
  `key: [lo, hi]` or `[lo, hi, curve]`, and `home` ∈ [0, 1]. **No Device key** appears in a preset.
- **A macro may bind continuous keys only** (class `continuous`, never `device`). A stepped key through a macro would
  flip at an arbitrary threshold; the validator refuses a binding on one.
- **The factory bank uses `macroLaw: modify`** (ADR-251): each binding adds a contribution to the preset's own value
  (the base, a fixed control's value included, RS-5.3 as amended), clamped to the key's range. For a key in Hz (the
  six filter cutoffs, `mod.rateHz`) the contribution is in **octaves** (base · 2^Σc). Under `modify`, a `log` curve
  is refused. A preset from outside the bank may still say `set`.
- A binding on `time.ms` is inert while `time.sync` is on; that is the key's own behaviour (§3.1), not the macro's.

### 16.4 Fixed controls (RS-5.8)

Echo declares **three**, in this order (also `params.json` → `fixedControls`):

| k | label | key |
|---|---|---|
| 1 | Time | `time.scale` |
| 2 | Feedback | `fb.amount` |
| 3 | Mix | `mix` |

Each is always live, Time up to the 12 s line (§3.4): Time Scale scales the time whether free or synced
(continuously, or by octaves, §3.1), where `time.ms` does nothing while synced and `time.div` is stepped. A fixed control is an ordinary parameter: saved in the
state (§16.2), present in every preset, and in A12's round trip. Tone and Mod Depth are ordinary keys a preset's macros
may bind (D-048).

### 16.5 The tail declaration's mapping

FOUNDATIONS' `TailDecl` counts samples in 32 bits. Echo's tail (§10) is rounded up to whole samples at the current
rate; a tail **at or past 2³² − 1 samples** (6.2 h at 192 kHz, reachable at feedback just under unity) **declares
`kInfiniteTail`**. It never under-declares, and the gate still brings Echo to rest at exact zero.

### 16.6 Not declared

`noteContext` (RS-4.11; A22 not applicable) and `morphPosition` (RS-4.10).

### 16.7 Actions (RS-4.12; A23)

Echo declares **one action** (also `params.json` → `actions`):

| key | label | what it does |
|---|---|---|
| `clear` | Clear | ends the repeats |

- **Fired at its sample offset** in the per-block context, before `process`. From that sample the old repeats **fade
  out over 20 ms under the current settings and end**: the lines and loop state are cleared, exactly as a load ends
  them (§10, D-020), but **no value lands** and the heads stay where they are. The dry path is untouched; input that
  arrives during the fade is lost from the line. **Clarified 2026-10-23 by poll (D-040):** "the heads stay where they
  are" means they are not landed: they **keep moving exactly as without the Clear** (a glide continues, a move keeps
  its mode), never frozen for the fade as a load's are. "Under the current settings" means the **live** mix: the
  fading wet follows a mix ramp, where a load's fade keeps the old wet level.
- **A performance event:** not a host parameter, never automated, no history node; the saved state and the patch are
  unchanged (ADR-267 A1). Only runtime state changes.
- A second Clear, or a load, during the fade lands nothing new on the fade: it never restarts it (as D-020). A Clear
  at rest does nothing.
- **Deterministic:** the same actions at the same offsets give the same output; no action, the output is unchanged
  (A23's judge). The must-fail control: a shim that applies the action at the block's start instead of its offset.
- Until FOUNDATIONS rules the action entry, Echo's local shim carries it (contract §12.6, the ABI map).
