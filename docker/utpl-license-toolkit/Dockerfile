ARG BaseImageTag=latest

FROM utpl/devel:${BaseImageTag} as transcoder-builder

# Copy sources
COPY transcoder  /tmp/src/transcoder
COPY version.txt /tmp/src/

# Build transcoder application
RUN \
    cd /tmp/src/transcoder && \
    mkdir -p build && \
    cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j && \
    DESTDIR=/tmp/license_tools make install-license-tools

FROM utpl/base:${BaseImageTag} as license-toolkit

RUN \
    DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt install -y --no-install-recommends --no-install-suggests \
        libspdlog1.12 \
        libboost-program-options1.83.0 \
        libboost-url1.83.0 \
        libboost-filesystem1.83.0 \
        xxd


COPY --from=transcoder-builder [ "/tmp/license_tools", "/tmp/license_tools"]

RUN \
    mkdir -p /opt/utpl/etc && \
    cp -avr /tmp/license_tools/* / && \
    rm -rf /tmp/license_tools

ENV LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/opt/utpl/lib"
ENV PATH="$PATH:/opt/utpl/bin"

CMD ["/bin/bash"]
