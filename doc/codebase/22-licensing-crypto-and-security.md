# Licensing, Crypto, and Security

## License flow

The executable accepts a license path through `-l`. `app/src/main.cpp` loads/validates the license before the application runs. `Application::configure` requires an HTTP or HTTPS license-server URI for a non-offline license. `application/license/manager.*` manages online-session validation and renewal behavior; offline licenses do not require that service path.

An invalid or expired license makes the engine log the reason and exit with status 11 (`LICENSE_ERROR`), as does a license the manager reports invalid while running. Supervisor's `autorestart=unexpected` retries it, so replacing the license file is picked up on the next attempt.

### Inspecting a license

`transcoder -l <file> --license-info` needs no config. It runs the same load, signature check and decrypt as startup (`loadLicense` in `main.cpp`), with the keys embedded in that binary, and prints one JSON object as the last stdout line (a library initialization log line precedes it):

| Field | Meaning |
| --- | --- |
| `status` | `valid`, `expired`, `notYetValid`, or `invalid` |
| `error` | Only with `invalid`: why the file could not be read, authenticated, or decrypted, including a missing file |
| `id`, `name` | License identity |
| `validSince`, `validUntil`, `ttl` | Validity window, milliseconds since the epoch / milliseconds |
| `offline` | `true` for an offline license; `false` needs `application.licenseServer.uri` |
| `maxSessions` | Concurrent sessions for online licenses (enforced by the license server only) |
| `createdAt`, `checkedAt` | Issue time, and the clock the status was computed against |

The exit status is 0 for `valid`, 1 for an authentic but `expired` or `notYetValid` license, and 2 for `invalid`. A license signed with the other key set (a development license against a Release build, or the reverse) is `invalid`. `--license-info` without `-l` exits 4. The management API's `/license` routes use this mode because Python cannot decrypt a license ([17](17-rest-grpc-and-sqlite-control-plane.md#license-routes)).

### Replacing and removing the installed license

The engine reads the license only at start; there is no hot reload. `PUT /license` validates an upload with `--license-info`, replaces the file, and restarts the engine, which takes the channel off air for the restart. `DELETE /license` stops the engine and removes the file, leaving the channel off air until a license is uploaded. The path is `TRANSCODER_LICENSE_PATH` (default `/opt/utpl/etc/transcoder.lic`), shared by the API and the engine command line in `supervisord.conf`. Details and status codes are in [17](17-rest-grpc-and-sqlite-control-plane.md#license-routes).

Generating, signing and listing issued licenses are vendor-side operations and are not exposed by the appliance API: they need the signing private key. Use `transcoder/license/make-license.sh` or the tools under `transcoder/license/tools` (`doc/license/license-tools.md`).

The `transcoder/lib/license` library models license files, metadata, signatures, JSON serialization, ELF key loading/storing, and embedded-key utilities. `transcoder/lib/crypto` provides OpenSSL-backed cipher, hashing, signature, and manager primitives. The CMake build runs `keyEmbedder` after building the transcoder executable to embed the configured public/encryption material.

## Packaging and environments

The build chooses development or production key directories based on CMake build type. The repository contains development and production key/license material under `transcoder/license`; handle these as sensitive regardless of their current checkout visibility. Do not include key bytes, license contents, tokens, user/password fields, or cloud credentials in documentation, logs, fixtures, or issues.

## Security boundaries

- The config/API path may carry credentials, license-server URLs, POIS endpoints, S3 settings, and media URIs.
- The FastAPI config and license routers can read/write configuration, replace or delete the engine license, and control a supervised process; deploy them only behind appropriate network access control/authentication. An unauthenticated caller who reaches `DELETE /license` can take the channel off air until a new license is uploaded.
- GStreamer plugins load native code from runtime search paths. Treat `GST_PLUGIN_PATH` and mounted SDK/plugin directories as code-execution boundaries.
- HTTP/WebDAV/S3/POIS/license connections are network trust boundaries; record authentication/TLS verification behavior when testing.
- The RTMP relay (`rtmp_relay`, MediaMTX) listens on `:1935` on every interface. Its only credential is the stream key in the publish path; reads are loopback-only, so only the engine can pull a stream. An RTMP input URI therefore contains a secret: the probe route redacts it from error text, and configs holding it should be handled like credentials. Keep MediaMTX's other protocols disabled when bumping its version (see the header of `mediamtx.yml`).

## Verification

Test offline startup, missing/invalid/mismatched license, expired/unsupported license, online URI validation, unavailable server, invalid server responses, and orderly shutdown. For the management routes, run `api/tests/license_test.py`, then in a running container check that a refused upload leaves the file and engine untouched, that an accepted upload restarts the engine (its log shows `Have a correct license '<name>'` with the new name), and that `DELETE /license` leaves Supervisor `STOPPED` with no file behind. Do not use production keys or licenses in test reports. Online-server behavior remains integration gated until exercised against an authorized endpoint.
