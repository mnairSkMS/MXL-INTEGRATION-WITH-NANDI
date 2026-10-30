#!/usr/bin/env bash
# The ring is fixed by the plugin (mxlsink has no history property). Record how it is sized and
# check the shared-memory cost against the nominal V210 frame size, because the compose file has to
# provide that much tmpfs. 1920x1080 V210 nominal frame: 5,529,600 bytes.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require mxlsink mxlsrc mxl-info

NOMINAL=5529600

for spec in "1280 720 30000/1001" "1920 1080 30000/1001" "1920 1080 60000/1001"; do
    set -- $spec
    rm -rf "$MXL_DOMAIN"; mkdir -p "$MXL_DOMAIN"
    mxl_write_video $MXL_FLOW_VIDEO $3 $1 $2 400
    writer=$MXL_LAST_PID
    mxl_wait_flow $MXL_FLOW_VIDEO 10 || { mxl_fail "$spec: flow did not appear"; continue; }

    grains=$(mxl_flow_field $MXL_FLOW_VIDEO 'Grain count')
    files=$(find "$MXL_DOMAIN" -path '*grains/data.*' | wc -l)
    bytes=$(find "$MXL_DOMAIN" -path '*grains/data.*' -printf '%s\n' | awk '{ s += $1 } END { print s + 0 }')
    kb=$(du -sk "$MXL_DOMAIN" | cut -f1)
    ms=$(awk -v g=$grains -v r=$3 'BEGIN { split(r, a, "/"); printf "%.0f", g * 1000 * a[2] / a[1] }')
    mxl_log "$spec: $grains grains, $files grain files, ${bytes} bytes of grain data, du ${kb} KB, about ${ms} ms of history"
    echo "RESULT $spec grains=$grains grain_bytes=$bytes du_kb=$kb history_ms=$ms"

    [ "$grains" -ge 3 ] 2>/dev/null || mxl_fail "$spec: unexpected grain count '$grains'"
    [ "$files" = "$grains" ]        || mxl_fail "$spec: $files grain files for $grains grains"

    # Per-grain size must be within 1 % of the nominal frame (stride and page padding only).
    expect=$(awk -v w=$1 -v h=$2 'BEGIN { print int((w + 47) / 48) * 128 * h }')
    pergrain=$(awk -v b=$bytes -v g=$grains 'BEGIN { print int(b / g) }')
    awk -v p=$pergrain -v e=$expect 'BEGIN { exit !(p >= e && p <= e * 1.01) }' \
        || mxl_fail "$spec: grain is $pergrain bytes, expected about $expect"

    mxl_stop $writer
done

mxl_test_end
