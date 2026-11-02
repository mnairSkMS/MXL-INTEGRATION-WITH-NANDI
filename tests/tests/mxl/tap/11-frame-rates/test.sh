#!/usr/bin/env bash
# Frame rates beyond 25: 1080p50, 720p59.94 and 1080p29.97. For each, the video flow must carry the
# source's rate, hold the picture size, and deliver frames in real time (a reader gets 100 frames in
# about 100 / rate seconds; a tap that dropped or stalled would take longer). The engine's CPU use is
# measured with the tap and again without it, so the cost of the tap at the high rates is on record.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

# size, rate given to ffmpeg, rate reported by the flow, frames per second
CASES=("1920x1080 50 50/1 50" "1280x720 60000/1001 60000/1001 59.94" "1920x1080 30000/1001 30000/1001 29.97")

for c in "${CASES[@]}"; do
    read -r size rate flowrate fps <<< "$c"
    w=${size%x*}; h=${size#*x}
    mxl_log "case $size @ $fps"

    rm -rf "$MXL_DOMAIN"; mkdir -p "$MXL_DOMAIN"
    export MXL_OUT_SIZE=$size
    mxl_write_config $MXL_TMP/config.json "$(mxl_tap_json)"
    mxl_start_source tone $size $rate
    src=$MXL_SRC_PID
    mxl_start_engine $MXL_TMP/config.json
    eng=$MXL_ENGINE_PID

    mxl_wait_flow $MXL_FLOW_VIDEO 40 || { mxl_fail "$size@$fps: the video flow did not appear (see engine.log)"; mxl_stop $eng; mxl_stop $src; continue; }
    sleep 4

    got=$(mxl_flow_field $MXL_FLOW_VIDEO 'Grain/sample rate')
    [ "$got" = "$flowrate" ] || mxl_fail "$size@$fps: the flow rate is '$got', expected $flowrate"

    t0=$(date +%s.%N)
    mxl_read_video 30 100 $MXL_TMP/v.y4m || mxl_fail "$size@$fps: could not read 100 frames"
    took=$(mxl_seconds_since $t0)
    expect=$(awk -v f=$fps 'BEGIN { printf "%.2f", 100 / f }')
    echo "RESULT $size@$fps: flow $got, 100 frames read in $took s (real time: $expect s)"
    awk -v a=$took -v e=$expect 'BEGIN { exit !(a >= e * 0.8 && a <= e + 1.5) }' || mxl_fail "$size@$fps: 100 frames took $took s, expected about $expect s"

    pic=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x $MXL_TMP/v.y4m | head -1)
    [ "$pic" = "$size" ] || mxl_fail "$size@$fps: the flow picture is $pic"

    cpu0=$(mxl_cpu_seconds $eng); sleep 15; cpu1=$(mxl_cpu_seconds $eng)
    with=$(awk -v a=$cpu0 -v b=$cpu1 'BEGIN { printf "%.2f", (b - a) / 15 }')

    mxl_alive $eng || mxl_fail "$size@$fps: the engine stopped"
    grep -q 'side pipeline error' $MXL_TMP/engine.log && mxl_fail "$size@$fps: the tap reported a side pipeline error"
    mxl_stop $eng

    # The same channel without the tap, on the same source.
    mxl_write_config $MXL_TMP/config-notap.json ""
    mxl_start_engine $MXL_TMP/config-notap.json
    eng=$MXL_ENGINE_PID
    sleep 12
    cpu0=$(mxl_cpu_seconds $eng); sleep 15; cpu1=$(mxl_cpu_seconds $eng)
    without=$(awk -v a=$cpu0 -v b=$cpu1 'BEGIN { printf "%.2f", (b - a) / 15 }')
    echo "RESULT $size@$fps: engine CPU $with cores with the tap, $without without: the tap costs $(awk -v a=$with -v b=$without 'BEGIN { printf "%.2f", a - b }') core"

    mxl_stop $eng; mxl_stop $src
    sleep 1
done

mxl_test_end
