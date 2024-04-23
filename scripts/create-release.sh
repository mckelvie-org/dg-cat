#!/bin/bash

set -eo pipefail
#set -x

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR/.."
source "$SCRIPT_DIR/env-common.sh"

git fetch origin main

# Update the index
git update-index -q --ignore-submodules --refresh

# Disallow unstaged changes in the working tree
if ! git diff-files --quiet --ignore-submodules --; then
    echo >&2 "Unstaged changes detected; please commit and push all changes and try again."
    exit 1
fi

# Disallow uncommitted changes in the index
if ! git diff-index --cached --quiet HEAD --ignore-submodules --; then
    echo >&2 "Uncommitted staged changed detected; please commit and push all changes and try again."
    exit 1
fi

if [ ( git log origin/main..HEAD | wc -c ) -gt 0 ]; then
    echo >&2 "Unpushed committed changes detected; please push all changes and try again"
    exit 1
fi

"$SCRIPT_DIR/build-docker.sh"

gh release create v$DG_CAT_VERSION -t v$DG_CAT_VERSION --generate-notes build/artifacts/*
