# Echo — the module spec (Phase A)

**Status: DRAFT 2026-10-20, awaiting the human's ratification** (ROADMAP Phase A gate). Once ratified, `spec/`
is a protected path. Written by the Phase A lead session from the ratified Phase 0 decisions (D-004–D-013), the
horde contract packet (`intake/`), and measurements on the lab (`lab/echo-lab.html`). Items still open are listed
in §14 and marked **OPEN** where they occur.

**What this is.** The behaviour Echo must have, stated so the C++ core (M1–M3) can be built and tested against it.
The machine-readable half is [`params.json`](params.json): every key, unit, range, default, morph class and rate
class (1.0 bar row 24, RS-2.1). The lab implements both and `./verify fast` checks the lab against `params.json`
key by key. The lab is a listening twin, never a parity target; where the lab and this file disagree, this file
wins and the lab has a bug.

**Units** (ADR-009). Times in s or ms, rates per second, frequencies in Hz, all converted at the current sample
rate. No per-sample constant appears anywhere in this spec.

---

## 1. Scope list (1.0 bar row 23)

**Ships in 1.0:**
- one stereo delay line per channel, up to **12 s** each, with fractional reads (D-004);
- **free time** (ms) or **synced time** by horde's division names, with a **left and right offset** as a percentage
  of the base time (§3);
- **routing**: stereo, ping-pong, mid/side (§4);
- **feedback** to 120 %, with a ceiling on the returning term only and a DC blocker in the loop (§5);
- **filters at three positions** (input, loop, output), each with true bypass (§6);
- **time-change behaviours**: repitch, crossfade, jump (§3.5–3.7);
- **line modulation**: sine or seeded drift, with depth, rate, stereo spread and seed (§7);
- **mix** and horde's **I/O gain** pair with meter taps (§8);
- up to **8 macros**, resolved by horde (§12);
- the declarations horde reads: latency, tail, memory, classes, switch methods (§10–11).

**Post-1.0, each by its own poll** (ROADMAP §Deferred): freeze / infinite hold, reverse, ducking, a smear amount
(diffusion is Scape's), a swarm-modulated line, a stereo width control, filter resonance, an anti-alias filter on
fast repitch reads.

**Not Echo's, by horde's boundary:** diffusion (Scape), graph morphing (Sluice). Echo makes discrete repeats.

**The visualiser** (M4) draws the true impulse response (D-011). The lab's echogram is a first sketch of it, not
its spec; M4's gate is set at Phase A close with the human (ROADMAP).

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
- Nothing limits the dry path (ADR-142).

## 3. Time

### 3.1 Free and synced

| | free (`time.sync` = free) | synced (`time.sync` = sync) |
|---|---|---|
| base time | `time.ms`, 1 ms – 12 s | `time.div` beats × 60 / bpm |
| stored as | ms | the division's **name** (`intake/tempo-divisions.md`) |
| tempo | ignored | data, pushed by the host per block; never read from a clock |

The 22 division names are horde's list, slow to fast, `2/1` to `1/64`; aliases (`1/1D` for `3/2`, …) are accepted
on load and stored as the canonical name. bpm ≤ 1 falls back to 120; re-sending an unchanged tempo is a no-op.

### 3.2 Left and right offsets (ruled by poll 2026-10-20)

Each side's time is `base × (1 + offset/100)`, with `time.offL` and `time.offR` in −50 … +50 %. A ratio, not
milliseconds, so a pattern survives a tempo change: +50 % turns 1/4 into 1/4D, −33.3 % into 1/4T, and a few percent
gives a Haas-style widening. Offsets apply in every routing; in mid/side they offset the M and S lines.

**Colouring zone (D-010).** When the two resolved times differ by less than about 40 ms (and are not equal), mono
playback combs. Echo's screen warns; the mono-fold report states it.

### 3.3 Tempo

Tempo arrives once per block. Within a block it is **interpolated per sample** from the previous block's value to
this one's (D-008), so a ramp never zippers. A tempo change re-resolves the synced times, and the move is handled
by the tempo policy (§3.8).

### 3.4 The 12 s line and its clamp

The line holds 12 s plus the modulation ceiling (20 ms) plus the kernel's margin (§11). After the offset:
- a **synced** time longer than 12 s is **halved until it fits** (it stays on the grid; the screen marks it folded);
- a **free** time longer than 12 s is clamped to 12 s (only reachable through a positive offset);
- every time is clamped below at **1 ms**.

Example: `2/1` at 30 bpm is 16 s, which plays at 8 s; `2/1` at 40 bpm with +50 % is 18 s, which plays at 9 s.

### 3.5 What a moving time does: `time.change`

A time moves when a knob, automation, a macro or a morph changes `time.ms`, `time.div`, `time.sync` or an offset
(a **control move**), or when the host tempo changes a synced time (a **tempo move**, §3.8). For a control move,
`time.change` decides:

| mode | what the read head does |
|---|---|
| **repitch** | glides to the new time by the repitch law (§3.6); the repeats in the line bend in pitch |
| **crossfade** | a second head starts at the new time and the two are crossfaded, equal-gain, over `time.xfadeMs` |
| **jump** | **OPEN (Q-A5)** — see §3.7 |

**Crossfade** (D-007): equal-gain, never equal-power (correlated copies would sum to 1.41× the loop gain mid-fade;
RS-3.6). **Retrigger rule: a queue, latest target wins** — a target that arrives mid-fade waits, and when the fade
ends the head fades to the most recent target (intermediate ones are dropped). Two heads at most, deterministic.
A move of less than half a sample starts no fade.

**Load and reset.** A preset or state load never glides the read head (ADR-142): parameters land at their loaded
values with no ramp-in (RS-6.7), and the head **crossfades** to the new time over `time.xfadeMs`. The line is kept,
so the old repeats ring on at the new time (LoadDecl: in place, crossfade of the head). `reset()` clears the lines
and snaps every head and smoother to its target; it is bit-identical to a fresh instance (A2).

**Switching `time.change`** takes effect at the next move; a glide in flight when the mode becomes crossfade is
finished by a crossfade.

### 3.6 Repitch

Reading at a moving delay D(t) plays at speed s = 1 − dD/dt, a pitch ratio of s (prior-art §3.1). Every repitch
law is held to two bounds:

1. **The law's cap**: |pitch| ≤ the cap, both directions (the lab default 12 st), so dD/dt ∈ [−(2^(cap/12) − 1),
   1 − 2^(−cap/12)].
2. **The hard invariant (D-006)**: s ∈ [**0.25, 4**] (±24 st) for the sounding head at every sample, modulation
   included. The read never reverses (zero reverse reads, tested) and never runs faster than 4×.

**Fallback (D-006's hybrid).** If the move would take longer than **3 s** at the cap's rate, the head crossfades
(§3.5) instead of gliding: a 12 s → 10 ms move crossfades rather than diving for 12 s.

**The law: OPEN (Q-A1), decided by ear in the lab.** The candidates (D-006):

| | law | measured on the lab, 0.5 ↔ 0.25 s, cap 12 st, glide 0.3 s, 48 kHz |
|---|---|---|
| (a) | asymmetric slew: the head moves at the cap's rate until it arrives | pitch ±12 st flat-topped; **kinks** at start and end; M2 4.1 |
| (b) | critically damped 2nd order, time scale max(glide, \|ΔD\|/(e·rate)) latched per retarget | starts and ends at unity pitch; peak +4.6 / −6.3 st; M2 0.43 |
| (c) | one-pole on ln D, time constant glide | steps in rate at onset; reaches the cap; M2 4.1 |
| (d) | varispeed: tape speed lags its target (time constant glide), heads fixed; a repeat recorded at v(t−D) plays at v(t) | each repeat carries the speed history; peak +9.1 / −6.7 st; M2 0.72 |

`time.glide` is the law's time scale. Its meaning is fixed with the law (Q-A1).

### 3.7 Jump — **OPEN (Q-A5), raised by this spec**

D-007 ratified a jump mode whose test asserts that the click metric **flags** it, which makes it a bare jump of the
read head. Drafting the classes found that horde cannot admit a bare jump:
- **contract A11 / §12.1**: a flip-class key that changes the time (`time.div`, `time.sync`) must switch by a
  declared crossfade, a `gate`, or a seeded jump; "a bare jump of a signal-path or detector state is not admitted";
- **A11's morph clause**: a morph-class key (`time.ms`, the offsets) must not step under a morph sweep. In jump
  mode a swept time steps at every control tick.

Measured on the lab (probe 431.7 Hz, 48 kHz): a bare jump reads **M2 ≈ 37** on a 0.5 → 0.25 s move and ≈ 7 on
division flips; a **3 ms equal-gain crossfade** reads **0.3**. The proposal: **jump = a 3 ms crossfade** (no
glide, no audible comb, no click), admissible as a declared crossfade, with the bare jump kept only as the click
metric's planted must-fail control (where D-009 already wants one). The alternatives are to keep the bare jump and
exclude jump mode from morph (horde would have to refuse it at admission), or to drop jump as a user mode.

### 3.8 Tempo moves — **OPEN (Q-A2), decided by ear in the lab**

Following a tempo ramp bends the repeats by 1 + D·BPM′/BPM (D-008): +17 cents at D = 1 s for a 1 %/s ramp, a
semitone and more at 3 s. The candidates (D-008), all measured in the lab's Q-A2 panel:

| | policy | measured |
|---|---|---|
| (A) | crossfade to the new grid time on every tempo move | a ramp becomes a stream of crossfades (135 over 8 s at 1.25 %/s) |
| (B) | glide through the repitch law | follows a ramp within cents at 0.25 s; dives ±12 st on a 2× step at 3 s |
| (C) | follow exactly while the move needs ≤ 50 cents, crossfade past it | follows ramps exactly; steps crossfade |
| (D) | a key, `time.tempoMode`, letting the patch choose | adds one stepped key |

The policy owns a move caused by the tempo alone; a move caused by a key always follows `time.change`.

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
  convex blend of two matrices of norm 1, so the loop gain bound (§5.2) holds.

### 4.1 Mono safety (B408) — Q-A4, ruled by poll 2026-10-20

The mono fold (L+R)/2 of each mode, stated:
- **stereo**: the fold of two independent lines; equal times fold without colouring. Offsets inside the colouring
  zone comb (§3.2).
- **ping-pong**: equal-time ping-pong folds to exactly 0 dB per repeat (no two taps overlap; D-010 P5).
- **mid/side**: **the fold is exactly the M line's output, by construction.** Repeats that live only in S vanish in
  mono, and that is **not** a B408 defect: the S line carries only the input's own side content, which the dry path
  also loses in mono. **Unintended cancellation** is defined as the output's fold losing more than the input's fold:
  ρ_out per repeat generation ≥ ρ_in, measured on a mono input (ρ = 1 every repeat) and on seeded decorrelated noise
  (ρ_out = ρ_in). The S-only impulse row of D-010 reads 0 and is labelled *by construction*.

The B408 report (M3) states, per mode, D-010's readings: ρ broadband, per generation and per octave band, and the
⅓-octave-smoothed worst notch with its frequency.

## 5. Feedback

### 5.1 The loop

`fb.amount`, 0 – 120 %. Per line, each sample: the read (after the loop filter) is routed (§4), multiplied by the
feedback g, passed through the **ceiling**, then the **DC blocker**, and added to the line's input.

- **The ceiling acts on the returning term only** (ADR-142): identity for |x| ≤ 0.9, above that
  0.9 + 0.1·tanh((|x| − 0.9)/0.1) with the sign kept, so it is continuous with slope 1 at the knee and never exceeds
  1.0. It never touches the dry path or the input. Loop gain is never divided by anything (ADR-142's trap).
- **Past unity** (g > 1) is a deliberate regime (D-005 as amended, D-013): the loop self-oscillates and parks at the
  ceiling. Measured on the lab (120 %, 50 ms, filters off): the read settles to a peak of about 1.25 and stays there,
  finite and not growing — the ceiling's 1.0 plus the DC blocker's overshoot on the clipped waveform it passes. The
  declared tail is then `kInfiniteTail`.
- **DC blocker**, in the loop after the ceiling, corner **5 Hz** (pole R = e^(−2π·5/fs), from Hz at the current
  rate). It is the **gain-normalised** form H(z) = ((1+R)/2)·(1 − z⁻¹)/(1 − R z⁻¹). *Finding:* the textbook form
  without (1+R)/2 peaks at 2/(1+R) > 1 at Nyquist, which would by itself breach the loop bound below.

### 5.2 The loop bound (D-005 as amended)

**The filter path is bounded, not the knob:** peak |H_loop(e^jω)| ≤ 1 over the frequency grid at the current rate,
for every reachable setting. H_loop is the loop filter, the interpolation kernel (max |H| = 1, L0002), the DC
blocker and, during a switch, the equal-gain blends. So the loop gain is at most g. The planted control (M1) is a
loop shelf with gain above 1. No loop element has resonance; the loop filter is one-pole (§6).

### 5.3 The generation ratio — **OPEN for ratification (S-1)**

D-000 Q4's oracle row says the generation ratio equals the feedback **exactly**, filters bypassed. D-005's DC
blocker is always in the loop, so it cannot be exact. Measured (48 kHz, 100 ms, g = 0.5, filters off): repeats land
on their samples exactly; generation 2 is g·(1+R)/2 = 0.499836 of generation 1 (the blocker's impulse gain); later
generations also carry the blocker's small negative tail from earlier passes, an error of order (1−R)·R^D, 3 × 10⁻⁵
relative at 100 ms and about 5 × 10⁻⁴ at 10 ms. The proposal: **state the ratio as g within 10⁻³** (the lab's gate)
and generation 2 as g·(1+R)/2 exactly, keeping D-005. Alternatives: lower the corner to 1 Hz (five times smaller
error, slower DC recovery), or switch the blocker out while the ceiling is idle (exact, but a switched loop element).

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
  peaks above 1. In the loop the one-pole compounds per repeat: generation *n* carries the loop filter *n* times.

## 7. Line modulation (`mod.*`)

A modulation offset is added to every read head of a line (both heads mid-crossfade): D_read = D + A·m(t).
- `mod.depthMs` 0 – 20 ms (0 = off: the read is exactly D). `mod.rateHz` 0.05 – 10 Hz.
- `mod.shape`: **sine**, or **drift** — a Catmull-Rom curve through seeded random points in [−1, 1], one point per
  period. Drift is the only randomness in Echo: two mulberry32 streams, derived from `mod.seed` alone.
- `mod.spread` 0 – 100 %: the right line's sine is phase-shifted by up to 90°; its drift is blended toward an
  independent stream.
- **Slope cap**: the depth is limited so the modulation alone moves the read speed by at most ±25 %
  (A ≤ 0.25/max|dm/dt|; 2πf for the sine, 3.5f for drift). At 10 Hz the sine's depth is at most 4 ms. Together with
  the repitch cap, the hard invariant (§3.6) still holds; where the two would add past it, the hard bound wins.
- **Seed change** is a declared **seeded jump** (contract §12.2): the new streams start from the value being
  output, so the modulation does not step; only its slope changes.
- **Smoothing is slower here than elsewhere, by arithmetic.** Depth and spread ramp over **250 ms**, a shape switch
  blends the two shapes' *values* over 250 ms, and the rate glides in log-frequency (100 ms). A 20 ms ramp of a
  20 ms depth would move the read at up to 2× speed; over 250 ms the change adds at most 8 % to the speed.
- The modulated read is a snapshot for the visualiser (D-011).

## 8. Mix and I/O gain

- **`io.inGain`, `io.outGain`**: horde's standard exactly (B435, RS-4.6): dB −24 … +24, law 48·(v − ½), exactly unity
  at 0 dB, a 20 ms ramp linear in amplitude, opening at the saved gain, Device class, meter taps with clip latches
  after each. Echo owns them (the slot adds none).
- **`mix`** 0 – 100 %, **equal-power**: dry = cos(πm/2), wet = sin(πm/2), ramped over 20 ms. Mix 0 is exactly the dry
  signal; mix 100 % is exactly the wet. (Dry and wet are decorrelated except at the shortest times, so equal-power
  keeps the loudness steady across the knob.)

## 9. The parameter manifest

Authoritative: [`params.json`](params.json) (27 keys). Morph classes use the local enum mapped one to one onto OQ #33
(RS-2.2): **continuous** = `continuous` + `morphs`; **jump** (flip) = `stepped` + `morphs`; **device** = `excluded`.
Rate class is FOUNDATIONS' `ReadCadence`: every key is `kContinuous`. Every key has a display name and a normalised
law (ADR-203): `lin`, `log` (geometric between min and max), `io` (B435), `step`.

| key | name | unit | range | default | class | law | switch / smoothing |
|---|---|---|---|---|---|---|---|
| `io.inGain` | Input Gain | dB | −24 … 24 | 0 | device | io | 20 ms amplitude ramp |
| `io.outGain` | Output Gain | dB | −24 … 24 | 0 | device | io | 20 ms amplitude ramp |
| `mix` | Mix | % | 0 … 100 | 30 | continuous | lin | 20 ms ramp |
| `time.sync` | Sync | free / sync | — | sync | flip | step | moves the time (§3.5) |
| `time.ms` | Time | ms | 1 … 12000 | 375 | continuous | log | moves the time (§3.5) |
| `time.div` | Division | 22 names | 2/1 … 1/64 | 1/4 | flip | step | moves the time (§3.5) |
| `time.offL` | Left Offset | % | −50 … 50 | 0 | continuous | lin | moves the time (§3.5) |
| `time.offR` | Right Offset | % | −50 … 50 | 0 | continuous | lin | moves the time (§3.5) |
| `time.change` | Time Change | repitch / crossfade / jump | — | repitch | flip | step | takes effect at the next move |
| `time.glide` | Glide | s | 0.01 … 5 | 0.3 | continuous | log | the repitch law's time scale (Q-A1) |
| `time.xfadeMs` | Crossfade | ms | 2 … 500 | 60 | continuous | log | latched per fade |
| `fb.amount` | Feedback | % | 0 … 120 | 35 | continuous | lin | 20 ms ramp |
| `route.mode` | Routing | stereo / pingpong / midside | — | stereo | flip | step | 20 ms equal-gain matrix crossfade |
| `filt.in.on` | Input Filter | off / on | — | off | flip | step | 20 ms equal-gain blend |
| `filt.in.hpHz` | Input Low Cut | Hz | 20 … 2000 | 20 | continuous | log | 10 ms log glide |
| `filt.in.lpHz` | Input High Cut | Hz | 500 … 20000 | 20000 | continuous | log | 10 ms log glide |
| `filt.loop.on` | Loop Filter | off / on | — | on | flip | step | 20 ms equal-gain blend |
| `filt.loop.hpHz` | Loop Low Cut | Hz | 20 … 2000 | 60 | continuous | log | 10 ms log glide |
| `filt.loop.lpHz` | Loop High Cut | Hz | 500 … 20000 | 9000 | continuous | log | 10 ms log glide |
| `filt.out.on` | Output Filter | off / on | — | off | flip | step | 20 ms equal-gain blend |
| `filt.out.hpHz` | Output Low Cut | Hz | 20 … 2000 | 20 | continuous | log | 10 ms log glide |
| `filt.out.lpHz` | Output High Cut | Hz | 500 … 20000 | 20000 | continuous | log | 10 ms log glide |
| `mod.depthMs` | Mod Depth | ms | 0 … 20 | 0 | continuous | lin | 250 ms ramp (§7) |
| `mod.rateHz` | Mod Rate | Hz | 0.05 … 10 | 0.6 | continuous | log | 100 ms log glide, continuous phase |
| `mod.shape` | Mod Shape | sine / drift | — | sine | flip | step | 250 ms blend of the modulation values (§7) |
| `mod.spread` | Mod Spread | % | 0 … 100 | 0 | continuous | lin | 250 ms ramp (§7) |
| `mod.seed` | Mod Seed | integer | 0 … 65535 | 1 | flip | step | seeded jump (§7) |

**Switch declarations for horde (A11 as amended).** Every flip-class key declares its method above. The
time-moving flips (`time.sync`, `time.div`) switch by the active time-change mode: a bounded glide (repitch), an
equal-gain crossfade, or jump (**Q-A5**). `time.change` itself moves nothing. If Q-A2 rules (D), one stepped key
`time.tempoMode` is added. Keys freeze at the first release (B428).

## 10. Declarations

| declaration | value |
|---|---|
| `latency()` | **0** samples. Nothing is buffered ahead. |
| `min_block` | **1**. Any block size 1 … 4096; parameters and tempo ramp per sample (A3). |
| `channels()` | **2** in, 2 out (RS-2.8). |
| `tail()` | feedback g < 1: T_max × ⌈−120 dB / (20·log₁₀ g)⌉ + 50 ms, where T_max is the longer resolved time plus the modulation depth; g = 0: T_max + 50 ms; **g ≥ 1: `kInfiniteTail`**. `may_change` = true. |
| tail end | the output reaches **exact zero** when the declared tail ends: past it, a deterministic tail gate clears the lines once the input has been silent and the line's peak is below −120 dBFS for a full line length (M1). FTZ is set inside `process`, the host's flags saved and restored (RS-7.8). |
| `changes_image`, `changes_level` | both **true** (ping-pong and mid/side move the image; the wet adds level) (RS-2.4). |
| `LoadDecl` | in place: the line is kept, the head crossfades (§3.5). |
| `morphPosition` | not declared (RS-4.10 is optional; Echo has no costly per-corner design). |
| non-finite | input NaN/±Inf is replaced by 0, and each repair is counted and latched (RS-7.9); a non-finite loop value clears that line's DC state, counted. |

## 11. Memory (1.0 bar MUST, RS-2.6)

All allocated at `prepare`, for the host rate (ADR-237). Lines: 2 × (⌈12.02 s × fs⌉ + 8) samples × 4 B (float32
storage; the loop state is double):

| rate | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| lines | 4.04 MiB | 4.40 MiB | 8.80 MiB | **17.61 MiB** |

(The manifest's 17.58 MiB is the bare 12 s; the 20 ms modulation margin adds 0.03 MiB.) The full per-instance
figure, with filters, heads and the varispeed tape history if Q-A1 picks varispeed (a second line of doubles,
+2×), is stated at M1. **State size**: 27 keys, well inside the 2 KiB budget per corner (RS-6.12).

## 12. Macros

Echo offers up to 8 macros per preset, ordered and labelled by the preset (ADR-169 A4). It does not resolve them
itself: horde resolves them through SPEC-MODULE-MACROS §5 (RS-5.5, second case). Nothing is driven by horde's
intents unless a preset binds it.

## 13. Constants

| constant | value | where |
|---|---|---|
| maximum delay | 12 s per line | §3.4 |
| minimum delay | 1 ms; the read never closer than 3 samples to the write | §3.4 |
| ceiling | knee 0.9, asymptote 1.0 | §5.1 |
| DC blocker | 5 Hz, gain-normalised | §5.1 |
| read speed | s ∈ [0.25, 4] | §3.6 |
| repitch fallback | 3 s at the cap's rate | §3.6 |
| modulation slope cap | ±25 % speed | §7 |
| ramps | 20 ms (gains, mix, feedback, routing and filter switches); 250 ms (modulation depth, spread, shape) | §4–8 |
| cutoff glide | 10 ms, log-frequency | §6 |
| crossfade queue | latest target wins | §3.5 |
| kernel | 4-point FIR (cubic Hermite in the lab; Hermite vs Lagrange left open by D-004, both in the lab) | D-004 |

## 14. Open items at this draft

| id | question | how it closes |
|---|---|---|
| **Q-A1** | the repitch law (§3.6) | the human, by ear in the lab's Q-A1 panel, by poll |
| **Q-A2** | tempo moves (§3.8) | the human, by ear in the Q-A2 panel, by poll |
| **Q-A5** | what "jump" means (§3.7); new, raised by this spec | the human, by ear in the Q-A5 panel, by poll |
| **S-1** | the generation ratio's exactness against the DC blocker (§5.3) | by poll, with the spec |
| Q-A4 | mid/side and B408 | **closed** by poll 2026-10-20 (§4.1) |
| offsets | L/R offset model | **closed** by poll 2026-10-20 (§3.2) |

## 15. The lab and the oracles

- The lab (`lab/echo-lab.html`) implements this spec plus the open candidates as lab-only switches. Its smoke
  (`tools/lab_smoke.mjs`, in `./verify fast`) proves: no clock and no `Math.random` in the engine (static and at run
  time); the lab's manifest equals `params.json`; two fresh instances render bit-identically, and `reset()` equals a
  fresh instance; silence in gives exact silence out in every routing and time-change mode at 120 % feedback; the
  output stays finite under NaN/Inf injection and every repair is counted; the read speed stays in [0.25, 4] with
  zero reverse reads for every candidate law and move; repeats land on their sample with the §5.3 ratio; settled-off
  filters are bit-identical bypasses; mid/side folds to its M line; past unity the loop parks; the click reading
  separates a crossfade from a bare jump. **Every one of those detectors is first run against a planted fault and
  must read red on it.**
- *Finding on D-009's M2*, from the lab: M2 reads ≤ 1 for smooth speed changes, but a **speed kink** (a slew law's
  corners, a log one-pole's onset) reads a few (4.1 at 48 kHz on the 431.7 Hz probe), because a step in speed is a
  step in the first difference. A bare jump reads tens. So M2 ranks clicks above kinks above smooth glides; it is a
  measurement, and no perceptual threshold is claimed (D-009).
- *Finding on probes*: a 440 Hz probe is blind to a 0.5 → 0.25 s jump (exactly 110 cycles, the same phase). Click
  probes must be incommensurate with the test times; the lab uses 431.7 Hz.
- M1's C++ invariant suite tests this spec directly (ROADMAP M1); the lab is never its reference.
