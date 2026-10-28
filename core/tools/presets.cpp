// presets.cpp — print Echo's factory bank (echo/presets.h, the one source; D-042)
// as presets/echo-factory.json. ./verify compares the committed file with this.
#include <cstdio>
#include <string>
#include <string_view>

#include "echo/presets.h"

int main(int argc, char** argv) {
  // --resolve: resolve every factory preset at its homes and print one hash line.
  // It exists so resolve() is compiled into echo_presets_fpc / _nopragma, whose
  // fused-op counts ./verify compares (the no-FMA invariant for presets.h).
  if (argc > 1 && std::string_view(argv[1]) == "--resolve") {
    double acc = 0.0;
    for (const auto& p : echo::presets::factory())
      for (double v : echo::presets::resolve(p, {})) acc = acc * 1.0000001 + v;
    std::printf("%.17g\n", acc);
    return 0;
  }
  if (argc < 2 || std::string_view(argv[1]) != "--json") {
    std::fprintf(stderr, "usage: echo_presets --json | --resolve\n");
    return 2;
  }
  const auto bank = echo::presets::factory();
  for (const auto& p : bank) {
    std::string why;
    if (!echo::presets::validate(p, &why)) {
      std::fprintf(stderr, "echo_presets: factory preset %s is invalid: %s\n", p.id.c_str(), why.c_str());
      return 1;
    }
  }
  std::fputs(echo::presets::writeBank(bank).c_str(), stdout);
  return 0;
}
