#!/bin/bash

set -eo pipefail
#set -x

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR/.."
source "$SCRIPT_DIR/env-common.sh"

"$SCRIPT_DIR/build-docker.sh"

gh release create v$DG_CAT_VERSION -t v$DG_CAT_VERSION --generate-notes build/artifacts/*
