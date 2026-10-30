#!/usr/bin/env bash
# MXL between separate containers that share only a tmpfs volume (the domain). Needs Docker on the
# host, so it is not part of tests/run.sh. Checks:
#   1. a writer in one container and a reader in another exchange video;
#   2. the reader still works when the domain is mounted READ-ONLY (a consumer must not need write
#      access to the producer's memory);
#   3. the reader works as a different (non-root) user than the writer;
#   4. the tmpfs volume size needed for the flows.
#
#   tests/manual/mxl/cross-container.sh [image]      default image: utpl/transcoder-test:mxl

IMAGE="${1:-utpl/transcoder-test:mxl}"
FLOW=5fbec3b1-1b0f-417d-9059-8b94a47197ed
VOL=mxl-cross-test-$$
WRITER=mxl-cross-writer-$$
RC=0

fail() { echo "FAIL: $*"; RC=1; }
ok()   { echo "ok:   $*"; }

cleanup() {
    docker rm -f "$WRITER" > /dev/null 2>&1
    docker volume rm "$VOL" > /dev/null 2>&1
}
trap cleanup EXIT

docker volume create --driver local --opt type=tmpfs --opt device=tmpfs --opt o=size=256m,uid=1000,gid=1000,mode=0775 "$VOL" > /dev/null || exit 1

# Writer: 1080p29.97, runs as uid 1000, domain mounted read-write.
docker run -d --name "$WRITER" --user 1000:1000 --entrypoint bash -v "$VOL:/run/mxl" "$IMAGE" -c "
    gst-launch-1.0 -q videotestsrc is-live=true do-timestamp=true pattern=bar num-buffers=900 \
        ! videoconvert ! 'video/x-raw,format=v210,width=1920,height=1080,framerate=30000/1001' \
        ! queue ! mxlsink flow-id=$FLOW domain=/run/mxl" > /dev/null

# Wait for the flow to appear.
for i in $(seq 1 100); do
    docker run --rm --user 1001:1001 --entrypoint mxl-info -v "$VOL:/run/mxl:ro" "$IMAGE" -d /run/mxl -l 2>/dev/null | grep -qi "$FLOW" && break
    sleep 0.2
done

reader() {  # reader <docker user> <mount options>
    docker run --rm --user "$1" --entrypoint bash -v "$VOL:/run/mxl$2" "$IMAGE" -c "
        timeout 30 gst-launch-1.0 -q mxlsrc video-flow-id=$FLOW domain=/run/mxl num-buffers=60 \
            ! videoconvert ! 'video/x-raw,format=I420' ! y4menc ! filesink location=/tmp/o.y4m \
        && ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=width,height,r_frame_rate,nb_read_frames -of csv=p=0 /tmp/o.y4m"
}

echo "== reader as uid 1001 (different user), domain read-write"
out=$(reader 1001:1001 "") ; echo "   $out"
[ "$out" = "1920,1080,30000/1001,60" ] && ok "frames received across containers" || fail "unexpected result '$out'"

echo "== reader as uid 1001, domain READ-ONLY"
out=$(reader 1001:1001 ":ro") ; echo "   $out"
[ "$out" = "1920,1080,30000/1001,60" ] && ok "read-only consumer works" || fail "read-only consumer failed: '$out'"

echo "== domain contents and tmpfs use"
docker run --rm --entrypoint bash -v "$VOL:/run/mxl:ro" "$IMAGE" -c "du -sk /run/mxl; df -k /run/mxl | tail -1; mxl-info -d /run/mxl -f $FLOW | grep -E 'Active|Last (write|read)'"

echo "== tmpfs too small (8 MB) must fail visibly, not silently"
SMALL=mxl-cross-small-$$
docker volume create --driver local --opt type=tmpfs --opt device=tmpfs --opt o=size=8m "$SMALL" > /dev/null
if docker run --rm --entrypoint bash -v "$SMALL:/run/mxl" "$IMAGE" -c "
    timeout 20 gst-launch-1.0 -q videotestsrc num-buffers=30 pattern=bar \
        ! 'video/x-raw,format=v210,width=1920,height=1080,framerate=30000/1001' \
        ! mxlsink flow-id=$FLOW domain=/run/mxl sync=false" > /tmp/mxl-small.log 2>&1; then
    fail "writer reported success on an 8 MB tmpfs (a 1080p flow needs about 27 MB)"
else
    ok "writer failed on a too-small tmpfs (exit status non-zero)"
fi
tail -n 3 /tmp/mxl-small.log | sed 's/^/   /'
docker volume rm "$SMALL" > /dev/null 2>&1

exit $RC
