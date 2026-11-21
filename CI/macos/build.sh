#!/bin/bash
set -euxo pipefail

# BUILD_DIR names another build tree (the unit-test tree is build-tests); the arguments are make targets.
cd "${BUILD_DIR:-build}"

if [[ $# -eq 0 ]]; then
    # the package target no longer builds everything first (CMAKE_SKIP_INSTALL_ALL_DEPENDENCY in before_script.macos.sh)
    set -- all package
fi

make -j "$(sysctl -n hw.logicalcpu)" "$@"
