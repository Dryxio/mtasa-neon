#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output="$root/Build/project2dfx-tests"
mkdir -p "$output"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    "$root/Tests/standalone/Project2DFX_Tests.cpp" -o "$output/test"
"$output/test" "$root/Shared/data/MTA San Andreas/MTA/data/SALodLights.dat"
