"""Read-only HTTP server for the live HLS directory, for LAN players.

The management API also serves /hls, but it is unauthenticated and PUT /config
writes, so it stays on loopback. This serves only the HLS directory, GET/HEAD
only, with the same headers as the API's /hls mount: no caching (segments roll
continuously) and a permissive CORS header so browser players on other origins
can fetch the manifest.

    python hls_lan_server.py [directory] [port]
"""
import functools
import http.server
import mimetypes
import sys

mimetypes.add_type("application/vnd.apple.mpegurl", ".m3u8")
mimetypes.add_type("video/mp2t", ".ts")
# CMAF DASH output (docker-compose.dash-out.yml serves /runtime/dash with this server).
mimetypes.add_type("application/dash+xml", ".mpd")
mimetypes.add_type("video/mp4", ".cmfv")
mimetypes.add_type("video/mp4", ".cmfi")
mimetypes.add_type("audio/mp4", ".cmfa")


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        super().end_headers()

    def list_directory(self, path):
        self.send_error(404, "No such file")
        return None


def main() -> None:
    directory = sys.argv[1] if len(sys.argv) > 1 else "/runtime/hls"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8081
    handler = functools.partial(Handler, directory=directory)
    http.server.ThreadingHTTPServer(("0.0.0.0", port), handler).serve_forever()


if __name__ == "__main__":
    main()
