#!/usr/bin/env bash
# Helpers for the tests of a channel split into a main engine and a worker (Step 5, WP3; tests/tests/mxl/worker).
#
# The same channel is run twice, one run after the other, with the same source and the same encoder settings:
#   P    one engine: the source, encoded to MPEG-TS on port 4444 (with the SCTE-35 passthrough on PID 500 when asked)
#   M+W  a main engine that decodes the source and writes its programme as MXL flows (video, audio, SCTE-35 data), nothing encoded,
#        and a worker, a second engine whose input is those flows and whose output is P's
# Needs framework/mxl.sh loaded first and mxl_test_begin already called.

MXL_FLOW_DATA=7c1d2a52-3f0e-4b6e-9d54-0a6e5b1f8a31
WORKER_SCTE='{ "pid": 500, "mode": "passthrough" }'

# worker_domain_in_memory: puts the MXL domain in /dev/shm, as in a deployment (the Compose volume is a tmpfs). At 1080p a flow is written at
# about 140 MB/s, which a domain on the disk of the test machine does not always keep up with. Without /dev/shm (or with under 300 MB of it)
# the domain stays in the test directory. Call BEFORE mxl_test_begin; the test must then call worker_trap after it.
function worker_domain_in_memory() {
    if [ -d /dev/shm ] && [ -w /dev/shm ] && [ "$(df -m --output=avail /dev/shm | tail -1)" -ge 300 ]; then
        MXL_TEST_DOMAIN=/dev/shm/mxl-worker-$$
    fi
}

function worker_trap() {
    trap 'mxl_cleanup; rm -rf "$MXL_TEST_DOMAIN"' EXIT
    echo "RESULT the MXL domain is in ${MXL_DOMAIN}"
}

# worker_configs <size WxH> <scte: yes|no>: writes p.json, main.json and w.json in $MXL_TMP
function worker_configs() {
    local size="$1" scte="$2" scte_json="" data=""
    if [ "$scte" = "yes" ]; then
        scte_json="$WORKER_SCTE"
        data=", \"data\": { \"flowId\": \"$MXL_FLOW_DATA\" }"
    fi

    MXL_OUT_SIZE=$size MXL_OUT_SCTE="$scte_json" mxl_write_config $MXL_TMP/p.json ""

    cat > $MXL_TMP/main.json <<JSON
{ "version": "1.1.0",
  "application": { "logger": { "transcoder": { "level": "trace", "console": { "enable": true, "colors": false, "stderr": false } } } },
  "transcoder": {
    "inputs": [ { "transport": { "uri": "udp://127.0.0.1:${MXL_PORT_IN}" } } ],
    "outputs": [ { "type": "mxl", "name": "programme", "mxl": { "domain": "$MXL_DOMAIN", "video": { "flowId": "$MXL_FLOW_VIDEO" },
                   "audio": { "flowId": "$MXL_FLOW_AUDIO", "channels": 2 }${data} } } ] } }
JSON

    if [ "$scte" = "yes" ]; then
        MXL_INPUT_DATA_FLOW=$MXL_FLOW_DATA MXL_INPUT_TRANSPORT="$(MXL_INPUT_DATA_FLOW=$MXL_FLOW_DATA mxl_input_json)" \
            MXL_OUT_SIZE=$size MXL_OUT_SCTE="$scte_json" mxl_write_config $MXL_TMP/w.json ""
    else
        MXL_INPUT_TRANSPORT="$(mxl_input_json)" MXL_OUT_SIZE=$size mxl_write_config $MXL_TMP/w.json ""
    fi
}

# worker_cpu <pid> <seconds from cpu0>: cores used since worker_cpu_start
function worker_cpu_start() { WORKER_CPU0=$(mxl_cpu_seconds $1); }
function worker_cpu_end() { awk -v a=$WORKER_CPU0 -v b=$(mxl_cpu_seconds $1) -v w=$2 'BEGIN { printf "%.2f", (b - a) / w }'; }

# worker_run_p <source kind> <size> <rate> <settle s> <window s>: run P. Leaves p.ts, and WORKER_CPU_P in cores over the window.
function worker_run_p() {
    local kind="$1" size="$2" rate="$3" settle="$4" window="$5"
    mxl_start_source $kind $size $rate
    mxl_start_engine $MXL_TMP/p.json p
    local pid=$MXL_ENGINE_PID
    sleep $settle
    worker_cpu_start $pid
    mxl_bg tsp -I ip 4444 -P until --seconds $window -O file $MXL_TMP/p.ts
    local cap=$MXL_LAST_PID
    sleep $(( window + 3 ))
    wait $cap 2> /dev/null || true
    WORKER_CPU_P=$(worker_cpu_end $pid $window)
    mxl_alive $pid || mxl_abort "the in-process engine is not running (see engine-p.log)"
    mxl_stop $pid
    mxl_stop $MXL_SRC_PID
    sleep 2
}

# worker_run_split <source kind> <size> <rate> <settle s> <window s> <data flow: yes|no>: run M+W. Leaves w.ts, WORKER_CPU_M and
# WORKER_CPU_W. The source is started again (a source of the scte kinds plays its file once), so that its events come at the same
# distance from the engines' start as in the run of P.
function worker_run_split() {
    local kind="$1" size="$2" rate="$3" settle="$4" window="$5" data="$6"
    mxl_start_source $kind $size $rate
    mxl_start_engine $MXL_TMP/main.json main
    local pid_m=$MXL_ENGINE_PID
    mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the main engine's video flow did not appear (see engine-main.log)"
    [ "$data" = "yes" ] && { mxl_wait_flow $MXL_FLOW_DATA 40 || mxl_abort "the main engine's data flow did not appear (see engine-main.log)"; }
    mxl_start_engine $MXL_TMP/w.json w
    local pid_w=$MXL_ENGINE_PID
    sleep $settle
    local cm0=$(mxl_cpu_seconds $pid_m)
    worker_cpu_start $pid_w
    mxl_bg tsp -I ip 4444 -P until --seconds $window -O file $MXL_TMP/w.ts
    local cap=$MXL_LAST_PID
    sleep $(( window + 3 ))
    wait $cap 2> /dev/null || true
    WORKER_CPU_W=$(worker_cpu_end $pid_w $window)
    WORKER_CPU_M=$(awk -v a=$cm0 -v b=$(mxl_cpu_seconds $pid_m) -v w=$window 'BEGIN { printf "%.2f", (b - a) / w }')
    mxl_alive $pid_m || mxl_abort "the main engine is not running (see engine-main.log)"
    mxl_alive $pid_w || mxl_abort "the worker is not running (see engine-w.log)"
}

# worker_streams <file>: the streams of a capture, sorted, one per line
function worker_streams() {
    ffprobe -v error -show_entries stream=codec_type,codec_name,width,height,r_frame_rate,sample_rate,channels -of csv=p=0 "$1" 2>/dev/null | sort -u
}

# worker_decode_errors <file> <seconds>: decoder complaints within the first seconds, not counting those about the pictures before the first
# keyframe (a capture starts in the middle of a group of pictures)
function worker_decode_errors() {
    ffmpeg -v error -t "$2" -i "$1" -f null - 2>&1 | grep -vE 'non-existing PPS|decode_slice_header error|no frame!|Last message repeated' | wc -l
}

# worker_frames <file>: "<video packets> <first pts s> <last pts s>"
function worker_frames() {
    ffprobe -v error -select_streams v:0 -show_entries packet=pts_time -of csv=p=0 "$1" 2>/dev/null | grep -v '^$' | sort -n \
        | awk 'NR == 1 { f = $1 } { l = $1 } END { print NR, f, l }'
}

### Failure behaviour of a split channel (Step 5, WP5; tests/tests/mxl/worker/04 to 07)

# worker_probe <name>: records 2 s of the worker's output and says whether it carries a moving picture and the tone ("yes" or "no").
function worker_probe() {
    rm -f $MXL_TMP/$1.ts
    timeout 12 ffmpeg -v error -y -i "udp://127.0.0.1:${MXL_PORT_OUT}?fifo_size=5000000&overrun_nonfatal=1" -t 2 -c copy $MXL_TMP/$1.ts >> $MXL_LOG 2>&1 || true
    [ -s $MXL_TMP/$1.ts ] || { echo no; return; }
    # A capture starts wherever the output is in its group of pictures and ffmpeg drops what comes before the first keyframe, so the
    # capture holds between nothing and 2 s of picture. The picture moves if most of the ones it holds differ (a frozen or black one
    # gives one).
    local total pics tone
    read -r total pics <<< "$(ffmpeg -v error -y -i $MXL_TMP/$1.ts -an -vf "fps=10,scale=160:90,format=gray" -f framemd5 - 2>/dev/null | awk '!/^#/ { n++; seen[$NF] = 1 } END { c = 0; for (k in seen) c++; print n + 0, c }')"
    ffmpeg -v error -y -i $MXL_TMP/$1.ts -vn -ac 1 $MXL_TMP/$1.wav >> $MXL_LOG 2>&1 || true
    tone=$(mxl_tone_db $MXL_TMP/$1.wav 1000)
    mxl_log "probe $1: ${pics:-0} different pictures of ${total:-0} sampled, tone ${tone:-none} dB"
    if [ "${total:-0}" -ge 5 ] && [ $(( ${pics:-0} * 2 )) -ge "${total:-0}" ] && awk -v t="${tone:--99}" 'BEGIN { exit !(t > -35) }'; then echo yes; else echo no; fi
}

# worker_probe_until <name> <tries>: probes until one says yes. Prints the seconds from the call to that probe, and "no" after the tries.
function worker_probe_until() {
    local t0=$(date +%s.%N) i
    for i in $(seq 1 $2); do
        if [ "$(worker_probe $1)" = "yes" ]; then mxl_seconds_since $t0; return; fi
    done
    echo no
}

# worker_log_epoch <log line>: the time of a log line ("[ MM/DD/YY HH:MM:SS.mmm ]", UTC) in seconds
function worker_log_epoch() {
    local stamp
    stamp=$(grep -o '^\[ [0-9/]* [0-9:.]* \]' <<< "$1" | tr -d '[]' | xargs)
    date -u -d "20${stamp:6:2}-${stamp:0:2}-${stamp:3:2} ${stamp:9}" +%s.%N
}

# worker_main_state: "<video flow lag ms> <head index>" of the main engine's programme flow
function worker_main_state() {
    echo "$(mxl_head_lag_ms $MXL_FLOW_VIDEO) $(mxl_flow_field $MXL_FLOW_VIDEO 'Head index')"
}

# worker_wait_writer <seconds>: waits until the head index of the programme's video flow moves (a writer is writing); prints the time in
# seconds since the epoch at which it saw it, or "no"
function worker_wait_writer() {
    local h0 h i
    h0=$(mxl_flow_field $MXL_FLOW_VIDEO 'Head index')
    for ((i = 0; i < $1 * 10; i++)); do
        h=$(mxl_flow_field $MXL_FLOW_VIDEO 'Head index')
        if [ -n "$h" ] && [ "$h" != "$h0" ]; then date +%s.%N; return; fi
        sleep 0.1
    done
    echo no
}

# worker_main_unaffected <name> <lag ms before> <cpu cores before> <cpu cores now> [seconds the main engine was watched]: the checks of Q8
# ("a dead or slow worker never touches the main engine"): it runs, its programme flow's head still keeps the time of the wall
# clock (a drift would show as a change of the lag), its CPU is what it was, and it logged no error.
function worker_main_unaffected() {
    local name="$1" lag0="$2" cpu0="$3" cpu1="$4" lag1 head
    mxl_alive $WORKER_PID_M || { mxl_fail "$name: the main engine stopped"; return; }
    read -r lag1 head <<< "$(worker_main_state)"
    echo "RESULT $name: main engine's flow lag ${lag1} ms (before: ${lag0} ms), CPU ${cpu1} cores (before: ${cpu0})"
    awk -v a="$lag1" -v b="$lag0" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 80) }' \
        || mxl_fail "$name: the main engine's flow lag moved from ${lag0} to ${lag1} ms"
    awk -v a="$cpu1" -v b="$cpu0" 'BEGIN { exit !(a <= b * 1.25 + 0.05) }' || mxl_fail "$name: the main engine's CPU went from ${cpu0} to ${cpu1} cores"
    local errors
    errors=$(grep -E 'rror' $MXL_TMP/engine-main.log | grep -v 'no such table' || true)
    [ -z "$errors" ] || { echo "$errors" | head -5 >> $MXL_LOG; mxl_fail "$name: the main engine logged errors"; }
}

# worker_cpus: sets WORKER_CPU_ONE to the first CPU this test may use and WORKER_CPU_REST to a list of the others ("" with fewer than 3)
function worker_cpus() {
    local list
    list=$(awk '/Cpus_allowed_list/ { print $2 }' /proc/self/status)
    read -r WORKER_CPU_ONE WORKER_CPU_REST <<< "$(python3 -I - "$list" <<'PY'
import sys
cpus = []
for part in sys.argv[1].split(","):
    a, _, b = part.partition("-")
    cpus += range(int(a), int(b or a) + 1)
print(cpus[0], ",".join(str(c) for c in cpus[1:]) if len(cpus) >= 3 else "")
PY
)"
}

# worker_cue_restart <victim: worker|main>: a split channel with the SCTE-35 data flow, fed by the `scte-sync` source (a splice_insert every
# 3 s from 9 s, ids 100 to 111, sent once each by the source). The worker's output is captured to w1.ts until the victim is killed (SIGKILL,
# 14 s after the source starts) and to w2.ts from the moment it is started again. The two are separate files because each engine counts its
# timestamps from its own start, so the two lives of the worker overlap in time.
function worker_cue_restart() {
    local victim="$1" pid_m pid_w cap1 cap2
    mxl_start_source scte-sync $SIZE 25
    WORKER_SOURCE_T0=$(date +%s.%N)
    mxl_start_engine $MXL_TMP/main.json main
    pid_m=$MXL_ENGINE_PID
    mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the main engine's video flow did not appear (see engine-main.log)"
    mxl_wait_flow $MXL_FLOW_DATA 40 || mxl_abort "the main engine's data flow did not appear (see engine-main.log)"
    mxl_start_engine $MXL_TMP/w.json w
    pid_w=$MXL_ENGINE_PID
    mxl_bg tsp -I ip 4444 -O file $MXL_TMP/w1.ts
    cap1=$MXL_LAST_PID
    sleep $(awk -v e=$(mxl_seconds_since $WORKER_SOURCE_T0) 'BEGIN { s = 14 - e; print (s > 0 ? s : 0) }')
    echo "RESULT the $victim is killed $(mxl_seconds_since $WORKER_SOURCE_T0) s after the source starts"
    if [ "$victim" = "worker" ]; then
        kill -9 $pid_w; mxl_stop $pid_w
        mxl_stop $cap1
        mxl_start_engine $MXL_TMP/w.json w2
    else
        kill -9 $pid_m; mxl_stop $pid_m
        # the worker goes on; its output is one capture (its time does not restart), cut here for the same two files
        mxl_stop $cap1
        mxl_start_engine $MXL_TMP/main.json main2
    fi
    WORKER_PID_RESTARTED=$MXL_ENGINE_PID
    mxl_bg tsp -I ip 4444 -P until --seconds 36 -O file $MXL_TMP/w2.ts
    cap2=$MXL_LAST_PID
    sleep 40
    wait $cap2 2> /dev/null || true
    mxl_alive $WORKER_PID_RESTARTED || mxl_fail "the restarted $victim stopped"
    if [ "$victim" = "worker" ]; then mxl_alive $pid_m || mxl_fail "the main engine stopped"; else mxl_alive $pid_w || mxl_fail "the worker stopped"; fi
}

# worker_cue_report: the events in w1.ts (before the kill) and w2.ts (after). Prints RESULT and FAIL lines: the ids of the source's events in
# each, the ones in neither (lost while the victim was down), the ones in both, and a FAIL when ids after the first one of w2 have a hole
# (an event lost after the output was back), when an event is carried twice within w2, or when the order is not the source's.
function worker_cue_report() {
    python3 -I - $MXL_TMP/w1.ts $MXL_TMP/w2.ts <<'PY'
import subprocess, sys
import xml.etree.ElementTree as ET

def events(ts):
    xml = subprocess.run(["tstables", "--pid", "500", "--tid", "0xFC", "--max-tables", "5000", "--xml-output", "-", ts], capture_output=True, text=True).stdout
    order = []
    for table in ET.fromstring(xml).iter("splice_information_table"):
        for insert in table.iter("splice_insert"):
            if insert.get("splice_event_cancel") != "true":
                order.append(int(insert.get("splice_event_id").replace(",", ""), 0))
    return order

one, two = events(sys.argv[1]), events(sys.argv[2])
def show(order):
    c = {}
    for e in order:
        c[e] = c.get(e, 0) + 1
    return c, " ".join(f"{e}x{n}" for e, n in sorted(c.items()))
c1, t1 = show(one)
c2, t2 = show(two)
print("RESULT events before the kill (id x sections):", t1 or "none")
print("RESULT events after the restart (id x sections):", t2 or "none")
lost = [e for e in range(100, 112) if e not in c1 and e not in c2]
both = [e for e in c1 if e in c2]
print("RESULT events of the source in neither output (lost while the victim was down):", " ".join(map(str, lost)) or "none")
print("RESULT events in both outputs (the cue was carried by the first life and again by the second):", " ".join(map(str, both)) or "none")
if two != sorted(two) or one != sorted(one):
    print("FAIL: the events are not in the order of the source")
if c2:
    holes = [e for e in range(min(c2), max(c2) + 1) if e not in c2]
    if holes:
        print("FAIL: events lost after the output was back:", holes)
    if max(c2) != 111:
        print("FAIL: the last event of the source (111) is not in the output after the restart")
else:
    print("FAIL: no event in the output after the restart")
PY
}

### Cues across a split channel (Step 5, plan tests S1, S2, S4)

# worker_start_barcode_source <seconds>: a live 720p25 source on the engine's input whose picture carries its frame number as 16 blocks
# across the top (read back by framework/gate_a.py), so that the picture a splice lands on can be named in any output. Sets MXL_SRC_PID.
function worker_start_barcode_source() {
    local video="color=c=black:s=192x108:r=25,format=gray,geq=lum='if(lt(Y\,24)\,235*mod(floor(N/pow(2\,floor(X/12)))\,2)+16*(1-mod(floor(N/pow(2\,floor(X/12)))\,2))\,if(lt(mod(N\,25)\,2)\,235\,16))',scale=1280:720:flags=neighbor,format=yuv420p"
    mxl_bg ffmpeg -v error -re -f lavfi -i "$video" -re -f lavfi -i "sine=frequency=1000:sample_rate=48000" -t "$1" \
        -c:v libx264 -preset veryfast -g 50 -b:v 3M -c:a aac -ac 2 -f mpegts "udp://127.0.0.1:${MXL_PORT_IN}?pkt_size=1316"
    MXL_SRC_PID=$MXL_LAST_PID
}

# worker_event <engine tag> <seconds from now> <type> <details json>: a scheduler event in the database of a running engine
function worker_event() {
    local db="$MXL_TMP/db/e-$1.sqlite"
    sqlite3 "$db" "INSERT INTO events(type, time, details, creation_time, read_time) VALUES('$3', '$(date -u '+%Y-%m-%d %H:%M:%S.%N' -d "+$2 seconds")Z', '$4', '$(date -u '+%Y-%m-%d %H:%M:%S.%N' -d '-30 seconds')Z', NULL);"
}

# worker_start_engine_db <config> <tag>: as mxl_start_engine, but with the events table in the database before the engine opens it
function worker_start_engine_db() {
    mxl_start_engine "$1" "$2"
    sqlite3 "$MXL_TMP/db/e-$2.sqlite" < "$MXL_FRAMEWORK_DIR/schema/db.sql"
}
