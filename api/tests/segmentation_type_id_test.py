"""segmentation_type_id: the server rule, and the three copies of SCTE 35 Table 23.

The operator page lets an operator choose the segmentation message rather than always
sending 0x34, so the table now exists in three places that must agree:

  doc/codebase/38-scte35-segmentation-type-id-table.md  - the transcription of Table 23
  services/schedule.py                                  - the break start/end id sets
  static/operator.js                                    - the dropdown and the same two sets

A fourth copy lives in the C++ HLS serialiser (include/utpl/hls/types.h) and is out of
reach from here; schedule.py's comment is what ties it in.

The behavioural rule pinned here: a splice_insert opens an avail, so a segmentation_type_id
that closes one is refused. scte35_returntonetwork is what closes the break.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest

import transcoder_api
from transcoder_api.services.schedule import (
    SEGMENTATION_TYPE_IDS_BREAK_END,
    SEGMENTATION_TYPE_IDS_BREAK_START,
)

BREAK_MS = 30_000
EVENT_ID = 2001
UPID = "PROMO-1234"

OPERATOR_JS = Path(transcoder_api.__file__).parent / "static" / "operator.js"

# Only present when the tests run against a checkout. The image build copies api/ alone, so
# the doc comparison skips there rather than failing on a missing file.
DOC_TABLE = (
    Path(transcoder_api.__file__).resolve().parents[3]
    / "doc"
    / "codebase"
    / "38-scte35-segmentation-type-id-table.md"
)


def _cue_out(type_id=None, upid=UPID, splice_event_id=EVENT_ID):
    details = {"duration": BREAK_MS, "splice_event_id": splice_event_id}
    if upid is not None:
        details["segmentation_upid"] = upid
    if type_id is not None:
        details["segmentation_type_id"] = type_id
    return {"type": "scte35_spliceinsert", "time": None,
            "details": {"scte35_spliceinsert": details}}


def _post(client, event):
    return client.post("/scheduled_events", json=[event])


# --- the server rule --------------------------------------------------------------

@pytest.mark.parametrize("type_id", sorted(SEGMENTATION_TYPE_IDS_BREAK_END))
def test_splice_insert_refuses_a_break_end_type_id(client, type_id):
    r = _post(client, _cue_out(type_id=type_id))

    assert r.status_code == 400, r.text
    assert "closes an avail" in r.text


@pytest.mark.parametrize("type_id", sorted(SEGMENTATION_TYPE_IDS_BREAK_START))
def test_splice_insert_accepts_every_break_start_type_id(client, type_id):
    r = _post(client, _cue_out(type_id=type_id, splice_event_id=EVENT_ID + type_id))

    assert r.status_code == 200, r.text


def test_splice_insert_type_id_defaults_to_provider_placement_opportunity_start(client):
    r = _post(client, _cue_out())

    assert r.status_code == 200, r.text
    stored = client.get("/scheduled_events").json()
    assert stored[-1]["details"]["scte35_spliceinsert"]["segmentation_type_id"] == 0x34


def test_splice_insert_keeps_a_reserved_type_id_verbatim(client):
    # 0x2A has no Table 23 entry. The UI labels it "Reserved" but still sends it; the server
    # is not in the business of second-guessing which reserved values an operator may use.
    r = _post(client, _cue_out(type_id=0x2A))

    assert r.status_code == 200, r.text
    stored = client.get("/scheduled_events").json()
    assert stored[-1]["details"]["scte35_spliceinsert"]["segmentation_type_id"] == 0x2A


# --- the three copies of Table 23 -------------------------------------------------

def _js_id_list(name: str) -> set[int]:
    source = OPERATOR_JS.read_text(encoding="utf-8")
    match = re.search(rf"const {name} = \[([^\]]*)\];", source)
    assert match, f"{name} not found in operator.js"
    return {int(value, 16) for value in re.findall(r"0x[0-9A-Fa-f]+", match.group(1))}


def _js_table() -> dict[int, str]:
    source = OPERATOR_JS.read_text(encoding="utf-8")
    match = re.search(r"const SEGMENTATION_TYPES = \[(.*?)\n  \];", source, re.DOTALL)
    assert match, "SEGMENTATION_TYPES not found in operator.js"
    rows = re.findall(r'\{id: (0x[0-9A-Fa-f]+), name: "([^"]+)"\}', match.group(1))
    assert rows, "SEGMENTATION_TYPES parsed empty"
    return {int(id_, 16): name for id_, name in rows}


def _doc_table() -> dict[int, str]:
    rows = {}
    for line in DOC_TABLE.read_text(encoding="utf-8").splitlines():
        # Concrete rows only: the reserved ranges are written "0x03-0x0F" with no decimal.
        match = re.match(r"\| (.+?) \| 0x([0-9A-F]{2}) \(\d+\) \|", line)
        if match:
            rows[int(match.group(2), 16)] = match.group(1)
    assert rows, "no Table 23 rows parsed from the doc"
    return rows


def _doc_rows() -> list[list[str]]:
    """Every Table 23 row as six display cells, reserved ranges included."""
    rows = []
    for line in DOC_TABLE.read_text(encoding="utf-8").splitlines():
        if not line.startswith("| "):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) != 6:
            continue
        if cells[0] == "Segmentation Message" or set(cells[1]) <= set("-"):
            continue
        rows.append(cells)
    assert rows, "no Table 23 rows parsed from the doc"
    return rows


def _js_rows() -> list[list[str]]:
    source = OPERATOR_JS.read_text(encoding="utf-8")
    match = re.search(r"const SEGMENTATION_TABLE_ROWS = \[(.*?)\n  \];", source, re.DOTALL)
    assert match, "SEGMENTATION_TABLE_ROWS not found in operator.js"
    rows = [
        [cell for cell in re.findall(r'"((?:[^"\\]|\\.)*)"', line)]
        for line in match.group(1).splitlines()
        if line.strip().startswith("[")
    ]
    assert rows, "SEGMENTATION_TABLE_ROWS parsed empty"
    return rows


def _normalise(name: str) -> str:
    return " ".join(name.replace("*", "").split()).casefold()


def test_operator_js_break_end_ids_match_the_server():
    assert _js_id_list("SEGMENTATION_TYPE_IDS_BREAK_END") == set(
        SEGMENTATION_TYPE_IDS_BREAK_END
    )


def test_operator_js_break_start_ids_match_the_server():
    assert _js_id_list("SEGMENTATION_TYPE_IDS_BREAK_START") == set(
        SEGMENTATION_TYPE_IDS_BREAK_START
    )


def test_every_break_id_the_server_knows_is_offered_in_the_dropdown():
    offered = set(_js_table())
    missing = (
        set(SEGMENTATION_TYPE_IDS_BREAK_START) | set(SEGMENTATION_TYPE_IDS_BREAK_END)
    ) - offered

    assert not missing, f"ids the server rules on but the UI cannot select: {sorted(missing)}"


def test_operator_js_table_matches_the_transcribed_standard():
    if not DOC_TABLE.exists():
        pytest.skip(f"{DOC_TABLE} not present; runs against a checkout only")

    js, doc = _js_table(), _doc_table()

    # 0x02 is Private: a real Table 23 row, but not a segmentation message an operator picks
    # from a list. The UI reaches it by number and labels it via segmentationLabel().
    assert set(doc) - set(js) == {0x02}
    assert not set(js) - set(doc)

    mismatched = {
        id_: (js[id_], doc[id_])
        for id_ in js
        if _normalise(js[id_]) != _normalise(doc[id_])
    }
    assert not mismatched, f"names drifted from the standard: {mismatched}"


def test_the_reference_panel_reproduces_every_table_23_row():
    """The (i) panel beside Segmentation Message shows the whole table, not just the names.

    Its rows are generated from the markdown, so this compares them cell by cell rather than
    trusting the generation to have been re-run. The markdown formats a few names in italics
    (*Reserved*, *Private*) which carry no meaning in a rendered HTML table, so the asterisks
    are the one difference allowed.
    """
    if not DOC_TABLE.exists():
        pytest.skip(f"{DOC_TABLE} not present; runs against a checkout only")

    js, doc = _js_rows(), _doc_rows()

    assert len(js) == len(doc), f"{len(js)} rows in operator.js, {len(doc)} in the doc"

    for index, (js_row, doc_row) in enumerate(zip(js, doc)):
        assert len(js_row) == 6, f"row {index} has {len(js_row)} cells: {js_row}"
        expected = [cell.replace("*", "") for cell in doc_row]
        assert js_row == expected, f"row {index} drifted: {js_row} vs {expected}"


def test_the_reference_panel_covers_the_whole_byte():
    """No id between 0x00 and 0xFF may be missing from the panel."""
    covered: set[int] = set()
    for row in _js_rows():
        cell = row[1]
        if "-" in cell:
            low, high = cell.split("-")
            covered |= set(range(int(low, 16), int(high, 16) + 1))
        else:
            covered.add(int(cell.split()[0], 16))

    assert covered == set(range(256)), f"gaps: {sorted(set(range(256)) - covered)}"
