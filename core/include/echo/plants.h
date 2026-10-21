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
ECHO_PLANT(irHashSkipsKey)       // the IR cache hash skips the last key: a changed mod.seed serves a stale drawing
ECHO_PLANT(irReusesState)        // the renderer's clone is not reset between renders: the last patch's repeats leak in
ECHO_PLANT(irDropsDevice)        // the renderer never sends the Device keys: the drawing ignores the I/O gains
ECHO_PLANT(irLevelIgnored)       // the renderer's impulse is always 0 dBFS, whatever the stated level
ECHO_PLANT(irSkipsOutputFilter)  // the renderer forces the output filter off (T-019's named plant)
ECHO_PLANT(irAllocsInRender)     // render() allocates a scratch buffer: unsafe to run from a preallocated service
ECHO_PLANT(irSeedAfterReset)     // the clone is reset BEFORE the values are sent: drift follows the last render's seed (critic T-030 H1)
ECHO_PLANT(irAcceptsClampedRate) // a rate the engine clamped is rendered under the requested rate's hash (critic T-030 M3)
ECHO_PLANT(irRestAtMissesEnd)    // a clone that rested inside the last block reports restAt -1 (critic T-030 M1)
}  // namespace echo::plant

#undef ECHO_PLANT
