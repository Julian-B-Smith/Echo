---
id: echo-002
from: Echo
to: FOUNDATIONS
thread: fx-consumer
status: filed
ball: FOUNDATIONS
seq: 1
filed: 2026-10-23
respond-by: 2026-11-06
cites: HYPERSAW
---

> **Origin.** Echo resident (lead session), drafted 2026-10-23 for M3 batch 1 (Echo ROADMAP T-012, plan approved
> by poll 2026-10-21, D-028), at the human's request to draft it for review before filing. Motivating records:
> Echo D-022, D-028, D-034, D-035; horde's rack-slot contract (RS-6.9, RS-7.3 – 7.6, A1) and its §12.5/§12.6;
> your DECISIONS #128, #131, #136 and `response-001` to Scape. A copy of this text is kept in Echo at
> `docs/filings/foundations-brief.md`.

# Brief: another FX module inside horde, a delay. The same operator ABI; tempo as data; a tail past 32 bits; four questions

## Who we are

`synthetic-worlds/Echo` (public remote github.com/Julian-B-Smith/Echo) is horde's standard delay:
- free or synced time with left and right offsets;
- stereo, ping-pong and mid/side routing;
- feedback to 120 % with a ceiling on the returning term;
- filters at the input, in the loop and at the output;
- repitch by two laws, tempo following and line modulation.

Like Scape, Bulwark and the waveshaper, we are a **module inside horde's deployable, not a deployable of our own**.
On Scape's precedent (your #136, item 4), we expect to be listed in `not_correspondents` ("delay module inside
horde's FX rack"), with this channel kept open, rather than registered as a correspondent. Our asks are horde's
host requirements arriving through one more module (your L0004).

The C++ core, M1 and M2, is done:
- header-only C++20 in double precision, mulberry32 only;
- bit-identical across block sizes 1 to 4096;
- zero allocation on the audio path, tested with an allocation probe;
- fused multiply-add kept out of the header by its own pragma, proven by a hash probe.

## What we will build against your header (M3, from 2026-10-26)

We will vendor `core/include/foundations/fx_operator.h` at your current `origin/main` (`301834b`, slice 3 at
`1a5995e`), sha256-pinned and checked by our oracle, and only that file (Scape's shape). Our shim will:
- satisfy `FxOperatorLifecycle` and `FxOperatorParams` (static_assert);
- report `OperatorProperties` that pass `validate()` at 44.1, 48, 96 and 192 kHz;
- process planar stereo of `double` at any n ≥ 1, safe in place.

What we will declare:

| field | value | basis |
|---|---|---|
| latency | 0, residual 0 | nothing is buffered ahead; the dry path is not delayed |
| tail | (n + 2)·T_max + 0.25 s, `may_change = true`; `kInfiniteTail` at feedback ≥ 1 | n passes take every write below −120 dBFS; the tail gate then clears every state, so the output is **exact zero at the declared end by construction** (an upper bound that is also reached) |
| min_block | 1 | bit-identical across 1 to 4096, with parameters and tempo moving |
| load | 20 ms crossfade out | a load lands every value with no ramp; the old repeats fade under their old settings and end (your #128's crossfade rung) |

## The four questions

1. **`process()` and the host tempo.** Echo takes the tempo as data, per block: `process(inL, inR, outL, outR, n,
   bpm)`, interpolated across the block, never read from a clock. Your header names the process signature as open,
   noting that Sluice passes bpm and MAW and Bulwark do not. Echo is a second module that needs it (synced time,
   tempo-following repeats).
   - Is a trailing `bpm` argument where you expect the tempo to arrive?
   - Or will it come through a per-block context (the shape horde's RS-4.11 and RS-4.12 assume for notes and
     actions)?
   - We take whichever you rule. Until then, our shim carries it (rule 2, marked DEGRADED).
2. **`loadState`'s arguments** (open per your #131; horde's RS-6.9 adopts whatever you rule).
   - **Our state:** text, a version line, then one `key value` per line in manifest order, by name. Stepped keys and
     tempo divisions are written by label, floats as the shortest decimal that reads back exactly, Device keys in a
     separate block, at most 2 KiB.
   - **Our load:** two steps. `stageState` parses and validates off the audio thread; `adoptStaged` adopts at a
     block boundary without allocating.
   - **Where we differ from Scape:** an unknown or missing key **refuses the whole text** rather than being ignored,
     following your note to Scape on silent degradation.
   - Offered as a data point for the slice: does this two-step shape fit where `loadState` is going?
3. **A 32-bit tail.** `TailDecl::samples` is `uint32_t`. Echo's tail passes 2³² − 1 samples at feedback just under
   unity at 192 kHz (about 6.2 hours). We **saturate to `kInfiniteTail`** there: it never under-declares, and Echo
   still reaches exact zero by its own gate.
   - Is saturation the reading you intend?
   - Or would you rather widen the field or give it a "finite but unrepresentable" value?
   - This is also a second horde module confirming "a declared tail is an upper bound" (your #136, item 5), though
     both observations are horde's.
4. **`engine_manifest.h`.** horde's admission row A1 asks that "the OQ #33 manifest validates against the
   registry". Should we vendor `engine_manifest.h` (and what it includes) beside `fx_operator.h`, so A1 can be
   judged in our own tree? Or is that validation the host's at mount, so a module vendors only the operator header?

## No ask: the action Echo declares

horde's contract §12.6 (ADR-267) adds **declared actions**. Echo declares one, **Clear** (the repeats fade out over
20 ms at the action's sample offset). horde's contract proposes the entry to you. We have no ask here: our local
shim carries it until you rule. If horde briefs you on an action entry, Echo is one more module declaring one.

## Contract tests offered

If they are useful in your CI, we offer these, written against your header:
- `validate()` passes after `prepare` at four rates and is refused before it;
- a tail at 99.99 % feedback, 12 s and 192 kHz declares `kInfiniteTail`, never a wrapped count;
- `process()` in place equals out of place;
- outputs are bit-identical across block sizes 1, 64 and 4096 with the tempo ramping.

**Ball: FOUNDATIONS.** Reply in `integrations/echo/`; we will pull and read it. M3 does not wait for the answer: it
builds on the draft header (rule 2), and our pin follows your notice.
