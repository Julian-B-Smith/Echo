// render_hash.cpp — renders one fixed scenario (seeded noise through every
// filter, feedback, a routing switch and a time move) and prints an FNV-1a
// 64-bit hash of the output's bits. ./verify builds it three ways to prove the
// header's fp-contract pragma (critic M7):
//   echo_hash_ref        -ffp-contract=off        (the reference)
//   echo_hash            the compiler's contracting mode, pragma in force: must EQUAL ref
//   echo_hash_nopragma   the same mode, pragma removed:                    must DIFFER
// The last one is the planted control: if contraction changed nothing, the
// probe could not see a broken pragma, and the gate says so.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "echo/engine.h"

int main() {
  echo::Echo e;
  e.prepare(48000, 512);
  for (auto [k, v] : {std::pair<const char*, double>{"time.sync", 0}, {"time.ms", 90}, {"fb.amount", 70}, {"mix", 60},
                      {"filt.in.on", 1}, {"filt.in.hpHz", 120}, {"filt.out.on", 1}, {"filt.out.lpHz", 7000}})
    e.setParam(k, v);
  const int n = 96000;
  std::vector<double> L(n), R(n), oL(n), oR(n);
  std::uint32_t s = 1;
  for (int i = 0; i < n; ++i) {
    s = s * 1664525u + 1013904223u;
    L[i] = (static_cast<double>(s >> 8) / 16777216.0 - 0.5) * (i < 24000 ? 0.8 : 0.0);
    R[i] = L[i] * 0.7;
  }
  for (int p = 0; p < n; p += 512) {
    if (p == 30720) e.setParam("route.mode", 2);
    if (p == 40960) e.setParam("time.ms", 140);
    e.process(L.data() + p, R.data() + p, oL.data() + p, oR.data() + p, 512, 120);
  }
  std::uint64_t h = 1469598103934665603ull;
  for (const auto* v : {&oL, &oR})
    for (double x : *v) {
      std::uint64_t b;
      std::memcpy(&b, &x, 8);
      for (int k = 0; k < 8; ++k) { h ^= (b >> (8 * k)) & 0xFF; h *= 1099511628211ull; }
    }
  std::printf("%016llx\n", static_cast<unsigned long long>(h));
  return 0;
}
