function utils_config_is_legacy() {
    local configPath=$1; shift

    # TODO: Check version
    utils_json_key_has "$configPath" ".transcoder.inputs[$index]" "uri"
}

function utils_config_input_count() {
    local configPath=$1; shift

    utils_json_array_length "$configPath" ".transcoder.inputs"
}

function utils_config_input_uri() {
    local configPath=$1; shift
    local index=$1; shift
    local result=()

    if utils_config_is_legacy "$configPath"; then
        local uri=$(utils_json_value_get "$configPath" ".transcoder.inputs[$index].uri")
        if [ ! -z "$uri" ]; then
            result+=("$uri")
        fi

    else
        local isArray=$(jq "if .transcoder.inputs[$index].transport.uri | type == \"array\" then 1 else 0 end" "$configPath")
        
        if [ "$isArray" = "1" ]; then
            local count=$(utils_json_array_length "$configPath" ".transcoder.inputs[$index].transport.uri")

            for ((i=0; i<count; i++)); do
                local uri=$(utils_json_value_get "$configPath" ".transcoder.inputs[$index].transport.uri[$i]")
                if [ ! -z "$uri" ]; then
                    result+=("$uri")
                fi
            done

        else
            local uri=$(utils_json_value_get "$configPath" ".transcoder.inputs[$index].transport.uri")
            
            if [ ! -z "$uri" ]; then
                result+=("$uri")
            fi
        fi
    fi

    echo "${result[@]}"
}

function utils_config_output_count() {
    local configPath=$1; shift

    utils_json_array_length "$configPath" ".transcoder.outputs"
}

function utils_config_output_transport_count() {
    local configPath=$1; shift
    local index=$1; shift

    if utils_config_is_legacy "$configPath"; then
        echo 1
    else
        utils_json_array_length "$configPath" ".transcoder.outputs[$index].transports"
    fi
}

function utils_config_output_transport_uri() {
    local configPath=$1; shift
    local outputIndex=$1; shift
    local transportIndex=$1; shift

    if utils_config_is_legacy "$configPath"; then
        utils_json_value_get "$configPath" ".transcoder.outputs[$outputIndex].uri"
    else
        utils_json_value_get "$configPath" ".transcoder.outputs[$outputIndex].transports[$transportIndex].uri"
    fi
}

function utils_config_output_streams_count() {
    local configPath=$1; shift
    local outputIdx=$1; shift

    utils_json_array_length "$configPath" ".transcoder.outputs[$outputIdx].streams"
}

function utils_config_output_stream_type() {
    local configPath=$1; shift
    local outputIdx=$1; shift
    local streamIdx=$1; shift

    utils_json_value_get "$configPath" ".transcoder.outputs[$outputIdx].streams[$streamIdx].type"
}

function utils_config_output_stream_codec() {
    local configPath=$1; shift
    local outputIdx=$1; shift
    local streamIdx=$1; shift

    utils_json_value_get "$configPath" ".transcoder.outputs[$outputIdx].streams[$streamIdx].codec"
}

function _getStreamsCount() {
    local configPath=$1; shift
    local outputIdx=$1; shift   
    local type=$1; shift

    utils_json_array_length "$configPath" "[.transcoder.outputs[$outputIdx].streams[] | select(.type == \"$type\")]"
}

function utils_config_output_stream_audio_count() {
    local configPath=$1; shift
    local outputIdx=$1; shift

    _getStreamsCount "$configPath" "$outputIdx" "audio"
}

function _getTypedStreamValue() {
    local configPath=$1; shift
    local outputIdx=$1; shift
    local streamIdx=$1; shift
    local streamType=$1; shift
    local valueName=$1; shift

    utils_json_value_get "$configPath" "[.transcoder.outputs[$outputIdx].streams[] | select(.type == \"${streamType}\")][$streamIdx].${valueName}"
}

function utils_config_output_stream_audio_codec() {
    local configPath=$1; shift
    local outputIdx=$1; shift
    local streamIdx=$1; shift

    _getTypedStreamValue $configPath $outputIdx $streamIdx "audio" "codec"
}

function utils_config_output_stream_audio_rate() {
    local configPath=$1; shift
    local outputIdx=$1; shift
    local streamIdx=$1; shift

    _getTypedStreamValue $configPath $outputIdx $streamIdx "audio" "audio.rate"
}

function utils_config_output_stream_audio_channels() {
    local configPath=$1; shift
    local outputIdx=$1; shift
    local streamIdx=$1; shift

    _getTypedStreamValue $configPath $outputIdx $streamIdx "audio" "audio.channels"
}

function utils_config_output_stream_video_count() {
    local configPath=$1; shift
    local outputIdx=$1; shift

    _getStreamsCount "$configPath" "$outputIdx" "video"
}

function utils_config_output_stream_video_codec() {
    local configPath=$1; shift
    local outputIdx=$1; shift
    local streamIdx=$1; shift

    _getTypedStreamValue $configPath $outputIdx $streamIdx "video" "codec"
}

function utils_config_output_stream_video_width() {
    local configPath=$1; shift
    local outputIdx=$1; shift
    local streamIdx=$1; shift

    _getTypedStreamValue $configPath $outputIdx $streamIdx "video" "video.width"
}

function utils_config_output_stream_video_height() {
    local configPath=$1; shift
    local outputIdx=$1; shift
    local streamIdx=$1; shift

    _getTypedStreamValue $configPath $outputIdx $streamIdx "video" "video.height"
}

function utils_config_output_stream_video_rate() {
    local configPath=$1; shift
    local outputIdx=$1; shift
    local streamIdx=$1; shift

    _getTypedStreamValue $configPath $outputIdx $streamIdx "video" "video.rate"
}

# SCTE
function utils_config_output_scte_count() {
    local configPath=$1; shift
    local outputIdx=$1; shift

    utils_json_array_length "$configPath" "[.transcoder.outputs[] | select(.scte.mode == \"passthrough\")]"
}

function utils_config_output_scte_pid() {
    local configPath=$1; shift
    local outputIdx=$1; shift

    utils_json_value_get "$configPath" ".transcoder.outputs[$outputIdx].scte.pid"
}

function utils_config_output_scte_enabled() {
    local configPath=$1; shift
    local outputIdx=$1; shift

    if [ "x$(utils_config_output_scte_pid $configPath $outputIdx)" != "x" ]; then
        echo "1"
    else
        echo "0"
    fi
}

function utils_config_output_scte_mode() {
    local configPath=$1; shift
    local outputIdx=$1; shift    

    utils_json_value_get "$configPath" ".transcoder.outputs[$outputIdx].scte.mode"
}

function utils_config_output_scte_passthrough() {
    local configPath=$1; shift
    local outputIdx=$1; shift

    local mode=$(utils_config_output_scte_mode $configPath $outputIdx)
    if [ "x$mode" = "xpassthrough" ]; then
        echo 1
    else
        echo 0
    fi
}

function utils_config_output_scte_removal() {
    local configPath=$1; shift
    local outputIdx=$1; shift

    local mode=$(utils_config_output_scte_mode $configPath $outputIdx)
    if [ "x$mode" = "xremoval" ]; then
        echo 1
    else
        echo 0
    fi
}

function utils_config_output_scte_nullInterval() {
    local configPath=$1; shift
    local outputIdx=$1; shift

    local value=$(utils_json_value_get "$configPath" ".transcoder.outputs[$outputIdx].scte.nullInterval")
    if [ "x$value" == "x" ]; then
        echo 0
    else
        echo $value
    fi
}