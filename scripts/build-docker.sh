#!/bin/bash

set -eo pipefail
#set -x

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

for arch in $local_arch $other_arch; do
    docker buildx build --platform linux/$arch --target buildenv -t dg-cat-buildenv-$arch --load .
    docker buildx build --platform linux/$arch --target dg-cat-build -t dg-cat-build-$arch --load .
    docker buildx build --platform linux/$arch --target dg-cat-debug -t dg-cat-debug-$arch --load .
    docker buildx build --platform linux/$arch --target dg-cat-release -t dg-cat-release-$arch --load .
    docker buildx build --platform linux/$arch --target dg-cat-pkg-debug -t dg-cat-pkg-debug-$arch --load .
    docker buildx build --platform linux/$arch --target dg-cat-pkg-release -t dg-cat-pkg-release-$arch --load .
done

docker tag dg-cat-buildenv-$local_arch dg-cat-buildenv:latest
docker tag dg-cat-build-$local_arch dg-cat-build:latest
docker tag dg-cat-release-$local_arch dg-cat:latest
docker tag dg-cat-release-$local_arch $docker_username/dg-cat:$app_version
docker tag dg-cat-release-$local_arch $docker_username/dg-cat:latest
docker tag dg-cat-debug-$local_arch dg-cat-debug:latest
docker tag dg-cat-debug-$local_arch $docker_username/dg-cat:debug-$app_version
docker tag dg-cat-debug-$local_arch $docker_username/dg-cat:debug-latest

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
for arch in $local_arch $other_arch; do
    export_image dg-cat-pkg-debug-$arch build/artifacts/dg-cat-$app_version-$arch-debug.tar.gz
    export_image dg-cat-pkg-release-$arch build/artifacts/dg-cat-$app_version-$arch.tar.gz
done
