#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

. ${SCRIPT_DIR}/../../../../../framework/test.sh

__TEST_DURATION=20

function _user_verify() {
    local name=$1; shift
    local parent=$1; shift

    $parent $name ""

#    test_assert_video_frame 0 0 5  ${TEST_PATH_DIR_RESULT}/original.png
#    test_assert_video_frame 0 0 11 ${TEST_PATH_DIR_RESULT}/logo.png
#    test_assert_video_frame 0 0 15 ${TEST_PATH_DIR_RESULT}/ticker.png
#    test_assert_video_frame 0 0 20 ${TEST_PATH_DIR_RESULT}/original.png
}

function _user_prepare_src() {
    test_prepare_src -t bunny -d $__TEST_DURATION -O 1
}

declare -A CALLBACKS=(
    ["prepare-src"]="_user_prepare_src"
    ["verify"]="_user_verify"
)

test_execute "$SCRIPT_DIR" $__TEST_DURATION CALLBACKS