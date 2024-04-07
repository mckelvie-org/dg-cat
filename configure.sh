#!/bin/bash

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

mkdir -p build/Debug
mkdir -p build/Release

# Configure a debug build
cmake -S . -B build/Debug -D CMAKE_BUILD_TYPE=Debug

# Configure a release build
cmake -S . -B build/Release -D CMAKE_BUILD_TYPE=Release

# build the debug binaries
cmake --build build/Debug

# Build release binaries
cmake --build build/Release
