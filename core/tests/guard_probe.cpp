// guard_probe.cpp — compiled by ./verify's core_gate with -fsyntax-only: clean
// it must compile; under -ffast-math and -ffinite-math-only it must fail with
// the engine's "forbidden" #error. A guard nobody has seen fire is a guess.
#include "echo/engine.h"

int main() {
  echo::Echo e;
  return e.latency();
}
