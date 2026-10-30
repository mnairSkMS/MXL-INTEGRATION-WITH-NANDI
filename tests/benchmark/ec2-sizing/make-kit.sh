#!/usr/bin/env bash
# Assemble the EC2 sizing kit from this repository into a zip.
#   make-kit.sh <output.zip> [--image <image:tag>]
# --image also exports that Docker image into the zip (about 0.6-0.8 GB compressed).
# The zip always carries the transcoder source under source/ (no runtime state, no media, no .git),
# enough to rebuild the image: docker build -f source/docker/utpl-transcoder/Dockerfile source
# The kit contains the offline transcoder license: treat the zip as confidential.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
OUT="$(realpath -m "${1:?usage: make-kit.sh <output.zip> [--image <image:tag>]}")"
IMAGE=""
[ "${2:-}" = "--image" ] && IMAGE="${3:?--image needs a tag}"

STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
KIT="$STAGE/ec2-sizing-kit"
mkdir -p "$KIT/assets/media/queue" "$KIT/assets/media/graphics" "$KIT/assets/license" "$KIT/docs" "$KIT/results"
cp -r "$HERE/bench" "$HERE/setup" "$HERE/media-gen" "$HERE/profiles" "$KIT/"
cp "$HERE/README.md" "$KIT/"
find "$KIT" -name '__pycache__' -prune -exec rm -rf {} +
cp "$REPO/doc/codebase/36-ec2-hardware-sizing-plan.md" "$KIT/docs/sizing-plan.md"
cp "$REPO/config/migrated-media-playlist.json" "$KIT/docs/reference-compose-config.json"

# Source tree: everything needed to build the image, plus tests and docs.
mkdir -p "$KIT/source"
SRC="${KIT_SOURCE:-$REPO}"   # tree the image was built from (default: this repository)
tar -C "$SRC" -cf - \
    --exclude='.git' --exclude='runtime' --exclude='media' --exclude='*.sqlite*' --exclude='__pycache__' \
    --exclude='node_modules' --exclude='.venv' --exclude='build' --exclude='tests/benchmark/ec2-sizing/results' \
    --exclude='*.pyc' --exclude='hls.dump' \
    api config doc docker tests transcoder AGENTS.md README.md version.txt docker-compose.playlist.yml 2>/dev/null \
    | tar -C "$KIT/source" -xf -
mkdir -p "$KIT/source/media"; cp -r "$SRC/media/graphics" "$KIT/source/media/"

# Programme, ad-reel and seed-playlist media used by the profiles' "repo" content set.
for f in one-cent-call-1080p.mp4 toyota-etios.mp4 coke-commercial-1080p-h264.mp4 air-asia-billboard.mp4 \
         red.mp4 green.mp4 blue.mp4 yellow.mp4 magenta.mp4 playlist.m3u8; do
    cp "$REPO/media/queue/$f" "$KIT/assets/media/queue/"
done
cp "$REPO"/media/graphics/*.png "$KIT/assets/media/graphics/"
cp "$REPO/transcoder/license/prod/licenses/offline.lic" "$KIT/assets/license/transcoder.lic"
echo "1.0.0 built $(date -u +%FT%TZ) from $(cat "$REPO/version.txt")" > "$KIT/KIT_VERSION"

if [ -n "$IMAGE" ]; then
    bash "$HERE/setup/pack-image.sh" "$IMAGE" "$KIT/transcoder-image.tar.gz"
fi
chmod +x "$KIT"/setup/*.sh "$KIT"/media-gen/*.sh "$KIT"/bench/*.py
rm -f "$OUT"
(
    cd "$STAGE"
    zip -qr "$OUT" ec2-sizing-kit -x 'ec2-sizing-kit/transcoder-image.tar.gz*'
    # The image is already compressed: store it without recompression.
    if [ -f ec2-sizing-kit/transcoder-image.tar.gz ]; then
        zip -q -0 "$OUT" ec2-sizing-kit/transcoder-image.tar.gz ec2-sizing-kit/transcoder-image.tar.gz.txt
    fi
)
ls -lh "$OUT"
