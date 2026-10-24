// FX-operator properties, lifecycle and parameter entry — OQ #32 slices 1–3, declarations only.
// (FOUNDATIONS.md §5; DECISIONS #117, #118, #120, #121, #124. UNFROZEN: excluded
// from the v0.1 freeze, like mod_routing.h and port.h.)
//
// An FX operator is a payload whose job is audio in → audio out (Sluice's
// network, MAW's waveshaper, Bulwark's dynamics). Three properties of one have
// converged across all three consumers, each measured or derived on their side
// before reaching us: how late its output is (latency), how long it keeps
// sounding after its input stops (tail), and the smallest block it can be run
// at (min_block). This header declares those three and checks their SHAPE.
//
// WHAT THIS SLICE DELIBERATELY DOES NOT DECLARE — read before adding to it:
//   - process() and loadState() signatures. Still OPEN: the process signature
//     waits on channel topology (port.h's open width axis) and on transport
//     (Sluice passes bpm to process; MAW and Bulwark do not); loadState's
//     argument shape exists in one consumer only. Slice 3 settled the two
//     questions that blocked the rest (DECISIONS #131): parameters are keyed by
//     ADDRESS, resolved once off the audio thread into a handle; and the sample
//     type is double (FxSample below).
//   - Channel count. A sidechain is another kAudio sink PORT (port.h), not a
//     larger integer; port width is an open axis there.
//   - Smoothing classes, meters, gain reduction. Open axis / host-side /
//     OQ #33's engine manifest respectively.
//
// The values are per PREPARED configuration: latency at 48 kHz with 4×
// oversampling differs from 44.1 kHz. So an OperatorProperties is a value a
// module reports after prepare(), and validate() is constexpr so it runs both
// in static_assert on fixtures and at runtime on what a module reports.
#pragma once

#include <cstdint>
#include <concepts>
#include <optional>
#include <string_view>

namespace foundations {

// Which way the reported whole-sample latency is off from the true group delay,
// after the host compensates. A residual WITHOUT a sign cannot tell the host
// whether the module is early or late, which is the only thing it would need
// the residual for (DECISIONS #121).
enum class ResidualSign : std::uint8_t {
  kNone,   // reported latency is exact
  kEarly,  // true delay < reported: output arrives early after compensation
  kLate,   // true delay > reported: output arrives late after compensation
};

// Latency in WHOLE host samples, because every host compensates whole samples.
// Rounding rule: NEAREST integer, ties UP. Both consumers that have a residual
// already follow it — Sluice 42.5 → 43 (0.5 early), MAW 0.25 → 0 (0.25 late).
// The sub-sample residual is a documented property, never returned as latency;
// a fractional latency enters the contract only when a consumer measures an
// audible need for one (DECISIONS #117).
struct LatencyDecl {
  std::uint32_t samples{0};
  double residual{0.0};  // |true − reported|, in host samples
  ResidualSign sign{ResidualSign::kNone};
};

// "Never decays to silence": a self-oscillating loop, a feedback network at
// full regeneration. A large finite number pretending to be infinite would be
// a lie the host plans buffers around. Same value both major host APIs use.
inline constexpr std::uint32_t kInfiniteTail = 0xFFFFFFFFu;

struct TailDecl {
  std::uint32_t samples{0};  // 0 = output stops when input stops; kInfiniteTail allowed
  // True if the tail can change after prepare() — a reverb's or a feedback
  // network's tail follows its decay/regeneration parameters. The NOTIFICATION
  // mechanism is the host adapter's business, not this header's. A module whose
  // tail is constant per configuration (all of Bulwark's) leaves this false.
  bool may_change{false};
};

// How a state load ends the OLD audio (DECISIONS #128). In every conforming
// module the NEW parameters land at the loaded values with no ramp-in; what
// differs is the old sound: kept in place, or crossfaded out from a fresh
// engine. transition_samples bounds that crossfade; 0 = in place (Bulwark).
// Sluice declares its 20 ms crossfade at the prepared rate.
struct LoadDecl {
  std::uint32_t transition_samples{0};
};

struct OperatorProperties {
  // EVERY FIELD STARTS UNDECLARED, and validate() refuses an undeclared
  // latency, tail or min_block. Slice 1 defaulted them to 0 / 0 / 1, which made
  // a FORGOTTEN field a confident false claim: a module that never set its tail
  // declared "no tail" and a host would cut a reverb off; one that never set
  // min_block declared "safe at n = 1", which is exactly ADR-175's feedback-loop
  // hazard. L0015 in our own header: a placeholder must be an absence, never a
  // value with meaning (DECISIONS #129).
  std::optional<LatencyDecl> latency{};
  std::optional<TailDecl> tail{};
  // The smallest block, in host samples, for which output is defined to equal
  // processing the same input in one call. 1 = any length, including one
  // sample at a time. A host that runs per-sample feedback cycles must not
  // place a module with min_block > 1 inside one, and must never call it with
  // 0 < n < min_block (maw-002; horde ADR-175). This header rules nothing about
  // audio-graph feedback itself — that is #113's, and the host's.
  std::optional<std::uint32_t> min_block{};
  // Undeclared here is LEGAL and has a stated meaning: the module offers no
  // load entry, so a host loads it through ordinary parameter changes (which
  // ramp). MAW has none. Declared = the module has a load entry with this bound.
  std::optional<LoadDecl> load{};

  enum class Violation : std::uint8_t {
    kNone,
    kLatencyUndeclared,     // a forgotten latency is not "zero latency"
    kTailUndeclared,        // a forgotten tail is not "no tail"
    kMinBlockUndeclared,    // a forgotten min_block is not "safe at n = 1"
    kZeroMinBlock,          // a block of 0 samples defines nothing
    kResidualNotFinite,     // NaN/inf residual is a typo, not a property
    kResidualOutOfRange,    // nearest-integer rounding bounds it to [0, 0.5]
    kResidualWithoutSign,   // a nonzero residual must say early or late
    kSignWithoutResidual,   // a sign on an exact latency says something false
    kTieRoundedDown,        // residual 0.5 reported LATE means ties went down
  };

  struct Check {
    Violation violation{Violation::kNone};
    constexpr explicit operator bool() const { return violation == Violation::kNone; }
  };

  // Shape checks only: each one is wrong under every candidate semantics, which
  // is the bar a doorframe check must clear (the mod_routing.h discipline).
  constexpr Check validate() const {
    if (!latency) return {Violation::kLatencyUndeclared};
    if (!tail) return {Violation::kTailUndeclared};
    if (!min_block) return {Violation::kMinBlockUndeclared};
    if (*min_block == 0) return {Violation::kZeroMinBlock};
    const double r = latency->residual;
    if (!(r == r) || r > 1e300 || r < -1e300) return {Violation::kResidualNotFinite};
    if (r < 0.0 || r > 0.5) return {Violation::kResidualOutOfRange};
    if (r > 0.0 && latency->sign == ResidualSign::kNone) return {Violation::kResidualWithoutSign};
    if (r == 0.0 && latency->sign != ResidualSign::kNone) return {Violation::kSignWithoutResidual};
    // Ties UP means a true delay of k + 0.5 reports k + 1: the output is EARLY.
    // A 0.5 residual reported late is only reachable by rounding ties down.
    if (r == 0.5 && latency->sign == ResidualSign::kLate) return {Violation::kTieRoundedDown};
    return {};
  }
};

// The lifecycle all three FX consumers already spell identically (DECISIONS
// #129). The signature is checkable; the SEMANTICS are not, so they are stated
// here and pinned by conformance tests, never by this concept:
//   prepare(sample_rate, max_block) — main thread; may allocate; after it, the
//     module reports its OperatorProperties for this configuration.
//   reset() — the BOTTOM rung of the #124/#128 ladder: snap every smoother to
//     the current settings, clear all audio state, rewind seeded RNG and
//     sample counters. Output after reset() must be bit-identical to a freshly
//     prepared instance with the same parameters. All three consumers test
//     exactly this today.
// max_block is taken as uint32_t; a prepare(double, int) satisfies it through
// the ordinary conversion, which is how MAW and Bulwark spell it.
template <class T>
concept FxOperatorLifecycle = requires(T& t, double sample_rate, std::uint32_t max_block) {
  t.prepare(sample_rate, max_block);
  t.reset();
};

// The operator's sample type (DECISIONS #131). All three FX consumers compute in
// double: MAW's and Bulwark's cores are double end to end; Sluice's arithmetic is
// double and only its CLAP boundary is float. Precision is a property of the
// CORE, so the operator works in double and the HOST ADAPTER converts at the
// boundary. Two obligations on that adapter, both from MAW, both load-bearing:
//   - conversion buffers are allocated in prepare(), never in process();
//   - conversion works at n = 1 with no internal chunking. A module declaring
//     min_block == 1 may be called one sample at a time inside a feedback cycle
//     (ADR-175), and an adapter that buffered to amortise conversion would add
//     latency the module declares as zero.
// Internal storage precision (Sluice's float32 delay lines) is the module's own
// business and does not cross the ABI.
using FxSample = double;

// Parameter entry (DECISIONS #131). The ADDRESS is the identity; the audio thread
// never sees a string. Both independent designs proposed this shape for
// different reasons from their own code: Sluice will not put a string compare
// on the audio thread per host event; MAW measured that Registry::byAddress is a
// linear scan with a compare per row, up to 85 per call, which is waste inside an
// n = 1 feedback cycle.
//   resolve(address) — OFF the audio thread (at prepare, or on registration).
//     Must be able to say "not found" (optional-like). A sentinel id such as 0
//     is a value standing in for absence and is only correct while nobody
//     assigns that id, which nothing enforces (L0015).
//   setParam(handle, value) — the audio-thread entry: O(1), allocation-free,
//     returns whether the handle was accepted. It ramps (the top rung of the
//     #124/#128 ladder).
// The handle type is the module's own (MAW: its registry id or row index; Sluice:
// its frozen host id). Host-facing ids remain the host adapter's, mapped through
// the registry.
template <class T>
concept FxOperatorParams = requires(T& t, std::string_view address, double value) {
  { static_cast<bool>(t.resolve(address)) };
  { t.setParam(*t.resolve(address), value) } -> std::convertible_to<bool>;
};

}  // namespace foundations
