# Strom (Eyevinn) with MXL: the upstream Strom image plus MXL v1.1.0 and the gst-mxl-rs plugin (mxlsrc / mxlsink).
#
# Step 1 of the OpenLive / Strom integration (project-status.md, doc/integration/). Strom itself is not changed here:
# STROM_IMAGE is the image built from the pinned upstream commit with Strom's own Dockerfile, e.g.
#   (cd ../strom && git checkout 0df2e4b && docker build -t strom:0df2e4b .)
# Build this from the repository root, because it applies the MXL patches in transcoder/patches/mxl:
#   docker build -f docker/strom-mxl/Dockerfile -t strom-mxl:0df2e4b-mxl1.1.0 .
#
# The MXL recipe is the one in docker/utpl-base/Dockerfile (mxl-builder stage): same tag, same patches, same Rust
# toolchain. The only difference is the base: Ubuntu 26.04, so the plugin is built against the GStreamer 1.28 that
# Strom's runtime image has (Nandi's images are Ubuntu 24.04 with GStreamer 1.24). Both sides use the same libmxl, so
# they share one domain. Fabrics (libfabric) is not built.

ARG STROM_IMAGE=strom:0df2e4b

FROM ubuntu:26.04 AS mxl-builder

ARG MXL_TAG=v1.1.0
# MXL needs Rust >= 1.87 (edition 2024); its own devcontainer uses 1.90.0.
ARG MXL_RUST_TOOLCHAIN=1.90.0
ARG MXL_JOBS=4

# The Rust bindings are generated with bindgen (libclang) and load libmxl.so at run time.
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get install -y --no-install-recommends --no-install-suggests \
        build-essential cmake ninja-build pkg-config nasm autoconf automake libtool bison flex \
        git curl ca-certificates zip unzip tar python3 \
        libclang-dev \
        libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev && \
    rm -rf /var/lib/apt/lists/*

# vcpkg supplies MXL's C++ dependencies. CMakePresets.json expects it in ~/vcpkg.
RUN git clone https://github.com/microsoft/vcpkg /root/vcpkg && \
    /root/vcpkg/bootstrap-vcpkg.sh -disableMetrics
ENV VCPKG_ROOT=/root/vcpkg

# The source stays at this fixed path: the Rust plugin, built with `mxl-not-built`, records
# <repo>/build/Linux-Clang-Release/lib/libmxl.so at compile time as the library to load. The runtime
# stage below recreates that one path as a symlink to the installed library.
RUN git clone --depth 1 --branch ${MXL_TAG} https://github.com/dmf-mxl/mxl.git /opt/mxl-src

RUN cd /opt/mxl-src && \
    cmake --preset Linux-GCC-Release -DBUILD_TESTS=OFF -DBUILD_DOCS=OFF -DCMAKE_INSTALL_PREFIX=/opt/mxl-install && \
    cmake --build build/Linux-GCC-Release -j${MXL_JOBS} && \
    cmake --install build/Linux-GCC-Release

RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain ${MXL_RUST_TOOLCHAIN} --no-modify-path

# Patches to the MXL Rust plugin. 010 (mxlsrc releases its locks before it pushes caps) is still needed:
# on 2026-11-26 it applied cleanly to v1.1.0 and to MXL main (doc/integration/00-readiness-check.md, risk 3).
COPY transcoder/patches/mxl /tmp/mxl-patches
RUN cd /opt/mxl-src && \
    for patchFile in $(find /tmp/mxl-patches -type f -name '*.patch' | sort); do \
        echo "Patching MXL with: $patchFile" && git apply --verbose "$patchFile" || exit 1; \
    done

RUN cd /opt/mxl-src/rust && \
    /root/.cargo/bin/cargo build --release --locked -j${MXL_JOBS} -p gst-mxl-rs --features mxl/mxl-not-built && \
    /root/.cargo/bin/cargo build --release --locked -j${MXL_JOBS} -p gst-avsynctest-rs && \
    mkdir -p /opt/mxl-plugins && \
    cp target/release/libgstmxl.so target/release/libgstavsynctest.so /opt/mxl-plugins/ && \
    dpkg-query -W -f='${Package} ${Version}\n' libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev > /opt/mxl-plugins/BUILT-AGAINST


FROM ${STROM_IMAGE}

ARG MXL_TAG=v1.1.0
LABEL org.opencontainers.image.title="strom-mxl" \
      mxl.tag="${MXL_TAG}" \
      mxl.patches="010-mxlsrc-release-locks-before-set-caps"

# MXL: libraries and tools on the default paths, plugins in the GStreamer plugin directory, so neither
# LD_LIBRARY_PATH nor GST_PLUGIN_PATH is needed. The symlink is the library path that gst-mxl-rs recorded
# at build time (see the builder stage).
COPY --from=mxl-builder /opt/mxl-install /usr/local
COPY --from=mxl-builder /opt/mxl-plugins /opt/mxl-plugins
RUN pluginsDir=/usr/lib/x86_64-linux-gnu/gstreamer-1.0 && \
    cp /opt/mxl-plugins/*.so "$pluginsDir/" && \
    mkdir -p /opt/mxl-src/build/Linux-Clang-Release/lib && \
    ln -s "$(ls /usr/local/lib/libmxl.so* | head -n1)" /opt/mxl-src/build/Linux-Clang-Release/lib/libmxl.so && \
    ldconfig && \
    echo "gst-mxl-rs built against:" && cat /opt/mxl-plugins/BUILT-AGAINST && \
    echo "runtime:" && dpkg-query -W -f='${Package} ${Version}\n' libgstreamer1.0-0 libgstreamer-plugins-base1.0-0 && \
    gst-inspect-1.0 mxlsrc > /dev/null && gst-inspect-1.0 mxlsink > /dev/null && \
    mkdir -p /run/mxl

# The MXL domain: mount the same tmpfs volume as the Nandi containers at /run/mxl (see docker-compose.yml).
