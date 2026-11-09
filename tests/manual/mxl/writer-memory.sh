#!/usr/bin/env bash
# Where does the memory of a channel that writes MXL flows go? The 4-hour input soak showed the writing channel growing
# in a straight line (about 18 MB per hour) while the reading channel stays flat. This runs one writing channel on its
# own, in one of several variants, and records what grows: the process's memory split into heap-like (anonymous) and
# file-backed (the flows' memory), the number of threads and open files, and at the end which memory areas grew.
# Run the variants side by side (one container each) and compare the slopes.
#
#   tests/manual/mxl/writer-memory.sh <variant> [minutes] [image]     default: 40 minutes, utpl/transcoder-test:mxl
#
# Variants:
#   no-mxl         the channel of the soak with no MXL at all: input, decode, two encoders, UDP output
#   source-tap     the same, with the input's source tap (inputs[0].mxlTap) and nothing else
#   programme      the same, with a programme output of type "mxl" (this is channel A of the input soak)
#   both           source tap and programme output, on different flows
#   plain-mxlsink  no engine: gst-launch writes a synthetic picture and sound with mxlsink (the plugin alone)
#   gst-demux      no engine: gst-launch takes the source's MPEG-TS apart (tsdemux and the parsers) and throws the pieces away
#   gst-decode     no engine: gst-launch takes the source apart and decodes it (avdec_h264, avdec_aac)
#   gst-decodebin  no engine: gst-launch decodes the source with decodebin, as the engine does
#   gst-h264       no engine: only the picture is decoded (avdec_h264); the sound is thrown away undecoded
#   gst-h264-heaptrack  the same under heaptrack (installed in the container with apt): where the memory is allocated
#   gst-h264-t1    the same with avdec_h264 limited to one thread (max-threads=1)
#   gst-aac        no engine: only the sound is decoded (avdec_aac); the picture is thrown away undecoded
#   ffmpeg-decode  no engine, no GStreamer: the ffmpeg command line decodes the source's picture and throws it away
# WM_SRC=testsrc2 uses a cheaper source than the flash-and-beep one for the gst-* and ffmpeg variants, and
# WM_X264_OPTS="-bf 0" adds options to that source's encoder.
#
# WM_SIGNAL_AT="8 24" sends SIGUSR1 to the process at those minutes (with GST_TRACERS=leaks GST_LEAKS_TRACER_SIG=1
# GST_DEBUG=GST_TRACER:7 in WM_ENV, the engine then lists the GStreamer objects that are alive), and WM_KEEP_LOG=1
# keeps the process's log in the output directory.
#
# Output goes to $WM_OUT (default: a new directory under /tmp), as writer-memory-<tag>.tsv and .txt, where the tag is
# $WM_TAG, or the variant when that is not set. WM_ENV="NAME=value ..." sets environment variables for the process
# (for example MALLOC_ARENA_MAX=2), and WM_TAG then tells the runs apart.

VARIANT="${1:-programme}"

if [ "$1" = "--inside" ]; then
    VARIANT="$2"
    WM_MINUTES="${WM_MINUTES:-40}"
    . /tests/framework/mxl.sh
    mxl_test_begin /tmp/wm
    mxl_require mxlsink mxlsrc mxl-info

    FLOW_V2=0b2c5d90-5a63-4e5d-a0a7-6b0b2a8c1f01
    FLOW_A2=0b2c5d90-5a63-4e5d-a0a7-6b0b2a8c1f02

    case "$VARIANT" in
        plain-mxlsink)
            mxl_bg gst-launch-1.0 -q \
                videotestsrc is-live=true do-timestamp=true pattern=smpte \
                    ! videoconvert ! "video/x-raw,format=v210,width=1280,height=720,framerate=25/1" \
                    ! queue ! mxlsink flow-id="$MXL_FLOW_VIDEO" domain="$MXL_DOMAIN" \
                audiotestsrc is-live=true do-timestamp=true wave=sine freq=1000 volume=0.5 samplesperbuffer=480 \
                    ! "audio/x-raw,format=F32LE,rate=48000,channels=2,layout=interleaved,channel-mask=(bitmask)0x3" \
                    ! queue ! mxlsink flow-id="$MXL_FLOW_AUDIO" domain="$MXL_DOMAIN"
            pid=$MXL_LAST_PID
            ;;
        gst-demux | gst-decode | gst-decodebin | gst-h264 | gst-h264-t1 | gst-h264-heaptrack | gst-aac | ffmpeg-decode)
            if [ -n "$WM_SRC" ]; then
                mxl_bg ffmpeg -v error -re -f lavfi -i "testsrc2=size=1280x720:rate=25" -f lavfi -i "sine=frequency=1000:sample_rate=48000" \
                    -c:v libx264 -preset veryfast -g 50 -b:v 3M $WM_X264_OPTS -c:a aac -ac 2 -f mpegts "udp://127.0.0.1:${MXL_PORT_IN}?pkt_size=1316"
            else
                mxl_start_source sync
            fi
            case "$VARIANT" in
                gst-demux)
                    mxl_bg gst-launch-1.0 -q udpsrc port=$MXL_PORT_IN caps="video/mpegts,systemstream=true,packetsize=188" ! tsdemux name=d \
                        d. ! queue ! h264parse ! fakesink sync=false  d. ! queue ! aacparse ! fakesink sync=false ;;
                gst-decode)
                    mxl_bg gst-launch-1.0 -q udpsrc port=$MXL_PORT_IN caps="video/mpegts,systemstream=true,packetsize=188" ! tsdemux name=d \
                        d. ! queue ! h264parse ! avdec_h264 ! fakesink sync=false  d. ! queue ! aacparse ! avdec_aac ! fakesink sync=false ;;
                ffmpeg-decode)
                    mxl_bg ffmpeg -v error -i "udp://127.0.0.1:${MXL_PORT_IN}?fifo_size=20000&overrun_nonfatal=1" -map 0:v -f null - ;;
                gst-h264)
                    mxl_bg gst-launch-1.0 -q udpsrc port=$MXL_PORT_IN caps="video/mpegts,systemstream=true,packetsize=188" ! tsdemux name=d \
                        d. ! queue ! h264parse ! avdec_h264 ! fakesink sync=false  d. ! queue ! aacparse ! fakesink sync=false ;;
                gst-h264-heaptrack)
                    # heaptrack is installed inside this throwaway container only; its data goes to /out (WM_OUT)
                    (apt-get update -qq && apt-get install -y -qq heaptrack) > /dev/null 2>&1 || mxl_abort "heaptrack could not be installed"
                    mxl_bg heaptrack -o /out/heaptrack-h264 gst-launch-1.0 -q udpsrc port=$MXL_PORT_IN caps="video/mpegts,systemstream=true,packetsize=188" ! tsdemux name=d \
                        d. ! queue ! h264parse ! avdec_h264 ! fakesink sync=false  d. ! queue ! aacparse ! fakesink sync=false ;;
                gst-h264-t1)
                    mxl_bg gst-launch-1.0 -q udpsrc port=$MXL_PORT_IN caps="video/mpegts,systemstream=true,packetsize=188" ! tsdemux name=d \
                        d. ! queue ! h264parse ! avdec_h264 max-threads=1 ! fakesink sync=false  d. ! queue ! aacparse ! fakesink sync=false ;;
                gst-aac)
                    mxl_bg gst-launch-1.0 -q udpsrc port=$MXL_PORT_IN caps="video/mpegts,systemstream=true,packetsize=188" ! tsdemux name=d \
                        d. ! queue ! h264parse ! fakesink sync=false  d. ! queue ! aacparse ! avdec_aac ! fakesink sync=false ;;
                gst-decodebin)
                    mxl_bg gst-launch-1.0 -q udpsrc port=$MXL_PORT_IN caps="video/mpegts,systemstream=true,packetsize=188" ! decodebin name=d \
                        d. ! queue ! fakesink sync=false  d. ! queue ! fakesink sync=false ;;
            esac
            pid=$MXL_LAST_PID
            ;;
        no-mxl | source-tap | programme | both)
            mxl_require_engine
            tap=""; extra=""
            case "$VARIANT" in
                source-tap) tap="$(mxl_tap_json)" ;;
                programme)  extra="$(mxl_programme_json)" ;;
                both)       tap="$(mxl_tap_json)"
                            extra="$(mxl_programme_json | sed "s/$MXL_FLOW_VIDEO/$FLOW_V2/; s/$MXL_FLOW_AUDIO/$FLOW_A2/")" ;;
            esac
            MXL_OUT_PORT=4445 MXL_EXTRA_OUTPUT="$extra" mxl_write_config "$MXL_TMP/a.json" "$tap"
            mxl_start_source sync
            mxl_start_engine "$MXL_TMP/a.json" a
            pid=$MXL_ENGINE_PID
            ;;
        *)
            echo "unknown variant: $VARIANT" >&2
            exit 2
            ;;
    esac

    if [ "$VARIANT" != "no-mxl" ] && [[ "$VARIANT" != gst-* ]] && [ "$VARIANT" != "ffmpeg-decode" ]; then
        mxl_wait_flow "$MXL_FLOW_VIDEO" 40 || mxl_abort "the video flow did not appear"
        mxl_wait_flow "$MXL_FLOW_AUDIO" 40 || mxl_abort "the audio flow did not appear"
    fi
    sleep 30      # the first seconds are startup

    # One line per pass: pid's memory in MB (total, anonymous, file-backed, shared), threads, open files, CPU seconds.
    snap() {
        awk -v m="$1" '
            /^VmRSS:/     { rss = $2 / 1024 }
            /^RssAnon:/   { anon = $2 / 1024 }
            /^RssFile:/   { file = $2 / 1024 }
            /^RssShmem:/  { shm = $2 / 1024 }
            /^Threads:/   { thr = $2 }
            END { printf "%s\t%.1f\t%.1f\t%.1f\t%.1f\t%s", m, rss, anon, file, shm, thr }' "/proc/$pid/status"
        printf '\t%s\t%s\t%s\n' "$(ls "/proc/$pid/fd" 2> /dev/null | wc -l)" "$(mxl_cpu_seconds "$pid")" "$(awk '/^VmSwap:/ { printf "%.1f", $2 / 1024 }' "/proc/$pid/status")"
    }

    series="$MXL_TMP/series.tsv"
    printf 'minute\trss_mb\tanon_mb\tfile_mb\tshmem_mb\tthreads\tfds\tcpu_s\tswap_mb\n' | tee "$series"

    # The memory map at the start, to be compared with the one at the end.
    cp "/proc/$pid/smaps" "$MXL_TMP/smaps-start" 2> /dev/null || true

    for ((m = 1; m <= WM_MINUTES; m++)); do
        sleep 60
        mxl_alive "$pid" || { mxl_fail "the process died at minute $m"; break; }
        snap "$m" | tee -a "$series"
        if [[ " $WM_SIGNAL_AT " == *" $m "* ]]; then
            echo "=== SIGNAL at minute $m ===" >> "$MXL_TMP/engine-a.log"
            kill -USR1 "$pid"
            sleep 5
            echo "=== END SIGNAL at minute $m ===" >> "$MXL_TMP/engine-a.log"
        fi
    done

    cp "/proc/$pid/smaps" "$MXL_TMP/smaps-end" 2> /dev/null || true

    slope() {  # least-squares slope of column $1 in MB per hour, from minute $2 on
        awk -F'\t' -v c="$1" -v from="$2" 'NR > 1 && $1 >= from { sx += $1; sy += $c; sxx += $1 * $1; sxy += $1 * $c; n++ }
            END { if (n < 2) { print "n/a"; exit } d = n * sxx - sx * sx; printf "%.1f", 60 * (n * sxy - sx * sy) / d }' "$series"
    }

    {
        echo "variant $VARIANT${WM_TAG:+ ($WM_TAG)}, $WM_MINUTES minutes"
        echo "RESULT total memory: $(slope 2 10) MB per hour (from minute 10), now $(awk -F'\t' 'END { print $2 }' "$series") MB"
        echo "RESULT anonymous memory: $(slope 3 10) MB per hour"
        echo "RESULT file-backed memory: $(slope 4 10) MB per hour"
        echo "RESULT footprint (resident + swapped out, which is what the process really holds): $(awk -F'\t' 'NR > 1 { $2 = $2 + $9; print }' OFS='\t' "$series" | awk -F'\t' -v from=10 'NR > 0 && $1 >= from { sx += $1; sy += $2; sxx += $1 * $1; sxy += $1 * $2; n++ } END { if (n < 2) { print "n/a"; exit } d = n * sxx - sx * sx; printf "%.1f", 60 * (n * sxy - sx * sy) / d }') MB per hour, now $(awk -F'\t' 'END { printf "%.1f", $2 + $9 }' "$series") MB (swapped out now: $(awk -F'\t' 'END { print $9 }' "$series") MB)"
        echo "RESULT shared memory: $(slope 5 10) MB per hour"
        echo "RESULT threads: $(awk -F'\t' 'NR == 2 { a = $6 } END { print a " -> " $6 }' "$series"), open files: $(awk -F'\t' 'NR == 2 { a = $7 } END { print a " -> " $7 }' "$series")"
        echo
        echo "Memory areas that grew (resident MB at the start -> at the end), largest first:"
        python3 -I - "$MXL_TMP/smaps-start" "$MXL_TMP/smaps-end" <<'PY'
import re, sys

def read(path):
    areas, cur = {}, None
    try:
        lines = open(path, errors="replace").read().splitlines()
    except OSError:
        return areas
    for line in lines:
        m = re.match(r"^([0-9a-f]+)-([0-9a-f]+) (\S+) \S+ \S+ \S+\s*(.*)$", line)
        if m:
            cur = (m[1], m[2], m[3], m[4] or "[anon]")
            areas[cur] = 0
        elif line.startswith("Rss:") and cur:
            areas[cur] = int(line.split()[1])
    return areas

a, b = read(sys.argv[1]), read(sys.argv[2])
rows = []
for k, v in b.items():
    d = v - a.get(k, 0)
    if d >= 256:
        rows.append((d, a.get(k, 0), v, k))
rows.sort(reverse=True)
for d, v0, v1, k in rows[:12]:
    print(f"  +{d / 1024:6.1f} MB  {v0 / 1024:6.1f} -> {v1 / 1024:6.1f}  {k[0]}-{k[1]} {k[2]} {k[3]}")
tot = sum(b.values()) - sum(a.values())
print(f"  total resident change over all areas: {tot / 1024:+.1f} MB")
PY
    } | tee "$MXL_TMP/summary.txt"

    if [ -n "$WM_OUT" ]; then
        cp "$series" "$WM_OUT/writer-memory-${WM_TAG:-$VARIANT}.tsv"
        cp "$MXL_TMP/summary.txt" "$WM_OUT/writer-memory-${WM_TAG:-$VARIANT}.txt"
        cp "$MXL_TMP/smaps-end" "$WM_OUT/smaps-end-${WM_TAG:-$VARIANT}.txt" 2> /dev/null || true
        [ -n "$WM_KEEP_LOG" ] && gzip -c "$MXL_TMP/engine-a.log" > "$WM_OUT/engine-${WM_TAG:-$VARIANT}.log.gz" 2> /dev/null
    fi
    chmod -R a+rwX /out 2> /dev/null || true
    mxl_test_end
fi

IMAGE="${3:-utpl/transcoder-test:mxl}"
ROOT=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )/../../.." &> /dev/null && pwd )
OUT="${WM_OUT:-$(mktemp -d)}"
mkdir -p "$OUT"
extra=()
if [ -n "$MXL_ENGINE_BUILD_DIR_HOST" ]; then
    extra=(-e MXL_ENGINE_BUILD_DIR=/dev-build -v "$MXL_ENGINE_BUILD_DIR_HOST:/dev-build:ro")
fi
# Extra environment for the engine (for example MALLOC_ARENA_MAX=1), passed as WM_ENV="NAME=value NAME=value".
for kv in $WM_ENV; do extra+=(-e "$kv"); done
user=(--user "$(id -u):$(id -g)")
[ "$VARIANT" = "gst-h264-heaptrack" ] && user=()     # apt needs root inside the throwaway container
docker run --rm "${user[@]}" --entrypoint bash "${extra[@]}" \
    -e WM_MINUTES="${2:-40}" -e WM_TAG="$WM_TAG" -e WM_SIGNAL_AT="$WM_SIGNAL_AT" -e WM_KEEP_LOG="$WM_KEEP_LOG" -e WM_SRC="$WM_SRC" -e WM_X264_OPTS="$WM_X264_OPTS" -e WM_OUT=/out -v "$OUT:/out" -v "$ROOT/tests:/tests" "$IMAGE" \
    -c "bash /tests/manual/mxl/writer-memory.sh --inside $VARIANT"
status=$?
echo "results in $OUT"
exit $status
