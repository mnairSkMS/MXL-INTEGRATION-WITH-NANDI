# Playlist Input API Runtime Verification

This record covers the current-source verification performed on 2026-08-03. It answers three operational questions independently: how to count effective inputs and switch targets, how to switch a playlist item, and how to add/configure a playlist input.

## Environment and build result

The production multi-stage Dockerfile was built from this checkout:

```bash
docker build \
  -f docker/utpl-transcoder/Dockerfile \
  -t utpl/transcoder:playlist-api-verify-20260803 \
  .
```

| Check | Result |
| --- | --- |
| Runtime image ID | `sha256:f3eabecaef5e8d35957debfc7ec02fdca3702de1223083ab9688b085124cb6ed` |
| Builder image ID retained for CTest | `sha256:f961cbee35b359bfd6ac456a670662ee87d978270b45f866dbd15100096dfe69` |
| Product version | `1.1.0-beta.2-skandha.3` |
| Toolchain | GCC 13.3.0, CMake 3.28.3, GStreamer 1.24.2 |
| C++ Release tests | 4/4 CTest tests passed |
| Python API tests | Code generation completed; 11/11 tests passed |
| Relevant development factories | `x264enc` and `fallbackswitch` present |
| Final-image diagnostic CLIs | `gst-inspect-1.0` and `ffprobe` not packaged |

## Deterministic media fixture

Two 120-second H.264/MPEG-TS files were generated at 320×180 and 10 fps. Item A used FFmpeg `testsrc`; item B used `smptebars`. Their visibly different frames make a switch observable without trusting only API state.

```m3u8
#EXTM3U
#EXTINF:-1,
file:///verify/input-a.ts
#EXTINF:-1,
file:///verify/input-b.ts
```

The effective JSON configuration was:

```json
{
  "version": "1.4.2",
  "application": {
    "logger": {
      "transcoder": {
        "level": "log",
        "console": { "enable": true, "colors": false, "stderr": true }
      },
      "gstreamer": {
        "level": "warning",
        "console": { "enable": true, "colors": false, "stderr": true }
      }
    }
  },
  "transcoder": {
    "inputs": [
      {
        "transport": {
          "uri": "pls+file:///verify/playlist.m3u8",
          "playlist": {}
        }
      },
      {
        "transport": {
          "uri": "file:///verify/ignored-extra-input.ts",
          "file": {}
        }
      }
    ],
    "outputs": [
      {
        "name": "verification-output",
        "type": "single",
        "transports": [
          {
            "uri": "udp://127.0.0.1:5555",
            "udp": { "unicast": { "ttl": 64 } }
          }
        ],
        "container": "mpegts",
        "streams": [
          {
            "type": "video",
            "codec": "h264",
            "video": {
              "width": 320,
              "height": 180,
              "rate": 10,
              "format": "I420"
            },
            "h264": {
              "encoder": "x264enc",
              "properties": {
                "bitrate": "500",
                "key-int-max": "20",
                "speed-preset": "ultrafast",
                "tune": "zerolatency"
              },
              "caps": { "profile": "baseline" }
            }
          }
        ]
      }
    ]
  }
}
```

The deliberately supplied second input object tested whether the engine constructs multiple independent inputs. It does not.

## 1. Count configured/effective inputs

### Engine inputs and switch targets

```bash
curl -sS http://127.0.0.1:18080/inputs | jq 'length'
curl -sS http://127.0.0.1:18080/inputs | jq '.[0].streams | length'
```

Observed:

```text
engine logical inputs: 1
switchable playlist streams: 2
```

`GET /inputs` returned one input UUID, two stream UUIDs/URIs, and the first stream UUID as `currentStreamId`.

### Raw JSON configuration objects

```bash
curl -sS http://127.0.0.1:18080/config/get-inputs | jq 'length'
```

Observed: `2`. This route reads the file; it does not report what the C++ engine instantiated. Production JSON parsing consumes only `transcoder.inputs[0]`, so the authoritative runtime count is `GET /inputs | length`.

## 2. Switch playlist items

Select IDs from the engine response, then POST the target stream:

```bash
API=http://127.0.0.1:18080
INPUT_ID=$(curl -sS "$API/inputs" | jq -r '.[0].id')
TARGET_ID=$(curl -sS "$API/inputs" | jq -r '.[0].streams[1].id')

curl -sS -X POST "$API/inputs/$INPUT_ID" \
  -H 'Content-Type: application/json' \
  --data "{\"currentStreamId\":\"$TARGET_ID\"}"

curl -sS "$API/inputs/$INPUT_ID" | jq -r '.currentStreamId'
```

Observed behavior:

- POST returned HTTP 200.
- The POST response still contained the old current stream; switching is asynchronous.
- The first follow-up GET contained the requested target UUID.
- Repeating the operation for item A switched back successfully.
- A well-formed but unknown target UUID returned HTTP 404 with code `INVALID_REQUEST`.
- An empty JSON object returned HTTP 400 with `Stream ID to be set cannot be null`.

Clients must poll `GET /inputs/{id}` until the requested stream becomes current. UUIDs are runtime-generated and must be discovered, not stored in static configuration.

## Media verification of the switch

Because UDP was bound to the transcoder container loopback, capture used a sidecar sharing its network namespace:

```bash
docker run --rm \
  --network container:skandha-playlist-api-verify \
  -v "$VERIFY_DIR:/verify" \
  utpl/devel:latest \
  ffmpeg -hide_banner -loglevel error -y \
    -i 'udp://127.0.0.1:5555?fifo_size=1000000&overrun_nonfatal=1' \
    -t 4 -map 0:v:0 -c copy /verify/capture.ts

ffprobe -v error -select_streams v:0 \
  -show_entries stream=codec_name,width,height,avg_frame_rate,duration \
  -of json "$VERIFY_DIR/capture.ts"
```

Captures before and after the switch both probed as H.264, 320×180, 10 fps MPEG-TS. Decoded frame A showed the `testsrc` pattern; decoded frame B showed SMPTE bars, and their PNG SHA-256 values differed. This proves the media source changed, not merely the API field.

The receiver joined an already-running MPEG-TS/H.264 stream mid-GOP. FFmpeg logged missing PPS/decode-slice messages before reaching decodable parameter sets, though the later frames decoded. Seamless receiver join and switch continuity are therefore not claimed.

## 3. Add/configure a playlist input

There is no dedicated REST endpoint to add a playlist. The current verified path is configuration before process startup:

1. Write the M3U8 file.
2. Put the `pls+file://...` URI and `"playlist": {}` marker in `transcoder.inputs[0].transport`.
3. Use explicit transcode codecs/encoders; do not use audio/video passthrough.
4. Mount/deploy the config and playlist where the process can read them.
5. Start or restart the transcoder.
6. Verify the effective input/stream counts and decoded output.

The intended configuration routes were tested with no-op payloads:

| Request | Observed response |
| --- | --- |
| `POST /config/set-input/0` with the unchanged object | HTTP 200, `{"message":"error parsing request"}` |
| `POST /config/set-input/2` as an append attempt | HTTP 200, `{"message":"error parsing request"}` |
| `POST /config/set-inputs` with the unchanged array | HTTP 400, request expected a dictionary |

The configuration SHA-256 remained `860331b4b859d18e25578179f34853857eed49d7fbee9ecd11d9a7d332437049` before and after. These routes cannot currently add a playlist and must not be treated as a deployment API.

## Reassessed conclusions

| Question | Verified answer |
| --- | --- |
| How many engine inputs are configured? | `GET /inputs | length`; current production behavior is one because only `inputs[0]` is parsed. |
| How many switch choices exist? | Count `streams` inside the returned logical input; the two-item M3U8 returned two. |
| How is an item switched? | `POST /inputs/{inputUuid}` with `currentStreamId`, then poll GET for asynchronous completion. |
| How is a playlist added? | Put an M3U8 behind a `pls+` URI in `inputs[0]` before startup/restart. No working add API exists. |
| Was media output checked? | Yes: UDP MPEG-TS was captured, probed, decoded, and visually distinguished before/after switching. |
| What remains unverified? | Automatic looping, long-run timestamp continuity, audio switching/A-V sync, and SRT/UDP/HLS playlist items. |
