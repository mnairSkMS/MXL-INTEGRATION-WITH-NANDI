ARG BaseImageTag=latest

FROM utpl/devel:${BaseImageTag} as transcoder-builder

# Compiler processes to run in parallel. Deliberately not "unlimited" - see below.
ARG BuildJobs=4

# Copy sources
COPY transcoder  /tmp/src/transcoder
COPY version.txt /tmp/src/

# Build transcoder application.
#
# Parallelism is bounded on purpose. A bare "make -j" is unlimited, and this tree needs
# roughly 1GB per compiler process (GStreamer + TSDuck + Boost headers in every TU). On a
# host with more cores than gigabytes that overruns RAM, and the OOM killer takes down the
# desktop rather than just the build. Raise it with --build-arg BuildJobs=N.
RUN \
    cd /tmp/src/transcoder && \
    mkdir -p build && \
    cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j"${BuildJobs}" && \
    DESTDIR=/tmp/transcoder_dist make install


# The channel-configuration SPA served at /channel.
#
# A built copy is committed under api/src/transcoder_api/static/channel so that the tree works
# without Node - the dev bind mount in docker-compose.playlist.yml serves it directly. This
# stage rebuilds it from source anyway, so an image can never ship assets older than the
# frontend/src it was built from. The api-builder below discards the committed copy before
# taking this one.
FROM node:20-alpine as frontend-builder

WORKDIR /tmp/frontend
COPY ./frontend/package.json ./frontend/package-lock.json ./
RUN npm ci --no-audit --no-fund

COPY ./frontend ./
# tsc and vite are run separately rather than through `npm run build` so that outDir can be
# redirected here: the configured one points into ../api, which does not exist in this stage.
# --emptyOutDir is required because that path is outside the Vite root.
RUN npx tsc --noEmit && \
    npx vitest run && \
    npx vite build --outDir /tmp/frontend-dist --emptyOutDir


FROM utpl/devel:${BaseImageTag} as api-builder

COPY ./api/poetry.lock ./api/pyproject.toml /tmp/api/
WORKDIR /tmp/api
RUN poetry lock --no-update && \
    poetry install --no-root --no-interaction --no-ansi && \
    mkdir -p /tmp/trancoder/app/

COPY ./api /tmp/api/
COPY ./transcoder/app/grpc /tmp/transcoder/app/grpc

# Replace, not merge. Vite names assets by content hash, so copying a new build over an old
# directory leaves every superseded bundle behind in the image.
RUN rm -rf /tmp/api/src/transcoder_api/static/channel
COPY --from=frontend-builder /tmp/frontend-dist /tmp/api/src/transcoder_api/static/channel

RUN poetry run poe codegen && \
    poetry lock --no-update && \
    poetry install && \
    poetry run pytest && \
    poetry build


# RTMP ingest relay. Pinned by version and checksum (the checksum is upstream's published
# checksums.sha256 for this release). The image is x86_64-only, like the NDI SDK it carries.
FROM alpine:3.20 as rtmp-relay-fetcher

ARG MediaMtxVersion=v1.21.1
ARG MediaMtxSha256=653abc672a3e693f8d3b2717752492fdcfb8072291ec108d03d3dd857411b0ee

RUN wget -q -O /tmp/mediamtx.tar.gz \
        "https://github.com/bluenviron/mediamtx/releases/download/${MediaMtxVersion}/mediamtx_${MediaMtxVersion}_linux_amd64.tar.gz" && \
    echo "${MediaMtxSha256}  /tmp/mediamtx.tar.gz" | sha256sum -c - && \
    tar -xzf /tmp/mediamtx.tar.gz -C /tmp mediamtx


FROM utpl/base:${BaseImageTag} as transcoder

RUN \
    DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt install -y --no-install-recommends --no-install-suggests \
        libspdlog1.12 \
        libboost-program-options1.83.0 \
        libboost-url1.83.0 \
        libboost-filesystem1.83.0 \
        va-driver-all \
        ffmpeg \
        supervisor

COPY docker/utpl-transcoder/supervisord.conf /etc/supervisor/conf.d/supervisord.conf

COPY --from=rtmp-relay-fetcher /tmp/mediamtx /opt/utpl/bin/mediamtx
COPY docker/utpl-transcoder/mediamtx.yml /opt/utpl/etc/mediamtx.yml

COPY --from=transcoder-builder [ "/tmp/transcoder_dist", "/tmp/transcoder_dist"]


RUN \
    mkdir -p /opt/utpl/etc && \
    cp -avr /tmp/transcoder_dist/* / && \
    rm -rf /tmp/transcoder_dist

COPY --from=api-builder [ "/tmp/api/dist", "/tmp/api/dist"]
RUN python3 -m venv /opt/venv
RUN /opt/venv/bin/pip install /tmp/api/dist/transcoder_api-*-py3-none-any.whl

ENV NDI_SDK_DIR=/opt/ndi
ENV LD_LIBRARY_PATH="/opt/ndi/lib/x86_64-linux-gnu:/opt/utpl/lib"
ENV PATH="$PATH:/opt/utpl/bin"
ENV API_PORT=8080
# Where the engine reads its config and PUT /config writes it. Overridden by compose
# to a path inside a bind-mounted directory; the default keeps the documented
# single-file `docker run -v host.json:/opt/utpl/etc/config.json` deployment working.
ENV TRANSCODER_CONFIG_PATH=/opt/utpl/etc/config.json
# Where the engine reads its license and PUT/DELETE /license replace or remove it. Same story
# as the config: compose points it inside a bind-mounted directory, and the default keeps the
# single-file `-v host.lic:/opt/utpl/etc/transcoder.lic` deployment starting (read-only there,
# so the license routes report 409 rather than writing).
ENV TRANSCODER_LICENSE_PATH=/opt/utpl/etc/transcoder.lic
ENV DB_PATH=/opt/utpl/events.sqlite
ENV INIT_DATA=/opt/utpl/events.json

CMD ["/usr/bin/supervisord"]
