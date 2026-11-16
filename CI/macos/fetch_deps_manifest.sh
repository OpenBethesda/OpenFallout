#!/bin/bash -e

# Downloads the manifest of the Apple Silicon dependency set to $1 (default /tmp/openmw-manifest.txt).
# The manifest is served from the main branch of openmw-deps, so the workflow hashes it into the dependency cache key.

source ./CI/macos/deps_versions.sh

VCPKG_FILE="vcpkg-arm64-osx-dynamic"

curl --fail --retry 3 -sSL "https://gitlab.com/OpenMW/openmw-deps/-/raw/main/macos/${VCPKG_FILE}-${VCPKG_TAG}-manifest.txt" -o "${1:-/tmp/openmw-manifest.txt}"
