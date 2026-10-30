#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

. ${SCRIPT_DIR}/../../../../../framework/test.sh

__TEST_DURATION=10

function _user_prepare_src() {
    test_prepare_src -t bunny -d $__TEST_DURATION -p hls
}

declare -A CALLBACKS=(
    ["prepare-src"]="_user_prepare_src"
)

test_execute "$SCRIPT_DIR" $__TEST_DURATION CALLBACKS
