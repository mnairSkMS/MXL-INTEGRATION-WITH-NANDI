import base64
import binascii
import json
from datetime import datetime, timezone, timedelta
from typing import Optional, List

from sqlalchemy.orm import Session

from ..exceptions import InvalidRequestException
from ..model.db import Event, db_json_serialize
from ..model.rest_api import (
    EventList,
    Event as ApiEvent,
    EventId as ApiEventId,
    EventType,
    ErrorCode,
    Details,
    SCTE35SegmentationDescriptor,
    SegmentationCancelIndicator,
)


def normalize_upid(value: Optional[str]) -> Optional[str]:
    """
    Validates a UPID and stores it as the literal ASCII text it is.

    Hex input is deliberately not accepted, and the stored form is no longer hex either:
    the engine reads these characters as the UPID bytes directly. A hex string therefore
    now means itself - "4578616D" puts those eight characters on the wire, not the four
    bytes it used to denote.
    """
    if not value:
        return None

    # Raises UnicodeEncodeError on any non-ASCII codepoint, which is what we want: the
    # UPID is a byte string, and silently expanding to UTF-8 would change it.
    value.encode("ascii")

    return value


# segmentation_type_id values the HLS serialiser treats as closing an avail - the switch in
# include/utpl/hls/types.h. A time_signal break closes on its duration, never on an operator
# action, so scheduling one of these is rejected rather than silently producing a cue-in the
# product does not support.
SEGMENTATION_TYPE_IDS_BREAK_END = frozenset({0x23, 0x31, 0x33, 0x35, 0x37, 0x39, 0x3B})

# The matching start ids. A start with no duration would open a break nothing can close.
SEGMENTATION_TYPE_IDS_BREAK_START = frozenset({0x22, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A})


def validate_segmentation_descriptor(seg_desc: SCTE35SegmentationDescriptor) -> None:
    # Every descriptor is normalised, not only cancelled ones. This used to sit inside the
    # SEGMENTATION_EVENT_CANCELED branch below, so an undecodable UPID on a normal avail -
    # the overwhelmingly common case - was accepted with 201 and then thrown away by the
    # engine with nothing logged and the ad break silently lost.
    try:
        seg_desc.segmentation_upid = normalize_upid(seg_desc.segmentation_upid)
    except UnicodeEncodeError:
        raise InvalidRequestException(
            message="segmentation_upid must be ASCII text, or empty",
            code=ErrorCode.INVALID_REQUEST,
        )

    if (
        seg_desc.segmentation_cancel_indicator
        == SegmentationCancelIndicator.SEGMENTATION_EVENT_CANCELED
    ):
        if (
            seg_desc.segmentation_upid_type is None
            or seg_desc.segmentation_upid is None
            or seg_desc.segmentation_type_id is None
            or seg_desc.segment_num is None
            or seg_desc.segments_expected is None
        ):
            raise InvalidRequestException(
                message="if segmentation_cancel_indicator is set "
                "fields: segmentation_upid_type, segmentation_upid, segmentation_type_id,"
                "segment_num and segments_expected ar required ",
                code=ErrorCode.MISSING_FIELDS,
            )
        if seg_desc.segmentation_type_id in [0x34, 0x30, 0x32, 0x36, 0x38, 0x3A, 0x44, 0x46]:
            if seg_desc.sub_segment_num is None or seg_desc.sub_segments_expected is None:
                raise InvalidRequestException(
                    message="invalid segment cancellation params. "
                    f"For segmentation_type_id: 0x{seg_desc.segmentation_type_id:02x} "
                    "sub_segment_num and sub_segments_expected ar required",
                    code=ErrorCode.MISSING_FIELDS,
                )


# SCTE-35 splice_info_section, per SCTE 35 section 9.2. Offsets of the fixed header:
#   0      table_id (must be 0xFC)
#   1-2    section_syntax_indicator, private, sap_type, section_length (12 bits)
#   3      protocol_version
#   4      encrypted_packet flag (bit 7), encryption_algorithm, pts_adjustment MSB
#   9      cw_index
#   10-12  tier (12 bits), splice_command_length (12 bits)
#   13     splice_command_type
# and, for splice_insert, the command body that follows it:
#   14-17  splice_event_id
#   18     splice_event_cancel_indicator (bit 7)
#   19     out_of_network_indicator (bit 7), program_splice_flag, duration_flag, ...
SCTE35_TABLE_ID = 0xFC
SCTE35_HEADER_SIZE = 14
SCTE35_MAX_SECTION_SIZE = 4096

SCTE35_CMD_SPLICE_INSERT = 0x05
SCTE35_CMD_TIME_SIGNAL = 0x06

SCTE35_CMD_NAMES = {
    0x00: "splice_null",
    0x04: "splice_schedule",
    SCTE35_CMD_SPLICE_INSERT: "splice_insert",
    SCTE35_CMD_TIME_SIGNAL: "time_signal",
    0x07: "bandwidth_reservation",
    0xFF: "private_command",
}

# Which splice command each event type is allowed to carry.
SCTE35_EXPECTED_COMMAND = {
    EventType.scte35_spliceinsert: SCTE35_CMD_SPLICE_INSERT,
    EventType.scte35_returntonetwork: SCTE35_CMD_SPLICE_INSERT,
    EventType.scte35_timesignal: SCTE35_CMD_TIME_SIGNAL,
}


def _cmd_name(cmd: int) -> str:
    return SCTE35_CMD_NAMES.get(cmd, f"0x{cmd:02x}")


def validate_scte35_base64(blob: str, event_type: EventType) -> bytes:
    """Validate a base64-encoded SCTE-35 splice_info_section for the given event type.

    Rejects here rather than downstream: an unusable section that reaches the transcoder
    is silently dropped at injection time, so the operator would see a 200 and no marker
    in the output. Returns the decoded section.
    """

    def reject(message: str, code: ErrorCode = ErrorCode.INVALID_REQUEST) -> None:
        raise InvalidRequestException(message=f"scte35_base64: {message}", code=code)

    try:
        data = base64.b64decode(blob, validate=True)
    except (binascii.Error, ValueError):
        reject("not valid base64")

    if len(data) < SCTE35_HEADER_SIZE:
        reject(
            f"decodes to {len(data)} bytes, too short to be a splice_info_section "
            f"(minimum {SCTE35_HEADER_SIZE})"
        )

    if data[0] != SCTE35_TABLE_ID:
        reject(
            f"table_id is 0x{data[0]:02x}, expected 0x{SCTE35_TABLE_ID:02x} "
            "(this is not a splice_info_section)"
        )

    # section_length counts the bytes after itself, so the whole section is 3 bytes longer.
    section_length = ((data[1] & 0x0F) << 8) | data[2]
    total_length = section_length + 3

    if total_length != len(data):
        reject(
            f"section_length declares {total_length} bytes but {len(data)} were decoded"
        )

    if total_length > SCTE35_MAX_SECTION_SIZE:
        reject(f"section is {total_length} bytes, over the {SCTE35_MAX_SECTION_SIZE} byte limit")

    if data[4] & 0x80:
        reject("section is encrypted; encrypted sections cannot be re-timed for insertion")

    command = data[13]
    expected = SCTE35_EXPECTED_COMMAND[event_type]

    if command != expected:
        reject(
            f"carries a {_cmd_name(command)} command, but event type "
            f"{event_type.value} requires {_cmd_name(expected)}"
        )

    if command == SCTE35_CMD_SPLICE_INSERT:
        # splice_event_cancel_indicator sits directly after the 32-bit splice_event_id.
        if len(data) < 19:
            reject("splice_insert command is truncated")

        cancelled = bool(data[18] & 0x80)

        if not cancelled:
            if len(data) < 20:
                reject("splice_insert command is truncated")

            out_of_network = bool(data[19] & 0x80)

            if event_type == EventType.scte35_returntonetwork and out_of_network:
                reject(
                    "out_of_network_indicator is set, which starts a break rather than "
                    "returning to network; use scte35_spliceinsert for that"
                )

            if event_type == EventType.scte35_spliceinsert and not out_of_network:
                reject(
                    "out_of_network_indicator is clear, which returns to network rather "
                    "than starting a break; use scte35_returntonetwork for that"
                )

    return data


def validate_event(ev: ApiEvent, now: datetime | None = None) -> None:
    now = now or datetime.now(timezone.utc)

    if ev.time is None:
        ev.time = now

    elif not (now <= ev.time <= now + timedelta(days=14)):
        raise InvalidRequestException(
            message=f"Invalid time in event {ev.id}. {ev.time} must be"
            " now or in the future "
            "but not more than 14 days in the future.",
            code=ErrorCode.INVALID_TIME_RANGE,
        )
    if getattr(ev.details, ev.type.value) is None:
        raise InvalidRequestException(
            message=f"event {ev.id} type: {ev.type.value} dose not match {type(ev.details)}",
            code=ErrorCode.DETAILS_TYPE_MISMATCH,
        )
    if ev.type == EventType.squeeze:
        squeeze_details = ev.details.squeeze
        assert squeeze_details is not None
        if squeeze_details.squeeze_name is None and (
            squeeze_details.input_window_norm is None
            or squeeze_details.output_window_norm is None
        ):
            raise InvalidRequestException(
                message=f"event {ev.id} input_window_norm and output_window_norm "
                f"are required when squeeze_name is not provided",
                code=ErrorCode.MISSING_FIELDS,
            )
    elif ev.type == EventType.scte35_timesignal:
        time_signal = ev.details.scte35_timesignal
        assert time_signal is not None

        if time_signal.scte35_base64 is not None:
            validate_scte35_base64(time_signal.scte35_base64, ev.type)

        elif time_signal.scte35_descriptors:
            for seg_desc in time_signal.scte35_descriptors:
                validate_segmentation_descriptor(seg_desc)

                type_id = seg_desc.segmentation_type_id

                if type_id in SEGMENTATION_TYPE_IDS_BREAK_END:
                    raise InvalidRequestException(
                        message=(
                            f"event {ev.id}: segmentation_type_id 0x{type_id:02x} closes an "
                            "avail, and a time_signal break closes on its duration rather "
                            "than on a manual cue-in. Use scte35_spliceinsert with "
                            "auto_return false if you need an operator-closed break."
                        ),
                        code=ErrorCode.INVALID_REQUEST,
                    )

                if (
                    type_id in SEGMENTATION_TYPE_IDS_BREAK_START
                    and not seg_desc.segmentation_duration
                ):
                    raise InvalidRequestException(
                        message=(
                            f"event {ev.id}: segmentation_type_id 0x{type_id:02x} opens an "
                            "avail, so segmentation_duration is required - it is the only "
                            "thing that closes a time_signal break."
                        ),
                        code=ErrorCode.MISSING_FIELDS,
                    )

        else:
            raise InvalidRequestException(
                message=f"event {ev.id} requires either scte35_descriptors or scte35_base64",
                code=ErrorCode.MISSING_FIELDS,
            )

    elif ev.type == EventType.scte35_spliceinsert:
        splice_insert = ev.details.scte35_spliceinsert
        assert splice_insert is not None

        if splice_insert.scte35_base64 is not None:
            validate_scte35_base64(splice_insert.scte35_base64, ev.type)

        elif splice_insert.splice_event_id is None:
            raise InvalidRequestException(
                message=f"event {ev.id} requires either splice_event_id or scte35_base64",
                code=ErrorCode.MISSING_FIELDS,
            )

        # Same rule as the time_signal descriptor: the UPID is literal ASCII and its
        # characters are the bytes, so anything else is refused rather than re-encoded.
        try:
            splice_insert.segmentation_upid = normalize_upid(splice_insert.segmentation_upid)
        except UnicodeEncodeError:
            raise InvalidRequestException(
                message="segmentation_upid must be ASCII text, or empty",
                code=ErrorCode.INVALID_REQUEST,
            )

        # A splice_insert scheduled through this API is always out-of-network - it opens an
        # avail - so a segmentation_type_id that closes one contradicts the command it rides
        # on. scte35_returntonetwork is the event that closes the break. Same set as the
        # time_signal rule above.
        if splice_insert.segmentation_type_id in SEGMENTATION_TYPE_IDS_BREAK_END:
            raise InvalidRequestException(
                message=(
                    f"event {ev.id}: segmentation_type_id "
                    f"0x{splice_insert.segmentation_type_id:02x} closes an avail, but a "
                    "splice_insert opens one. Send scte35_returntonetwork to close the "
                    "break, or use a start type such as 0x34."
                ),
                code=ErrorCode.INVALID_REQUEST,
            )

    elif ev.type == EventType.scte35_returntonetwork:
        return_to_network = ev.details.scte35_returntonetwork
        assert return_to_network is not None

        if return_to_network.scte35_base64 is not None:
            validate_scte35_base64(return_to_network.scte35_base64, ev.type)

        elif return_to_network.splice_event_id is None:
            raise InvalidRequestException(
                message=f"event {ev.id} requires either splice_event_id or scte35_base64",
                code=ErrorCode.MISSING_FIELDS,
            )


def find_open_break(db: Session, splice_event_id: int, now: datetime) -> Optional[Event]:
    """
    The most recent splice_insert whose break is open at `now` and carries this id.

    No separate table tracks open breaks: `events.end_time` is already time + duration for
    a splice_insert (see get_event_end_time) and is indexed, so the schedule itself is the
    record. That cannot drift from what was actually scheduled, and there is no expiry
    bookkeeping for breaks that never close.
    """
    rows = (
        db.query(Event)
        .filter(
            Event.type == EventType.scte35_spliceinsert.value,
            Event.time <= now,
            Event.end_time > now,
        )
        .order_by(Event.time.desc())
        .all()
    )

    for row in rows:
        try:
            details = json.loads(row.details)
        except (TypeError, ValueError):
            continue

        splice = (details or {}).get("scte35_spliceinsert") or {}

        if splice.get("splice_event_id") == splice_event_id:
            return row

    return None


def validate_cue_in_pairing(ev: ApiEvent, db: Session, now: datetime) -> None:
    """
    A manual cue-in is only meaningful for a break that was opened to wait for one.

    Kept out of validate_event() so that stays pure and unit-testable without a session.
    """
    if ev.type != EventType.scte35_returntonetwork:
        return

    return_to_network = ev.details.scte35_returntonetwork
    assert return_to_network is not None

    # The event id lives inside the section; validate_scte35_base64 has already checked it.
    if return_to_network.scte35_base64 is not None:
        return

    splice_event_id = return_to_network.splice_event_id
    open_break = find_open_break(db, splice_event_id, now)

    if open_break is None:
        raise InvalidRequestException(
            message=(
                f"event {ev.id}: no open break with splice_event_id {splice_event_id}. "
                "A manual cue-in must match a splice_insert whose break is still running."
            ),
            code=ErrorCode.INVALID_REQUEST,
        )

    details = json.loads(open_break.details)
    splice = (details or {}).get("scte35_spliceinsert") or {}

    # Absent means true - that is the field default, and the behaviour before it existed.
    if splice.get("auto_return", True):
        raise InvalidRequestException(
            message=(
                f"event {ev.id}: the break with splice_event_id {splice_event_id} was "
                "opened with auto_return true, so it closes itself when its duration "
                "elapses. Open the break with auto_return false to close it manually."
            ),
            code=ErrorCode.INVALID_REQUEST,
        )


def get_event_end_time(ev: ApiEvent) -> Optional[datetime]:
    duration = None
    if ev.details.scte35_spliceinsert:
        duration = ev.details.scte35_spliceinsert.duration
    elif ev.details.squeeze:
        duration = ev.details.squeeze.duration
    elif ev.details.slate:
        duration = ev.details.slate.duration
    elif ev.details.image_overlay:
        duration = ev.details.image_overlay.duration
    elif ev.type == EventType.scte35_timesignal:
        duration = 0

    if duration is not None:
        return (ev.time + timedelta(milliseconds=duration)).astimezone(timezone.utc)
    return None


def to_api_event(event: Event) -> ApiEvent:
    assert event.id is not None
    assert event.time is not None
    assert event.type is not None
    assert event.details is not None

    return ApiEvent(
        id=event.id,
        type=EventType(event.type),
        time=event.time.replace(tzinfo=timezone.utc),
        details=Details.parse_raw(event.details),
    )


def to_api_events(events: List[Event]) -> List[ApiEvent]:
    result = []
    for ev in events:
        result.append(to_api_event(ev))
    return result


def create_or_update_events(event_list: EventList, db: Session):
    ret = []
    # Events submitted as one request share the same immediate timestamp. This is
    # required for coordinated overlay/squeeze cue pairs.
    request_now = datetime.now(timezone.utc)
    for event in event_list.root:
        validate_event(event, request_now)
        validate_cue_in_pairing(event, db, request_now)

        eventId = event.id
        if eventId:
            eventId = eventId.root

        db_event = db.query(Event).filter(Event.id == eventId).first()
        if db_event:
            db_event.type = event.type.value
            db_event.time = event.time.astimezone(timezone.utc)
            db_event.details = db_json_serialize(event.details)
            db_event.end_time = get_event_end_time(event)
            ret.append(db_event)
        else:
            new_event = Event(
                type=event.type.value,
                time=event.time.astimezone(timezone.utc),
                end_time=get_event_end_time(event),
                details=db_json_serialize(event.details),
            )
            db.add(new_event)
            ret.append(new_event)
    db.commit()
    return to_api_events(ret)


def list_events(
    start_time: Optional[datetime],
    end_time: Optional[datetime],
    event_type: Optional[EventType],
    is_read: Optional[bool],
    db: Session,
):
    query = db.query(Event)

    if start_time:
        query = query.filter(Event.time >= start_time)
    if end_time:
        query = query.filter(Event.time <= end_time)
    if event_type:
        query = query.filter(Event.type == event_type.value)
    if is_read is not None:
        if is_read:
            query = query.filter(Event.read_time.isnot(None))
        else:
            query = query.filter(Event.read_time.is_(None))

    events = query.all()
    return to_api_events(events)


def delete_event(id: int, db: Session, *, commit: bool = True):
    if id is None:
        raise InvalidRequestException(
            message=f"event ID is missing or empty",
            code=ErrorCode.MISSING_FIELDS,
        )

    dbEvent = db.query(Event).filter(Event.id == id).first()
    if dbEvent:
        # pass
        db.delete(dbEvent)
        if commit:
            db.commit()
        else:
            db.flush()

        return to_api_event(dbEvent)

    else:
        raise InvalidRequestException(
            message=f"Event {id} does not exist in the scheduled events list",
            code=ErrorCode.INVALID_REQUEST,
        )
