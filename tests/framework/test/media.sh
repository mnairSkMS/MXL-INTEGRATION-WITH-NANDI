function test_media_output_get_path() {
    local outIdx=$1; shift
    local trsIdx=$1; shift

    echo "${TEST_PATH_OUT_TS}.$outIdx.$trsIdx"
}

function test_media_output_get_meta_path() {
    local outIdx=$1; shift
    local trsIdx=$1; shift

    echo "${TEST_PATH_OUT_TS_META}.$outIdx.$trsIdx"
}

function test_media_output_get_scte_path() {
    local outIdx=$1; shift
    local trsIdx=$1; shift

    echo "${TEST_PATH_OUT_TS_SCTE}.$outIdx.$trsIdx"
}

function test_media_output_get_audio_raw_path() {
    local outIdx=$1; shift
    local trsIdx=$1; shift
    local streamIdx=$1; shift

    echo "${TEST_PATH_OUT_AUDIO_RAW}.${outIdx}.${trsIdx}.${streamIdx}"
}

function test_media_output_has_beep_at() {
    local outIdx=$1; shift
    local trsIdx=$1; shift
    local strIdx=$1; shift
    local timeOffset=$1; shift
    local tolerance=$1; shift

    local beeps=$(utils_media_get_beeps $(test_media_output_get_audio_raw_path $outIdx $trsIdx $strIdx))
    local found=0

    for i in $(seq 0 $(( $(utils_json_array_length "$beeps" ".beat_times") - 1 ))); do
        local pos=$(utils_json_value_get "$beeps" ".beat_times[$i]")

        local res=$(echo "($pos >= ($timeOffset - $tolerance)) && ($pos <= ($timeOffset + $tolerance))" | bc)
        if [ "$res" -eq 1 ]; then
            echo $pos
            return
        fi
    done
}
