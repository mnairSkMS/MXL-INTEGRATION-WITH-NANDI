# Graphics Upload: Frontend Design Guide

This page is for whoever builds a screen that adds sponsor graphics (logo, bug, L-band, aston) and puts them on air. It covers the API calls in order, what each returns, the screens and states a frontend needs, and the errors to expect. The source is `api/src/transcoder_api/routers/graphics.py`, `services/graphics_upload.py` and `services/graphics.py`. The shipped operator page (`/operator`, `static/operator.js`) is a working reference.

## The flow at a glance

```
pick file ──► POST /graphics/uploads ──► draw the rectangle on the preview ──► POST /graphics/assets
                (stage + validate)         (GET …/uploads/{id}/preview)          (commit to the catalog)
                       │                                                               │
                       └─ cancel: DELETE /graphics/uploads/{id}                        ▼
                                                                    GET /graphics/catalog (list)
                                                                    POST /graphics/cues  (put on air)
                                                                    GET /graphics/cues   (status)
                                                                    DELETE /graphics/cues/{id} (stop)
```

Upload is two-phase because the operator draws the asset's geometry on top of the artwork, so the geometry is not known when the file arrives. Staging returns the picture size and a preview URL for the drawing surface. The commit carries the rectangle the operator drew.

## 1. Stage the file

`POST /graphics/uploads`, multipart, one part named `file`.

- **Accepted:** anything `ffmpeg` can decode that has a video stream.
  - Images: `.png .jpg .jpeg .webp .gif .apng .avif .bmp .tga .tif .tiff`.
  - Video containers: the same list as managed media, for example `.mov .mp4 .ts .mxf`.
- **Size limit:** `GRAPHICS_MAX_UPLOAD_BYTES` (default 256 MB).
- **Lifetime:** a staged file expires after `GRAPHICS_UPLOAD_TTL_SECONDS` (default 1 h) unless committed.

Response `201`:

```json
{
  "uploadId": "4b6f…",
  "mediaType": "image",            // or "video": decided by frame count, not by extension
  "width": 1920, "height": 1080,   // source pixels: use them for the drawing surface's aspect ratio
  "previewUrl": "/graphics/uploads/4b6f…/preview",
  "expiresAt": "2026-10-24T10:15:00Z",
  "contentBox": {"x": 0.0, "y": 0.0, "width": 0.8, "height": 0.85}   // or null
}
```

- **`contentBox`:** the bounding box of the artwork's transparent region, as fractions of the frame. For an L-band or aston PNG with a hole for the programme, this is exactly where the squeezed programme belongs. Use it to pre-fill the squeeze rectangle, because a hand-drawn rectangle cannot hit it to the pixel. It is `null` when the artwork has no transparency.
- **`mediaType: "video"`:** the asset is animated. It only appears in the catalog as `enabled: true` when the deployment sets `ENABLE_VIDEO_GRAPHICS=true`. Tell the operator up front.

**UI states:** idle → uploading (show a progress bar; use `XMLHttpRequest` or `fetch` with a stream for progress) → validating (the server probes the file before it answers) → staged. On `422` show the message: it names the problem (unsupported extension, not decodable, too large).

## 2. Show the preview and draw

`GET /graphics/uploads/{uploadId}/preview` returns the staged file itself (`Cache-Control: no-store`). For an image, put it in an `<img>`; for a video, use a `<video>` element if the browser can play the container, otherwise fall back to a still.

Draw on a surface with the **source** aspect ratio (`width:height` from step 1), overlaid on a 16:9 programme frame. All rectangles are **fractions of the output frame**: `x, y, width, height` in 0–1, with `x + width ≤ 1` and `y + height ≤ 1`. Snap to sensible values and show the pixel equivalent for a 1920×1080 frame next to the handles.

What to draw depends on the kind the operator picks:

| Kind | What the operator draws | Sent as | Notes |
|---|---|---|---|
| `logo`, `bug` | Where the graphic sits on the programme | `geometry` | Sending `squeezeWindow` is an error. |
| `lband`, `aston` | Where the **programme** goes while the band is up (the hole) | `squeezeWindow` | The artwork itself is always full frame. Sending `geometry` is an error. Pre-fill from `contentBox`. |

Offer a cancel button that calls `DELETE /graphics/uploads/{uploadId}` (`204`). Staged files also expire on their own.

## 3. Commit to the catalog

`POST /graphics/assets`:

```json
{
  "uploadId": "4b6f…",
  "id": "lband-voltedge",           // lowercase letters, digits, hyphens; must not already exist
  "kind": "lband",                  // logo | bug | lband | aston
  "label": "VoltEdge",              // optional display name; defaults to id
  "squeezeWindow": {"x": 0.0, "y": 0.0, "width": 0.8, "height": 0.85}
}
```

`201` returns the new catalog entry. The server fills in, per kind:
- `supportedModes`, `defaultMode`, `defaultDurationMs` (15 s for L-band/aston), `defaultAnimationMs` (600)
- `thumbnail` (`/graphics/assets/{id}/thumbnail`)
- `clipDurationMs` for video

Validate the `id` in the form (`^[a-z0-9][a-z0-9-]*$`), and suggest one from the label and kind (`lband-` + slug).

| Status | Meaning | Show |
|---|---|---|
| 404 | Staged upload expired or unknown | "Upload expired, please upload again" and return to step 1 |
| 409 | An asset with that `id` exists | Ask for another id; keep the drawing |
| 422 | Bad kind/id, wrong or missing rectangle, rectangle off the frame | The message, next to the field it names |
| 503 | Graphics storage is not writable | A deployment problem: show the message as-is (it names the path and the setting) |

## 4. List, cue and stop

- **`GET /graphics/catalog`** lists every asset with `id`, `label`, `kind`, `mediaType`, `thumbnail`, `supportedModes`, `defaultMode`, `defaultDurationMs` and `enabled`. Show disabled (video) assets greyed out, with the reason.
- **`GET /graphics/assets/{id}/thumbnail`** returns the asset's media file. Use it for the tile.
- **`POST /graphics/cues`** puts one on air: `{"assetId", "mode", "durationMs"?, "startInMs": 0, "fadeMs": 0}`.
  - `mode` must be one of the asset's `supportedModes`:
    - `snap` or `fade` for logo and bug;
    - `snap`, `shrink_overlay` or `reveal` for L-band and aston.
  - Omitting `durationMs` uses the asset default. Only logo and bug may hold with no duration.
  - With an animated mode, `durationMs` must exceed `2 × fadeMs`.
  - Only one L-band or aston can be active at a time: a second returns `409` ("stop it first"). Offer a "replace" action that stops the current one and then cues the new one.
- **`GET /graphics/cues`** returns each cue's `phase`: `scheduled` (start delay running), `on_air`, or `restoring` (animating out). It also returns `startsAt`, `endsAt` and `restoreUntil`. Poll it every 1–2 s while anything is active, and drive a countdown from `endsAt`.
- **`DELETE /graphics/cues/{id}`** stops a cue early (`204`).

## Screens to build

1. **Library:** a grid of catalog tiles (thumbnail, label, kind badge, disabled badge) and an "Add graphic" button.
2. **Add graphic** (one dialog, three steps):
   - pick a file and upload it, with progress;
   - choose the kind and draw the rectangle over the preview, pre-filled from `contentBox`;
   - name it and save.
   Keep the `uploadId` and drawing in component state, so a `409` on save does not lose work.
3. **On-air panel:** the active cues with phase, countdown and a Stop button, plus quick-cue buttons per asset (mode picker, duration) that disable while a sponsor cue is already up.

## Practical notes

- The API has no authentication. Bind the UI to a trusted network (`UI_BIND` in compose).
- All times in responses are UTC ISO-8601.
- Uploads are large for video graphics. Show progress, and do not retry a `422` automatically.
- Graphics media lives in `GRAPHICS_MEDIA_DIR` (default `/media/graphics`). The catalog file is `GRAPHICS_CATALOG_PATH`. Both must be writable for commits.

## Related

[15 events, overlays and squeeze](15-events-overlays-slates-and-squeeze.md) · [17 REST/gRPC control plane](17-rest-grpc-and-sqlite-control-plane.md) · [Commercial L-band/aston from Swagger](../api/COMMERCIAL_LBAND_ASTON_SWAGGER.md)
