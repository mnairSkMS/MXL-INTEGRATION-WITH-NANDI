#!/usr/bin/env bash
# Frame rates. A flow is written by gst-launch at 25, 29.97 (30000/1001), 50 and 59.94 (60000/1001) frames per
# second with a moving picture, and the channel reads it. The output must have the same frame rate, no gap longer
# than three frames, and show a moving picture. (The recording starts at a keyframe, so its length is not the time
# it was recorded for; the rate is taken from the first and last frame.)

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

n=0
for rate in 25/1 30000/1001 50/1 60000/1001; do
    n=$((n + 1))
    MXL_DOMAIN="$MXL_TMP/domain-$n"; mkdir -p "$MXL_DOMAIN"

    mxl_write_video $MXL_FLOW_VIDEO $rate 1280 720 100000 ball
    vw=$MXL_LAST_PID
    mxl_write_audio $MXL_FLOW_AUDIO 2 0 100000
    aw=$MXL_LAST_PID
    mxl_wait_flow $MXL_FLOW_VIDEO 20 || mxl_abort "$rate: the video writer did not start"
    mxl_wait_flow $MXL_FLOW_AUDIO 20 || mxl_abort "$rate: the audio writer did not start"

    MXL_INPUT_TRANSPORT="$(mxl_input_json)" mxl_write_config $MXL_TMP/b$n.json ""
    mxl_start_engine $MXL_TMP/b$n.json b$n
    engine=$MXL_ENGINE_PID
    sleep 8
    mxl_alive $engine || mxl_abort "$rate: the channel did not start (see engine-b$n.log)"
    mxl_capture_output 8 $MXL_TMP/out$n.ts

    got=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 $MXL_TMP/out$n.ts | head -n 1)
    read -r frames measured gap <<< "$(ffprobe -v error -select_streams v:0 -show_entries packet=pts_time -of csv=p=0 $MXL_TMP/out$n.ts | grep -v '^$' | sort -n \
        | awk 'NR == 1 { first = $1 } NR > 1 { d = $1 - p; if (d > m) m = d } { p = $1; last = $1; c = NR } END { printf "%d %.3f %.0f", c, (c - 1) / (last - first), m * 1000 }')"
    nominal=$(awk -v r=$rate 'BEGIN { split(r, f, "/"); printf "%.3f", f[1] / f[2] }')
    pics=$(ffmpeg -v error -y -i $MXL_TMP/out$n.ts -an -vf "fps=5,scale=160:90,format=gray" -f framemd5 - 2>/dev/null | awk '!/^#/ { print $NF }' | sort -u | wc -l)
    echo "RESULT flow at $rate: output frame rate $got, measured $measured frames per second over ${frames} frames, largest gap ${gap} ms, ${pics} different pictures in 40 sampled"
    [ "$got" = "$rate" ] || mxl_fail "$rate: the output frame rate is $got"
    awk -v m="$measured" -v n="$nominal" 'BEGIN { exit !(m >= n * 0.97 && m <= n * 1.03) }' || mxl_fail "$rate: the output runs at $measured frames per second, not $nominal"
    awk -v g="$gap" -v n="$nominal" 'BEGIN { exit !(g <= 3000 / n) }' || mxl_fail "$rate: the output has a gap of ${gap} ms"
    [ "$pics" -ge 25 ] || mxl_fail "$rate: the picture does not move ($pics different pictures in 40 samples)"
    [ "$(mxl_decode_errors $MXL_TMP/out$n.ts)" = "0" ] || mxl_fail "$rate: the output has decode errors"
    mxl_alive $engine || mxl_fail "$rate: the channel stopped"

    mxl_stop $engine; mxl_stop $vw; mxl_stop $aw
done

mxl_test_end
