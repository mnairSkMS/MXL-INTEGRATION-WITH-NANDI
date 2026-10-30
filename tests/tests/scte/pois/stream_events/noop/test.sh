#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

. ${SCRIPT_DIR}/../../../../../framework/test.sh

function _user_verify() {
    local name=$1; shift
    local parent=$1; shift

    $parent $name ""

    for outIdx in $(seq 0 $(( $(test_config_output_count) - 1 ))); do
        for trsIdx in $(seq 0 $(( $(test_config_output_transport_count $outIdx ) - 1))); do
            local outSctePath=$(test_media_output_get_scte_path $outIdx $trsIdx)

            utils_assert_equals $(utils_media_get_scte_stream_splices_count       $outSctePath) 3 "Checking if there are SCTE events on output $outIdx"
            utils_assert_equals $(utils_media_get_scte_stream_splice_insert_count $outSctePath) 2 "Checking number of splice_insert events on output $outIdx"

            utils_assert_equals "$(utils_media_get_scte_stream_splice_insert_at_id             $outSctePath 0)" "100"   "Event 0 ID"
            utils_assert_equals "$(utils_media_get_scte_stream_splice_insert_at_out_of_network $outSctePath 0)" "true"  "Event 0 out_of_network flag"
            utils_assert_equals "$(utils_media_get_scte_stream_splice_insert_at_id             $outSctePath 1)" "100"   "Event 1 ID"
            utils_assert_equals "$(utils_media_get_scte_stream_splice_insert_at_out_of_network $outSctePath 1)" "false" "Event 1 out_of_network flag"

            utils_assert_equals $(utils_media_get_scte_stream_time_signal_count $outSctePath) 1 "Checking number of time_signal events on output $outIdx"
            utils_assert_equals $(utils_media_get_scte_stream_time_signal_segmentation_descriptor_count $outSctePath 0) 1 "Checking number of descriptors in time_signal event"
        done
    done
}

declare -A CALLBACKS=(
    ["verify"]="_user_verify"
)

test_execute "$SCRIPT_DIR" 20 CALLBACKS
