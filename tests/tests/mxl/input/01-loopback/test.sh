#!/usr/bin/env bash
# An input of "mxl://<domain>" reads MXL flows into a channel. The loopback: channel A takes a UDP
# source and publishes its programme as MXL flows (an output of type "mxl"); channel B reads those flows as
# its input and encodes them to its own UDP output. B's output is checked with ffprobe and by decoding it:
# the streams, the picture size and rate, that the picture moves, that the 1 kHz tone arrives, and no
# decode errors. A's own on-air output goes to another port and is not looked at.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

# Channel A: the UDP source to the MXL flows
MXL_OUT_PORT=4445 MXL_EXTRA_OUTPUT="$(mxl_programme_json)" mxl_write_config $MXL_TMP/a.json ""
# Channel B: the MXL flows to UDP
MXL_INPUT_TRANSPORT="$(mxl_input_json)" mxl_write_config $MXL_TMP/b.json ""

mxl_start_source tone
mxl_start_engine $MXL_TMP/a.json a
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "channel A's video flow did not appear (see engine-a.log)"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "channel A's audio flow did not appear (see engine-a.log)"
mxl_start_engine $MXL_TMP/b.json b
pid_b=$MXL_ENGINE_PID
sleep 8

mxl_alive $pid_b || { tail -20 $MXL_TMP/engine-b.log >> $MXL_LOG; mxl_abort "channel B is not running (see engine-b.log)"; }

mxl_capture_output 12 $MXL_TMP/out.ts
v() { ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=$1 -of default=nw=1:nk=1 $MXL_TMP/out.ts | head -n 1; }
a() { ffprobe -v error -select_streams a:0 -show_entries stream=$1 -of default=nw=1:nk=1 $MXL_TMP/out.ts | head -n 1; }

[ "$(v codec_name)" = "h264" ]   || mxl_fail "B's video is not h264 (got '$(v codec_name)')"
[ "$(v width)" = "1280" ]        || mxl_fail "B's video width is not 1280 (got '$(v width)')"
[ "$(v height)" = "720" ]        || mxl_fail "B's video height is not 720"
[ "$(v r_frame_rate)" = "25/1" ] || mxl_fail "B's frame rate is not 25/1"
[ "$(a codec_name)" = "aac" ]    || mxl_fail "B has no AAC audio (got '$(a codec_name)')"
frames=$(v nb_read_frames)
echo "RESULT channel B's output: ${frames} frames in 12 s"
[ "${frames:-0}" -ge 200 ] || mxl_fail "B's output has only ${frames:-0} frames in 12 s (25 fps expects about 300)"

ffmpeg -v error -y -i $MXL_TMP/out.ts -an -vf "fps=5,scale=160:90,format=gray" -f framemd5 $MXL_TMP/frames.md5 >> $MXL_LOG 2>&1 || true
pics=$(awk '!/^#/ { print $NF }' $MXL_TMP/frames.md5 | sort -u | wc -l)
echo "RESULT B's picture: ${pics} different pictures in 60 sampled"
[ "$pics" -ge 40 ] || mxl_fail "B's picture is frozen or black ($pics different pictures in 60 samples)"

ffmpeg -v error -y -i $MXL_TMP/out.ts -vn -ac 1 $MXL_TMP/a.wav >> $MXL_LOG 2>&1 || true
on=$(mxl_tone_db $MXL_TMP/a.wav 1000); off=$(mxl_tone_db $MXL_TMP/a.wav 4000)
echo "RESULT B's audio: 1 kHz peak ${on} dB, 4 kHz peak ${off} dB"
awk -v a="$on" -v b="$off" 'BEGIN { exit !(a > -35 && b < a - 25) }' || mxl_fail "the 1 kHz tone did not arrive in B's output (1 kHz ${on} dB, 4 kHz ${off} dB)"

vgap=$(mxl_max_gap_ms $MXL_TMP/out.ts v:0); agap=$(mxl_max_gap_ms $MXL_TMP/out.ts a:0)
echo "RESULT B's largest gap between video packets: ${vgap} ms, audio packets: ${agap} ms"
awk -v g="$vgap" 'BEGIN { exit !(g <= 120) }' || mxl_fail "B's video has a ${vgap} ms gap"
awk -v g="$agap" 'BEGIN { exit !(g <= 120) }' || mxl_fail "B's audio has a ${agap} ms gap"
[ "$(mxl_decode_errors $MXL_TMP/out.ts)" = "0" ] || mxl_fail "B's output has decode errors"
mxl_alive $pid_b || mxl_fail "channel B stopped"
# (the engine logs "no such table: events" twice a second when it has no event database; it is not an error of the channel)
errors=$(grep -E 'rror' $MXL_TMP/engine-b.log | grep -v 'no such table' || true)
[ -z "$errors" ] || { echo "$errors" | head -5 >> $MXL_LOG; mxl_fail "channel B logged errors"; }

mxl_test_end
