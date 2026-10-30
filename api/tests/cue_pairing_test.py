"""Auto-return and cue-in rules for splice_insert and time_signal.

The product rules these pin:

  splice_insert, auto_return true   -> break closes itself; a manual cue-in is refused
  splice_insert, auto_return false  -> break stays open; a manual cue-in is required
  time_signal                       -> no auto-return field, no manual cue-in at all;
                                       the break closes on its segmentation_duration

The open-break lookup reads the `events` table rather than any dedicated state: end_time is
already time + duration for a splice_insert, so the schedule is the record.
"""

from __future__ import annotations

import pytest

BREAK_MS = 30_000
EVENT_ID = 1001


def _cue_out(auto_return=None, splice_event_id=EVENT_ID, duration=BREAK_MS):
    details = {"duration": duration, "splice_event_id": splice_event_id}
    if auto_return is not None:
        details["auto_return"] = auto_return
    return {"type": "scte35_spliceinsert", "time": None,
            "details": {"scte35_spliceinsert": details}}


def _cue_in(splice_event_id=EVENT_ID):
    return {"type": "scte35_returntonetwork", "time": None,
            "details": {"scte35_returntonetwork": {"splice_event_id": splice_event_id}}}


def _time_signal(type_id, duration=2_700_000, event_id=EVENT_ID):
    desc = {
        "segmentation_cancel_indicator": "SEGMENTATION_EVENT_NOT_CANCELED",
        "segmentation_event_id": event_id,
        "segmentation_type_id": type_id,
    }
    if duration is not None:
        desc["segmentation_duration"] = duration
    return {"type": "scte35_timesignal", "time": None,
            "details": {"scte35_timesignal": {"scte35_descriptors": [desc]}}}


def _post(client, event):
    return client.post("/scheduled_events", json=[event])


# --- auto_return field ------------------------------------------------------------

def test_auto_return_defaults_to_true_when_omitted(client):
    r = _post(client, _cue_out())
    assert r.status_code == 200, r.text

    stored = r.json()[0]["details"]["scte35_spliceinsert"]
    assert stored["auto_return"] is True


def test_auto_return_false_is_preserved(client):
    r = _post(client, _cue_out(auto_return=False))
    assert r.status_code == 200, r.text
    assert r.json()[0]["details"]["scte35_spliceinsert"]["auto_return"] is False


# --- manual cue-in pairing --------------------------------------------------------

def test_manual_cue_in_is_refused_for_an_auto_returning_break(client):
    assert _post(client, _cue_out(auto_return=True)).status_code == 200

    r = _post(client, _cue_in())
    assert r.status_code == 400, r.text
    assert "auto_return" in r.text


def test_manual_cue_in_is_refused_when_auto_return_was_omitted(client):
    """Omitted means true, so the same refusal must apply."""
    assert _post(client, _cue_out()).status_code == 200

    assert _post(client, _cue_in()).status_code == 400


def test_manual_cue_in_is_accepted_for_a_manual_break(client):
    assert _post(client, _cue_out(auto_return=False)).status_code == 200

    r = _post(client, _cue_in())
    assert r.status_code == 200, r.text


def test_manual_cue_in_is_refused_when_no_break_is_open(client):
    r = _post(client, _cue_in(splice_event_id=4242))
    assert r.status_code == 400, r.text
    assert "no open break" in r.text


def test_cue_in_matches_on_the_splice_event_id(client):
    assert _post(client, _cue_out(auto_return=False, splice_event_id=7001)).status_code == 200

    # A different id has no open break of its own.
    assert _post(client, _cue_in(splice_event_id=7002)).status_code == 400
    assert _post(client, _cue_in(splice_event_id=7001)).status_code == 200


# --- time_signal ------------------------------------------------------------------

@pytest.mark.parametrize("type_id", [0x23, 0x31, 0x33, 0x35, 0x37, 0x39, 0x3B])
def test_time_signal_break_end_descriptors_are_refused(client, type_id):
    r = _post(client, _time_signal(type_id))
    assert r.status_code == 400, r.text
    assert "duration" in r.text


def test_time_signal_break_start_requires_a_duration(client):
    r = _post(client, _time_signal(0x34, duration=None))
    assert r.status_code == 400, r.text


def test_time_signal_break_start_with_a_duration_is_accepted(client):
    r = _post(client, _time_signal(0x34))
    assert r.status_code == 200, r.text


# --- UPID on a splice_insert ------------------------------------------------------
#
# SCTE-35 carries descriptors on the splice_info_section rather than on the command, so a
# splice_insert can carry a segmentation_descriptor exactly as a time_signal does. The
# builder emits one only when a UPID is supplied.

def test_splice_insert_accepts_an_ascii_upid(client):
    event = _cue_out(auto_return=False)
    event["details"]["scte35_spliceinsert"]["segmentation_upid"] = "PROMO-1234"

    r = _post(client, event)
    assert r.status_code == 200, r.text

    stored = r.json()[0]["details"]["scte35_spliceinsert"]
    assert stored["segmentation_upid"] == "PROMO-1234"
    # Defaults chosen so decoders print the value whole and the cue-out reads as a break start.
    assert stored["segmentation_upid_type"] == 15
    assert stored["segmentation_type_id"] == 52


def test_splice_insert_upid_defaults_to_absent(client):
    r = _post(client, _cue_out())
    assert r.status_code == 200, r.text

    assert r.json()[0]["details"]["scte35_spliceinsert"]["segmentation_upid"] is None


def test_splice_insert_rejects_a_non_ascii_upid(client):
    event = _cue_out()
    event["details"]["scte35_spliceinsert"]["segmentation_upid"] = "caf\u00e9"

    r = _post(client, event)
    assert r.status_code == 400, r.text
    assert "ASCII" in r.text


def test_splice_insert_upid_type_is_overridable(client):
    event = _cue_out()
    event["details"]["scte35_spliceinsert"]["segmentation_upid"] = "PROMO-1234"
    event["details"]["scte35_spliceinsert"]["segmentation_upid_type"] = 12

    r = _post(client, event)
    assert r.status_code == 200, r.text
    assert r.json()[0]["details"]["scte35_spliceinsert"]["segmentation_upid_type"] == 12
