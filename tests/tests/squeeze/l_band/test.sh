#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

. ${SCRIPT_DIR}/../../../framework/test.sh

__TEST_DURATION=25

function _user_verify() {
    local name=$1; shift
    local parent=$1; shift

    $parent $name ""

    test_assert_video_frame 0 0 7  ${TEST_PATH_DIR_RESULT}/original.png
    test_assert_video_frame 0 0 12 ${TEST_PATH_DIR_RESULT}/squeeze.png
    test_assert_video_frame 0 0 17 ${TEST_PATH_DIR_RESULT}/original.png
}

function _user_prepare_src() {
    test_prepare_src -t image -d $__TEST_DURATION
}

declare -A CALLBACKS=(
    ["prepare-src"]="_user_prepare_src"
    ["verify"]="_user_verify"
)

test_execute "$SCRIPT_DIR" $__TEST_DURATION CALLBACKS
