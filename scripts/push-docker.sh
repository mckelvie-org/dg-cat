#!/bin/bash

set -eo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR/.."

docker_username="$(docker info | sed '/Username:/!d;s/.* //')"
app_version="$(cat "$SCRIPT_DIR/../include/dg_cat/version.hpp" | sed '/DG_CAT_VERSION=/!d;s/.*"\(.*\)".*/\1/')"
local_ostype="$(docker system info --format '{{.OSType}}')"
if [ "$local_ostype" != "linux" ]; then
    echo "Unsupported ostype: $local_ostype"
    exit 1
fi
local_arch="$(docker system info --format '{{.Architecture}}')"
if [ "$local_arch" == "x86_64" ]; then
    local_arch="amd64"
    other_arch="arm64"
elif [ "$local_arch" == "aarch64" ]; then
    local_arch="arm64"
    other_arch="amd64"
else
    echo "Unsupported architecture: $local_arch"
    exit 1
fi

echo "Pushing docker images to Docker Hub; version: $app_version; user: $docker_username"

docker buildx build --platform linux/amd64,linux/arm64 --target dg-cat-debug \
          -t $docker_username/dg-cat:debug-$app_version \
          -t $docker_username/dg-cat:debug-latest \
          --push .
docker buildx build --platform linux/amd64,linux/arm64 --target dg-cat-release \
          -t $docker_username/dg-cat:$app_version \
          -t $docker_username/dg-cat:latest \
          --push .
