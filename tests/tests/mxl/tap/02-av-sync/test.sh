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

# 1. The source itself, with no engine: where its own flash and beep fall.
ffmpeg -v error -y -f lavfi -i "color=c=black:s=1280x720:r=25,geq=lum='if(lt(mod(T\,1)\,0.08)\,235\,16)':cb=128:cr=128" \
    -f lavfi -i "aevalsrc='if(lt(mod(t\,1)\,0.04)\,0.5*sin(2*PI*1000*t)\,0)':s=48000:c=stereo" \
    -t 9 -c:v libx264 -preset veryfast -g 50 -b:v 3M -c:a aac -ac 2 -f mpegts $MXL_TMP/source.ts >> $MXL_LOG 2>&1
read -r src_ms src_n <<< "$(mxl_lipsync_file $MXL_TMP/source.ts)"

# 2. The channel with the tap.
mxl_write_config $MXL_TMP/config.json "$(mxl_tap_json)"
mxl_start_source sync
mxl_start_engine $MXL_TMP/config.json
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the video flow did not appear"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "the audio flow did not appear"
sleep 6

mxl_bg ffmpeg -v error -y -i "udp://127.0.0.1:${MXL_PORT_OUT}?fifo_size=5000000&overrun_nonfatal=1" -t 12 -c copy $MXL_TMP/onair.ts
capture=$MXL_LAST_PID
mxl_lipsync_flows 14 $MXL_TMP/flows.log
wait $capture 2>/dev/null || true

read -r onair_ms onair_n <<< "$(mxl_lipsync_file $MXL_TMP/onair.ts)"
flash_list=$(sed -n 1p $MXL_TMP/flows.log.onsets); beep_list=$(sed -n 2p $MXL_TMP/flows.log.onsets)
read -r mxl_ms mxl_n <<< "$(mxl_lipsync_pairs "$flash_list" "$beep_list")"

echo "RESULT flash-minus-beep in the generated source:   ${src_ms} ms (${src_n} pairs)"
echo "RESULT flash-minus-beep in the MXL flows:           ${mxl_ms} ms (${mxl_n} pairs)"
echo "RESULT flash-minus-beep in the engine's on-air output: ${onair_ms} ms (${onair_n} pairs)"
mxl_log "source ${src_ms} ms, MXL ${mxl_ms} ms, on-air ${onair_ms} ms"

[ "${mxl_n:-0}" -ge 5 ] || mxl_abort "could not measure the MXL flows (pairs: ${mxl_n:-0}, flashes: $(echo $flash_list | wc -w), beeps: $(echo $beep_list | wc -w))"
[ "${src_n:-0}" -ge 5 ] || mxl_abort "could not measure the generated source (pairs: ${src_n:-0})"
awk -v a="$mxl_ms" -v b="$src_ms" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 80) }' \
    || mxl_fail "the MXL flows are $(awk -v a=$mxl_ms -v b=$src_ms 'BEGIN { print a - b }') ms from the source's own offset; limit 80 ms"

mxl_test_end
