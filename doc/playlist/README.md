# Playlist Input: Configuration and REST Control

## Declarative channel file

For a managed channel, the primary operator entry point is `config/channel.yml`, mounted at
`PLAYLIST_BOOTSTRAP_FILE`. It declares the ordered clips and their graphics in one flat document
and is reconciled into SQLite on every container start, so a channel can be edited, version
controlled, and redeployed rather than reproduced by replaying operator clicks. The file is
bidirectional: hand edits win at startup, and Publish in the operator UI rewrites the file, so a
applied change survives a restart. Unapplied edits in a browser tab do not. Set
`PLAYLIST_BOOTSTRAP_WRITEBACK=false` to keep the file strictly one-way.

Because Publish rewrites the whole file, comments and terse shorthand in it are replaced by the
generated, fully explicit form the first time that happens. The directory, not the file, must be
bind-mounted read-write: a single-file mount is bound to one inode and the atomic rename would fail.

The engine still boots from the materialized M3U8; the file is lowered into a rundown version and
published into the managed queue, which the existing reconciler applies. See
[the transactional rundown service](../codebase/30-transactional-rundown-service.md) for the schema,
blast radius, and failure policy.

## Optional YAML rundown authoring

The normal playlist remains an M3U8 file (`pls+file://`) or the existing managed SQLite queue.
An optional `ENABLE_RUNDOWN_SERVICE=true` layer can import and publish a versioned YAML rundown
that maps clip items to graphics cues. It does not replace the engine M3U8 parser: publication
projects a validated version into the managed queue, which the existing reconciler applies.

See [the transactional rundown service](../codebase/30-transactional-rundown-service.md) for the
`/operator` timeline, the apply API, and the required pre-enable live-output
acceptance gates. The timeline is present while disabled but clearly reports that scheduling cannot
be activated until the feature flag is enabled and the service is restarted.

The transcoder uses two different documents: JSON configures the process, while an M3U8 file contains the playlist items. Production code does not implement a JSON playlist-file format. A playlist input is selected by a `pls+file://` URI in `transcoder.inputs[0]`.

## M3U8 Playlist Format

The application supports extended M3U8 playlists with per-item transport attributes. The playlist must start with `#EXTM3U`. The outer playlist document is currently loaded only from a local file; its items may use the schemes described below.

### Basic format

```m3u8
#EXTM3U
#EXTINF:<duration>,<attributes>
<URI>
```

The current parser uses `#EXTINF` to recognize an item and does not use its duration to schedule playback. Use `-1` for an indefinite source.

### Playlist processing

The implementation advances through playlist items in order and uses modulo indexing to return to the first item after the last. Automatic transition/loop continuity is not yet covered by a runtime check and must still be validated with timestamps and decoded output.

### Supported item protocols

The item URI is passed into the normal single-input path. The playlist parser currently recognizes transport attributes for:

1. **HTTP/HLS-oriented input** (`https://` or `http://`)

   - `scte35Source=manifest` or `segments`
   - `user="..."` and `password="..."`
   - `bandwidth=<bits-per-second>`

2. **SRT** (`srt://`)

   - `mode=caller` or `listener`
   - `latency=<milliseconds>`
   - `bufferSize=<bytes>`

3. **UDP** (`udp://`)

4. **File** (`file:///absolute/path`)

Only local file items are runtime-verified. SRT, UDP, and HTTP/HLS playlist items remain source-backed but require peers and focused runtime tests. The source contains a TODO to filter unsupported item protocols, so successful M3U8 parsing alone is not proof that an arbitrary scheme can play.

### Example M3U8 playlist

```m3u8
#EXTM3U
#EXTINF:-1,scte35Source=manifest,user="test",password="secret",bandwidth=1000000
https://example.com/stream.m3u8
#EXTINF:-1
file:///path/to/video.ts
#EXTINF:-1
https://example.com/stream.m3u8
```

## Transcoder JSON configuration

Below is the input shape for a playlist. Passthrough audio/video codecs are rejected with playlist input; every requested output stream must use an actual transcode codec and installed encoder.

You can define transport parameters globally in JSON. Item parameters parsed from `#EXTINF` are merged when that item is created; non-default item values override the corresponding global values. Notice that the production JSON parser uses `bandwith` (misspelled), nested `credentials`, and nested `scte.source`; the M3U8 attribute names are different.

Example configuration with global attributes:

```json
{
    "version": "1.4.2",
    "application": {
        "logger": {
            "transcoder": {
                "level": "error",
                "console": {
                    "enable": true
                }
            }
        }
    },
    "transcoder": {
        "inputs": [
            {
                "transport": {
                    "uri": "pls+file:///path/to/playlist.m3u8",
                    "playlist": {},
                    "hls": {
                        "bandwith": 1000000,
                        "credentials": {
                            "username": "default_user",
                            "password": "default_password"
                        },
                        "scte": {
                            "source": "manifest"
                        }
                    }
                }
            }
        ],
        "outputs": [
            {
                "name": "main",
                "type": "single",
                "transports": [
                    {
                        "uri": "udp://127.0.0.1:4444"
                    }
                ],
                "container": "mpegts",
                "streams": [
                    {
                        "type": "video",
                        "codec": "h264",
                        "video": {
                            "width": 1920,
                            "height": 1080,
                            "rate": 25
                        },
                        "h264": {
                            "encoder": "x264enc",
                            "properties": {
                                "bitrate": "4000",
                                "key-int-max": "50"
                            },
                            "caps": {
                                "profile": "high"
                            }
                        }
                    }
                ]
            }
        ]
    }
}
```

`x264enc` is an example factory and was present in the verification environment. Inspect the deployed GStreamer registry before using it; no universal default encoder is guaranteed.

## Usage notes

1. Start the M3U8 with `#EXTM3U` and pair every playable URI with `#EXTINF`.
2. For HTTP authentication, provide both username and password.
3. Use absolute `file:///` paths that are visible inside the transcoder container/process namespace.
4. Put the playlist input at `transcoder.inputs[0]`; additional input objects are ignored by the engine.
5. Validate actual output with `ffprobe`/decoded frames, not only process state or HTTP 200.

## Local Docker Compose playlist

This checkout includes [`docker-compose.playlist.yml`](../../docker-compose.playlist.yml). It mounts the migrated clips at `/media/queue`, the matching static playlist at `/media/queue/playlist.m3u8`, and the JSON configuration at `/opt/utpl/etc/config.json`.

```bash
docker compose -f docker-compose.playlist.yml up -d --build
docker compose -f docker-compose.playlist.yml logs -f transcoder
```

Access the same-origin operator page at `http://127.0.0.1:8080/operator` and the REST reference at `http://127.0.0.1:8080/docs`. The compose file binds the API to loopback only because this API has no authentication.

The transcoder writes MPEG-TS to its own `udp://127.0.0.1:5555`; an FFmpeg copy relay in the Compose bootstrap forwards that stream to host port 5555. Open `udp://@:5555` on the Docker host in a compatible player such as VLC. For another receiver, replace the final FFmpeg destination in `docker-compose.playlist.yml` with `udp://<receiver-ip>:5555`, then recreate the service:

```bash
docker compose -f docker-compose.playlist.yml up -d
```

The supplied managed profile seeds from `media/queue/playlist.m3u8`, then materializes the durable
queue at `/runtime/main.m3u8` before the transcoder starts. The current persistent queue contains
Air Asia and Coke; One Cent Call is registered for insertion. Use `/operator` to insert, reorder,
remove or skip clips while on air. A mutation is live only when its `appliedRevision` reaches
`desiredRevision`; `applyError` explains rejected application. The on-air item and the final
remaining item cannot be removed.

The unified page uses the old compact `here`, `▲`, `▼`, `remove`, and `Skip ▸` interaction model.
Select a media row, then click `here` on the queue row after which it should be inserted. Controls
remain disabled while a revision is applying. Skip changes `skipSequence` but intentionally leaves
the queue revision and persistent order unchanged. Managed Start/Pause are unavailable because this
profile is a continuous loop.

The current Compose profile normalizes every source to H.264 1920×1080/25 and AAC 48 kHz stereo.
A 52-second decoded capture showed Air Asia, Coke, an inserted One Cent Call item, and the following
loop. VLC 3.0.20 rendered the engine's multi-slice zero-latency H.264 with vertical corruption. The
Compose host relay now decodes it and emits single-slice H.264 to ports 5555/5556 while copying AAC
and the SCTE/data PID. A final 20-second capture contained all three streams with monotonic audio
and video timestamps. Open `udp://@:5555`.

## API Control

The playlist can be controlled through the REST API. The system exposes endpoints that allow you to monitor the playlist state and control stream switching.

### Counting inputs and monitoring playlist state

Use these engine-backed endpoints:

- `GET /inputs` - Lists logical inputs instantiated by the engine
- `GET /inputs/{id}` - Gets detailed information about a specific input, including:
  - List of all streams in the playlist
  - Current active stream
  - State of each stream (UNKNOWN, CREATED, BUFFERING, BUFFERED)

Count logical engine inputs and the first input's selectable items separately:

```bash
curl -sS http://transcoder:8080/inputs | jq 'length'
curl -sS http://transcoder:8080/inputs | jq '.[0].streams | length'
```

Do not use `GET /config/get-inputs | length` as the engine count. That route reads raw JSON and can include additional objects that the C++ parser ignores.

Example input response:

```json
{
    "id": "550e8400-e29b-41d4-a716-446655440000",
    "streams": [
        {
            "id": "550e8400-e29b-41d4-a716-446655440001",
            "uri": "file:///path/to/video1.ts",
            "state": "BUFFERED"
        },
        {
            "id": "550e8400-e29b-41d4-a716-446655440002",
            "uri": "srt://example.com:1234",
            "state": "BUFFERING"
        }
    ],
    "currentStreamId": "550e8400-e29b-41d4-a716-446655440001"
}
```

### Stream control

You can switch between streams in the playlist using:

- `POST /inputs/{id}` - Updates the current stream by providing the target stream ID
  ```json
  {
      "currentStreamId": "550e8400-e29b-41d4-a716-446655440002"
  }
  ```

The playlist switch is asynchronous. HTTP 200 means the target was accepted; the returned object may still contain the old `currentStreamId`. Poll `GET /inputs/{id}` until it equals the target, and impose a timeout. Unknown target UUIDs return 404; a missing `currentStreamId` returns 400.

## Adding a playlist

There is no dedicated add-playlist REST endpoint in this revision. `POST /config/set-input/{index}` is also not usable: both a replace request at index 0 and an append attempt at index 2 returned HTTP 200 with `{"message":"error parsing request"}`. The replace-all `/config/set-inputs` route declares a dictionary and rejects the intended array.

Deploy the JSON and M3U8 through a controlled configuration workflow, then start/restart the process. Confirm the items through `GET /inputs` and inspect output media. Adding `transcoder.inputs[1]` does not create a second logical input.

## Error handling

Playlist setup can fail when the outer file cannot be opened, M3U8 parsing fails, the first tag is not `#EXTM3U`, an item URI cannot be parsed, or the playlist contains no valid playback items. Some malformed/unsupported entries may be skipped because protocol filtering is incomplete; verify the resulting `streams` array rather than assuming every textual item was accepted.

The current-source build, exact fixture, REST calls, errors, and decoded output evidence are documented in [`doc/codebase/28-playlist-input-api-runtime-verification.md`](../codebase/28-playlist-input-api-runtime-verification.md).
The control-plane contract for these calls is documented in [`doc/codebase/17-rest-grpc-and-sqlite-control-plane.md`](../codebase/17-rest-grpc-and-sqlite-control-plane.md).
