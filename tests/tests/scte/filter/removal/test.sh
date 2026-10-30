#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

. ${SCRIPT_DIR}/../../../../framework/test.sh

function _user_verify() {
    local name=$1; shift
    local parent=$1; shift

    $parent $name ""

    for outIdx in $(seq 0 $(( $(test_config_output_count) - 1 ))); do
        for trsIdx in $(seq 0 $(( $(test_config_output_transport_count $outIdx ) - 1))); do
            local outSctePath=$(test_media_output_get_scte_path $outIdx $trsIdx)

            utils_assert_equals 0 $(utils_media_get_scte_stream_splices_count $outSctePath) "Checking if there is not SCTE events on output $outIdx"
        done
    done
}

declare -A CALLBACKS=(
    ["verify"]="_user_verify"
)

test_execute "$SCRIPT_DIR" 10 CALLBACKS
