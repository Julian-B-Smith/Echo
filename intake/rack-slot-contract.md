# The hosted-module rack-slot contract (B450)

> **Origin.** horde lead session, 2026-10-09, on Bulwark's brief seq 11
> (`integrations/bulwark/brief-rack-slot-status.md`) and ROADMAP B450, which the human approved the
> same day ("go ahead with B450"). An implementer drafted it from the lead's brief. The state,
> preset and history clauses were added at the lead's direction, after the human asked what
> FOUNDATIONS says about them.

**Status: RATIFIED by the human 2026-10-09** ("contract ratified"), with the lead's recommendation
on every open question (§10) accepted. Q3 (the 3 dB click ceiling) and Q9 (the 2 KiB state budget)
are PROVISIONAL: set from measurement before 1.0. ADR-201 records the ruling. **Amended 2026-10-13 (ADR-218, §12):** the 3 dB click ceiling is suspended as a pass/fail rule until the first listening batch; A4, A11, A16, RS-4.4 and RS-7.6 are read with §12. Clauses marked P
below are now ruled, except where §10 says provisional. Last verified
2026-10-09 against horde `main` at `cebe17e`, FOUNDATIONS `1a5995e` (`fx_operator.h`), Bulwark
`d2a4f2d`, Sluice `04fb7a1`, Shriek `bab145c` and Scape `8b5dc33`. Queue item: **B450**.

**What this is.** This is the contract a hosted FX module meets to sit in horde 2's FX rack. Bulwark,
Sluice, Shriek and Scape are hosted modules. Most of the contract is already ratified, but it is
spread across a dozen records. This document collects those records and cites each one. It
invents only what is missing, and it marks every such clause PROPOSED.

**What this is not.** It is not code and not the admission test's implementation. It is not a
second module ABI: the code-level interface is FOUNDATIONS' FX-operator ABI (OQ #32), adopted
here as the lead's working direction (B450), which still needs the human's ruling (Q1 in §10).

## 0. How to read it

Every clause has an id (`RS-n.m`) and one of two statuses:

| Mark | Meaning |
|---|---|
| **R** | RATIFIED. A human ruling already covers it, and its source is cited. This document only restates it. If the restatement and the source disagree, the source wins and this document has a bug. |
| **P** | PROPOSED. New in this document, or a reading of a ratified rule that the rule does not state itself. Each P clause that needs a ruling appears in §10. |

**MUST / SHOULD** follow the module 1.0 bar (`docs/proposals/module-1.0-bar.md`, B439). A module
meets every MUST before horde admits it to the rack. A SHOULD may ship as a known gap.

**Two corrections to the sources, found while drafting.** Both are reported to the lead, and
neither changes a ruling.
1. **ADR-193 holds rulings D1–D8 of `docs/proposals/fx-chain-morph-round2.md` and nothing more.**
   The static-node model, cable-weight morphing, one-sample feedback cables, sleep with state
   kept, and the click metric come from **B265 rounds 3 and 3b**. That is the human's direction
   of 2026-10-08 ("They all sit static, and the connections between them gradually emerge and
   retreat"), plus the human's listening notes and lab PRs #980, #982 and #985. None of it is an
   ADR. Our seq-12 answer to Bulwark (`integrations/bulwark/response-rack-slot-status.md` §2)
   described all of it as "ruled (ADR-193)". This document marks those clauses **P** and asks the
   human to ratify the static-node model as the product rack (Q2).
2. **ADR-175** (a cyclic topology runs at n = 1) and **ADR-173** (parameter classes) are both
   still recorded as PROPOSED. Clauses that rest on them alone are marked **P**. One-sample audio
   feedback itself is ADR-128, which is ACCEPTED.

## 1. The interface: FOUNDATIONS' FX-operator ABI (OQ #32)

The slot speaks to a module through FOUNDATIONS' FX-operator ABI. The draft today is slices 1–3
in FOUNDATIONS `core/include/foundations/fx_operator.h` (DECISIONS #127, #129 and #131; unfrozen).
Its parameter declarations come from OQ #33's engine manifest
(`core/include/foundations/engine_manifest.h`, DECISIONS #132). Every clause in this contract
maps to one ABI entry, or else it is the slot's job (horde's side).

| ABI entry (OQ #32) | State of the draft | Contract clauses that ride on it | Slot-side (horde) duty |
|---|---|---|---|
| `prepare(sample_rate, max_block)` | slice 2: `FxOperatorLifecycle` | allocate here only; warm statics (RS-7.10); report properties; I/O gains open at the saved value (RS-4.6) | call on the main thread at activate, never while processing |
| `reset()` | slice 2: bit-identical to a fresh instance | determinism (row A2); transport jump; drops tails | call at the host's reset and at a transport jump |
| `tick` / `process` | OPEN: channel topology and transport are undecided; `FxSample = double` (slice 3) | RT rules (RS-7.7); n ≥ `min_block` (RS-7.2) | adapter converts float↔double, with buffers sized in `prepare`; never chunks |
| `paramSpec` | `registry.h` `ParamDesc` + OQ #33 `MorphKind` / `MorphParticipation` | manifest (RS-2.1, RS-2.2); parameter classes (§4) | read once at load; validate; refuse an undeclared class |
| `setTarget` → `resolve(address)` + `setParam(handle, v)` | slice 3: resolve off the audio thread, then O(1) set that ramps | morph-class smoothing (RS-4.2); macros (§5) | resolve every address at load; send handles only |
| `loadState(…)` | slice 2: `LoadDecl`; arguments OPEN (slice 3 questions due 2026-10-22) | state adoption (§6.3) | stage off-thread; adopt at a block boundary (RS-6.6) |
| `latency()` | slice 1: `LatencyDecl`, whole samples, signed residual | RS-7.1 | report one constant plugin latency |
| `tail()` | slice 1: `TailDecl`, `kInfiniteTail`, `may_change` | RS-7.3–7.6 | never signal a tail change to the host (B429); decide sleep |
| `channels()` | not declared: an open `port.h` width axis | stereo in, stereo out for 1.0 (RS-2.8) | none |
| `minBlock()` | slice 1: `min_block`, undeclared is refused | RS-7.2 | refuse cycle membership when `min_block > 1` |
| meters | #124: I/O peak, clip and non-finite are the host's at the slot boundary; gain reduction is a module output (OQ #33) | RS-4.7, RS-7.9 | tap the slot boundary when the slot owns the gains |
| `morphWeights(w[4], exact)` + the corner patches at load (OPTIONAL) | not in the draft: proposed to FOUNDATIONS (brief `morph-position`, 2026-10-18) | RS-4.10, row A21 | send only to a module that declared it; compute `exact` from the movers it knows; stage the corner patches with the load (RS-6.6) |

### 1.1 Where the three shims diverge from the draft

Bulwark, Sluice and Shriek each carry a local stand-in for the ABI. Scape has no C++ yet.

| Entry | FOUNDATIONS draft | Bulwark (`core/include/bulwark/abi.h`) | Sluice (`netcore/include/sluice/node.h`) | Shriek (`mawcore/include/mawcore/abi.h`) |
|---|---|---|---|---|
| prepare | `(double, uint32_t)` | `(double, int)`; throws on invalid input | `(double, uint32_t)` | `(double, int)` |
| reset | ✓ | ✓, plus `audioReset()` (clears audio, keeps ramps) | ✓ (drops tails) | ✓ |
| process | double; signature open | `double*` L/R, `int n` | **float** L/R, `uint32_t n`, `+bpm` | `double*` L/R, `int n` |
| parameters | `resolve(address)` → handle; `setParam(handle, v)` | `setParam(string_view key, v)`: **no handle** | `idOf(address)` with **0 = none** (a sentinel; the draft cites L0015 against it); `setParam(id, v)` | `setParam(string_view address, v)`: **no handle** |
| latency | `LatencyDecl{samples, residual, sign}` | `int` (0 compressor; the limiter's lookahead) | `uint32_t`; residual in a comment; **varies with the patch's oversampling** | `int` 0; residual 0.25 late, in a comment |
| tail | `TailDecl` | `tail()` + `tailChanged()` | **none declared**; tails bounded internally (at most 3, 8 s cap) | **none declared** |
| min_block | required | 1 | 1 | 1 |
| channels | not in the draft | 2 | 2 | 2 |
| loadState | `LoadDecl` (optional) | `loadState(span<{key, value}>)`, in place | `setPatch` (20 ms crossfade or spill-over) | **none** (legal: loads ramp) |
| legacy `SlotContract` | none | `contract()` | none | `contract()` |
| meters | split (#124) | `meters()`: in/out peak, clip, non-finite count, GR, boost | `inputMeter()` / `outputMeter()` with clip latch | none (slot-provided) |

The shims are close to the draft. The slices themselves name the differences: the parameter
handle, Sluice's float boundary, and the missing tail declarations. Swapping a shim for the real
header is a rename at the call site, as both Bulwark and Shriek planned. horde can admit a shim
module before OQ #32 freezes (Q1).

## 2. Admission

### 2.1 What a module declares

| id | Clause | Status | Source |
|---|---|---|---|
| RS-2.1 | **MUST** ship a manifest in which every parameter has a key, unit, range, default, morph class and rate class. Keys are frozen from the first release. | R | B439 §1; B428 |
| RS-2.2 | The manifest's per-parameter classes use OQ #33's fields: `MorphKind` (continuous / stepped / gate) and `MorphParticipation` (morphs / excluded). A module's local enum is fine if it maps one to one, as Bulwark's `Morph{Continuous, Jump, Device}` does. | P | FOUNDATIONS DECISIONS #132; Bulwark `core/include/bulwark/param_table.h` |
| RS-2.3 | **MUST** declare `latency`, `tail` and `min_block` after `prepare`. An undeclared value is refused, never read as zero. | R (as FOUNDATIONS' rule) / P (as horde's admission rule) | FOUNDATIONS DECISIONS #129 |
| RS-2.4 | **MUST** declare whether it changes the stereo image or the broadband level beyond its I/O gains (`changes_image`, `changes_level`). These are the two fields of the legacy `SlotContract` that still mean something without a rack-owned mix (§2.3). | P | `docs/proposals/fx-slot-contract.md`; ADR-095 |
| RS-2.5 | **MUST** declare `LoadDecl` if it has a load entry. Without one, it is loaded by ordinary parameter changes (RS-6.8). | R (FOUNDATIONS) | FOUNDATIONS DECISIONS #128, #129 |
| RS-2.6 | **MUST** report a worst-case memory figure per instance, including tails it keeps itself. | P (the bar has it as a SHOULD) | B439 §4; Sluice `patchBytes` / `residentBytes` |
| RS-2.7 | One instance per module type. A module is never asked to run twice in one rack, except as a second live instance during a flip or a spill-over tail. | R | ADR-172; ADR-193 D7; ADR-169 A1 ruling 3; ADR-188 |
| RS-2.8 | Stereo in and stereo out (`channels() = 2`) for 1.0. A sidechain is a typed port, not a wider integer, and it is deferred past 1.0. | P | FOUNDATIONS DECISIONS #124 |

### 2.2 The admission test (a specification, not code)

The legacy `slotcontract_check` (`tools/slotcontract_check.cpp`, B281) loops over horde's
built-in slot types through the legacy shell. It cannot admit a hosted module. The hosted-module
admission test is a new check. It drives the module through the ABI exactly as the slot will,
and it runs in horde's tree against the module's code or its shim. Every row has a must-fail
control: a planted faulty version that must turn the row red, naming the module and the row.

| Row | Inputs | Pass rule | Must-fail control |
|---|---|---|---|
| A1 Declarations | the manifest; `OperatorProperties` after `prepare` at 44.1, 48, 96 and 192 kHz | FOUNDATIONS `validate()` passes; the OQ #33 manifest validates against the registry; every parameter has a class | a copy with `tail` left undeclared, and one with a parameter missing its class |
| A2 Reset identity | seeded noise, 2 s, at the default and one stress preset | output after `reset()` is bit-identical to a freshly prepared instance | a reset that leaves one smoother unsnapped |
| A3 Block independence | the same render split at n = 1, 7, 64, 256 and 4096, plus random splits | 0 differing samples against one call (if `min_block = 1`) | ramps advanced per block, not per sample (Bulwark's `rampPerBlock` plant) |
| A4 Latency truth (see §12) | impulse and swept sine | measured group delay equals the declared samples, within the declared residual and sign | latency declared one sample short |
| A5 Silence and tails | 1 s of input, then 60 s of silence; separately, silence from the start | silence in gives exact-zero out; after input stops, output reaches exact zero within the declared tail (or the declared tail is infinite) | a denormal residue plant; a tail longer than declared |
| A6 I/O gain | `docs/proposals/module-io-gain.md` tests 1–7, plus A1's ceiling test for limiter faces | as written there | that document's planted double gain (test 6) and planted post-limiter boost |
| A7 Off | "off" engaged and settled; then off→on→off | settled "off" is bit-identical to no module; gain staging does not jump | an "off" that leaves the module in the signal path |
| A8 Stereo image | L 220 Hz, R 330 Hz, decorrelated | `|L − R|` does not collapse unless `changes_image` is declared | a mono-summing plant (the Notch case) |
| A9 Non-finite | NaN and ±Inf injected at the input and planted in internal state | output stays finite; the module's guard counts each event, latches, and reports | a guard that zeroes without counting (the B448 B1 pattern) |
| A10 Real-time | a scripted session: parameter sweeps, macro moves, corner flips, state loads, sleep and wake | zero allocations, locks and syscalls on the audio thread (the RealtimeSanitizer probe of B448 B3) | a planted `malloc` in `process` must be reported |
| A11 Parameter classes (see §12) | a sweep of every morph-class parameter, and a flip of every flip-class parameter, on a sine probe | morph-class: no step; flip-class: the click metric stays under the ceiling (RS-4.4); Device keys unchanged by a corner flip and a patch load | a flip-class parameter blended instead of flipped; a Device key stored in a preset |
| A12 Macros | save, then load, every factory preset | slot order, labels, bindings and curves round-trip exactly; curves hit `lo` and `hi` exactly; invalid `log` endpoints fail validation | a load that reorders slots |
| A13 Corner presets | load a module preset into each of horde's four corners, flip, save the horde preset, reload | identical render before and after the round trip | a preset that carries a Device key |
| A14 Feedback | place the module inside a cycle at loop gain 1.2 | silence in gives silence out; a module with `min_block > 1` is refused membership | a `min_block = 2` module that the slot lets into a cycle |
| A15 Determinism | two instances, same seed and input, rendered side by side | bit-identical, and neither changes the other (B230's isolation test) | shared detector state between instances |
| A16 Sleep and wake (see §12) | quiet input until sleep, then a note | no step on wake (click metric); parameters changed during sleep land at their targets | a module that ramps from its stale values on wake |
| A17 State completeness | save state mid-session; load it into a fresh instance; `reset()` the original | the two render bit-identically from there on, so nothing outside the saved state and `reset()` shapes the sound | a module with one parameter missing from its saved state |
| A18 Restore exactness | save → load → save | the two saved states are byte-identical, and the renders after each load are bit-identical | a float written through a lossy decimal format (L0008) |
| A19 State size | every factory preset, and a worst-case state | saved state fits the per-module budget (RS-6.12) | an oversized state, which must be refused before allocation |
| A20 Live-state exclusion | save during a ringing tail, a ramp in flight and a latched clip | the saved state is identical to one saved at rest with the same parameters | a module that serialises its tail buffer or meter latch |
| A21 Morph position (RS-4.10, only for a module declaring it) | a morph sweep across four corners with different continuous values, once with `morphWeights` and once from the values alone; again with a macro offset on a design key (`exact` false) and on a non-design key (`exact` true); and a corner edited mid-session | the two renders agree within the module's stated tolerance; with `exact` false the render equals the values-only render; after the edit the module's designs follow the new corner | a module that keeps blending designs when `exact` is false; weights in the wrong corner order (a permuted-weights control must read red); a stale corner design after an edit |

**Layering.** Rows A1–A20 are Layer-0: deterministic, no listening, CI-blocking once wired.
CPU is Layer-E: measured and reported, never a verify gate (ADR-187 §8; RS-8.3). The human's
listening sign-off stays a separate MUST (B439 §6).

### 2.3 The legacy "rack owns dry/wet" rule does not carry over

`docs/proposals/fx-slot-contract.md` (2026-08-15) became ADR-095 (ACCEPTED): for the legacy
rack's built-in slots, the rack applies `out = (mix == 0) ? in : lerp(in, wet, mix)`. **This
contract does not apply that rule to hosted modules (P):**
- Every hosted module already owns its dry/wet as a parameter (Sluice's Dry/Wet, Shriek's `wet`,
  Bulwark's `comp.mb.depth`). A rack mix on top would be a second blend at one point, the same
  doubling B435 forbids for gain.
- A rack blend sums the module with its unprocessed input. ADR-195 forbids exactly that for a
  multiband Bulwark (the crossover notch). A latent module would comb-filter against it (the
  legacy proposal's own point 2).
- The property ADR-095 bought, "off" bit-identical by construction, survives in another form.
  horde's router gives true bypass by routing around the module (ADR-195), so "off" never
  depends on the module (RS-4.8).
- Of the legacy declaration, `changes_image` and `changes_level` survive (RS-2.4). `identity_at`
  and `blends_dry` describe a mix the slot no longer owns. `latency_samples` becomes `latency()`.

ADR-095 stays in force for the legacy shell, which is frozen (ADR-186).

## 3. Where a module sits in the rack

A module does not need these clauses to work, but they set what the slot will ask of it.

| id | Clause | Status | Source |
|---|---|---|---|
| RS-3.1 | **Static nodes.** Every admitted module is resident and prepared at activate, at a fixed place in the rack. The morph never inserts, removes or reorders a module; it moves only the weights of the cables between modules. A module is therefore never created or destroyed on the audio thread. | P | B265 round 3 (the human's direction, 2026-10-08; lab #980) |
| RS-3.2 | **Presence is a coefficient.** "Absent at this corner" means the module's cables are at weight 0, not that the module has been unloaded. | R (as a rule of the matrix) | ADR-088; B50 ("presence is a coefficient") |
| RS-3.3 | **Order and approved chains.** Which cables may exist at a pad point comes from the round-2 pipeline: a weighted order target, constrained to an approved set V (a rule-based house partial order plus curated exceptions), with corner sharpening γ = 2. No pair order that all four corners agree on is ever inverted, and two-change jumps are accepted and shown. | R | ADR-193 D1–D4 |
| RS-3.4 | **House rules a module can trigger.** A multiband Bulwark is never summed in parallel with its own dry or with a branch not phased the same way. Its presence is flip-only in blend mode. Scape→Sluice is unavailable in the lab's V. | R (ADR-195) / P (Scape→Sluice) | ADR-195 §2; B265 round 3 |
| RS-3.5 | **Feedback cables carry one sample of delay** and the crosspoint sum runs inside the sample loop, so a module inside a live cycle is called at n = 1. A loop's gain is bounded through the cycle, and a loop cannot start itself from silence. | R (one sample, ADR-128) / P (the bound and the soft limiter in the loop) | ADR-128; ADR-175; B50 phase 2 (4); B265 round 3b |
| RS-3.6 | **Crossfades between correlated paths are equal-gain** (p = 1), not equal-power. A module that crossfades inside itself (a patch switch, a band-count flip) uses the same law for correlated signals. | P (the lab default; a product default needs an ADR and a migration) | B265 round 3b; Sluice's answer seq 36; ADR-197 rule E |
| RS-3.7 | **Tails may ring after a module's cables close.** A removed delay or reverb keeps its output cable until it is quiet. A per-module tail cap is planned, Sluice's long-feedback settings first. | R | ADR-193 D5 and consequences |

## 4. Parameter classes, I/O gain and "off"

This is the parameter-class contract we promised Bulwark in our seq-9 response. It uses names
that already exist. horde's ADR-173 calls the classes morphable, structural and device. ADR-188
and ADR-195 call them morph, flip and Device. OQ #33 encodes them as `MorphKind` plus
`MorphParticipation`.

### 4.1 The three classes

| Class | OQ #33 declaration | Across the pad | At a corner flip | Examples |
|---|---|---|---|---|
| **morph** | `continuous` + `morphs` | blends: the bilinear corner weights, under the module's own law (linear, or geometric where the module declares it) | moves with the blend; no event | Bulwark `comp.thr`; Shriek `s1.drive`; Sluice's pool numbers |
| **flip** | `stepped` + `morphs` (or `gate`, see RS-4.3) | never blends; one corner's value wins, at a seeded threshold with hysteresis | the module switches click-free by itself (RS-4.3) | Bulwark `comp.bands` (ADR-195); Bulwark `comp.stereo`; a Sluice patch's structure |
| **Device** | any kind + `excluded` | never blends, never flips | unchanged | `io.inGain`, `io.outGain` (B435); Bulwark `comp.bypass`; oversampling factors |

| id | Clause | Status | Source |
|---|---|---|---|
| RS-4.1 | **MUST** declare each parameter's class in the manifest (RS-2.2). A parameter with no class is refused at admission (row A1). | R (class in the manifest) / P (refusal) | B439 §1; ADR-188 |
| RS-4.2 | **Morph-class parameters** reach the module through `setParam`, which ramps under the module's own smoothing, stated in seconds (ADR-009). The slot sends targets at control rate and never smooths a second time. | R (seconds) / P (no second smoothing) | ADR-009; FOUNDATIONS DECISIONS #131 |
| RS-4.3 | **Flip-class parameters** switch at a seeded threshold with hysteresis (ADR-188's band of about 0.1 for patch structure). The module makes the switch click-free itself: Bulwark's own 20 ms switch IS the flip (ADR-195). A module that cannot switch cleanly declares `gate` instead, and the slot ducks the wet path around the swap (duck, swap, un-duck). | R (ADR-188, ADR-195) / P (`gate` as the duck request) | ADR-188 §2; ADR-195 §2; `docs/proposals/fx-chain-morph-round2.md` §2.6 |
| RS-4.4 | **Click ceiling (SUSPENDED as pass/fail, §12).** Every flip-class switch, and every wake (RS-7.6), measures at most **3 dB** on the B265 click metric: the RMS of the second difference against its local median, per transition, on a sine probe. The lab's reversal ceiling is 7 dB (claim 9), and at its defaults equal-gain switches measure 1.7 dB. | P (threshold) | B265 round 3 and 3b |
| RS-4.5 | **Device-class parameters** are global per module instance. They never appear in a module preset or a corner, and a patch load or a corner flip leaves them alone. | R | ADR-188; B435 (scope); B439 §2 |
| RS-4.6 | **MUST** meet the I/O gain standard: `io.inGain` and `io.outGain`, ±24 dB, the 48·(v − ½) law, exactly unity at 0 dB, a 20 ms linear ramp, opening at the saved gain, Input before every path and detector, Output after every path and tail. A limiter face's output is cut-only. | R | `docs/proposals/module-io-gain.md` (B435 + A1) |
| RS-4.7 | **Who provides the gains.** A module that declares the two keys owns them, and publishes a meter tap after each, with peak and a clip latch. A module that does not declare them gets the identical pair, and the taps, from the slot. Never both: admission checks it (row A6). FOUNDATIONS #124 puts I/O metering on the host side at the slot boundary; that is the second case, and the first case keeps B435's rule, because only the module can tap after its own gain stage. | R (B435) / P (the #124 reconciliation) | B435; FOUNDATIONS DECISIONS #124 |
| RS-4.8 | **"Off" is the router's.** A module switched off in horde's rack is routed around, with the slot's gain staging kept, so a settled "off" is bit-identical to no module by construction. A module's own bypass key (Bulwark's `comp.bypass`) stays a Device-class parameter that the module's face may show; the rack does not use it for "off". A multiband Bulwark's "off" may be allpass-phased inside the module; the router's "off" is not. | R (B435's bypass; ADR-195's router bypass) / P (the rack ignores the module's own key) | B435 §Bypass; ADR-195 §2 |
| RS-4.9 | **Host bypass** of the whole plugin passes the input through untouched. | R | B435 §Bypass |
| RS-4.10 | **Morph position (OPTIONAL; ADR-242, RULED in principle 2026-10-18; the shape below PROPOSED, under review by Scape and FOUNDATIONS).** A module may declare `morphPosition` at admission, together with its **design keys**: the continuous keys its costly design depends on. A module that does not declare it never receives either call. (a) **At load**, inside the staged load (RS-6.6) and off the audio thread, it receives its patch at each of horde's morph corners (ADR-188 §2; an unused corner repeats corner A), so it may precompute a design per corner. **When a corner's patch is edited while the module is loaded,** that one corner is delivered again the same way, off the audio thread, and adopted at a block boundary. (b) **At control rate**, on the same tick as the blended `setParam` targets and before them, it receives `morphWeights(w[4], exact)`: the bilinear weights of the four corners in a fixed order, A = (0, 0), B = (1, 0), C = (0, 1), D = (1, 1) on the morph's x and y, so w = [(1−x)(1−y), x(1−y), (1−x)y, xy] (the quantum-morph field's geometry, QM-0 §2), each in [0, 1] and summing to 1; and `exact`, true only when every one of the module's **design keys** sits at the bilinear blend of its corner values, with no macro, modulation or automation offset on it. Offsets on the module's other keys do not clear it. (c) **The values stay the truth.** When `exact` is false, and for every stepped key (RS-4.3), the module renders from the values. A blend of corner designs may differ from the design at the blended values only within the tolerance the module's fidelity suite states (row A21). (d) The call is O(1), allocates nothing and takes no lock, and the slot never smooths the weights (RS-4.2). Compatible with RS-3.1 (weights are not topology) and ADR-188 (corners hold patches; the module's own XY still holds one). | R (ADR-188) / P (the shape) | Scape's brief `morph-corner-designs` (seq 1); ADR-242 |

## 5. Macros

| id | Clause | Status | Source |
|---|---|---|---|
| RS-5.1 | A module **may** offer up to **8 macros**, ordered and labelled by its presets. They are expected of modules with deep internals (Sluice, Shriek). There is no role vocabulary and no standard label set across modules. | R | ADR-169 A4; A1 |
| RS-5.2 | **Nothing is driven automatically.** horde's intents drive a module's macro, or one of its parameters, only where a preset's designer bound it, per corner, with a depth and a curve. Recommended default mappings may exist as editing shortcuts; none is locked. | R | ADR-169 A4 items 2 and 4; SPEC-MODULE-MACROS A4 |
| RS-5.3 | **Binding curve:** a binding is `[lo, hi]` or `[lo, hi, curve]`, with `curve` ∈ {`lin`, `exp`, `log`} and `lin` when absent. Every curve returns `lo` and `hi` exactly at 0 and 1. A manifest with `log` on invalid endpoints fails validation; a host that meets one anyway resolves it as `lin` and flags it. The curve applies only at the final slot → parameter step. | R | ADR-169 A3; SPEC-MODULE-MACROS §4–§5 |
| RS-5.4 | **Macros act on the whole patch**, not on a corner or snapshot, so moving one never makes a snapshot a draft. A module with its own XY (Sluice) keeps macro mappings per patch and ranges per snapshot corner. | R | ADR-188 item 5; ADR-188 A1 |
| RS-5.5 | **Sluice resolves its own macros** (order plus label, its own breakpoint curves). horde sends the macro value; Sluice applies its mapping. A module that does not resolve its own macros has horde resolve them through SPEC-MODULE-MACROS §5. | R | ADR-169 A1; A3 |
| RS-5.6 | **DAW exposure** is the slots only, named `FX<n> Macro <k>`. The preset's label stays UI-only. Module internals get no host parameter ids. horde 2 allocates these ids from its own manifest (ADR-186), so the legacy reservation of ids 300–331 does not carry over. | R (names, slots-only) / P (ids from the h2 manifest) | SPEC-MODULE-MACROS §6 and A4 item 5; ADR-169 A1 ruling 4; ADR-186 |
| RS-5.7 | **Round trip.** A module preset's macros (order, labels, bindings, curves) survive save and load exactly. Admission row A12 tests it. | R | B439 §2 (macro row) |

## 6. Presets, morph, state and history

**Three separate things, kept separate.** The module ABI carries **state**: what a live instance
holds and how a load reaches it (OQ #32 slice 2). **Presets** are FOUNDATIONS' scoped-preset
cascade (`core/include/foundations/preset_cascade.h`, DECISIONS #70), a main-thread facility the
audio path never calls. **History** has no FOUNDATIONS clause at all; it is horde's own (ADR-160,
B389). This section aligns horde's corner presets with the cascade rather than inventing a third
scheme.

### 6.1 Module patches at horde's morph corners

| id | Clause | Status | Source |
|---|---|---|---|
| RS-6.1 | A module's own XY (Sluice's four corners) morphs the snapshots of **one** patch. horde's morph XY may hold a **different** module patch at each of its corners. | R | ADR-188 items 1–2 |
| RS-6.2 | Where the corners' patches have **parity**, their evaluated parameters blend. Where they do not, the live patch quantum-flips with a hysteresis band of about 0.1. The module's dry/wet glides across a flip; Sluice's fixed four (Dry/Wet, Width, Time, Tune) stay continuous. | R | ADR-188 items 2–3 |
| RS-6.3 | Per-corner preset identity is **opt-in**. The default is one preset locked across all four corners. At most two live instances of a module exist at once, and only during a flip or a spill-over tail. | R | ADR-169 A1 rulings 2–3; ADR-188 consequences |
| RS-6.4 | **Parity is the module's call.** A module whose presets all share one structure has parity between every pair, and its flip-class parameters carry every discontinuity. A module with a structure (Sluice) exports the predicate (Sluice's `glides(a, b)`: the same structure and the same macro mapping), and the slot asks it. | P | ADR-188 item 2; Sluice `netcore/include/sluice/node.h` |
| RS-6.5 | The module's XY position at each corner is exposed to horde as parameters, modulatable and mappable to horde's macros. Tails across a flip, a bypass and a patch load follow the user's spill-over setting. | R | ADR-188 items 4 and 6 |

### 6.2 The FX-slot preset mapping

Nothing ratified gives a file format for a module patch at a corner. SPEC-MODULE-MACROS §4 has a
`ModulePreset` (internal defaults plus macro slots), which Bulwark (`core/include/bulwark/presets.h`)
and Shriek (`presets/shriek-module-presets.json`) already write. Sluice keeps its own patch
format (Sluice `docs/horde/SLUICE-IN-HORDE.md` §13). **The proposal (P): a module patch at a corner is a FOUNDATIONS
cascade `Preset` at the module's scope,** following OQ 10's direction of scoped presets derived
from addressing:

| Cascade field | What a module patch puts there |
|---|---|
| `scope` | the module's address prefix, for example `fx.bulwark` |
| `values` + `basis` | morph-class scalars, stored absolutely with their basis (the cascade's L0008 rule: identity is exact by construction) |
| `absolute` | flip-class (stepped) values, and the module's own structure as one `kBlob` key: a Sluice patch, or a `ModulePreset`'s macro slots (`<scope>.macros`). horde stores a blob verbatim and never parses it. The module validates it. |
| `routes` / `symbolic` | none in 1.0. horde's intent bindings live on horde's corner, not in the module patch (SPEC-MODULE-MACROS §4 `Corner.bindings`). |
| `payloads` | adaptive state, only for a component whose OQ 24 policy is "snapshot into preset". None in 1.0 (RS-6.13). |
| (excluded) | Device-class keys. They are saved once per instance, outside every corner (RS-4.5). |

The horde preset then holds, per corner, `fx[<module>] = {origin_id, module_version, preset}`,
embedded by value, with `origin_id` for optional re-linking (SPEC-MODULE-MACROS §8, R). It also
holds one `device[<module>]` block of Device keys and the macro tier state (SPEC-MODULE-MACROS
§4 `HostSlotState`, R). `module_version` is the module's manifest lock hash (B428), so a load can
tell when a module's keys have changed.

**Where this diverges (P):** `ModulePreset`'s `internal_defaults` become cascade `values` and
`absolute`, and its `slots` become one blob. Bulwark and Shriek keep writing their format, and
horde converts at import. This avoids a third preset scheme. Q7 asks the human.

### 6.3 Loading state into a running module

| id | Clause | Status | Source |
|---|---|---|---|
| RS-6.6 | **One staged swap per load, at a block boundary.** horde does the expensive work off the audio thread: it parses, validates, resolves addresses, and asks Sluice to build its engine bundle. The audio thread then adopts every module's staged state at **one** block boundary, by one call per module. A horde preset load or a non-parity corner flip never leaves the rack half-loaded across a boundary. A module never adopts a load mid-block on its own. | R (shell rule) / P (applied to modules) | B448 thread-handoff plan (ratified 2026-10-09; loads adopted at a block boundary) |
| RS-6.7 | **At that boundary**, the module's new parameters land at their loaded values with no ramp-in. Its old audio ends either in place (bound 0; Bulwark) or by a crossfade out (Sluice: 20 ms), and the module declares the bound in `LoadDecl`. During the crossfade it reports tail = max(current tail, remaining spill-over). | R (FOUNDATIONS) / P (as horde's rule) | FOUNDATIONS DECISIONS #124, #128, #129 |
| RS-6.8 | **A module with no load entry** (Shriek; undeclared `LoadDecl` is legal) is loaded at the same boundary through ordinary `setParam` calls, which ramp. horde accepts that for 1.0 and records it as the module's known difference. | P | FOUNDATIONS DECISIONS #129 |
| RS-6.9 | `loadState`'s arguments are still open (slice 3; FOUNDATIONS' questions to Shriek and Sluice are due 2026-10-22). This contract adopts whatever slice 3 rules and adds nothing to it. | R (deferral) | FOUNDATIONS ROADMAP row 32 |

### 6.4 What a module owes horde's history

FOUNDATIONS has no undo or history clause. horde's history is a shell-owned tree of **snapshots**,
one node per gesture, preset load or corner apply, held in memory with 200 nodes (ADR-160). For
horde 2 it must survive FX module edits (B389). Every clause here is **P**.

| id | Clause | Admission row |
|---|---|---|
| RS-6.10 | **Complete state.** A module's saved state holds every input to its sound: every parameter, its structure blob, its seed. Nothing outside the saved state and `reset()` changes what it renders (no hidden state). | A17 |
| RS-6.11 | **Deterministic restore.** Save then load is bit-exact. Save → load → save gives byte-identical text, and floats round-trip exactly (C99 hex, or a shortest round-trip format; never `%.6g`). | A18 |
| RS-6.12 | **Bounded size.** A module's state per corner is at most **2 KiB**. With four modules and four corners that is 32 KiB, half of B446's 64 KiB cap on state text (`kMaxPastedStateBytes`, `src/input_guards.h`, re-derived from the factory bank by `paste_cap_check`). An oversized state is refused before anything is allocated. | A19 |
| RS-6.13 | **Live state is excluded by design**, in OQ 24's vocabulary: it is never snapshotted into a preset. The table below says which policy each kind of live state takes. | A20 |
| RS-6.14 | **Edits arrive as gestures.** Every edit made inside a module's face (a knob, a snapshot capture, Sluice's randomiser drawing a new patch) reaches horde through its gesture brackets, so history makes one node per gesture. A module never changes its saved state silently. | A13 (round trip after an edit) |

| Live state | OQ 24 policy | Why |
|---|---|---|
| Tails ringing, crossfades in flight | reset on load (they end per the module's `LoadDecl`) | a tail is the old patch's sound, not the new patch's state |
| Smoothers in flight | not saved; the parameter is saved at its target | a restore opens at the value, with no ramp (RS-6.7) |
| Meters, clip latches, non-finite counters | retained through load, never saved | latches clear only when the user clears them (B225; ADR-197 rule D) |
| Adaptive state (detector envelopes, ecology envelopes, oscillator phases) | retained through load (`LoadDecl` 0) or reset (the crossfade) | the module declares which; "snapshot into preset" is out for 1.0 |
| Seeded RNG position, sample counters | rewound by `reset()`; not saved | determinism comes from the seed in the saved state |

## 7. Process lifecycle

### 7.1 Latency, block size, tails and sleep

| id | Clause | Status | Source |
|---|---|---|---|
| RS-7.1 | **Latency** is declared in whole host samples (nearest integer, ties up), with the sub-sample residual and its sign documented, not returned. Latency is a plugin constant, never a morph variable: morphable modules are zero-latency in 1.0, and a lookahead mode is a restart-time toggle. A module whose latency depends on its patch (Sluice at 2× or 4× oversampling) keeps that setting fixed from activate onwards, so a corner flip can never change it. | R (D6; the rounding is FOUNDATIONS') / P (the fixed setting) | ADR-193 D6; B429 (2); FOUNDATIONS DECISIONS #121, #127 |
| RS-7.2 | **Block size.** A module is called with any n from `min_block` to `max_block`, and the slot never calls it with 0 < n < `min_block`. Inside a live cycle it is called at n = 1, so a module with `min_block > 1` (lookahead, block FFT) is refused cycle membership as a declared rule, not discovered as a glitch. | R (n = 1 follows from ADR-128) / P (the refusal; ADR-175 is PROPOSED) | ADR-128; ADR-175; FOUNDATIONS `fx_operator.h` |
| RS-7.3 | **Tail** is declared in host samples. `kInfiniteTail` is allowed (a self-oscillating loop, or a Sluice patch with no release gate). `may_change` is set by a module whose tail follows its parameters (a reverb, a feedback network). | R (FOUNDATIONS) / P (as horde's rule) | FOUNDATIONS DECISIONS #124, #127, #128 |
| RS-7.4 | **What horde tells the host.** horde reports **one constant tail bound** and never signals a tail change. The pinned clap-wrapper maps `tail.changed` to a VST3 latency restart, with no null check. A module's `may_change` is therefore read inside horde only. horde never returns SLEEP while any module's tail rings. | R | B429 (1) and its 2026-10-05 addendum |
| RS-7.5 | **Tails survive their cables.** A module whose output cable is closing keeps ringing until it is quiet (RS-3.7). Spill-over across a flip, a bypass or a load is the user's setting (RS-6.5). Sluice enforces its own tail limits: at most 3 at once, and none past 8 s. | R | ADR-193 D5; ADR-188 item 6; Sluice `docs/horde/SLUICE-IN-HORDE.md` §10 |
| RS-7.6 | **Sleep and wake (see §12 for the tail's meaning and the wake's judge).** The slot may stop calling a module whose input cables are silent and whose output has been quiet for its declared tail. Sleep keeps the module's state, warm. On wake, parameters changed during sleep land at their targets with no ramp (through the load entry where one is declared), and the output continues with no step (RS-4.4). A module with an infinite tail that is live never sleeps. A module needs nothing special for this, beyond resuming correctly after a gap in calls. | P | B265 round 3 ("sleep keeps state and wakes without a step"); round 3b (woken modules jump to their targets) |

### 7.2 Real-time rules and threading

| id | Clause | Status | Source |
|---|---|---|---|
| RS-7.7 | **MUST be real-time safe:** no allocation, locks, syscalls or I/O in `process`, `setParam`, `reset` or the load entry; finite output under NaN and Inf injection; silence in gives silence out; tails decay to exact zero with no denormal residue. | R | B439 §3; B430 |
| RS-7.8 | **Denormals.** A module that sets FTZ/DAZ does it inside its own `process`, saving and restoring the host's flags (Bulwark's RAII guard). It never leaves flags changed, and it never relies on the host having set them. horde turns FTZ on nowhere today. Turning it on would be a default change, needing an ADR and a migration. | R (FTZ/DAZ in the bar) / P (save and restore) | B439 §3; B430 (3); B448 B1 (FTZ audit); ADR-197 rule E |
| RS-7.9 | **Non-finite guards latch and report.** A module that repairs a non-finite sample (zeroing it, flushing a loop, clearing its state) counts each event and holds a latch until it is cleared, readable from another thread. It never repairs silently. horde's own guard at the slot boundary does the same. Input validation at a boundary is exempt from the no-clamping rule; a module's designed loop limiter (Sluice's ±4 clamp) is DSP, not a guard. | R (ADR-197 rule D) / P (applied to modules and the slot) | ADR-197; B448 B1; FOUNDATIONS DECISIONS #124 |
| RS-7.10 | **Statics are warmed at prepare.** Every function-local static that `process` can reach is built in `prepare`, never on first use on the audio thread. The swarm core's table, built under a lock on first call, was the RealtimeSanitizer probe's one finding. | R (shell rule) / P (applied to modules) | B448 B3 (h2 shell rule) |
| RS-7.11 | **Deterministic:** the same input and seed give identical output; mulberry32 streams only; no wall clock. **Rate and block independent** across 44.1–192 kHz and blocks 1 … 4096. | R | B439 §3; SPEC §5.7 |

**Threading.** horde 2's shell rules are part of the thread-handoff plan the human ratified on
2026-10-09 (B448). Read as obligations on a hosted module, they say (P):

| Shell rule (R, B448) | What it asks of a module (P) |
|---|---|
| One writer per state object | Only the audio thread writes audio state. Main-thread work (`prepare`, `resolve`, building a Sluice bundle) hands its result over by one atomic exchange, and never writes what the audio thread reads. |
| No main-thread reads of audio memory | The GUI and the main thread read values, meters and latches through published atomics (Bulwark's `meters()`, Sluice's value atomics), never from working buffers. |
| Loads adopted at a block boundary | RS-6.6. |
| Bounded channels that report overflow | Any queue from the main thread to the audio thread inside a module is bounded, never blocks and never allocates. An overflow is counted and readable, never dropped silently. A call from the audio thread asking the host to wake the main thread (Sluice's `NodeHost::requestMainThread`) goes through an RT-safe entry the slot provides. |
| Statics warmed at activate | RS-7.10. |

## 8. Cost

| id | Clause | Status | Source |
|---|---|---|---|
| RS-8.1 | **CPU budget per FX module, one instance:** ≤ 2 % of a min-spec core at its defaults and ≤ 4 % at its worst settings. The FX slice for both racks together is ≤ 12 %. A module that needs oversampling may use ≤ 6 % / ≤ 10 %, but only if it also offers a cheaper mode within 2 % / 4 % and its factory presets default to that mode. | R | B439 appendix (budget approved 2026-10-04) |
| RS-8.2 | **horde measures it on its own bench:** a Release build, as a ratio to the calibration loop (B262, B236), on this Mac (M3), with an assumed factor of 1.5 to min-spec labelled in every row. It is judged at the E-6 reference (44.1 kHz, 128-sample buffer). 48 kHz is also reported; higher rates are informational. A module's own figures are indicative. | R (the method) / P (the rate reading and who measures) | B439 appendix; B262; the lead's answer to Bulwark's seq 6 (B439 row); `integrations/bulwark/response-rack-slot-status.md` §4 |
| RS-8.3 | **CPU is Layer-E:** measured and reported, never a verify gate. | R | ADR-187 §8; B439 appendix |
| RS-8.4 | **The worst case includes the module's own extras:** its spill-over tails (Sluice: up to about 3 % of a core per ringing tail), and the second live instance during a flip. A sleeping module costs about nothing; static nodes make sleep the main CPU lever. | P | Sluice `docs/horde/SLUICE-IN-HORDE.md` §10; B265 round 3 (8.6 vs 15 CPU points with sleep) |
| RS-8.5 | **Memory:** a worst-case figure per instance (RS-2.6). Static nodes keep every module resident, so memory adds up even when a module is silent. A Sluice delay at 96 kHz × 4 is 12.9 MiB. | R (SHOULD in the bar) / P (MUST here) | B439 §4; Sluice `docs/horde/SLUICE-IN-HORDE.md` §10 |

## 9. What is NOT in the rack

| Item | Where it lives | Status | Source |
|---|---|---|---|
| **The master limiter** | Bulwark's limiter face: one instance, fixed as the last stage before the output, never a rack slot. Its 1.5 ms lookahead is constant and outside the morph. It is admitted against this contract's ABI, gain, RT and determinism clauses, but not §3. Its CPU budget is ≤ 0.5 %. | R | B438; ADR-193 D6; B439 appendix |
| **The master strip** | The mixer page: limiter on/off, Ceiling (default **−1.0 dBFS**), Release (auto or ms), a gain-reduction meter, the clip latch, and Volume. Volume IS the limiter's cut-only `io.outGain` (−∞ … 0 dB), one control, never two. | R | B402; B438; ADR-195 ruling 1; B435 A1 |
| **Mixer taps and gain staging** | The mixer page binds to each module's `io.inGain` / `io.outGain` and reads the meter taps (RS-4.7). It adds no gain stage of its own at a module's I/O. | R | B435; B225; B402 |
| **FX-C (Shriek)** | **Still open:** a fixed post-stage, or a slot in the matrix (H2-PLAN item 26; B318). Shriek meets this contract either way. If FX-C is a fixed post-stage, §3 (cables and order) does not apply to it, and the rest does. | open / P | `docs/H2-PLAN.md` item 26; B318; ADR-092 amendment; ADR-170 |
| **The legacy rack** | ADR-095's rack-owned mix, the legacy `SlotContract` and `slotcontract_check` stay with the frozen legacy shell (§2.3). | R | ADR-186; ADR-095; B281 |

## 10. Open questions for the human

Each question is a PROPOSED clause that needs a ruling. The lead's recommendation is given, and
the human rules.

| # | Question | Clauses | Recommendation |
|---|---|---|---|
| Q1 | Should the slot's code-level interface be FOUNDATIONS' FX-operator ABI (OQ #32), with shim modules admitted before it freezes? | §1; RS-2.3, RS-2.5, RS-6.7, RS-7.3 | **Yes.** It is the lead's working direction (B450). All three shims map onto it by renaming (§1.1). Admit a shim when its gaps are only renames. |
| Q2 | Should the static-node model become the product rack, as well as the lab's? That covers resident modules, cable-weight morphing, sleep with state kept, and equal-gain crossfades. And do ADR-175 (n = 1 in a cycle) and ADR-173 (parameter classes) apply to hosted modules? | RS-3.1, RS-3.5, RS-3.6, RS-7.2, RS-7.6 | **Yes.** Round 3 removed the clicks the human heard (B265). Equal-gain is a default change under ADR-197 rule E: it needs an ADR, and its migration is empty, because no horde 2 state has been saved yet. |
| Q3 | Should the click ceiling for a module's flip-class switches and wakes be 3 dB on the B265 metric? | RS-4.4 | **Yes, 3 dB.** Revisit it after a capped listening batch (ADR-197). |
| Q4 | Should a module that cannot switch a stepped parameter cleanly declare `gate`, so that the slot ducks the wet path around the swap? | RS-4.3 | **Yes.** It is the round-2 brief's "flip-with-dip", and it uses OQ #33's existing `gate` kind. |
| Q5 | Should "off" belong to the router, with ADR-095's rack-owned mix not extended to hosted modules, and a module's own bypass key not used for "off"? | §2.3; RS-2.4, RS-4.8 | **Yes.** A rack mix around a hosted module doubles its dry/wet, and around a multiband Bulwark it breaks ADR-195. |
| Q6 | Should a module whose latency depends on a setting (Sluice's oversampling) have that setting fixed at activate, as Device class, and never different between corners? | RS-7.1 | **Yes.** This is ADR-193 D6 applied: a latency setting is a restart-time toggle. |
| Q7 | Should a module patch at a corner be a FOUNDATIONS cascade `Preset` at the module's scope, with `ModulePreset` converted at import, rather than a third format? | §6.2 | **Yes.** It reuses the cascade's exact-identity rule and OQ 10's addressing. It changes nothing for Bulwark or Shriek, whose files convert. |
| Q8 | Should a module with no load entry (Shriek) be loaded by ramped `setParam` calls in 1.0, rather than being required to add `loadState`? | RS-6.8 | **Yes, for 1.0.** FOUNDATIONS made an undeclared `LoadDecl` legal. List it as the module's known difference. |
| Q9 | Should a module's state be capped at 2 KiB per corner? | RS-6.12 | **Yes.** Four modules × four corners is then half of B446's 64 KiB cap. Re-derive the figure once Sluice's largest factory patch is measured. |
| Q10 | The history clauses: complete state, deterministic restore, bounded size, live state excluded, and edits as gestures. | RS-6.10–6.14 | **Yes.** Each has an admission row (A13, A17–A20). |
| Q11 | Should the memory figure be a MUST for hosted modules, not a SHOULD? | RS-2.6, RS-8.5 | **Yes.** Static nodes keep every module resident. |
| Q12 | Should horde measure every module's CPU itself, judged at the E-6 reference? | RS-8.2 | **Yes.** It is what our seq-12 answer told Bulwark. |
| Q13 | Where should the admission test live? | §2.2 | **A new check in horde's `tools/`**, wired into `./verify` when it lands (ADR-180 §1). Build it with the horde 2 shell (B398), when the first module can be hosted. |

## 11. Per-module readiness

Read from each module's own repository on 2026-10-09. The paths are relative to that repository.
**met**: the evidence shows it. **partial**: some of it is there. **unknown**: no evidence either
way. **not met**: the evidence shows it is missing. **slot**: the slot provides it, which is
allowed. Scape has a ratified browser lab, and its C++ core and hosting have not started
(`README.md` status table), so every code row reads **not met**.

| MUST | Bulwark | Sluice | Shriek | Scape |
|---|---|---|---|---|
| ABI lifecycle: `prepare` / `reset`, reset bit-identical (A2) | met: `core/include/bulwark/abi.h` | met: `netcore/include/sluice/node.h`; `netcore/tests/node_contract.cpp` | met: `mawcore/include/mawcore/abi.h`; `mawcore/tests/abi_test.cpp` | not met |
| Any block size, `min_block` declared (A3) | met: `minBlock() = 1`, chunking tested | met: `minBlock() = 1`, split-render test | met: `minBlock() = 1`, `mawcore/tests/parity_test.cpp` | not met |
| `latency()` declared (A4) | met: 0 (compressor) | partial: declared, but varies with oversampling (RS-7.1) | met: 0, residual documented | not met |
| `tail()` declared (RS-7.3) | met: `tail()`, `tailChanged()` | partial: semantics confirmed with FOUNDATIONS (#128); no `tail()` on the Node | not met: no tail declaration | not met |
| `channels()` = 2 | met | met | met | not met |
| Address-keyed parameters with a registry | partial: `setParam(key)`, no handle | partial: `idOf(address)` with a 0 sentinel | partial: `registry()` + `setParam(address)`, no handle | not met |
| Manifest with a class per parameter (A1) | met: `core/include/bulwark/param_table.h` `kMorph` (Continuous / Jump / Device) | partial: `docs/horde/sluice-manifest.json` has morph laws and scopes, not OQ #33 classes | partial: stepped vs scalar in `mawcore/include/mawcore/param_table.h`; no participation | not met |
| I/O gain pair and meter taps (A6) | met: `io.inGain` / `io.outGain`, `meters()` | met: aliases of ids 8 / 9; `inputMeter()` / `outputMeter()` | slot (its D-050) | unknown (R20 trims are its open M2 call) |
| Macros: ≤ 8, ordered, labelled, curves (A12) | partial: 4 ordered slots, curves `lin` and `log` only, no `exp`; a comment still calls the curve "pending horde's ruling" (`core/include/bulwark/presets.h`) | met: Macro 1–8; it resolves its own (ADR-169 A1) | met: 8 slots, `lin` / `exp` / `log`, `tools/macro_check.mjs` | not met |
| Module preset format (§6.2) | met: `ModulePreset`, `core/include/bulwark/presets.h` | partial: its own patch format (accepted; converts to a blob) | met: `presets/shriek-module-presets.json` | not met |
| Load entry (RS-6.7) | met: `loadState`, in place | met: `setPatch`, 20 ms crossfade | none (legal; RS-6.8) | not met |
| Non-finite guard latches and reports (A9) | met: sanitize + state guard; `nonfinite` count, latches (`core/include/bulwark/common.h`) | unknown: a loop NaN flush exists (`docs/horde/SLUICE-IN-HORDE.md` §10); no counter found | not met: the watchdog clears state with no counter (`mawcore/include/mawcore/core.h`) | not met |
| FTZ/DAZ and exact-zero tails (A5) | met: FTZ RAII (`core/include/bulwark/ftz.h`) | unknown | unknown: no FTZ found; tail gate in `./verify full` | not met |
| Real-time safe (A10) | met: allocation only in construction and `prepare`; an `allocInProcess` plant | met (declared in `node.h`); stability fuzz not gated (its Q-024) | met (declared): allocates only in `prepare` | not met |
| Deterministic (A15) | met: 192 exact goldens; B230 isolation test | met: parity with the lab at 1e-6 | met: C++ goldens | partial: the lab is seeded and deterministic; no C++ |
| CPU on horde's bench (RS-8.2) | unknown: indicative figures in `docs/cpu/` | unknown: its own bench (about 3 % per tail) | unknown: projected about 9.7 % per instance (B262); needs a cheaper mode | unknown |
| Memory figure (RS-2.6) | unknown | met: `patchBytes` / `engineBytes` / `residentBytes` | unknown | unknown |
| State complete, exact restore, bounded (A17–A19) | unknown | unknown | unknown | not met |

**Summary.** Bulwark is closest: 13 of 18 rows met, with gaps in macro curves, the parameter
handle, CPU, memory and the history rows. Sluice has 9 met and the most partials: latency, tail, the parameter
handle, manifest classes and the preset format. Shriek has 8 met, and needs a tail declaration and a
reporting NaN guard. Scape is a lab and is not yet hostable. Nothing here is a fault in the
modules: the contract did not exist when they built their shims.

## 12. Amendments

### 12.1 ADR-218 (2026-10-13), on Bulwark's readiness notice (seq 18)

Ruled by the human: "Go with your recommendations on all four." Where a row above and this section disagree, this section holds.

**Why.** The B265 click metric divides a transition's second-difference energy by that of the steady signal around it, and a sine's own second difference falls with the square of its frequency over the sample rate. One switch therefore reads far higher on a low probe or at a high rate. Rows A11 and RS-4.4 named no frequency, level or rate; the 3 dB figure was calibrated on the round-3 lab's chord at 16 kHz. Bulwark measured at 48 kHz from 30 Hz to 5 kHz and read 0.1 dB at 1 kHz and 8 dB at 30 Hz for one flip.

| Row | As amended |
|---|---|
| **RS-4.4** | **The 3 dB ceiling is suspended as a pass/fail rule** until the first listening batch (ADR-201, ADR-209 item 13) sets a ceiling that does not depend on the probe's frequency or the sample rate. The click reading stays a REPORTED figure (below). |
| **A11**, flip-class clause | **Pass rule (structural):** the module declares how each flip-class key switches: an equal-gain crossfade of a stated length (RS-3.6), or a declared `gate` (RS-4.3). A bare jump of a signal-path or detector state is not admitted. **Reported, never judged:** the B265 click reading on two pinned probes at 48 kHz: the lab's chord (sines at 55, 110, 130.81 and 164.81 Hz, equal amplitude, the sum peaking at -6 dBFS) and a 1 kHz sine at -6 dBFS; at the module's defaults and at each factory preset; both directions; with, beside each, the reading of an ideal offline equal-gain crossfade of the two steady renders over the same length. The morph-class clause and the Device-key clause are unchanged. **Must-fail control:** a flip-class key switched with its crossfade removed is refused by the declaration check. |
| **A16** | **Judge:** an identical instance that never slept, given the same input. From the wake on, the two outputs agree to a stated tolerance, and parameters changed during sleep are at their targets. The click metric is not the judge: a module's own attack on a note out of silence reads on it with no wake involved. The must-fail control is unchanged. |
| **RS-7.6** | The **declared tail** a slot sleeps on may be "the time until the instance is at rest": output zero AND every envelope recovered. A module whose state outlives its audible output (a compressor's gain reduction) declares that. The bound on AUDIBLE output that horde reports to a DAW is a separate constant of the module. "The output continues with no step" is judged as A16 says. |
| **A4** | The frequency-dependent group delay of a minimum-phase crossover belongs in the **declared residual**. A module with modes declares the residual per mode. |

**What the modules are asked.** Bulwark: crossfade `comp.scOn` and `comp.detector`, which switch a detector with no crossfade today; declare its multiband residual. Every module: declare each flip-class key's switch method and length.

### 12.2 ADR-234 (2026-10-17), on Bulwark's brief `switch-classes` (seq 20)

Q1 was ruled by the human ("go with B for Bulwark Q1"); Q2 and Q3 are the lead's amendments, ratified by the merge of their PR. Where a row above and this section disagree, this section holds.

| Row | As amended |
|---|---|
| **A11**, flip-class clause | A flip-class key may also switch by a **declared seeded jump**: the state that starts running is seeded from the value being applied at the switch sample, so the applied gain (or level) does not step and only its slope changes. A seeded jump is admitted only when the module's switch table lists it with its reported click reading on the wide probe set, and the module's tests hold a must-fail control: a planted unseeded jump on the same key reads red. Bulwark's `comp.autoRel` and `lim.autoRel` are the first two (0.04 and 0.06 dB on the wide set). |
| **A4** | Replaces 12.1's A4 row. FOUNDATIONS' `LatencyDecl::residual` means the rounding error of a whole-sample latency, bounded to [0, 0.5] samples. A frequency-dependent group delay (a minimum-phase crossover's) is **not** latency and is not a residual. The module reports the latency the host should compensate (0 for Bulwark's multiband compressor) and states the group delay, with its frequency range, in its manifest or notice outside `OperatorProperties`, until FOUNDATIONS offers a field for it. horde compensates only declared latency, never a group delay. |
| **Morph classes (Bulwark)** | `comp.scOn` is a **Device** key: set by a preset's designer and never morphed, like `comp.bypass`. A morph between presets that differ in it holds one value. `comp.scHz` stays continuous and morphable, so a preset can morph the sidechain corner down to 20 Hz to reach "effectively off". No crossfade of `comp.scOn` is owed. |

### 12.3 ADR-237 (2026-10-17), on Shriek's brief `maw-004` (seq 3) and its audit notice (seq 2)

Ruled by the human, 2026-10-17, from the lead's recommendations.

| Row | As amended |
|---|---|
| **The rack's rate (new)** | **horde's FX rack runs at the host rate.** The engine hands the rack one stereo pair at the host rate (spec RC-5), and a module that needs oversampling does it internally, as Sluice does. There is no rack-level oversampling. |
| **B439, the oversampling exception** | A module's factory presets may default to its oversampled mode within 4 % (the worst-case figure), provided it offers a cheaper mode within the standard 2 % / 4 % (module 1.0 bar, amended). |
| **RS-7.1** | A module's oversampling latency, set by a Device-class setting fixed at activate, is declared in whole host samples and held until the next activate, as for Sluice. It is admitted in 1.0 although the module is morphable: the latency is a plugin constant per activation, never a morph variable. Shriek's 2× halfband (about 4 host samples) is the first case. |

### 12.4 ADR-242 (2026-10-18), on Scape's brief `morph-corner-designs` (seq 1)

Ruled by the human, 2026-10-18 ("Offer it, optional"), against the lead's recommendation to wait until after 1.0.

| Row | As amended |
|---|---|
| **RS-4.10 (new)** | An optional morph-position call: the corner patches at load, and the bilinear weights with an `exact` flag at control rate. The values stay the truth. |
| **A21 (new)** | Its admission test, with a permuted-weights control. |
| **The ABI map** | A new optional entry, proposed to FOUNDATIONS. Until FOUNDATIONS rules, the shape is horde's proposal and a module's local shim carries it. |
| **RS-4.10, after Scape's review (seq 3, 2026-10-18)** | Three refinements, accepted by the lead within the ruling: the corner map is fixed (A = (0, 0), B = (1, 0), C = (0, 1), D = (1, 1), QM-0 §2's weights); `exact` is judged over the module's declared design keys only; an edited corner is delivered again through the staged load. The shape stays PROPOSED until FOUNDATIONS answers; then it goes to the human for ratification (B513). |
