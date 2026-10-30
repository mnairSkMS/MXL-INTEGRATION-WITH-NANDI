#!/usr/bin/env bash
# A tap whose domain cannot be used (here: a path that cannot be created, and a read-only
# directory) must stay out of the channel's way: the engine keeps running, the on-air output is
# clean, the failure is logged, and the tap retries slowly (every 5 s) instead of on every frame.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

mkdir -p $MXL_TMP/readonly && chmod 555 $MXL_TMP/readonly

for domain in /proc/no-such-mxl-domain $MXL_TMP/readonly; do
    case "$domain" in /proc*) name=missing ;; *) name=readonly ;; esac
    rm -f $MXL_TMP/engine.log
    mxl_write_config $MXL_TMP/config-$name.json "$(mxl_tap_json $domain)"
    mxl_start_source tone
    src=$MXL_SRC_PID
    mxl_start_engine $MXL_TMP/config-$name.json
    eng=$MXL_ENGINE_PID
    sleep 14
    mxl_capture_output 10 $MXL_TMP/out-$name.ts
    mxl_alive $eng || mxl_fail "$name: the engine stopped"

    errors=$(grep -c 'side pipeline error\|unable to build the side pipeline\|would not start' $MXL_TMP/engine.log || true)
    starts=$(grep -c 'mxltap.c, _start' $MXL_TMP/engine.log || true)
    echo "RESULT $name domain: ${starts} start attempts and ${errors} error lines in about 24 s"
    mxl_log "$name: errors $errors, starts $starts"

    [ "$errors" -ge 1 ] || mxl_fail "$name: the unusable domain was not reported"
    # One attempt per tap every 5 s at most: two taps (video and audio) over about 24 s of input is
    # at most 2 * (24 / 5 + 1) = 11 attempts. (One failed attempt writes several error lines.)
    [ "$starts" -le 12 ] || mxl_fail "$name: $starts start attempts in about 24 s: the tap is retrying too often"
    [ "$starts" -ge 2 ]  || mxl_fail "$name: only $starts start attempt(s): the tap is not retrying"
    [ "$(mxl_decode_errors $MXL_TMP/out-$name.ts)" = "0" ] || mxl_fail "$name: the on-air output has decode errors"
    kinds=$(ffprobe -v error -show_entries stream=codec_type -of csv=p=0 $MXL_TMP/out-$name.ts | grep -v '^$' | sort -u | tr '\n' ' ')
    [ "$kinds" = "audio video " ] || mxl_fail "$name: on-air output streams: $kinds"

    mxl_stop $eng; mxl_stop $src
    sleep 2
done

mxl_test_end
