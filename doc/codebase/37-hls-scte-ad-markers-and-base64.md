# HLS SCTE-35 Ad Markers and the Base64 Payload

A working guide to the `hls.adMarkers` feature: how a SCTE-35 section becomes
`#EXT-OATCLS-SCTE35` / `SCTE35=` text in a media playlist, and exactly where to cut in when you
want to change the base64 encoding or the configuration surface.

Scope: the `elemental` and `enhanced` ad-marker flavours. Everything below is current source in
this checkout. Status: **Build verified** for the serialiser and its unit tests; **Statically
implemented** for the end-to-end live path (no runtime capture of a real avail is recorded here).

Related pages: [14 — HLS, HTTP and S3](14-hls-http-and-s3.md),
[16 — SCTE-35, ESAM, POIS, blackout and blanking](16-scte35-esam-pois-blackout-and-blanking.md),
[05 — Configuration reference](05-configuration-reference.md).

---

## 1. The shape of the thing

Two flavours are live. `ADOBE` exists in the enum but is not serialised.

| Flavour | Config string | Tags written |
| --- | --- | --- |
| Elemental | `"elemental"` | `#EXT-X-CUE-OUT:<dur>`, `#EXT-X-CUE-OUT-CONT:<elapsed>/<dur>`, `#EXT-X-CUE-IN` |
| Enhanced (OATCLS) | `"enhanced"` | all of the above, **plus** `#EXT-OATCLS-SCTE35:<base64>` before each cue-out, and `#EXT-X-CUE-OUT-CONT:ElapsedTime=…,Duration=…,SCTE35=<base64>` |

Enhanced does not replace Elemental — it is Elemental **plus** two base64 carriers, and it
reshapes the continuation tag from the rational `6.000/30.000` form into the attribute-list form.

Shape of the enhanced output (schematic — `<b64>` stands for the encoded section; a 30 s break
across 6 s segments, matching the unit-test fixture):

```
#EXT-OATCLS-SCTE35:<b64>
#EXT-X-CUE-OUT:30.000
#EXTINF:6.000,
seg0.ts
#EXT-X-CUE-OUT-CONT:ElapsedTime=6.000,Duration=30.000,SCTE35=<b64>
#EXTINF:6.000,
seg1.ts
```

### Breaks that do not end by themselves

A `splice_insert` with `break_duration_auto_return = 0` carries a duration, but that duration is
advisory: the avail closes when a matching return to network arrives, not when it elapses. Such a
break is signalled without an end time — a bare `#EXT-X-CUE-OUT`, and continuations carrying
`ElapsedTime` and `SCTE35` but **no** `Duration`:

```
#EXT-OATCLS-SCTE35:<b64>
#EXT-X-CUE-OUT
#EXTINF:6.000,
seg0.ts
#EXT-X-CUE-OUT-CONT:ElapsedTime=6.000,SCTE35=<b64>
#EXTINF:6.000,
seg1.ts
```

The continuation keeps running until the return to network is seen; the advisory duration never
closes it. Elemental's `<elapsed>/<duration>` form has nowhere to put an unknown duration, so that
flavour emits the cue-out and no continuation rather than inventing an end time.

### Where the cue-in lands

`#EXT-X-CUE-IN` goes on the first segment that begins at or after the return to network. An
auto-returning break always ends near a segment boundary — its end is derived from a start that
was itself aligned — but an operator's cue-in lands wherever they press it, so a boundary-margin
test alone missed it and the avail stopped being signalled without ever being closed. The segment
carrying the cue-in is the same one the continuation stops on, so no segment is marked as both
continuing and ending.

Before 2026-09-02 the duration was discarded outright for these breaks
(`hls/types.h`, `duration_flag && break_duration_auto_return`), which suppressed the continuation
entirely: the only marker scrolled out of the live window while the break was still open, leaving a
player joining mid-break no way to know it was inside an avail.

`<b64>` is **not** the base64 that came in on the wire. The test feeds in
`/DAlAAAAAAAAAP/wFAUAADA5f+/+cr0AUP4AKTLgAAAAAAAAa/2GtQ==`, but `sectionFromBase64()` rewrites the
splice time to the local running time and `packetize()` recomputes the CRC, so the emitted string
differs from the input. That is why the test compares against
`makeBreakStartEvent().getBase64()` rather than a hardcoded literal — do the same in any test you
add.

---

Each `EXT-X-CUE-OUT`/`EXT-X-CUE-IN` placement is logged once as `HLS <tag> placed …`, and the injection itself as a scheduler `FIRED` line; see [21](21-logging-observability-and-debugging.md#as-run-event-log).

## 2. End-to-end flow

```
JSON config  "adMarkers": ["enhanced"]
   │
   ▼  transcoder/app/src/application/config/parser/json/parser.cpp:733
HlsParameters::addAdMarker(ScteMarker::ENHANCED)      (parameters.h:342)
   │
   ▼  transcoder/lib/utpl/src/utpl/transcoder/output/utils.cpp:20
hls::Streamer::Parameters::setScteMarkers(...)        (streamer.h:72)
   │
   ├──▶ GATE:  Streamer::putScteData()  streamer.cpp:361
   │           if marker list is EMPTY  ->  event discarded, nothing reaches the playlist
   │           also drops anything that is not splice_insert / time_signal (streamer.cpp:367)
   │
   ▼
hls::ScteEvent newEvent(data)           streamer.cpp:373
   │            └── CTOR ENCODES BASE64 ONCE   types.h:86-94
   │                gst_mpegts_section_packetize()  ->  utils::toBase64()
   │            └── derives isOut()/isIn()/getDuration() from the SIT
   │
   ▼  held in Streamer::_scteEvents, aged out as the window slides
MediaPlaylist::serialize(..., scteEvents, scteMarkers)   streamer.cpp:594
   │
   ▼  playlist.cpp:100   const bool enhanced = _hasMarker(scteMarkers, ENHANCED);
   ├── playlist.cpp:167  if (enhanced) _addOatclsTag()   ->  #EXT-OATCLS-SCTE35
   └── playlist.cpp:273  if (enhanced) attribute-form CONT with SCTE35=<base64>
                         else          rational-form CONT
```

Two facts worth internalising before you change anything:

1. **The marker list gates ingestion, not serialisation.** `serialize()` writes the Elemental cue
   tags for whatever events it is handed, unconditionally. An empty `adMarkers` produces no tags
   only because `Streamer::putScteData()` throws the events away first. The unit test
   `elementalIsTheDefaultShapeAndEnhancedOnlyAdds` pins this: `serializeWith({})` must be
   byte-identical to `serializeWith({ELEMENTAL})`.
2. **Base64 is only produced on the passthrough path.** `HlsOutputSink::putScteData()`
   ([hls.cpp:246](../../transcoder/lib/utpl/src/utpl/transcoder/output/sink/hls.cpp#L246)) does all
   of this inside `if (getMode() == ScteOutputMode::PASSTHROUGH)` and only for
   `Container::MPEGTS`. Other SCTE output modes never reach the streamer, so no markers appear.

---

## 3. Where the base64 actually comes from

One site, in the `ScteEvent` constructor —
[include/utpl/hls/types.h:86-94](../../transcoder/lib/utpl/include/utpl/hls/types.h#L86-L94):

```cpp
// Encode once here rather than per playlist refresh: serialize() walks
// every live event for every segment, on every refresh.
if (this->section) {
    gsize dataSize = 0;

    auto *raw = gst_mpegts_section_packetize(this->section, &dataSize);
    if (raw) {
        this->base64 = utils::toBase64(raw, dataSize);
    }
}
```

Three design points, each of which you will trip over if you change it blindly:

- **`gst_mpegts_section_packetize()` re-serialises the section to its wire bytes**, including the
  table header and CRC. It is *not* a stored copy of the bytes that arrived on the input. If the
  section was constructed in-process (scheduled avail, ESAM response) the CRC is recomputed here.
  The buffer is owned by the section — do not free it, and it is only valid while the section is.
- **Encoded once at construction, not at serialisation.** The playlist is re-serialised on every
  segment close, and `serialize()` loops every live event for every segment; encoding there would
  be O(events × segments × refreshes) of pure repeat work. The cached string is stored in
  `ScteEvent::base64` and copied/moved by the copy and move constructors (types.h:60, :77).
- **Failure degrades quietly.** If `packetize()` returns null, `base64` stays empty, and both
  emission sites skip their attribute rather than writing an empty one — so the output falls back
  to plain Elemental instead of emitting `SCTE35=`.

### The encoder itself

`utils::toBase64()` —
[transcoder/lib/common/src/utpl/common/utils.cpp:60](../../transcoder/lib/common/src/utpl/common/utils.cpp#L60):

```cpp
std::string toBase64(const void *data, size_t dataSize,
                     bool urlEncodeMode = false, bool newLines = false);
```

OpenSSL BIO chain. Defaults matter here:

- `newLines = false` sets `BIO_FLAGS_BASE64_NO_NL`, giving one unwrapped line. **Required** — a
  wrapped line would corrupt the playlist.
- `urlEncodeMode = false` gives the standard alphabet (`+`, `/`, `=` padding). The OATCLS
  convention expects standard, padded base64. `urlEncodeMode = true` would emit `-`/`_` and strip
  padding — correct for URLs, wrong for these tags.

The SCTE call site passes neither flag, i.e. takes both defaults. That is deliberate.

### Why it is written unquoted

[playlist.cpp:53-64](../../transcoder/lib/utpl/src/utpl/hls/playlist.cpp#L53-L64):

```cpp
m3u.addTagEntry(TAG_EXT_OATCLS_SCTE35)->
    addAttribute(std::make_shared<utils::M3U8::AttributeValueEnum>(event.getBase64()));
```

`AttributeValueEnum` serialises bare; `AttributeValueString` would wrap the value in double
quotes. Unquoted is what the OATCLS convention puts on the wire, and it is unambiguous inside an
attribute list because the base64 alphabet contains no comma. **Swapping in
`AttributeValueString` here is the single most likely accidental break** — it compiles, it looks
harmless, and it silently produces manifests some packagers reject. The test
`enhancedFlavourCarriesTheSectionAsBase64` asserts `#EXT-OATCLS-SCTE35:"` is absent precisely to
catch that.

---

## 4. Recipes

### 4.1 Change how the base64 is produced

Everything about the encoding is behind one call in `ScteEvent`'s constructor
([types.h:86-94](../../transcoder/lib/utpl/include/utpl/hls/types.h#L86-L94)). Edit there; do not
add a second encode site.

| You want | Do this |
| --- | --- |
| URL-safe alphabet | `utils::toBase64(raw, dataSize, /*urlEncodeMode=*/true)` — then update the tests, which compare against `makeBreakStartEvent().getBase64()` and so follow automatically, but also update the OpenAPI description in `Scte35AdMarker`. |
| Encode a *different* byte range (e.g. strip the CRC, or emit only the splice command) | Slice `raw`/`dataSize` before the call. Verify against a decoder — `threefive`, or TSDuck's `tsdump`. Note `threefiveresult.txt` at the repo root is a prior decode capture. |
| Hex or another representation | Replace the `toBase64` call. The tag name still says SCTE35/base64, so rename the macros in `playlist.cpp:35,38` too, or you are lying in the manifest. |
| Quote the value | Swap `AttributeValueEnum` → `AttributeValueString` at **both** playlist.cpp:63 and :283, and delete the unquoted assertion in the test. Understand why before you do it. |
| Encode lazily / drop the cache | Move the call into `_addOatclsTag()` and the CONT branch. Measure first — the cache exists because serialisation is hot. |

Do **not** reach for `utils::toBase64` overloads taking `std::string`/`std::vector` here — the
section bytes are a raw `guint8*` with an explicit length and may contain NULs.

### 4.2 Add a new marker flavour

`adMarkers` is a contract across five layers. AGENTS.md requires all of them move together.

1. **Enum** — add to `hls::ScteMarker`,
   [include/utpl/hls/types.h:13](../../transcoder/lib/utpl/include/utpl/hls/types.h#L13), with a
   doc comment listing the tags it implies (match the existing style).
2. **Parser** — add the string branch in
   [parser.cpp:733](../../transcoder/app/src/application/config/parser/json/parser.cpp#L733).
   Unknown strings currently fall through to a `WARN`; keep that.
3. **Serialiser** — add a `const bool <flavour> = _hasMarker(scteMarkers, …);` alongside
   [playlist.cpp:100](../../transcoder/lib/utpl/src/utpl/hls/playlist.cpp#L100) and branch at the
   cue-out (:167) and cue-out-cont (:273) sites. Define tag-name macros in the block at
   playlist.cpp:28-38.
4. **OpenAPI** — extend the `Scte35AdMarker` enum and its description,
   [transcoder/config/doc/openapi.yaml:509](../../transcoder/config/doc/openapi.yaml#L509).
5. **Tests** — add a `serializeWith({YOURS})` case in
   [test/src/scte.cpp](../../transcoder/lib/utpl/test/src/scte.cpp), following
   `elementalFlavourIsUnchanged` / `enhancedFlavourCarriesTheSectionAsBase64`. Assert both what
   your flavour adds and what it must *not* add.

Then per the documentation rule: update `02-feature-catalog.md`, `05-configuration-reference.md`,
`26-implementation-index.md`, and this page.

If you implement `ADOBE`, also add an `adobe` branch next to `enhanced` in the `adMarkers` loop of
[parser.cpp](../../transcoder/app/src/application/config/parser/json/parser.cpp).

### 4.3 Change the config surface

`adMarkers` lives under each HLS output target. The layers to keep in sync:

| Layer | File |
| --- | --- |
| Parser | `transcoder/app/src/application/config/parser/json/parser.cpp:733` |
| Parameter class | `transcoder/lib/utpl/include/utpl/transcoder/output/parameters.h:338` (`getAdMarkers`/`addAdMarker`) |
| Parameter bridge | `transcoder/lib/utpl/src/utpl/transcoder/output/utils.cpp:20` |
| Streamer parameters | `transcoder/lib/utpl/include/utpl/hls/streamer.h:72,110,126` |
| OpenAPI schema | `transcoder/config/doc/openapi.yaml:271` and `:509` |
| Examples | `config/config.json:47`, `transcoder/config/new_example/srt_to_hls_out.json:41` |

Current shape:

```json
"hls": {
    "segmentLength": 4,
    "vodMode": false,
    "adMarkers": ["enhanced"]
}
```

Note `addAdMarker` appends to a `std::vector` with no de-duplication and no ordering guarantee.
`["elemental","enhanced"]` behaves identically to `["enhanced"]`, since only `_hasMarker(ENHANCED)`
is consulted and the Elemental tags are unconditional. If you want mutually exclusive flavours,
that has to become a validation rule in the parser — there is none today.

Beware the empty-list gate (§2.1). If you ever want "parse markers but emit nothing", or "emit
tags without configuring markers", you must change `Streamer::putScteData()`
([streamer.cpp:361](../../transcoder/lib/utpl/src/utpl/hls/streamer.cpp#L361)) — not the
serialiser.

---

## 5. Validating a change

The unit tests are the fast loop. `test/src/scte.cpp` is wired into the utpl test target at
[test/CMakeLists.txt:29](../../transcoder/lib/utpl/test/CMakeLists.txt#L29). It builds a
`FakeSegment` and a real `GstMpegtsSection` from a known base64 splice_insert, so it exercises the
genuine packetize → encode → serialise path with no pipeline.

```bash
# in the utpl/devel container, from a disposable build dir
cmake -S transcoder -B /tmp/build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/build -j"$(nproc)"
/tmp/build/lib/utpl/test/<test-binary> --gtest_filter='hlsScteMarkers.*'
```

Per AGENTS.md, a green unit test is not sufficient for a media change. Also:

- Run a real config with `"adMarkers": ["enhanced"]` and inspect the emitted playlist under
  `runtime/hls/`. The `variant_0.m3u8` currently checked in there was produced without any avail,
  so it carries no cue tags — it is a shape reference, not an example of this feature.
- Round-trip the emitted base64 back through a SCTE-35 decoder (`threefive`, TSDuck) and confirm
  it reproduces the section the transcoder acted on, splice event ID and duration included.
- Confirm the value is unwrapped, unquoted, and on one line.
- Check the continuation tags across the whole break, not just the first segment: the
  `attachContTag` logic at [playlist.cpp:199-266](../../transcoder/lib/utpl/src/utpl/hls/playlist.cpp#L199-L266)
  suppresses the CONT tag once a matching cue-in has been seen, matching splice event IDs for
  `splice_insert` and segmentation event IDs for `time_signal`.

## 6. Preconditions a cue tag depends on

A correctly parsed avail is not sufficient. Two things must also hold, and when they do not
the tag is simply absent:

**The event must have a resolvable splice time.** `ScteEvent::start` comes from
`Scte35Data::getSpliceTime()`, which reads the running-time map — key `"splice-time"` for
`time_signal`, `"event-<id>-splice-time"` for `splice_insert`. **Different keys, so one
command type can resolve while the other does not.** When the key is absent the call returns
`GST_CLOCK_TIME_NONE`; the event then matches no segment and `start + duration` overflows.

This used to be silent. It is now guarded in three places:

- `Streamer::putScteData()` rejects such an event with a `WARN` naming the command type
  rather than storing one that can never be placed;
- the acquisition path falls back to `scheduler::buildRunningTimeMap(section, runningTime)`
  when the demuxer's map is unusable
  ([input/single.cpp](../../transcoder/lib/utpl/src/utpl/transcoder/input/single.cpp));
- `MediaPlaylist::serialize()` skips an unplaceable event instead of overflowing.

Regression test: `hlsScteMarkers.unplaceableTimeSignalIsSkippedNotSilentlyMisplaced`.

**The splice must land near a segment boundary.** A cue tag is emitted only when the event
start is within `segmentDuration / 4` of a segment start (1 s for 4 s segments) —
[playlist.cpp:158](../../transcoder/lib/utpl/src/utpl/hls/playlist.cpp#L158). In practice
`HlsOutputSink::putScteData()` forces a split at the splice point, so this holds; but an
event that reaches the streamer without that split will not produce a tag.

## 7. Tag order

Cue tags are emitted **between `EXT-X-PROGRAM-DATE-TIME` and `EXTINF`**, never after it:
`#EXTINF` must be the last tag before the segment URI it describes. `EXTINF` also carries its
mandatory trailing comma (`#EXTINF:4.000,`) per RFC 8216 4.3.2.1. Verified live:

```
#EXT-X-PROGRAM-DATE-TIME:2026-08-29T04:37:27Z
#EXT-OATCLS-SCTE35:/DA/AAAAAAAAAP/wBQbyACQv2QAp...
#EXT-X-CUE-OUT:30.000
#EXTINF:3.720,
variant_0_section_7.ts
```

Pinned by `hlsScteMarkers.cueTagsPrecedeExtinfWhichPrecedesTheUri` and
`hlsScteMarkers.extinfCarriesItsMandatoryComma`.

## 8. Known limits

- `ADOBE` is declared but not serialised and has no parser branch: `adobe` warns as unsupported.
- The `ENHANCED` doc comment in `types.h` mentions an `ASSET` tag carrying CAID/UPID. **That is not
  implemented** — only `EXT-OATCLS-SCTE35` and `SCTE35=` are written. Treat the comment as a
  description of the upstream convention, not of this code.
- Only `splice_insert` and `time_signal` reach the playlist; anything else is dropped with a
  `WARN` at streamer.cpp:367.
- Markers require `ScteOutputMode::PASSTHROUGH` and `Container::MPEGTS`.
