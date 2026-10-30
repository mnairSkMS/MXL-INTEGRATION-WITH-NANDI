TEST_FRAMEWORK_SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

. ${TEST_FRAMEWORK_SCRIPT_DIR}/config.sh
. ${TEST_FRAMEWORK_SCRIPT_DIR}/log.sh

. ${TEST_FRAMEWORK_SCRIPT_DIR}/utils.sh
. ${TEST_FRAMEWORK_SCRIPT_DIR}/utils/uri.sh
. ${TEST_FRAMEWORK_SCRIPT_DIR}/utils/config.sh
. ${TEST_FRAMEWORK_SCRIPT_DIR}/utils/json.sh
. ${TEST_FRAMEWORK_SCRIPT_DIR}/utils/assert.sh
. ${TEST_FRAMEWORK_SCRIPT_DIR}/utils/media.sh

. ${TEST_FRAMEWORK_SCRIPT_DIR}/test/utils.sh
. ${TEST_FRAMEWORK_SCRIPT_DIR}/test/config.sh
. ${TEST_FRAMEWORK_SCRIPT_DIR}/test/proc.sh
. ${TEST_FRAMEWORK_SCRIPT_DIR}/test/assert.sh
. ${TEST_FRAMEWORK_SCRIPT_DIR}/test/media.sh


TEST_PATH_DIR_MEDIA="${TEST_FRAMEWORK_SCRIPT_DIR}/../${TEST_DIR_MEDIA}"
TEST_PATH_DIR_TOOLS="${TEST_FRAMEWORK_SCRIPT_DIR}/../${TEST_DIR_TOOLS}"
TEST_PATH_DIR_SCHEMA="${TEST_FRAMEWORK_SCRIPT_DIR}/${TEST_DIR_SCHEMA}"
TEST_PATH_DIR_POIS="${TEST_FRAMEWORK_SCRIPT_DIR}/../pois"
TEST_PATH_DIR_POIS_SERVER="${TEST_PATH_DIR_POIS}/server"

TEST_RC=$TEST_RC_SUCCESS

### Log wrappers

FORCE_STD_OUT_OPTION=

function test_trace() {
    log_trace -m "$@" -o "$TEST_PATH_OUT_TEST_LOG" $FORCE_STD_OUT_OPTION
}

function test_dbg() {
    log_dbg -m "$@" -o "$TEST_PATH_OUT_TEST_LOG" $FORCE_STD_OUT_OPTION
}

function test_log() {
    log_log -m "$@" -o "$TEST_PATH_OUT_TEST_LOG" $FORCE_STD_OUT_OPTION
}

function test_warn() {
    log_warn -m "$@" -o "$TEST_PATH_OUT_TEST_LOG" $FORCE_STD_OUT_OPTION
}

function test_err() {
    log_err -m "$@" -o "$TEST_PATH_OUT_TEST_LOG" $FORCE_STD_OUT_OPTION
}

function test_fatal() {
    log_err -m "$@" -o "$TEST_PATH_OUT_TEST_LOG" $FORCE_STD_OUT_OPTION
    test_fail
}

### Test API

function test_cleanup_disabled() {
    if [[ ($TEST_RC -eq $TEST_RC_SUCCESS) && ((-z $TEST_DISABLE_CLEANUP) || $TEST_DISABLE_CLEANUP -eq 0)  ]]; then
        return 1
    fi

    return 0
}

function test_cleanup() {
    local stopSrc=0
    local stopCap=0
    local stopTranscoder=0
    local stopHttp=0
    local stopPois=0

    local OPTIND
    while getopts ":asctph" opt; do
        case "$opt" in
            a)
                stopSrc=1
                stopCap=1
                stopTranscoder=1
                stopHttp=1
            ;;
            s)
                stopSrc=1
            ;;
            c)
                stopCap=1
            ;;
            t)
                stopTranscoder=1
            ;;
            p)
                stopPois=1
            ;;
            h)
                stopHttp=1
            ;;
        esac
    done 
    shift $((OPTIND-1))

    if [ $stopSrc -eq 1 ]; then
        for idx in "${!TEST_PID_TSPLAY[@]}"; do
            if [ ! -z "${TEST_PID_TSPLAY[$idx]}" ]; then
                test_log "Stopping tsplay $idx (pid: ${TEST_PID_TSPLAY[$idx]})"
                test_proc_kill_and_wait -p "${TEST_PID_TSPLAY[$idx]}"
                unset 'TEST_PID_TSPLAY[$idx]'
            fi
        done

        for idx in "${!TEST_PID_SRT_SRC[@]}"; do
            if [ ! -z "${TEST_PID_SRT_SRC[$idx]}" ]; then
                test_log "Stopping SRT source $idx (pid: ${TEST_PID_SRT_SRC[$idx]})"
                test_proc_kill_and_wait -p "${TEST_PID_SRT_SRC[$idx]}"
                unset 'TEST_PID_SRT_SRC[$idx]'
            fi
        done

        for idx in "${!TEST_PID_HLS_HTTPD[@]}"; do
            if [ ! -z "${TEST_PID_HLS_HTTPD[$idx]}" ]; then
                test_log "Stopping HLS HTTPD $idx (pid: ${TEST_PID_HLS_HTTPD[$idx]})"
                test_proc_kill_and_wait -p "${TEST_PID_HLS_HTTPD[$idx]}"
                unset 'TEST_PID_HLS_HTTPD[$idx]'
            fi
        done
    fi

    if [ $stopCap -eq 1 ]; then
        for outIdx in "${!TEST_PID_CAPTURES[@]}"; do
            test_log "Stopping capture (pid: ${TEST_PID_CAPTURES["$outIdx"]})"
            test_proc_kill_and_wait -p "${TEST_PID_CAPTURES["$outIdx"]}"
        done

        declare -Ag TEST_PID_CAPTURES
    fi

    if [ $stopTranscoder -eq 1 ]; then
        if [ ! -z "${TEST_PID_TRANSCODER}" ]; then
            test_log "Stopping transcoder (pid: ${TEST_PID_TRANSCODER})"
            test_proc_kill_and_wait -p ${TEST_PID_TRANSCODER} -s SIGINT
            TEST_PID_TRANSCODER=
        fi
    fi

    if [ $stopPois -eq 1 ]; then
        if [ ! -z "${TEST_PID_POIS}" ]; then
            test_log "Stopping POIS server (pid: ${TEST_PID_POIS})"
            test_proc_kill_and_wait -p ${TEST_PID_POIS}
            TEST_PID_POIS=
        fi            
    fi

    if [ $stopHttp -eq 1 ]; then
        if [ ! -z "${TEST_PID_HTTPD}" ]; then
            test_log "Stopping HTTPD server (pid: ${TEST_PID_HTTPD})"
            test_proc_kill_and_wait -p ${TEST_PID_HTTPD}
            TEST_PID_HTTPD=
        fi
    fi
}

function test_prepare_src() {
    local type="bunny"
    local duration=${TEST_DEFAULT_DURATION_SECONDS}
    local audioOpts=${TEST_DEFAULT_FFMPEG_CODEC_PARAMS_AUDIO}
    local videoOpts=${TEST_DEFAULT_FFMPEG_CODEC_PARAMS_VIDEO}
    local inputFile=
    local packager=
    local overlay=0

    local OPTIND
    while getopts ":i:t:d:a:v:p:O:" opt; do
        case "$opt" in
            i)
                inputFile="$OPTARG"
            ;;
            t)
                type="$OPTARG"
            ;;
            d)
                duration="$OPTARG"
            ;;
            a)
                audioOpts="$OPTARG"
            ;;
            v)
                videoOpts="$OPTARG"
            ;;
            p)
                packager="$OPTARG"
            ;;
            O)
                overlay="$OPTARG"
            ;;
        esac
    done 
    shift $((OPTIND-1))

    test_dbg "Preparing source file of type \"${type}\", length \"${duration} seconds\""

    test_utils_prepare_src_file -t $type -i "$inputFile" -d $duration -o "${TEST_PATH_SRC_TS}" -s "${TEST_PATH_SRC_SUBTITLES}" -e "${TEST_PATH_SRC_SCTE}" -a "${audioOpts}" -v "${videoOpts}" -p "${packager}"  -O "${overlay}"
}

function test_cleanup_src() {
    if ! test_cleanup_disabled; then
        rm -f ${TEST_PATH_SRC_TS}
    fi

    local srcUris=$(test_config_input_uri 0)

    for srcUri in "${srcUris[@]}"; do
        if utils_uri_proto_is_file $srcUri; then
            local defaultEntryPoint=${DEFAULT_ENTRY_POINT_MAP["prepare-src"]}

            if [ "$defaultEntryPoint" = "_default_prepare_src" ]; then
                local path=$(utils_uri_get_path $srcUri)
                rm -f "$path"
            fi
        fi
    done
}

function __handle_src_startStop() {
    local listener=$1; shift
    local startOperation=$1; shift
    local srcUris=($(test_config_input_uri 0))

    for uriIdx in "${!srcUris[@]}"; do
        local srcUri=${srcUris[$uriIdx]}
        local proceed=0

        if utils_uri_is_listener_mode $srcUri; then
            if [ $listener -eq 0 ]; then
                proceed=1
            else
                test_log "Requested start in listener mode, but src $uriIdx is listener mode "
            fi
        else
            if [ $listener -eq 1 ]; then
                proceed=1
            else
                test_log "Requested start in caller mode, but src $uriIdx is caller mode "

                if [ $startOperation -eq 1 ]; then
                    if utils_uri_proto_is_srt $srcUri; then
                        local host=$(utils_uri_get_host $srcUri)
                        local port=$(utils_uri_get_port $srcUri)
                        local udpPort=$(( $port + 133 )) # FIXME:
                    
                        test_log "Starting delayed UDP stream for SRT source $uriIdx"

                        sleep 2
                        TEST_PID_TSPLAY["$uriIdx"]=$(test_proc_call_async "tsplay ${TEST_PATH_SRC_TS} $host:$udpPort")
                    fi
                fi
            fi
        fi

        if [ $proceed -eq 1 ]; then
            if [ $startOperation -eq 1 ]; then
                if utils_uri_proto_is_udp $srcUri; then
                    local host=$(utils_uri_get_host $srcUri)
                    local port=$(utils_uri_get_port $srcUri)

                    test_log "Starting UDP source $uriIdx to host '$host' and port '$port'"

                    TEST_PID_TSPLAY["$uriIdx"]=$(test_proc_call_async "tsplay ${TEST_PATH_SRC_TS} $host:$port")

                elif utils_uri_proto_is_srt $srcUri; then
                    local host=$(utils_uri_get_host $srcUri)
                    local port=$(utils_uri_get_port $srcUri)
                    local udpPort=$(( $port + 133 )) # FIXME:

                    local srtUri="srt://$host:$port"
                    if [ $listener -eq 1 ]; then
                        srtUri="$srtUri?mode=listener"
                        
                    else
                        srtUri="$srtUri?mode=caller"
                    fi

                    srtUri="${srtUri}&transtype=live"

                    test_log "Starting SRT source $uriIdx to host '$host' and port '$port'"

                    TEST_PID_SRT_SRC["$uriIdx"]=$(test_proc_call_async "srt-live-transmit udp://$host:$udpPort \"$srtUri\"")

                    # Wait for UDP port
                    utils_udp_port_wait $udpPort 5

                    if [ $listener -eq 0 ]; then
                        TEST_PID_TSPLAY["$uriIdx"]=$(test_proc_call_async "tsplay ${TEST_PATH_SRC_TS} $host:$udpPort")
                    fi

                elif utils_uri_proto_is_file $srcUri; then
                    local dstPath=$(utils_uri_get_path "$srcUri")

                    test_proc_call cp "${TEST_PATH_SRC_TS}" "$dstPath"

                elif utils_uri_proto_is_http $srcUri; then
                    local host=$(utils_uri_get_host $srcUri)
                    local port=$(utils_uri_get_port $srcUri)

                    test_log "Starting HTTP source $uriIdx to host '$host' and port '$port'"

                    TEST_PID_HLS_HTTPD["$uriIdx"]=$(test_proc_call_async /opt/venv/bin/python3 -m http.server -d ${TEST_PATH_DIR_TMP} ${port})

                    utils_http_file_wait "http://$host:$port/playlist.m3u8" 5
                fi
            fi
        fi
    done

    if [ $startOperation -eq 0 ]; then
        test_cleanup -s
    fi
}

function test_start_src() {
    __handle_src_startStop $1 1
}

function test_stop_src() {
    __handle_src_startStop $1 0
}

function __handle_capture_startStop() {
    local listener=$1; shift
    local startOperation=$1; shift

    for outIdx in $(seq 0 $(( $(test_config_output_count) - 1 ))); do

        for transportIdx in $(seq 0 $(( $(test_config_output_transport_count $outIdx) - 1 ))); do
            local uri=$(test_config_output_transport_uri $outIdx $transportIdx)
            local start=0

            if utils_uri_is_listener_mode $uri; then
                if [ $listener -eq 1 ]; then
                    start=1
                fi
            else
                if [ $listener -eq 0 ]; then
                    start=1
                fi
            fi

            if [ $start -eq 1 ]; then
                local outputFile="${TEST_PATH_OUT_TS}.$outIdx.$transportIdx"

                if utils_uri_proto_is_srt $uri; then
                    if [ $startOperation -eq 1 ]; then
                        local port=$(utils_uri_get_port $uri)
                        local host=$(utils_uri_get_host $uri)
                        local srtUri="srt://$host:$port"

                        test_log "Starting SRT capture from $uri ('$host', '$port')"

                        if [ $listener -eq 1 ]; then
                            srtUri="$srtUri?mode=caller"
                        else
                            srtUri="$srtUri?mode=listener"
                        fi

                        srtUri="${srtUri}&transtype=live"

                        TEST_PID_CAPTURES["$outIdx"]=$(test_proc_call_async "srt-live-transmit \"$srtUri\" file://con > ${outputFile}")

                    else
                        test_log "Stopping SRT capture from $uri"
                    fi

                elif utils_uri_proto_is_udp $uri; then
                    if [ $startOperation -eq 1 ]; then
                        local port=$(utils_uri_get_port $uri)

                        test_log "Starting UDP capture from '$uri' to '$outputFile'"

                        TEST_PID_CAPTURES["$outIdx"]=$(test_proc_call_async "ncat --recv-only -u -l -o ${outputFile} ${port} > /dev/null")

                    else
                        test_log "Stopping UDP capture from $uri"
                    fi

                elif utils_uri_proto_is_file $uri; then
                    if [ $startOperation -eq 1 ]; then
                        test_log "Output is a file - no need to start capturing"
                    else
                        test_log "Output is a file - moving the result '$(utils_uri_get_path $uri)' -> ${outputFile}"

                        test_proc_call mv "$(utils_uri_get_path $uri)" ${outputFile}
                    fi

                else
                    test_err "Not supported output protocol! ($(utils_uri_get_proto $uri))"
                fi

                if [ $startOperation -eq 0 ]; then
                    if [[ -n "${TEST_PID_CAPTURES["$outIdx"]}" ]]; then
                        test_proc_kill_and_wait -p "${TEST_PID_CAPTURES["$outIdx"]}"

                        unset 'TEST_PID_CAPTURES[$outIdx]'
                    fi
                fi
            fi
        done
    done
}

function test_start_capture() {
    __handle_capture_startStop $1 1
}

function test_stop_capture() {
    __handle_capture_startStop $1 0
}

### Default entry points
function _default_initialize() {
    mkdir -p $TEST_PATH_DIR_TMP
}

function _default_prepare_src() {
    test_prepare_src -d ${TEST_DURATION}
}

function _default_prepare_db() {
    local file=${TEST_PATH_SRC_EVENTS}

    test_log "Initializing transcoder database"

    test_proc_call "sqlite3 $TEST_PATH_SRC_DB < ${TEST_PATH_SCHEMA_DB}"

    if [ -e ${file} ]; then
        local json=$(cat $file)
        local eventsCount=$(utils_json_array_length "${json}" '.')

        test_log "Preloading transcoder database using ${file} file"

        for i in $(seq 0 $(( $eventsCount - 1)) ); do
            local timeshift=$(utils_json_value_get "${json}" ".[$i].timeOffsetMs")
            local eventTime=$(date -u '+%Y-%m-%d %H:%M:%S.%N' -d "+$(( $timeshift / 1000))seconds")Z
            local creationTime=$(date -u '+%Y-%m-%d %H:%M:%S.%N' -d "-30seconds")Z
            local eventDetails=$(utils_json_value_get "${json}" ".[$i].details")
            local eventType=$(utils_json_key_get "${eventDetails}" "." 0)

            test_dbg "Inserting event of type '$eventType' that should fire at '$eventTime' (timeshift: ${timeshift}ms)"

            test_proc_call "sqlite3 $TEST_PATH_SRC_DB <<EOF
                INSERT INTO events(type, time, details, creation_time, read_time) VALUES('${eventType}','${eventTime}','$eventDetails','$creationTime', :null);
EOF"
        done
    fi
}

function _default_analyze_src() {
    utils_media_meta_save "${TEST_PATH_SRC_TS}" "${TEST_PATH_SRC_TS_META}"
}

function _default_start_src_caller() {
    test_start_src 0
}

function _default_start_cap_listener() {
    test_start_capture 1
}

function _default_start_httpd() {
    if [ -e ${TEST_PATH_DIR_HTDOCS} ]; then
        local port=${TEST_DEFAULT_PORT_HTTPD}

        test_log "Starting HTTPD server on port $port"
        
        TEST_PID_HTTPD=$(test_proc_call_async /opt/venv/bin/python3 -m http.server -d ${TEST_PATH_DIR_HTDOCS} ${port})
    fi
}

function _default_start_pois() {
    if [ -e ${TEST_PATH_SRC_POIS} ]; then
        local port=${TEST_DEFAULT_PORT_POIS}

        test_log "Starting POIS server on port $port"

        TEST_PID_POIS=$(test_proc_call_async /opt/venv/bin/python3 ${TEST_PATH_DIR_POIS_SERVER}/app.py --port ${port} --custom_processor "${TEST_PATH_SRC_POIS}")
    fi
}

function _default_start_transcoder() {
    local srcUris=($(test_config_input_uri 0))

    test_log "Starting transcoder"

    TEST_PID_TRANSCODER=$(test_proc_call_async "GST_DEBUG_DUMP_DOT_DIR=$TEST_PATH_DIR_TMP transcoder -c ${TEST_PATH_SRC_CONFIG} -b ${TEST_PATH_SRC_DB} -l ${TEST_PATH_SRC_LICENSE}")

    test_log "Transcoder started, PID $TEST_PID_TRANSCODER"

    for srcUri in "${srcUris[@]}"; do
        if utils_uri_is_listener_mode $srcUri; then
            local port=$(utils_uri_get_port $srcUri)

            utils_udp_port_wait $port 5

            test_log "Transcoder input is listening for data"
        fi
    done
}

function _default_start_cap_caller() {
    test_start_capture 0
}

function _default_start_src_listener() {
    test_start_src 1
}

function _default_idle() {
    sleep ${TEST_DURATION}
}

function _default_stop_src_listener() {
    test_stop_src 1
}

function _default_stop_cap_caller() {
    test_stop_capture 0
}

function _default_stop_transcoder() {
    test_cleanup -t
}

function _default_stop_pois() {
    test_cleanup -p
}

function _default_stop_httpd() {
    test_cleanup -h
}

function _default_stop_cap_listener() {
    test_stop_capture 1
}

function _default_stop_src_caller() {
    test_stop_src 0
}

function _default_analyze_out() {
    for outIdx in $(seq 0 $(( $(test_config_output_count) - 1 ))); do
        local pid=$(test_config_output_scte_pid $outIdx)

        for trsIdx in $(seq 0 $(( $(test_config_output_transport_count $outIdx) - 1 ))); do
            local f="$(test_media_output_get_path $outIdx $trsIdx)"

            if [ -e "$f" ]; then
                local metaPath=$(test_media_output_get_meta_path $outIdx $trsIdx)

                utils_media_meta_save "${f}" "$metaPath"
                utils_media_scte_save "${f}" "$(test_media_output_get_scte_path $outIdx $trsIdx)" "$pid"

                for i in $(seq 0 $(( $(utils_media_get_audio_streams_count "$metaPath") - 1)) ); do
                    test_proc_call ffmpeg -y -i  "${f}" -map 0:a:$i -acodec pcm_s16le -f wav $(test_media_output_get_audio_raw_path $outIdx $trsIdx $i)
                done
            fi
        done
    done
}

function _default_verify() {
    for outIdx in $(seq 0 $(( $(test_config_output_count) - 1 ))); do
        for trsIdx in $(seq 0 $(( $(test_config_output_transport_count $outIdx) - 1 ))); do
            local outMetaPath=$(test_media_output_get_meta_path $outIdx $trsIdx)
            local outSctePath=$(test_media_output_get_scte_path $outIdx $trsIdx)
            local inMetaPath=${TEST_PATH_SRC_TS_META}
            
            local length=$(utils_media_get_duration ${outMetaPath})

            local expected=
            local actual=

            test_log ""
            test_log "### Verifying transport $trsIdx on output $outIdx"
            
            expected=$(utils_media_get_audio_streams_count $outMetaPath)
            actual=$(test_config_output_stream_audio_count $outIdx)
            utils_assert_equals "$actual" "$expected" "Audio streams count"

            for streamIdx in $(seq 0 $(( $(test_config_output_stream_audio_count $outIdx) - 1 ))); do
                # Codec
                local expected=$(test_config_output_stream_audio_codec $outIdx $streamIdx)

                if [ "$expected" = "passthrough" ]; then
                    expected=$(utils_media_get_audio_stream_codec_at $inMetaPath 0)
                fi

                actual=$(utils_media_get_audio_stream_codec_at $outMetaPath $streamIdx)
                utils_assert_equals "$actual" "$expected" " [$streamIdx] codec"

                # Sample rate
                expected=$(test_config_output_stream_audio_rate $outIdx $streamIdx)
                if [ "x$expected" = "x" ]; then
                    expected=$(utils_media_get_audio_stream_rate $inMetaPath 0)
                fi

                actual=$(utils_media_get_audio_stream_rate $outMetaPath $streamIdx)
                utils_assert_equals "$actual" "$expected" " [$streamIdx] rate"

                # Channels
                expected=$(test_config_output_stream_audio_channels $outIdx $streamIdx)
                if [ "x$expected" = "x" ]; then
                    expected=$(utils_media_get_audio_stream_channels $inMetaPath 0)
                fi

                actual=$(utils_media_get_audio_stream_channels $outMetaPath $streamIdx)

                if [ $expected -gt 6 ]; then
                    test_warn "Verification of Atmos content is not yet supported!"
                else
                    utils_assert_equals "$actual" "$expected" " [$streamIdx] channels"
                fi
            done


            expected=$(utils_media_get_video_streams_count $outMetaPath)
            actual=$(test_config_output_stream_video_count $outIdx)
            utils_assert_equals "$actual" "$expected" "Video streams count"

            for streamIdx in $(seq 0 $(( $(test_config_output_stream_video_count $outIdx) - 1 ))); do
                local expected=$(test_config_output_stream_video_codec $outIdx $streamIdx)

                if [ "$expected" = "passthrough" ]; then
                    expected=$(utils_media_get_video_stream_codec_at $inMetaPath 0)
                fi

                actual=$(utils_media_get_video_stream_codec_at $outMetaPath $streamIdx)
                utils_assert_equals "$actual" "$expected" " [$streamIdx] codec"

                # width
                expected=$(test_config_output_stream_video_width $outIdx $streamIdx)
                if [ "x$expected" = "x" ]; then
                    expected=$(utils_media_get_video_stream_width $inMetaPath 0)
                fi

                actual=$(utils_media_get_video_stream_width $outMetaPath $streamIdx)
                utils_assert_equals "$actual" "$expected" " [$streamIdx] width"

                # height    
                expected=$(test_config_output_stream_video_height $outIdx $streamIdx)
                if [ "x$expected" = "x" ]; then
                    expected=$(utils_media_get_video_stream_height $inMetaPath 0)
                fi

                actual=$(utils_media_get_video_stream_height $outMetaPath $streamIdx)
                utils_assert_equals "$actual" "$expected" " [$streamIdx] height"

                # rate    
                expected=$(test_config_output_stream_video_rate $outIdx $streamIdx)
                if [ "x$expected" = "x" ]; then
                    expected=$(utils_media_get_video_stream_rate $inMetaPath 0)
                fi

                actual=$(utils_media_get_video_stream_rate $outMetaPath $streamIdx)
                utils_assert_equals "$actual" "$expected" " [$streamIdx] rate"
            done

            expected=$(utils_media_get_subtitle_streams_count $outMetaPath)
            actual=$(utils_media_get_subtitle_streams_count $inMetaPath)
            utils_assert_equals "$actual" "$expected" "Subtitle streams count"


            expected=$(test_config_output_scte_enabled $outIdx)
            if [ $expected -eq 1 ]; then
                local pid=$(test_config_output_scte_pid $outIdx)

                if utils_uri_proto_is_hls $(test_config_output_transport_uri $outIdx $trsIdx); then
                    test_log "FIXME: Skipping SCTE PID existence check for HLS output on output $outputIdx"
                else
                    actual=$(utils_media_get_scte_stream_exist $outMetaPath $pid)

                    utils_assert_equals "$actual" "$expected" "SCTE PID existence on output $outputIdx"
                fi

                local nullInterval=$(test_config_output_scte_nullInterval $outIdx)
                if [ $nullInterval -gt 0 ]; then
                    expected=$(bc <<< "($duration / ($nullInterval / 1000)) / 1")
                    actual=$(utils_media_get_scte_stream_splice_null_count $outSctePath)

                    utils_assert_equals "$actual" "$expected" "SCTE splice_null count on output $outputIdx"

                else
                    test_log "nullInterval = 0 - checking if no splice_null was inserted"
                fi

            else
                expected=0
                actual=$(utils_media_get_scte_streams_count $outMetaPath)

                utils_assert_equals "$actual" "$expected" "No SCTE PID existence on output $outputIdx"
            fi
        done
    done
}

function _default_cleanup_src() {
    test_cleanup_src
}

function _default_terminate() {
    :
}

declare -a DEFAULT_ENTRY_POINT_NAMES;
declare -A DEFAULT_ENTRY_POINT_MAP;

DEFAULT_ENTRY_POINT_NAMES+=("initialize");         DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_initialize";
DEFAULT_ENTRY_POINT_NAMES+=("prepare-src");        DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_prepare_src";
DEFAULT_ENTRY_POINT_NAMES+=("prepare-db");         DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_prepare_db";
DEFAULT_ENTRY_POINT_NAMES+=("analyze-src");        DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_analyze_src";
DEFAULT_ENTRY_POINT_NAMES+=("start-src-listener"); DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_start_src_listener";
DEFAULT_ENTRY_POINT_NAMES+=("start-cap-listener"); DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_start_cap_listener";
DEFAULT_ENTRY_POINT_NAMES+=("start-httpd");        DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_start_httpd";
DEFAULT_ENTRY_POINT_NAMES+=("start-pois");         DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_start_pois";
DEFAULT_ENTRY_POINT_NAMES+=("start-transcoder");   DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_start_transcoder";
DEFAULT_ENTRY_POINT_NAMES+=("start-cap-caller");   DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_start_cap_caller";
DEFAULT_ENTRY_POINT_NAMES+=("start-src-caller");   DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_start_src_caller";

DEFAULT_ENTRY_POINT_NAMES+=("idle"); DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_idle";

DEFAULT_ENTRY_POINT_NAMES+=("stop-src-caller");   DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_stop_src_caller";
DEFAULT_ENTRY_POINT_NAMES+=("stop-cap-caller");   DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_stop_cap_caller";
DEFAULT_ENTRY_POINT_NAMES+=("stop-transcoder");   DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_stop_transcoder";
DEFAULT_ENTRY_POINT_NAMES+=("stop-pois");         DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_stop_pois";
DEFAULT_ENTRY_POINT_NAMES+=("stop-httpd");        DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_stop_httpd";
DEFAULT_ENTRY_POINT_NAMES+=("stop-cap-listener"); DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_stop_cap_listener";
DEFAULT_ENTRY_POINT_NAMES+=("stop-src-listener"); DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_stop_src_listener";
DEFAULT_ENTRY_POINT_NAMES+=("analyze-out");       DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_analyze_out";

DEFAULT_ENTRY_POINT_NAMES+=("verify");            DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_verify";

DEFAULT_ENTRY_POINT_NAMES+=("cleanup-src");       DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_cleanup_src";
DEFAULT_ENTRY_POINT_NAMES+=("terminate");         DEFAULT_ENTRY_POINT_MAP["${DEFAULT_ENTRY_POINT_NAMES[-1]}"]="_default_terminate";

function test_execute() {
    local testDir=
    local userEntryPoints=
    local duration=${TEST_DEFAULT_DURATION_SECONDS}

    if [ $# -gt 0 ]; then
        testDir=$1; shift
    fi

    if [ $# -gt 0 ]; then
        duration=$1; shift
    fi

    if [ $# -gt 0 ]; then
        local -n userEntryPoints=$1; shift
    fi

    # global paths
    TEST_PATH_SRC_BBB="${TEST_PATH_DIR_MEDIA}/${TEST_FILE_SRC_BBB}"

    # Test related paths
    TEST_PATH_DIR_TEST_BASE=$testDir
    TEST_PATH_DIR_TMP=${TEST_PATH_DIR_TEST_BASE}/${TEST_DIR_TMP}
    TEST_PATH_DIR_SRC=${TEST_PATH_DIR_TEST_BASE}/${TEST_DIR_SRC}
    TEST_PATH_DIR_RESULT=${TEST_PATH_DIR_TEST_BASE}/${TEST_DIR_RESULT}
    TEST_PATH_DIR_HTDOCS=${TEST_PATH_DIR_TEST_BASE}/${TEST_DIR_HTDOCS}

    TEST_PATH_SRC_LICENSE=${TEST_PATH_DIR_SRC}/${TEST_FILE_SRC_LICENSE}
    TEST_PATH_SRC_CONFIG=${TEST_PATH_DIR_SRC}/${TEST_FILE_SRC_CONFIG}
    TEST_PATH_SRC_EVENTS=${TEST_PATH_DIR_SRC}/${TEST_FILE_SRC_EVENTS}
    TEST_PATH_SRC_SCTE=${TEST_PATH_DIR_SRC}/${TEST_FILE_SRC_SCTE}
    TEST_PATH_SRC_POIS=${TEST_PATH_DIR_SRC}/${TEST_FILE_SRC_POIS}
    TEST_PATH_SRC_SUBTITLES=${TEST_PATH_DIR_SRC}/${TEST_FILE_SRC_SUBTITLES}
    
    TEST_PATH_SRC_TS=${TEST_PATH_DIR_TMP}/${TEST_FILE_SRC_TS}    
    TEST_PATH_SRC_TS_META=${TEST_PATH_DIR_TMP}/${TEST_FILE_SRC_TS_META}
    TEST_PATH_SRC_DB=${TEST_PATH_DIR_TMP}/${TEST_FILE_SRC_DB}

    TEST_PATH_SCHEMA_DB=${TEST_PATH_DIR_SCHEMA}/${TEST_FILE_SCHEMA_DB}

    TEST_PATH_OUT_TEST_LOG=${TEST_PATH_DIR_TMP}/${TEST_FILE_OUT_TEST_LOG}
    TEST_PATH_OUT_CMD_LOG=${TEST_PATH_DIR_TMP}/${TEST_FILE_OUT_CMD_LOG}
    TEST_PATH_OUT_TS=${TEST_PATH_DIR_TMP}/${TEST_FILE_OUT_TS}
    TEST_PATH_OUT_TS_META=${TEST_PATH_DIR_TMP}/${TEST_FILE_OUT_TS_META}
    TEST_PATH_OUT_TS_SCTE=${TEST_PATH_DIR_TMP}/${TEST_FILE_OUT_TS_SCTE}
    TEST_PATH_OUT_AUDIO_RAW=${TEST_PATH_DIR_TMP}/${TEST_FILE_OUT_AUDIO_RAW}

    TEST_PID_HTTPD=
    TEST_PID_POIS=
    TEST_PID_TRANSCODER=

    declare -Ag TEST_PID_TSPLAY
    declare -Ag TEST_PID_SRT_SRC
    declare -Ag TEST_PID_HLS_HTTPD
    declare -Ag TEST_PID_CAPTURES

    TEST_DURATION=$duration

    if [ ! -z $TEST_LOG_TO_STDOUT ] && [ $TEST_LOG_TO_STDOUT -eq 1 ]; then
        FORCE_STD_OUT_OPTION="-f"
    fi

    rm -rf ${TEST_PATH_DIR_TMP}; mkdir -p ${TEST_PATH_DIR_TMP}

    # FIXME
    if [ ! -e ${TEST_PATH_SRC_LICENSE} ]; then
        TEST_PATH_SRC_LICENSE=/tmp/${TEST_FILE_SRC_LICENSE}
    fi

    test_dbg "TEST_PATH_DIR_TMP    = ${TEST_PATH_DIR_TMP}"
    test_dbg "TEST_PATH_DIR_SRC    = ${TEST_PATH_DIR_SRC}"
    test_dbg "TEST_PATH_DIR_RESULT = ${TEST_PATH_DIR_RESULT}"    

    for name in "${DEFAULT_ENTRY_POINT_NAMES[@]}"; do
        local defaultEntryPoint=${DEFAULT_ENTRY_POINT_MAP[$name]}

        if [[ -n "${userEntryPoints["${name}"]}" ]]; then
            local userEntryPoint=${userEntryPoints["${name}"]}

            test_dbg "Running user entry point for name \"$name\""

            $userEntryPoint "$name" "$defaultEntryPoint"
        else
            test_dbg "Running default entry point for name \"$name\""

            $defaultEntryPoint "$name" ""
        fi 
    done
}

function test_fail() {
    TEST_RC=$TEST_RC_ERROR

    exit
}

function _handle_int() {
    test_dbg "Interrupt handler triggered"

    TEST_RC=$TEST_RC_SIGNALED
}

function _handle_err() {
    local line_no=$BASH_LINENO
    local function_name=${FUNCNAME[1]}
    local command=$BASH_COMMAND
    
    test_err "Error occurred in function '$function_name' at line $line_no"
    test_err "Failed command: $command"
    test_dbg "Error handler triggered"

    TEST_RC=$TEST_RC_ERROR
}

function _handle_exit() {
    local rc=$?

    test_dbg "Exit handler triggered"

    if [ $TEST_RC -eq 0 ]; then
        TEST_RC=$rc
    fi

    test_cleanup -a

    if ! test_cleanup_disabled; then
        rm -fr $TEST_PATH_DIR_TMP 
    fi

    exit $TEST_RC
}

trap _handle_err  ERR
trap _handle_int  INT
trap _handle_exit EXIT
