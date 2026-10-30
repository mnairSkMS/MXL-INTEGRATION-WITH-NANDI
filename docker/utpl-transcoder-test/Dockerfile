ARG BaseImageTag=latest

FROM utpl/transcoder:${BaseImageTag}

ENV ThreeFiveVersion=3.0.21

RUN DEBIAN_FRONTEND=noninteractive apt-get update && \
    DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt install -y --no-install-recommends --no-install-suggests \
    ffmpeg \
    jq \
    sqlite3 \
    imagemagick \
    wget \
    netcat-openbsd \
    tstools \
    bc \
    gstreamer1.0-tools \
    gstreamer1.0-x \
    srt-tools \
    net-tools \
    ncat

RUN /opt/venv/bin/pip install xmltodict "threefive3==$ThreeFiveVersion" librosa superkabuki
COPY tests /tests
COPY transcoder/license/prod/licenses/offline.lic /tmp/transcoder.lic
WORKDIR /tests

CMD ["bash", "run.sh"]
