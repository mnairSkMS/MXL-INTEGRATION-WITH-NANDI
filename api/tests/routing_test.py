import json
import os

from freezegun import freeze_time
from sqlalchemy import text

from transcoder_api.model.db import Event

ROOT_DIR = os.path.dirname(os.path.realpath(__file__))
SQUEEZE_DETAILS = {"squeeze": {"squeeze_name": "crop", "duration": 1000000}}


@freeze_time("2024-07-05 13:59:45", tz_offset=-2)
def test_positive_scenario(client):
    response = client.get("/scheduled_events")
    assert response.status_code == 200
    assert response.json() == []

    with open(f"{ROOT_DIR}/test.json", "r") as test_data:
        response = client.post("/scheduled_events", json=json.load(test_data))
    assert response.status_code == 200

    response = client.get("/scheduled_events")
    assert response.status_code == 200
    response_data = response.json()
    assert len(response_data) == 5

    events = {ev["id"]: ev for ev in response_data}
    assert events[1]["type"] == "image_overlay"
    assert events[1]["time"] == "2024-07-05T12:00:00Z"
    assert events[2]["type"] == "squeeze"
    assert events[2]["time"] == "2024-07-05T13:00:00Z"
    assert events[2]["details"]["squeeze"]["squeeze_name"] == "crop"


@freeze_time("2024-07-05 13:59:45", tz_offset=-2)
def test_event_id_generation(client):
    # create first event
    response = client.post(
        "/scheduled_events",
        json=[{"type": "squeeze", "time": "2024-07-05T14:00:00+02:00", "details": SQUEEZE_DETAILS}],
    )
    assert response.status_code == 200
    assert [ev["id"] for ev in response.json()] == [1]

    response = client.get("/scheduled_events")
    assert response.status_code == 200
    assert [ev["id"] for ev in response.json()] == [1]

    # create second event
    response = client.post(
        "/scheduled_events",
        json=[{"type": "squeeze", "time": "2024-07-05T14:00:00+02:00", "details": SQUEEZE_DETAILS}],
    )
    assert response.status_code == 200
    assert [ev["id"] for ev in response.json()] == [2]
    response = client.get("/scheduled_events")
    assert response.status_code == 200
    assert [ev["id"] for ev in response.json()] == [1, 2]


@freeze_time("2024-07-05 13:59:45", tz_offset=-2)
def test_events_update(client):
    # add test events set
    with open(f"{ROOT_DIR}/test.json", "r") as test_data:
        response = client.post("/scheduled_events", json=json.load(test_data))
    assert response.status_code == 200

    # replace ev:1 overlay with squeeze
    response = client.post(
        "/scheduled_events",
        json=[
            {
                "id": 1,
                "type": "squeeze",
                "time": "2024-07-05T14:00:00+02:00",
                "details": SQUEEZE_DETAILS,
            }
        ],
    )
    assert response.status_code == 200
    assert response.json()[0]["type"] == "squeeze"
    assert response.json()[0]["id"] == 1

    # check
    response = client.get("/scheduled_events")
    assert response.status_code == 200
    response_data = response.json()
    assert len(response_data) == 5
    events = {ev["id"]: ev for ev in response_data}
    assert events[1]["type"] == "squeeze"


@freeze_time("2024-07-05 13:59:45", tz_offset=-2)
def test_events_query(client):
    with open(f"{ROOT_DIR}/test.json", "r") as test_data:
        response = client.post("/scheduled_events", json=json.load(test_data))
    assert response.status_code == 200

    for event_ids, params in [
        ([4], {"start_time": "2024-07-05T15:00:00Z"}),
        ([1, 5], {"end_time": "2024-07-05T12:00:00Z"}),
        ([2], {"event_type": "squeeze"}),
        ([], {"is_read": True}),
    ]:
        response = client.get("/scheduled_events", params=params)
        assert response.status_code == 200
        response_data = response.json()
        assert [ev["id"] for ev in response_data] == event_ids


@freeze_time("2024-07-05 13:59:45", tz_offset=-2)
def test_request_validation(client):
    # object instead of list
    response = client.post(
        "/scheduled_events",
        json={},
    )
    assert response.status_code == 400
    assert response.json()["code"] == "INVALID_REQUEST"
    assert "Input should be a valid list" in response.json()["message"]

    response = client.post(
        "/scheduled_events",
        json=[
            {
                "type": "Invalid value",
            }
        ],
    )
    assert response.status_code == 400
    assert response.json()["code"] == "INVALID_REQUEST"
    assert (
        "Input should be 'image_overlay', 'slate', 'squeeze', 'scte35_timesignal', 'scte35_spliceinsert' or 'scte35_returntonetwork'"
        in response.json()["message"]
    )

    response = client.post(
        "/scheduled_events",
        json=[
            {
                "type": "image_overlay",
                "time": "2024-07-05T15:00:00+02:00",
                "details": {
                    "image_overlay": {
                        "image_urls": ["https://example.com"],
                        "geometry_norm": {
                            "x": -1,
                        },
                        "duration": 1000000,
                    }
                },
            }
        ],
    )
    assert response.status_code == 400
    assert response.json()["code"] == "INVALID_REQUEST"
    assert "greater_than_equal" in response.json()["message"]


@freeze_time("2024-07-05 14:00:00", tz_offset=-2)
def test_custom_errors_handling(client):
    # An event at the current time is accepted; there is no minimum 15-second lead.
    response = client.post(
        "/scheduled_events",
        json=[{"type": "squeeze", "time": "2024-07-05T14:00:00+02:00", "details": SQUEEZE_DETAILS}],
    )
    assert response.status_code == 200

    response = client.post(
        "/scheduled_events",
        json=[{"type": "squeeze", "time": "2024-07-05T13:59:59+02:00", "details": SQUEEZE_DETAILS}],
    )
    assert response.status_code == 400
    assert response.json()["code"] == "INVALID_TIME_RANGE"

    response = client.post(
        "/scheduled_events",
        json=[
            {
                "type": "image_overlay",
                "time": "2024-07-05T15:00:00+02:00",
                "details": SQUEEZE_DETAILS,
            }
        ],
    )
    assert response.status_code == 400
    assert response.json()["code"] == "DETAILS_TYPE_MISMATCH"


@freeze_time("2024-07-05 14:00:00", tz_offset=-2)
def test_squeeze_validator(client):
    response = client.post(
        "/scheduled_events",
        json=[
            {
                "type": "squeeze",
                "time": "2024-07-05T15:00:00+02:00",
                "details": {
                    "squeeze": {
                        "squeeze_name": None,
                        "input_window_norm": {},
                        "duration": 1000000,
                    }
                },
            }
        ],
    )
    assert response.status_code == 400
    assert response.json()["code"] == "MISSING_FIELDS"
    assert "input_window_norm and output_window_norm" in response.json()["message"]


@freeze_time("2024-07-05 14:00:00", tz_offset=-2)
def test_time_serialization(test_db, client):
    response = client.post(
        "/scheduled_events",
        json=[
            {
                "type": "image_overlay",
                "time": "2024-07-05T15:00:00+02:00",
                "details": {
                    "image_overlay": {
                        "image_urls": ["https://example.com"],
                        "end_time": "2024-07-05T15:00:00+02:00",
                    }
                },
            }
        ],
    )
    assert response.status_code == 200
    # check time format in response
    # assert response.json()[0]["details"]["image_overlay"]["end_time"] == "2024-07-05T13:00:00Z"

    # check time format in db event.time and time format in details json
    db_event_time, event_details, creation_time = test_db.execute(
        text("SELECT time, details, creation_time FROM events;")
    ).first()
    db_event_details = json.loads(event_details)
    # assert db_event_details["image_overlay"]["end_time"] == "2024-07-05 13:00:00.000000Z"
    assert db_event_time == "2024-07-05 13:00:00.000000Z"
    # FIXME I can't freeze time for sqlite
    # assert creation_time == "2024-07-05 13:00:00.000000Z"
    assert creation_time.endswith("Z")


@freeze_time("2024-07-05 14:00:00", tz_offset=-2)
def test_squeeze_param_validation(client):
    response = client.post(
        "/scheduled_events",
        json=[
            {
                "type": "squeeze",
                "time": "2024-07-05T15:00:00+02:00",
                "details": {
                    "squeeze": {
                        "input_window_norm": {
                            "x": None,
                            "y": None,
                            "width": None,
                            "height": None,
                        },
                        "output_window_norm": {
                            "x": None,
                            "y": None,
                            "width": None,
                            "height": None,
                        },
                        "duration": 1000000,
                    }
                },
            }
        ],
    )
    assert response.status_code == 200


@freeze_time("2024-07-05 14:00:00", tz_offset=-2)
def test_time_signal_segmentation_cancellation_validation(client):
    cancellation_descriptor = {
        "segmentation_event_id": 1001,
        "segmentation_cancel_indicator": "SEGMENTATION_EVENT_CANCELED",
    }
    req_body = [
        {
            "type": "scte35_timesignal",
            "time": "2024-07-05T15:00:00+02:00",
            "details": {"scte35_timesignal": {"scte35_descriptors": [cancellation_descriptor]}},
        }
    ]

    response = client.post("/scheduled_events", json=req_body)
    assert response.status_code == 400
    assert response.json()["code"] == "MISSING_FIELDS"
    assert (
        "segmentation_upid_type, segmentation_upid,"
        " segmentation_type_id,segment_num and segments_expected "
        "ar required"
    ) in response.json()["message"]

    cancellation_descriptor["segmentation_upid_type"] = 1
    cancellation_descriptor["segmentation_type_id"] = 52
    cancellation_descriptor["segmentation_upid"] = "abcdef"
    cancellation_descriptor["segment_num"] = 1
    cancellation_descriptor["segments_expected"] = 1

    response = client.post("/scheduled_events", json=req_body)
    assert response.status_code == 400
    assert response.json()["code"] == "MISSING_FIELDS"
    assert (
        "For segmentation_type_id: 0x34 " "sub_segment_num and sub_segments_expected ar required"
    ) in response.json()["message"]


@freeze_time("2024-07-05 13:59:45", tz_offset=-2)
def test_end_time_calculation(client, test_db):
    # load test data
    with open(f"{ROOT_DIR}/test.json", "r") as test_data:
        response = client.post("/scheduled_events", json=json.load(test_data))
    assert response.status_code == 200

    # get raw data from db
    db_events = test_db.execute(text("SELECT id, end_time FROM events;")).all()

    result = {event_id: end_time for (event_id, end_time) in db_events}

    print("Result from DB:", result)

    assert result == {
        1: "2024-07-05 12:16:40.000000Z",
        2: "2024-07-05 13:16:40.000000Z",
        3: "2024-07-05 14:00:00.000000Z",
        4: "2024-07-05 15:00:00.120000Z",
        5: None,
    }
