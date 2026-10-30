#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

. ${SCRIPT_DIR}/../../../framework/test.sh


function _user_prepare_src() {
    test_prepare_src -t custom -i ${TEST_PATH_DIR_TEST_BASE}/media/fileSequence0.ts
}

declare -A CALLBACKS=(
    ["prepare-src"]="_user_prepare_src"
)

test_execute "$SCRIPT_DIR" 10 CALLBACKS
