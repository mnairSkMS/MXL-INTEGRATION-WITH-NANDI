#!/usr/bin/env bash
# Run on the build machine: export the transcoder image you intend to benchmark.
#   pack-image.sh <image:tag> [output.tar.gz]
set -euo pipefail
IMAGE="${1:?usage: pack-image.sh <image:tag> [output.tar.gz]}"
OUT="${2:-transcoder-image.tar.gz}"
docker image inspect "$IMAGE" >/dev/null
ID="$(docker image inspect "$IMAGE" --format '{{.Id}}')"
GZ="gzip -1"; command -v pigz >/dev/null && GZ="pigz -1"
echo "saving $IMAGE ($ID) -> $OUT"
docker save "$IMAGE" | $GZ > "$OUT"
echo "$IMAGE $ID" > "$OUT.txt"
ls -lh "$OUT"
