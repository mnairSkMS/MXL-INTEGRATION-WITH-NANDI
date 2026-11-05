#!/usr/bin/env bash
# What an MXL input costs the engine, against the same picture arriving as H.264 over UDP. The channel is the
# test channel (one x264 output, same size as the input). The UDP case decodes H.264; the MXL case converts
# v210 to I420 and decodes nothing. Engine CPU is the mean over 20 s after 12 s of warm-up. The numbers are printed;
# only a sanity limit is enforced. 720p25 and 1080p25.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

# cores <pid>: engine CPU over 20 s, in cores
cores() {
    local a b t0 t1
    a=$(mxl_cpu_seconds "$1"); t0=$(date +%s.%N)
    sleep 20
    b=$(mxl_cpu_seconds "$1"); t1=$(date +%s.%N)
    awk -v a="$a" -v b="$b" -v t0="$t0" -v t1="$t1" 'BEGIN { printf "%.2f", (b - a) / (t1 - t0) }'
}

for size in 1280x720 1920x1080; do
    export MXL_OUT_SIZE=$size
    w=${size%x*}; h=${size#*x}

    # H.264 over UDP
    mxl_write_config $MXL_TMP/udp-$size.json ""
    mxl_start_source tone $size
    mxl_start_engine $MXL_TMP/udp-$size.json udp-$size
    engine=$MXL_ENGINE_PID
    sleep 12
    mxl_alive $engine || mxl_abort "$size: the UDP channel did not start"
    udp=$(cores $engine)
    mxl_stop $engine; mxl_stop $MXL_SRC_PID

    # MXL flows
    MXL_DOMAIN="$MXL_TMP/domain-$size"; mkdir -p "$MXL_DOMAIN"
    mxl_write_video $MXL_FLOW_VIDEO 25/1 $w $h 100000 ball
    vw=$MXL_LAST_PID
    mxl_write_audio $MXL_FLOW_AUDIO 2 0 100000
    aw=$MXL_LAST_PID
    mxl_wait_flow $MXL_FLOW_VIDEO 20 || mxl_abort "$size: the video writer did not start"
    MXL_INPUT_TRANSPORT="$(mxl_input_json)" mxl_write_config $MXL_TMP/mxl-$size.json ""
    mxl_start_engine $MXL_TMP/mxl-$size.json mxl-$size
    engine=$MXL_ENGINE_PID
    sleep 12
    mxl_alive $engine || mxl_abort "$size: the MXL channel did not start"
    mxlc=$(cores $engine)
    mxl_stop $engine; mxl_stop $vw; mxl_stop $aw

    echo "RESULT $size at 25 fps: engine CPU ${udp} cores with the H.264 UDP input, ${mxlc} cores with the MXL input"
    mxl_log "$size: udp $udp, mxl $mxlc"
    awk -v u="$udp" -v m="$mxlc" 'BEGIN { exit !(m < u + 0.5) }' || mxl_fail "$size: the MXL input costs ${mxlc} cores, which is more than 0.5 core above the UDP input's ${udp}"
done

mxl_test_end
