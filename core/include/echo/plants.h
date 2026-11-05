// echo/plants.h — planted defects, TEST-ONLY.
//
// Every detector in core/tests must first be seen to fire on the fault it
// guards (charter §Domain: "every detector has a must-fail planted control").
// Each plant below is a deliberate, documented bug.
//
// HOW THE SWITCH WORKS. Without ECHO_PLANTS (every production consumer, and
// the clean test binary echo_tests) each flag is `inline constexpr bool = false`,
// so every `if (plant::x)` is a constant-false branch the optimiser deletes:
// the clean suite runs exactly the code horde links. With ECHO_PLANTS (only the
// echo_plants test binary) each flag is a mutable global the suite switches on
// one at a time, requiring its named check to FAIL. One plant binary instead of
// one binary per plant (Bulwark's shape) keeps `./verify fast` to one extra
// compile; the price is that plants are runtime branches in THAT binary only.
#pragma once

#ifdef ECHO_PLANTS
#define ECHO_PLANT(name) inline bool name = false;
#else
#define ECHO_PLANT(name) inline constexpr bool name = false;
#endif

namespace echo::plant {
ECHO_PLANT(noCeiling)            // the ceiling on the returning term is removed (§5.1)
ECHO_PLANT(dryLimiter)           // the dry path is soft-limited (ADR-142's first bug)
ECHO_PLANT(mixLeak)              // mix 100 % keeps the cos(pi/2) dry leak (§8)
ECHO_PLANT(loopFilterInCircuit)  // a loop filter that is "off" still filters (ADR-142's second bug)
ECHO_PLANT(timeHalfSample)       // every read sits half a sample late (time accuracy)
ECHO_PLANT(fixedCoefRate)        // filter coefficients computed at 48 kHz whatever the host rate
ECHO_PLANT(tempoIgnored)         // synced time ignores the host tempo (always 120 bpm)
ECHO_PLANT(clampTruncates)       // a synced time past 12 s is clamped, not halved (§3.4)
ECHO_PLANT(antiDenormalDc)       // 1e-20 added to every line write (D-005's rejected injection)
ECHO_PLANT(noTailGate)           // the tail gate never fires: float decay outlives the declaration
ECHO_PLANT(sharedState)          // the DC blocker's state is one static shared by all instances
ECHO_PLANT(resetKeepsLine)       // reset() leaves the old repeats in the lines
ECHO_PLANT(guardUncounted)       // the input guard zeroes non-finite samples without counting
ECHO_PLANT(loopRepairPartial)    // a non-finite loop sample is zeroed, but filter state is kept
ECHO_PLANT(ftzNotRestored)       // process() leaves FTZ set in the host's FP control word
ECHO_PLANT(allocInProcess)       // process() allocates on the audio thread
ECHO_PLANT(dbRamp)               // the I/O gain ramp is linear in dB, not amplitude (B435)
ECHO_PLANT(rampPerBlock)         // ramps advance once per process() call, not per sample
ECHO_PLANT(instantSwitch)        // routing and filter switches flip with no crossfade
ECHO_PLANT(equalPower)           // time crossfades are equal-power (1.41x on correlated heads)
ECHO_PLANT(bareJump)             // jump mode moves the read head with no 3 ms crossfade
ECHO_PLANT(deferralBroken)       // a time.change flip shortens the fade already running
ECHO_PLANT(loadKeepsLine)        // a load leaves the old repeats ringing in the line
ECHO_PLANT(loadWritesDevice)     // a load writes the Device keys (io gains)
ECHO_PLANT(restRamps)            // at rest, ramps keep running instead of landing
ECHO_PLANT(msSign)               // mid/side decode R = M + S (sign error)
ECHO_PLANT(pingpongNoCross)      // ping-pong feedback is not crossed
ECHO_PLANT(loopGainOverOne)      // +1 dB inside the loop filter (stands in for any boosting loop element)
ECHO_PLANT(dcTextbook)           // DC blocker without the (1+R)/2 normalisation (peaks 2/(1+R) at Nyquist)
ECHO_PLANT(retargetMidFade)      // a new target retargets the running fade (queue broken, §3.5)
ECHO_PLANT(rangeClamps)          // setParam clamps an out-of-range value instead of refusing it
ECHO_PLANT(gateLineOnly)         // the gate's quiet test reads the line writes only (SPEC §10 as written)
ECHO_PLANT(oldReadWrap)          // the read wraps in floating point (critic H1): a near-whole delay reads the wrong sample
ECHO_PLANT(tailClockFromSilence) // the tail clock counts from silence, ignoring holds and tail changes (critic H2)
ECHO_PLANT(tailInt32)            // the declared tail is narrowed to 32 bits and wraps (critic H3)
ECHO_PLANT(loadRestartsFade)     // a second load mid-fade restarts the fade gain at full (critic H4)
ECHO_PLANT(loadSnapsDevice)      // a load snaps the Device gain ramps (critic H4)
ECHO_PLANT(loadClearsInstantly)  // a load clears the old audio at once, with no fade
ECHO_PLANT(loadNewSettingsOnOld) // the old audio fades under the NEW routing / filters / wet level
ECHO_PLANT(tempoGlidesAtRest)    // at rest the tempo does not land: a stale tempo glides on wake
ECHO_PLANT(jumpUsesXfade)        // jump fades over time.xfadeMs instead of 3 ms
ECHO_PLANT(xfadeIgnored)         // crossfade ignores time.xfadeMs (a fixed 30 ms)
ECHO_PLANT(ceilingBeforeGain)    // the ceiling acts before the feedback gain
ECHO_PLANT(ceilingHardClip)      // the ceiling is a hard clip at 1.0
ECHO_PLANT(dcFixedRate)          // the DC blocker's pole is computed at 48 kHz whatever the rate
ECHO_PLANT(rampFixedSamples)     // the 20 ms ramps are 960 samples whatever the rate
ECHO_PLANT(ftzNeverSet)          // process() never sets FTZ (restoring trivially)
ECHO_PLANT(msSideGain2)          // mid/side encodes S at twice its level
ECHO_PLANT(noMinFloor)           // the 1 ms floor on the resolved time is removed
ECHO_PLANT(wakeLeftOnly)         // only the left input wakes a resting instance
ECHO_PLANT(noOutputGuard)        // a non-finite output sample is passed through
ECHO_PLANT(tailFormulaShort)     // the tail's pass count n is 7x too small (a log-base slip)
// ---- M2 ----
ECHO_PLANT(modSlewOff)           // the modulation offset is not slew-limited (SPEC §7)
ECHO_PLANT(driftOvershoot)       // the drift curve is not clamped to [-1, 1] (Catmull-Rom reaches 1.25)
ECHO_PLANT(unseededSeed)         // a mod.seed change restarts the streams from fresh points (a step)
ECHO_PLANT(shapeInstant)         // a mod.shape switch flips with no 250 ms blend
ECHO_PLANT(modDepthUnlimited)    // the depth is not limited by the curve's slope (SPEC §7)
ECHO_PLANT(noLawCap)             // the repitch laws' 12 st cap is removed
ECHO_PLANT(noFallback)           // a repitch move longer than 3 s at the cap glides instead of crossfading
ECHO_PLANT(tapeNoHistory)        // the tape law reads D = 1/v: no history, so no recorded-speed memory
ECHO_PLANT(expLawLinear)         // the exponential law is a fixed-tau one-pole on D (D-006's rejected law)
ECHO_PLANT(tempoModeIgnored)     // every tempo move crossfades, whatever time.tempoMode
ECHO_PLANT(followNoCap)          // tempo "follow" follows any move, past its 50-cent cap
ECHO_PLANT(integerRead)          // the read position is rounded to a whole sample (stepped D)
ECHO_PLANT(blockSteppedD)        // the read position is held for a whole process() call, then steps
// ---- M2, after the critic ----
ECHO_PLANT(tailClockIgnoresMotion) // the tail clock runs while a head still glides or fades (D-025)
ECHO_PLANT(seedResendRestarts)   // re-sending an unchanged mod.seed reseeds (a frozen drift)
ECHO_PLANT(loadKeepsModState)    // a load keeps the old modulation phase and streams (D-026)
ECHO_PLANT(tempoStealsKeyMove)   // a tempo ramp turns a key move in flight into a tempo move
ECHO_PLANT(followNoTrackInFade)  // a follow crossfade's destination does not track the grid
ECHO_PLANT(followKeepsResidual)  // follow keeps a residual offset from the grid forever
ECHO_PLANT(modNoFloorBound)      // a slewing offset may read under the 3-sample floor
ECHO_PLANT(wakeOffsetFromZero)   // after a wake the offset slews in from 0
ECHO_PLANT(tapeLinearHistory)    // the tape history is linearly interpolated (the pre-critic form)
ECHO_PLANT(glideFixedRate)       // the repitch laws' time constant is counted in 48 kHz samples
// ---- M4 (core/tests/ir_tests.cpp guards these; echo_ir_plants) ----
ECHO_PLANT(irHashSkipsKey)       // the IR cache hash skips the last key (mod.seed): a change to it serves a stale drawing
ECHO_PLANT(irReusesState)        // the renderer's clone is not reset between renders: the last patch's repeats leak in
ECHO_PLANT(irDropsDevice)        // the renderer never sends the Device keys: the drawing ignores the I/O gains
ECHO_PLANT(irLevelIgnored)       // the renderer's impulse is always 0 dBFS, whatever the stated level
ECHO_PLANT(irSkipsOutputFilter)  // the renderer forces the output filter off (T-019's named plant)
ECHO_PLANT(irAllocsInRender)     // render() allocates a scratch buffer: unsafe to run from a preallocated service
ECHO_PLANT(irSeedAfterReset)     // the clone is reset BEFORE the values are sent: drift follows the last render's seed (critic T-030 H1)
ECHO_PLANT(irAcceptsClampedRate) // a rate the engine clamped is rendered under the requested rate's hash (critic T-030 M3)
ECHO_PLANT(irRestAtMissesEnd)    // a clone that rested inside the last block reports restAt -1 (critic T-030 M1)
// ---- M4, B4-2 (T-031 service, T-032 display model) ----
ECHO_PLANT(irChunkResetsNoise)   // each renderNext restarts the noise stream: a chunked render differs from the one-shot
ECHO_PLANT(irRenderInline)       // the service renders inside request(), on the caller's thread
ECHO_PLANT(irWholeTailFirst)     // the service publishes nothing until the whole window is rendered
ECHO_PLANT(irHeadRestarts)       // frame 1 re-renders the head from sample 0 forty times: right samples, ~40x the CPU (T-036)
ECHO_PLANT(irWorkerNaps)         // the worker sleeps 1.5 s before publishing frame 1: no CPU spent, the wall only (T-036)
ECHO_PLANT(irRendersWhilePaused) // pause() is ignored: renders run with the editor closed
ECHO_PLANT(irCacheIgnored)       // the cache never hits: every request re-renders
ECHO_PLANT(irNoCancel)           // a new request waits for the render in flight to finish
ECHO_PLANT(viewDecimatesPeaks)   // a display column shows its first sample, not its peak
ECHO_PLANT(gridFixedTempo)       // the tempo grid is drawn at 120 bpm whatever the tempo
ECHO_PLANT(viewGenWindowFromBase) // the spectrum strip's windows ignore the L/R offsets (the base time)
ECHO_PLANT(edcForward)           // the energy decay curve integrates forward, not backward (Schroeder)
ECHO_PLANT(viewGenIndexOffByOne) // the spectrum strip draws generation n+1 as generation n (critic B4-2 H2)
ECHO_PLANT(irCancelKeepsSlot)    // a cancelled render stays a valid, freshest slot and pushes complete renders out (critic B4-2 M2)
ECHO_PLANT(irPublishesEmpty)     // a request is shown before anything is rendered: the screen blanks on every change (critic B4-2 M3)
ECHO_PLANT(viewUnrenderedIsZero) // a column past what is rendered reads as exact zero: a partial frame draws an ended tail (critic B4-2 M4)
// ---- M3, B2 (T-013: the ABI and the declarations) ----
ECHO_PLANT(propsNoTail)          // properties() leaves the tail undeclared (a forgotten field, FOUNDATIONS #129)
ECHO_PLANT(tailWraps32)          // the 64-bit tail is narrowed to TailDecl's 32 bits and wraps (SPEC §16.5)
ECHO_PLANT(paramNoClass)         // the Device class maps to no OQ #33 class (RS-2.2)
ECHO_PLANT(processReadsAfterWrite) // outputs cleared before the input is read: in place, the input is lost
ECHO_PLANT(loadBoundShort)       // properties() declares half the real load fade (critic T-013 MED-1)
// ---- M3, B2 (T-014: meters and the I/O gain standard's tests 1-7) ----
ECHO_PLANT(latchClears)          // a clip latch clears itself at the next block (B225: it must hold)
ECHO_PLANT(tapBeforeGain)        // the input tap reads the signal BEFORE Input Gain
ECHO_PLANT(prepareRampsIn)       // the gains ramp in from unity at every landing instead of opening at their value (test 3)
ECHO_PLANT(inGainAfterLoop)      // Input Gain applied at the output: it never drives the loop (test 5)
ECHO_PLANT(doubleGain)           // TEST-SIDE: the simulated rack slot adds its own gain pair beside Echo's (test 6)
// ---- M3, B2, after the T-014 critic ----
ECHO_PLANT(inGainDryOnly)        // Input Gain reaches the dry path but is divided back out of what enters the loop (H1)
ECHO_PLANT(outTapDryOnly)        // the output tap reads the dry term only: blind to the wet and the tail (H2)
ECHO_PLANT(tapReadsRaw)          // the input tap reads the raw input, before the non-finite repair (M1)
ECHO_PLANT(resetClearsLatch)     // reset() clears the meter latches (RS-6.13: they are retained)
// ---- M3, B2 (T-015: the saved state, echo/state.h) ----
ECHO_PLANT(stateFloat6g)         // floats written as %.6g: lossy, so save -> load -> save drifts (L0008)
ECHO_PLANT(stateKeyMissing)      // one key (fb.amount) left out of the saved state (A17)
ECHO_PLANT(stateLatchSerialised) // the meter latch written into the state (A20: live state saved)
ECHO_PLANT(stateOversizedAccepted) // a text over 2 KiB is parsed instead of refused first (A19)
ECHO_PLANT(stateDivByIndex)      // time.div written by index, not by name (SPEC §3.1)
ECHO_PLANT(stateAcceptsUnknown)  // an unknown key is skipped silently instead of refusing the text
ECHO_PLANT(adoptResets)          // adoptStaged resets first: a load mid-play cuts the repeats (D-020 broken; critic H1)
// ---- M3, B2 (T-037: the Clear action, SPEC §16.7, row A23) ----
ECHO_PLANT(actionAtBlockStart)   // A23's must-fail shim: an action lands at the block's start, not its offset
ECHO_PLANT(clearCuts)            // Clear cuts the repeats with no fade
ECHO_PLANT(clearLandsValues)     // Clear's fade ends by adopting the patch (snapping ramps): a value lands
ECHO_PLANT(clearWritesState)     // Clear edits the patch (feedback to 0): the saved state changes
ECHO_PLANT(clearRestartsFade)    // a Clear during a fade restarts it (D-020: never)
ECHO_PLANT(clearFreezesHeads)    // a Clear's fade holds the heads like a load's (critic T-037 H1; D-040: they keep moving)
ECHO_PLANT(clearFreezesWet)      // a Clear's fading wet is frozen at the Clear (D-040: it follows the live mix)
// ---- M3, B3 (T-017: module presets, echo/presets.h; the macro plants left with the macros, T-039 / D-050) ----
ECHO_PLANT(presetDeviceAccepted) // a Device key is accepted in a preset (RS-4.5)
// ---- M3, B3 (T-018: the admission self-test; contract rows A1 - A23) ----
ECHO_PLANT(resetKeepsModPhase)   // A2: reset() leaves the modulation phase where it was (rest holds it; D-043)
ECHO_PLANT(dryLate)              // A4: the dry path runs one sample late (an undeclared latency)
ECHO_PLANT(monoSum)              // A8: the output is summed to mono (the image collapses)
ECHO_PLANT(selfStart)            // A14: at rest the output is a 1e-24 speck, not zero: in a cycle it starts itself
ECHO_PLANT(b265HoodHalved)       // TEST-SIDE: the click metric's +-16 neighbourhood halved (the port must match horde exactly)
// ---- M3, B3 (T-019: the B408 stereo report, D-010 / D-044; core/tests/fold.h) ----
ECHO_PLANT(stereoInvertsR)       // standard routing decodes line 1 inverted: a centred source cancels in mono (P1 in the engine)
ECHO_PLANT(pingpongBothLines)    // ping-pong also feeds the input into the right line: repeats land on both sides at once
ECHO_PLANT(pingpongInvertsR)     // ping-pong decodes the right line inverted: per repeat r stays 0, but sustained material combs (critic T-019 M1)
ECHO_PLANT(foldScaleSlip)        // TEST-SIDE: rho divides by the SUM of the channel energies (the 1 + r scale slip D-044 found)
ECHO_PLANT(foldOctaveNarrow)     // TEST-SIDE: the octave bands are half an octave wide
ECHO_PLANT(foldNotchUnsmoothed)  // TEST-SIDE: the notch search reads one raw bin, not the 1/3 octave (D-010's rejected raw minimum)
ECHO_PLANT(zoneAnyMode)          // TEST-SIDE: the colouring flag is raised in every mode (the rule before D-044)
ECHO_PLANT(zoneAtEqual)          // TEST-SIDE: the colouring flag is raised at equal times too
// ---- M3, B4 (T-021: the factory bank) ----
ECHO_PLANT(presetStateOversized) // TEST-SIDE: one factory preset's saved text is padded past 2 KiB (A19 over the bank)
ECHO_PLANT(presetPatchOfDropsKey) // patchOf leaves fb.amount out of the load: the engine keeps its default (critic T-021 M1)
ECHO_PLANT(factoryLabelTypo)     // pingpong-dotted's division label is "3/16 ": step() answers -1 and validate() must refuse it (critic T-021 M2)
// ---- M3, B4 (T-039: no macros, D-050) ----
ECHO_PLANT(factoryCarriesSlot)   // the bank's file gives dub-loop a slot: Echo offers no macros (RS-5.1), so readBank must refuse it, naming the preset
// ---- M3, B4 (T-038: the offset's expression, kept after the scale key left, D-050) ----
ECHO_PLANT(offsetRewrite)        // the offset is applied as base + base * off, not base * (1 + off): can round another way at a nonzero offset (at 0 both are exactly base; critic T-038 L2)
ECHO_PLANT(stateMissingKeyDefaults) // the reader takes ANY missing key as its default instead of refusing the text (critic T-038 L4; D-048 A1, D-050 ruling 2)
// ---- M3, B4 (T-040: the exponential law's mid-glide reversal, D-049) ----
ECHO_PLANT(expSlewOff)           // the exponential law's 0.1 ms speed-step bound is removed: a retarget steps the speed in one sample (an onset reads 3.5, a reversal 5.1)
ECHO_PLANT(expSlewLong)          // the exponential law's speed-step bound is 10 ms, not 0.1 ms: it binds all through a relaxation (the bound must bind only just after a retarget)
ECHO_PLANT(phaseSweepCollapsed)  // TEST-SIDE: every probe phase of the click gate's sweep is the same phase (shift 0): the gate would read one lucky phase
ECHO_PLANT(expSlewBlockStart)    // the exponential law's bound takes the previous speed from the block's first sample, not the previous sample: it depends on the host's block size
ECHO_PLANT(expSpuriousRetarget)  // the target drops to a quarter for 100 samples early on: a move nobody asked for (the bind census must flag a bind before any move)
// ---- M3, B4 (T-041: every one-sample speed step, D-049 A4) ----
ECHO_PLANT(tapeKinkUnbounded)    // the tape law's catch-up has neither bound nor brake: the cap's release steps the speed by 2 - s_model (a plain 500 -> 125 ms move reads 2.4 at 48 kHz, 5.2 at 96, 11.6 at 192 on the reversals)
ECHO_PLANT(tapeNoBrake)          // the catch-up has the 0.1 ms bound but no brake: M2 passes (<= 1.06), but the head overshoots the model by up to 1.8 samples and the speed dips to 0.5 (only the catch-up gate sees it)
ECHO_PLANT(tapeNoTargetBrake)    // the catch-up brakes on the model but not on the target: a lengthening move held at the lower cap passes its target at the landing (0.19 sample at 44.1 kHz, 1.7 at 192; the catch-up gate's lengthening cases see it)
ECHO_PLANT(fbFrozenOutgoing)     // a fade freezes its outgoing head (speed 2.0 or 0.5 to 1.0 in one sample): the fallback and a crossfade-mode retarget of a glide in flight read 3.5 at 48 kHz, 14 at 192
ECHO_PLANT(fbClampUnsigned)       // the outgoing head's clamp to the line uses kMinRead + |offset| (too tight): under a modulated fade from a shortening glide it binds and steps the speed by 0.132 in one sample at 192 kHz
ECHO_PLANT(fadeLinear)           // the time crossfade's weight is linear, not raised-cosine: its slope kinks at both ends and reads in proportion to the rate (the 3 ms jump: 3.6 at 96 kHz, 7.0 at 192)
// ---- M3, B4 (T-042: the modulation offset's speed steps, D-049 A8) ----
ECHO_PLANT(modAccelUnbounded)    // the offset's slope is not bounded, only its size (the old clamp to +-0.25 a sample): it stops dead on its target, a speed step that reads in proportion to the rate (20 ms of modulation, a 20 -> 2 ms glide retargeted 1 - 12 ms in: M2 4.5 at 192 kHz)
ECHO_PLANT(modFloorClampsBoth)   // during a fade the offset's floor clamp answers to BOTH heads (the old headSpan), so a fade to a short time moves the offset by up to its whole size in one sample under the sounding head (a crossfade or jump to 3 ms at the trough of a 5 Hz sine)
ECHO_PLANT(modNoFloorBrake)      // a shortening head's closing speed toward the read floor is not limited to the stopping speed: the offset, slope-bounded, cannot retreat as fast, the read pins to the floor and the speed stops dead (a 100 -> 2 ms repitch at the offset's trough)
ECHO_PLANT(modRideKink)          // while a fade's incoming head rides the floor the offset does not aim at it braking: it reaches the floor with the curve's slope and the read kinks as the head leaves (a crossfade 100 -> 5 ms over 40 ms at the offset's trough)
ECHO_PLANT(modBoundKeepsSlope)   // a floor clamp leaves the offset's tracked slope as it was: the offset leaves the bound still carrying the slope it arrived with, and the read kinks (a 100 -> 3 ms jump at the trough of a 5 Hz sine)
ECHO_PLANT(fbClampStale)         // the outgoing head's clamp to the line reads last sample's offset and misses the offset's coming move: it binds a sample late and steps the head's speed (a 20 -> 2 ms glide crossfaded 12 ms in, 20 ms of modulation at 1.25 Hz near the trough: M2 4.06 at 192 kHz)
// ---- M3, B4 (T-044: Time in Sec, Hz or Sync, D-050) ----
ECHO_PLANT(hzCountsAsSync)       // synced() reads time.mode >= 0.5, the old sync test: hz plays the division instead of reading time.ms
ECHO_PLANT(hzFlipIsMove)         // a sec <-> hz flip is treated as a move though no time moved: it lands the heads on their targets at once (a bare jump), so a move in flight stops dead
ECHO_PLANT(modeFlipSwallowed)    // a flip OUT of sync that moves a time (sync -> sec, sync -> hz) is not marked a control move: a tempo move's latch stays, instead of following time.change
ECHO_PLANT(modeFlipIntoSyncSwallowed) // a flip INTO sync that moves a time is not marked a control move: a stale tempo latch (a tempo step, a same-time flip to sec, the flip back) keeps its latch
ECHO_PLANT(modeFlipAlwaysMove)   // every flip is marked a control move, one that moved no time too: a same-time sec -> sync flip in the sample a tempo step lands follows time.change, not time.tempoMode
ECHO_PLANT(moveReadsOneLine)     // whether a send moved a time is read from line 0 alone: a time.offR move is not marked, so it follows time.tempoMode after a tempo step, not time.change
ECHO_PLANT(keyResendIsMove)      // time.ms / time.div / time.offL / time.offR mark a control move whether or not a time moved (the old unconditional mark): a synced patch's time.ms, or a re-sent value, re-latches a tempo move landing in the same sample
ECHO_PLANT(hzFlipDelaysRest)     // a flip that moves nothing restarts the quiet count: a sec / hz flip while a tail rings puts off the rest (a flip restarting tailClock_ instead is unobservable: the quiet condition always rests first)
ECHO_PLANT(tailReadsDivInHz)     // the declared tail is built from the division's time while hz plays time.ms: hz declares the wrong tail (over, when the division is the longer)
// ---- M3, B4 (T-046: a move landing on a moving tempo, D-050 A3) ----
ECHO_PLANT(movingHeadStopsDead)     // a head still moving, less than half a sample from its target, starts no fade under time.tempoMode crossfade or a crossfade / jump retarget: it stops where it stands, and steps again when the next fade picks up the speed it kept (a time.div move on 120 +- 60 bpm: M2 9.85 at 192 kHz; a retarget at a steady tempo: 13.80)
ECHO_PLANT(followHandoverStopsDead) // the same under time.tempoMode follow, when the tempo is past the 50-cent cap: the head stops where it stands (a time.div move on 120 +- 60 bpm: M2 11.07 at 192 kHz)
ECHO_PLANT(followCapDropsLengthening) // follow's in-cap branch does not read the law's speed when the delay is lengthening: a head handed over faster than the cap drops to the cap's step in one sample (a fast 1/8 -> 1/4 glide: speed 0.445 -> 0.028, M2 5.72 at 192 kHz)
ECHO_PLANT(followCapDropsShortening)  // the same when the delay is shortening (a fast 1/2 -> 1/4 glide: M2 4.15 at 192 kHz)
ECHO_PLANT(followKeepsStaleSpeed)   // follow's in-cap branch leaves the law's last speed in the head: it never moves again by it, but it makes a head at rest look moving, so a later time.tempoMode flip and a tiny tempo move start a fade
ECHO_PLANT(keyFadeChains)           // a key's fade that ends on a moved target queues another key's fade, however the target moved: time.tempoMode never takes over during a ramp (2,667 jumps over 8 s)
ECHO_PLANT(tempoTickRelatches)      // a tempo tick under a key's move in flight re-latches it: a time.change or time.law flip applies on the next tick of a moving tempo, not from the next move
}  // namespace echo::plant

#undef ECHO_PLANT
