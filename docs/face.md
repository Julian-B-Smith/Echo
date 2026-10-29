# Echo — the face and the expanded view

Echo specifies its screen; horde's FX screens workshop draws it in house style (module 1.0 bar row 16,
B336/B531, D-028). **The GUI review is horde's.** This is the spec the drawing starts from, not a drawing.

- **Names, ranges, defaults, laws:** `spec/params.json` is the one source (SPEC §9). This file never restates
  a value; it only says which key sits where, in what order, and as what kind of control. A key named here
  that is not in `params.json`, or a `params.json` key missing here, fails `./verify`
  (`tools/check_manifest.py`).
- **Keyboard:** every one of the 29 keys is reachable from the keyboard, in the keyboard order below
  (1 … 29, each number once): the face first, left to right, then the expanded view, top to bottom.
- **What the DAW sees:** horde shows the DAW the preset's macro slots by default (SPEC-MODULE-MACROS §6),
  not these controls. Exposing more keys is horde's later choice, never Echo's (ADR-203).

## The laws (ADR-203: every key has a display name and a normalised law)

A normalised value `v` runs over [0, 1] and maps onto the key's range and back. Four laws, by the key's `law`:

| law | value from `v` | notes |
|---|---|---|
| `lin` | `x = min + (max − min)·v` | |
| `log` | `x = min·(max/min)^v` | geometric; every log key has `min > 0` |
| `io` | `dB = 48·(v − ½)` | `v = ½` is exactly 0 dB (B435); the two I/O gains, −24 … 24 dB |
| `step` | `v = index / (n − 1)`, back `index = round(v·(n − 1))` | `n = max − min + 1`; both ends exact (poll ruling 2026-10-28); `mod.seed` has no labels |

`v = 0` is exactly `min` and `v = 1` exactly `max` in every law. The check is `paramLaws` in the core suite
(`core/tests/core_tests.cpp`), run by `./verify`, with planted faults for a key with no law, a key with no
display name, and a law that does not round-trip. The code is `echo::toNorm` / `echo::fromNorm`
(`core/include/echo/params.h`); nothing on the audio thread calls them.

## The face

Eight keys, left to right: the time controls and the Routing switch, then horde's fixed four
(`fixedControls` in `params.json`: Feedback, Tone, Mod Depth, Mix, in that order).

**Sync, Time and Division share one place.** The Sync switch sits beside it; when Sync is free the place shows
the Time knob, when synced it shows the Division selector. Both keys keep their own keyboard number.

| keyboard | key | name on the face | control | group |
|---|---|---|---|---|
| 1 | `time.sync` | Sync | switch | Time |
| 2 | `time.ms` | Time | knob (shown when Sync is free) | Time |
| 3 | `time.div` | Division | selector (shown when synced) | Time |
| 4 | `route.mode` | Routing | selector | Routing |
| 5 | `fb.amount` | Feedback | knob | Fixed four |
| 6 | `filt.loop.lpHz` | Tone | knob | Fixed four |
| 7 | `mod.depthMs` | Mod Depth | knob | Fixed four |
| 8 | `mix` | Mix | knob | Fixed four |

**Tone** is the fixed-control label for `filt.loop.lpHz` (its display name is Loop High Cut, so the expanded
view does not repeat it).

**Clear** is not a key (it is the one declared action in `params.json`, `actions`): a button on the face, after
Mix. It has no keyboard number among the 29; the keyboard reaches it right after Mix (8) and before the expanded
view (9). A performance event: never automated, no history.

## The expanded view

Four groups, top to bottom, continuing the keyboard order.

| keyboard | key | name | control | group |
|---|---|---|---|---|
| 9 | `time.offL` | Left Offset | knob | Time |
| 10 | `time.offR` | Right Offset | knob | Time |
| 11 | `time.change` | Time Change | selector | Time |
| 12 | `time.law` | Repitch Law | switch | Time |
| 13 | `time.tempoMode` | Tempo Follow | selector | Time |
| 14 | `time.glide` | Glide | knob | Time |
| 15 | `time.xfadeMs` | Crossfade | knob | Time |
| 16 | `filt.in.on` | Input Filter | switch | Filters |
| 17 | `filt.in.hpHz` | Input Low Cut | knob | Filters |
| 18 | `filt.in.lpHz` | Input High Cut | knob | Filters |
| 19 | `filt.loop.on` | Loop Filter | switch | Filters |
| 20 | `filt.loop.hpHz` | Loop Low Cut | knob | Filters |
| 21 | `filt.out.on` | Output Filter | switch | Filters |
| 22 | `filt.out.hpHz` | Output Low Cut | knob | Filters |
| 23 | `filt.out.lpHz` | Output High Cut | knob | Filters |
| 24 | `mod.rateHz` | Mod Rate | knob | Modulation |
| 25 | `mod.shape` | Mod Shape | switch | Modulation |
| 26 | `mod.spread` | Mod Spread | knob | Modulation |
| 27 | `mod.seed` | Mod Seed | knob (integer, shown as a number) | Modulation |
| 28 | `io.inGain` | Input Gain | knob | I/O |
| 29 | `io.outGain` | Output Gain | knob | I/O |

The filters read as three stages (Input, Loop, Output), each an on/off switch then its cuts. The Loop stage has
one cut here because its high cut is the face's Tone.

**I/O is Device class:** the two gains never morph and never appear in a preset (RS-4.5); a preset load or a
corner flip leaves them alone.

**Count:** 8 on the face and 21 in the expanded view are the 29 keys, each once.
