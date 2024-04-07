#!/bin/bash

BOOST_VERSION=1.84.0
BOOST_VERSION_UNDERSCORE="$(echo "$BOOST_VERSION" | tr '.' '_')"

set -eo pipefail

SCRIPTDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPTDIR"


mkdir -p build/boost
cd build/boost
rm -fr "boost_$BOOST_VERSION_UNDERSCORE"
curl -Ls https://boostorg.jfrog.io/artifactory/main/release/$BOOST_VERSION/source/boost_$BOOST_VERSION_UNDERSCORE.tar.gz -o boost.tar.gz
tar -xzf boost.tar.gz
cd "boost_$BOOST_VERSION_UNDERSCORE"
./bootstrap.sh --prefix="$HOME/.local"
./b2 install
