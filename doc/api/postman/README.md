# Postman collection

Exported copy of the **Skandha Transcoder API** collection. The live copy lives in Postman;
this is the version-controlled artefact, so the collection is not cloud-only and changes are diffable.

```
skandha-transcoder-api.postman_collection.json   14 folders, 89 requests
skandha-local.postman_environment.json           26 variables, baseUrl = http://127.0.0.1:8080
```

Import both, select the **skandha-local** environment, and start with `W0 Smoke test`.

## Layout

Folders `01`–`07` are a **reference**: one request per endpoint, each with a description covering its
purpose, required variables, expected status and failure modes.

Folders `W0`–`W6` are **runnable workflows** — multi-call sequences with variable capture, bounded
polling via `setNextRequest`, and cleanup that restores the channel.

| | Folder | Re-runnable | Needs a local file |
|---|---|---|---|
| W0 | Smoke test (read-only) | yes | no |
| W1 | Air an existing clip | yes | no |
| W2 | Upload and air a new clip | yes | **yes** (a video) |
| W3 | Edit the schedule safely | yes | no |
| W4 | Import a program list | yes | **yes** (a channel YAML) |
| W5 | Graphic cue lifecycle | yes | no |
| W6 | Insert an ad break | yes | no |

## Running

Collection Runner → pick **one folder** → **Delay 1000 ms** → Run.

- **Run folders, not the whole collection.** Folder 07 contains a request that restarts the engine.
- `setNextRequest` only works in the Runner, never on a single Send.
- W2 and W4 store a *local file path*, so they do not survive sync and need `--working-dir` under Newman.

## Three behaviours the workflows encode

These were each found by running the chains against a live container, and they are the reason the
folders are shaped the way they are.

1. **The rundown owns the queue.** While `ENABLE_RUNDOWN_SERVICE=true`, a direct
   `POST /playlists/{id}/items` is accepted with `202` and then reverted within about a second — the
   reconciler deletes any playlist item absent from the active version (`on_air_bridge_removed`).
   Add programs with `POST /rundowns/{id}/apply` and `insertItems` instead.
2. **Applies cannot overlap.** `POST /rundowns/{id}/apply` returns
   `409 the managed playlist is still applying a prior revision` while the queue is mid-reconciliation,
   so consecutive applies need a settle-wait between them.
3. **`DELETE /media/{id}` unlinks the file from disk**, and `POST /media/import` dedupes by relative
   path — so importing a known clip returns the *existing* asset. Deleting that would remove a live
   channel file. Only delete assets you uploaded.

## Regenerating this export

The live collection is the source of truth. Re-export after editing it in Postman
(or via the Postman API) and commit the result.
