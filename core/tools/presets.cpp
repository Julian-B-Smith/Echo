// presets.cpp — print Echo's factory bank (echo/presets.h, the one source; D-042)
// as presets/echo-factory.json. ./verify compares the committed file with this.
#include <cstdio>
#include <string>
#include <string_view>

#include "echo/presets.h"

int main(int argc, char** argv) {
  if (argc < 2 || std::string_view(argv[1]) != "--json") {
    std::fprintf(stderr, "usage: echo_presets --json\n");
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
