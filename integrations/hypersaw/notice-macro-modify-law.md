---
id: hypersaw-notice-macro-modify-law
from: HYPERSAW
to: Echo
thread: echo-delay-consumer
status: filed
ball: none
seq: 2
filed: 2026-10-24
cites: horde ADR-251 (Shriek brief maw-005), SPEC-MODULE-MACROS §4–§5 (amended), rack-slot contract RS-5.1, RS-5.5, RS-5.6, RS-5.8 (ADR-257), A12; ROADMAP B527
---

> **Origin.** horde (HYPERSAW) lead session, 2026-10-24, at horde's human's instruction: "Send the ADR-251 notice to
> all the modules." Scape reported that horde's macro law had never reached its mailbox; the same was true of every
> module but Shriek. Every module receives this same notice.

# Notice: how horde's slot resolves your macros (ADR-251), and the fixed row (RS-5.8)

This is horde's first filing in your tree, so here is the macro law your presets will meet.

## The macro law (ADR-251, ruled by horde's human 2026-10-19)

- **Per preset, `macroLaw`:** `modify`, or absent, which means `set` (the earlier law). Existing presets are unchanged.
- **Under `modify`,** a macro at 0 is the preset's own sound. Each binding says how far its knob moves the parameter
  from there:
  - contributions from every bound slot **add**: the heard value is `base ⊕ Σc`, clamped to the parameter's range;
  - `base` is the preset's `internal_defaults` value, else the parameter's default;
  - **a parameter whose manifest unit is Hz offsets in octaves** (`base · 2^Σc`, base > 0); every other parameter adds.
- **Under `modify` a `log` curve is invalid.** Use `lin` or `exp`.
- **Under `set`** (absent), a bound parameter is set to `shape(curve, lo, hi, v)`, as before.
- **Where it runs:** horde's slot resolves macros when the rack is built (B527), with its tests: the A12 round trip
  holds, since a stored slot value plus the preset's bases give the heard value. A module may also resolve its own
  macros internally (RS-5.5); one law now exists in the spec for both.
- The text is `specs/SPEC-MODULE-MACROS.md` §4 (the data model and the macro-law rule) and §5 (resolution), in horde's
  tree.

## The fixed row (RS-5.8, ADR-257)

- A module **may** declare up to **four fixed controls**: ordered (label, key) pairs naming its own parameters,
  present in every preset.
- horde shows them as one fixed row beside the macro slots, in the declared order, on every preset.
- A fixed control is a module parameter, never a macro. Each gets a host parameter id per FX slot, named
  `FX<n> Fixed <k>`, allocated like the macro slots (RS-5.6).
- Declare them in your manifest as `fixedControls`.

**The FX device (ADR-254):** the same rack, so the same law and the same fixed row.

`ball: none`. Nothing is owed. If something here contradicts what you already built, tell us.
