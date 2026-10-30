"""Validation of pre-authored SCTE-35 sections supplied as base64 on schedule events.

The fixtures below are complete splice_info_sections built to SCTE 35 section 9.2 and
cross-checked against an independent SCTE-35 decoder; their CRC_32 values are valid.
"""

from __future__ import annotations

import pytest

from transcoder_api.exceptions import InvalidRequestException
from transcoder_api.model.rest_api import EventType
from transcoder_api.services.schedule import validate_scte35_base64

# splice_insert, out_of_network_indicator set, event 12345, 30s auto-return break.
SPLICE_INSERT_OUT = "/DAlAAAAAAAAAP/wFAUAADA5f+/+cr0AUP4AKTLgAAAAAAAAa/2GtQ=="

# The matching return to network: same event id, out_of_network_indicator clear.
SPLICE_INSERT_IN = "/DAgAAAAAAAAAP/wDwUAADA5f0/+cr0AUAAAAAAAAEFvN+0="

# time_signal carrying a Provider Placement Opportunity Start (0x34) with a UPID.
TIME_SIGNAL = (
    "/DA/AAAAAAAAAP/wBQb+cr0AUAApAidDVUVJqrvM3X/fAABSZcAMEVNLQU5ESEEt"
    "VEVTVC1VUElENAEBAADYWc/1"
)

# splice_null: a well-formed section carrying a command that cannot be injected.
SPLICE_NULL = "/DARAAAAAAAAAP/wAAAAAHpPv/8="


@pytest.mark.parametrize(
    "blob,event_type",
    [
        (SPLICE_INSERT_OUT, EventType.scte35_spliceinsert),
        (SPLICE_INSERT_IN, EventType.scte35_returntonetwork),
        (TIME_SIGNAL, EventType.scte35_timesignal),
    ],
)
def test_accepts_matching_command(blob, event_type):
    decoded = validate_scte35_base64(blob, event_type)

    assert decoded[0] == 0xFC
    assert len(decoded) == ((decoded[1] & 0x0F) << 8 | decoded[2]) + 3


def test_rejects_command_type_mismatch():
    with pytest.raises(InvalidRequestException) as excinfo:
        validate_scte35_base64(TIME_SIGNAL, EventType.scte35_spliceinsert)

    assert "time_signal" in excinfo.value.message
    assert "splice_insert" in excinfo.value.message


def test_rejects_uninjectable_command():
    with pytest.raises(InvalidRequestException) as excinfo:
        validate_scte35_base64(SPLICE_NULL, EventType.scte35_spliceinsert)

    assert "splice_null" in excinfo.value.message


def test_rejects_break_start_as_return_to_network():
    with pytest.raises(InvalidRequestException) as excinfo:
        validate_scte35_base64(SPLICE_INSERT_OUT, EventType.scte35_returntonetwork)

    assert "out_of_network_indicator" in excinfo.value.message


def test_rejects_return_to_network_as_break_start():
    with pytest.raises(InvalidRequestException) as excinfo:
        validate_scte35_base64(SPLICE_INSERT_IN, EventType.scte35_spliceinsert)

    assert "out_of_network_indicator" in excinfo.value.message


@pytest.mark.parametrize(
    "blob,expected",
    [
        ("not!base64!", "not valid base64"),
        ("/DA=", "too short"),
        ("AAAAAAAAAAAAAAAAAAAA", "table_id"),
        # Same section as SPLICE_INSERT_OUT with the tail lopped off.
        ("/DAlAAAAAAAAAP/wFAUAADA5f+/+cr0AUP4AKTLg", "section_length"),
    ],
)
def test_rejects_malformed_sections(blob, expected):
    with pytest.raises(InvalidRequestException) as excinfo:
        validate_scte35_base64(blob, EventType.scte35_spliceinsert)

    assert expected in excinfo.value.message
