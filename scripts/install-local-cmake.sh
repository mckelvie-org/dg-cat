#!/bin/bash

# This script installs CMake under $HOME/.local

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."
PROJECT_DIR="$(pwd)"
mkdir -p "$PROJECT_DIR/.tools/cmake"
mkdir -p build/tmp
cd build/tmp

# CMake version
CMAKE_VERSION="3.29.1"
ARCH="$(uname -m)"

curl -sL "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${ARCH}.sh" -o cmake-install.sh
chmod +x cmake-install.sh
./cmake-install.sh --prefix="$PROJECT_DIR/.tools/cmake" --skip-license
