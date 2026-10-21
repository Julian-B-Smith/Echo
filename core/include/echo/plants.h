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
}  // namespace echo::plant

#undef ECHO_PLANT
