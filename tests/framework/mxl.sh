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
    mxl_bg gst-launch-1.0 -q videotestsrc is-live=true do-timestamp=true pattern="${6:-bar}" num-buffers="$5" \
        ! videoconvert ! "video/x-raw,format=v210,width=$3,height=$4,framerate=$2" \
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

# mxl_write_config <file> <tap json or empty> [domain]
# A 720p25 H.264 + AAC channel from udp://127.0.0.1:2222 to udp://127.0.0.1:4444. The tap json is the
# value of "mxlTap", for example '{"domain":"...","video":{"flowId":"..."}}'.
function mxl_write_config() {
    local file="$1" tap="$2" extra=""
    [ -n "$tap" ] && extra=", \"mxlTap\": $tap"
    cat > "$file" <<JSON
{ "version": "1.1.0",
  "application": { "logger": { "transcoder": { "level": "trace", "console": { "enable": true, "colors": false, "stderr": false } } } },
  "transcoder": {
    "inputs": [ { "transport": { "uri": "udp://127.0.0.1:${MXL_PORT_IN}" } ${extra} } ],
    "outputs": [ { "transports": [ { "uri": "udp://127.0.0.1:${MXL_PORT_OUT}" } ], "container": "mpegts",
      "streams": [
        { "type": "audio", "codec": "aac", "audio": { "rate": 48000, "channels": 2 },
          "aac": { "encoder": "fdkaacenc", "properties": { "bitrate": "128000" }, "caps": {} } },
        { "type": "video", "codec": "h264", "video": { "width": 1280, "height": 720 },
          "h264": { "encoder": "x264enc",
                    "properties": { "key-int-max": "50", "tune": "zerolatency", "speed-preset": "veryfast", "bitrate": "3000" },
                    "caps": { "profile": "high" } } } ] } ] } }
JSON
}

# mxl_tap_json [domain] [audio channels]: the tap block using the fixed flow ids.
function mxl_tap_json() {
    echo "{ \"domain\": \"${1:-$MXL_DOMAIN}\", \"video\": { \"flowId\": \"$MXL_FLOW_VIDEO\" }, \"audio\": { \"flowId\": \"$MXL_FLOW_AUDIO\", \"channels\": ${2:-2} } }"
}

# mxl_start_source <kind> [size] [rate]: a live MPEG-TS source into the engine's input.
#   tone  testsrc2 picture and a 1 kHz sine
#   sync  black picture that flashes white for 2 frames at the start of each second, and a 1 kHz beep
#         of 40 ms starting at the same instants, so any offset between the two is added later
#   interlaced  as tone, 1080i25 top field first
function mxl_start_source() {
    local kind="$1" size="${2:-1280x720}" rate="${3:-25}" video audio filt="" opts=""
    case "$kind" in
        sync)
            video="color=c=black:s=${size}:r=${rate},geq=lum='if(lt(mod(T\,1)\,0.08)\,235\,16)':cb=128:cr=128"
            audio="aevalsrc='if(lt(mod(t\,1)\,0.04)\,0.5*sin(2*PI*1000*t)\,0)':s=48000:c=stereo"
            ;;
        interlaced)
            video="testsrc2=size=1920x1080:rate=50"
            audio="sine=frequency=1000:sample_rate=48000"
            filt="-vf tinterlace=mode=interleave_top"
            opts="-flags +ildct+ilme -x264opts tff=1"
            ;;
        *)
            video="testsrc2=size=${size}:rate=${rate}"
            audio="sine=frequency=1000:sample_rate=48000"
            ;;
    esac
    mxl_bg ffmpeg -v error -re -f lavfi -i "$video" -f lavfi -i "$audio" $filt \
        -c:v libx264 -preset veryfast -g 50 -b:v 3M $opts -c:a aac -ac 2 -f mpegts "udp://127.0.0.1:${MXL_PORT_IN}?pkt_size=1316"
    MXL_SRC_PID=$MXL_LAST_PID
}

# mxl_start_engine <config>: starts the engine in the background (log: $MXL_TMP/engine.log).
function mxl_start_engine() {
    mkdir -p "$MXL_TMP/db"; : > "$MXL_TMP/db/e.sqlite"
    "$MXL_ENGINE" -c "$1" -l "$MXL_LICENSE" -b "$MXL_TMP/db/e.sqlite" > "$MXL_TMP/engine.log" 2>&1 &
    MXL_ENGINE_PID=$!
    MXL_PIDS+=("$MXL_ENGINE_PID")
}

# mxl_capture_output <seconds> <file>: the engine's output, copied for a while (foreground).
function mxl_capture_output() {
    ffmpeg -v error -y -i "udp://127.0.0.1:${MXL_PORT_OUT}?fifo_size=5000000&overrun_nonfatal=1" -t "$1" -c copy "$2" >> "$MXL_LOG" 2>&1
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
