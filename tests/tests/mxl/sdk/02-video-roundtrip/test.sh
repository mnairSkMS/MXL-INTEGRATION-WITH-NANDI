#!/usr/bin/env bash
# 1080p29.97 v210 through mxlsink -> mxlsrc in a second process: the flow carries the right
# identity and the reader gets the right number of frames, with the picture unchanged.
# The picture (static colour bars) is compared by frame checksum against the same source converted without MXL.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require mxlsink mxlsrc mxl-info ffprobe ffmpeg

FRAMES=90

mxl_write_video $MXL_FLOW_VIDEO 30000/1001 1920 1080 300
mxl_wait_flow $MXL_FLOW_VIDEO 10 || mxl_abort "flow did not appear in the domain"

[ "$(mxl_flow_field $MXL_FLOW_VIDEO 'Grain/sample rate')" = "30000/1001" ] || mxl_fail "flow rate is not 30000/1001"
[ "$(mxl_flow_field $MXL_FLOW_VIDEO Format)" = "Video" ]                   || mxl_fail "flow is not a video flow"

timeout 30 gst-launch-1.0 -q -e mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN num-buffers=$FRAMES \
    ! videoconvert ! "video/x-raw,format=I420" ! y4menc ! filesink location=$MXL_TMP/out.y4m >> $MXL_LOG 2>&1 \
    || mxl_fail "reader did not finish"

# Reference: the same source and conversions, with no MXL in between.
gst-launch-1.0 -q videotestsrc num-buffers=1 pattern=bar \
    ! videoconvert ! "video/x-raw,format=v210,width=1920,height=1080,framerate=30000/1001" \
    ! videoconvert ! "video/x-raw,format=I420" ! y4menc ! filesink location=$MXL_TMP/ref.y4m >> $MXL_LOG 2>&1

stream() { ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=$2 -of default=nw=1:nk=1 "$1"; }
[ "$(stream $MXL_TMP/out.y4m width)" = "1920" ]        || mxl_fail "width is not 1920"
[ "$(stream $MXL_TMP/out.y4m height)" = "1080" ]       || mxl_fail "height is not 1080"
[ "$(stream $MXL_TMP/out.y4m r_frame_rate)" = "30000/1001" ] || mxl_fail "frame rate is not 30000/1001"
[ "$(stream $MXL_TMP/out.y4m nb_read_frames)" = "$FRAMES" ]  || mxl_fail "expected $FRAMES frames, got $(stream $MXL_TMP/out.y4m nb_read_frames)"

refsum=$(ffmpeg -v error -i $MXL_TMP/ref.y4m -f framemd5 - | awk '!/^#/ { gsub(/,/, ""); print $NF }' | sort -u)
outsums=$(ffmpeg -v error -i $MXL_TMP/out.y4m -f framemd5 - | awk '!/^#/ { gsub(/,/, ""); print $NF }' | sort -u)
[ "$outsums" = "$refsum" ] || mxl_fail "frames differ from the no-MXL reference (ref: $refsum / got: $(echo $outsums | tr '\n' ' '))"

mxl_test_end
