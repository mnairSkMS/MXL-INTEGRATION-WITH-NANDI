ARG SrcImageBranch=ubuntu
ARG SrcImageTag=24.04

FROM ${SrcImageBranch}:${SrcImageTag} as base-builder

RUN \
    sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/ubuntu.sources && \
    apt update && \
    DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt install -y --no-install-recommends --no-install-suggests \
    gcc \
    g++ \
    cmake \
    make \
    pkg-config \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-good1.0-dev \
    \
    dpkg-source-gitarchive \
    build-essential \
    fakeroot \
    devscripts \
    debhelper-compat \
    libcurl4-openssl-dev \
    libssl-dev \
    \
    cargo \
    curl \
    ca-certificates \
    gstreamer1.0-tools

ENV CPR_VERSION=1.10.5
RUN \
   mkdir -p /tmp/built/deb && \
   cd /tmp/ && \
   git clone --branch $CPR_VERSION --depth 1 https://github.com/libcpr/cpr.git && \
   VERSION=$CPR_VERSION cpr/package-build/build-package.sh cpr && \
   rm -rf cpr && \
   cp libcpr*.deb /tmp/built/deb

# Patch gstreamer
RUN \
    DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt build-dep -y --no-install-recommends --no-install-suggests gstreamer1.0-plugins-bad 
# Copy sources
# Only the patches this stage applies: a patch for another stage (transcoder/patches/mxl) must not rebuild the GStreamer packages
COPY transcoder/patches/gstreamer1.0-plugins-bad /tmp/transcoder/patches/gstreamer1.0-plugins-bad
COPY transcoder/patches/gst-plugins-rs /tmp/transcoder/patches/gst-plugins-rs

RUN \
    mkdir -p /tmp/deb_build && \
    cd /tmp/deb_build && \
        apt source gstreamer1.0-plugins-bad && \
        cd gst-plugins-bad1.0-1.24.2/ && \
            for patchFile in $(find /tmp/transcoder/patches/gstreamer1.0-plugins-bad/ -type f -name '*.patch' -printf "%f\t%p\n" | sort -k1 | cut -d "`printf \"\t\"`" -f2); do echo "Patching with: $patchFile" && patch -p1 < $patchFile; done && \
            EMAIL=unknown debchange -i "Patched for UTPL project" && \
            EDITOR=/bin/true dpkg-source --commit . utpl-patch && \
            debuild -us -uc -i -I && \
            mv ../*.deb /tmp/built/deb

# Compilation of extra Rust plugins (fallbackswitch).
#
# CMAF DASH output: dashsink2 exists only after the GStreamer 1.24 tags and needs a newer Rust than
# Ubuntu's cargo, so it is built from a pinned commit with a rustup toolchain and patched from
# transcoder/patches/gst-plugins-rs. It sets cmafmux properties (manual-split, send-force-keyunit)
# that fmp4 0.12 lacks, so fmp4 is built from 0.14.5, the newest release that still builds against
# GStreamer 1.24, instead of from the GStreamer tag. The other plugins keep Ubuntu's cargo.
ARG DASH_RUST_TOOLCHAIN=1.99.1
ARG FMP4_PLUGIN_TAG=0.14.5
ARG DASHSINK2_COMMIT=be6a5cfc28aa22b119f0568549fe8d72cc2e7beb
RUN \
    cargo install cargo-deb@2.11.0 --locked && \
    cd /tmp && \
    git clone https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs.git && \
    cd gst-plugins-rs && \
        git checkout gstreamer-`gst-launch-1.0 --version | grep -Po "^GStreamer\s+\K(\d+\.\d+\.\d+)"` && \
        cp Cargo.toml Cargo.toml.orig && \
        cargo build -p gst-plugin-fallbackswitch --release && \
        cargo build -p gst-plugin-aws --release && \
        cargo build -p gst-plugin-ndi --release && \
    \
    curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain none --no-modify-path && \
    /root/.cargo/bin/rustup toolchain install ${DASH_RUST_TOOLCHAIN} --profile minimal && \
    git worktree add /tmp/gst-plugins-rs-fmp4 ${FMP4_PLUGIN_TAG} && \
    ( cd /tmp/gst-plugins-rs-fmp4 && /root/.cargo/bin/cargo +${DASH_RUST_TOOLCHAIN} build -p gst-plugin-fmp4 --release ) && \
    git worktree add /tmp/gst-plugins-rs-dash ${DASHSINK2_COMMIT} && \
    ( cd /tmp/gst-plugins-rs-dash && \
        for patchFile in $(find /tmp/transcoder/patches/gst-plugins-rs/ -type f -name '*.patch' | sort); do echo "Patching with: $patchFile" && patch -p1 < $patchFile; done && \
        /root/.cargo/bin/cargo +${DASH_RUST_TOOLCHAIN} build -p gst-plugin-dashsink2 --release ) && \
    cp /tmp/gst-plugins-rs-fmp4/target/release/libgstfmp4.so /tmp/gst-plugins-rs-dash/target/release/libgstdashsink2.so target/release/ && \
        strip target/release/*.so && \
    \
    CARGO_TOML=./utils/fallbackswitch/Cargo.toml && \
    TARGET_PLUGINS_DIR=$(pkg-config --variable=pluginsdir gstreamer-1.0) && \
    echo "\
[package.metadata.deb]\n\
assets = [ \n\
    ['target/release/*.so', '%GST_PLUGINS_DIR%/', '644'] \n\
] \n\
" >> $CARGO_TOML && \
    sed -i "s@%GST_PLUGINS_DIR%@$TARGET_PLUGINS_DIR@" $CARGO_TOML && \
    cargo deb -p gst-plugin-fallbackswitch --no-build && \
    mv target/debian/*.deb /tmp/built/deb


# MXL (Media eXchange Layer): SDK, tools and the gst-mxl-rs GStreamer plugin (mxlsink / mxlsrc).
# Built in its own stage so a change here does not rebuild the GStreamer packages above.
# ENABLE_MXL=0 builds an empty stage, which restores the image as it was before MXL.
# Fabrics (libfabric) is not built; see doc/codebase/42-mxl-implementation-plan.md.
FROM ${SrcImageBranch}:${SrcImageTag} as mxl-builder

ARG ENABLE_MXL=1
ARG MXL_TAG=v1.1.0
# MXL needs Rust >= 1.87 (edition 2024); its own devcontainer uses 1.90.0.
ARG MXL_RUST_TOOLCHAIN=1.90.0
ARG MXL_JOBS=4

# The Rust bindings are generated with bindgen (libclang) and load libmxl.so at run time.
RUN [ "${ENABLE_MXL}" = "1" ] || exit 0; \
    apt update && \
    DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt install -y --no-install-recommends --no-install-suggests \
        build-essential cmake ninja-build pkg-config nasm autoconf automake libtool bison flex \
        git curl ca-certificates zip unzip tar python3 \
        libclang-dev \
        libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev

# vcpkg supplies MXL's C++ dependencies. CMakePresets.json expects it in ~/vcpkg.
RUN [ "${ENABLE_MXL}" = "1" ] || exit 0; \
    git clone https://github.com/microsoft/vcpkg /root/vcpkg && \
    /root/vcpkg/bootstrap-vcpkg.sh -disableMetrics
ENV VCPKG_ROOT=/root/vcpkg

# The source stays at this fixed path: the Rust plugin, built with `mxl-not-built`, records
# <repo>/build/Linux-Clang-Release/lib/libmxl.so at compile time as the library to load. The base
# stage below recreates that one path as a symlink to the installed library.
RUN [ "${ENABLE_MXL}" = "1" ] || exit 0; \
    git clone --depth 1 --branch ${MXL_TAG} https://github.com/dmf-mxl/mxl.git /opt/mxl-src

RUN [ "${ENABLE_MXL}" = "1" ] || exit 0; \
    cd /opt/mxl-src && \
    cmake --preset Linux-GCC-Release -DBUILD_TESTS=OFF -DBUILD_DOCS=OFF -DCMAKE_INSTALL_PREFIX=/opt/mxl-install && \
    cmake --build build/Linux-GCC-Release -j${MXL_JOBS} && \
    cmake --install build/Linux-GCC-Release

RUN [ "${ENABLE_MXL}" = "1" ] || exit 0; \
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain ${MXL_RUST_TOOLCHAIN} --no-modify-path

# Patches to the MXL Rust plugin (transcoder/patches/mxl). Applied after the C++ build, because they
# only touch rust/, so a changed patch does not rebuild libmxl.
COPY transcoder/patches/mxl /tmp/mxl-patches
RUN [ "${ENABLE_MXL}" = "1" ] || exit 0; \
    cd /opt/mxl-src && \
    for patchFile in $(find /tmp/mxl-patches -type f -name '*.patch' | sort); do \
        echo "Patching MXL with: $patchFile" && git apply --verbose "$patchFile" || exit 1; \
    done

RUN [ "${ENABLE_MXL}" = "1" ] || exit 0; \
    cd /opt/mxl-src/rust && \
    /root/.cargo/bin/cargo build --release --locked -p gst-mxl-rs --features mxl/mxl-not-built && \
    /root/.cargo/bin/cargo build --release --locked -p gst-avsynctest-rs && \
    mkdir -p /opt/mxl-plugins && \
    cp target/release/libgstmxl.so target/release/libgstavsynctest.so /opt/mxl-plugins/

RUN mkdir -p /opt/mxl-install /opt/mxl-plugins


FROM ${SrcImageBranch}:${SrcImageTag} as base

RUN \
    apt update && \
    DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt install -y --no-install-recommends --no-install-suggests \
        libgstreamer1.0-0 \
        gstreamer1.0-libav \
        gstreamer1.0-plugins-base \
        gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-ugly \
        gstreamer1.0-fdkaac \
        gstreamer1.0-vaapi \
        \
        i965-va-driver \
        intel-media-va-driver \
        nvidia-vaapi-driver \
        \
        libsrt1.5-openssl \
        libpcsclite1 \
        python3.12 \
        python3-pip \
        python3.12-venv \
        libpugixml1v5 \
        libcurl4t64 \
        \
        libprotobuf32t64 \
        libgrpc++1.51t64


COPY --from=base-builder [ "/tmp/built/deb", "/opt/utpl/deb" ]

RUN \
    apt -y install --no-install-recommends --no-install-suggests $(find /opt/utpl/deb/ -name libgstreamer-plugins-bad1.0-0*.deb) && \
    apt -y install --no-install-recommends --no-install-suggests $(find /opt/utpl/deb/ -name gstreamer1.0-plugins-bad_*.deb) && \
    apt -y install --no-install-recommends --no-install-suggests $(find /opt/utpl/deb/ -name gst-plugin-fallbackswitch_*.deb) && \
    apt -y install --no-install-recommends --no-install-suggests /opt/utpl/deb/libcpr1_1.10.5-1_amd64.deb

ENV TSDUCK_DEB=tsduck_3.37-3670.ubuntu23_amd64.deb
COPY docker/debs/$TSDUCK_DEB /tmp/
RUN apt -y install --no-install-recommends --no-install-suggests /tmp/$TSDUCK_DEB && rm -rf /tmp/$TSDUCK_DEB

# MXL: libraries and tools on the default paths, plugins in the GStreamer plugin directory, so
# neither LD_LIBRARY_PATH nor GST_PLUGIN_PATH is needed. The symlink is the library path that
# gst-mxl-rs recorded at build time (see the mxl-builder stage).
COPY --from=mxl-builder [ "/opt/mxl-install", "/usr/local" ]
COPY --from=mxl-builder [ "/opt/mxl-plugins", "/opt/mxl-plugins" ]
RUN \
    if ls /opt/mxl-plugins/*.so > /dev/null 2>&1; then \
        cp /opt/mxl-plugins/*.so "$(pkg-config --variable=pluginsdir gstreamer-1.0 2>/dev/null || echo /usr/lib/x86_64-linux-gnu/gstreamer-1.0)/" && \
        mkdir -p /opt/mxl-src/build/Linux-Clang-Release/lib && \
        ln -s "$(ls /usr/local/lib/libmxl.so* | head -n1)" /opt/mxl-src/build/Linux-Clang-Release/lib/libmxl.so; \
    fi && \
    rm -rf /opt/mxl-plugins && \
    ldconfig
