#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

. ${SCRIPT_DIR}/../../../../framework/test.sh


function _user_verify() {
    local name=$1; shift
    local parent=$1; shift

    $parent $name ""

    utils_assert_equals $(utils_media_get_audio_stream_bitrate $(test_media_output_get_meta_path 0 0) 0) 448000 "Audio bitrate"
}

declare -A CALLBACKS=(
    ["verify"]="_user_verify"
)

test_execute "$SCRIPT_DIR" 5 CALLBACKS
