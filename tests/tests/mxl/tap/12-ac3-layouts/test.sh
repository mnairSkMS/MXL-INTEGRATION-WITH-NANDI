#!/usr/bin/env bash
# AC-3 sources, decoded by the engine's open decoder (the Dolby SDK paths are integration gated and
# not exercised here). The tap's audio flow takes the configured number of channels:
#   stereo AC-3         -> 2 channels, the tone on both
#   5.1 AC-3, tone only on the centre channel:
#       channels = 6    -> 6 channels, the tone on channel 2 (centre) only
#       channels = 2    -> a stereo downmix, the tone on both channels

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

# source layout, tap channels, expected loud channels (space separated), expected quiet channels
CASES=("stereo 2 0,1 -" "5.1 6 2 0,1,3,4,5" "5.1 2 0,1 -")

for c in "${CASES[@]}"; do
    read -r layout chans loud quiet <<< "$c"
    mxl_log "case source $layout, tap channels $chans"

    rm -rf "$MXL_DOMAIN"; mkdir -p "$MXL_DOMAIN"
    mxl_write_config $MXL_TMP/config.json "$(mxl_tap_json "" $chans)"
    MXL_SRC_AUDIO=$layout mxl_start_source ac3
    src=$MXL_SRC_PID
    mxl_start_engine $MXL_TMP/config.json
    eng=$MXL_ENGINE_PID

    if ! mxl_wait_flow $MXL_FLOW_AUDIO 40; then
        mxl_fail "$layout/$chans: the audio flow did not appear (see engine.log)"
        mxl_stop $eng; mxl_stop $src; continue
    fi
    sleep 4

    mxl_read_audio 3 $MXL_TMP/a.wav || mxl_fail "$layout/$chans: could not read the audio flow"
    got=$(ffprobe -v error -show_entries stream=channels -of default=nw=1:nk=1 $MXL_TMP/a.wav)
    [ "$got" = "$chans" ] || mxl_fail "$layout/$chans: the flow has $got channels"

    for ch in ${loud//,/ }; do
        db=$(mxl_tone_db $MXL_TMP/a.wav 1000 $ch)
        echo "RESULT source $layout, $chans channels: channel $ch 1 kHz peak ${db} dB (expected: tone)"
        awk -v d="$db" 'BEGIN { exit !(d > -40) }' || mxl_fail "$layout/$chans: no tone on channel $ch (${db} dB)"
    done
    if [ "$quiet" != "-" ]; then
        for ch in ${quiet//,/ }; do
            db=$(mxl_tone_db $MXL_TMP/a.wav 1000 $ch)
            echo "RESULT source $layout, $chans channels: channel $ch 1 kHz peak ${db} dB (expected: quiet)"
            awk -v d="$db" 'BEGIN { exit !(d < -55) }' || mxl_fail "$layout/$chans: tone leaked into channel $ch (${db} dB)"
        done
    fi

    mxl_alive $eng || mxl_fail "$layout/$chans: the engine stopped"
    grep -q 'side pipeline error' $MXL_TMP/engine.log && mxl_fail "$layout/$chans: the tap reported a side pipeline error"
    mxl_stop $eng; mxl_stop $src
    sleep 1
done

mxl_test_end
