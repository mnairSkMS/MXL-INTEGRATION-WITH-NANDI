#!/usr/bin/env bash
# Long run of the MXL input, to see what a short test cannot: drift of the flows' timeline against the channel's
# clock, drift of lip-sync, memory growth, and whether the reader ever has to be reattached. Channel A takes the
# flash-and-beep source and publishes its programme as MXL flows; channel B reads them and encodes them. A is the
# writer and only the means; B is measured.
#
# Every minute it records B's memory and CPU, A's memory, how many times B reattached a flow, how many times A's
# taps started and the number of errors in both logs. At the start, in the middle and at the end it measures the
# flash-to-beep offset in B's output, next to A's on-air output. The summary gives each trend as a slope per hour. A flat
# line is what is wanted; the limits are not agreed (Gate A), so nothing is judged except hard faults: a reattach,
# a restart, an engine that died.
#
#   tests/manual/mxl/input-soak.sh [minutes] [image]     default: 20 minutes, utpl/transcoder-test:mxl
#   MXL_ENGINE_BUILD_DIR_HOST=/var/tmp/utpl-build-mxl/rwdi tests/manual/mxl/input-soak.sh 5
#       runs an engine built in a scratch directory instead of the one in the image

if [ "$1" = "--inside" ]; then
    SOAK_MINUTES="${SOAK_MINUTES:-20}"
    . /tests/framework/mxl.sh
    mxl_test_begin /tmp/soak
    mxl_require_engine
    mxl_require mxlsink mxlsrc mxl-info videoanalyse level

    # lipsync <ts file>: B's output is recorded for 14 s and the median flash-minus-beep is printed in ms
    lipsync() {
        timeout 30 ffmpeg -v error -y -i "udp://127.0.0.1:${MXL_PORT_OUT}?fifo_size=5000000&overrun_nonfatal=1" -t 14 -c copy "$1" >> $MXL_LOG 2>&1
        mxl_lipsync_file "$1"
    }

    MXL_OUT_PORT=4445 MXL_EXTRA_OUTPUT="$(mxl_programme_json)" mxl_write_config "$MXL_TMP/a.json" ""
    MXL_INPUT_TRANSPORT="$(mxl_input_json)" mxl_write_config "$MXL_TMP/b.json" ""
    mxl_start_source sync
    mxl_start_engine "$MXL_TMP/a.json" a
    pid_a=$MXL_ENGINE_PID
    mxl_wait_flow "$MXL_FLOW_VIDEO" 40 || mxl_abort "channel A's video flow did not appear"
    mxl_wait_flow "$MXL_FLOW_AUDIO" 40 || mxl_abort "channel A's audio flow did not appear"
    mxl_start_engine "$MXL_TMP/b.json" b
    pid_b=$MXL_ENGINE_PID
    sleep 30      # the first seconds are startup

    series="$MXL_TMP/series.tsv"
    printf 'minute\tb_rss_mb\ta_rss_mb\tb_cpu_cores\ta_cpu_cores\tb_reattaches\ta_tap_starts\terrors\n' | tee "$series"

    read -r lip0 n0 <<< "$(lipsync "$MXL_TMP/start.ts")"
    echo "RESULT lip-sync of B's output at the start: ${lip0} ms (${n0} pairs)"

    cpu_b_prev=$(mxl_cpu_seconds "$pid_b"); cpu_a_prev=$(mxl_cpu_seconds "$pid_a"); t_prev=$(date +%s.%N)
    mid_done=0
    for ((m = 1; m <= SOAK_MINUTES; m++)); do
        sleep 58
        rss_b=$(awk '/VmRSS/ { printf "%.1f", $2 / 1024 }' "/proc/$pid_b/status" 2>/dev/null)
        rss_a=$(awk '/VmRSS/ { printf "%.1f", $2 / 1024 }' "/proc/$pid_a/status" 2>/dev/null)
        cpu_b=$(mxl_cpu_seconds "$pid_b"); cpu_a=$(mxl_cpu_seconds "$pid_a"); t_now=$(date +%s.%N)
        cores_b=$(awk -v a="$cpu_b_prev" -v b="$cpu_b" -v t0="$t_prev" -v t1="$t_now" 'BEGIN { printf "%.2f", (b - a) / (t1 - t0) }')
        cores_a=$(awk -v a="$cpu_a_prev" -v b="$cpu_a" -v t0="$t_prev" -v t1="$t_now" 'BEGIN { printf "%.2f", (b - a) / (t1 - t0) }')
        cpu_b_prev=$cpu_b; cpu_a_prev=$cpu_a; t_prev=$t_now
        reattach=$(grep -c 'no data on the .* flow' "$MXL_TMP/engine-b.log" || true)
        starts=$(grep -Ec 'MXL tap [0-9a-f-]+ \((video|audio)\) started' "$MXL_TMP/engine-a.log" || true)
        errors=$( { grep -h 'side pipeline error\|Received an error message' "$MXL_TMP/engine-a.log" "$MXL_TMP/engine-b.log" || true; } | wc -l)
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$m" "$rss_b" "$rss_a" "$cores_b" "$cores_a" "$reattach" "$starts" "$errors" | tee -a "$series"
        mxl_alive "$pid_a" || { mxl_fail "channel A died at minute $m"; break; }
        mxl_alive "$pid_b" || { mxl_fail "channel B died at minute $m"; break; }
        if [ "$mid_done" = 0 ] && [ "$m" -ge $((SOAK_MINUTES / 2)) ]; then
            read -r lipm nm <<< "$(lipsync "$MXL_TMP/middle.ts")"
            echo "RESULT lip-sync of B's output in the middle (minute $m): ${lipm} ms (${nm} pairs)"
            mid_done=1
        fi
    done

    read -r lip1 n1 <<< "$(lipsync "$MXL_TMP/end.ts")"
    echo "RESULT lip-sync of B's output at the end: ${lip1} ms (${n1} pairs)"

    # Slope of a column in units per hour, by least squares over the minutes.
    slope() {
        awk -F'\t' -v c="$1" 'NR > 1 { sx += $1; sy += $c; sxx += $1 * $1; sxy += $1 * $c; n++ }
            END { if (n < 2) { print "n/a"; exit } d = n * sxx - sx * sx; printf "%.1f", 60 * (n * sxy - sx * sy) / d }' "$series"
    }
    mean() { awk -F'\t' -v c="$1" 'NR > 1 { s += $c; n++ } END { printf "%.2f", s / n }' "$series"; }
    echo "RESULT channel B memory trend: $(slope 2) MB per hour (now $(awk -F'\t' 'END { print $2 }' "$series") MB)"
    echo "RESULT channel A memory trend: $(slope 3) MB per hour (now $(awk -F'\t' 'END { print $3 }' "$series") MB)"
    echo "RESULT channel B CPU: mean $(mean 4) cores; channel A CPU: mean $(mean 5) cores"
    echo "RESULT reattaches in B / tap starts in A / errors: $(awk -F'\t' 'END { print $6 " / " $7 " / " $8 }' "$series") (0 / 2 / 0 are what a clean run shows)"

    [ "$(awk -F'\t' 'END { print $6 }' "$series")" = "0" ] || mxl_fail "channel B had to reattach a flow"
    [ "$(awk -F'\t' 'END { print $7 }' "$series")" = "2" ] || mxl_fail "channel A's taps did not start exactly once each"
    [ "$(awk -F'\t' 'END { print $8 }' "$series")" = "0" ] || mxl_fail "the engines reported errors"
    [ "${n0:-0}" -ge 5 ] && [ "${n1:-0}" -ge 5 ] || mxl_fail "lip-sync could not be measured at the start or the end"
    echo "series kept in /tmp/soak/tmp/series.tsv inside the container; copied to ${SOAK_OUT:-nowhere}"
    [ -n "$SOAK_OUT" ] && cp "$series" "$SOAK_OUT/input-soak-series.tsv"
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
    -c 'bash /tests/manual/mxl/input-soak.sh --inside'
status=$?
echo "series: $OUT/input-soak-series.tsv"
exit $status
