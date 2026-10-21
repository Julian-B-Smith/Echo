// manifest.cpp — print echo/params.h as JSON, one parameter per line, for
// tools/check_manifest.py to compare with spec/params.json (the authority).
#include <cstdio>

#include "echo/params.h"

int main() {
  const char* morph[] = {"continuous", "jump", "device"};
  std::printf("[\n");
  for (std::size_t i = 0; i < echo::kParams.size(); ++i) {
    const auto& p = echo::kParams[i];
    std::printf(" {\"key\":\"%.*s\",\"name\":\"%.*s\",\"unit\":\"%.*s\",\"type\":\"%s\",\"min\":%.17g,\"max\":%.17g,\"def\":%.17g,"
                "\"morph\":\"%s\",\"law\":\"%.*s\",\"labels\":[",
                static_cast<int>(p.key.size()), p.key.data(), static_cast<int>(p.name.size()), p.name.data(),
                static_cast<int>(p.unit.size()), p.unit.data(), p.stepped ? "stepped" : "scalar", p.min, p.max, p.def,
                morph[static_cast<int>(p.morph)], static_cast<int>(p.law.size()), p.law.data());
    for (std::size_t j = 0; j < p.labels.size(); ++j)
      std::printf("%s\"%.*s\"", j ? "," : "", static_cast<int>(p.labels[j].size()), p.labels[j].data());
    std::printf("]}%s\n", i + 1 < echo::kParams.size() ? "," : "");
  }
  std::printf("]\n");
  return 0;
}
