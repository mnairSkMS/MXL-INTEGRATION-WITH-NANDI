"""Capture live API responses for the operator UI smoke test.

The harness is only worth trusting because it replays what the server actually
sent, not what we assumed it would. Re-run this against a running stack whenever
a response shape changes:

    python3 api/tests/ui/capture_fixtures.py [base-url]
"""
from __future__ import annotations

import json
import pathlib
import sys
import urllib.request

PATHS = [
    "/graphics/catalog", "/rundowns/status", "/rundowns/main",
    "/rundowns/main/executions", "/rundowns/main/clock",
    "/playlists/main", "/media", "/graphics/cues",
]


def main() -> int:
    base = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8080"

    def get(path: str):
        with urllib.request.urlopen(base + path) as response:
            return json.load(response)

    fixtures = {f"GET {path}": get(path) for path in PATHS}
    version_id = fixtures["GET /rundowns/main"]["activeVersionId"]
    key = f"/rundowns/main/versions/{version_id}"
    fixtures[f"GET {key}"] = get(key)

    target = pathlib.Path(__file__).with_name("fixtures.json")
    target.write_text(json.dumps(fixtures, indent=1), encoding="utf-8")
    print(f"wrote {target} — version {version_id}, {len(fixtures)} responses")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
