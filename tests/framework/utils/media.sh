function utils_media_get_duration() {
    local path=$1

    utils_json_value_get $path '.format.duration'
}

function utils_media_get_streams_count() {
    local path=$1

    utils_json_array_length $path '.streams'
}

function __get_streams_count() {
    local path=$1; shift
    local type=$1; shift
    local name=$1; shift

    local query="[.streams[] | select(.codec_type == \"$type\""

    if [ "x$name" != "x" ]; then
        query="$query and .codec_name == \"$name\""
    fi

    query="$query )]"

    utils_json_array_length $path "$query"
}

function utils_media_get_audio_streams_count() {
    local path=$1

    __get_streams_count "$path" "audio" ""
}

function __getTypedStreamValue() {
    local path=$1; shift
    local idx=$1; shift
    local codecType=$1; shift
    local codecName=$1; shift
    local value=$1; shift

    local query="[.streams[] | select(.codec_type == \"$codecType\""

    if [ "x$codecName" != "x" ]; then
        query="$query and .codec_name == \"$codecName\""
    fi

    query="$query)][$idx].$value"

    utils_json_value_get $path "$query"    
}

function utils_media_get_audio_stream_codec_at() {
    local path=$1; shift
    local idx=$1; shift

    __getTypedStreamValue $path $idx "audio" "" "codec_name" 
}

function utils_media_get_audio_stream_bitrate() {
    local path=$1; shift
    local idx=$1; shift

    __getTypedStreamValue $path $idx "audio" "" "bit_rate" 
}

function utils_media_get_audio_stream_rate() {
    local path=$1; shift
    local idx=$1; shift

    __getTypedStreamValue $path $idx "audio" "" "sample_rate" 
}

function utils_media_get_audio_stream_channels() {
    local path=$1; shift
    local idx=$1; shift

    __getTypedStreamValue $path $idx "audio" "" "channels" 
}

function utils_media_get_video_streams_count() {
    local path=$1

    __get_streams_count "$path" "video" ""
}

function utils_media_get_video_stream_codec_at() {
    local path=$1; shift
    local idx=$1; shift

    __getTypedStreamValue $path $idx "video" "" "codec_name" 
}

function utils_media_get_video_stream_width() {
    local path=$1; shift
    local idx=$1; shift

    __getTypedStreamValue $path $idx "video" "" "width" 
}

function utils_media_get_video_stream_height() {
    local path=$1; shift
    local idx=$1; shift

    __getTypedStreamValue $path $idx "video" "" "height" 
}

function utils_media_get_video_stream_rate() {
    local path=$1; shift
    local idx=$1; shift

    local rate=$(__getTypedStreamValue $path $idx "video" "" "avg_frame_rate")

    echo $rate | cut -d '/' -f 1
}

function utils_media_get_subtitle_streams_count() {
    local path=$1

    __get_streams_count "$path" "subtitle" ""
}

### SCTE

function utils_media_get_scte_streams_count() {
    local path=$1

    __get_streams_count "$path" "data" "scte_35"
}

function utils_media_get_scte_stream_pid() {
    local path=$1; shift
    local idx=$1; shift

    __getTypedStreamValue $path $idx "data" "scte_35" "id"
}

function utils_media_get_scte_stream_exist() {
    local path=$1; shift
    local pid=$1; shift

    for idx in $(seq 0 $(( $(utils_media_get_scte_streams_count $path) - 1 ))); do
        local tmp=$(utils_media_get_scte_stream_pid $path $idx)

        if [ $(bc <<< "$(( $tmp )) == ${pid}") -eq 1 ]; then
            echo 1
            return
        fi
    done

    echo 0
}

function utils_media_get_scte_stream_splices_count() {
    local path=$1; shift

    utils_json_array_length "$path" "[.[] | select(.\"#name\" == \"splice_information_table\")]"
}

function __getScteEventsOfType() {
    local path=$1; shift
    local type=$1; shift

    utils_json_value_get "$path" "[.[] | select((.\"#name\" == \"splice_information_table\") and (.\"#nodes\"[] | .\"#name\" == \"$type\")) ]"
}

function utils_media_get_scte_stream_splice_null_count() {
    local path=$1; shift

    utils_json_array_length "$(__getScteEventsOfType $path splice_null)" "." 
}

function utils_media_get_scte_stream_splice_insert_count() {
    local path=$1; shift

    utils_json_array_length "$(__getScteEventsOfType $path splice_insert)" "." 
}

function utils_media_get_scte_stream_splice_insert_at_id() {
    local path=$1; shift
    local index=$1; shift

    utils_json_value_get "$(__getScteEventsOfType $path splice_insert)" ".[$index].\"#nodes\"[] | select(.\"#name\" == \"splice_insert\").splice_event_id"
}

function utils_media_get_scte_stream_splice_insert_at_out_of_network() {
    local path=$1; shift
    local index=$1; shift

    local json=$(__getScteEventsOfType $path splice_insert)

    utils_json_value_get "$json" "if .[$index].\"#nodes\"[] | select(.\"#name\" == \"splice_insert\").out_of_network then \"true\" else \"false\" end"
}

function utils_media_get_scte_stream_time_signal_count() {
    local path=$1; shift

    utils_json_array_length "$(__getScteEventsOfType $path time_signal)" "." 
}

function utils_media_get_scte_stream_time_signal_segmentation_descriptor_count() {
    local path=$1; shift
    local index=$1; shift

    utils_json_array_length "$(__getScteEventsOfType $path time_signal)" "[.[$index].\"#nodes\"[] | select(.\"#name\" == \"splice_segmentation_descriptor\")]" 
}

function utils_media_get_scte_stream_time_signal_segmentation_descriptor_event_id() {
    local path=$1; shift
    local eventIdx=$1; shift
    local descrIdx=$1; shift

    utils_json_value_get "$(__getScteEventsOfType $path time_signal)" "[.[$eventIdx].\"#nodes\"[] | select(.\"#name\" == \"splice_segmentation_descriptor\")][$descrIdx].segmentation_event_id"
}

function utils_media_get_scte_stream_time_signal_segmentation_descriptor_canceled() {
    local path=$1; shift
    local descrIdx=$1; shift

    utils_json_value_get "$(__getScteEventsOfType $path time_signal)" ".[$index].\"#nodes\"[] | select(.\"#name\" == \"splice_segmentation_descriptor\").segmentation_event_cancel"
}

function utils_media_get_beeps() {
    local filePath=$1; shift

    /opt/venv/bin/python3 ${TEST_PATH_DIR_TOOLS}/beep.py $filePath
}