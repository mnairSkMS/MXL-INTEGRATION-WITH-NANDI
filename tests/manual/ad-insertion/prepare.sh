#!/usr/bin/env bash
#
# Builds synthetic demo media: labelled commercials, and a programme carrying real SCTE-35 cues.
#
# The unified runtime plays the operator's own media, declared in config/*.yml, and needs none of
# this. Use this when you want media that makes the switching legible - each spot carries a burnt-in
# counter, so an interrupted spot shows exactly where it was cut - or a programme with cues inside
# the transport stream rather than cues sent from the operator UI.
#
# Runs inside utpl/transcoder-test (ffmpeg + TSDuck):
#
#   docker run --rm -v $PWD:/demo:ro -v /var/tmp/utpl-ad-demo:/demo-media \
#       -e MEDIA_DIR=/demo-media utpl/transcoder-test:latest bash /demo/prepare.sh
#
# Copy the results into media/queue and name them in a channel YAML to put them on air.
#
#   media/programme.ts   live-style programme with SCTE-35 cues on PID 600
#   media/spot-N.ts      commercials cut from media/source.mp4
#   media/spots.m3u8     the ad playlist the transcoder loads as its second input
#   scte.xml             generated from BREAKS below, so the cues and this schedule cannot drift
#
# All of the above are written to MEDIA_DIR (default /demo-media), which is mounted from outside
# the repository. Nothing generated is written back into the checkout.
#
# Each spot carries a burnt-in "SPOT n" label and an elapsed-time readout, so the point at which a
# spot is interrupted and the point at which it comes back are both readable off the picture. These
# are encoded into the media here, once - the transcoder draws nothing at runtime.
#
# LABELS=0 builds clean spots instead, for a demo that should look like real playout. The trade-off
# is that verify.sh then has no tag box to read and cannot tell programme from spot, and the resume
# behaviour stops being visible on the picture.

set -eu

# /demo is this script's directory (read-only, in the repo); /demo-media is a scratch directory
# mounted from outside the tree. Everything generated goes to the latter - the repository must not
# accumulate media.
DEMO_DIR=${DEMO_DIR:-/demo}
MEDIA_DIR=${MEDIA_DIR:-/demo-media}

# Default: one reel long enough to outlast the whole run, so the playlist never reaches the end of
# an item. That matters - a break that overlaps an item rollover renders black (see the README), and
# it is not a switching problem. Set SPOT_COUNT=4 SPOT_DURATION=14 to exercise that defect instead.
SPOT_COUNT=${SPOT_COUNT:-1}
SPOT_DURATION=${SPOT_DURATION:-330}
PROGRAMME_DURATION=${PROGRAMME_DURATION:-300}

# Break schedule, "cueOut:cueIn" in seconds of programme time.
BREAKS=${BREAKS:-"25:40 70:85 115:132 160:172 200:218 245:262"}

LABELS=${LABELS:-1}

FONT=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf
SCTE_PID=600

if [ ! -e "${MEDIA_DIR}/source.mp4" ]; then
    echo "Missing ${MEDIA_DIR}/source.mp4 - see README.md for the yt-dlp command" >&2
    exit 1
fi

echo "==> Cutting ${SPOT_COUNT} spots of ${SPOT_DURATION}s from source.mp4"

sourceDuration=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "${MEDIA_DIR}/source.mp4" \
                 | cut -d. -f1)

for i in $(seq 1 ${SPOT_COUNT}); do
    start=$(( (i - 1) * SPOT_DURATION ))

    # A spot longer than the source repeats it. That is how the reel is made long enough to outlast
    # every break in the run without the playlist ever reaching the end of an item - see the
    # rollover defect in the README.
    loop=""

    if [ $(( start + SPOT_DURATION )) -gt ${sourceDuration} ]; then
        loop="-stream_loop -1"
        start=0
    fi

    # -ss before -i seeks on the input, so %{pts} restarts at 0 for every spot and the readout is
    # the position *within the spot*, which is exactly what resume has to preserve.
    if [ "${LABELS}" = "1" ]; then
        filter="scale=1280:720,drawbox=x=1140:y=40:w=100:h=100:color=yellow@1:t=fill,drawtext=fontfile=${FONT}:text='SPOT ${i}':x=40:y=40:fontsize=48:fontcolor=yellow:box=1:boxcolor=black@1:boxborderw=12,drawtext=fontfile=${FONT}:text='%{pts\\:hms}':x=40:y=110:fontsize=48:fontcolor=yellow:box=1:boxcolor=black@1:boxborderw=12"
    else
        filter="scale=1280:720"
    fi

    ffmpeg -y -v error \
        ${loop} -ss ${start} -t ${SPOT_DURATION} -i "${MEDIA_DIR}/source.mp4" \
        -vf "${filter}" \
        -r 25 -vcodec h264 -preset veryfast -b:v 4000k -g 25 -keyint_min 25 -sc_threshold 0 \
        -acodec mp2 -ar 44100 -ac 2 \
        -muxdelay 0 -muxpreload 0 \
        "${MEDIA_DIR}/spot-${i}.ts"
done

echo "==> Writing spots.m3u8"

{
    echo "#EXTM3U"

    for i in $(seq 1 ${SPOT_COUNT}); do
        echo "#EXTINF:${SPOT_DURATION},spot-${i}"
        echo "file://${MEDIA_DIR}/spot-${i}.ts"
    done
} > "${MEDIA_DIR}/spots.m3u8"

# PRIMARY=playlist sets this: that programme is the operator's own media, so the generated one and
# its embedded cues are not built at all.
if [ "${SKIP_PROGRAMME:-0}" = "1" ]; then
    echo "==> Skipping the generated programme (SKIP_PROGRAMME=1)"
    echo "==> Media ready in ${MEDIA_DIR}"
    exit 0
fi

echo "==> Building the ${PROGRAMME_DURATION}s programme"

# A moving pattern plus a running clock: the clock makes it obvious that the programme keeps
# running underneath a break, and that the return lands at live "now" rather than where it left.
ffmpeg -y -v error \
    -f lavfi -t ${PROGRAMME_DURATION} -i "testsrc2=size=1280x720:rate=25" \
    -f lavfi -t ${PROGRAMME_DURATION} -i "sine=frequency=500:sample_rate=44100" \
    -vf "drawbox=x=1140:y=40:w=100:h=100:color=blue@1:t=fill,drawtext=fontfile=${FONT}:text='LIVE PROGRAMME':x=40:y=40:fontsize=48:fontcolor=white:box=1:boxcolor=black@1:boxborderw=12,drawtext=fontfile=${FONT}:text='%{pts\\:hms}':x=40:y=110:fontsize=48:fontcolor=white:box=1:boxcolor=black@1:boxborderw=12" \
    -vcodec h264 -preset veryfast -b:v 4000k -g 25 \
    -acodec mp2 -ar 44100 -ac 2 \
    -muxdelay 0 -muxpreload 0 \
    "${MEDIA_DIR}/programme.src.ts"

echo "==> Writing scte.xml for breaks: ${BREAKS}"

# pts_time is in 90 kHz ticks and the programme starts at PTS 0, so tick = second * 90000.
{
    echo '<?xml version="1.0" encoding="UTF-8"?>'
    echo '<!-- Generated by prepare.sh from BREAKS. Edit BREAKS, not this file. -->'
    echo '<tsduck>'

    eventId=201

    for pair in ${BREAKS}; do
        out=${pair%%:*}
        in=${pair##*:}

        printf '    <splice_information_table>\n'
        printf '        <splice_insert splice_event_id="%d" unique_program_id="1" out_of_network="true"  pts_time="%d"/>\n' \
            ${eventId} $(( out * 90000 ))
        printf '    </splice_information_table>\n'
        printf '    <splice_information_table>\n'
        printf '        <splice_insert splice_event_id="%d" unique_program_id="1" out_of_network="false" pts_time="%d"/>\n' \
            ${eventId} $(( in * 90000 ))
        printf '    </splice_information_table>\n'

        eventId=$(( eventId + 1 ))
    done

    echo '</tsduck>'
} > "${MEDIA_DIR}/scte.xml"

echo "==> Injecting SCTE-35 cues on PID ${SCTE_PID}"

# --max-file-size matters: spliceinject silently *ignores* a polled XML file over its 2048-byte
# default, and --wait-first-batch then blocks forever waiting for sections that never arrive. Six
# breaks already exceed that limit.

# Same TSDuck recipe the shell test suite uses: declare the SCTE PID in the PMT with the CUEI
# registration, then inject the sections from scte.xml at their signalled PTS.
tsp \
    --add-input-stuffing 1/10 \
    -I file "${MEDIA_DIR}/programme.src.ts" \
    -P pmt \
        --service 1 \
        --add-programinfo-id 0x43554549 \
        --add-pid ${SCTE_PID}/0x86 \
    -P spliceinject \
        --inject-count 1 \
        --service 1 \
        --start-delay 2000 \
        --files "${MEDIA_DIR}/scte.xml" \
        --min-bitrate 1024 \
        --wait-first-batch \
        --max-file-size 65536 \
    -P filter --negate --pid 0x1FFF \
    -O file "${MEDIA_DIR}/programme.ts"

rm -f "${MEDIA_DIR}/programme.src.ts"

echo "==> Cue sections present in the programme:"
tsp -I file "${MEDIA_DIR}/programme.ts" -P splicemonitor -a -d -j -s ${SCTE_PID} -O drop 2>&1 \
    | grep -E '"command"|"pts"|out_of_network' | head -20

echo "==> Media ready in ${MEDIA_DIR}"
ls -la "${MEDIA_DIR}"
