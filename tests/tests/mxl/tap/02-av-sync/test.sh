#!/usr/bin/env bash
# Lip-sync of the tap. The source flashes the picture and beeps at the same instants. Both flows are
# read in one pipeline (so they share a timeline) and the flash-to-beep offset is measured from the
# flows' own timestamps, using videoanalyse (picture brightness) and level (audio RMS). The same
# offset is measured in the generated source file and in the engine's on-air output. The tap must
# not add more than two frames (80 ms) to the source's own offset. Numbers are printed as RESULT
# lines. The 80 ms limit is provisional (Gate A).

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info videoanalyse level

# Median flash-minus-beep in ms and the number of pairs, from onset lists (seconds).
pairs() {  # pairs "<flash onsets>" "<beep onsets>"
    python3 -I - "$1" "$2" <<'PY'
import sys
flash = [float(x) for x in sys.argv[1].split()]
beep = [float(x) for x in sys.argv[2].split()]
d = []
for t in flash:
    if not beep:
        break
    b = min(beep, key=lambda x: abs(x - t))
    if abs(b - t) < 0.5:
        d.append((t - b) * 1000)
d.sort()
print(f"{d[len(d) // 2]:.0f} {len(d)}" if d else "none 0")
PY
}

# From a media file (ffprobe / ffmpeg).
measure_file() {
    local f="$1" flash beep
    flash=$(ffprobe -v error -f lavfi -i "movie=$f,signalstats" -show_entries frame=pts_time:frame_tags=lavfi.signalstats.YAVG -of csv=p=0 \
        | awk -F, '{ y = $2 + 0; if (y > 120 && prev <= 120) printf "%s ", $1; prev = y }')
    # silencedetect times start at 0 for the audio stream, ffprobe picture times include the stream's
    # start offset (ffmpeg's mux delay, about 1.4 s), so the audio start time is added back.
    local astart; astart=$(ffprobe -v error -select_streams a:0 -show_entries stream=start_time -of default=nw=1:nk=1 "$f" | head -1)
    beep=$(ffmpeg -v info -i "$f" -map 0:a -af silencedetect=noise=-35dB:d=0.3 -f null - 2>&1 | grep -o 'silence_end: [0-9.]*' \
        | awk -v s="$astart" '{ printf "%.4f ", $2 + s }')
    pairs "$flash" "$beep"
}

# From the gst-launch -m log of the two flows.
measure_flows() {
    python3 -I - "$1" <<'PY'
import re, sys
flash, beep, prev_hi, prev_loud = [], [], False, False
for line in open(sys.argv[1], errors="replace"):
    m = re.search(r"GstVideoAnalyse.*?running-time=\(guint64\)(\d+).*?luma-average=\(double\)([0-9.e+-]+)", line)
    if m:
        hi = float(m[2]) > 0.45
        if hi and not prev_hi:
            flash.append(int(m[1]) / 1e9)
        prev_hi = hi
        continue
    m = re.search(r"level, endtime=.*?running-time=\(guint64\)(\d+).*?rms=\(GValueArray\)< ?([-0-9.e+]+)", line)
    if m:
        loud = float(m[2]) > -30
        if loud and not prev_loud:
            beep.append(int(m[1]) / 1e9)   # running-time is the start of the 10 ms interval
        prev_loud = loud
print(" ".join(f"{t:.4f}" for t in flash))
print(" ".join(f"{t:.4f}" for t in beep))
PY
}

# 1. The source itself, with no engine: where its own flash and beep fall.
ffmpeg -v error -y -f lavfi -i "color=c=black:s=1280x720:r=25,geq=lum='if(lt(mod(T\,1)\,0.08)\,235\,16)':cb=128:cr=128" \
    -f lavfi -i "aevalsrc='if(lt(mod(t\,1)\,0.04)\,0.5*sin(2*PI*1000*t)\,0)':s=48000:c=stereo" \
    -t 9 -c:v libx264 -preset veryfast -g 50 -b:v 3M -c:a aac -ac 2 -f mpegts $MXL_TMP/source.ts >> $MXL_LOG 2>&1
read -r src_ms src_n <<< "$(measure_file $MXL_TMP/source.ts)"

# 2. The channel with the tap.
mxl_write_config $MXL_TMP/config.json "$(mxl_tap_json)"
mxl_start_source sync
mxl_start_engine $MXL_TMP/config.json
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the video flow did not appear"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "the audio flow did not appear"
sleep 6

mxl_bg ffmpeg -v error -y -i "udp://127.0.0.1:${MXL_PORT_OUT}?fifo_size=5000000&overrun_nonfatal=1" -t 12 -c copy $MXL_TMP/onair.ts
capture=$MXL_LAST_PID
timeout 14 gst-launch-1.0 -m \
    mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN ! videoconvert ! videoanalyse message=true ! fakesink sync=false \
    mxlsrc audio-flow-id=$MXL_FLOW_AUDIO domain=$MXL_DOMAIN ! audioconvert ! level interval=10000000 post-messages=true ! fakesink sync=false \
    > $MXL_TMP/flows.log 2>&1 || true
wait $capture 2>/dev/null || true

read -r onair_ms onair_n <<< "$(measure_file $MXL_TMP/onair.ts)"
flows_out=$(measure_flows $MXL_TMP/flows.log)
flash_list=$(echo "$flows_out" | sed -n 1p); beep_list=$(echo "$flows_out" | sed -n 2p)
read -r mxl_ms mxl_n <<< "$(pairs "$flash_list" "$beep_list")"

echo "RESULT flash-minus-beep in the generated source:   ${src_ms} ms (${src_n} pairs)"
echo "RESULT flash-minus-beep in the MXL flows:           ${mxl_ms} ms (${mxl_n} pairs)"
echo "RESULT flash-minus-beep in the engine's on-air output: ${onair_ms} ms (${onair_n} pairs)"
mxl_log "source ${src_ms} ms, MXL ${mxl_ms} ms, on-air ${onair_ms} ms"

[ "${mxl_n:-0}" -ge 5 ] || mxl_abort "could not measure the MXL flows (pairs: ${mxl_n:-0}, flashes: $(echo $flash_list | wc -w), beeps: $(echo $beep_list | wc -w))"
[ "${src_n:-0}" -ge 5 ] || mxl_abort "could not measure the generated source (pairs: ${src_n:-0})"
awk -v a="$mxl_ms" -v b="$src_ms" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 80) }' \
    || mxl_fail "the MXL flows are $(awk -v a=$mxl_ms -v b=$src_ms 'BEGIN { print a - b }') ms from the source's own offset; limit 80 ms"

mxl_test_end
