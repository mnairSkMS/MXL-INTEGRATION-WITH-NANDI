#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

. ${SCRIPT_DIR}/../../../../../framework/test.sh

__TEST_DURATION=20

function _user_initialize() {
    local name=$1; shift
    local parent=$1; shift

    $parent $name ""

    for outIdx in $(seq 0 $(( $(test_config_output_count) - 1 ))); do
        for trsIdx in $(seq 0 $(( $(test_config_output_transport_count $outIdx ) - 1))); do
            local uri=$(test_config_output_transport_uri $outIdx $transportIdx)
            if utils_uri_proto_is_file $uri; then
                test_dbg "Creating output directory for output $outIdx and transport $trsIdx $(utils_uri_get_path $uri))"
                mkdir -p $(utils_uri_get_path $uri)
            fi
        done
    done
}


function _user_analyze_out() {
    for outIdx in $(seq 0 $(( $(test_config_output_count) - 1 ))); do
        local pid=$(test_config_output_scte_pid $outIdx)

        for trsIdx in $(seq 0 $(( $(test_config_output_transport_count $outIdx) - 1 ))); do
            local f="$(test_media_output_get_path $outIdx $trsIdx)"

            if [ -e "$f" ]; then
                local metaPath=$(test_media_output_get_meta_path $outIdx $trsIdx)

                utils_media_meta_save "${f}/master.m3u8" "$metaPath"

                for i in $(seq 0 $(( $(utils_media_get_audio_streams_count "$metaPath") - 1)) ); do
                    test_proc_call ffmpeg -y -i  "${f}/master.m3u8" -map 0:a:$i -acodec pcm_s16le -f wav $(test_media_output_get_audio_raw_path $outIdx $trsIdx $i)
                done
            fi
        done
    done
}

function _user_prepare_src() {
    test_prepare_src -t bunny -d $__TEST_DURATION -p hls
}

declare -A CALLBACKS=(
    ["initialize"]="_user_initialize"
    ["analyze-out"]="_user_analyze_out"
    ["prepare-src"]="_user_prepare_src"
)

test_execute "$SCRIPT_DIR" 20 CALLBACKS
