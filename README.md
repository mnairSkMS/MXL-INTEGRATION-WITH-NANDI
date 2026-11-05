# UTPL - Universal Transcoding Pipeline

A transcoding channel: one or more inputs, a processing graph, and one or more outputs, driven by a
JSON configuration file and controlled at runtime over REST/gRPC.

**Version:** see [`version.txt`](version.txt)

**This checkout tests integrating MXL (Media eXchange Layer) and MXL Fabrics with Nandi** (this
engine). MXL is built into the images, and the engine can publish an input's decoded source and the finished
programme as MXL flows, and a channel can read MXL flows as its input (all built and tested); an external encoder and Fabrics are not yet. The plan, source-verified integration points, test
plan and open decisions are in [41 MXL integration](doc/codebase/41-mxl-integration.md); the planning
brief and review deck are in [`doc/mxl/`](doc/mxl/).

## Setting it up

### 1. Base images

The build images are not on a registry; build them once from this checkout.

```bash
cd docker && ./build-docker.sh
```

That produces `utpl/base` (patched GStreamer, TSDuck, runtime libraries), `utpl/devel` (compilers,
headers, Poetry), `utpl/transcoder` (the product image), `utpl/transcoder-test` (adds media-analysis
tools and the shell test suite) and `utpl/license-toolkit`. See
[03](doc/codebase/03-build-development-and-runtime.md) for what each one contains.

### 2. Run a channel

`docker-compose.yml` is the default, parameterised deployment: one container running any channel file in
`config/`, chosen with `CHANNEL` (copy `.env.example` to `.env` to set it and the ports):

```bash
docker compose up -d                                  # config/rtmp_to_hls.json
CHANNEL=srt_to_hls_srt.json docker compose up -d      # any other file in config/
docker compose --profile hls-lan up -d                # also serve the HLS output to the LAN on :8081
```

It needs an image whose `supervisord.conf` reads `TRANSCODER_LICENSE_PATH`, so build it from this
checkout (`docker compose build`) rather than reusing an older `skandha-transcoder:local`. The default
channel pulls `rtmp://127.0.0.1:1935/live/CHANGE_ME`; replace the placeholder key in the JSON (the
built-in relay rejects keys under 16 characters) and publish to `rtmp://<host>:1935/live/<key>`.
Playlist channels also set `PLAYLIST_BOOTSTRAP_FILE`; see `.env.example`.

The ad-insertion channel below runs from `docker-compose.ad-insertion.yml`. `docker-compose.playlist.yml`
is the managed-playlist operator deployment (licence API, slates, HLS LAN serving); the other
`docker-compose.*.yml` files are single-purpose variants.

```bash
docker compose -f docker-compose.ad-insertion.yml up -d --build
```

This is the runtime: Supervisor starts the database preload, then the REST API, then the transcoder.
Nothing else needs to run alongside it.

| | where |
| --- | --- |
| Management API and operator UI | `http://127.0.0.1:8080` |
| Channel preview, two identical copies | `udp://@:5555` and `udp://@:5556` |

**Open `udp://@:5555`, not `udp://@127.0.0.1:5555`.** The container is on a bridge network, so it
sends the preview to the Docker gateway address (`172.17.0.1` on a default install), not to
loopback. A player bound to `127.0.0.1` receives nothing at all and looks exactly like a dead
channel. The bare `@` binds every interface, which is the only form that always works.

Both host ports carry the same thing: a re-encoded single-slice preview. The channel's own output
never leaves the container - it is encoded for latency, in multi-slice zero-latency H.264 that VLC
3.0.x visibly corrupts, so the Compose file decodes and re-encodes a preview for the host and keeps
the engine's own graph untouched. That costs about a second of latency on what you watch, and
nothing on what the channel actually emits.

If your player errors with GPU decoding on, turn hardware decoding off (`vlc --avcodec-hw=none`, or
`ffplay -vcodec h264`): a zero-latency stream with no B-frames is not what hardware decoders expect.

### 3. Build the binaries alone (development)

```bash
docker run --rm -v "$PWD:/src:ro" -v /var/tmp/utpl-build:/scratch utpl/devel:latest bash -lc \
  'cmake -S /src/transcoder -B /scratch/rwdi -DCMAKE_BUILD_TYPE=RelWithDebInfo && \
   cmake --build /scratch/rwdi -j4 && ctest --test-dir /scratch/rwdi --output-on-failure'
```

Keep parallelism at `-j4` on a 16 GB machine; an unbounded `-j` invites the OOM-killer. Use
`RelWithDebInfo`, not `Debug`: a Debug build links AddressSanitizer and embeds development license
keys that the runtime image does not carry.

Run it directly with:

```text
transcoder -c <config.json> -l <license> -b <events.sqlite>
```

## Configuring it

Everything is configuration; there is one runtime and one image regardless of which features are on.

| Decision | File |
| --- | --- |
| Channel topology: inputs, processing, outputs, encoders | `config/migrated-media-playlist.json`, mounted at `/opt/utpl/etc/config.json` |
| What the programme plays | `config/channel.yml` |
| What the ad break reel plays | `config/ads.yml` |
| Which channel files exist, and which input each drives | `PLAYLIST_BOOTSTRAP_FILES` in `docker-compose.ad-insertion.yml`, in input order |
| Whether ad insertion happens | `transcoder.adInsertion.enable` |
| Media available to the playlists | `media/queue/` |
| Graphics catalog | `api/src/transcoder_api/assets/graphics_catalog.json`, assets in `media/graphics/` |
| Feature gates, ports, paths | environment in the compose file you run |

A channel file is one managed playlist. Declaring one file gives an ordinary single-playlist channel;
declaring two, with a `secondary` input and `adInsertion.enable: true`, gives a channel that cuts to
its break reel on a cue and back on the next one. Nothing else differs between those two setups.

Both are authored the same way, validated the same way, and rewritten in full by every publish, so a
change made in the operator UI survives a restart and a hand edit survives a publish.

Cues reach the switcher from the operator UI, from anything writing the `events` table, or from the
input itself when the programme carries SCTE-35:

```bash
curl -X POST localhost:8080/scheduled_events -H 'Content-Type: application/json' -d '[
  {"type":"scte35_spliceinsert","time":"2026-08-18T07:23:03.000Z",
   "details":{"scte35_spliceinsert":{"splice_event_id":7001,"duration":20000}}},
  {"type":"scte35_returntonetwork","time":"2026-08-18T07:23:25.000Z",
   "details":{"scte35_returntonetwork":{"splice_event_id":7001}}}
]'
```

The switch lands about 4 s after the cue time: the section is built with a splice time that far
ahead. That 4 s is fixed - `scte.prerollTime` appears in the schema but is not parsed, so the
runtime always uses its class default ([05](doc/codebase/05-configuration-reference.md)).

## Where to read further

| | |
| --- | --- |
| Every configuration key, source-backed, with known schema differences | [05 configuration reference](doc/codebase/05-configuration-reference.md) |
| Build graph, images, runtime layout, build-type traps | [03 build and runtime](doc/codebase/03-build-development-and-runtime.md) |
| Declarative channel files and the rundown service | [30 rundown service](doc/codebase/30-transactional-rundown-service.md) |
| Ad insertion: configuration, break lifecycle, open defects | [34 ad insertion](doc/codebase/34-scte-ad-insertion.md) |
| REST, gRPC and the SQLite control plane | [17 control plane](doc/codebase/17-rest-grpc-and-sqlite-control-plane.md), [API README](doc/api/README.md) |
| Operator guide for the playlist channel | [playlist guide](doc/playlist/README.md) |
| Everything else about the source tree | [codebase handbook](doc/codebase/README.md) |
| What is known to be broken | [27 known gaps](doc/codebase/27-known-gaps-contradictions-and-remediation.md) |
| MXL / MXL Fabrics integration: plan, integration points, tests, open decisions | [41 MXL integration](doc/codebase/41-mxl-integration.md), [`doc/mxl/`](doc/mxl/) |

## Repository layout

| Path | Contents |
| --- | --- |
| `transcoder/` | The engine: C++ library (`lib/utpl/`), application and JSON config parser (`app/`), in-tree GStreamer plugins (`plugins/`), upstream patches (`patches/`), licence tooling (`license/`) |
| `api/` | Python FastAPI control plane: REST routes, SQLite state, operator UI |
| `frontend/` | Vite/React channel configuration UI served at `/channel`; its build is committed under `api/src/transcoder_api/static/channel` |
| `docker/` | Dockerfiles and dependencies for development and production images |
| `docker-compose.yml`, `.env.example` | Default deployment: any `config/` channel file picked by `CHANNEL`; ports, image and licence via `.env` |
| `docker-compose.*.yml` | Specialised local deployments; they bind-mount `config/`, `media/`, `runtime/` and `licenses/` |
| `config/` | Channel configurations mounted into the engine |
| `licenses/` | Licence files used by the local deployments |
| `license-server/` | Standalone licence session service (separate deployable) |
| `tests/` | Product integration tests |
| `media/`, `runtime/` | Local media assets and live runtime state (not versioned) |
| `doc/` | Documentation, see below |

## Documentation

- [Codebase handbook](doc/codebase/README.md), with a
  [code-reading roadmap](doc/codebase/31-code-reading-roadmap.md)
- [Configuration reference](doc/configuration-reference.md): every JSON key
- [Graphics guide](doc/graphics-config-guide.md)
- [Create-channel API guide](doc/api-create-channel.md)
- [API guide](doc/api/)
- [Reference material](doc/reference/): SCTE-35 standard, API document export
- [MXL integration material](doc/mxl/): the Nandi + MXL + MXL Fabrics planning brief and review deck

For development against this tree, start with `AGENTS.md`.
