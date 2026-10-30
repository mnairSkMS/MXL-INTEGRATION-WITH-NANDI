#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

. ${SCRIPT_DIR}/../../../../framework/test.sh

__TEST_DURATION=10

function _user_verify() {
    local name=$1; shift
    local parent=$1; shift

    $parent $name ""

    utils_assert_not_equals $(test_media_output_has_beep_at 0 0 0 2 0.2) "" "Beep at specified position"
    utils_assert_not_equals $(test_media_output_has_beep_at 0 0 0 3 0.2) "" "Beep at specified position"
    utils_assert_not_equals $(test_media_output_has_beep_at 0 0 0 4 0.2) "" "Beep at specified position"

    test_assert_video_frame 0 0 2  ${TEST_PATH_DIR_RESULT}/2.png
    test_assert_video_frame 0 0 3  ${TEST_PATH_DIR_RESULT}/3.png
    test_assert_video_frame 0 0 4  ${TEST_PATH_DIR_RESULT}/4.png
}

function _user_start_src_caller() {
    local name=$1; shift
    local parent=$1; shift

    sleep 15

    $parent $name ""
}

function _user_prepare_src() {
    test_prepare_src -t control -d $__TEST_DURATION
}

declare -A CALLBACKS=(
    ["prepare-src"]="_user_prepare_src"
    ["start-src-caller"]="_user_start_src_caller"
    ["verify"]="_user_verify"
)

test_execute "$SCRIPT_DIR" $__TEST_DURATION CALLBACKS
