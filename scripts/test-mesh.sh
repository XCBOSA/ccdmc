#!/bin/sh
set -eu
if [ "$#" -ne 1 ] || [ ! -f "$1/ccd_sdl.h" ] || [ ! -f "$1/ccd_sdl_gpu.h" ]; then
    printf 'Usage: sh %s /path/to/ccd-sdk/share/wasi-sysroot/include\n' "$0" >&2
    exit 2
fi
ccd_include=$(cd "$1" && pwd -P)
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/ccdmc-mesh.XXXXXX")
trap 'rm -f "$test_dir/mesh-tests"; rmdir "$test_dir"' EXIT
case $(uname -s) in
    Darwin) strip_unused=-Wl,-dead_strip ;;
    *) strip_unused=-Wl,--gc-sections ;;
esac

# Native SDL supplies the scheduler's clock. Discard the unused GPU entry
# points, leaving the real CPU decoder and pthread workers under sanitizers.
sdl_flags=$(pkg-config --cflags sdl3)
sdl_libs=$(pkg-config --libs sdl3)
run_mesh_tests() {
    printf 'Native mesh regression: %s\n' "$*"
    "${CCDMC_TEST_CC:-clang}" -std=c11 -D_POSIX_C_SOURCE=200809L \
        -Wall -Wextra -Werror -Wno-unknown-attributes "$@" -fsanitize=address,undefined \
        -ffunction-sections -fdata-sections "$strip_unused" -pthread $sdl_flags \
        -idirafter "$ccd_include" -IContents -ITests/core Contents/game/*.c Contents/world/*.c Contents/render/*.c \
        -x c - $sdl_libs -lm -o "$test_dir/mesh-tests" <<'MAIN'
#include "render/renderer.h"
int main(void) { return renderer_mesh_selftest(); }
MAIN
    "$test_dir/mesh-tests"
}

run_mesh_tests -O1
# Clang's non-deprecated spelling for -Ofast.
run_mesh_tests -O3 -ffast-math
