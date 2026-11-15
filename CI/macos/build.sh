#!/bin/bash
set -euxo pipefail

# BUILD_DIR names another build tree (the unit-test tree is build-tests); the arguments are make targets.
cd "${BUILD_DIR:-build}"

if [[ $# -eq 0 ]]; then
    set -- package
fi

if [[ -n "${MACOS_AMD64:-}" ]]; then
    arch -x86_64 make -j "$(sysctl -n hw.logicalcpu)" "$@"
else
    make -j "$(sysctl -n hw.logicalcpu)" "$@"
fi
