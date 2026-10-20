# Module macro tiers — spec recommendation

**Status:** ADOPTED as amended. Amendments 3 (curves) and 4 (roles retired) are RULED by the human (ADR-169 A3, A4). Where the body below still describes role-keyed slots (§2–§3, §9–§10, §12.4), Amendment 4 governs. (Header updated 2026-10-09 with the human's approval, ADR-203.)
**ball:** horde
**Parity oracle:** `horde-module-macros.html` (browser prototype, 2026-09-15)
**Scope:** applies to every modular FX module hosted in HORDE (the Roar-derived shaper, the allpass/delay rack) and is intended to be lifted unchanged into FOUNDATIONS later.

> **Amendment 4 (2026-10-05, RULED by the human; ADR-169 A4). Roles are retired; read this spec
> through it.**
> 1. **No role vocabulary.** There is no `Amount` / `Tone` / `Motion` / `Regen` enum. A module's
>    macros are **slots 1…8, bound by ORDER, carrying the module preset's own LABEL**. This is ADR-169
>    A1's ruling for Sluice, generalised to every module. There is no standard label set across
>    modules. A module may define its own macro conventions (Sluice has; Shriek may).
> 2. **Macros are optional.** A module offers macros when they make its surface usable for people who
>    only handle presets. The human expects them of modules with deep internals, Sluice and Shriek
>    above all. There is no required count. "Exactly four, each mapped" is gone.
> 3. **Nothing is driven automatically.** horde's intents (ADR-192: Tone, Space, Time, Motion, plus
>    four named slots) drive a module's macro or parameter ONLY where a preset's designer bound it, per
>    corner, with a depth and a curve (A3). horde may offer recommended default mappings as editing
>    shortcuts (for example "Time → this module's time macro"), but nothing is locked in.
> 4. **Module parameters are bindable.** A designer may bind horde's intents directly to a module's
>    parameters, not only to its macros. This supersedes §2 rule 5's "never corner-bound". DAW
>    exposure stays limited to the slots (§6), so the host-facing surface stays small.
> 5. **DAW names.** Wherever this spec says a slot is keyed or named by role, read slot index. The CLAP
>    name is `FX<n> Macro <k>`, and the label stays UI-only (§6's rule, with the index in place of
>    the role). Reserved ids keep their numbers (ADR-169 A1: `300 + 8·host + slot`).
> 6. **Retired outright:** §3's `role` row, §10 (role vocabulary), ADR-MM-2, and §14's "four-role
>    vocabulary" criterion. The rest of the spec stands, with "role" read as "slot index".
>
> **Amendment 5 (2026-10-09, RULED by the human; ADR-169 A5). A slot's `home`: where a macro starts
> when a module preset loads into a fresh slot.**
> 1. **The field.** A slot in a module preset may carry an optional `home`, a value in [0, 1]. An
>    absent `home` is 0.5, so every preset written before A5 keeps its meaning.
>    Example: `"7": {label: "Squash", bind: {cmpAmt: [0, 0.8]}, home: 0}`.
> 2. **When it applies.** When a module preset loads into a slot for which the HORDE preset stores
>    NO value, `slot_base` (corner tier) or `global_value` (global tier) starts at `home`. Once the
>    HORDE preset stores a value, that value wins. §14's rule stands: loading a different module
>    preset never changes a stored `slot_base`.
> 3. **What it is not.** `home` is a load-time default only. It is not morphed, it is not a
>    modulation source, and nothing is driven automatically (A4.3 stands).
> 4. **Validation.** A `home` outside [0, 1] is invalid and fails validation. A host that meets one
>    anyway uses 0.5.
> 5. **Acceptance (added to §14).** For slots with `home` 0, absent, and 1, a fresh load resolves
>    each bound parameter at `shape(curve, lo, hi, home)`. A slot with a stored value is unchanged
>    by the same load.
> 6. **The prototype is unchanged.** `horde-module-macros.html` does not model a fresh-slot load, so
>    it stays the parity oracle for §5's resolution. A5 is checked by its own acceptance row.

---

## 1. Problem

FX modules are themselves modular: a module preset can change the module's internal parameter surface (the rack's topology is part of its preset). Nothing inside the module is stable enough for a morph corner or a DAW automation lane to bind to. Two naive fixes both fail for the same reason:

- Macros that appear and disappear with morph position → parameter IDs are unstable.
- Floating macros that get reassigned as morph moves → IDs are stable but meaning isn't.

The DAW needs a fixed set of parameter IDs whose meaning does not depend on state the DAW can't see. Morph position is such state.

## 2. Decision (the rule)

Module macro slots are ordinary HORDE parameters and obey the existing two-tier rule. Nothing new is invented at the plumbing level.

1. Each hosted module exposes a **fixed count of macro slots** (`N_SLOTS`, recommend 4 for v1, hard-cap 8), keyed by **role**, not by index or name.
2. Each slot is independently **global** or **corner**:
   - **global** — one value, exposed as a stable CLAP parameter, morph-independent, no corner bindings.
   - **corner** — a base value per corner, interpolated by morph, offset by global intents via per-corner bindings, **not** exposed as a CLAP parameter.
3. The **module preset** supplies, per role it implements: a display label and the internal bindings (internal param → [lo, hi] over slot value 0..1). It never supplies values.
4. The **corner** supplies: which module preset is loaded, and the base value for every corner-tier slot.
5. Internal module parameters are never host-exposed and never corner-bound. The only path from outside to inside a module is through its slots.

The "secondary macro system" is a UI scope (a strip of the module's slots on its panel), not separate machinery.

## 3. Vocabulary

| term | meaning |
|---|---|
| role | one of a fixed enum shared by all modules: `Amount`, `Tone`, `Motion`, `Regen` (v1). Extensible only by ADR. |
| slot | a host-side macro parameter on a module host, keyed by role. Exists whether or not the loaded preset implements it. |
| module preset | a saved state of a module: topology/internal defaults + per-role label + per-role internal bindings. |
| tier | `global` or `corner`, per slot, stored on the HORDE preset (not on the module preset). |
| resolve | the per-block pass that turns (morph, macros, corners, tiers) into slot values, then internal params, then instance weights. |

## 4. Data model

```yaml
ModulePreset:
  id: string             # stable, used for re-linking
  module: string         # 'rack' | 'shaper' | ...
  internal_defaults: {param_name: value}
  slots:                 # only roles this preset implements
    Amount: {label: 'Size',  bind: {delay: [0.015, 0.22], diff: [1800, 350]}}
    Tone:   {label: 'Damp',  bind: {damp: [1200, 9000]}}
    Regen:  {label: 'Regen', bind: {fb: [0, 0.88]}}
    # Motion absent: this preset does not implement the Motion role
    # optional third element = curve (ADR-169 A3); default lin:
    # Tone: {label: 'SC HPF', bind: {scHz: [20, 500, log]}}

HostSlotState:           # per module host, per role; lives on the HORDE preset
  tier: 'global' | 'corner'
  global_value: float    # used iff tier == global

Corner:                  # existing corner schema, extended
  module_presets: {host_id: ModulePreset}   # embedded by value + origin id
  slot_base: {host_id: {role: float}}        # used iff tier == corner
  bindings: {intent: {target: depth}}        # target may now be `host_id.role`
```

Rules on the model:

- A corner binding whose target is a global-tier slot is **inert**, retained on disk, and surfaced greyed in the editor. Flipping the tier back re-activates it. No silent deletion.
- A corner binding whose target role the corner's module preset does not implement is **inert for that corner**, retained, and labelled "unbound here". Interpolation toward a corner where the role *is* implemented still works — the slot value is computed regardless; only the internal mapping is absent on the unbound side.
- `slot_base` is stored for every role, even roles the preset doesn't implement, so preset swaps don't lose values.
- **Binding curve** (ADR-169 A3, 2026-10-04). A binding is `[lo, hi]` or `[lo, hi, curve]`; an absent curve is `lin`, so every preset written before A3 is unchanged. `curve` is one of:
  - `lin`: `lo + (hi − lo)·v`;
  - `exp`: `lo + (hi − lo)·v²`;
  - `log`: `lo·(hi/lo)^v`, geometric. It is valid only when `lo` and `hi` are both nonzero and of the same sign.

  Every curve returns `lo` exactly at v = 0 and `hi` exactly at v = 1 (special-cased, since neither `lo + (hi − lo)·1` nor `lo·(hi/lo)^1` is guaranteed to round to `hi`).

  A module manifest that declares `log` on other endpoints is invalid and fails validation. A host that meets one anyway (a preset from outside the manifest check) resolves it as `lin` and flags the binding in the editor. It never produces a NaN. The names and laws are the ones Sluice already builds (`lo + curve(v)·(hi − lo)`), so one vocabulary serves every module. Sluice's breakpoint curves are not part of this contract: Sluice resolves its own macros (A1).

## 5. Resolution algorithm

Runs once per control block, after the existing intent resolution, before module DSP.

```
t        = morph position (existing)
macro[i] = global intent value (existing)

for each host h, role r:
  if tier[h][r] == global:
      slot[h][r] = global_value[h][r]                       # DAW/mod-owned
  else:
      base = lerp(cornerA.slot_base[h][r], cornerB.slot_base[h][r], t)
      off  = Σ_i lerp(bindA[i][h.r], bindB[i][h.r], t) * macro[i]
      slot[h][r] = clamp01(base + off)

for each host h, each live module instance k (see §7):
  ip = preset_k.internal_defaults
  for role r in preset_k.slots:
      for (name, [lo, hi, curve = lin]) in preset_k.slots[r].bind:
          ip[name] = shape(curve, lo, hi, slot[h][r])         # §4 binding curve; lin == lerp
  apply ip to instance k with the module's own smoothing
```

The curve applies only at this last slot → internal-parameter step. Slot values, their morph interpolation, offsets and clamp stay linear in [0, 1]. Binding depths are interpolated per corner exactly as they are for synth params today. Home-plus-offset semantics are identical to the pad.

Determinism: no wall-clock; the quantum resolve mode (§7) draws from the existing seeded RNG stream.

## 6. CLAP parameter surface

- Every module host reserves `N_SLOTS` parameter IDs at build time: `FX1.slot.Amount` … `FX1.slot.Regen`. These IDs exist permanently regardless of tier.
- A slot's CLAP param is **flagged automatable + visible** iff tier == global; otherwise flagged hidden/read-only (or however the existing corner-tier params are hidden today — match that, don't invent).
- Display name of the CLAP param is the **role**, prefixed by host: `FX1 Regen`. Not the module preset's label. The label is UI-only and may change with preset or morph; the DAW-facing name must not.
- Tier change → `clap_host_params.rescan(CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT)`. Automation lanes on a slot that flips to corner tier become orphaned by the host's normal rules; do not attempt to remap them.
- Module internal params get no CLAP IDs.

## 7. Preset identity across corners

Module preset identity per corner is a **discrete corner parameter**. It is either locked global (same preset in all corners; the common case and the default) or per-corner, in which case it resolves under one of:

| mode | behaviour | when |
|---|---|---|
| crossfade | both instances live, equal-power weights by morph | default for the rack (feedback tails) |
| flip | hard switch at t = 0.5 | shaper-class modules, stateless |
| quantum | seeded per-tick pick with P(B) = t | mirrors the existing quantum-morph parameter flip |

Instance weights are keyed by **preset**, not corner: if both corners hold the same preset, one instance at weight 1. Max two live instances per host in v1.

Both live instances receive the **same slot values** and map them through their own preset's bindings. This is what makes cross-preset morphing coherent: the slot value is the shared contract; the internal meaning is per-preset.

## 8. Save and embed semantics

- Module presets are saveable standalone and are **embedded by value** in every HORDE preset and corner, with `origin_id` for optional re-linking. A HORDE preset never dangles.
- Editing a slot's label or internal bindings while inside a corner edits the embedded copy in that corner. It does **not** write back to the standalone module preset unless the user explicitly saves it. UI must make this distinction unmissable (see §9).
- Tier state is a property of the HORDE preset, not the module preset. Loading a module preset never changes a slot's tier.

## 9. UI requirements

- Module panel shows exactly `N_SLOTS` knobs in fixed role order. A knob whose role the current preset doesn't implement is drawn, inert, labelled "unbound in <preset>".
- Corner-tier knob label mid-morph: dominant corner's label emphasized, other de-emphasized, `Size → Depth`. Global-tier knob label: the role name plus a global marker.
- Tier toggle on each knob. Toggling global ↔ corner is one gesture.
- Corner editor: base sliders and binding columns for a global-tier slot are disabled with a "global" lock, not hidden.
- Readout per slot: `base (A → B) + intents = value` for corner tier; `value — DAW-owned` for global tier. This readout was the single most useful thing in the prototype; keep it.
- A "promote to global" gesture on any corner-tier slot: sets tier global and seeds `global_value` from the current resolved value.

## 10. Role vocabulary and the Roar module

> **RETIRED by Amendment 4** (ADR-169 A4, 2026-10-05). Kept as history; it no longer governs.

Roles are the cross-module contract. Before the shaper module's preset format ships, confirm its presets can be expressed in the same four roles (`Amount` = drive, `Tone` = curve/shape, `Regen` = feedback/pre-post color, `Motion` = internal LFO if any). If the shaper genuinely needs a fifth role, add it by ADR with a one-line justification and apply it to the rack too. Do not let modules define private roles.

## 11. Parity with the prototype

Match:
- Resolution order and arithmetic in §5, including interpolated binding depths and clamp.
- Inert-but-retained binding behaviour for global-tier and unbound-role cells.
- Instance weight formulas for the three resolve modes.
- DAW-facing list contents: `morph`, one per global intent, one per global-tier slot; nothing else.

The prototype binds linearly only. A `lin` binding keeps exact parity with it. `exp` and `log` are a ratified divergence (ADR-169 A3), judged by their closed forms (§14), not by the prototype.

Do **not** replicate (incidental to the browser prototype):
- The specific Diffuse/Comb topologies, their internal parameter names, ranges, or sound. They exist only to have two presets with different surfaces.
- The fake triangle "lane" automation, the 180 ms quantum tick, the equal-power cosine law (use whatever the existing quantum-morph and crossfade code paths already do).
- Web Audio node graph, smoothing constants, the three-oscillator saw synth, the keyboard mapping.
- Any DOM/render structure. The panel layout is illustrative.

## 12. Open decisions — staged for DECISIONS.md

1. **Global-tier slot modulation.** A global slot is DAW-automatable, but can it also be a target for HORDE's global mod sources? Recommendation: yes, via the existing global mod-target path, since it's just a global param. Confirm no double-ownership issue with the pad.
2. **Preset identity default.** Locked-global by default, or per-corner by default? Recommendation: locked-global, with per-corner opt-in, because cross-preset morph is the exotic case and costs a second instance.
3. **Instance count cap.** Two live instances per host is enough for a 1D morph; a 4-corner XY with four different presets would need four. Recommendation: cap at two for v1 and refuse (with UI feedback) to assign a third distinct preset across corners.
4. **`N_SLOTS`.** Four roles fit both current modules. Recommend 4, reserve IDs for 8 so raising it later doesn't renumber.
5. **Write-back policy.** Should there be a "save embedded copy back to origin" shortcut, or is Save-As-module-preset enough? Recommendation: Save-As only in v1.

## 13. ADR entries

- **ADR-MM-1:** Module macro slots are host parameters under the existing two-tier rule; no separate macro system.
- **ADR-MM-2:** Slots are keyed by a fixed cross-module role enum; global intents and corner bindings target roles, never indices or labels.
- **ADR-MM-3:** DAW-facing parameter IDs and names for slots are position-independent; only global-tier slots are automatable.
- **ADR-MM-4:** Module presets embed by value in HORDE presets with an origin reference; module internals are never host-exposed.
- **ADR-MM-5:** Module preset identity is a discrete corner parameter resolved by crossfade / flip / quantum, with instances keyed by preset.

## 14. Acceptance criteria

- [ ] Exposed CLAP parameter list is byte-identical across all morph positions for a fixed set of tiers (`./verify fast` compares snapshots at t = 0, 0.5, 1).
- [ ] Flipping one slot's tier changes the exposed list by exactly one entry and triggers a rescan.
- [ ] Slot resolution matches the prototype oracle to 1e-6 for a fixed (morph, macros, corners, tiers) fixture, including an unbound-role case and a global-tier case.
- [ ] Loading a different module preset into a corner never changes any slot's tier or any `slot_base` value.
- [ ] A HORDE preset saved with an embedded module preset reloads correctly when the origin module preset is deleted.
- [ ] Cross-preset morph in crossfade mode produces no discontinuity in module output at t = 0.5 (null test over a sweep); flip mode produces exactly one.
- [ ] Quantum resolve is bit-reproducible under a fixed seed.
- [ ] Slot `home` (A5): for slots with `home` 0, absent (0.5) and 1, a fresh module-preset load into a slot with no stored value resolves each bound parameter at `shape(curve, lo, hi, home)`; a slot with a stored value is unchanged by the same load; a `home` outside [0, 1] fails validation.
- [ ] Binding curves (A3): for each of `lin`, `exp`, `log` over v ∈ {0, ¼, ½, ¾, 1} and at least one decreasing range, the resolved value matches the §4 closed form to 1e-12, and the endpoints are exact. A `log` binding with endpoints of mixed sign or a zero endpoint fails manifest validation, and when forced past it resolves as `lin` with no NaN.
- ~~Shaper module presets round-trip through the four-role vocabulary without a private role.~~ RETIRED by Amendment 4 (2026-10-05); struck 2026-10-09 (ADR-203).
- [ ] Corner editor disables (does not hide) base sliders and binding columns for global-tier slots.
- [ ] Trace artifact: per-block dump of `slot[h][r]` alongside `base`, `off`, and instance weights, viewable in the existing visual trace tooling.
