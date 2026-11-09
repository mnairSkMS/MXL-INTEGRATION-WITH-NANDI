#!/usr/bin/env bash
# How much the writer's lag moves, which is what an MXL input's delay (transport.mxl.delayMs) has to cover.
# Channel A takes the source and publishes its programme as MXL flows; channel B reads them. B's engine log
# says, once a second and for each flow, how much room the flow's buffers had before the time at which B is due to
# play them: the least and the most. The difference between the most and the least over a run is the movement of
# the writer's lag, the delay the input needs. B is given a long delay (1000 ms) so that nothing is late and the
# measurement does not disturb itself.
#
# Three conditions, each for a while: 720p25 on an idle host; 720p25 with every core kept busy by other
# processes; 1080p25 on an idle host. The numbers are printed; nothing is judged.
#
#   tests/manual/mxl/input-jitter.sh [seconds per condition] [image]     default: 120 s, utpl/transcoder-test:mxl
#   MXL_ENGINE_BUILD_DIR_HOST=/var/tmp/utpl-build-mxl/rwdi tests/manual/mxl/input-jitter.sh 60
#       runs an engine built in a scratch directory instead of the one in the image

if [ "$1" = "--inside" ]; then
    JITTER_SECONDS="${JITTER_SECONDS:-120}"
    . /tests/framework/mxl.sh
    mxl_test_begin /tmp/jitter
    mxl_require_engine
    mxl_require mxlsink mxlsrc mxl-info

    # run_condition <label> <size> <busy processes>
    run_condition() {
        local label="$1" size="$2" busy="$3" busy_pids=() i
        export MXL_OUT_SIZE=$size
        MXL_DOMAIN="$MXL_TMP/domain-${label// /_}"; mkdir -p "$MXL_DOMAIN"
        local w=${size%x*} h=${size#*x}

        MXL_OUT_PORT=4445 MXL_EXTRA_OUTPUT="$(mxl_programme_json $w $h)" mxl_write_config "$MXL_TMP/a-${label// /_}.json" ""
        MXL_INPUT_TRANSPORT="$(MXL_INPUT_DELAY_MS=1000 mxl_input_json)" mxl_write_config "$MXL_TMP/b-${label// /_}.json" ""
        mxl_start_source tone "$size"
        local src=$MXL_SRC_PID
        mxl_start_engine "$MXL_TMP/a-${label// /_}.json" "a-${label// /_}"
        local pid_a=$MXL_ENGINE_PID
        mxl_wait_flow "$MXL_FLOW_VIDEO" 40 || mxl_abort "$label: channel A's video flow did not appear"
        mxl_wait_flow "$MXL_FLOW_AUDIO" 40 || mxl_abort "$label: channel A's audio flow did not appear"
        mxl_start_engine "$MXL_TMP/b-${label// /_}.json" "b-${label// /_}"
        local pid_b=$MXL_ENGINE_PID

        for ((i = 0; i < busy; i++)); do
            ( while :; do :; done ) &
            busy_pids+=($!)
        done

        sleep 20      # startup
        local log="$MXL_TMP/engine-b-${label// /_}.log" mark
        mark=$(wc -l < "$log")
        sleep "$JITTER_SECONDS"

        tail -n +"$((mark + 1))" "$log" | grep -o "MXL input '[^']*': the [a-z]* flow's buffers had -\?[0-9]* to -\?[0-9]* ms to spare" \
            | sed -E "s/.*the ([a-z]+) flow's buffers had (-?[0-9]+) to (-?[0-9]+) ms.*/\1 \2 \3/" > "$MXL_TMP/samples-${label// /_}.txt"

        for kind in video audio; do
            awk -v k="$kind" -v label="$label" '$1 == k { n++; if (min == "" || $2 < min) min = $2; if (max == "" || $3 > max) max = $3; if ($2 < 0) late++ ;
                    # the seconds in which the spread was widest
                    s = $3 - $2; if (s > w) w = s }
                END { if (n == 0) { printf "RESULT %s, %s flow: no samples\n", label, k; exit }
                      printf "RESULT %s, %s flow: %d s sampled; room over the run %d to %d ms, so the lag moved %d ms; widest single second %d ms\n", label, k, n, min, max, max - min, w }' "$MXL_TMP/samples-${label// /_}.txt"
        done

        # The delay that would have kept every buffer on time: what B was given less the least room either flow had.
        awk -v label="$label" '{ if (min == "" || $2 < min) min = $2 } END { if (min != "") printf "RESULT %s: a delay of %d ms would have kept every buffer of both flows on time (1000 ms less the least room, %d ms)\n", label, 1000 - min, min }' "$MXL_TMP/samples-${label// /_}.txt"

        local reattached
        reattached=$(grep -c 'no data on the .* flow' "$log" || true)
        echo "RESULT $label: B reattached a flow ${reattached} times"

        for p in "${busy_pids[@]}"; do kill "$p" 2>/dev/null; done
        mxl_stop "$pid_b"; mxl_stop "$pid_a"; mxl_stop "$src"
        sleep 2
    }

    run_condition "720p25 idle host" 1280x720 0
    run_condition "720p25 all cores busy" 1280x720 "$(nproc)"
    run_condition "1080p25 idle host" 1920x1080 0

    echo "samples kept in /tmp/jitter/tmp inside the container; copied to ${JITTER_OUT:-nowhere}"
    [ -n "$JITTER_OUT" ] && cp "$MXL_TMP"/samples-*.txt "$JITTER_OUT/"
    mxl_test_end
fi

IMAGE="${2:-utpl/transcoder-test:mxl}"
ROOT=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )/../../.." &> /dev/null && pwd )
OUT=$(mktemp -d)
extra=()
if [ -n "$MXL_ENGINE_BUILD_DIR_HOST" ]; then
    extra=(-e MXL_ENGINE_BUILD_DIR=/dev-build -v "$MXL_ENGINE_BUILD_DIR_HOST:/dev-build:ro")
fi
# Environment for the engines (for example UTPL_EXPERIMENT_H264_THREADS=1), passed as JITTER_ENV="NAME=value NAME=value".
for kv in $JITTER_ENV; do extra+=(-e "$kv"); done
docker run --rm --user "$(id -u):$(id -g)" --entrypoint bash "${extra[@]}" \
    -e JITTER_SECONDS="${1:-120}" -e JITTER_OUT=/out -v "$OUT:/out" -v "$ROOT/tests:/tests" "$IMAGE" \
    -c 'bash /tests/manual/mxl/input-jitter.sh --inside'
status=$?
echo "samples: $OUT"
exit $status
