ARG BaseImageTag=latest

FROM utpl/base:${BaseImageTag} as gstreamer-test-builder

RUN \
    sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/ubuntu.sources && \
    apt update

RUN \
    DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt install -y --no-install-recommends --no-install-suggests \
    gcc \
    g++ \
    cmake \
    make \
    pkg-config \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    \
    dpkg-source-gitarchive \
    build-essential \
    fakeroot \
    devscripts

# Copy sources
COPY . /tmp/transcoder

# Patch gstreamer
RUN \
    DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt build-dep -y --no-install-recommends --no-install-suggests gstreamer1.0-plugins-bad 

RUN \
    mkdir -p /tmp/deb_build && \
    cd /tmp/deb_build && \
        apt source gstreamer1.0-plugins-bad && \
        cd gst-plugins-bad1.0-1.24.2/ && \
            for patchFile in $(find /tmp/transcoder/patches/gstreamer1.0-plugins-bad/ -type f -name '*.patch' -printf "%f\t%p\n" | sort -k1 | cut -d "`printf \"\t\"`" -f2); do echo "Patching with: $patchFile" && patch -p1 < $patchFile; done && \
            EMAIL=unknown debchange -i "Patched for UTPL project" && \
            EDITOR=/bin/true dpkg-source --commit . utpl-patch && \
            debuild -us -uc -i -I && \
            mkdir -p /tmp/built/deb && \
            mv ../*.deb /tmp/built/deb

# Build transcoder plugins
RUN \
    cd /tmp/transcoder && \
    mkdir -p build && \
    cd build && \
    cmake -DGST_PLUGINS_DIR=/tmp/built/gstreamer/dlbpat .. && \
    make -j && \
    make install && \
    echo -n "$(pkg-config --variable=pluginsdir gstreamer-1.0)" > /tmp/built/gstreamer/plugin-dir.txt


FROM utpl/base:${BaseImageTag} as gstreamer-test

RUN \
    DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt install -y --no-install-recommends --no-install-suggests \
    ca-certificates \
    ffmpeg \
    gstreamer1.0-tools \
    gstreamer1.0-vaapi \
    i965-va-driver \
    intel-media-va-driver \
    nvidia-vaapi-driver

COPY --from=gstreamer-test-builder [ "/tmp/built", "/tmp/built"]

RUN \
    mv /tmp/built/gstreamer/dlbpat/* $(cat /tmp/built/gstreamer/plugin-dir.txt)/ && \
    dpkg -i /tmp/built/deb/libgstreamer-plugins-bad1.0-0*.deb && \
    dpkg -i /tmp/built/deb/gstreamer1.0-plugins-bad*.deb && \
    rm -rf /tmp/built
