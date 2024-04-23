#!/bin/bash

set -eo pipefail
#set -x

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR/.."
source "$SCRIPT_DIR/env-common.sh"

for arch in $LOCAL_ARCH $OTHER_ARCH; do
    docker buildx build --platform linux/$arch --target buildenv -t dg-cat-buildenv-$arch --load .
    docker buildx build --platform linux/$arch --target dg-cat-build -t dg-cat-build-$arch --load .
    docker buildx build --platform linux/$arch --target dg-cat-debug -t dg-cat-debug-$arch --load .
    docker buildx build --platform linux/$arch --target dg-cat-release -t dg-cat-release-$arch --load .
    docker buildx build --platform linux/$arch --target dg-cat-pkg-debug -t dg-cat-pkg-debug-$arch --load .
    docker buildx build --platform linux/$arch --target dg-cat-pkg-release -t dg-cat-pkg-release-$arch --load .
done

docker tag dg-cat-buildenv-$LOCAL_ARCH dg-cat-buildenv:latest
docker tag dg-cat-build-$LOCAL_ARCH dg-cat-build:latest
docker tag dg-cat-release-$LOCAL_ARCH dg-cat:latest
docker tag dg-cat-release-$LOCAL_ARCH $DOCKER_HUB_ORG_NAME/dg-cat:$DG_CAT_VERSION
docker tag dg-cat-release-$LOCAL_ARCH $DOCKER_HUB_ORG_NAME/dg-cat:latest
docker tag dg-cat-debug-$LOCAL_ARCH dg-cat-debug:latest
docker tag dg-cat-debug-$LOCAL_ARCH $DOCKER_HUB_ORG_NAME/dg-cat:debug-$DG_CAT_VERSION
docker tag dg-cat-debug-$LOCAL_ARCH $DOCKER_HUB_ORG_NAME/dg-cat:debug-latest

export_image() {
    # set -x
    image_name="$1"
    gz_file="$2"
    arch="$(docker inspect --format='{{.Architecture}}' "$image_name")"
    id="$(docker create --platform="linux/$arch" "$image_name" /bin/sh)"
    if [ -z "$id" ]; then
        echo "Failed to create container for $image_name"
        exit 1
    fi
    ret=0
    docker cp $id:dg-cat/ - | gzip > "$gz_file" || ret=$?
    if [ $ret -ne 0 ]; then
        echo "Failed to export $image_name to $gz_file"
        docker rm $id
        exit $ret
    fi
    docker rm $id
}

rm -fr build/artifacts
mkdir -p build/artifacts
for arch in $LOCAL_ARCH $OTHER_ARCH; do
    export_image dg-cat-pkg-debug-$arch build/artifacts/dg-cat-$DG_CAT_VERSION-$arch-debug.tar.gz
    export_image dg-cat-pkg-release-$arch build/artifacts/dg-cat-$DG_CAT_VERSION-$arch.tar.gz
done
