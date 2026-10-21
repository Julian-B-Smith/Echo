// alloc_probe.h — counts heap allocations while armed (RS-7.7: nothing
// allocates in process / setParam / reset / loadState). INCLUDE FROM EXACTLY
// ONE TRANSLATION UNIT PER EXECUTABLE: it replaces the global operator new.
//
// Approach from Bulwark's core/tests/alloc_probe.h (sibling, same contract row):
//   - every operator new / new[] (throwing, nothrow, aligned) is replaced;
//   - on Darwin, malloc / calloc / realloc / posix_memalign are seen through
//     libmalloc's public `malloc_logger` hook (defining `malloc` in the test
//     binary does not interpose on Darwin's two-level namespace).
//   - NOT seen, by design: locks and syscalls. Those stay "by reading": the
//     core holds no mutex or condition variable and makes no I/O call
//     (`grep -n "mutex\|lock_guard\|condition_variable\|fopen\|printf"
//     core/include/echo/` finds none).
// It also sums the BYTES requested while armed, which is how the per-instance
// memory figure (1.0 bar, a MUST row) is measured at prepare().
#pragma once
// Under AddressSanitizer the probe STANDS DOWN: ASan owns operator new and
// malloc, and replacing them aborts its start-up. The sanitizer build (CI)
// then runs every check except the two that count allocations, and says so;
// those two run in the normal echo_tests build in the same CI job.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ECHO_ALLOC_PROBE 0
#endif
#endif
#if !defined(ECHO_ALLOC_PROBE) && defined(__SANITIZE_ADDRESS__)
#define ECHO_ALLOC_PROBE 0
#endif
#if !defined(ECHO_ALLOC_PROBE)
#define ECHO_ALLOC_PROBE 1
#endif
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#if defined(__APPLE__)
extern "C" {
extern void (*malloc_logger)(std::uint32_t type, std::uintptr_t a1, std::uintptr_t a2, std::uintptr_t a3,
                             std::uintptr_t result, std::uint32_t skip);
}
#endif

namespace alloc_probe {

inline std::atomic<long> g_allocs{0};
inline std::atomic<long long> g_bytes{0};
inline std::atomic<bool> g_armed{false};
// A new-family call also reaches malloc_logger: count it once. A plain global,
// NOT thread_local: malloc_logger runs inside malloc, and touching a
// thread_local there can recurse into TLS setup (it crashed, and it double-
// counted every allocation). The suite is single-threaded.
inline volatile bool t_inNew = false;

inline void note(std::size_t n) {
  if (g_armed.load(std::memory_order_relaxed)) {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(static_cast<long long>(n), std::memory_order_relaxed);
  }
}

#if defined(__APPLE__)
inline void logger(std::uint32_t type, std::uintptr_t, std::uintptr_t a2, std::uintptr_t, std::uintptr_t, std::uint32_t) {
  if ((type & 2u) && !t_inNew) note(static_cast<std::size_t>(a2));  // bit 2 = allocation; a2 = size
}
#endif

inline void arm() {
#if defined(__APPLE__) && ECHO_ALLOC_PROBE
  malloc_logger = &logger;
#endif
  g_allocs.store(0);
  g_bytes.store(0);
  g_armed.store(true);
}
inline void disarm() {
  g_armed.store(false);
#if defined(__APPLE__) && ECHO_ALLOC_PROBE
  malloc_logger = nullptr;
#endif
}

inline void* take(std::size_t n) {
  note(n);
  t_inNew = true;
  void* p = std::malloc(n ? n : 1);
  t_inNew = false;
  return p;
}
inline void* takeAligned(std::size_t n, std::size_t a) {
  note(n);
  t_inNew = true;
  void* p = nullptr;
  if (posix_memalign(&p, a < sizeof(void*) ? sizeof(void*) : a, n ? n : 1) != 0) p = nullptr;
  t_inNew = false;
  return p;
}

}  // namespace alloc_probe

#if ECHO_ALLOC_PROBE
// The replacements below allocate with malloc, so the matching deletes call
// free: correct by construction. GCC 13's -Wmismatched-new-delete cannot see
// that the pairing is ours and flags every delete (CI, -Werror); silenced for
// these definitions only, GCC only (clang has no such warning).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void* operator new(std::size_t n) {
  if (void* p = alloc_probe::take(n)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
  if (void* p = alloc_probe::take(n)) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return alloc_probe::take(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return alloc_probe::take(n); }
void* operator new(std::size_t n, std::align_val_t a) {
  if (void* p = alloc_probe::takeAligned(n, static_cast<std::size_t>(a))) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t n, std::align_val_t a) {
  if (void* p = alloc_probe::takeAligned(n, static_cast<std::size_t>(a))) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif  // ECHO_ALLOC_PROBE
