#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

set -e

source $SCRIPT_DIR/_functions.sh

ARG_INPUT_ID=

function show_usage() {
    echo "Usage: $0 -i <input_id>"
    exit 1
}

while getopts ':i:' flag; do
    case "${flag}" in
        i) ARG_INPUT_ID=${OPTARG}
        ;;
        *) show_usage
        ;;
        
    esac
done

if [ -z "$ARG_INPUT_ID" ]; then
    show_usage
fi

call_get "inputs/${ARG_INPUT_ID}"
