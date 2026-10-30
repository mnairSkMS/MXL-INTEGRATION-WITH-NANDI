#!/usr/bin/env bash
# Verify SCTE-35 ad markers in an HLS media playlist.
#
#   verify_hls_scte.sh <playlist.m3u8> [scte-pid]
#
# Prints every cue tag in order, and decodes each EXT-OATCLS-SCTE35 / SCTE35=
# base64 payload. With a PID, also dumps the binary sections carried in the TS
# segments so the manifest can be checked against what is actually on the wire.

set -uo pipefail

playlist=${1:?usage: verify_hls_scte.sh <playlist.m3u8> [scte-pid]}
pid=${2:-}
dir=$(dirname "$playlist")

command -v base64 >/dev/null || { echo "base64 not found" >&2; exit 1; }

echo "== cue tags in $playlist =="
grep -nE 'EXT-X-CUE-OUT|EXT-X-CUE-IN|EXT-OATCLS-SCTE35|EXT-X-DATERANGE' "$playlist" \
  || echo "(none - no SCTE markers in this playlist)"

echo
echo "== base64 sections decoded =="
# Both tag shapes: '#EXT-OATCLS-SCTE35:<b64>' and '...,SCTE35=<b64>'
grep -oE '(EXT-OATCLS-SCTE35:|SCTE35=)[A-Za-z0-9+/=]+' "$playlist" 2>/dev/null \
  | sed -E 's/^(EXT-OATCLS-SCTE35:|SCTE35=)//' | sort -u \
  | while read -r b64; do
      [ -z "$b64" ] && continue
      echo "--- $b64"
      # Show the raw bytes: table_id must be 0xfc, byte 13 is the splice command.
      hex=$(printf '%s' "$b64" | base64 -d 2>/dev/null | xxd -p | tr -d '\n')
      if [ -z "$hex" ]; then echo "    !! not valid base64"; continue; fi
      echo "    bytes      : $(( ${#hex} / 2 ))"
      echo "    table_id   : 0x${hex:0:2} $([ "${hex:0:2}" = fc ] && echo '(splice_info_section)' || echo '(NOT SCTE-35)')"
      cmd=${hex:26:2}
      case "$cmd" in
        00) name=splice_null ;; 04) name=splice_schedule ;; 05) name=splice_insert ;;
        06) name=time_signal ;; 07) name=bandwidth_reservation ;; ff) name=private_command ;;
        *)  name=unknown ;;
      esac
      echo "    command    : 0x$cmd ($name)"
      # threefive gives a full field-level decode when it is available.
      if command -v threefive >/dev/null 2>&1; then
        echo "    --- threefive ---"
        # threefive writes its decode to stderr, not stdout, so 2>/dev/null here
        # silently discards the whole point of calling it. Its optional-dependency
        # notices go to the same stream and are filtered out instead.
        printf '%s\n' "$b64" | threefive 2>&1 \
          | grep -vE '^pip install|^# threefive:' | sed 's/^/    /'
      fi
    done

if [ -n "$pid" ]; then
  echo
  echo "== sections actually on PID $pid in the segments =="
  if ! command -v tsp >/dev/null 2>&1; then
    echo "(tsduck not installed - skipping wire check)"
  else
    seg=$(grep -vE '^#' "$playlist" | head -1)
    [ -n "$seg" ] && tsp -I file "$dir/$seg" -P tables --pid "$pid" -O drop 2>&1 | head -40 \
                  || echo "(no segments listed in playlist yet)"
  fi
fi
