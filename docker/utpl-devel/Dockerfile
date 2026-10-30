ARG BaseImageTag=latest
FROM utpl/base:${BaseImageTag}

# build dependencies and tools (cargo -> rust)
RUN DEBIAN_FRONTEND=noninteractive apt-get update
RUN \
    DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt install -y --no-install-recommends --no-install-suggests \
    g++ \
    cmake \
    make \
    pkg-config \
    git \
    cargo-c \
    \
    less \
    tree \
    \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-good1.0-dev \
    libavformat-dev \
    \
    libsrt-openssl-dev \
    librist-dev \
    libedit-dev \
    libusb-1.0-0-dev \
    libpcsclite-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    ca-certificates \
    cargo \
    \
    libspdlog-dev \
    nlohmann-json3-dev \
    libboost-url-dev \
    libboost-program-options-dev \
    libboost-filesystem-dev \
    libavdevice-dev \
    libsqlitecpp-dev \
    libgtest-dev \
    \
    libprotobuf-dev \
    libgrpc++-dev \
    protobuf-compiler-grpc \
    python3-dev \
    \
    libcurl4t64 \
    ffmpeg \
    curl \
    jq \
    gstreamer1.0-tools \
    sqlite3 \
    python3-poetry \
    libpugixml-dev \
    gdb \
    gdbserver \
    gstreamer1.0-plugins-base-apps \
    gstreamer1.0-x

RUN \
    apt -y install --no-install-recommends --no-install-suggests $(find /opt/utpl/deb/ -name gir1.2-gst-plugins-bad-1.0*.deb) && \
    apt -y install --no-install-recommends --no-install-suggests $(find /opt/utpl/deb/ -name libgstreamer-opencv1.0*.deb) && \
    apt -y install --no-install-recommends --no-install-suggests $(find /opt/utpl/deb/ -name libgstreamer-plugins-bad1.0-dev*.deb) && \
    apt -y install --no-install-recommends --no-install-suggests /opt/utpl/deb/libcpr-dev_1.10.5-1_amd64.deb

ENV TSDUCK_DEB=tsduck-dev_3.37-3670.ubuntu23_amd64.deb
COPY docker/debs/$TSDUCK_DEB /tmp/
RUN dpkg -i /tmp/$TSDUCK_DEB && rm -rf /tmp/$TSDUCK_DEB