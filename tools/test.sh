#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$project_dir/build/host-tests"
mkdir -p "$test_dir"
# Host stubs are isolated from the real IDF build. GNU++26 matches firmware;
# sanitizers exercise bounds/lifetime errors in the tested production modules.
"${CXX:-g++}" -std=gnu++26 -Wall -Wextra -Wpedantic -Wconversion \
    -Wsign-conversion -Wshadow -Werror -pthread -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$project_dir/tests/fakes" -I "$project_dir/main" \
    "$project_dir/tests/firmware_tests.cpp" "$project_dir/main/model.cpp" \
    "$project_dir/main/shared_state.cpp" "$project_dir/main/camera.cpp" \
    -o "$test_dir/firmware_tests"
"$test_dir/firmware_tests"
node "$project_dir/tests/browser_tests.mjs"
python3 "$project_dir/tests/wifi_config_tests.py"
