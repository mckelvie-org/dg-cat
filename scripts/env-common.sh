SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
if [ -f "$SCRIPT_DIR/../.env" ]; then
    source "$SCRIPT_DIR/../.env"
fi
if [ -z "$DOCKER_HUB_ORG_NAME" ]; then
    DOCKER_HUB_ORG_NAME="$(docker info | sed '/Username:/!d;s/.* //')"
fi
if [ -z "$DG_CAT_VERSION" ]; then
    DG_CAT_VERSION="$(cat "$SCRIPT_DIR/../include/dg_cat/version.hpp" | sed '/DG_CAT_VERSION=/!d;s/.*"\(.*\)".*/\1/')"
fi
if [ -z "$LOCAL_OS_TYPE" ]; then
    LOCAL_OS_TYPE="$(docker system info --format '{{.OSType}}')"
fi
if [ "$LOCAL_OS_TYPE" != "linux" ]; then
    echo "Unsupported ostype: $LOCAL_OS_TYPE"
    exit 1
fi
if [ -z "$LOCAL_ARCH" ]; then
    LOCAL_ARCH="$(docker system info --format '{{.Architecture}}')"
fi
if [ "$LOCAL_ARCH" == "x86_64" ]; then
    LOCAL_ARCH="amd64"
elif [ "$LOCAL_ARCH" == "aarch64" ]; then
    LOCAL_ARCH="arm64"
fi
if [ "$LOCAL_ARCH" == "amd64" ]; then
    OTHER_ARCH="arm64"
elif [ "$LOCAL_ARCH" == "arm64" ]; then
    OTHER_ARCH="amd64"
else
    echo "Unsupported architecture: $LOCAL_ARCH"
    exit 1
fi
