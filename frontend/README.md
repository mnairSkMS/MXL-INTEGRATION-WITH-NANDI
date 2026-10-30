# Channel configuration UI

The React single-page app served by the management API at **`/channel`**. It is the
provisioning screen for the engine configuration document: load, edit, validate, commit,
restart, and watch the supervisor come back up.

It is a separate page from `/operator`, not a replacement. Playout, rundown, graphics and
slates live on the operator page; engine configuration lives here. Graphics upload is
deliberately not part of this screen — it remains at `/operator/tools/graphics-upload`.

## API surface it drives

Every call goes to the FastAPI app in `api/src/transcoder_api`. There is no mock layer: all
five endpoints are implemented by this appliance, and simulating any of them would report
behaviour the engine does not have.

| Call | Purpose |
| --- | --- |
| `GET /config` | Load the current document. 404 means an unconfigured appliance, not an error. |
| `PUT /config?dryRun=true` | Structural pre-flight. Never writes. |
| `PUT /config` | Whole-document replacement, atomic rename on the server. |
| `POST /config/restart-transcoder` | Re-read the config. Takes the channel off air. |
| `GET /config/check-transcoder` | Supervisor process state, polled after restart. |
| `POST /config/stop-transcoder` | Take the channel off air and leave it there. |
| `POST /config/start-transcoder` | Bring a stopped engine back up. |
| `GET /inputs/probe` | ffprobe the prospective source; fills the output ladder. |

## Things that are load-bearing

**The config `version` selects a parser dialect.** Authority is
`transcoder/app/src/application/config/parser/json/parser.cpp`, which accepts 1.0.0 through
1.4.2. Below 1.4.0 `outputs[].container` is mandatory; below 1.1.0 stream handling differs. A
new document is written at `DEFAULT_CONFIG_VERSION` in `features/channels/constants/schema.ts`;
an existing one keeps its own version, because re-stamping a live channel would change how the
engine reads the rest of the file.

**Unknown keys are preserved.** `PUT /config` replaces the whole document, so rebuilding the
JSON from form fields alone would destroy hand-edited keys — logger settings, license paths,
advanced GStreamer properties, squeeze and blanking blocks. `channelFormToConfig` merges onto
the fetched base document rather than constructing from scratch. The engine's JSON parser
silently ignores keys it does not know, so a dropped key is not an error; it is a setting that
stops applying.

**Probing opens the source.** A probe is only safe while the engine is not holding *that
endpoint*. Probing a single-subscriber source — an SRT caller target, or a UDP port the engine
already has — can steal or duplicate the live subscription. The guard is per input and compares
endpoints, not engine state: `utils/sourceIdentity.ts` ignores query strings, so editing
`latency` cannot sneak past it, while a different port on the same host is allowed. Probing a
*new* URI while a channel runs is safe and is the main reason to be on this screen. To probe the
source the engine is actually holding, stop the engine first — the Stop Engine control does
this. That guard is the UI's, not the API's.

**Stop is not restart.** A restart ends with the channel back on air; a stop leaves it down
until someone starts it. They have separate confirmation dialogs for that reason — reusing the
restart dialog would promise a recovery that never comes.

**Most probe fields are optional.** On a live MPEG-TS, ffprobe reports no per-stream bitrate,
often no frame rate, and PIDs only when the container is MPEG-TS. The types in
`features/channels/types/view.ts` mirror `InputProbe` in `api/src/transcoder_api/model/rest_api.py`
exactly, including nullability, and "apply to ladder" copies only fields the probe actually
established.

**Restart is destructive and never automatic.** If the supervisor reports `RUNNING`, the page
requires explicit confirmation before restarting, disables controls in flight, and never retries
a failed save or restart on its own.

**NDI cannot be probed.** `GET /inputs/probe` answers 422 for `ndi` because ffmpeg in the
runtime image has no NDI input protocol. Integration gated — the button is disabled rather than
sending a request that cannot succeed.

## Layout

```
src/
  app/                 shell, providers
  components/          presentational primitives
  features/channels/
    api/               the six calls above
    mappers/           form <-> engine config, both directions
    schemas/           zod validation
    types/             form model, API DTO, view model - three deliberately separate layers
    components/        input, output, probe result, restart confirmation
    pages/             CreateChannelPage - orchestrates the whole sequence
  services/            fetch client, error normalization
```

The three type layers are separate on purpose: binding form controls straight to engine JSON
couples the UI to a schema that changes for reasons that have nothing to do with the screen.

## Working on it

```sh
npm ci
npm run dev      # :3000, proxies /config /inputs /hls to localhost:8080
npm test         # vitest, 27 tests
npm run build    # writes ../api/src/transcoder_api/static/channel
```

`npm run build` output is **committed**, so the tree serves `/channel` without Node installed
and the `./api/src/transcoder_api` bind mount in `docker-compose.playlist.yml` picks up a local
build without an image rebuild. Rebuild and commit it when you change anything under `src/`.
The `frontend-builder` stage in `docker/utpl-transcoder/Dockerfile` also rebuilds from source
and runs `tsc` and `vitest`, so a type error or failing test breaks the image build.

The Vite `base` is `/channel/`. Building without it produces a page whose every asset 404s.

To point a dev server at another appliance, set `VITE_API_BASE_URL` (see `.env.example`).
