#!/usr/bin/env bash
# The MXL plugin, library and tools are present in the image and load against its GStreamer.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"

mxl_require mxlsink mxlsrc mxl-info

for element in mxlsink mxlsrc; do
    mxl_check "gst-inspect-1.0 $element" gst-inspect-1.0 "$element"
done

mxl_check "mxlsink accepts v210 video"     bash -c "gst-inspect-1.0 mxlsink | grep -q 'format: v210'"
mxl_check "mxlsink accepts F32LE audio"    bash -c "gst-inspect-1.0 mxlsink | grep -q 'format: F32LE'"
mxl_check "mxlsrc outputs v210 video"      bash -c "gst-inspect-1.0 mxlsrc  | grep -q 'format: v210'"
mxl_check "mxlsrc has the flow-id properties" bash -c "gst-inspect-1.0 mxlsrc | grep -q video-flow-id && gst-inspect-1.0 mxlsrc | grep -q audio-flow-id"
mxl_check "libmxl.so is resolvable"        bash -c "ldconfig -p | grep -q libmxl.so"

# The Nandi plugins this project depends on must still load next to the MXL ones.
for element in fallbackswitch dlbpatenc; do
    mxl_check "existing element $element still loads" gst-inspect-1.0 "$element"
done

mxl_test_end
