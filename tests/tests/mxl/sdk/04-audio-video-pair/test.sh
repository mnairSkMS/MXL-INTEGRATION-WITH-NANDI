#!/usr/bin/env bash
# One video and one audio flow in the same domain, read by a single pipeline and muxed: both
# flows are found, and their first timestamps agree within two frames (66 ms at 29.97 fps).
# This checks that the two flows share one timeline; it is not a lip-sync measurement.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require mxlsink mxlsrc mxl-info ffprobe x264enc matroskamux

mxl_write_video $MXL_FLOW_VIDEO 30000/1001 1920 1080 400 ball
mxl_write_audio $MXL_FLOW_AUDIO 2 0 1400
mxl_wait_flow $MXL_FLOW_VIDEO 10 || mxl_abort "video flow did not appear"
mxl_wait_flow $MXL_FLOW_AUDIO 10 || mxl_abort "audio flow did not appear"

timeout 40 gst-launch-1.0 -q -e matroskamux name=m ! filesink location=$MXL_TMP/av.mkv \
    mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN num-buffers=150 \
        ! videoconvert ! x264enc speed-preset=ultrafast tune=zerolatency ! queue ! m.video_0 \
    mxlsrc audio-flow-id=$MXL_FLOW_AUDIO domain=$MXL_DOMAIN num-buffers=500 \
        ! audioconvert ! queue ! m.audio_0 >> $MXL_LOG 2>&1 || mxl_fail "muxing reader did not finish"

kinds=$(ffprobe -v error -show_entries stream=codec_type -of csv=p=0 $MXL_TMP/av.mkv | sort | tr '\n' ' ')
[ "$kinds" = "audio video " ] || mxl_fail "expected one audio and one video stream, got: $kinds"

vstart=$(ffprobe -v error -select_streams v -show_entries stream=start_time -of csv=p=0 $MXL_TMP/av.mkv)
astart=$(ffprobe -v error -select_streams a -show_entries stream=start_time -of csv=p=0 $MXL_TMP/av.mkv)
mxl_log "first timestamps: video ${vstart}s audio ${astart}s"
awk -v v="$vstart" -v a="$astart" 'BEGIN { d = v - a; if (d < 0) d = -d; exit !(d < 0.067) }' \
    || mxl_fail "video ($vstart s) and audio ($astart s) start more than two frames apart"

mxl_test_end
