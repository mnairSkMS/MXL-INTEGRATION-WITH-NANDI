#!/usr/bin/env bash
# MXL between a Nandi image (GStreamer 1.24) and the Strom image with MXL (GStreamer 1.28), sharing only a tmpfs
# volume as the domain. Step 1 of the OpenLive / Strom integration (doc/integration/01-strom-image.md). Needs Docker
# and both images, so it is not part of tests/run.sh.
#
#   tests/manual/mxl/strom-roundtrip.sh [nandi image] [strom image]
#       defaults: utpl/transcoder-test:mxl  strom-mxl:0df2e4b-mxl1.1.0
#
# Checks:
#   0. the plugin in the Strom image: mxlsrc / mxlsink load, libraries resolve, and how their pads and properties
#      compare with the Nandi image's (printed, not judged);
#   1. video Nandi -> Strom and 2. Strom -> Nandi: 1080p25 v210, every frame byte-identical to a reference frame made in
#      the writer's own image (the bytes, not a converted picture, so a 1.24 / 1.28 videoconvert difference cannot hide
#      or fake a fault);
#   3. audio both ways: 48 kHz Float32 stereo, a -6 dBFS tone on channel 1 only;
#   4. a reader started before its writer;
#   5. a writer killed and started again under the same flow id.
# Nandi runs as uid 1000 (as in docker-compose.yml) and Strom as root (its image's default), so every exchange is also
# between two users.

NANDI="${1:-utpl/transcoder-test:mxl}"
STROM="${2:-strom-mxl:0df2e4b-mxl1.1.0}"
FLOW_V=5fbec3b1-1b0f-417d-9059-8b94a47197ed
FLOW_A=b3bb5be7-9a4b-4be1-8b13-c96b3d6a7a10
FRAMES=50
VOL=mxl-strom-test-$$
OUT=$(mktemp -d "${TMPDIR:-/tmp}/mxl-strom-XXXXXX")
chmod 0777 "$OUT"
RC=0
CONTAINERS=()

fail() { echo "FAIL: $*"; RC=1; }
ok()   { echo "ok:   $*"; }
note() { echo "note: $*"; }

cleanup() {
    for c in "${CONTAINERS[@]}"; do docker rm -f "$c" > /dev/null 2>&1; done
    # Files written by root inside a container: remove them from a container.
    docker run --rm -v "$OUT:/out" --entrypoint sh "$NANDI" -c 'rm -rf /out/*' > /dev/null 2>&1
    rmdir "$OUT" 2> /dev/null
    docker volume rm "$VOL" > /dev/null 2>&1
}
trap cleanup EXIT

for img in "$NANDI" "$STROM"; do
    docker image inspect "$img" > /dev/null 2>&1 || { echo "skip: image $img not found"; exit 4; }
done

docker volume create --driver local --opt type=tmpfs --opt device=tmpfs --opt o=size=256m,uid=1000,gid=1000,mode=0777 "$VOL" > /dev/null || exit 1

# run <nandi|strom> <command>: a one-off command in that image, with the domain and the output directory mounted.
run() {
    local img user
    if [ "$1" = nandi ]; then img=$NANDI; user=1000:1000; else img=$STROM; user=0:0; fi
    docker run --rm --user "$user" --entrypoint bash -v "$VOL:/run/mxl" -v "$OUT:/out" "$img" -c "$2"
}

# start <name> <nandi|strom> <command>: the same, detached, remembered for cleanup.
start() {
    local img user
    if [ "$2" = nandi ]; then img=$NANDI; user=1000:1000; else img=$STROM; user=0:0; fi
    docker run -d --name "$1" --user "$user" --entrypoint bash -v "$VOL:/run/mxl" -v "$OUT:/out" "$img" -c "$3" > /dev/null
    CONTAINERS+=("$1")
}

VIDEO_CAPS="video/x-raw,format=v210,width=1920,height=1080,framerate=25/1"
video_writer() {  # video_writer <frames>
    echo "gst-launch-1.0 -q videotestsrc is-live=true do-timestamp=true pattern=bar num-buffers=$1 \
        ! videoconvert ! '$VIDEO_CAPS' ! queue ! mxlsink flow-id=$FLOW_V domain=/run/mxl"
}
video_reference() {  # video_reference <file>: one frame of the same source, in v210, with no MXL
    echo "gst-launch-1.0 -q videotestsrc num-buffers=1 pattern=bar ! videoconvert ! '$VIDEO_CAPS' ! filesink location=$1"
}
video_reader() {  # video_reader <frames> <file> [timeout]
    echo "timeout ${3:-30} gst-launch-1.0 -q mxlsrc video-flow-id=$FLOW_V domain=/run/mxl num-buffers=$1 ! filesink location=$2"
}
audio_writer() {  # audio_writer <buffers of 10 ms>: 1 kHz at -6 dBFS on channel 1 of 2, silence on channel 0
    echo "gst-launch-1.0 -q audiotestsrc is-live=true do-timestamp=true wave=sine freq=1000 volume=0.5 num-buffers=$1 samplesperbuffer=480 \
        ! 'audio/x-raw,format=F32LE,rate=48000,channels=1' \
        ! audiomixmatrix in-channels=1 out-channels=2 matrix='<<0.0>,<1.0>>' channel-mask=0x3 \
        ! 'audio/x-raw,format=F32LE,rate=48000,channels=2,layout=interleaved,channel-mask=(bitmask)0x3' \
        ! queue ! mxlsink flow-id=$FLOW_A domain=/run/mxl"
}
audio_reader() {  # audio_reader <buffers> <file>
    echo "timeout 30 gst-launch-1.0 -q -e mxlsrc audio-flow-id=$FLOW_A domain=/run/mxl num-buffers=$1 ! audioconvert ! wavenc ! filesink location=$2"
}

wait_flow() {  # wait_flow <flow id> [seconds]
    for ((i = 0; i < ${2:-10} * 5; i++)); do
        run nandi "mxl-info -d /run/mxl -l 2>/dev/null" | grep -qi "$1" && return 0
        sleep 0.2
    done
    return 1
}
flow_field() {  # flow_field <flow id> <label>
    run nandi "mxl-info -d /run/mxl -f $1 2>/dev/null" | awk -F': *' -v k="$2" '{ gsub(/^[ \t]+/, "", $1) } $1 == k { print $2; exit }'
}
remove_dead_flows() { run nandi "mxl-info -d /run/mxl -g" > /dev/null 2>&1; }

# frames_match <file> <reference>: the file is a whole number of reference-sized frames, all equal to the reference.
# Prints "<frames> <distinct checksums>".
frames_match() {
    local size total n sums ref
    size=$(stat -c %s "$OUT/$2"); total=$(stat -c %s "$OUT/$1" 2>/dev/null || echo 0)
    [ "$size" -gt 0 ] && [ $((total % size)) -eq 0 ] || { echo "0 size-mismatch($total/$size)"; return 1; }
    n=$((total / size))
    ref=$(md5sum < "$OUT/$2" | cut -d' ' -f1)
    sums=$(split -b "$size" --filter='md5sum' "$OUT/$1" | cut -d' ' -f1 | sort -u)
    echo "$n $(echo "$sums" | wc -l)"
    [ "$sums" = "$ref" ]
}

# --- 0. The plugin in the Strom image ---------------------------------------------------------------------------------
echo "== 0. plugin in $STROM"
run strom "gst-inspect-1.0 --version | head -2; gst-inspect-1.0 mxl | grep -E '^ *(Version|Filename)'" | sed 's/^/   /'
if run strom "gst-inspect-1.0 mxlsrc > /dev/null && gst-inspect-1.0 mxlsink > /dev/null"; then ok "mxlsrc and mxlsink load"; else fail "mxlsrc / mxlsink do not load"; fi
missing=$(run strom "ldd /usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstmxl.so /usr/local/lib/libmxl.so* /usr/local/bin/mxl-info 2>&1 | grep 'not found'")
[ -z "$missing" ] && ok "libraries resolve (libgstmxl, libmxl, mxl-info)" || fail "unresolved libraries: $missing"
for el in mxlsrc mxlsink; do
    # gst-inspect 1.28 prints a value's type after it ("v210 (gchararray)"), 1.24 does not: drop it before comparing.
    run nandi "gst-inspect-1.0 $el" | sed -n '/^Pad Templates/,$p' | sed 's/ ([A-Za-z0-9]*)$//' > "$OUT/$el.nandi.txt"
    run strom "gst-inspect-1.0 $el" | sed -n '/^Pad Templates/,$p' | sed 's/ ([A-Za-z0-9]*)$//' > "$OUT/$el.strom.txt"
    d=$(diff "$OUT/$el.nandi.txt" "$OUT/$el.strom.txt")
    if [ -z "$d" ]; then ok "$el: pad templates and properties identical in both images (types ignored)"
    else note "$el: pad templates / properties differ between Nandi and Strom:"; echo "$d" | sed 's/^/      /'; fi
done

# --- 1. and 2. Video both ways ----------------------------------------------------------------------------------------
video_way() {  # video_way <writer> <reader>
    local tag="$1-to-$2"
    run "$1" "$(video_reference /out/$tag.ref.v210)" || { fail "$tag: reference frame not made"; return; }
    start "w-$tag-$$" "$1" "$(video_writer 400)"
    wait_flow $FLOW_V 10 || { fail "$tag: flow did not appear"; docker rm -f "w-$tag-$$" > /dev/null; return; }
    run "$2" "$(video_reader $FRAMES /out/$tag.v210)" || fail "$tag: reader did not finish"
    local r; r=$(frames_match $tag.v210 $tag.ref.v210) \
        && ok "video $1 -> $2: $r (frames, distinct checksums), every frame equal to the writer's reference" \
        || fail "video $1 -> $2: $r (frames, distinct checksums), expected $FRAMES identical to the reference"
    [ "${r%% *}" = "$FRAMES" ] || fail "video $1 -> $2: expected $FRAMES frames"
    docker rm -f "w-$tag-$$" > /dev/null; remove_dead_flows
}
echo "== 1. video Nandi -> Strom (1080p25 v210)"; video_way nandi strom
echo "== 2. video Strom -> Nandi (1080p25 v210)"; video_way strom nandi

# --- 3. Audio both ways -----------------------------------------------------------------------------------------------
audio_way() {
    local tag="$1-to-$2"
    start "w-$tag-$$" "$1" "$(audio_writer 1000)"
    wait_flow $FLOW_A 10 || { fail "audio $tag: flow did not appear"; docker rm -f "w-$tag-$$" > /dev/null; return; }
    run "$2" "$(audio_reader 200 /out/$tag.wav)" || fail "audio $tag: reader did not finish"
    local res
    res=$(run nandi "
        p() { ffprobe -v error -show_entries stream=\$1 -of default=nw=1:nk=1 /out/$tag.wav; }
        pk() { ffmpeg -v info -i /out/$tag.wav -af \"pan=mono|c0=c\$1,volumedetect\" -f null - 2>&1 | awk '/max_volume/ { print \$(NF-1) }'; }
        echo \$(p sample_rate) \$(p channels) \$(p codec_name) \$(pk 0) \$(pk 1)")
    echo "   rate channels format peak0 peak1: $res"
    set -- $res
    if [ "$1" = 48000 ] && [ "$2" = 2 ] && [ "$3" = pcm_f32le ] \
        && awk -v a="$4" -v b="$5" 'BEGIN { exit !(a < -80 && b > -6.5 && b < -5.5) }'; then
        ok "audio $tag: 48 kHz Float32 stereo, tone on channel 1 at -6 dB, channel 0 silent"
    else
        fail "audio $tag: unexpected result"
    fi
    docker rm -f "w-$tag-$$" > /dev/null; remove_dead_flows
}
echo "== 3a. audio Nandi -> Strom"; audio_way nandi strom
echo "== 3b. audio Strom -> Nandi"; audio_way strom nandi

# --- 4. Reader before writer ------------------------------------------------------------------------------------------
echo "== 4. reader (Strom) started 3 s before its writer (Nandi)"
run nandi "$(video_reference /out/early.ref.v210)"
start "r-early-$$" strom "$(video_reader $FRAMES /out/early.v210 40); echo \"reader exit \$?\" > /out/early.rc"
sleep 3
early_state=$(docker inspect -f '{{.State.Status}}' "r-early-$$")
start "w-early-$$" nandi "$(video_writer 400)"
docker wait "r-early-$$" > /dev/null
echo "   reader was '$early_state' when the writer started; $(cat "$OUT/early.rc" 2>/dev/null)"
r=$(frames_match early.v210 early.ref.v210) \
    && ok "reader started before the writer got its frames: $r" \
    || fail "reader started before the writer: $r ($(docker logs "r-early-$$" 2>&1 | tail -n 2 | tr '\n' ' '))"
docker rm -f "w-early-$$" > /dev/null; remove_dead_flows

# --- 5. Writer restart ------------------------------------------------------------------------------------------------
# The mixer's programme is the writer that matters (Strom -> Nandi). A reader stays attached across the restart (finding,
# recorded), and a new reader after the restart must get frames (asserted).
echo "== 5. writer (Strom) killed and started again; reader (Nandi)"
run strom "$(video_reference /out/restart.ref.v210)"
start "w1-restart-$$" strom "$(video_writer 3000)"
wait_flow $FLOW_V 10 || fail "restart: flow did not appear"
start "r-attached-$$" nandi "$(video_reader 200 /out/attached.v210 40); echo \"reader exit \$?\" > /out/attached.rc"
sleep 3
docker kill "w1-restart-$$" > /dev/null
sleep 1
[ "$(flow_field $FLOW_V Active)" = "false" ] && ok "killed writer's flow shows Active: false" \
    || note "killed writer's flow Active is '$(flow_field $FLOW_V Active)'"
t0=$(date +%s.%N)
start "w2-restart-$$" strom "$(video_writer 3000)"
for ((i = 0; i < 50; i++)); do [ "$(flow_field $FLOW_V Active)" = "true" ] && break; sleep 0.2; done
if [ "$(flow_field $FLOW_V Active)" = "true" ]; then
    ok "a new writer took the same flow id without clean-up ($(awk -v a="$(date +%s.%N)" -v b="$t0" 'BEGIN { printf "%.1f", a - b }') s to Active)"
else
    note "a new writer did not take the flow id over a dead one; removing dead flows and trying again"
    docker rm -f "w2-restart-$$" > /dev/null; remove_dead_flows
    start "w3-restart-$$" strom "$(video_writer 3000)"
    wait_flow $FLOW_V 10 || fail "restart: flow did not reappear after clean-up"
fi
run nandi "$(video_reader $FRAMES /out/after.v210 20)" || fail "restart: new reader did not finish"
r=$(frames_match after.v210 restart.ref.v210) && ok "new reader after the restart: $r" || fail "new reader after the restart: $r"
docker wait "r-attached-$$" > /dev/null
attached=$(( $(stat -c %s "$OUT/attached.v210" 2>/dev/null || echo 0) / $(stat -c %s "$OUT/restart.ref.v210") ))
note "reader attached across the restart: $attached of 200 frames, $(cat "$OUT/attached.rc" 2>/dev/null) (124 = still waiting at 40 s)"

exit $RC
