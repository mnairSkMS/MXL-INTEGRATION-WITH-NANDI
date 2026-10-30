#!/usr/bin/env bash
# Measures what the MXL boundary costs per 1080p frame, single-threaded, and prints it as RESULT
# lines. Nandi's compositor canvas is BGRA and typical encoder input is I420, so both conversions
# to and from V210 are measured. Only a loose sanity limit is enforced: the acceptance numbers
# (Gate C) are for the project owner to set from these measurements.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require mxlsink mxlsrc mxl-info

N=300
CAPS="width=1920,height=1080,framerate=30000/1001"
SRC="videotestsrc num-buffers=$N pattern=smpte"

# cpu_ms_per_frame <frames> <command...>: user+system CPU of the command, per frame, best of 3.
cpu_ms_per_frame() {
    local frames="$1"; shift
    local best=""
    for i in 1 2 3; do
        local t; t=$( { TIMEFORMAT='%U %S'; time "$@" > /dev/null 2>&1; } 2>&1 )
        local ms; ms=$(echo $t | awk -v n=$frames '{ printf "%.3f", ($1 + $2) * 1000 / n }')
        [ -z "$best" ] && best=$ms
        best=$(awk -v a=$best -v b=$ms 'BEGIN { print (b < a) ? b : a }')
    done
    echo $best
}
sub() { awk -v a=$1 -v b=$2 'BEGIN { printf "%.2f", a - b }'; }
report() { echo "RESULT $1 ${2} ms/frame"; mxl_log "$1: $2 ms/frame"; }

bgra=$(cpu_ms_per_frame $N gst-launch-1.0 -q $SRC ! "video/x-raw,format=BGRA,$CAPS" ! fakesink sync=false)
i420=$(cpu_ms_per_frame $N gst-launch-1.0 -q $SRC ! "video/x-raw,format=I420,$CAPS" ! fakesink sync=false)
b2v=$(cpu_ms_per_frame $N gst-launch-1.0 -q $SRC ! "video/x-raw,format=BGRA,$CAPS" ! videoconvert ! "video/x-raw,format=v210" ! fakesink sync=false)
i2v=$(cpu_ms_per_frame $N gst-launch-1.0 -q $SRC ! "video/x-raw,format=I420,$CAPS" ! videoconvert ! "video/x-raw,format=v210" ! fakesink sync=false)
v2m=$(cpu_ms_per_frame $N gst-launch-1.0 -q $SRC ! "video/x-raw,format=v210,$CAPS" ! fakesink sync=false)
mkdir -p $MXL_TMP/w
wsink=$(cpu_ms_per_frame $N gst-launch-1.0 -q $SRC ! "video/x-raw,format=v210,$CAPS" ! mxlsink flow-id=$MXL_FLOW_VIDEO domain=$MXL_TMP/w sync=false)

report "BGRA->v210 (programme tap, write side)"   "$(sub $b2v $bgra)"
report "I420->v210 (source tap, write side)"      "$(sub $i2v $i420)"
report "mxlsink ring write"                        "$(sub $wsink $v2m)"

mxl_write_video $MXL_FLOW_VIDEO 30000/1001 1920 1080 3000
mxl_wait_flow $MXL_FLOW_VIDEO 10 || mxl_abort "flow did not appear"
RD="timeout 30 gst-launch-1.0 -q mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN num-buffers=120"
rsrc=$(cpu_ms_per_frame 120 $RD ! fakesink sync=false)
rbgra=$(cpu_ms_per_frame 120 $RD ! videoconvert ! "video/x-raw,format=BGRA" ! fakesink sync=false)
ri420=$(cpu_ms_per_frame 120 $RD ! videoconvert ! "video/x-raw,format=I420" ! fakesink sync=false)

report "mxlsrc ring read"                          "$rsrc"
report "v210->BGRA (MXL input into compositor)"    "$(sub $rbgra $rsrc)"
report "v210->I420 (reader towards an encoder)"    "$(sub $ri420 $rsrc)"

# Sanity: nothing at the boundary should cost more than 25 ms per 1080p frame on one core.
for v in "$(sub $b2v $bgra)" "$(sub $wsink $v2m)" "$rsrc" "$(sub $rbgra $rsrc)"; do
    awk -v v=$v 'BEGIN { exit !(v < 25) }' || mxl_fail "a boundary cost of ${v} ms/frame exceeds the 25 ms sanity limit"
done

mxl_test_end
