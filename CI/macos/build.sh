#!/bin/bash
set -euxo pipefail

# BUILD_DIR names another build tree (the unit-test tree is build-tests); the arguments are make targets.
cd "${BUILD_DIR:-build}"

if [[ $# -eq 0 ]]; then
    set -- package
fi

make -j "$(sysctl -n hw.logicalcpu)" "$@"
