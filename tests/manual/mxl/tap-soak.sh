#!/usr/bin/env bash
# Long run of the input MXL tap, to see what a short test cannot: drift of the flows' timeline against
# the system clock, drift of lip-sync, memory growth, and whether the tap ever restarts or fails.
# The channel is the one of the tap tests, fed by the flash-and-beep source.
#
# Every minute it records how far the head of each flow is behind the wall clock (the mean of ten
# samples at different phases, because the head moves in whole frames), the engine's memory and CPU, and
# the number of tap starts and errors in the engine log. At the start, in the middle and at the end it
# also measures the flash-to-beep offset between the flows. The summary gives each trend as a slope in
# ms per hour. A flat line is what is wanted; the limits are not agreed (Gate A), so nothing is judged
# except hard faults: a tap that restarted or failed, a flow that stopped, an engine that died.
#
#   tests/manual/mxl/tap-soak.sh [minutes] [image]     default: 20 minutes, utpl/transcoder-test:mxl
#   MXL_ENGINE_BUILD_DIR_HOST=/var/tmp/utpl-build-mxl/rwdi tests/manual/mxl/tap-soak.sh 5
#       runs an engine built in a scratch directory instead of the one in the image

if [ "$1" = "--inside" ]; then
    SOAK_MINUTES="${SOAK_MINUTES:-20}"
    . /tests/framework/mxl.sh
    mxl_test_begin /tmp/soak
    mxl_require_engine
    mxl_require mxlsink mxlsrc mxl-info videoanalyse level

    mean_lag() {  # the mean over ten samples taken at different phases of the frame
        local flow="$1" sum=0 n=0 v
        for i in 1 2 3 4 5 6 7 8 9 10; do
            v=$(mxl_head_lag_ms "$flow")
            sum=$(awk -v s="$sum" -v v="$v" 'BEGIN { print s + v }'); n=$((n + 1))
            sleep 0.0$(( (i * 7) % 10 + 1 ))
        done
        awk -v s="$sum" -v n="$n" 'BEGIN { printf "%.1f", s / n }'
    }

    lipsync() {  # median flash-minus-beep in ms through the flows
        mxl_lipsync_flows 14 "$MXL_TMP/lip.log"
        local flash beep
        flash=$(sed -n 1p "$MXL_TMP/lip.log.onsets"); beep=$(sed -n 2p "$MXL_TMP/lip.log.onsets")
        mxl_lipsync_pairs "$flash" "$beep"
    }

    mxl_write_config "$MXL_TMP/config.json" "$(mxl_tap_json)"
    mxl_start_source sync
    mxl_start_engine "$MXL_TMP/config.json"
    pid=$MXL_ENGINE_PID

    mxl_wait_flow "$MXL_FLOW_VIDEO" 40 || mxl_abort "the video flow did not appear"
    mxl_wait_flow "$MXL_FLOW_AUDIO" 40 || mxl_abort "the audio flow did not appear"
    sleep 20      # the first seconds are startup

    series="$MXL_TMP/series.tsv"
    printf 'minute\tvideo_lag_ms\taudio_lag_ms\trss_mb\tcpu_cores\ttap_starts\ttap_errors\n' | tee "$series"

    read -r lip0 n0 <<< "$(lipsync)"
    echo "RESULT lip-sync at the start: ${lip0} ms (${n0} pairs)"

    cpu_prev=$(mxl_cpu_seconds "$pid"); t_prev=$(date +%s.%N)
    mid_done=0
    for ((m = 1; m <= SOAK_MINUTES; m++)); do
        sleep 45
        vlag=$(mean_lag "$MXL_FLOW_VIDEO"); alag=$(mean_lag "$MXL_FLOW_AUDIO")
        rss=$(awk '/VmRSS/ { printf "%.1f", $2 / 1024 }' "/proc/$pid/status" 2>/dev/null)
        cpu_now=$(mxl_cpu_seconds "$pid"); t_now=$(date +%s.%N)
        cores=$(awk -v a="$cpu_prev" -v b="$cpu_now" -v t0="$t_prev" -v t1="$t_now" 'BEGIN { printf "%.2f", (b - a) / (t1 - t0) }')
        cpu_prev=$cpu_now; t_prev=$t_now
        starts=$(grep -Ec 'MXL tap [0-9a-f-]+ \((video|audio)\) started' "$MXL_TMP/engine.log")
        errors=$(grep -c 'side pipeline error' "$MXL_TMP/engine.log" || true)
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$m" "$vlag" "$alag" "$rss" "$cores" "$starts" "$errors" | tee -a "$series"
        mxl_alive "$pid" || { mxl_fail "the engine died at minute $m"; break; }
        if [ "$mid_done" = 0 ] && [ "$m" -ge $((SOAK_MINUTES / 2)) ]; then
            read -r lipm nm <<< "$(lipsync)"
            echo "RESULT lip-sync in the middle (minute $m): ${lipm} ms (${nm} pairs)"
            mid_done=1
        fi
    done

    read -r lip1 n1 <<< "$(lipsync)"
    echo "RESULT lip-sync at the end: ${lip1} ms (${n1} pairs)"

    # Slope of a column in units per hour, by least squares over the minutes.
    slope() {
        awk -F'\t' -v c="$1" 'NR > 1 { x[NR] = $1; y[NR] = $c; sx += $1; sy += $c; sxx += $1 * $1; sxy += $1 * $c; n++ }
            END { if (n < 2) { print "n/a"; exit } d = n * sxx - sx * sx; printf "%.1f", 60 * (n * sxy - sx * sy) / d }' "$series"
    }
    echo "RESULT video flow lag trend:   $(slope 2) ms per hour (mean $(awk -F'\t' 'NR > 1 { s += $2; n++ } END { printf "%.1f", s / n }' "$series") ms)"
    echo "RESULT audio flow lag trend:   $(slope 3) ms per hour (mean $(awk -F'\t' 'NR > 1 { s += $3; n++ } END { printf "%.1f", s / n }' "$series") ms)"
    echo "RESULT engine memory trend:    $(slope 4) MB per hour (now $(awk -F'\t' 'END { print $4 }' "$series") MB)"
    echo "RESULT engine CPU:             mean $(awk -F'\t' 'NR > 1 { s += $5; n++ } END { printf "%.2f", s / n }' "$series") cores"
    echo "RESULT tap starts / errors:    $(awk -F'\t' 'END { print $6 " / " $7 }' "$series") (2 starts and 0 errors are what a clean run shows)"

    last_starts=$(awk -F'\t' 'END { print $6 }' "$series"); last_errors=$(awk -F'\t' 'END { print $7 }' "$series")
    [ "$last_starts" = "2" ] || mxl_fail "the taps started $last_starts times; a clean run starts them once each"
    [ "$last_errors" = "0" ] || mxl_fail "the tap reported $last_errors side pipeline errors"
    [ "${n0:-0}" -ge 5 ] && [ "${n1:-0}" -ge 5 ] || mxl_fail "lip-sync could not be measured at the start or the end"
    echo "series kept in /tmp/soak/tmp/series.tsv inside the container; copied to ${SOAK_OUT:-nowhere}"
    [ -n "$SOAK_OUT" ] && cp "$series" "$SOAK_OUT/tap-soak-series.tsv"
    mxl_test_end
fi

IMAGE="${2:-utpl/transcoder-test:mxl}"
ROOT=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )/../../.." &> /dev/null && pwd )
OUT=$(mktemp -d)
extra=()
if [ -n "$MXL_ENGINE_BUILD_DIR_HOST" ]; then
    extra=(-e MXL_ENGINE_BUILD_DIR=/dev-build -v "$MXL_ENGINE_BUILD_DIR_HOST:/dev-build:ro")
fi
docker run --rm --user "$(id -u):$(id -g)" --entrypoint bash "${extra[@]}" \
    -e SOAK_MINUTES="${1:-20}" -e SOAK_OUT=/out -v "$OUT:/out" -v "$ROOT/tests:/tests" "$IMAGE" \
    -c 'bash /tests/manual/mxl/tap-soak.sh --inside'
status=$?
echo "series: $OUT/tap-soak-series.tsv"
exit $status
