# SCTE-35 `segmentation_type_id` reference (Table 23)

Transcribed verbatim from **ANSI/SCTE 35 2023r1, Table 23 — segmentation_type_id**
(pages 76-79 of [`doc/reference/ANSI-SCTE_35_2023r1.pdf`](../reference/ANSI-SCTE_35_2023r1.pdf)), plus the surrounding
normative text for the fields the table constrains.

## Why this file exists

`segmentation_type_id` is the byte that tells a downstream packager *what kind of
boundary* a `segmentation_descriptor` marks. In this codebase it appears in:

- `api/api.yaml` — `segmentation_type_id` on both `SCTE35TimeSignal` and
  `SCTE35SpliceInsert` (default `52` = `0x34`, Provider Placement Opportunity Start).
- `api/src/transcoder_api/services/schedule.py` — the
  `SEGMENTATION_TYPE_IDS_BREAK_START` / `_BREAK_END` sets used for cue pairing.
- `transcoder/lib/utpl/include/utpl/hls/types.h` — the switch that decides whether a
  descriptor opens (`EXT-X-CUE-OUT`) or closes (`EXT-X-CUE-IN`) an HLS break.
- `api/src/transcoder_api/static/operator.html` / `operator.js` — the SSAI panel reads this
  table twice. `SEGMENTATION_TYPES` (id and name) backs the **Segmentation Message**
  combobox, which filters as you type and shows at most five matches; the italicised rows
  below — Private and the reserved ranges — are deliberately absent from it, reachable by
  typing the number instead. `SEGMENTATION_TABLE_ROWS` is every row and every column, and is
  what the **(i)** beside that field renders as an on-page reference.

`api/tests/segmentation_type_id_test.py` compares both constants against this file — names
against `SEGMENTATION_TYPES`, all six columns cell-by-cell against `SEGMENTATION_TABLE_ROWS`
— and against the id sets in `schedule.py`. It also asserts the table covers all 256 values
with no gap. The copies therefore cannot drift apart silently.

Editing the table below is how you change what the operator page offers. Keep the column
count, the message names and the `0xNN (dd)` format exactly as they are — the drift test
parses them, and the reference panel is a transcription of these rows.

The **end**-type ids relevant to HLS cue-in are `0x23, 0x31, 0x33, 0x35, 0x37, 0x39, 0x3B`;
their matching **start** ids are one lower (`0x22, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A`).
As a rule of thumb across the 0x30-0x47 block, **even = start, odd = end**.

## Normative text preceding the table

> **segmentation_upid_length** — There is no entry in the XML schema for
> `segmentation_upid_length`. The value **shall** be derived when converting an XML
> representation of the SegmentationUpid to Bit Stream Format. In the case of UPID type
> MID(), this reflects the total length of nested UPID types structure (see Section
> 10.3.3.4). If there are no SegmentationUpid elements in an XML representation,
> `segmentation_upid_length` **shall** be set to zero in the Bit Stream Format.

> **segmentation_upid()** — Length and identification from Table 22. This structure's
> contents and length are determined by the `segmentation_upid_type` and
> `segmentation_upid_length` fields. An example would be a type of `0x06` for ISAN and a
> length of 12 bytes. This field would then contain the ISAN identifier for the content to
> which this descriptor refers.
>
> SegmentationUpid [Optional, xsd:SegmentationUpidType] Zero, one or more SegmentationUpid
> Elements *may* be specified. If multiple SegmentationUpid Elements are present in an XML
> representation, the MID() structure **shall** be generated per Section 10.3.3.4. See
> Section 10.3.3.12 for additional details on SegmentationUpidType.

> **segmentation_type_id** — The 8-bit value **shall** contain one of the values in Table 23
> to designate type of segmentation. All unused values are reserved. When the
> `segmentation_type_id` is `0x01` (Content Identification), the value of
> `segmentation_upid_type` **shall** be non-zero. If `segmentation_upid_length` is zero,
> then `segmentation_type_id` **shall** be set to `0x00` for Not Indicated.

## Table 23 — segmentation_type_id

`segmentation_type_id` is given as Hex (Decimal). Blank cells are blank in the source.

| Segmentation Message | segmentation_type_id | segment_num | segments_expected | sub_segment_num | sub_segments_expected |
| --- | --- | --- | --- | --- | --- |
| Not Indicated | 0x00 (00) | 0 | 0 | Not used | Not Used |
| Content Identification | 0x01 (01) | 0 | 0 | Not used | Not Used |
| *Private* | 0x02 (02) | | | | |
| *Reserved* | 0x03-0x0F | | | | |
| Program Start | 0x10 (16) | 1 | 1 | Not used | Not Used |
| Program End | 0x11 (17) | 1 | 1 | Not used | Not Used |
| Program Early Termination | 0x12 (18) | 1 | 1 | Not used | Not Used |
| Program Breakaway | 0x13 (19) | 1 | 1 | Not used | Not Used |
| Program Resumption | 0x14 (20) | 1 | 1 | Not used | Not Used |
| Program Runover Planned | 0x15 (21) | 1 | 1 | Not used | Not Used |
| Program Runover Unplanned | 0x16 (22) | 1 | 1 | Not used | Not Used |
| Program Overlap Start | 0x17 (23) | 1 | 1 | Not used | Not Used |
| Program Blackout Override | 0x18 (24) | 0 | 0 | Not used | Not Used |
| Program Join | 0x19 (25) | 1 | 1 | Not used | Not used |
| Program Immediate Resumption | 0x1A (26) | 0 | 0 | Not used | Not used |
| *Reserved* | 0x1B-0x1F | | | | |
| Chapter Start | 0x20 (32) | Non-zero | Non-zero | Not used | Not Used |
| Chapter End | 0x21 (33) | Non-zero | Non-zero | Not used | Not Used |
| Break Start | 0x22 (34) | 0 or Non-zero | 0 or Non-zero | Not used | Not used |
| Break End | 0x23 (35) | 0 or Non-zero | 0 or Non-zero | Not used | Not used |
| Opening Credit Start *(deprecated)* | 0x24 (36) | 1 | 1 | Not used | Not used |
| Opening Credit End *(deprecated)* | 0x25 (37) | 1 | 1 | Not used | Not used |
| Closing Credit Start *(deprecated)* | 0x26 (38) | 1 | 1 | Not used | Not used |
| Closing Credit End *(deprecated)* | 0x27 (39) | 1 | 1 | Not used | Not used |
| *Reserved* | 0x28-0x2F | | | | |
| Provider Advertisement Start | 0x30 (48) | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero |
| Provider Advertisement End | 0x31 (49) | 0 or Non-zero | 0 or Non-zero | Not used | Not Used |
| Distributor Advertisement Start | 0x32 (50) | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero |
| Distributor Advertisement End | 0x33 (51) | 0 or Non-zero | 0 or Non-zero | Not used | Not Used |
| Provider Placement Opportunity Start | 0x34 (52) | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero |
| Provider Placement Opportunity End | 0x35 (53) | 0 or Non-zero | 0 or Non-zero | Not used | Not Used |
| Distributor Placement Opportunity Start | 0x36 (54) | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero |
| Distributor Placement Opportunity End | 0x37 (55) | 0 or Non-zero | 0 or Non-zero | Not used | Not Used |
| Provider Overlay Placement Opportunity Start | 0x38 (56) | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero |
| Provider Overlay Placement Opportunity End | 0x39 (57) | 0 or Non-zero | 0 or Non-zero | Not used | Not Used |
| Distributor Overlay Placement Opportunity Start | 0x3A (58) | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero |
| Distributor Overlay Placement Opportunity End | 0x3B (59) | 0 or Non-zero | 0 or Non-zero | Not used | Not Used |
| Provider Promo Start | 0x3C (60) | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero |
| Provider Promo End | 0x3D (61) | 0 or Non-zero | 0 or Non-zero | Not used | Not used |
| Distributor Promo Start | 0x3E (62) | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero |
| Distributor Promo End | 0x3F (63) | 0 or Non-zero | 0 or Non-zero | Not used | Not used |
| Unscheduled Event Start | 0x40 (64) | 0 | 0 | Not used | Not Used |
| Unscheduled Event End | 0x41 (65) | 0 | 0 | Not used | Not Used |
| Alternate Content Opportunity Start | 0x42 (66) | 0 or Non-zero | 0 or Non-zero | Not used | Not Used |
| Alternate Content Opportunity End | 0x43 (67) | 0 or Non-zero | 0 or Non-zero | Not used | Not Used |
| Provider Ad Block Start | 0x44 (68) | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero |
| Provider Ad Block End | 0x45 (69) | 0 or Non-zero | 0 or Non-zero | Not used | Not Used |
| Distributor Ad Block Start | 0x46 (70) | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero | 0 or Non-zero |
| Distributor Ad Block End | 0x47 (71) | 0 or Non-zero | 0 or Non-zero | Not used | Not Used |
| *Reserved* | 0x48-0x4F | | | | |
| Network Start | 0x50 (80) | 0 | 0 | Not used | Not Used |
| Network End | 0x51 (81) | 0 | 0 | Not used | Not Used |
| *Reserved* | 0x52-0xFF | | | | |

### Notes (from the standard)

1. Only one Program Overlap Start is allowed to be active at a time. A Program End
   **shall** occur before a subsequent Program Overlap Start can occur.
2. See [SCTE 223] for further usage of `segmentation_type_id`.
3. The opening credit and closing credit start/end types are *deprecated*. New
   implementations *should* use the Segment Content Reference for this purpose.

## Field definitions for the table's columns

**segment_num** — This field provides support for numbering segments within a given
collection of Segments (such as Chapters or Advertisements). This value, when utilized, is
expected to reset to one at the beginning of a collection of Segments. This field is
expected to increment for each new Segment (such as a Chapter). The value of this field
**shall** be as indicated in Table 23.
`@segmentNum [Optional, xsd:unsignedByte]`

**segments_expected** — This field provides a count of the expected number of individual
Segments (such as Chapters) within a collection of Segments. The value of this field
**shall** be as indicated in Table 23.
`@segmentsExpected [Optional, xsd:unsignedByte]`

**sub_segment_num** — `sub_segment_num` is optional for the applicable `segment_type_id`
values in Table 23. If specified, this field provides identification for a specific
sub-Segment within a collection of sub-Segments. This value, when utilized, is expected to
be set to one for the first sub-Segment within a collection of sub-Segments. This field is
expected to increment by one for each new sub-Segment within a given collection. If
present, `descriptor_length` **shall** include `sub_segment_num` in the byte count and
serve as an indication to an implementation that `sub_segment_num` is present in the
descriptor. The value of this field **shall** be as indicated in Table 23. Any other usage
of `sub_segment_num` beyond that defined in Table 23 is out of scope of this standard.
`@subSegmentNum [Optional, xsd:unsignedByte]`

**sub_segments_expected** — `sub_segments_expected` **shall** be present if
`sub_segment_num` is present. If specified, this field provides a count of the expected
number of individual sub-Segments within the collection of sub-Segments. If present,
`descriptor_length` **shall** include `sub_segments_expected` in the byte count and serve
as an indication to an implementation that `sub_segments_expected` is present in the
descriptor. The value of this field **shall** be as indicated in Table 23. Any other usage
of `sub_segments_expected` beyond that defined in Table 23 is out of scope of this
standard.
`@subSegmentsExpected [Optional, xsd:unsignedByte]`
