#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

. ${SCRIPT_DIR}/../../../../framework/test.sh


function _user_prepare_src() {
    test_prepare_src -t bunny -d 5 -a '-acodec ac3 -ac 2'
}

declare -A CALLBACKS=(
    ["prepare-src"]="_user_prepare_src"
)

test_execute "$SCRIPT_DIR" 5 CALLBACKS
