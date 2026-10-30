#!/usr/bin/env bash
#
# Reads the recorded output and reports which source was on air, second by second, then pulls the
# switch decisions and the playlist freeze/resume positions out of the transcoder log.
#
# Identification is by the solid yellow tag box prepare.sh burns into the top right corner of every
# spot. Cropping to exactly that box and testing for saturated yellow separates a spot from anything
# else without needing a reference image.
#
# The test is deliberately narrow rather than "more red than blue": with PRIMARY=playlist the
# programme is the operator's own media, which carries no tag of its own, so any warm-coloured
# corner in it would otherwise read as a spot. A frame is only called AD when that corner is very
# close to pure yellow, which nothing but the burnt-in box is.

set -eu

DEMO_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# A recording of the channel output and the transcoder log to read it against.
#
# The unified runtime does not record by default - it sends the channel to UDP and nothing else -
# so capture a stretch of it first:
#
#   ffmpeg -i 'udp://@:5555?fifo_size=1000000&overrun_nonfatal=1' -c copy -t 300 \
#          /var/tmp/utpl-ad-demo/out/output.ts
#   docker logs skandha-transcoder > /var/tmp/utpl-ad-demo/out/transcoder.log 2>&1
MEDIA_DIR=${MEDIA_DIR:-/var/tmp/utpl-ad-demo}

OUTPUT=${OUTPUT:-${MEDIA_DIR}/out/output.ts}
LOG=${LOG:-${MEDIA_DIR}/out/transcoder.log}

# Same geometry prepare.sh draws: 100x100 at x=1140, y=40 in a 1280x720 frame. Sampled a little
# inside the edges so encoding softness at the border cannot bleed in.
TAG_CROP="crop=80:80:1150:50"

if [ ! -s "${OUTPUT}" ]; then
    echo "No recorded output at ${OUTPUT} - see the capture command at the top of this script" >&2
    exit 1
fi

duration=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "${OUTPUT}" | cut -d. -f1)

echo "Recorded output: ${duration}s"

# One decode pass: scale each frame's tag box down to a single pixel and dump it raw, one RGB
# triplet per sampled second. Far faster than seeking per sample, and it cannot miss a frame.
mapfile -t samples < <(
    ffmpeg -v error -i "${OUTPUT}" \
        -vf "fps=1,${TAG_CROP},scale=1:1" -f rawvideo -pix_fmt rgb24 - 2>/dev/null \
    | od -An -tu1 -w3 -v | tr -s ' ' | sed 's/^ //'
)

strip=""
declare -a state=()

for sample in "${samples[@]}"; do
    r=$(echo "${sample}" | cut -d' ' -f1)
    g=$(echo "${sample}" | cut -d' ' -f2)
    b=$(echo "${sample}" | cut -d' ' -f3)

    if [ "${r}" -lt 40 ] && [ "${g}" -lt 40 ] && [ "${b}" -lt 40 ]; then
        # No tag at all: the output is black, which means neither source reached the encoder.
        state+=("BLACK")
        strip="${strip}."
    elif [ "${r}" -gt 170 ] && [ "${g}" -gt 170 ] && [ "${b}" -lt 90 ]; then
        state+=("AD")
        strip="${strip}A"
    else
        state+=("PROG")
        strip="${strip}P"
    fi
done

echo
echo "On air, one character per second (P = programme, A = spot, . = black):"
echo

for (( offset = 0; offset < ${#strip}; offset += 60 )); do
    printf "  %4ss  %s\n" "${offset}" "${strip:offset:60}"
done

echo
echo "Transitions detected in the output:"

previous=""

for (( i = 0; i < ${#state[@]}; i++ )); do
    if [ "${state[$i]}" != "${previous}" ]; then
        if [ -n "${previous}" ]; then
            printf "  %4ss  %s -> %s\n" "${i}" "${previous}" "${state[$i]}"
        fi

        previous="${state[$i]}"
    fi
done

if [ -s "${LOG}" ]; then
    echo
    echo "Switch decisions:"
    grep -E "Ad break is (starting|ending)|Ad break [0-9]+ is starting|Programme is back on air" "${LOG}" \
        | sed 's/^/  /' || true

    echo
    echo "Playlist freeze / resume positions:"
    grep -E "froze: bin|resumed at videoPos" "${LOG}" | sed 's/^/  /' || true
fi
