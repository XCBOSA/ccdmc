#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
project_root=$(pwd -P)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/ccdmc-core.XXXXXX")
trap 'rm -f "$test_dir/core-tests" "$test_dir/game" "$test_dir/world" "$test_dir/save"; rm -f "$test_dir/tests/"*.c "$test_dir/tests/"*.h; rmdir "$test_dir/tests" "$test_dir"' EXIT

# Match the opt-in CCD test package layout to preserve relative includes.
# Clang resolves .. against the real tests directory, so stage the small suite
# there. The game rules and persistence are still built from the live checkout.
mkdir "$test_dir/tests"
cp Tests/core/*.c Tests/core/*.h "$test_dir/tests/"
for dir in game world save; do ln -s "$project_root/Contents/$dir" "$test_dir/$dir"; done
"${CCDMC_TEST_CC:-clang}" -std=c11 -Wall -Wextra -Werror -O1 \
    -fsanitize=address,undefined -pthread -I"$test_dir" \
    Contents/game/*.c Contents/world/*.c Contents/save/*.c "$test_dir"/tests/*.c \
    -x c - -lm -o "$test_dir/core-tests" <<'MAIN'
#include "tests/world_test.h"
#include "tests/save_test.h"
int main(void) {
    int world_failed = world_selftest();
    int save_failed = save_selftest();
    return world_failed || save_failed;
}
MAIN
cd "$test_dir"
./core-tests
