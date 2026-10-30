#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

. ${SCRIPT_DIR}/../../../../framework/test.sh

__TEST_DURATION=25

# Solid-colour spots. The colour of the aired frame is what identifies the source on air, without
# needing a reference image: the programme is the grey test pattern, the spots are pure blue and
# pure yellow.
__AD_DURATION=10

function _user_prepare_src() {
    test_prepare_src -t image -d $__TEST_DURATION

    local adDir=${TEST_PATH_DIR_TMP}
    local index=1

    for colour in blue yellow; do
        test_proc_call ffmpeg -y -v error \
            -f lavfi -i "color=c=${colour}:size=960x540:rate=24:duration=${__AD_DURATION}" \
            -f lavfi -i "sine=frequency=1000:sample_rate=44100:duration=${__AD_DURATION}" \
            -vcodec h264 -acodec mp2 -ac 2 \
            -t ${__AD_DURATION} \
            ${adDir}/ad-${index}.ts

        index=$((index + 1))
    done

    {
        echo "#EXTM3U"
        echo "#EXTINF:${__AD_DURATION},ad-1"
        echo "file://${adDir}/ad-1.ts"
        echo "#EXTINF:${__AD_DURATION},ad-2"
        echo "file://${adDir}/ad-2.ts"
    } > ${adDir}/ads.m3u8
}

# Reports which source a frame came from: "programme" for the grey test pattern, "ad" for either
# saturated spot. Colour is compared loosely so encoding differences cannot flip the answer.
function _frame_source_at() {
    local timeOffset=$1; shift

    local framePath=${TEST_PATH_DIR_TMP}/frame_${timeOffset}.png

    test_proc_call ffmpeg -y -v error \
        -i $(test_media_output_get_path 0 0) -ss ${timeOffset} -vframes 1 ${framePath}

    if [ ! -s ${framePath} ]; then
        # No frame at this offset means the output is shorter than the scenario, which is a
        # failure in itself: reporting a source here would hide it.
        echo "missing"
        return
    fi

    local rgb=$(convert ${framePath} -resize 1x1 -depth 8 \
        -format "%[fx:int(255*r)] %[fx:int(255*g)] %[fx:int(255*b)]" info:)

    local r=$(echo $rgb | cut -d' ' -f1)
    local g=$(echo $rgb | cut -d' ' -f2)
    local b=$(echo $rgb | cut -d' ' -f3)

    if [ -z "$r" ] || [ -z "$g" ] || [ -z "$b" ]; then
        echo "unreadable"
        return
    fi

    # The test pattern averages to a neutral grey, so all three channels sit close together.
    # Either spot is saturated, so at least one channel is far from the others.
    local spread=$(( (r > g ? r - g : g - r) + (g > b ? g - b : b - g) + (r > b ? r - b : b - r) ))

    if [ $spread -lt 60 ]; then
        echo "programme"
    else
        echo "ad"
    fi
}

function _user_verify() {
    local name=$1; shift
    local parent=$1; shift

    $parent $name ""

    # Cue-out at 5 s and cue-in at 11 s, then a second break from 15 s to 19 s.
    utils_assert_equals "$(_frame_source_at 3)"  "programme" "Programme is on air before the first cue-out"
    utils_assert_equals "$(_frame_source_at 8)"  "ad"        "Ad source is on air during the first break"
    utils_assert_equals "$(_frame_source_at 13)" "programme" "Programme returns on the cue-in"
    utils_assert_equals "$(_frame_source_at 17)" "ad"        "Ad source is on air during the second break"
    utils_assert_equals "$(_frame_source_at 21)" "programme" "Programme returns after the second break"

    # Ad insertion defaults to passthrough, so the source markers must still reach the output
    # untouched while the replacement is on air.
    local spliceCount=$(utils_media_get_scte_stream_splice_insert_count $(test_media_output_get_scte_path 0 0))

    if [ "$spliceCount" -lt 1 ]; then
        test_fail "Expected source splice_insert sections to be passed through, found $spliceCount"
    fi
}

declare -A CALLBACKS=(
    ["prepare-src"]="_user_prepare_src"
    ["verify"]="_user_verify"
)

test_execute "$SCRIPT_DIR" $__TEST_DURATION CALLBACKS
