#### Config file extraction utils
function test_config_input_count() {
    utils_config_input_count "${TEST_PATH_SRC_CONFIG}"
}

function test_config_input_uri() {
    utils_config_input_uri "${TEST_PATH_SRC_CONFIG}" $1
}

function test_config_output_count() {
    utils_config_output_count "${TEST_PATH_SRC_CONFIG}"
}

function test_config_output_transport_count() {
    utils_config_output_transport_count "${TEST_PATH_SRC_CONFIG}" $1
}

function test_config_output_transport_uri() {
    utils_config_output_transport_uri "${TEST_PATH_SRC_CONFIG}" $1 $2
}

### Stream
function test_config_output_streams_count() {
    utils_config_output_streams_count "${TEST_PATH_SRC_CONFIG}"
}

function test_config_output_stream_type() {
    utils_config_output_stream_type "${TEST_PATH_SRC_CONFIG}" $1
}

function test_config_output_stream_codec() {
    utils_config_output_stream_codec "${TEST_PATH_SRC_CONFIG}" $1
}

### Audio streams
function test_config_output_stream_audio_count() {
    utils_config_output_stream_audio_count "${TEST_PATH_SRC_CONFIG}" $1
}

function test_config_output_stream_audio_codec() {
    utils_config_output_stream_audio_codec "${TEST_PATH_SRC_CONFIG}" $1 $2
}

function test_config_output_stream_audio_rate() {
    utils_config_output_stream_audio_rate "${TEST_PATH_SRC_CONFIG}" $1 $2
}

function test_config_output_stream_audio_channels() {
    utils_config_output_stream_audio_channels "${TEST_PATH_SRC_CONFIG}" $1 $2
}

### Video streams
function test_config_output_stream_video_count() {
    utils_config_output_stream_video_count "${TEST_PATH_SRC_CONFIG}" $1
}

function test_config_output_stream_video_codec() {
    utils_config_output_stream_video_codec "${TEST_PATH_SRC_CONFIG}" $1 $2
}

function test_config_output_stream_video_width() {
    utils_config_output_stream_video_width "${TEST_PATH_SRC_CONFIG}" $1 $2
}

function test_config_output_stream_video_height() {
    utils_config_output_stream_video_height "${TEST_PATH_SRC_CONFIG}" $1 $2
}

function test_config_output_stream_video_rate() {
    utils_config_output_stream_video_rate "${TEST_PATH_SRC_CONFIG}" $1 $2
}

### SCTE
function test_config_output_scte_count() {
    utils_config_output_scte_count "${TEST_PATH_SRC_CONFIG}"
}

function test_config_output_scte_pid() {
    utils_config_output_scte_pid "${TEST_PATH_SRC_CONFIG}" $1
}

function test_config_output_scte_enabled() {
    utils_config_output_scte_enabled "${TEST_PATH_SRC_CONFIG}" $1
}

function test_config_output_scte_mode() {
    utils_config_output_scte_mode "${TEST_PATH_SRC_CONFIG}" $1
}

function test_config_output_scte_passthrough() {
    utils_config_output_scte_passthrough "${TEST_PATH_SRC_CONFIG}" $1
}

function test_config_output_scte_removal() {
    utils_config_output_scte_removal "${TEST_PATH_SRC_CONFIG}" $1
}

function test_config_output_scte_nullInterval() {
    utils_config_output_scte_nullInterval "${TEST_PATH_SRC_CONFIG}" $1
}