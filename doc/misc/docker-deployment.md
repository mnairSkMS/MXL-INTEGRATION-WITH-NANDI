# Running and Configuring the Transcoder Container

## Required files

The default entrypoint of the `utpl/transcoder` image requires two files that are **not** included in
the image:

- `/opt/utpl/etc/config.json` — transcoder configuration file
- `/opt/utpl/etc/transcoder.lic` — license file

The operator must supply them, for example through a bind mount:

```sh
docker run -it --rm \
    -v (...)/config.json:/opt/utpl/etc/config.json \
    -v (...)/transcoder.lic:/opt/utpl/etc/transcoder.lic \
    ghcr.io/skandha-media-services/utpl/transcoder
```

For a complete runnable operator setup with media, playlist, and SQLite state mounted, see
`docker-compose.playlist.yml` in the repository root and the
[playlist guide](../playlist/README.md).

## Environment variables

| Variable | Purpose | Default |
| --- | --- | --- |
| `API_PORT` | TCP port serving the REST API and Swagger UI. | `8080` |
| `DB_PATH` | Path to the transcoder SQLite database file. | `/opt/utpl/events.sqlite` |
| `INIT_DATA` | Path to a JSON file of predefined API events inserted into the database at container start. | `/opt/utpl/events.json` |
| `PLAYLIST_BOOTSTRAP_FILE` | Declarative channel file reconciled into the database on every container start, and rewritten by every Publish. Unset leaves the feature inert. Requires `ENABLE_RUNDOWN_SERVICE`. Mount its **directory** read-write (`./config:/opt/utpl/channel`): a single-file bind mount is inode-bound and the atomic rewrite fails on it with `EBUSY`. | unset |
| `PLAYLIST_BOOTSTRAP_WRITEBACK` | `false` makes the channel file one-way again: Publish goes on air without persisting, and the next start reverts it. | `true` |
| `PLAYLIST_BOOTSTRAP_STRICT` | `true` exits `preload_db` non-zero when that file is rejected. The engine and API still start on the previously saved playlist either way. | `false` |

## Preloading the database with events

The container can preload the SQLite database with predefined events. The source file must be JSON
and follow the structure of the `Event` API model; the `id` field is not required.

```json
[
    {
        "type": "squeeze",
        "time": "2024-10-24T05:22:40.513Z",
        "details": {
            "squeeze": {
                "squeeze_name": "l_shape",
                "duration": 10000
            }
        }
    },
    {
        "type": "image_overlay",
        "time": "2024-10-24T09:36:25.728Z",
        "details": {
            "image_overlay": {
                "geometry": {
                    "x": 0,
                    "y": 0,
                    "width": 300,
                    "height": 300
                },
                "duration": 10000,
                "image_urls": [
                    "http://127.0.0.1:8888/elephant.png"
                ],
                "opacity": 1,
                "fade_in": 0,
                "fade_out": 0,
                "delay_between_images": 0
            }
        }
    }
]
```
