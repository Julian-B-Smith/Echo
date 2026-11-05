# The tap swarm as an opt-in option: a design note for the poll

**Origin:** Echo lead session, 2026-11-05. Asked for by the human mid-T-046, after looking at the legacy echo's tap
distribution: "could make for an ethereal effect... consider appending the behavior as an option". **Status: a
proposal. Nothing here is decided, and no protected path is touched.** The listening prototype is
[`tap-swarm-proto.html`](tap-swarm-proto.html), in this folder.

ROADMAP "Deferred / demoted" lists "a swarm-modulated line" among the extras to revisit after M3, each by poll. SPEC §1
lists it as post-1.0. This note gives that poll its material: what the legacy engine does, three ways it could fit
Echo, what each one costs, and the questions for the human.

## 0. In short

- **What made the legacy echo sound ethereal** (measured, §1): a cloud of 8 taps spread over 0.28–0.58 s, each tap with
  its own ±31-cent drift. Its feedback averaged all the taps into one write head. That averaging is the law ADR-142
  rejected. Its broadband loop gain is regen/√n (0.318 at regen 0.9), so the highs die first and the tail turns
  into a dark wash. Its echo density reaches that of a diffuse field (reverb) within about 3 s.
- **One line cannot average several taps in its loop without losing gain** (§2). This is arithmetic, not a bad
  choice of normalisation. The only lossless multi-tap loops are unitary networks and allpass chains, and both are
  diffusion, which is Scape's ground.
- **Three shapes, all in the prototype** (§3):
  - **(a) scatter**: an output cloud around each repeat, with feedback from the one main head. The repeats stay
    discrete, the loop is untouched, and the loop gain is exactly the feedback.
  - **(b) smear**: the cloud's average shares the loop with the main head. The loop bound holds by construction,
    but the density grows every pass. At 70 % smear it is a diffuse field by 6 s. **Flagged: this is Scape's
    diffusion.**
  - **(c) wander**: one head in the loop at a time, hopping between the swarm's times after each pass. The loop gain
    stays exactly the feedback, and the repeats stay discrete but fall off the grid.
- **Recommendation** (§5): offer (a) only, with taps placed before the repeat, after M3, under a saved-state version
  bump. Leave (b) to Scape, or let Echo feed Scape in the rack. (c) is the listener's call.

## 1. What the legacy engine does, measured

Source: `intake/legacy/swarm-time-lab.html`, engine "echo — tap swarm": `rebuild()` lines 224–249, drift lines
278–284, the per-sample path lines 349–367. It was measured headlessly at 48 kHz by slicing its `TimeLab` class and
driving `controlTick()` and `processSample()` with a poisoned `Math.random`. The exciter, the one user of
`Math.random`, was not run. The script lives outside the repo; it reproduces every number below.

**The taps** at the defaults (count 8, size 0.55 → centre 0.397 s, spread 0.6 → ±1.2 octaves, seed 1234):

| distribution | tap times (s) |
|---|---|
| even | 0.173 0.219 0.278 0.352 0.447 0.567 0.719 0.911 |
| gaussian (default) | 0.283 0.327 0.401 0.430 0.451 0.455 0.556 0.580 |
| cauchy | 0.173 0.173 0.210 0.339 0.402 0.404 0.405 0.422 |

The taps are not a rhythm. They are a cloud around one centre time, and the cloud has no main head.

**The per-sample path:**
- One shared buffer of 2¹⁷ samples (2.7 s at 48 kHz). The write is `tanh(x + fbLp·regen)`, so the dry input is
  limited too.
- n linear (2-point) reads. The wet is `wetRaw/√n`. The feedback is `wetRaw/n`, DC-tracked and then damped by a
  one-pole that is always in circuit: 8 kHz at damp 0, 2.6 kHz at the default 0.4.
- Mono out (`outL = outR`).

**The feedback law's magnitude** |(1/n) Σ e^(−jωdᵢ)| over 100 Hz – 10 kHz, at the default gaussian taps:

| n | RMS | 1/√n | band peak | broadband loop gain at regen 0.9 |
|---|---|---|---|---|
| 4 | 0.500 | 0.500 | 0.999 | 0.450 |
| 8 | 0.354 | 0.354 | 0.945 | **0.318** (ADR-142's "≈0.32", confirmed) |
| 12 | 0.289 | 0.289 | 0.816 | 0.260 |

**What the tail does** (an impulse in, regen 0.9, damp 0, drift off):

| 0.5 s window | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| energy, dB re window 1 | 0.0 | −4.5 | −14.8 | −23.1 | −28.6 | −33.9 | −38.2 | −42.5 |
| share of energy below 400 Hz (a seeded noise burst in) | 4 % | 6 % | 12 % | 17 % | 20 % | 23 % | 25 % | 28 % |
| echo density, NED | 0.01 | 0.01 | 0.09 | 0.21 | 0.43 | 0.76 | 0.90 | 0.93 |

- **Arrivals** per 0.25 s (local maxima above −60 dB): 0, 6, 18, 56, 104, 201, 300, 444, 598, 731, 818, 836.
- **NED** is the normalised echo density of Abel and Huang (2006): the fraction of samples beyond one standard
  deviation in ~21 ms frames, energy-weighted, over the Gaussian's 0.3173. It reads about 1 for a diffuse
  (reverberant) field and near 0 for discrete echoes.

So the broadband energy drops ~15 dB within 1.5 s. After that the decay slows to ~9 dB/s, because only the low
frequencies keep a loop gain near regen: there the taps are still correlated, which is the band peak above. The tail
darkens, its density grows by about two orders of magnitude, and by 3–4 s it is a diffuse field.

**The drift:** each tap does its own random walk in log-time, 30 cents deep at the defaults. Measured over 10 s, the
read speed runs 0.982 – 1.018, which is ±31 cents of wow per tap, independently. That independent wow is much of the
"ethereal" shimmer: every repeat is chorused by its own cloud.

**What the sound is made of, then:** (1) a drifting cloud of taps on every echo; (2) a feedback that re-spreads that
cloud on every pass, so the density explodes; (3) a broadband loss that leaves a dark low-end wash. (1) is a
multi-tap delay, which is Echo's ground. (2) and (3) are diffusion, which is Scape's ground (CLAUDE.md §Domain,
horde's ruling).

## 2. The constraint behind every shape: one line, many taps

Echo has one line per channel, 12 s long (SPEC §11). Feeding back n taps of that line gives the loop response
H(ω) = Σ wᵢ e^(−jωdᵢ). SPEC §5.2 bounds the **peak** |H| ≤ 1 (times g), over the band at the current rate.

- With positive weights the peak sits at DC, where every tap is in phase, so the bound needs Σ wᵢ ≤ 1. The
  **broadband RMS** of H for decorrelated delays is √(Σ wᵢ²). Under Σ wᵢ = 1 that is at most 1, and it equals 1 only
  for a single tap. Equal weights give exactly 1/√n. **ADR-142's trap is therefore not a choice of normalisation. It
  is what averaging several taps in one line costs.**
- Signs or other weights do not escape it. Normalising by 1/√n instead puts the peak at **2.73** for the prototype's
  default 8 taps (its self-check plant), which breaks §5.2 at every feedback above 37 %.
- The only lossless multi-path loops are **unitary networks** (an FDN with a Householder matrix, which is the legacy
  lab's own "room" engine) or **allpass chains**. Both are diffusion. They also need n separate lines, and at 12 s each
  that is n × 17.6 MiB at 192 kHz, which Echo's memory statement (SPEC §11) cannot carry.

So "the swarm in the loop without the trap" can mean only two things. Either (b): share the loop between the main
head and the cloud, and accept the broadband loss in proportion to the share. Or (c): keep exactly one tap in the
loop at a time.

## 3. The shapes

The prototype's DSP is Echo's loop: one line per channel; the ceiling on the returning term only; the gain-normalised
5 Hz DC blocker; the one-pole loop filter with true bypass; the 4-point read (D-004); the equal-power mix; stereo,
ping-pong and mid/side. The shapes are weight vectors on three signals per line: the main head, the cloud and the
wander head.

| mode | wet = | feedback = |
|---|---|---|
| off (Echo) | main | main |
| (a) scatter | (1 − c)·main + c·cloud/√n | main |
| (b) smear | (1 − c)·main + c·cloud/√n | (1 − σ)·main + σ·cloud/n |
| (c) wander | (1 − c)·wander + c·cloud/√n | wander |
| legacy law | cloud/√n | cloud/n |

Here c is `cloud` and σ is `smear`. Each tap sits at T·rᵢ, with rᵢ drawn from the legacy lab's three distributions.
**Placement** maps them either *around* the repeat, T·2^(x·spread), or *before* it, T·2^((x − 1)/2·spread) ≤ T.

**Measured on the prototype.** These are its own DSP, sliced from the page and run in Node. T = 0.5 s, n = 8,
gaussian, seed 1234, feedback 90 %, loop filter off, drift off:

| shape | law peak / RMS | broadband loop gain | late decay (Echo alone: −1.8 dB/s) | arrivals per 0.5 s, windows 2 → 12 | NED, windows 2 → 12 | reads per sample per line |
|---|---|---|---|---|---|---|
| off (Echo) | 1.000 / 1.000 | 0.900 | −1.8 dB/s | 2 → 2 | 0.00 → 0.00 | 1 |
| (a) scatter, around | 1.000 / 1.000 | 0.900 | −1.8 dB/s | 18 → 18 | 0.01 → 0.01 | 9 |
| (a) scatter, before (spread 1.2) | 1.000 / 1.000 | 0.900 | −1.8 dB/s | 18 → 19 | 0.01 → 0.01 | 9 |
| (b) smear 30 % | 0.954 / 0.708 | 0.637 | −5.7 dB/s | 79 → 277 (peak 448) | 0.01 → **0.19** | 9 |
| (b) smear 70 % | 0.900 / 0.389 | 0.350 | −7.4 dB/s | 83 → 0 (peak 841, then decayed away) | 0.01 → **0.91** | 9 |
| (c) wander (before, spread 1) | 1.000 / 1.000 | 0.900 | −3.5 dB/s † | 4 → 26 | 0.01 → 0.02 | 1 (2 mid-hop) |
| legacy law | 0.965 / 0.353 | 0.318 | −6.1 dB/s | 89 → 0 (peak 1682) | 0.03 → **0.99** | 9 |

† Wander's loss per pass is exactly g. Its passes are shorter, because the "before" taps sit at 0.5 – 1 T, so it
decays faster per second. The 40 ms hops also dip the broadband gain mid-fade (equal-gain, as Echo's crossfade mode
does).

The loop gain of off, (a) and (c) is exactly g at n = 4, 8 and 12 alike. With the
loop filter on (60 Hz / 9 kHz) and 25 ¢ of drift, the discrete shapes read NED 0.1–0.17 by 4 s, from the filters'
tails. Smear 60 % reads 0.58 and the legacy law 1.00.

### 3.1 (a) scatter: the cloud on the output

Each repeat carries the same seeded, drifting cloud of n taps. The cloud never re-enters the loop, so the repeats stay
exactly Echo's: same times, same generation ratio, same decay. The density does not grow (18 arrivals per window at
every generation). This is a multi-tap delay with a seeded, chorused pattern, in Echo's lane by horde's ruling.
"Around" puts some of the cloud after the repeat, which is closest to the legacy sound. "Before" makes the cloud
bloom into the repeat. §4.4 gives the reason "before" may be the only cheap one.

### 3.2 (b) smear: the swarm in the loop, flagged as nearing Scape's diffusion

The loop bound holds **by construction**: the feedback is a convex blend of paths whose peak gain is 1, the argument
SPEC §4 uses for the routing switch, and every self-check sweep reads a peak ≤ 1. ADR-142's divide-by-n is gone from
the main path: at smear σ the main head keeps (1 − σ)·g. But §2 still applies to the shared part. The broadband loop
gain is g·√((1 − σ)² + σ²/n), so the highs decay faster than the lows. The arrivals grow on every pass and the field
goes diffuse: NED 0.19 at σ = 0.3 and **0.91 at σ = 0.7 by 6 s**, against 0.99 for the legacy law. Above about
σ = 0.5 this *is* a diffuser built from delay taps, which horde gave to Scape. A capped smear (≤ 30 %: a halo that
thickens, but stays ≤ 0.2 NED at 6 s) is the most that could be argued as Echo's.

### 3.3 (c) wander: the swarm in the loop, one tap at a time

After each pass the loop's head hops to another of the swarm's times, or back to the main time, chosen by a seeded
stream. The hop is SPEC §3.5's raised-cosine equal-gain crossfade (40 ms in the prototype). The loop gain is exactly g
outside the hops and convex through them. The density stays discrete (NED ≤ 0.02), and generation k lands at a sum
of k swarm times, so the repeats walk off the grid in a seeded, unrepeating pattern. It reuses machinery Echo already
has and gates (two heads at most, the D-049 A4 fade). It sounds less "ethereal" than "a tape that skips", and it is
the only swarm feedback that keeps the full loop gain. The human decides by ear whether it earns a place.

## 4. What each shape costs Echo

### 4.1 Keys and classes (a proposal; no key is added by this note)

| key | unit | range | default | class | switch / smoothing | shapes |
|---|---|---|---|---|---|---|
| `swarm.mode` | off / scatter / smear / wander | — | off | flip | the mode weights ramp 20 ms; convex, so the loop bound holds mid-switch | all |
| `swarm.cloud` | % | 0 … 100 | 50 | continuous | 20 ms ramp | a, b, c |
| `swarm.spread` | octaves | 0 … 2 | 0.6 | continuous | **moves taps**: a time move (§4.4) | a, b, c |
| `swarm.drift` | cents | 0 … 100 | 25 | continuous | 250 ms ramp, like `mod.depthMs` | a, b |
| `swarm.seed` | integer | 0 … 65535 | 1 | flip | **a tap-set crossfade**, equal-gain 30 ms (not §7's seeded jump: the taps move) | a, b, c |
| `swarm.smear` | % | 0 … 100 (or capped) | 30 | continuous | 20 ms ramp | b |
| `swarm.count` | integer | 4 … 12 | 8 | flip | tap-set crossfade | optional |
| `swarm.dist` | even / gaussian / cauchy | — | gaussian | flip | tap-set crossfade | optional |
| `swarm.place` | before / around | — | before | flip | tap-set crossfade | optional |
| `swarm.stereo` | independent / shared | — | (§4.5) | flip | tap-set crossfade | optional |
| `swarm.driftRate` | Hz | 0.1 … 3.2 | 0.28 | continuous | log glide, like `mod.rateHz` | optional |

- **The minimal set for (a) is five keys:** mode, cloud, spread, drift and seed. Fix count at 8, gaussian, "before"
  placement and a drift rate near 0.3 Hz. (b) adds smear. The full set is 11.
- A swarm with its own seed keeps a tap pattern, which is a rhythm the user chose, apart from `mod.seed`. Deriving it
  from `mod.seed` saves a key, but couples the two: reseeding the modulation would reshuffle the taps.
- **`swarm.mode` = off must be bit-identical to Echo without the swarm**: the cloud is not computed and its state is
  cleared, the true-bypass rule of SPEC §6, held as T-038's criterion 3 held `time.scale` at ×1. In the prototype,
  off skips the cloud entirely.

### 4.2 Determinism

Every stream is mulberry32, seeded from `swarm.seed` alone, one per (line, tap, purpose): the tap draw, each tap's
drift curve and the wander schedule. They are derived by a murmur3 finaliser, so neighbouring seeds and taps are
unrelated streams. "Shared" stereo derives both lines' streams as line 0. A load restarts them from the patch's seed
at phase 0, as §7 does for the modulation. The drift is Echo's own shape: a Catmull-Rom curve through seeded points,
clamped to [−1, 1]. No clock is read and nothing calls `Math.random`.

The prototype checks itself (`?check=1`, or its button). In the page and in Node, all 14 checks pass, each after its
planted fault reads red:
- a static scan and a poisoned-`Math` run of every mode;
- two renders of every mode bit-identical, while seed + 1 differs;
- the loop bound in every mode, count, spread and placement (worst 1.000000), against a 1/√n plant (2.73);
- silence in gives exact zero out, against a 10⁻²⁰ write-bias plant;
- the line bounded at 120 %, against a no-ceiling plant (91);
- the speed bound in §4.4.

### 4.3 Real-time cost, measured

- **Reads:** (a) and (b) add n 4-point reads per line, and 2n during a tap-set crossfade. (c) adds one head (two
  mid-hop), plus n if its cloud is up.
- **Memory:** no new line. The memory statement (4.68 MiB at 48 kHz) is unchanged except for the tap state:
  12 taps × 2 sets × 2 lines × ~10 doubles ≈ 4 KiB, preallocated at `prepare`. Nothing allocates on the audio
  thread. (The prototype's worklet does allocate a new tap set on a parameter change. The C++ would not.)
- **CPU.** A C++ kernel did the swarm's per-tap work: the drift curve, the cents-to-ratio `exp2`, the offset slew,
  the cap and one read of `engine.h`'s 4-point kernel. It was timed against `echo::Echo` at the manifest defaults,
  interleaved, best of 7, Release `-O3 -ffp-contract=off`, single-threaded, on the M3. **The machine was loaded**
  (load average 7–10, horde's suite on a core), so only the ratios are reported. Echo's own default read 0.364 % of
  the core here against `docs/cost.md`'s 0.267 %.

| extra work, as a multiple of Echo's whole default patch | n = 4 | n = 8 | n = 12 |
|---|---|---|---|
| drift computed per sample, 44.1 / 48 / 96 / 192 kHz | 2.0 / 2.1 / 1.9 / 2.0 | 3.8 / 4.0 / 3.6 / 3.6 | 5.6 / 5.5 / 5.4 / 5.5 |
| drift at control rate (every 32 samples), same rates | 0.7 / 0.7 / 0.7 / 0.7 | 1.3 / 1.3 / 1.3 / 1.2 | 1.8 / 2.1 / 1.8 / 1.7 |

  The reads are cheap. **The per-sample drift math is the cost**, so the C++ should run the drift at control rate,
  as SPEC §11 already does for the tape law's history.
- **Against the budget** (Layer-E, never a gate): we take `docs/cost.md`'s defaults (0.267 % at 44.1 kHz, the
  judged row; 0.281 % at 48) and its worst rows (worst-morph: 1.635 % and 1.789 % of a min-spec core, ×1.5 assumed).
  - At 44.1 kHz with control-rate drift: n = 8 adds 0.50 % (worst row 2.14 %) and n = 12 adds 0.73 % (2.37 %). Both
    are inside 4 %, even with a tap-set crossfade (2n reads) in flight.
  - At 48 kHz: n = 8 makes 2.35 % and n = 12 makes 2.67 %. Still inside.
  - Per-sample drift at n = 12 reaches 3.88 % at 44.1 kHz and 4.1 % at 48 kHz, over budget.
  - 96 and 192 kHz are informational rows. 192's worst row is already over without the swarm, and with control-rate
    drift the swarm adds 1.2 – 1.3 (n = 8) to 1.7 – 1.8 (n = 12) times Echo's default at each rate.
- (b) needs a second loop filter per line, because the wet and the feedback then differ. That is one more one-pole
  pair, small and not measured.

### 4.4 Clicks and the speed bound

- **The hard invariant (D-006, s ∈ [0.25, 4] for every sounding head) has to hold for every tap.** A tap at r·T moves
  r times as fast as the main head in a glide, so a tap is a head in its own right.
  - **"Before" placement keeps it by construction.** With r ≤ 1, drift that only shortens a tap, and a per-tap offset
    slew of ±0.25 sample a sample (SPEC §7's rule, applied per tap), every tap's step is r × (the main head's step,
    in [−1, 0.5]) plus at most ±0.25. That lands inside Echo's [0.25, 2.25] range, exactly as the main head does.
    The prototype's check: a capped 1.5 s → 100 ms → 1.9 s sweep with 100 ¢ of drift at its fastest rate, taps from
    T/4 to T, engages the per-tap cap **0 times**.
  - **"Around" does not.** Its positive control (taps up to 4T) engages the cap **280 545 times** in the same sweep.
    A tap past T needs its own cap and catch-up brake, the D-049 A4 tape-law work, repeated per tap and gated at four
    rates. That is the main engineering reason to prefer "before".
- **A tap-set change** (seed, and count, distribution, placement or stereo if they are keys) jumps every tap. It is
  declared as an equal-gain raised-cosine crossfade between the old and new sets. The latest target wins, as in
  SPEC §3.5. It needs the M2 click gate at 44.1–192 kHz with a planted bare set swap as its must-fail control.
- **A spread move** moves every tap continuously, which makes it a new kind of time move. It should follow
  `time.change` (repitch, crossfade or jump) as `time.scale` does (D-048), and be gated the same way.
- **A mode switch** is a 20 ms convex ramp of the weights. The loop bound holds through it (this note's §3.2); the
  click reading is a blend between two continuous signals.
- **Wander's hops** are SPEC §3.5's crossfade exactly: raised cosine, equal-gain, with the outgoing head keeping its speed.
  Mid-hop, two heads sound, with crossfade mode's own brief comb.
- **The cloud's level:** the wet normalisation is 1/√n, the legacy's, right for decorrelated taps. As spread → 0 the
  taps coincide and sum coherently, up to √n louder (+9 dB at n = 8). A spread floor, or a coherence-aware gain, is a
  detail for the spec, not for the poll.

### 4.5 Mono safety (B408)

ρ of the mono fold per 1 s, for a mono impulse in (ρ_in = 1), at feedback 60 %, n = 8 and 25 ¢ drift:

| routing | stereo clouds | (a) scatter | (b) smear 30 % / 70 % | (c) wander | legacy law |
|---|---|---|---|---|---|
| stereo | independent | 0.74 – 0.89 | 0.76 – 0.89 / 0.60 – 0.85 | 0.89, then 0.50 | 0.50 |
| stereo | shared | 1.00 | 1.00 / 1.00 | 1.00 | 1.00 |
| ping-pong | either | 0.50 (no cross term) | 0.50 | 0.50 | 0.50 |
| mid/side | either | 1.00 (the fold is the M line) | 1.00 | 1.00 | 1.00 |

- **Nothing cancels.** Every cloud weight is positive, so no sign is ever flipped. Where ρ falls, it is
  decorrelation (a power sum), not cancellation. The worst ⅓-octave dip of an independent cloud's fold, against the
  mean of L and R, is **−4.4 dB at 63 Hz** ("around") and **−4.9 dB at 100 Hz** ("before"), against −3.0 dB for fully
  decorrelated reads. A shared cloud reads 0.0 dB.
- **But D-044's stereo gate (ρ_out ≥ ρ_in on equal-time patches) would fail independent clouds.** There are two ways
  to square it:
  - read independent L/R clouds as a *requested widening*, reported and not gated, as offsets already are (a D-044
    reading for the human);
  - or allow only the shared cloud in stereo mode.
- **Ping-pong and mid/side keep their by-construction statements unchanged** (ρ = ½ exact; the fold is the M line).

### 4.6 Tail and rest

- **(a):** the loop is untouched, so SPEC §10's tail formula stands, with T_max as the longest *tap*. Under "before"
  that is T, so nothing changes. Under "around" it is T·2^spread plus the drift.
- **(b) and (c):** every pass is at most the longest tap and loses at least the factor g (peak bound), so the same
  formula, with the same T_max, is an upper bound.
- The rest gate's −120 dBFS test reads the wet and the line writes as before.

### 4.7 The lock, `module_version` and the cutoff

**Every key added is the same chain of protected edits** as `time.scale` (D-048, T-038):
- SPEC §1 moves "a swarm-modulated line" from post-1.0 to *ships*, plus the §9 table;
- `spec/params.json`, and the lab's mirror (`lab_smoke` requires them equal);
- `core/manifest.lock.json`, rewritten only by `tools/manifest_lock.py write --decision D-NNN`. `module_version`
  moves from `bef5da84b046fb13`;
- regenerated presets and `docs/admission.md`;
- the face (T-022, drawn on 30 keys today);
- the stereo report's rows, the bench's rows, and the M4 IR renderer (the visualiser draws the true impulse
  response, D-011, so the cloud must be in `ir_render`);
- the click gates at four rates, with a planted fault per new switch.

**The precedent:** `time.scale` was one key on existing machinery. It took a day, and its critic found a click
defect that became T-040. The minimal swarm is five keys and a new kind of head, landing in the 36 days to
**2026-12-11** beside T-021 – T-027, T-039, T-042 and T-046. That is the risk to the cutoff.

**After 1.0 the keys freeze (B428), but they may be added to.**
- `module_version` moves, which is its purpose: "so a load can tell when a module's keys have changed".
- SPEC §16.2 refuses "a different version" of the saved text and any missing key. So the addition needs
  `echo-state 2`, with a migration that fills the new keys at their off defaults.
- Because off is bit-identical to Echo (§4.1), that migration is exact. It is a default change under horde's
  ADR-197 rule E, so it needs an ADR and a migration, and horde should get a notice.

## 5. Recommendation

1. **Ship shape (a), scatter, as the option, with "before" placement.**
   - It keeps discrete repeats, which keeps Echo in its lane.
   - It leaves the loop and every ADR-142 invariant untouched.
   - It keeps D-006 by construction.
   - It costs +0.5 % of a min-spec core at n = 8 with control-rate drift.
   - Its mono story fits D-044 with a shared cloud, or with independent clouds read as a widening.
   - It carries the drifting cloud, which is what made the legacy echo shimmer.
   - The listening question is whether "before" (the cloud blooms into the repeat) keeps enough of the magic of
     "around" (the cloud straddles the repeat). "Around" doubles the click work (§4.4).
2. **Do not ship (b), smear, in Echo beyond a small cap**, if at all. Above about 30 % it builds a diffuse field, the
   job horde gave Scape. If the human wants the legacy wash, the route is Echo → Scape in the rack, or a "tap-swarm
   diffusion" asked of Scape through horde. This note files nothing.
3. **(c), wander, is optional and cheap.** It is a discrete-repeat effect on machinery Echo already gates. Keep it
   only if the human likes it by ear.
4. **Timing: after M3**, as the ROADMAP already says, under `echo-state 2` with the exact off-default migration. If
   the human wants it in 1.0 instead, take the five-key set and (a) only, as one D-048-style decision and queue item
   behind T-046, and accept that it competes with T-021 – T-027 for the cutoff.

## 6. The prototype, briefly

[`tap-swarm-proto.html`](tap-swarm-proto.html) opens from disk in a browser (AudioWorklet; a secure context such as a
`file://` or `localhost` page).
- Slots **A** and **B** run side by side on the same seeded source: pluck, drums, chord, noise burst, clicks, or your
  own file. Space toggles between them with a 20 ms crossfade, and keys 1–5 pick the mode.
- Presets give a starting point for each shape: scatter before or around, smear halo or wash, wander, legacy.
- The page draws the impulse response of the slot you hear and measures the loop law, decay, arrivals, NED, ρ, reads,
  cap hits and hops.
- **It is not the core.** The main head glides by a plain exponential law under the 12-semitone cap. There is no
  tempo mode, no line modulation (the swarm's per-tap drift stands in), no load or rest logic and no I/O gain.
- **It is the shapes' own DSP**, and every number in §3 and §4.2 – 4.5 comes from it.

## 7. Questions for the human's poll

1. **By ear: which shapes do you want at all?** (a) scatter · (b) smear · (c) wander · none (keep the legacy as a
   reference only).
2. **Placement for (a):** "before" (D-006 by construction; recommended) · "around" (closer to the legacy; per-tap caps
   and brakes, more gates) · both, as a key.
3. **Smear (b):** leave it to Scape, and if wanted, ask horde for it there (recommended) · ship it in Echo capped at
   about 30 % · drop it.
4. **When:** after M3, under `echo-state 2` with the exact off-default migration (recommended) · into 1.0 now, as a
   D-048-style lock rewrite and queue item behind T-046 · park it.
5. **Stereo:** a shared cloud only in stereo mode (D-044 as it stands) · independent L/R clouds as a requested
   widening, reported and not gated (a D-044 reading) · both, as a key.
6. **Key budget:** the minimal five (mode, cloud, spread, drift, seed) · the minimal five plus smear and/or count ·
   the full set (up to 11).
