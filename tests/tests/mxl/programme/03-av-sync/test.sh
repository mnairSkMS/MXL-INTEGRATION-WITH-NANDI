#!/usr/bin/env bash
# Lip-sync of the programme flows. The source flashes the picture and beeps at the same instants. The
# offset between flash and beep is measured in the generated source, in the programme flows (read in
# one pipeline so they share a timeline) and in the channel's on-air output. The programme flows are
# taken before the encoders, so they must stay within 80 ms (two frames) of the source's own offset.
# The on-air output is printed for comparison only: it comes after the AAC and H.264 encoders and the
# muxer, which add an offset of their own (the AAC encoder alone delays the sound by about 43 ms).
# The numbers are printed as RESULT lines. The limit is provisional (Gate A).

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info videoanalyse level

ffmpeg -v error -y -f lavfi -i "color=c=black:s=1280x720:r=25,geq=lum='if(lt(mod(T\,1)\,0.08)\,235\,16)':cb=128:cr=128" \
    -f lavfi -i "aevalsrc='if(lt(mod(t\,1)\,0.04)\,0.5*sin(2*PI*1000*t)\,0)':s=48000:c=stereo" \
    -t 9 -c:v libx264 -preset veryfast -g 50 -b:v 3M -c:a aac -ac 2 -f mpegts $MXL_TMP/source.ts >> $MXL_LOG 2>&1
read -r src_ms src_n <<< "$(mxl_lipsync_file $MXL_TMP/source.ts)"

export MXL_EXTRA_OUTPUT="$(mxl_programme_json)"
mxl_write_config $MXL_TMP/config.json ""
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

echo "RESULT flash-minus-beep in the generated source:        ${src_ms} ms (${src_n} pairs)"
echo "RESULT flash-minus-beep in the programme MXL flows:     ${mxl_ms} ms (${mxl_n} pairs)"
echo "RESULT flash-minus-beep in the engine's on-air output:  ${onair_ms} ms (${onair_n} pairs)"
mxl_log "source ${src_ms} ms, programme ${mxl_ms} ms, on-air ${onair_ms} ms"

[ "${mxl_n:-0}" -ge 5 ]   || mxl_abort "could not measure the programme flows (pairs: ${mxl_n:-0}, flashes: $(echo $flash_list | wc -w), beeps: $(echo $beep_list | wc -w))"
[ "${src_n:-0}" -ge 5 ]   || mxl_abort "could not measure the generated source (pairs: ${src_n:-0})"
awk -v a="$mxl_ms" -v b="$src_ms" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 80) }' \
    || mxl_fail "the programme flows are $(awk -v a=$mxl_ms -v b=$src_ms 'BEGIN { print a - b }') ms from the source's own offset; limit 80 ms"

mxl_test_end
