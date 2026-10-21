// echo/ftz.h — flush-to-zero / denormals-are-zero for the duration of a scope
// (RS-7.8). The caller's floating-point control word is restored on exit, so a
// host that relies on its own setting is not disturbed.
//
// Shape taken from Bulwark's core/include/bulwark/ftz.h (sibling, same contract
// row). CORRECTNESS NEVER DEPENDS ON THIS: Echo's exact-zero guarantees come
// from the tail gate clearing state (SPEC §10), not from flushing. FTZ only
// removes the slow path of a subnormal the gate has not reached yet.
//
//   x86-64   MXCSR bit 15 (FTZ) and bit 6 (DAZ)
//   arm64    FPCR bit 24 (FZ; one flag covers both directions)
//   others   no-op
#pragma once
#include <cstdint>

#include "echo/plants.h"

#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace echo {

// The raw control word, or 0 where unsupported. Tests use it to prove that
// process() restores what it found.
inline std::uint64_t fpControlWord() {
#if defined(__x86_64__)
  return _mm_getcsr();
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
  std::uint64_t r;
  asm volatile("mrs %0, fpcr" : "=r"(r) : : "memory");
  return r;
#else
  return 0;
#endif
}

class FtzScope {
 public:
  FtzScope() : saved_(fpControlWord()) {
    if (!plant::ftzNeverSet) write(saved_ | kFlushBits);
  }
  ~FtzScope() {
    if (!plant::ftzNotRestored) write(saved_);
  }
  FtzScope(const FtzScope&) = delete;
  FtzScope& operator=(const FtzScope&) = delete;

 private:
#if defined(__x86_64__)
  static constexpr std::uint64_t kFlushBits = (1u << 15) | (1u << 6);
#elif defined(__aarch64__)
  static constexpr std::uint64_t kFlushBits = 1ull << 24;
#else
  static constexpr std::uint64_t kFlushBits = 0;
#endif
  static void write([[maybe_unused]] std::uint64_t v) {
#if defined(__x86_64__)
    _mm_setcsr(static_cast<unsigned>(v));
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    // "memory" clobber: no audio load/store may move across the write.
    asm volatile("msr fpcr, %0" : : "r"(v) : "memory");
#endif
  }
  std::uint64_t saved_;
};

}  // namespace echo
