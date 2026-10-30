#!/usr/bin/env bash
#
# Drives N ad breaks against the running unified runtime and reports what actually aired.
#
#   ./breaks.sh [count] [breakSeconds] [gapSeconds]
#
# Why this exists: the defect is that the *second* and later breaks render black, so a single
# break proves nothing. This posts a sequence, records the engine's own output, and lines the
# black periods up against the switch decisions - both on the transcoder's clock, because the
# measured file is written by the channel itself rather than by the re-encoded host preview.
#
# Requires the measurement overlay, which adds file:///runtime/probe.ts to the output:
#
#   docker compose -f docker-compose.playlist.yml \
#                  -f tests/manual/ad-insertion/measure/compose.measure.yml up -d --force-recreate

set -eu

COUNT=${1:-3}
BREAK=${2:-20}
GAP=${3:-15}

API=${API:-http://127.0.0.1:8080}
CONTAINER=${CONTAINER:-skandha-transcoder}
PROBE=${PROBE:-runtime/probe.ts}
REPO=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )/../../../.." &> /dev/null && pwd )
WORK=${WORK:-/var/tmp/utpl-measure}

mkdir -p "${WORK}"

if [ ! -e "${REPO}/${PROBE}" ]; then
    echo "No ${REPO}/${PROBE} - start the stack with the measurement overlay first" >&2
    exit 1
fi

# The probe file is appended to for the life of the channel. Note where it is now, so only this
# run's bytes are analysed, and note the wall clock at that point to convert file time to log time.
startSize=$(stat -c %s "${REPO}/${PROBE}")
startEpoch=$(date -u '+%s.%N')

echo "Posting ${COUNT} breaks of ${BREAK}s, ${GAP}s apart"

at=5
eventId=$(( $(date -u +%s) % 100000 ))

for i in $(seq 1 ${COUNT}); do
    out=$(date -u -d "+${at} seconds" '+%Y-%m-%dT%H:%M:%S.000Z')
    in=$(date -u -d "+$(( at + BREAK )) seconds" '+%Y-%m-%dT%H:%M:%S.000Z')

    curl -sf -X POST "${API}/scheduled_events" -H 'Content-Type: application/json' -d "[
      {\"type\":\"scte35_spliceinsert\",\"time\":\"${out}\",
       \"details\":{\"scte35_spliceinsert\":{\"splice_event_id\":${eventId},\"duration\":$(( BREAK * 1000 ))}}},
      {\"type\":\"scte35_returntonetwork\",\"time\":\"${in}\",
       \"details\":{\"scte35_returntonetwork\":{\"splice_event_id\":${eventId}}}}]" > /dev/null

    printf '  break %d: cue-out +%ds, cue-in +%ds, id %d\n' "${i}" "${at}" "$(( at + BREAK ))" "${eventId}"

    at=$(( at + BREAK + GAP ))
    eventId=$(( eventId + 1 ))
done

total=$(( at + 10 ))

echo "Waiting ${total}s for the sequence to finish"
sleep ${total}

# Only this run's bytes. Copied rather than analysed in place, because the channel is still writing.
tail -c +$(( startSize + 1 )) "${REPO}/${PROBE}" > "${WORK}/run.ts"

echo
echo "=== black in the channel's own output ==="

# d=0.1: shorter than one 2 s GOP, so a single dropped frame at a switch is still visible here.
mapfile -t blacks < <(
    ffmpeg -v info -i "${WORK}/run.ts" -vf blackdetect=d=0.1:pix_th=0.10 -an -f null - 2>&1 \
    | grep -oE "black_start:[0-9.]+ black_end:[0-9.]+ black_duration:[0-9.]+"
)

if [ ${#blacks[@]} -eq 0 ]; then
    echo "  none"
else
    printf '  %s\n' "${blacks[@]}"
fi

echo
echo "=== switch decisions and item commits (file time, same clock) ==="

# Log timestamps are wall clock; the file starts at startEpoch. Subtracting gives a position in
# the recording, so a black period and the decision that caused it can be compared directly.
docker logs --since "${total}s" "${CONTAINER}" 2>&1 \
    | grep -E "Ad break|Programme is back on air|Committed playlist item" \
    | sed -E 's/^\[ [0-9/]+ ([0-9:.]+) \].*(Ad break [0-9]+ is starting|Ad break is ending|Programme is back on air|Committed playlist item ([0-9a-f-]+) at index ([0-9]+)).*/\1|\2/' \
    | while IFS='|' read -r stamp what; do
        [ -z "${what:-}" ] && continue
        eventEpoch=$(date -u -d "$(date -u '+%Y-%m-%d') ${stamp}" '+%s.%N' 2>/dev/null) || continue
        printf '  %8.2fs  %s\n' "$(echo "${eventEpoch} - ${startEpoch}" | bc)" "${what}"
    done

echo
echo "Recording kept at ${WORK}/run.ts"
