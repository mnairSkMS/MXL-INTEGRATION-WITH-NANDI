import json


def test_operator_and_migrated_catalog_assets(client, monkeypatch):
    # Pin the gate rather than inheriting it: the shipped deployment sets
    # ENABLE_VIDEO_GRAPHICS=true, and the video-asset assertions below are
    # about what the catalog reports while the gate is CLOSED.
    monkeypatch.setenv("ENABLE_VIDEO_GRAPHICS", "false")

    operator = client.get("/operator")
    assert operator.status_code == 200
    assert "Nandi Transcoder — Operator" in operator.text
    assert "SSAI" in operator.text

    playout = client.get("/operator/playout")
    assert playout.status_code == 200
    assert "Nandi Transcoder — Operator" in playout.text
    assert "Playlist" in playout.text

    catalog = client.get("/graphics/catalog")
    assert catalog.status_code == 200
    assets = catalog.json()["assets"]
    # 9 image assets plus the 4 video ones: the two sponsor MOVs and the two
    # animated test cards that carry a burned-in frame counter.
    assert len(assets) == 13
    assert {asset["id"] for asset in assets if asset["mediaType"] == "video"} == {
        "lband-video", "lband-video-anim", "aston-video", "aston-video-anim",
    }
    assert {asset["kind"] for asset in assets} == {"logo", "lband", "bug", "aston"}
    expected_defaults = {"logo": "fade", "bug": "fade", "lband": "reveal", "aston": "reveal"}
    assert all(asset["defaultMode"] == expected_defaults[asset["kind"]] for asset in assets)
    assert all(asset["defaultMode"] in asset["supportedModes"] for asset in assets)
    assert all(not asset["enabled"] for asset in assets if asset["mediaType"] == "video")
    assert all(asset["enabled"] for asset in assets if asset["mediaType"] == "image")
    assert client.get("/operator/static/graphics/logo.png").status_code == 200
    assert client.get("/operator/static/graphics/bug.png").status_code == 200
    assert client.get("/operator/static/graphics/aston1.jpg").status_code == 200


def test_catalog_rejects_an_unsupported_default_mode(client, monkeypatch, tmp_path):
    catalog = tmp_path / "graphics.json"
    catalog.write_text(json.dumps({"assets": [{
        "id": "invalid", "kind": "logo", "mediaUri": "file:///logo.png",
        "geometry": {"x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0},
        "supportedModes": ["snap"], "defaultMode": "fade",
    }]}))
    monkeypatch.setenv("GRAPHICS_CATALOG_PATH", str(catalog))
    assert client.get("/graphics/catalog").status_code == 422


def test_catalog_cue_and_cancel(client, monkeypatch, tmp_path):
    catalog = tmp_path / "graphics.json"
    catalog.write_text(
        json.dumps(
            {
                "assets": [
                    {
                        "id": "lband-demo",
                        "label": "L-band demo",
                        "kind": "lband",
                        "mediaUri": "file:///media/graphics/lband.png",
                        "geometry": {"x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0},
                        "squeezeName": "l_band",
                        "supportedModes": ["snap", "reveal"],
                    }
                ],
            }
        )
    )
    monkeypatch.setenv("GRAPHICS_CATALOG_PATH", str(catalog))

    assert client.get("/graphics/catalog").json()["assets"][0]["id"] == "lband-demo"
    response = client.post(
        "/graphics/cues",
        json={
            "assetId": "lband-demo",
            "mode": "reveal",
            "durationMs": 15000,
            "startInMs": 0,
            "fadeMs": 600,
        },
    )
    assert response.status_code == 201
    assert response.json()["kind"] == "lband"
    assert response.json()["mode"] == "reveal"
    assert response.json()["phase"] == "on_air"
    assert response.json()["cancelable"] is True
    cues = client.get("/graphics/cues")
    assert cues.status_code == 200
    assert cues.json()[0]["assetId"] == "lband-demo"
    events = client.get("/scheduled_events").json()
    assert len(events) == 2
    assert events[0]["time"] == events[1]["time"]
    by_type = {event["type"]: event["details"] for event in events}
    # A 15-second reveal includes the 600 ms reveal/restore legs. The engine
    # starts its closing transition at 14.4 seconds rather than extending the
    # visible sponsor beyond the operator-entered duration.
    assert by_type["squeeze"]["squeeze"]["duration"] == 14400
    assert by_type["squeeze"]["squeeze"]["fade_in"] == 600
    assert by_type["squeeze"]["squeeze"]["fade_out"] == 600
    assert client.delete(f"/graphics/cues/{response.json()['id']}").status_code == 204
    assert client.get("/graphics/cues").json() == []
    assert client.get("/scheduled_events").json() == []
    # The supplied duration contains both animation legs; invalid combinations
    # are rejected instead of silently lengthening the visible cue.
    assert client.post(
        "/graphics/cues",
        json={"assetId": "lband-demo", "mode": "reveal", "durationMs": 1000, "fadeMs": 600},
    ).status_code == 422


def test_cue_emits_normalized_windows(client, monkeypatch, tmp_path):
    catalog = tmp_path / "graphics.json"
    catalog.write_text(
        json.dumps(
            {
                "assets": [
                    {
                        "id": "lband-demo",
                        "label": "L-band demo",
                        "kind": "lband",
                        "mediaUri": "file:///media/graphics/lband.png",
                        "geometry": {"x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0},
                        "squeezeWindow": {
                            "x": 0.0, "y": 0.0, "width": 0.78125, "height": 0.763889,
                        },
                        "supportedModes": ["snap", "reveal"],
                    }
                ],
            }
        )
    )
    monkeypatch.setenv("GRAPHICS_CATALOG_PATH", str(catalog))

    assert client.post(
        "/graphics/cues",
        json={"assetId": "lband-demo", "mode": "reveal", "durationMs": 15000, "fadeMs": 600},
    ).status_code == 201

    by_type = {
        event["type"]: event["details"]
        for event in client.get("/scheduled_events").json()
    }
    squeeze = by_type["squeeze"]["squeeze"]
    # The source window is now a constant full frame rather than a guessed 1920x1080
    # canvas, which is what lets the engine resolve it against the real output raster.
    assert squeeze["input_window_norm"] == {"x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0}
    assert squeeze["output_window_norm"] == {
        "x": 0.0, "y": 0.0, "width": 0.78125, "height": 0.763889,
    }
    assert by_type["image_overlay"]["image_overlay"]["geometry_norm"] == {
        "x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0,
    }


def test_catalog_rejects_pixel_geometry(client, monkeypatch, tmp_path):
    catalog = tmp_path / "graphics.json"
    catalog.write_text(
        json.dumps(
            {
                "assets": [
                    {
                        "id": "lband-pixels",
                        "kind": "lband",
                        "mediaUri": "file:///lband.png",
                        "geometry": {"x": 0, "y": 0, "width": 1920, "height": 1080},
                        "squeezeWindow": {"x": 0, "y": 0, "width": 1500, "height": 825},
                        "supportedModes": ["snap"],
                    }
                ],
            }
        )
    )
    monkeypatch.setenv("GRAPHICS_CATALOG_PATH", str(catalog))

    response = client.get("/graphics/catalog")
    assert response.status_code == 422
    assert "fraction of the output frame" in response.json()["message"]


def test_sponsor_lock_and_hold_cue(client, monkeypatch, tmp_path):
    catalog = tmp_path / "graphics.json"
    catalog.write_text(
        json.dumps(
            {
                "assets": [
                    {
                        "id": "logo-demo", "label": "Logo", "kind": "logo",
                        "mediaType": "image", "mediaUri": "file:///logo.png",
                        "geometry": {"x": 0.03125, "y": 0.055556, "width": 0.171875, "height": 0.125},
                        "supportedModes": ["snap"],
                    },
                    {
                        "id": "lband-demo", "label": "L-band", "kind": "lband",
                        "mediaType": "image", "mediaUri": "file:///lband.png",
                        "geometry": {"x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0},
                        "squeezeName": "l_band", "supportedModes": ["snap"],
                    },
                    {
                        "id": "aston-demo", "label": "Aston", "kind": "aston",
                        "mediaType": "image", "mediaUri": "file:///aston.png",
                        "geometry": {"x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0},
                        "squeezeName": "aston", "supportedModes": ["snap"],
                    },
                ],
            }
        )
    )
    monkeypatch.setenv("GRAPHICS_CATALOG_PATH", str(catalog))

    logo = client.post(
        "/graphics/cues",
        json={"assetId": "logo-demo", "mode": "snap", "durationMs": None},
    )
    assert logo.status_code == 201
    assert logo.json()["endsAt"] is None

    lband = client.post(
        "/graphics/cues",
        json={"assetId": "lband-demo", "mode": "snap", "durationMs": 5000},
    )
    assert lband.status_code == 201
    conflict = client.post(
        "/graphics/cues",
        json={"assetId": "aston-demo", "mode": "snap", "durationMs": 5000},
    )
    assert conflict.status_code == 409
    assert client.delete(f"/graphics/cues/{lband.json()['id']}").status_code == 204
    assert client.post(
        "/graphics/cues",
        json={"assetId": "aston-demo", "mode": "snap", "durationMs": 5000},
    ).status_code == 201
