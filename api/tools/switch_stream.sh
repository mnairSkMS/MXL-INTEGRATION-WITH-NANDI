#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
source $SCRIPT_DIR/_functions.sh

INPUT_ID="" # first input id
CURRENT_STREAM_ID=""
STREAM_IDS=()

get_inputs() {
    curl -s "${SERVER_URL}/inputs" | jq -r '.[0].id'
}

get_input_details() {
    curl -s "${SERVER_URL}/inputs/${INPUT_ID}" | jq -r '.streams[].id'
}

switch_stream() {
    local direction=$1
    local current_index=-1
    
    for i in "${!STREAM_IDS[@]}"; do
        if [[ "${STREAM_IDS[$i]}" == "$CURRENT_STREAM_ID" ]]; then
            current_index=$i
            break
        fi
    done
    
    if [ $current_index -eq -1 ]; then
        echo "Could not find current stream in list"
        exit 1
    fi
    
    if [ "$direction" == "next" ]; then
        new_index=$(( (current_index + 1) % ${#STREAM_IDS[@]} ))
    else
        new_index=$(( (current_index - 1 + ${#STREAM_IDS[@]}) % ${#STREAM_IDS[@]} ))
    fi
    
    call_post "inputs/${INPUT_ID}" "'{\"currentStreamId\": \"${STREAM_IDS[$new_index]}\"}'"
}

echo "Getting first input ID..."
INPUT_ID=$(get_inputs)
if [ -z "$INPUT_ID" ]; then
    echo "No inputs found"
    exit 1
fi

echo "Getting input details..."
STREAM_IDS=($(get_input_details))
if [ ${#STREAM_IDS[@]} -eq 0 ]; then
    echo "No streams found in input"
    exit 1
fi

CURRENT_STREAM_ID=$(curl -s "${SERVER_URL}/inputs/${INPUT_ID}" | jq -r '.currentStreamId')

if [ "$1" == "next" ]; then
    echo "Switching to next stream..."
    switch_stream "next"

elif [ "$1" == "prev" ]; then
    echo "Switching to previous stream..."
    switch_stream "prev"

else
    echo "Usage: $0 [next|prev]"
    exit 1
fi