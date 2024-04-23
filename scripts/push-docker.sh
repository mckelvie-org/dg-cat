#!/bin/bash

set -eo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR/.."
source "$SCRIPT_DIR/env-common.sh"

echo "Pushing docker images to Docker Hub; version: $DG_CAT_VERSION; user: $DOCKER_HUB_ORG_NAME"

docker buildx build --platform linux/amd64,linux/arm64 --target dg-cat-debug \
          -t $DOCKER_HUB_ORG_NAME/dg-cat:debug-$DG_CAT_VERSION \
          -t $DOCKER_HUB_ORG_NAME/dg-cat:debug-latest \
          --push .
docker buildx build --platform linux/amd64,linux/arm64 --target dg-cat-release \
          -t $DOCKER_HUB_ORG_NAME/dg-cat:$DG_CAT_VERSION \
          -t $DOCKER_HUB_ORG_NAME/dg-cat:latest \
          --push .
