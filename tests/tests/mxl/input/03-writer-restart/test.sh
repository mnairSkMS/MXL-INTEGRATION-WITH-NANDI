#!/usr/bin/env bash
# The writer of the flows is killed and comes back. A reader of an MXL flow never notices that its writer is
# gone (measured by the SDK test 07), so the input reattaches its readers when they have seen no data for 3 s.
# Channel A (UDP source to MXL flows) is killed with SIGKILL while channel B reads it, left dead for 6 s and
# started again. B must keep running, say that it reattached, and show A's picture and tone again; the
# time that takes is printed.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

MXL_OUT_PORT=4445 MXL_EXTRA_OUTPUT="$(mxl_programme_json)" mxl_write_config $MXL_TMP/a.json ""
MXL_INPUT_TRANSPORT="$(mxl_input_json)" mxl_write_config $MXL_TMP/b.json ""

mxl_start_source tone
mxl_start_engine $MXL_TMP/a.json a
pid_a=$MXL_ENGINE_PID
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "channel A's video flow did not appear (see engine-a.log)"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "channel A's audio flow did not appear (see engine-a.log)"
mxl_start_engine $MXL_TMP/b.json b
pid_b=$MXL_ENGINE_PID
sleep 10

# probe <name>: records 2 s of B's output and says whether it carries a moving picture and the tone ("yes" or "no")
probe() {
    rm -f $MXL_TMP/$1.ts
    timeout 12 ffmpeg -v error -y -i "udp://127.0.0.1:${MXL_PORT_OUT}?fifo_size=5000000&overrun_nonfatal=1" -t 2 -c copy $MXL_TMP/$1.ts >> $MXL_LOG 2>&1 || true
    [ -s $MXL_TMP/$1.ts ] || { echo no; return; }
    local pics tone
    pics=$(ffmpeg -v error -y -i $MXL_TMP/$1.ts -an -vf "fps=10,scale=160:90,format=gray" -f framemd5 - 2>/dev/null | awk '!/^#/ { print $NF }' | sort -u | wc -l)
    ffmpeg -v error -y -i $MXL_TMP/$1.ts -vn -ac 1 $MXL_TMP/$1.wav >> $MXL_LOG 2>&1 || true
    tone=$(mxl_tone_db $MXL_TMP/$1.wav 1000)
    mxl_log "probe $1: $pics pictures, tone ${tone:-none} dB"
    if [ "$pics" -ge 10 ] && awk -v t="${tone:--99}" 'BEGIN { exit !(t > -35) }'; then echo yes; else echo no; fi
}

[ "$(probe before)" = "yes" ] || mxl_abort "channel B shows no picture and tone before the writer is killed"

kill -9 $pid_a
sleep 6
mxl_alive $pid_b || mxl_abort "channel B stopped when its writer was killed"
down=$(probe during)
echo "RESULT while the writer is gone, B shows A's picture and tone: ${down}"
[ "$down" = "no" ] || mxl_fail "B still shows A's picture and tone while A is dead"

mxl_start_engine $MXL_TMP/a.json a
# the writer is back when channel A says that it publishes again
for i in $(seq 1 200); do
    grep -q "MXL tap $MXL_FLOW_VIDEO (video) started" $MXL_TMP/engine-a.log 2>/dev/null && break
    sleep 0.2
done
t0=$(date +%s.%N)
recovered=no
for i in $(seq 1 12); do
    if [ "$(probe after)" = "yes" ]; then recovered=yes; break; fi
done
secs=$(mxl_seconds_since $t0)
echo "RESULT B shows A's picture and tone again ${secs} s after channel A publishes again (measured in steps of about 4 s): ${recovered}"
[ "$recovered" = "yes" ] || mxl_fail "B did not show A's picture and tone again within ${secs} s"
mxl_alive $pid_b || mxl_fail "channel B stopped"
grep -q 'no data on the .* flow' $MXL_TMP/engine-b.log || mxl_fail "B never said that it reattached a flow"
echo "RESULT $(grep -c 'no data on the .* flow' $MXL_TMP/engine-b.log) reattach lines in B's log"

mxl_test_end
