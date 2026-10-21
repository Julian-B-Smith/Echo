# echocore — Echo's C++ core (ROADMAP M1). Header-only, standard library only, C++20.
# Build with Unix Makefiles and an ABSOLUTE build path: this Mac has Command
# Line Tools only (no Xcode, no Ninja), and the agent sandbox resets cwd between
# commands, so a relative `cmake --build build` builds nothing and exits 0.
cmake_minimum_required(VERSION 3.16)
project(echocore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-config generator: default to Release, the build every number means.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(echocore INTERFACE)
target_include_directories(echocore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(echocore INTERFACE cxx_std_20)
# No fp-contract flag here: an INTERFACE flag would change the numerics of every
# horde translation unit that links echocore (critic M7). The header scopes it
# itself with a pragma; echo_hash* below prove the pragma holds.

set(ECHO_WARN $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
# -Werror in ./verify (critic M5): grepping a build log for "warning:" goes
# green on a warm rebuild that recompiles nothing, with the warnings still there.
option(ECHO_WERROR "Treat warnings as errors" OFF)
if(ECHO_WERROR)
  set(ECHO_WARN ${ECHO_WARN} -Werror)
endif()
# Address + undefined-behaviour sanitizers on echo_tests (CI, Linux): the
# out-of-bounds read the critic found (H1) is what these exist to catch.
option(ECHO_SANITIZE "Build echo_tests with ASan + UBSan" OFF)

# echo_tests: the clean suite, exactly the code horde links (no plants).
add_executable(echo_tests tests/core_tests.cpp)
target_link_libraries(echo_tests PRIVATE echocore)
target_compile_options(echo_tests PRIVATE ${ECHO_WARN})
if(ECHO_SANITIZE)
  target_compile_options(echo_tests PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_link_options(echo_tests PRIVATE -fsanitize=address,undefined)
endif()

# echo_plants: the same suite with every planted fault switchable; each check
# must FAIL under each fault it guards (core/include/echo/plants.h).
add_executable(echo_plants tests/core_tests.cpp)
target_link_libraries(echo_plants PRIVATE echocore)
target_compile_definitions(echo_plants PRIVATE ECHO_PLANTS=1)
target_compile_options(echo_plants PRIVATE ${ECHO_WARN})

# echo_manifest: prints the C++ parameter table as JSON for
# tools/check_manifest.py, which compares it with spec/params.json.
add_executable(echo_manifest tools/manifest.cpp)
target_link_libraries(echo_manifest PRIVATE echocore)
target_compile_options(echo_manifest PRIVATE ${ECHO_WARN})

# The fp-contract probe (critic M7; see tools/render_hash.cpp). Clang's default
# contracting mode honours the pragma ("on"; "fast" would override it by
# design); GCC's ISO default is already "off", so it is probed with "fast".
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(ECHO_FPC_PROBE -ffp-contract=on)
else()
  set(ECHO_FPC_PROBE -ffp-contract=fast)
endif()
# Baseline x86-64 has no FMA instruction, so contraction has nothing to fuse
# into and the pragma-less control renders identically: the first Linux CI run
# reported the probe vacuous. -mfma (every CI runner has it) gives contraction an
# instruction; all three probe builds get it, so only fp-contract differs.
set(ECHO_FPC_ISA "")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
  set(ECHO_FPC_ISA -mfma)
endif()
add_executable(echo_hash_ref tools/render_hash.cpp)
target_link_libraries(echo_hash_ref PRIVATE echocore)
target_compile_options(echo_hash_ref PRIVATE ${ECHO_WARN} ${ECHO_FPC_ISA} -ffp-contract=off)
add_executable(echo_hash tools/render_hash.cpp)
target_link_libraries(echo_hash PRIVATE echocore)
target_compile_options(echo_hash PRIVATE ${ECHO_WARN} ${ECHO_FPC_ISA} ${ECHO_FPC_PROBE})
add_executable(echo_hash_nopragma tools/render_hash.cpp)
target_link_libraries(echo_hash_nopragma PRIVATE echocore)
target_compile_definitions(echo_hash_nopragma PRIVATE ECHO_FP_PRAGMA_OFF=1)
target_compile_options(echo_hash_nopragma PRIVATE ${ECHO_WARN} ${ECHO_FPC_ISA} ${ECHO_FPC_PROBE})
