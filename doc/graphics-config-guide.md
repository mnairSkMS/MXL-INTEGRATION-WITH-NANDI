# Graphics Guide — Adding and Configuring Logo, L-band, Aston, and Bug

How to add a new graphic of any kind to this channel, configure it, and put it
on air. Covers static image graphics and animated video (MOV) graphics.

| | |
|---|---|
| Build | `1.1.0-beta.2-skandha.3` |
| Catalog | `api/src/transcoder_api/assets/graphics_catalog.json` |
| Media | `media/graphics/` |
| Thumbnails | `api/src/transcoder_api/static/graphics/` |
| Geometry | Fractions of the output frame (0..1) |

---

## 1. The four kinds

Every graphic declares a `kind`. The kind is not cosmetic — it decides which
animation modes are legal, whether a duration is required, and whether the
programme video is squeezed to make room.

| Kind | Purpose | Squeezes programme? | Duration | Modes |
|---|---|---|---|---|
| `logo` | Persistent channel identity, typically top-left | No | Optional — may hold indefinitely | `snap`, `fade` |
| `bug` | Persistent small mark, typically a corner | No | Optional — may hold indefinitely | `snap`, `fade` |
| `lband` | Sponsor L-band along bottom/side | **Yes** | **Required** | `snap`, `shrink_overlay`, `reveal` |
| `aston` | Lower-third strap / name super | **Yes** | **Required** | `snap`, `shrink_overlay`, `reveal` |

Two rules follow directly from the code in
`api/src/transcoder_api/services/graphics.py`:

- **Only `logo` and `bug` may hold without a duration.** Anything else without a
  duration is rejected with *"only Logo and Bug may hold without a duration"*
  ([graphics.py:140](../api/src/transcoder_api/services/graphics.py#L140)).
- **`lband` and `aston` share one exclusive slot.** Starting a second while one
  is active fails with *"an L-band or Aston cue is already active; stop it
  first"* ([graphics.py:151](../api/src/transcoder_api/services/graphics.py#L151)).
  Logo and bug are unaffected and may run alongside.

---

## 2. Geometry vs squeezeWindow — the key concept

This trips people up, so get it right before authoring artwork.

### All geometry is a fraction of the output frame

Every rectangle in the catalog — `geometry` and `squeezeWindow` alike — is
expressed as fractions between `0.0` and `1.0`, not pixels. The engine resolves
them against the channel's real output raster when the cue fires, so one catalog
entry is correct at 1080p, 4K, or anything else. A value outside `0..1` fails
the catalog load.

To convert an existing pixel measurement, divide by the canvas you measured it
on: `1500 / 1920 = 0.78125`. Six decimal places is enough to round-trip exactly;
do not truncate further. The squeeze-box tool at `/operator/static/squeeze-box.html`
emits fractions directly.

### Logo and bug — geometry is the placement box

The image is drawn exactly into this rectangle.

```json
"geometry": { "x": 0.03125, "y": 0.055556, "width": 0.171875, "height": 0.125 }
```

That is a logo occupying 17.2% of the frame width, inset from the top-left
corner — 330x135 at 1080p, 660x270 at 4K.

### L-band and aston — geometry is the whole canvas

```json
"geometry":      { "x": 0.0, "y": 0.0, "width": 1.0,     "height": 1.0      },
"squeezeWindow": { "x": 0.0, "y": 0.0, "width": 0.78125, "height": 0.763889 }
```

The artwork is a **full-frame PNG with a transparent hole** where the programme
video shows through. `geometry` is `1.0 x 1.0` because the overlay *is* the whole
canvas. `squeezeWindow` then tells the compositor where to shrink the programme
picture so it lands inside that transparent hole.

**The two must agree.** If the transparent region of your PNG does not match
`squeezeWindow`, the programme video will be mispositioned — overlapping the
sponsor artwork, or leaving a black gap. Authoring the artwork and setting the
window are one task, not two.

Author the PNG at the raster you deliver. Geometry is resolution-independent, but
the artwork is not: a 1920x1080 sponsor bar upscaled to 4K will look soft, and
L-band artwork usually carries type.

Existing examples show the range:

| Asset | squeezeWindow | At 1080p | Shape |
|---|---|---|---|
| `lband-voltedge` | `0, 0, 0.78125, 0.763889` | `1500x825` | Programme top-left, L on right + bottom |
| `lband-playboxtv` | `0.161979, 0, 0.838021, 0.822222` | `311,0 1609x888` | Programme top-right, L on left + bottom |
| `aston-static` | `0, 0, 1.0, 0.849074` | `1920x917` | Full width, strap across the bottom |

---

## 3. Animation modes

| Mode | What happens | Available to |
|---|---|---|
| `snap` | Appears and disappears instantly, no transition | all kinds |
| `fade` | Opacity fade in and out over `fadeMs` | `logo`, `bug` |
| `shrink_overlay` | Programme shrinks while the overlay fades in on top | `lband`, `aston` |
| `reveal` | Overlay sits **behind** the programme (`z_order: 0`); the programme shrinks to reveal it | `lband`, `aston` |

`reveal` is the cleanest look for sponsor breaks and is the default for every
L-band and aston in the catalog. It works by placing the graphic on a layer
below the programme and then squeezing the programme out of the way, so the
graphic is uncovered rather than pasted on top.

Two mechanics worth knowing, both from
[graphics.py:180-206](../api/src/transcoder_api/services/graphics.py#L180-L206):

- **Duration is the complete visual interval**, including both animation legs.
  The engine event is shortened by one `fadeMs` internally so the total on-air
  time matches what you asked for.
- **`reveal` holds its behind-programme layer 1200 ms longer** than the visible
  duration, giving the programme time to unsqueeze without the graphic
  disappearing mid-move.

**Constraint:** for `fade`, `shrink_overlay`, and `reveal`, `2 * fadeMs` must be
strictly less than `durationMs` — the two legs have to fit inside the duration.
Otherwise: *"duration must leave time for both animation legs"*.

---

## 4. Adding a graphic — the procedure

Four steps, same shape for every kind. Details per kind follow in section 5.

### Step 1 — Put the media file in place

```bash
cp my_sponsor.png /home/skandha/skandha-transcoder-main/media/graphics/
```

Mounted read-only into the container at `/media/graphics`. Host edits appear
immediately; no copy into the image is needed.

- **Static graphics:** PNG with alpha is the convention. Any still the engine can
  decode also works — WebP, TIFF, TGA, BMP, single-frame GIF — because overlay media
  goes through libav and is converted to BGRA.
- **Animated graphics:** MOV with the QTRLE codec and alpha is the convention; WebM,
  MP4 and animated GIF/WebP decode too, though only some carry alpha. See section 6
  first — video graphics have a known defect.

Uploading through `/operator/tools/graphics-upload` types the asset for you: it counts
frames rather than trusting the extension, so an animated GIF is registered as a video
graphic and a single-frame one as a still.

### Step 2 — Add a thumbnail for the operator UI

```bash
cp my_sponsor_thumb.jpg \
   /home/skandha/skandha-transcoder-main/api/src/transcoder_api/static/graphics/
```

Served at `/operator/static/graphics/<filename>`. Only affects the operator UI
tile — a missing thumbnail leaves a broken image but the graphic still airs.

### Step 3 — Add a catalog entry

Edit `api/src/transcoder_api/assets/graphics_catalog.json` and append to
`assets`. Full field reference in section 7.

**No restart is required.** `catalog()` re-reads the file on every request
([graphics.py:30](../api/src/transcoder_api/services/graphics.py#L30)), and the
file is bind-mounted, so a save is live immediately.

### Step 4 — Verify it registered

```bash
curl -s http://172.16.105.68:8080/graphics/catalog \
  | python3 -c "import json,sys; [print(a['id'], a['kind'], a['enabled']) for a in json.load(sys.stdin)['assets']]"
```

Your new `id` should appear with `enabled: true`. If the whole call returns
*"graphics catalog is unavailable or invalid"*, the JSON is malformed —
check with `python3 -m json.tool`.

---

## 5. Per-kind recipes

### 5.1 Logo

Persistent identity mark. No squeeze, no mandatory duration.

```json
{
  "id": "logo-acme",
  "label": "ACME Network",
  "kind": "logo",
  "mediaType": "image",
  "mediaUri": "file:///media/graphics/logo_acme.png",
  "thumbnail": "/operator/static/graphics/logo_acme.png",
  "geometry": { "x": 60, "y": 60, "width": 330, "height": 135 },
  "defaultMode": "fade",
  "defaultAnimationMs": 600,
  "supportedModes": ["snap", "fade"]
}
```

- Author the PNG at exactly `width` x `height`.
- Omit `defaultDurationMs` so it holds until explicitly cancelled.
- Standard safe-area inset is 60px, matching the existing entries.

Put it on air indefinitely:

```bash
curl -X POST http://172.16.105.68:8080/graphics/cues \
  -H 'Content-Type: application/json' \
  -d '{"assetId":"logo-acme","mode":"fade","fadeMs":600}'
```

### 5.2 Bug

Identical mechanics to logo; different placement convention. Existing
`bug-default` sits top-right at `0.86875, 0.055556` in a `0.1 x 0.177778` box —
192x192 at 1080p, inset from the right edge by the same fraction at any raster.

```json
{
  "id": "bug-acme",
  "label": "ACME Bug",
  "kind": "bug",
  "mediaType": "image",
  "mediaUri": "file:///media/graphics/bug_acme.png",
  "thumbnail": "/operator/static/graphics/bug_acme.png",
  "geometry": { "x": 1668, "y": 60, "width": 192, "height": 192 },
  "defaultMode": "fade",
  "defaultAnimationMs": 600,
  "supportedModes": ["snap", "fade"]
}
```

To place a bug of a different size flush to the right margin, compute
`x = 1.0 - width - 0.03125`, where `0.03125` is a 60px inset at 1080p.

A logo and a bug can be on air simultaneously — they do not contend for the
sponsor lock.

### 5.3 L-band

Full-frame overlay plus a squeeze. This is the most involved kind.

**Artwork:** full-frame PNG at your delivery raster, alpha-transparent everywhere
the programme video should show. Decide the programme window first, then design the
L around it.

```json
{
  "id": "lband-acme",
  "label": "ACME Motors",
  "kind": "lband",
  "mediaType": "image",
  "mediaUri": "file:///media/graphics/lband_acme.png",
  "thumbnail": "/operator/static/graphics/lband_acme.jpg",
  "geometry":      { "x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0 },
  "squeezeWindow": { "x": 0.0, "y": 0.0, "width": 0.78125, "height": 0.763889 },
  "defaultDurationMs": 15000,
  "defaultAnimationMs": 600,
  "defaultMode": "reveal",
  "supportedModes": ["snap", "shrink_overlay", "reveal"]
}
```

- `squeezeWindow` **must** match the transparent hole in the PNG.
- Preserve 16:9 in the squeeze window or the programme will be distorted —
  `0.78125 x 0.763889` (1500x825 at 1080p) is close but not exact. Check your
  own numbers against the aspect you want.
- `defaultDurationMs` is required in practice: an L-band with no duration
  anywhere in the chain is rejected.

Air it for 15 seconds with a 600 ms reveal:

```bash
curl -X POST http://172.16.105.68:8080/graphics/cues \
  -H 'Content-Type: application/json' \
  -d '{"assetId":"lband-acme","mode":"reveal","durationMs":15000,"fadeMs":600}'
```

### 5.4 Aston

Same machinery as L-band — full-canvas overlay plus squeeze — but the squeeze
normally keeps full width and only trims height, leaving a strap along the
bottom.

```json
{
  "id": "aston-acme",
  "label": "ACME Lower Third",
  "kind": "aston",
  "mediaType": "image",
  "mediaUri": "file:///media/graphics/aston_acme.png",
  "thumbnail": "/operator/static/graphics/aston_acme.jpg",
  "geometry":      { "x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0 },
  "squeezeWindow": { "x": 0.0, "y": 0.0, "width": 1.0, "height": 0.849074 },
  "defaultDurationMs": 15000,
  "defaultAnimationMs": 600,
  "defaultMode": "reveal",
  "supportedModes": ["snap", "shrink_overlay", "reveal"]
}
```

Height `0.849074` leaves a strap across the bottom 15% of the frame — 163px at
1080p. Adjust to your artwork.

Astons contend with L-bands for the same exclusive slot — only one of the two
kinds can be on air at a time.

---

## 6. Animated video graphics (MOV)

Any kind can use a video asset instead of a still. Set `mediaType: "video"` and
add the clip-length fields:

```json
{
  "id": "lband-acme-anim",
  "kind": "lband",
  "mediaType": "video",
  "mediaUri": "file:///media/graphics/lband_acme_anim.mov",
  "clipDurationMs": 4200,
  "defaultDurationMs": 4200,
  "defaultAnimationMs": 0,
  "defaultMode": "reveal",
  "preloadLeadMs": 3000,
  "supportedModes": ["snap", "reveal"]
}
```

Differences from static assets:

- **`shrink_overlay` is not offered** on any video asset in the catalog; video
  kinds support `snap` and `reveal` only.
- **`defaultAnimationMs` is 0** — the clip carries its own animation, so no
  fade leg is added.
- **`clipDurationMs`** is the real clip length and becomes the fallback duration.
- **`preloadLeadMs`** (3000 in the shipped entries) gives the decoder lead time.

### Required runtime gate

Video assets are hidden unless the container sets:

```yaml
ENABLE_VIDEO_GRAPHICS: "true"
```

Without it, `catalog()` marks every video asset `enabled: false`
([graphics.py:44](../api/src/transcoder_api/services/graphics.py#L44)) and cueing
one fails with *"video graphics are disabled until their runtime gate passes"*.
It is currently enabled in `docker-compose.playlist.yml`.

### Known defect — evaluation only

Per [doc/codebase/27](codebase/27-known-gaps-contradictions-and-remediation.md),
measured 2026-09-01, two acceptance criteria **fail**:

- **~840 ms of black on air.** The squeeze starts on schedule but the first
  decoded frame reaches the compositor 21 frames late, and the clip is truncated
  by the same 21 frames at the tail. Reproduced on both sponsor clips and
  generated test cards — it is the pipeline, not the asset.
- **~19 MB retained per start/stop cycle.** 20 cycles retained ~385 MB and RSS
  did not recover. The same test on a static graphic retained ~0.3 MB/cycle.

Alpha handling and 25 fps cadence pass. **Do not use video graphics in
production** until these are fixed. Static PNG graphics have neither problem.

---

## 7. Catalog field reference

| Field | Required | Applies to | Meaning |
|---|---|---|---|
| `id` | yes | all | Unique key used by API and `channel.yml` |
| `label` | no | all | Display name in the operator UI |
| `kind` | yes | all | `logo` / `bug` / `lband` / `aston` |
| `mediaType` | no | all | `image` (default) or `video` |
| `mediaUri` | yes | all | `file:///media/graphics/<file>` |
| `thumbnail` | no | all | `/operator/static/graphics/<file>` |
| `geometry` | yes | all | Placement rect as frame fractions (0..1); `1.0 x 1.0` for lband/aston |
| `squeezeWindow` | lband/aston | lband, aston | Where the programme shrinks to, as frame fractions (0..1) |
| `squeezeName` | alt | lband, aston | Named squeeze from engine config, instead of a window |
| `supportedModes` | yes | all | Legal modes; anything else is rejected |
| `defaultMode` | no | all | Must be a member of `supportedModes` |
| `defaultDurationMs` | lband/aston | all | Fallback duration |
| `defaultAnimationMs` | no | all | Fallback fade/animation length |
| `clipDurationMs` | video | video | True clip length |
| `preloadLeadMs` | video | video | Decoder lead time |
| `enabled` | — | — | **Computed, do not set.** Overwritten by the video gate |

Validation you will hit:

- A `defaultMode` not in `supportedModes` fails the whole catalog load —
  *"has an unsupported defaultMode"* — taking every other graphic down with it.
- Missing any of `id`, `kind`, `mediaUri`, `geometry`, `supportedModes` gives
  *"graphics asset is incomplete"* when that asset is cued.
- An `lband`/`aston` with neither `squeezeName` nor `squeezeWindow` fails at cue
  time with *"squeeze graphics need squeezeName or squeezeWindow"*.
- A `geometry` or `squeezeWindow` component outside `0..1` fails the catalog load —
  *"geometry is a fraction of the output frame, not pixels"*.

---

## 8. Four ways to put a graphic on air

### 8.1 Operator UI

http://172.16.105.68:8080/operator — click the graphic tile. Reads the catalog
directly, so new entries appear on reload.

### 8.2 Cue API — immediate or delayed

```bash
# now
curl -X POST http://172.16.105.68:8080/graphics/cues \
  -H 'Content-Type: application/json' \
  -d '{"assetId":"lband-acme","mode":"reveal","durationMs":15000,"fadeMs":600}'

# in 5 seconds
curl -X POST http://172.16.105.68:8080/graphics/cues \
  -H 'Content-Type: application/json' \
  -d '{"assetId":"lband-acme","mode":"reveal","durationMs":15000,"fadeMs":600,"startInMs":5000}'

# what is on air
curl -s http://172.16.105.68:8080/graphics/cues | python3 -m json.tool

# take it off
curl -X DELETE http://172.16.105.68:8080/graphics/cues/<cue-id>
```

Request fields: `assetId`, `mode`, `durationMs` (>0, optional for logo/bug),
`startInMs` (default 0), `fadeMs` (default 0).

Cue phases reported back: `scheduled` → `on_air` → `restoring`.

### 8.3 channel.yml — declarative, survives restart

`config/channel.yml` is the source of truth, applied on every container start
and rewritten by every Publish.

```yaml
playlist:
- media: coke-commercial-1080p-h264.mp4
  graphics:
  - use: lband-acme          # catalog id
    mode: reveal
    key: graphic-0001        # unique within the file
    at: 0                    # ms after item start
    duration: 15s
    fade: 0.6s

scheduled:
- use: aston-acme
  mode: reveal
  key: graphic-0002
  at: '2026-09-05T18:30:00+00:00'   # wall clock
  duration: 15s
  fade: 0.6s
```

- Under a playlist item, `at` is an **offset in ms from item start**.
- Under `scheduled`, `at` is an **ISO wall-clock timestamp**.
- `key` must be unique — duplicates are rejected with *"duplicate graphics key"*.
- `duration` and `fade` accept unit suffixes (`15s`, `9992ms`, `0.6s`).
- Omitted `mode`, `duration`, `fade` fall back to the catalog defaults
  ([bootstrap.py:146](../api/src/transcoder_api/services/bootstrap.py#L146)).

Hand edits are honoured on the next container start:

```bash
docker compose -f docker-compose.playlist.yml restart
```

Note `PLAYLIST_BOOTSTRAP_WRITEBACK: "true"` means a Publish rewrites this file
in full — comments and shorthand are not preserved.

### 8.4 Raw scheduled event — bypasses the catalog

For one-off placement with no catalog entry. This is what the cue API builds
internally.

```bash
curl -X POST http://172.16.105.68:8080/scheduled_events \
  -H 'Content-Type: application/json' \
  -d '{"type":"image_overlay",
       "details":{"image_overlay":{
         "geometry":{"x":60,"y":60,"width":330,"height":135},
         "image_urls":["file:///media/graphics/logo.png"],
         "opacity":1.0,
         "delay_between_images":40,
         "fade_in":600,"fade_out":600,
         "duration":10000}}}'
```

`delay_between_images: 40` matches the 25 fps output cadence and should be kept
— static images otherwise default to one buffer per second, which starves the
compositor while a squeeze is in progress.

For a squeeze you must post a second `squeeze` event yourself; the catalog path
does this pairing automatically.

---

## 9. Troubleshooting

| Symptom | Cause |
|---|---|
| *"graphics asset does not exist"* | `assetId` not in the catalog — check the exact `id` |
| *"animation mode is not supported by this asset"* | Mode absent from `supportedModes` |
| *"only Logo and Bug may hold without a duration"* | lband/aston with no duration and no `defaultDurationMs` |
| *"an L-band or Aston cue is already active; stop it first"* | Sponsor lock held — DELETE the active cue first |
| *"duration must leave time for both animation legs"* | `2 * fadeMs >= durationMs` |
| *"video graphics are disabled until their runtime gate passes"* | `ENABLE_VIDEO_GRAPHICS` not true |
| *"graphics catalog is unavailable or invalid"* | Malformed JSON — run `python3 -m json.tool` |
| *"has an unsupported defaultMode"* | `defaultMode` not in `supportedModes` — breaks the whole catalog |
| Graphic airs but programme is mispositioned | `squeezeWindow` disagrees with the transparent hole in the PNG |
| Broken thumbnail in UI | File missing from `static/graphics/`; harmless to air |
| ~840 ms black at the start of a MOV | Known video-graphics defect — see section 6 |

### Checks

```bash
# catalog as the API sees it
curl -s http://172.16.105.68:8080/graphics/catalog | python3 -m json.tool

# file actually visible inside the container
docker exec skandha-transcoder ls -la /media/graphics

# active cues
curl -s http://172.16.105.68:8080/graphics/cues | python3 -m json.tool

# engine errors
docker logs --since 5m skandha-transcoder 2>&1 | grep -iE 'overlay|squeeze|error'
```

### Watching it on air

This channel is **SRT-only** — the operator UI preview pane does not work.

```bash
ffplay 'srt://172.16.105.68:9000?mode=caller&latency=120'
```

---

## 10. Checklists

**Static graphic (logo / bug)**

- [ ] PNG with alpha, authored at exactly `geometry` width x height
- [ ] File in `media/graphics/`
- [ ] Thumbnail in `api/src/transcoder_api/static/graphics/`
- [ ] Catalog entry: `id`, `label`, `kind`, `mediaUri`, `thumbnail`, `geometry`, `defaultMode`, `defaultAnimationMs`, `supportedModes`
- [ ] No `defaultDurationMs` if it should hold indefinitely
- [ ] `curl /graphics/catalog` shows it
- [ ] Cued and confirmed on the SRT output

**Squeeze graphic (lband / aston)**

- [ ] Full-frame PNG at the delivery raster, transparent where programme shows
- [ ] `squeezeWindow` matches the transparent hole exactly, as fractions of the frame
- [ ] Aspect ratio of the squeeze window checked for distortion
- [ ] File in `media/graphics/`, thumbnail in `static/graphics/`
- [ ] Catalog entry includes `squeezeWindow` and `defaultDurationMs`
- [ ] `supportedModes` includes `reveal`; `defaultMode` is a member
- [ ] Sponsor lock free before cueing
- [ ] Programme position verified on the SRT output, not just the still

**Video graphic — evaluation only**

- [ ] Above, plus MOV/QTRLE with alpha
- [ ] `mediaType: "video"`, `clipDurationMs`, `preloadLeadMs`
- [ ] `shrink_overlay` omitted from `supportedModes`
- [ ] `defaultAnimationMs: 0`
- [ ] `ENABLE_VIDEO_GRAPHICS: "true"` in compose
- [ ] Accepted the ~840 ms black lead and ~19 MB/cycle leak — not for production
