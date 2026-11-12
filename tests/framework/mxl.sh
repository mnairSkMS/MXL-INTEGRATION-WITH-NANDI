# Helpers for the MXL SDK tests (tests/tests/mxl/sdk). These tests exercise MXL and its GStreamer
# plugin directly, with no transcoder, so they use only the framework's logging and return codes
# instead of test.sh (which is built around starting a transcoder).
#
# Usage in a test:
#   . ${SCRIPT_DIR}/../../../../framework/mxl.sh
#   mxl_test_begin "$SCRIPT_DIR"
#   ... mxl_check "what is checked" <command...> ; mxl_fail "message" ...
#   mxl_test_end

MXL_FRAMEWORK_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

. ${MXL_FRAMEWORK_DIR}/config.sh
. ${MXL_FRAMEWORK_DIR}/log.sh

# Fixed flow ids, so that every run, and every container of a cross-container test, uses the same.
MXL_FLOW_VIDEO=5fbec3b1-1b0f-417d-9059-8b94a47197ed
MXL_FLOW_AUDIO=b3bb5be7-9a4b-4be1-8b13-c96b3d6a7a10

MXL_RC=$TEST_RC_SUCCESS
MXL_PIDS=()

function mxl_log() { log_log -m "$@" -o "$MXL_LOG"; }

function mxl_fail() {
    log_err -m "$@" -o "$MXL_LOG"
    echo "FAIL: $*" >&2
    MXL_RC=$TEST_RC_FAILURE
}

# Fail the test now.
function mxl_abort() {
    mxl_fail "$@"
    mxl_test_end
}

# mxl_test_begin <test directory>
# Creates <dir>/tmp for this run and a private MXL domain inside it (or MXL_TEST_DOMAIN, which a
# cross-container test points at a shared volume).
function mxl_test_begin() {
    MXL_TEST_DIR="$1"
    MXL_TMP="${MXL_TEST_DIR}/${TEST_DIR_TMP}"

    rm -rf "$MXL_TMP"
    mkdir -p "$MXL_TMP"

    MXL_LOG="${MXL_TMP}/${TEST_FILE_OUT_TEST_LOG}"
    : > "$MXL_LOG"

    MXL_DOMAIN="${MXL_TEST_DOMAIN:-${MXL_TMP}/domain}"
    mkdir -p "$MXL_DOMAIN"

    trap mxl_cleanup EXIT

    # A test runs under `bash -e`, which ends it at the first failing command without a word. Say which one failed.
    trap 'echo "FAIL: a command failed (status $?) at ${BASH_SOURCE[0]##*/}:${LINENO}: ${BASH_COMMAND}" | tee -a "$MXL_LOG" >&2' ERR
}

function mxl_cleanup() {
    for pid in "${MXL_PIDS[@]}"; do
        kill "$pid" > /dev/null 2>&1 || true
        wait "$pid" > /dev/null 2>&1 || true
    done
    MXL_PIDS=()
}

function mxl_test_end() {
    mxl_cleanup

    if [ $MXL_RC -eq $TEST_RC_SUCCESS ] && [ -z "$TEST_DISABLE_CLEANUP" -o "$TEST_DISABLE_CLEANUP" = "0" ]; then
        rm -rf "$MXL_TMP"
    fi

    exit $MXL_RC
}

# Skip (return code 4) when the MXL plugin or tools are not in the image.
function mxl_require() {
    for el in "$@"; do
        if ! gst-inspect-1.0 "$el" > /dev/null 2>&1 && ! command -v "$el" > /dev/null 2>&1; then
            echo "skip: $el not found (image built with ENABLE_MXL=0?)" >&2
            exit $TEST_RC_SKIP
        fi
    done
}

# mxl_check <description> <command...>: runs the command; a non-zero status fails the test.
function mxl_check() {
    local desc="$1"; shift
    if "$@" >> "$MXL_LOG" 2>&1; then
        mxl_log "ok: $desc"
        return 0
    fi
    mxl_fail "$desc ($*)"
    return 1
}

# mxl_bg <command...>: starts a background process, remembered for cleanup. Sets MXL_LAST_PID.
function mxl_bg() {
    "$@" >> "$MXL_LOG" 2>&1 &
    MXL_LAST_PID=$!
    MXL_PIDS+=("$MXL_LAST_PID")
}

# mxl_wait_flow <flow id> [seconds]: waits until the flow is listed in the domain.
function mxl_wait_flow() {
    local id="$1" limit="${2:-10}"
    for ((i = 0; i < limit * 10; i++)); do
        if mxl-info -d "$MXL_DOMAIN" -l 2>/dev/null | grep -qi "$id"; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

# mxl_write_video <flow id> <framerate> <width> <height> <frames> [pattern]
# Real-time v210 writer in the background (MXL_LAST_PID). The default bar pattern is static (smpte is animated), so
# every frame is identical and a reader can be compared against a reference frame.
function mxl_write_video() {
    # A frame rate is a fraction in caps; a bare 25 is not valid caps and the writer would die silently.
    local rate="$2"
    [[ "$rate" == */* ]] || rate="$rate/1"
    mxl_bg gst-launch-1.0 -q videotestsrc is-live=true do-timestamp=true pattern="${6:-bar}" num-buffers="$5" \
        ! videoconvert ! "video/x-raw,format=v210,width=$3,height=$4,framerate=$rate" \
        ! queue ! mxlsink flow-id="$1" domain="$MXL_DOMAIN"
}

# mxl_write_audio <flow id> <channels> <tone channel> <buffers of 10 ms>
# Real-time 48 kHz Float32 writer. A 1 kHz sine at half scale (-6 dBFS) is routed to one channel
# only, so channel order and level can be checked on the reading side.
function mxl_write_audio() {
    local flow="$1" channels="$2" tone="$3" buffers="$4" matrix="" mask
    for ((i = 0; i < channels; i++)); do
        [ $i -eq "$tone" ] && matrix+="<1.0>," || matrix+="<0.0>,"
    done
    matrix="<${matrix%,}>"
    mask=$(printf '0x%x' $(( (1 << channels) - 1 )))

    mxl_bg gst-launch-1.0 -q audiotestsrc is-live=true do-timestamp=true wave=sine freq=1000 volume=0.5 num-buffers="$buffers" samplesperbuffer=480 \
        ! "audio/x-raw,format=F32LE,rate=48000,channels=1" \
        ! audiomixmatrix in-channels=1 out-channels="$channels" matrix="$matrix" channel-mask="$mask" \
        ! "audio/x-raw,format=F32LE,rate=48000,channels=${channels},layout=interleaved,channel-mask=(bitmask)${mask}" \
        ! queue ! mxlsink flow-id="$flow" domain="$MXL_DOMAIN"
}

# mxl_flow_field <flow id> <label>: a value from `mxl-info -f`, e.g. "Grain/sample rate".
function mxl_flow_field() {
    mxl-info -d "$MXL_DOMAIN" -f "$1" 2>/dev/null | awk -F': *' -v k="$2" '{ gsub(/^[ \t]+/, "", $1) } $1 == k { print $2; exit }'
}

# mxl_seconds_since <start from `date +%s.%N`>
function mxl_seconds_since() {
    awk -v a="$(date +%s.%N)" -v b="$1" 'BEGIN { printf "%.2f", a - b }'
}

# mxl_stop <pid> [signal]: stops a background process and always succeeds. `wait` on a process
# that was just killed returns 128+signal, which would abort a test running under `bash -e`.
function mxl_stop() {
    kill "-${2:-TERM}" "$1" > /dev/null 2>&1 || true
    wait "$1" > /dev/null 2>&1 || true
    return 0
}

### Engine helpers, for tests of the input MXL tap (tests/tests/mxl/tap)

# The engine under test. In utpl/transcoder-test it is /opt/utpl/bin/transcoder. To try a build from
# a scratch directory instead, mount it and set MXL_ENGINE_BUILD_DIR (its libraries are found there).
MXL_ENGINE="${MXL_ENGINE:-/opt/utpl/bin/transcoder}"
MXL_LICENSE="${MXL_LICENSE:-/tmp/transcoder.lic}"
MXL_PORT_IN=2222
MXL_PORT_OUT=4444

function mxl_require_engine() {
    if [ -n "$MXL_ENGINE_BUILD_DIR" ]; then
        MXL_ENGINE="$MXL_ENGINE_BUILD_DIR/app/transcoder"
        export LD_LIBRARY_PATH=$(find "$MXL_ENGINE_BUILD_DIR" -name 'lib*.so' -printf '%h\n' | sort -u | tr '\n' ':')
    fi

    for f in "$MXL_ENGINE" "$MXL_LICENSE"; do
        if [ ! -e "$f" ]; then
            echo "skip: $f not found" >&2
            exit $TEST_RC_SKIP
        fi
    done
    mxl_require ffmpeg ffprobe
}

# mxl_write_config <file> <tap json or empty>
# A 720p25 H.264 + AAC channel from udp://127.0.0.1:2222 to udp://127.0.0.1:4444. The tap json is the
# value of "mxlTap", for example '{"domain":"...","video":{"flowId":"..."}}'. Variations, by environment:
#   MXL_INPUT_TRANSPORT  the members of the input's "transport" object, without the braces (default:
#                        the udp uri above), for example '"uri": ["udp://a", "udp://b"]' 
#   MXL_OUT_SIZE         output picture size, WxH (default 1280x720)
#   MXL_OUT_AUDIO        output audio channels (default 2)
#   MXL_OUT_PORT         the udp port the on-air output goes to (default MXL_PORT_OUT)
#   MXL_EXTRA_OUTPUT     one more output object, for example the result of mxl_programme_json
#   MXL_OUT_KEYINT       the video encoder's key-int-max (default 50)
#   MXL_OUT_X264OPTS     x264's option-string, for example scenecut=0 (default: none)
#   MXL_OUT_SCTE         the value of the output's "scte" block, for example '{ "pid": 500, "mode": "passthrough" }' (default: none)
function mxl_write_config() {
    local file="$1" tap="$2" extra="" transport size="${MXL_OUT_SIZE:-1280x720}"
    [ -n "$tap" ] && extra=", \"mxlTap\": $tap"
    transport="${MXL_INPUT_TRANSPORT:-\"uri\": \"udp://127.0.0.1:${MXL_PORT_IN}\"}"
    cat > "$file" <<JSON
{ "version": "1.1.0",
  "application": { "logger": { "transcoder": { "level": "trace", "console": { "enable": true, "colors": false, "stderr": false } } } },
  "transcoder": {
    "inputs": [ { "transport": { ${transport} }${extra} } ],
    "outputs": [ { "transports": [ { "uri": "udp://127.0.0.1:${MXL_OUT_PORT:-$MXL_PORT_OUT}" } ], "container": "mpegts",${MXL_OUT_SCTE:+ \"scte\": ${MXL_OUT_SCTE},}
      "streams": [
        { "type": "audio", "codec": "aac", "audio": { "rate": 48000, "channels": ${MXL_OUT_AUDIO:-2} },
          "aac": { "encoder": "fdkaacenc", "properties": { "bitrate": "128000" }, "caps": {} } },
        { "type": "video", "codec": "h264", "video": { "width": ${size%x*}, "height": ${size#*x} },
          "h264": { "encoder": "x264enc",
                    "properties": { "key-int-max": "${MXL_OUT_KEYINT:-50}", "tune": "zerolatency", "speed-preset": "veryfast", "bitrate": "3000"${MXL_OUT_X264OPTS:+, \"option-string\": \"${MXL_OUT_X264OPTS}\"} },
                    "caps": { "profile": "high" } } } ] }${MXL_EXTRA_OUTPUT:+, ${MXL_EXTRA_OUTPUT}} ] } }
JSON
}

# mxl_programme_json [width] [height] [rate] [audio channels]: an output of type "mxl" with the fixed flow
# ids. Size and rate left out follow the input.
function mxl_programme_json() {
    local video="\"flowId\": \"$MXL_FLOW_VIDEO\""
    [ -n "$1" ] && video+=", \"width\": $1"
    [ -n "$2" ] && video+=", \"height\": $2"
    [ -n "$3" ] && video+=", \"rate\": $3"
    echo "{ \"type\": \"mxl\", \"name\": \"programme\", \"mxl\": { \"domain\": \"$MXL_DOMAIN\", \"video\": { $video }, \"audio\": { \"flowId\": \"$MXL_FLOW_AUDIO\", \"channels\": ${4:-2} } } }"
}

# mxl_tap_json [domain] [audio channels]: the tap block using the fixed flow ids.
function mxl_tap_json() {
    echo "{ \"domain\": \"${1:-$MXL_DOMAIN}\", \"video\": { \"flowId\": \"$MXL_FLOW_VIDEO\" }, \"audio\": { \"flowId\": \"$MXL_FLOW_AUDIO\", \"channels\": ${2:-2} } }"
}

# mxl_input_json [video flow id] [audio flow id] [domain]: the members of an input's "transport" object for an
# MXL input (use as MXL_INPUT_TRANSPORT). An empty flow id leaves that flow out; the default is both flows
# with the fixed ids, in the test's domain. MXL_INPUT_DELAY_MS, when set, adds "delayMs"; MXL_INPUT_DATA_FLOW, when set, adds the
# SCTE-35 data flow with that id.
function mxl_input_json() {
    local video="${1-$MXL_FLOW_VIDEO}" audio="${2-$MXL_FLOW_AUDIO}" domain="${3:-$MXL_DOMAIN}" flows=""
    [ -n "$video" ] && flows="\"video\": { \"flowId\": \"$video\" }"
    [ -n "$audio" ] && flows="${flows:+$flows, }\"audio\": { \"flowId\": \"$audio\" }"
    [ -n "$MXL_INPUT_DATA_FLOW" ] && flows="${flows:+$flows, }\"data\": { \"flowId\": \"${MXL_INPUT_DATA_FLOW}\" }"
    [ -n "$MXL_INPUT_DELAY_MS" ] && flows="${flows:+$flows, }\"delayMs\": ${MXL_INPUT_DELAY_MS}"
    echo "\"uri\": \"mxl://${domain}\", \"mxl\": { $flows }"
}

# mxl_start_source <kind> [size] [rate]: a live MPEG-TS source into the engine's input.
#   tone  testsrc2 picture and a sine (1 kHz)
#   sync  black picture that flashes white for 2 frames at the start of each second, and a 1 kHz beep
#         of 40 ms starting at the same instants, so any offset between the two is added later
#   interlaced  as tone, 1080i25 top field first
#   ac3   as tone, with AC-3 audio. MXL_SRC_AUDIO selects the layout: "stereo" (default) or "5.1",
#         in which the tone is on the centre channel only
#   scte  as tone, with SCTE-35 splice_null sections every second on a data PID of their own
#   scte-sync  as sync, with the splice_insert events of scte (PID 500)
# Variations, by environment: MXL_SRC_PORT (default the engine's input port), MXL_SRC_FREQ (tone, default 1000 Hz).
# The size may be given as WxH, rate as 25, 50 or 60000/1001. Sets MXL_SRC_PID.
function mxl_start_source() {
    local kind="$1" size="${2:-1280x720}" rate="${3:-25}" video audio filt="" opts="" acodec="-c:a aac -ac 2" tail="" freq="${MXL_SRC_FREQ:-1000}"
    local port="${MXL_SRC_PORT:-$MXL_PORT_IN}"
    case "$kind" in
        sync)
            video="color=c=black:s=${size}:r=${rate},geq=lum='if(lt(mod(T\,1)\,0.08)\,235\,16)':cb=128:cr=128"
            audio="aevalsrc='if(lt(mod(t\,1)\,0.04)\,0.5*sin(2*PI*1000*t)\,0)':s=48000:c=stereo"
            ;;
        scte-sync)
            # the flash-and-beep picture of "sync", carrying the same splice_insert events as "scte"
            video="color=c=black:s=${size}:r=${rate},geq=lum='if(lt(mod(T\,1)\,0.08)\,235\,16)':cb=128:cr=128"
            audio="aevalsrc='if(lt(mod(t\,1)\,0.04)\,0.5*sin(2*PI*1000*t)\,0)':s=48000:c=stereo"
            ;;
        interlaced)
            video="testsrc2=size=1920x1080:rate=50"
            audio="sine=frequency=${freq}:sample_rate=48000"
            filt="-vf tinterlace=mode=interleave_top"
            opts="-flags +ildct+ilme -x264opts tff=1"
            ;;
        ac3)
            video="testsrc2=size=${size}:rate=${rate}"
            audio="sine=frequency=${freq}:sample_rate=48000"
            if [ "${MXL_SRC_AUDIO:-stereo}" = "5.1" ]; then
                tail="-af pan=5.1|FC=c0"
                acodec="-c:a ac3 -b:a 384k"
            else
                acodec="-c:a ac3 -ac 2"
            fi
            ;;
        *)
            video="testsrc2=size=${size}:rate=${rate}"
            audio="sine=frequency=${freq}:sample_rate=48000"
            ;;
    esac
    if [ "$kind" = "scte" ] || [ "$kind" = "scte-sync" ]; then
        # ffmpeg cannot write an SCTE-35 PID, so tsp adds one to a file, offline as the other SCTE tests do,
        # and the file is then played out in real time with tsp. A splice_insert every 3 s from 9 s to 42 s on PID 500 (more than 12 events make tsp hang).
        if [ ! -f "$MXL_TMP/scte.ts" ]; then
            # splice_insert events, one every 3 s through the file, so that some come after the engine has
            # joined. splice_null is not used: the engine treats it as a heartbeat, so finding it on the
            # output would prove nothing.
            {
                echo '<?xml version="1.0" encoding="UTF-8"?>'
                echo '<tsduck>'
                for ((k = 0; k < 12; k++)); do
                    echo "  <splice_information_table><splice_insert splice_event_id=\"$((100 + k))\" unique_program_id=\"1\" out_of_network=\"true\" pts_time=\"$(( (9 + 3 * k) * 90000 ))\"/></splice_information_table>"
                done
                echo '</tsduck>'
            } > "$MXL_TMP/splice.xml"
            ffmpeg -v error -y -f lavfi -i "$video" -f lavfi -i "$audio" -t 70 \
                -c:v libx264 -preset veryfast -g 50 -b:v 3M -c:a aac -ac 2 -f mpegts "$MXL_TMP/plain.ts" >> "$MXL_LOG" 2>&1
            tsp --add-input-stuffing 1/10 -I file "$MXL_TMP/plain.ts" \
                -P pmt --service 1 --add-programinfo-id 0x43554549 --add-pid 500/0x86 \
                -P spliceinject --inject-count 1 --service 1 --start-delay 1000 \
                    --files "$MXL_TMP/splice.xml" --min-bitrate 1024 --wait-first-batch \
                -P filter --negate --pid 0x1FFF -O file "$MXL_TMP/scte.ts" >> "$MXL_LOG" 2>&1
        fi
        # Played out packet by packet: ffmpeg would re-wrap the SCTE-35 sections as PES and lose them.
        mxl_bg tsp -I file "$MXL_TMP/scte.ts" -P regulate -O ip --packet-burst 7 "127.0.0.1:${port}"
    else
        mxl_bg ffmpeg -v error -re -f lavfi -i "$video" -f lavfi -i "$audio" $filt $tail \
            -c:v libx264 -preset veryfast -g 50 -b:v 3M $opts $acodec -f mpegts "udp://127.0.0.1:${port}?pkt_size=1316"
    fi
    MXL_SRC_PID=$MXL_LAST_PID
}

# mxl_start_engine <config> [tag]: starts the engine in the background (log: $MXL_TMP/engine.log, or engine-<tag>.log
# for a second engine in the same test). Sets MXL_ENGINE_PID.
function mxl_start_engine() {
    local tag="${2:+-$2}"
    mkdir -p "$MXL_TMP/db"; : > "$MXL_TMP/db/e${tag}.sqlite"
    "$MXL_ENGINE" -c "$1" -l "$MXL_LICENSE" -b "$MXL_TMP/db/e${tag}.sqlite" > "$MXL_TMP/engine${tag}.log" 2>&1 &
    MXL_ENGINE_PID=$!
    MXL_PIDS+=("$MXL_ENGINE_PID")
}

# mxl_capture_output <seconds> <file>: the engine's output, copied for a while (foreground).
function mxl_capture_output() {
    # The timeout is for an engine that sends nothing: ffmpeg would wait for the first packet for ever.
    timeout $(( $1 + 15 )) ffmpeg -v error -y -i "udp://127.0.0.1:${MXL_PORT_OUT}?fifo_size=5000000&overrun_nonfatal=1" -t "$1" -c copy "$2" >> "$MXL_LOG" 2>&1
}

# mxl_max_gap_ms <file> <stream selector, v:0 or a:0>: the largest spacing between consecutive packets in ms.
function mxl_max_gap_ms() {
    ffprobe -v error -select_streams "$2" -show_entries packet=pts_time -of csv=p=0 "$1" | grep -v '^$' | sort -n \
        | awk 'NR > 1 { d = ($1 - p) * 1000; if (d > m) m = d } { p = $1 } END { printf "%.0f", m }'
}

# mxl_decode_errors <file>: number of decoder error lines for the whole file.
function mxl_decode_errors() {
    ffmpeg -v error -i "$1" -f null - 2>&1 | wc -l
}

# mxl_alive <pid>
function mxl_alive() { kill -0 "$1" 2> /dev/null; }

# mxl_cpu_seconds <pid>: user + system CPU time so far.
function mxl_cpu_seconds() {
    awk '{ printf "%.2f", ($14 + $15) / 100 }' "/proc/$1/stat"
}

### Measurements, shared by the tap tests and the soak run

# mxl_lipsync_pairs "<flash onsets>" "<beep onsets>": median flash-minus-beep in ms and the number of
# pairs ("none 0" when nothing pairs up). Onsets are in seconds.
function mxl_lipsync_pairs() {
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

# mxl_lipsync_file <media file>: "<median ms> <pairs>" for a file made from the `sync` source.
function mxl_lipsync_file() {
    local f="$1" flash beep astart
    flash=$(ffprobe -v error -f lavfi -i "movie=$f,signalstats" -show_entries frame=pts_time:frame_tags=lavfi.signalstats.YAVG -of csv=p=0 \
        | awk -F, '{ y = $2 + 0; if (y > 120 && prev <= 120) printf "%s ", $1; prev = y }')
    # silencedetect times start at 0 for the audio stream, ffprobe picture times include the stream's
    # start offset (ffmpeg's mux delay, about 1.4 s), so the audio start time is added back.
    astart=$(ffprobe -v error -select_streams a:0 -show_entries stream=start_time -of default=nw=1:nk=1 "$f" | head -1)
    beep=$(ffmpeg -v info -i "$f" -map 0:a -af silencedetect=noise=-35dB:d=0.3 -f null - 2>&1 | grep -o 'silence_end: [0-9.]*' \
        | awk -v s="$astart" '{ printf "%.4f ", $2 + s }')
    mxl_lipsync_pairs "$flash" "$beep"
}

# mxl_lipsync_flows <seconds> <log file>: reads both flows in one pipeline, so that they share a
# timeline, and writes "flash onsets" and "beep onsets" (two lines, seconds) to <log file>.onsets.
function mxl_lipsync_flows() {
    local seconds="$1" log="$2"
    timeout "$seconds" gst-launch-1.0 -m \
        mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN ! videoconvert ! videoanalyse message=true ! fakesink sync=false \
        mxlsrc audio-flow-id=$MXL_FLOW_AUDIO domain=$MXL_DOMAIN ! audioconvert ! level interval=10000000 post-messages=true ! fakesink sync=false \
        > "$log" 2>&1 || true
    python3 -I - "$log" > "$log.onsets" <<'PY'
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

# mxl_head_lag_ms <flow id>: how far the newest written position of a flow is behind the wall clock, in
# ms (negative: ahead). The head index counts grains (video frames or audio samples) from the TAI
# epoch, so this is the flow's own timeline against the system clock. Constant means no drift.
function mxl_head_lag_ms() {
    local id="$1" rate head now
    rate=$(mxl_flow_field "$id" 'Grain/sample rate')
    head=$(mxl_flow_field "$id" 'Head index')
    now=$(date +%s.%N)
    awk -v r="$rate" -v h="$head" -v n="$now" 'BEGIN { split(r, f, "/"); printf "%.1f", (n - h * f[2] / f[1]) * 1000 }'
}

# mxl_read_video <seconds> <frames> <file.y4m>: the video flow as I420 frames.
function mxl_read_video() {
    timeout "$1" gst-launch-1.0 -q -e mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN num-buffers=$2 \
        ! videoconvert ! "video/x-raw,format=I420" ! y4menc ! filesink location="$3" >> $MXL_LOG 2>&1
}

# mxl_read_audio <seconds> <file.wav>: the audio flow (mxlsrc audio buffers are 1 ms each).
function mxl_read_audio() {
    timeout $(( $1 + 15 )) gst-launch-1.0 -q -e mxlsrc audio-flow-id=$MXL_FLOW_AUDIO domain=$MXL_DOMAIN num-buffers=$(( $1 * 1000 )) \
        ! audioconvert ! wavenc ! filesink location="$2" >> $MXL_LOG 2>&1
}

# mxl_tone_db <wav> <frequency> [channel]: peak level in dB of a narrow band around the frequency
# (one channel when given, counting from 0).
function mxl_tone_db() {
    local pre=""
    [ -n "$3" ] && pre="pan=mono|c0=c$3,"
    ffmpeg -v info -i "$1" -af "${pre}bandpass=f=$2:width_type=h:w=200,volumedetect" -f null - 2>&1 | awk '/max_volume/ { print $(NF-1) }'
}

# mxl_distinct_frames <y4m>: how many different pictures the file holds.
function mxl_distinct_frames() {
    ffmpeg -v error -i "$1" -f framemd5 - 2>/dev/null | awk '!/^#/ { print $NF }' | sort -u | wc -l
}
