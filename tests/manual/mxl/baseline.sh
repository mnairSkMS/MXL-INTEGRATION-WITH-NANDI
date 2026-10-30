#!/usr/bin/env bash
# Phase 0 baseline (tests T0/T1): what the engine costs today, with no MXL in the path.
# Generates a 1080p25 H.264/AAC UDP source, runs the real engine with N outputs, and records
# CPU per thread, memory, a graph snapshot (GStreamer pipeline-snapshot tracer, no code change)
# and an ffprobe/decode check of each output. Run it on :baseline and on :mxl to compare.
#
#   tests/manual/mxl/baseline.sh [image] [outputs 1|2] [warm-up s] [measure s]
#   default: utpl/transcoder-test:baseline 2 15 30

IMAGE="${1:-utpl/transcoder-test:baseline}"
OUTPUTS="${2:-2}"
WARM="${3:-15}"
MEASURE="${4:-30}"
ROOT=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )/../../.." &> /dev/null && pwd )
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

python3 -I - "$OUTPUTS" > "$WORK/config.json" <<'PY'
import json, sys
n = int(sys.argv[1])
def stream_set(w, h, vb):
    return [
        {"type": "audio", "codec": "aac", "audio": {"rate": 48000, "channels": 2},
         "aac": {"encoder": "fdkaacenc", "properties": {"bitrate": "128000"}, "caps": {}}},
        {"type": "video", "codec": "h264", "video": {"width": w, "height": h},
         "h264": {"encoder": "x264enc",
                  "properties": {"key-int-max": "50", "tune": "zerolatency", "speed-preset": "veryfast", "bitrate": str(vb)},
                  "caps": {"profile": "high"}}}]
specs = [(1920, 1080, 5000, 4444), (1280, 720, 2500, 4445)][:n]
cfg = {"version": "1.0.7",
       "application": {"logger": {"transcoder": {"level": "trace", "console": {"enable": True, "colors": False, "stderr": False}}}},
       "transcoder": {"inputs": [{"uri": "udp://127.0.0.1:2222"}],
                      "outputs": [{"uri": "udp://127.0.0.1:%d" % port, "container": "mpegts", "streams": stream_set(w, h, vb)}
                                  for (w, h, vb, port) in specs]}}
print(json.dumps(cfg, indent=1))
PY

docker run --rm --user "$(id -u):$(id -g)" --entrypoint bash \
    -e OUTPUTS="$OUTPUTS" -e WARM="$WARM" -e MEASURE="$MEASURE" \
    -v "$WORK:/work" -v "${OUT_DIR:-$WORK}:/out" -v "$ROOT/transcoder/license/prod/licenses:/lic:ro" "$IMAGE" -c '
set -u
mkdir -p /tmp/db; : > /tmp/db/e.sqlite
# Source: 1080p25, in real time, to the engine input.
ffmpeg -v error -re -f lavfi -i testsrc2=size=1920x1080:rate=25 -f lavfi -i sine=frequency=440:sample_rate=48000 \
    -c:v libx264 -preset veryfast -g 50 -b:v 6M -c:a aac -ac 2 -f mpegts "udp://127.0.0.1:2222?pkt_size=1316" > /work/src.log 2>&1 &
SRC=$!
GST_TRACERS="latency(flags=pipeline)" GST_DEBUG="GST_PADS:4,GST_ELEMENT_FACTORY:4,GST_TRACER:7" GST_DEBUG_NO_COLOR=1 \
    /opt/utpl/bin/transcoder -c /work/config.json -l /lic/offline.lic -b /tmp/db/e.sqlite > /work/engine.log 2>&1 &
ENG=$!
sleep "$WARM"
kill -0 $ENG 2>/dev/null || { echo "engine died during warm-up"; tail -n 8 /work/engine.log | cut -c1-200; exit 1; }

snap() { for t in /proc/$1/task/*; do awk -v n="$(cat $t/comm)" "{print \$14+\$15, n}" $t/stat 2>/dev/null; done | sort -k2 | awk "{a[\$2]+=\$1} END{for(k in a) print a[k], k}"; }
ticks() { awk "{print \$14+\$15}" /proc/$1/stat; }
snap $ENG | sort -k2 > /work/t0; E0=$(ticks $ENG); S0=$(ticks $SRC)

# Capture each output for the measurement window (also proves they are being produced).
for p in $(seq 0 $((OUTPUTS-1))); do
    port=$((4444+p))
    ffmpeg -v error -y -i "udp://127.0.0.1:$port?fifo_size=5000000&overrun_nonfatal=1" -t "$MEASURE" -c copy -f mpegts /work/out$p.ts > /work/cap$p.log 2>&1 &
done
sleep "$MEASURE"; wait $(jobs -p | grep -v -e "^$SRC\$" -e "^$ENG\$") 2>/dev/null
snap $ENG | sort -k2 > /work/t1; E1=$(ticks $ENG); S1=$(ticks $SRC)

echo "=== engine: $OUTPUTS output(s), measured over ${MEASURE}s after ${WARM}s warm-up"
awk -v a=$E0 -v b=$E1 -v s=$MEASURE "BEGIN{printf \"engine CPU: %.2f cores (%.1f CPU-seconds)\n\", (b-a)/100/s, (b-a)/100}"
awk -v a=$S0 -v b=$S1 -v s=$MEASURE "BEGIN{printf \"test source CPU (ffmpeg, not the engine): %.2f cores\n\", (b-a)/100/s}"
echo "--- engine threads by CPU, top 12 (cores over the window)"
join -1 2 -2 2 -a1 -e0 -o 0,1.1,2.1 /work/t0 /work/t1 2>/dev/null | awk -v s=$MEASURE "{d=(\$3-\$2)/100/s; if (d>0.005) printf \"%6.3f  %s\n\", d, \$1}" | sort -rn | head -12
echo "--- memory"
grep -E "VmRSS|VmHWM" /proc/$ENG/status
echo "threads: $(ls /proc/$ENG/task | wc -l)"

echo "--- latency (latency tracer, steady state after the warm-up) and graph"
python3 -I - /work/engine.log "$WARM" /out <<'"'"'PY'"'"'
import re, sys, collections
log, warm, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
txt = re.sub(r"\x1b\[[0-9;]*m", "", open(log, errors="replace").read())
rec = collections.defaultdict(list)
for m in re.finditer(r" latency, src-element-id=\(string\)\S+ src-element=\(string\)(\S+?), src=\(string\)\S+ sink-element-id=\(string\)\S+ sink-element=\(string\)(\S+?), sink=\(string\)\S+ time=\(guint64\)(\d+), ts=\(guint64\)(\d+);", txt):
    if int(m[4]) > warm * 1e9:
        rec[(m[1], m[2])].append(int(m[3]) / 1e6)
def pct(v, p):
    v = sorted(v); return v[min(len(v) - 1, int(len(v) * p / 100))]
print("source -> sink            samples   p50 ms   p95 ms   p99 ms   max ms")
for (a, b), v in sorted(rec.items(), key=lambda x: -len(x[1])):
    print("%-12s -> %-12s %6d %8.0f %8.0f %8.0f %8.0f" % (a, b, len(v), pct(v, 50), pct(v, 95), pct(v, 99), max(v)))
c = collections.Counter(re.findall(r"creating element \"([^\"]+)\"", txt))
print("elements created (%d): %s" % (sum(c.values()), ", ".join("%s x%d" % kv for kv in c.most_common())))
links = re.findall(r" linked (\S+):\S+ and (\S+):\S+, successful", txt)
print("pad links:", len(links))
open(out + "/links.txt", "w").write("\n".join("%s -> %s" % l for l in links) + "\n")
PY

echo "--- outputs"
for p in $(seq 0 $((OUTPUTS-1))); do
    echo "output $p:"
    ffprobe -v error -show_entries stream=codec_type,codec_name,width,height,r_frame_rate,sample_rate,channels,bit_rate -of compact /work/out$p.ts
    ffmpeg -v error -i /work/out$p.ts -f null - 2>&1 | head -3; echo "  decode errors above (none = clean); duration $(ffprobe -v error -show_entries format=duration -of csv=p=0 /work/out$p.ts) s"
done
kill $ENG $SRC 2>/dev/null; wait 2>/dev/null; cp /work/engine.log /out/engine.log 2>/dev/null; exit 0
'
