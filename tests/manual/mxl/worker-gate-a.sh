#!/usr/bin/env bash
# The Gate A run of a split channel (Step 5): a channel done in one engine (P) and the same channel as a main engine plus one or two
# workers, side by side, fed by the same live 1080p25 source for the same minutes. The source's picture carries its frame number, so a lost
# or repeated picture is seen, and so is the picture a splice lands on; splice_insert cues are put into the live source for known pictures.
# Judged (limits agreed 2026-11-11): no picture lost; lip-sync within 40 ms of P; a source picture arrives at most delayMs + 2 frames later
# than from P; CPU of main plus worker at most 1.25 times P (one worker); every cue within one picture of the authored one.
# Also recorded every minute: memory, CPU, late-flow reports, reattaches and errors. Needs a quiet host.
#
#   tests/manual/mxl/worker-gate-a.sh [minutes] [workers 1|2] [cues] [output directory]      default: 60 1 20 (a new directory in /var/tmp)

if [ "$1" = "--inside" ]; then
    MINUTES=$2; WORKERS=$3; CUES=$4
    . /tests/framework/mxl.sh
    . /tests/framework/mxl_worker.sh
    worker_domain_in_memory
    mxl_test_begin /out
    trap 'mxl_cleanup; rm -rf "$MXL_TEST_DOMAIN"' EXIT
    mxl_require_engine
    mxl_require mxlsink mxlsrc mxl-info tsp tstables

    FW=/tests/framework
    SECONDS_TOTAL=$(( MINUTES * 60 ))
    DELAY_MS=500
    export MXL_OUT_SIZE=1920x1080

    MXL_INPUT_TRANSPORT="\"uri\": \"udp://127.0.0.1:2223\"" MXL_OUT_PORT=4445 MXL_OUT_SCTE="$WORKER_SCTE" mxl_write_config $MXL_TMP/p.json ""
    cat > $MXL_TMP/main.json <<JSON
{ "version": "1.1.0",
  "application": { "logger": { "transcoder": { "level": "debug", "console": { "enable": true, "colors": false, "stderr": false } } } },
  "transcoder": {
    "inputs": [ { "transport": { "uri": "udp://127.0.0.1:2222" } } ],
    "outputs": [ { "type": "mxl", "name": "programme", "mxl": { "domain": "$MXL_DOMAIN", "video": { "flowId": "$MXL_FLOW_VIDEO" },
                   "audio": { "flowId": "$MXL_FLOW_AUDIO", "channels": 2 }, "data": { "flowId": "$MXL_FLOW_DATA" } } } ] } }
JSON
    names=(w1); ports=(4444)
    [ $WORKERS = 2 ] && { names+=(w2); ports+=(4446); }
    for i in "${!names[@]}"; do
        MXL_INPUT_TRANSPORT="$(MXL_INPUT_DATA_FLOW=$MXL_FLOW_DATA mxl_input_json)" MXL_OUT_PORT=${ports[$i]} MXL_OUT_SCTE="$WORKER_SCTE" \
            mxl_write_config $MXL_TMP/${names[$i]}.json ""
    done
    # debug, not trace: an hour of trace is gigabytes
    sed -i 's/"level": "trace"/"level": "debug"/' $MXL_TMP/*.json

    # The source: a busy moving picture (testsrc2, so the encoders have work to do) with two bands drawn small and scaled up. Top 200 rows: the
    # frame number, 16 blocks, least significant first. Bottom 90 rows: a flash for 2 frames at the start of every second, with a beep.
    VIDEO="testsrc2=s=1920x1080:r=25[bg];color=c=black:s=192x20:r=25,format=gray,geq=lum='235*mod(floor(N/pow(2\,floor(X/12)))\,2)+16*(1-mod(floor(N/pow(2\,floor(X/12)))\,2))',scale=1920:200:flags=neighbor,format=yuv420p[bar];color=c=black:s=192x9:r=25,format=gray,geq=lum='if(lt(mod(N\,25)\,2)\,235\,16)',scale=1920:90:flags=neighbor,format=yuv420p[fl];[bg][bar]overlay=0:0[a];[a][fl]overlay=0:990,format=yuv420p"
    AUDIO="aevalsrc='if(lt(mod(t\,1)\,0.04)\,0.5*sin(2*PI*1000*t)\,0)':s=48000:c=stereo"

    # The engines first, so that all of them see the source from its first picture
    mxl_bg python3 -I $FW/gate_a.py relay 2221 $MXL_TMP/source.ts $(( SECONDS_TOTAL + 20 )) 2222 2223
    mxl_start_engine $MXL_TMP/p.json p;       pid_p=$MXL_ENGINE_PID
    mxl_start_engine $MXL_TMP/main.json main; pid_m=$MXL_ENGINE_PID
    ffmpeg -v error -re -f lavfi -i "$VIDEO" -re -f lavfi -i "$AUDIO" -c:v libx264 -preset veryfast -g 50 -b:v 3M -c:a aac -ac 2 -muxrate 5M -f mpegts - 2>> $MXL_LOG \
        | tsp --realtime --initial-input-packets 70 --max-flushed-packets 70 --max-input-packets 70 -I file -P pmt --service 1 --add-programinfo-id 0x43554549 --add-pid 500/0x86 \
              -P spliceinject --service 1 --pid 500 --udp 5000 --inject-count 1 \
              -P filter --negate --pid 0x1FFF -O ip --enforce-burst --packet-burst 7 127.0.0.1:2221 >> $MXL_LOG 2>&1 &
    MXL_PIDS+=($!)
    mxl_wait_flow $MXL_FLOW_VIDEO 60 || mxl_abort "the main engine's video flow did not appear (see engine-main.log)"
    mxl_wait_flow $MXL_FLOW_DATA 40 || mxl_abort "the main engine's data flow did not appear (see engine-main.log)"
    pids_w=()
    for n in "${names[@]}"; do mxl_start_engine $MXL_TMP/$n.json $n; pids_w+=($MXL_ENGINE_PID); done
    sleep 15

    CAPTURE=$(( SECONDS_TOTAL - 30 ))
    caps=()
    mxl_bg python3 -I $FW/gate_a.py capture 4445 $MXL_TMP/p.ts $CAPTURE; caps+=($MXL_LAST_PID)
    for i in "${!names[@]}"; do mxl_bg python3 -I $FW/gate_a.py capture ${ports[$i]} $MXL_TMP/${names[$i]}.ts $CAPTURE; caps+=($MXL_LAST_PID); done
    mxl_bg python3 -I $FW/gate_a.py cues $MXL_TMP/source.ts 5000 $CUES $(( (CAPTURE - 60) / CUES )) $MXL_TMP/cues.log

    all=(p main "${names[@]}"); all_pids=($pid_p $pid_m "${pids_w[@]}")
    series=$MXL_TMP/series.tsv
    printf 'minute\tname\trss_mb\tcpu_cores\tlate_reports\treattaches\terrors\n' > $series
    declare -A c0
    for i in "${!all[@]}"; do c0[${all[$i]}]=$(mxl_cpu_seconds ${all_pids[$i]}); done
    t0=$(date +%s.%N); t_first=$t0; declare -A cfirst; for n in "${all[@]}"; do cfirst[$n]=${c0[$n]}; done
    for ((m = 1; m <= MINUTES - 1; m++)); do
        sleep 58
        t1=$(date +%s.%N)
        for i in "${!all[@]}"; do
            n=${all[$i]}; pid=${all_pids[$i]}
            mxl_alive $pid || { mxl_fail "engine $n died at minute $m"; continue; }
            rss=$(awk '/VmRSS/ { printf "%.1f", $2 / 1024 }' /proc/$pid/status)
            c=$(mxl_cpu_seconds $pid)
            cores=$(awk -v a=${c0[$n]} -v b=$c -v t0=$t0 -v t1=$t1 'BEGIN { printf "%.3f", (b - a) / (t1 - t0) }')
            c0[$n]=$c
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' $m $n $rss $cores \
                $(grep -c 'later than the .* ms delay allows' $MXL_TMP/engine-$n.log || true) \
                $(grep -c 'no data on the .* flow' $MXL_TMP/engine-$n.log || true) \
                $( { grep -E '^\[[^]]+\] \[[^]]+\] \[ *(error|critical) *\]|Received an error message|side pipeline error' $MXL_TMP/engine-$n.log || true; } | grep -vc 'no such table' || true ) >> $series
        done
        t0=$t1
    done
    declare -A mean
    for i in "${!all[@]}"; do
        n=${all[$i]}
        mean[$n]=$(awk -v a=${cfirst[$n]} -v b=$(mxl_cpu_seconds ${all_pids[$i]}) -v t0=$t_first -v t1=$(date +%s.%N) 'BEGIN { printf "%.3f", (b - a) / (t1 - t0) }')
    done
    wait "${caps[@]}" 2> /dev/null || true
    mxl_cleanup

    echo "RESULT mean CPU in cores over $(( MINUTES - 1 )) min: $(for n in "${all[@]}"; do printf '%s %s  ' $n ${mean[$n]}; done)"
    if [ $WORKERS = 1 ]; then
        ratio=$(awk -v m=${mean[main]} -v w=${mean[w1]} -v p=${mean[p]} 'BEGIN { printf "%.2f", (m + w) / p }')
        echo "RESULT CPU of main plus worker over in-process: $ratio (limit 1.25)"
        awk -v r=$ratio 'BEGIN { exit !(r <= 1.25) }' || mxl_fail "CPU ratio $ratio over 1.25"
    fi
    awk -F'\t' 'NR > 1 { if (!($2 in first)) { first[$2] = $3; } last[$2] = $3; late[$2] = $5; re[$2] = $6; err[$2] = $7 }
        END { for (n in first) printf "RESULT %s: memory %s MB at minute 1, %s MB at the end; late-flow reports %s, reattaches %s, errors %s\n", n, first[n], last[n], late[n], re[n], err[n] }' $series | sort
    python3 -I $FW/gate_a.py analyse $MXL_TMP $DELAY_MS "${names[@]}" | tee $MXL_TMP/analysis.txt
    [ ${PIPESTATUS[0]} = 0 ] || mxl_fail "Gate A limits not met (see analysis)"
    echo "EXIT $MXL_RC"
    exit $MXL_RC
fi

MINUTES=${1:-60}; WORKERS=${2:-1}; CUES=${3:-20}
OUT=${4:-$(mktemp -d /var/tmp/worker-gate-a-XXXXXX)}
mkdir -p "$OUT"
ROOT=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )/../../.." &> /dev/null && pwd )
echo "output: $OUT"
docker run --rm --user "$(id -u):$(id -g)" --shm-size=1g --entrypoint bash -v "$OUT:/out" -v "$ROOT/tests:/tests" utpl/transcoder-test:mxl \
    -c "bash /tests/manual/mxl/worker-gate-a.sh --inside $MINUTES $WORKERS $CUES"
