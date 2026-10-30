__assert_video_diff_margin=10

function test_assert_video_frame() {
    local outputIdx=$1; shift
    local outputTrs=$1; shift
    local timeOffset=$1; shift
    local expectedPath=$1; shift

    local testPath=$TEST_PATH_DIR_TMP/test_frame.png
    local testDiffPath=$TEST_PATH_DIR_TMP/test_frame_diff.png
    local timeOffsetStr=$(date -d@$timeOffset -u +%H:%M:%S)
    
    test_proc_call ffmpeg -y -i $(test_media_output_get_path $outputIdx $outputTrs) -ss $timeOffsetStr -vframes 1 $testPath

    utils_assert_equals "$(identify -format \"%wx%h\" $testPath)" "$(identify -format \"%wx%h\" $expectedPath)" "[$outputIdx] Checking image resolution at $timeOffsetStr"

    local error=$(compare -metric mse $testPath $expectedPath $testDiffPath 2>&1 | cut -f1 -d \()

    utils_assert_equals $(echo "$error < $__assert_video_diff_margin" | bc) 1 "[$outputIdx] Checking image contents at $timeOffsetStr"
}