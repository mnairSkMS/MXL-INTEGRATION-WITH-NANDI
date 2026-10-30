"""UPID handling on schedule events.

`segmentation_upid` is literal ASCII text. Its characters are the UPID bytes: nothing is
hex-decoded at any layer, and the value is stored and handed to the engine verbatim.

Two behaviours are pinned. First, every descriptor is validated, not only cancelled ones -
the check used to sit inside the SEGMENTATION_EVENT_CANCELED branch, so a bad UPID on a
normal avail was accepted with a success code and then discarded by the engine, losing the
ad break with nothing logged. Second, hex is not interpreted: a hex-looking string means
itself.
"""

from __future__ import annotations

import pytest

from transcoder_api.exceptions import InvalidRequestException
from transcoder_api.model.rest_api import (
    SCTE35SegmentationDescriptor,
    SegmentationCancelIndicator,
)
from transcoder_api.services.schedule import validate_segmentation_descriptor

ASCII_UPID = "ExampleUPID123"


def _descriptor(**kwargs) -> SCTE35SegmentationDescriptor:
    base = dict(
        segmentation_cancel_indicator=(
            SegmentationCancelIndicator.SEGMENTATION_EVENT_NOT_CANCELED
        ),
        segmentation_event_id=1001,
        segmentation_type_id=0x34,
        segmentation_duration=900000,
        segmentation_upid_type=15,
    )
    base.update(kwargs)
    return SCTE35SegmentationDescriptor(**base)


def test_ascii_text_is_stored_verbatim():
    d = _descriptor(segmentation_upid=ASCII_UPID)
    validate_segmentation_descriptor(d)

    assert d.segmentation_upid == ASCII_UPID


def test_a_hex_looking_string_is_taken_literally_not_decoded():
    """The breaking change: "4578616D" is now those eight characters, not four bytes."""
    d = _descriptor(segmentation_upid="4578616D")
    validate_segmentation_descriptor(d)

    assert d.segmentation_upid == "4578616D"


def test_a_0x_prefix_is_not_stripped():
    d = _descriptor(segmentation_upid="0x1234")
    validate_segmentation_descriptor(d)

    assert d.segmentation_upid == "0x1234"


def test_non_ascii_is_rejected_rather_than_silently_re_encoded():
    d = _descriptor(segmentation_upid="café")

    with pytest.raises(InvalidRequestException):
        validate_segmentation_descriptor(d)


def test_validation_runs_for_not_cancelled_descriptors_too():
    """Regression: the check used to run only on SEGMENTATION_EVENT_CANCELED."""
    d = _descriptor(segmentation_upid="prix fixe €")

    with pytest.raises(InvalidRequestException):
        validate_segmentation_descriptor(d)


def test_empty_upid_stays_empty():
    d = _descriptor(segmentation_upid=None)
    validate_segmentation_descriptor(d)

    assert d.segmentation_upid is None
