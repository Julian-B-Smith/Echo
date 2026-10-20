# Module I/O gain — one contract for every hosted FX module (B435)

**Status: APPROVED by the human, 2026-10-04** ("Approved"). It answers B435: "we should
standardize this for all modules so it can be controlled via the gain staging interface
in Horde's proposed mixer page". It becomes part of the rack slot contract
(`fx-slot-contract.md`, B50/B281). It is offered to FOUNDATIONS' module manifest.

**Why one contract.** horde's mixer page (B225, B402) stages gain at every point of the
signal chain. If each module invented its own trim, the mixer could not drive them
uniformly, and a module's trim and the mixer's could stack at one point. The law below
is the one Sluice already built for its standalone and its node. horde adopts it rather
than inventing a second ("reduce, never invent").

## The two controls

| | Input Gain | Output Gain |
|---|---|---|
| **manifest key** | `io.inGain` | `io.outGain` |
| **meaning** | a true trim before everything the module does | a trim after everything the module does |
| **unit / range** | dB, −24 … +24 | dB, −24 … +24 |
| **default** | 0 dB, exactly unity (bit-identical to no trim) | 0 dB, exactly unity |
| **normalised law** | dB = 48·(v − ½), v ∈ [0, 1]; v = ½ is exactly 0 dB | same |
| **smoothing** | a change ramps linearly in amplitude over 20 ms. It is stated in seconds and converted at the current sample rate (ADR-009). | same |
| **at load / prepare** | opens AT the saved gain, with no ramp in | same |
| **scope** | global per module instance: not per patch, not per morph corner. A patch load or corner flip keeps it. | same |
| **morph class** | Device (never blended, never quantum-flipped) | Device |

**Placement rules.**
- **Input** sits before every path inside the module, including the dry path, any
  feedback injection, and every detector or follower (a compressor's sidechain, a
  gate's key). So Input changes how hard the module is driven, not only its level.
  A gain *after* the module is never a substitute for it.
- **Output** sits after every path, including the dry/wet mix, the output stage and any
  tail that rings after the input stops.

**Naming.** These are **Gain** controls, per the B376 naming ruling: Gain is a stage's
multiplier, Level is a contribution into a mix, and Volume is reserved for the master
output.

## Metering

Every module publishes two **meter taps**: one directly after Input Gain, and one
directly after Output Gain. Each tap carries a peak value and a **clip latch** (B225:
an over latches until the user clears it). horde's mixer page reads these taps. The
module computes them on the audio thread without allocating. Display rate and feeding
follow the GUI's feed-gating rule (a ported roundup item): no feed while nothing
visible reads it.

## Who provides the controls

- **A module that declares `io.inGain` / `io.outGain`** in its manifest owns them, with
  the law above. horde's rack slot adds **no** gain stage of its own at those points,
  and the mixer binds to the module's keys. This is the normal case for Sluice, MAW,
  Scape, Bulwark and ECHO.
- **A module that does not** (a simple built-in, or a third-party node) gets the
  identical pair from its rack slot, applied at the same two points. The mixer cannot
  tell the difference.
- **Never both.** A slot-provided gain and a module-provided gain at the same point
  would be redundant. The slot checks the manifest and supplies only what is missing.
  This is checked when a module is admitted to the rack.

## Bypass

- A **host-level bypass** of the whole plugin passes the input through untouched.
- A **module "off"** inside horde's rack removes the module's processing. It KEEPS the
  slot's gain staging, so that switching a module off and on does not jump the mix
  level. When the module is the gain owner, the slot holds the module's current gains
  across its off state.

## Acceptance tests (rack admission and the mixer lab)

1. At 0 dB both trims are bit-identical to an unprocessed pass.
2. A step from 0 to +6 dB is a linear amplitude ramp over 20 ms ± one sample, at
   44.1, 48 and 96 kHz.
3. After prepare at a saved +6 dB, the first sample is already at +6 dB.
4. A patch load and a morph-corner flip leave both gains unchanged.
5. Input Gain at +12 dB measurably changes a level-dependent module's output (a
   compressor's gain reduction, a saturator's harmonics, a feedback loop's level),
   proving placement before the detectors. Output Gain at +12 dB scales only level.
6. A module that declares the keys gets no slot gain, and one that does not gets
   exactly one pair, checked by a planted double-gain control.
7. Both clip latches set on an over and hold until cleared.

## Amendment A1 — the limiter's output is cut-only (approved by the human, 2026-10-04)

The amendment follows Bulwark's reply (horde `integrations/bulwark/reply-io-gain.md`, their
option 1) and the human's master-limiter ruling (B438: "Approved, master only for 1.0").

- **A limiter face's `io.outGain` is attenuation-only:** −24 … 0 dB, with the same law, ramp
  and placement. At v ≥ ½ it is exactly 0 dB. A limiter guarantees a ceiling, and a boost
  after it would break that guarantee, so this is a range restriction for one module class,
  not a new placement.
- **The master limiter (B438)** is the one limiter in horde 1.0: Bulwark's limiter face, fixed
  as the last stage before the output, never a rack slot. On the mixer page's master strip,
  its `io.outGain` IS the master **Volume** (cut-only, so nothing after the limiter passes the
  ceiling). It follows A1's law over −24 … 0 dB and adds a **−∞ mute detent at v = 0**, as
  accepted in B438, so its full range is −∞ … 0 dB. It is one control, never two.
- Every other module, including Bulwark's compressor and OTT faces, keeps the standard as
  written above.

**Added acceptance test.** With the limiter on, at any Ceiling, Volume and input level (+24 dB
drive), no output sample exceeds the Ceiling. A planted +6 dB post-limiter boost must fail
this test.
