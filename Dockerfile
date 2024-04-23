FROM ubuntu:22.04 as buildenv

ARG BOOST_VERSION=1.84.0
ARG CMAKE_VERSION=3.29.1
ARG NUM_JOBS=8
ENV DEBIAN_FRONTEND noninteractive
ENV LC_ALL en_US.UTF-8
ENV LANG en_US.UTF-8
ENV LANGUAGE en_US.UTF-8

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        software-properties-common \
        autoconf \
        automake \
        libtool \
        pkg-config \
        ca-certificates \
        libssl-dev \
        wget \
        git \
        curl \
        language-pack-en \
        locales \
        locales-all \
        vim \
        gdb \
        valgrind && \
    apt-get clean

RUN mkdir -p /tmp/cmake && \
    cd /tmp/cmake && \
    ARCH="$(uname -m)" && \
    curl -sL "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${ARCH}.sh" -o cmake-install.sh && \
    chmod +x cmake-install.sh && \
    ./cmake-install.sh --prefix="/usr/local" --skip-license && \
    rm -rf /tmp/* && \
    echo "cmake $CMAKE_VERSION installed"
    
# See https://www.boost.org/doc/libs/1_84_0/more/getting_started/unix-variants.html
RUN cd /tmp && \
    BOOST_VERSION_MOD="$(echo $BOOST_VERSION | tr . _)" && \
    wget "https://boostorg.jfrog.io/artifactory/main/release/${BOOST_VERSION}/source/boost_${BOOST_VERSION_MOD}.tar.bz2" && \
    tar --bzip2 -xf boost_${BOOST_VERSION_MOD}.tar.bz2 && \
    cd boost_${BOOST_VERSION_MOD} && \
    ./bootstrap.sh --prefix=/usr/local && \
    ./b2 install && \
    rm -rf /tmp/* && \
    echo "boost $BOOST_VERSION installed"

FROM buildenv as dg-cat-build

COPY . /build

RUN cd /build && \
    mkdir -p build/Release && \
    cd build/Release && \
    cmake ../.. -D CMAKE_BUILD_TYPE=Release && \
    make -j${NUM_JOBS} && \
    echo "Built dg-cat Release version"

RUN cd /build && \
    mkdir -p build/Debug && \
    cd build/Debug && \
    cmake ../.. -D CMAKE_BUILD_TYPE=DEBUG && \
    make -j${NUM_JOBS} && \
    cp ./dg-cat /usr/local/bin/dg-cat-debug && \
    chmod +x /usr/local/bin/dg-cat-debug && \
    chown root:root /usr/local/bin/dg-cat-debug && \
    echo "Built dg-cat-debug Debug version"

FROM dg-cat-build as dg-cat-install-release

RUN cd /build/build/Release && \
    make install && \
    echo "Installed dg-cat Release version"

FROM dg-cat-build as dg-cat-install-debug

RUN cd /build/build/Debug && \
    make install && \
    echo "Installed dg-cat Debug version"

FROM scratch as dg-cat-pkg-src

COPY --from=dg-cat-install-release /usr/local/include/dg_cat /dg-cat/include/dg_cat
COPY --from=dg-cat-install-release /usr/local/lib/cmake/DgCat /dg-cat/lib/cmake/DgCat

FROM scratch as dg-cat-pkg-release

COPY --from=dg-cat-install-release /usr/local/bin/dg-cat /dg-cat/bin/
COPY --from=dg-cat-install-release /usr/local/lib/libdg_cat.a /dg-cat/lib/
COPY --from=dg-cat-install-release /usr/local/include/dg_cat /dg-cat/include/dg_cat
COPY --from=dg-cat-install-release /usr/local/lib/cmake/DgCat /dg-cat/lib/cmake/DgCat

FROM scratch as dg-cat-pkg-debug

COPY --from=dg-cat-install-debug /usr/local/bin/dg-cat /dg-cat/bin/
COPY --from=dg-cat-install-debug /usr/local/lib/libdg_cat.a /dg-cat/lib/
COPY --from=dg-cat-install-debug /usr/local/include/dg_cat /dg-cat/include/dg_cat
COPY --from=dg-cat-install-debug /usr/local/lib/cmake/DgCat /dg-cat/lib/cmake/DgCat

FROM ubuntu:22.04 as runtime-env

RUN apt-get clean

FROM runtime-env as dg-cat-debug

COPY --from=dg-cat-install-debug /usr/local/bin/dg-cat /usr/local/bin/

ENTRYPOINT [ "/usr/local/bin/dg-cat"]

FROM runtime-env as dg-cat-release

COPY --from=dg-cat-install-release /usr/local/bin/dg-cat /usr/local/bin/

ENTRYPOINT [ "/usr/local/bin/dg-cat"]

# Default is to run the release version
FROM dg-cat-release
ENTRYPOINT [ "/usr/local/bin/dg-cat"]
