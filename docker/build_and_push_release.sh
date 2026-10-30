#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

function help() {
    echo "Usage: $(basename $0) [-s] [-h]"
    echo "  -s - Skip cleanup of docker build cache"
    echo "  -h - shows this help"
    exit 1
}

function makeAliasesAndPush() {
    local VERSION="${1#[vV]}"
    local VERSION_MAJOR="${VERSION%%.*}"
    local VERSION_MINOR_PATCH="${VERSION#*.}"
    local VERSION_MINOR="${VERSION_MINOR_PATCH%%.*}"
    local VERSION_PATCH_PRE_RELEASE="${VERSION_MINOR_PATCH#*.}"
    local VERSION_PATCH="${VERSION_PATCH_PRE_RELEASE%%[-+]*}"
    local VERSION_PRE_RELEASE=""

    case "$VERSION" in
        *-*)
            VERSION_PRE_RELEASE="${VERSION#*-}"
            VERSION_PRE_RELEASE="${VERSION_PRE_RELEASE%%+*}"
            ;;
    esac

    echo "Parsed version: major: '$VERSION_MAJOR', minor: '$VERSION_MINOR', patch: '$VERSION_PATCH', pre: '$VERSION_PRE_RELEASE'"

    if [ "x$VERSION_PRE_RELEASE" != "x" ]; then
        ${SCRIPT_DIR}/make_docker_aliases_and_push.sh 
    else
        ${SCRIPT_DIR}/make_docker_aliases_and_push.sh -l
    fi
}

export VERSION=$(cat ${SCRIPT_DIR}/../version.txt)

echo "Building release, version: ${VERSION}"

VERBOSE=0
SKIP_BUILD_CACHE_CLEANUP=0

OPTSTRING="hs"
while getopts ${OPTSTRING} opt; do
    case ${opt} in
        h)
            help
            ;;
        s)
            SKIP_BUILD_CACHE_CLEANUP=1
            ;;
        :)
            echo "Error: Option -${OPTARG} requires an argument"
            exit 1
            ;;
        ?)
            echo "Invalid option: -${OPTARG}"
            exit 1
            ;;
    esac
done

echo "Cleaning up images"

for image in $(docker images -a | grep "ghcr.io/skandha-media-services/skandha-utpl/*" | awk '{ print $3; }'); do
    docker rmi --force $image
done

for image in $(docker images -a | grep "utpl/*" | awk '{ print $3; }'); do
    echo docker rmi --force $image
done

if [ $SKIP_BUILD_CACHE_CLEANUP -eq 0 ]; then
    echo "Cleaning up docker build cache"
		
    echo -ne "Hit Ctrl+C to abort within 5 seconds... "
    for (( I=5; I > 0; I-- )); do
        echo -n "${I} ";
        sleep 1;
    done
    echo "Go!"

    docker builder prune --all --force
fi

${SCRIPT_DIR}/build-docker.sh

makeAliasesAndPush $VERSION
