#!/usr/bin/env bash
# Run on the EC2 instance: load the exported image and record its tag for the harness.
#   load-image.sh [image.tar.gz]     (default: the transcoder-image.tar.gz in the kit root)
set -euo pipefail
KIT="$(cd "$(dirname "$0")/.." && pwd)"
TAR="${1:-$KIT/transcoder-image.tar.gz}"
[ -f "$TAR" ] || { echo "image archive $TAR not found" >&2; exit 1; }
LOADED="$( (command -v pigz >/dev/null && pigz -dc "$TAR" || gzip -dc "$TAR") | docker load | sed -n 's/^Loaded image: //p' | head -1)"
[ -n "$LOADED" ] || { echo "docker load reported no tagged image" >&2; exit 1; }
ID="$(docker image inspect "$LOADED" --format '{{.Id}}')"
echo "$LOADED $ID" > "$KIT/image.txt"
if [ -f "$TAR.txt" ] && [ "$(cut -d' ' -f2 "$TAR.txt")" != "$ID" ]; then
    echo "WARNING: loaded image id $ID differs from the packed id $(cut -d' ' -f2 "$TAR.txt")" >&2
fi
echo "loaded $LOADED ($ID); recorded in $KIT/image.txt"
