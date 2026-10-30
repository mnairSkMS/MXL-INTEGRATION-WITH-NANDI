#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

set -e

source $SCRIPT_DIR/_functions.sh

ARG_EVENT_ID=

function show_usage() {
    echo "$0 -e <event_id>"
    exit 1
}

while getopts ':e:' flag; do
    case "${flag}" in
        e) ARG_EVENT_ID=${OPTARG}
        ;;
        *) show_usage
        ;;
        
    esac
done

if [ -z "$ARG_EVENT_ID" ]; then
    show_usage
fi

call_delete "scheduled_events/${ARG_EVENT_ID}" ""
