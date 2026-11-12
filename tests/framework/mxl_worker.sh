#!/usr/bin/env bash
# Helpers for the tests of a channel split into a main engine and a worker (Step 5, WP3; tests/tests/mxl/worker).
#
# The same channel is run twice, one run after the other, with the same source and the same encoder settings:
#   P    one engine: the source, encoded to MPEG-TS on port 4444 (with the SCTE-35 passthrough on PID 500 when asked)
#   M+W  a main engine that decodes the source and writes its programme as MXL flows (video, audio, SCTE-35 data), nothing encoded,
#        and a worker, a second engine whose input is those flows and whose output is P's
# Needs framework/mxl.sh loaded first and mxl_test_begin already called.

MXL_FLOW_DATA=7c1d2a52-3f0e-4b6e-9d54-0a6e5b1f8a31
WORKER_SCTE='{ "pid": 500, "mode": "passthrough" }'

# worker_domain_in_memory: puts the MXL domain in /dev/shm, as in a deployment (the Compose volume is a tmpfs). At 1080p a flow is written at
# about 140 MB/s, which a domain on the disk of the test machine does not always keep up with. Without /dev/shm (or with under 300 MB of it)
# the domain stays in the test directory. Call BEFORE mxl_test_begin; the test must then call worker_trap after it.
function worker_domain_in_memory() {
    if [ -d /dev/shm ] && [ -w /dev/shm ] && [ "$(df -m --output=avail /dev/shm | tail -1)" -ge 300 ]; then
        MXL_TEST_DOMAIN=/dev/shm/mxl-worker-$$
    fi
}

function worker_trap() {
    trap 'mxl_cleanup; rm -rf "$MXL_TEST_DOMAIN"' EXIT
    echo "RESULT the MXL domain is in ${MXL_DOMAIN}"
}

# worker_configs <size WxH> <scte: yes|no>: writes p.json, main.json and w.json in $MXL_TMP
function worker_configs() {
    local size="$1" scte="$2" scte_json="" data=""
    if [ "$scte" = "yes" ]; then
        scte_json="$WORKER_SCTE"
        data=", \"data\": { \"flowId\": \"$MXL_FLOW_DATA\" }"
    fi

    MXL_OUT_SIZE=$size MXL_OUT_SCTE="$scte_json" mxl_write_config $MXL_TMP/p.json ""

    cat > $MXL_TMP/main.json <<JSON
{ "version": "1.1.0",
  "application": { "logger": { "transcoder": { "level": "trace", "console": { "enable": true, "colors": false, "stderr": false } } } },
  "transcoder": {
    "inputs": [ { "transport": { "uri": "udp://127.0.0.1:${MXL_PORT_IN}" } } ],
    "outputs": [ { "type": "mxl", "name": "programme", "mxl": { "domain": "$MXL_DOMAIN", "video": { "flowId": "$MXL_FLOW_VIDEO" },
                   "audio": { "flowId": "$MXL_FLOW_AUDIO", "channels": 2 }${data} } } ] } }
JSON

    if [ "$scte" = "yes" ]; then
        MXL_INPUT_DATA_FLOW=$MXL_FLOW_DATA MXL_INPUT_TRANSPORT="$(MXL_INPUT_DATA_FLOW=$MXL_FLOW_DATA mxl_input_json)" \
            MXL_OUT_SIZE=$size MXL_OUT_SCTE="$scte_json" mxl_write_config $MXL_TMP/w.json ""
    else
        MXL_INPUT_TRANSPORT="$(mxl_input_json)" MXL_OUT_SIZE=$size mxl_write_config $MXL_TMP/w.json ""
    fi
}

# worker_cpu <pid> <seconds from cpu0>: cores used since worker_cpu_start
function worker_cpu_start() { WORKER_CPU0=$(mxl_cpu_seconds $1); }
function worker_cpu_end() { awk -v a=$WORKER_CPU0 -v b=$(mxl_cpu_seconds $1) -v w=$2 'BEGIN { printf "%.2f", (b - a) / w }'; }

# worker_run_p <source kind> <size> <rate> <settle s> <window s>: run P. Leaves p.ts, and WORKER_CPU_P in cores over the window.
function worker_run_p() {
    local kind="$1" size="$2" rate="$3" settle="$4" window="$5"
    mxl_start_source $kind $size $rate
    mxl_start_engine $MXL_TMP/p.json p
    local pid=$MXL_ENGINE_PID
    sleep $settle
    worker_cpu_start $pid
    mxl_bg tsp -I ip 4444 -P until --seconds $window -O file $MXL_TMP/p.ts
    local cap=$MXL_LAST_PID
    sleep $(( window + 3 ))
    wait $cap 2> /dev/null || true
    WORKER_CPU_P=$(worker_cpu_end $pid $window)
    mxl_alive $pid || mxl_abort "the in-process engine is not running (see engine-p.log)"
    mxl_stop $pid
    mxl_stop $MXL_SRC_PID
    sleep 2
}

# worker_run_split <source kind> <size> <rate> <settle s> <window s> <data flow: yes|no>: run M+W. Leaves w.ts, WORKER_CPU_M and
# WORKER_CPU_W. The source is started again (a source of the scte kinds plays its file once), so that its events come at the same
# distance from the engines' start as in the run of P.
function worker_run_split() {
    local kind="$1" size="$2" rate="$3" settle="$4" window="$5" data="$6"
    mxl_start_source $kind $size $rate
    mxl_start_engine $MXL_TMP/main.json main
    local pid_m=$MXL_ENGINE_PID
    mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the main engine's video flow did not appear (see engine-main.log)"
    [ "$data" = "yes" ] && { mxl_wait_flow $MXL_FLOW_DATA 40 || mxl_abort "the main engine's data flow did not appear (see engine-main.log)"; }
    mxl_start_engine $MXL_TMP/w.json w
    local pid_w=$MXL_ENGINE_PID
    sleep $settle
    local cm0=$(mxl_cpu_seconds $pid_m)
    worker_cpu_start $pid_w
    mxl_bg tsp -I ip 4444 -P until --seconds $window -O file $MXL_TMP/w.ts
    local cap=$MXL_LAST_PID
    sleep $(( window + 3 ))
    wait $cap 2> /dev/null || true
    WORKER_CPU_W=$(worker_cpu_end $pid_w $window)
    WORKER_CPU_M=$(awk -v a=$cm0 -v b=$(mxl_cpu_seconds $pid_m) -v w=$window 'BEGIN { printf "%.2f", (b - a) / w }')
    mxl_alive $pid_m || mxl_abort "the main engine is not running (see engine-main.log)"
    mxl_alive $pid_w || mxl_abort "the worker is not running (see engine-w.log)"
}

# worker_streams <file>: the streams of a capture, sorted, one per line
function worker_streams() {
    ffprobe -v error -show_entries stream=codec_type,codec_name,width,height,r_frame_rate,sample_rate,channels -of csv=p=0 "$1" 2>/dev/null | sort -u
}

# worker_decode_errors <file> <seconds>: decoder complaints within the first seconds, not counting those about the pictures before the first
# keyframe (a capture starts in the middle of a group of pictures)
function worker_decode_errors() {
    ffmpeg -v error -t "$2" -i "$1" -f null - 2>&1 | grep -vE 'non-existing PPS|decode_slice_header error|no frame!|Last message repeated' | wc -l
}

# worker_frames <file>: "<video packets> <first pts s> <last pts s>"
function worker_frames() {
    ffprobe -v error -select_streams v:0 -show_entries packet=pts_time -of csv=p=0 "$1" 2>/dev/null | grep -v '^$' | sort -n \
        | awk 'NR == 1 { f = $1 } { l = $1 } END { print NR, f, l }'
}
