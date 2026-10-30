#!/usr/bin/env bash
# Generate, sign and inspect transcoder licenses using the utpl/license-toolkit image.
#
#   make-license.sh [options]            create a license
#   make-license.sh info <file.lic>      show license details
#
# Options:
#   -d DURATION   1h, 30m, 90s, 7d (default: 1h)
#   -e ENV        dev | prod (default: dev)
#   -n NAME       license name (default: "<duration> license")
#   -c SESSIONS   max concurrent sessions (default: 1)
#   -s START      UTC start, e.g. 2026-09-16T08:00:00 (default: now)
#   -O            offline license (default: online)
#   -o FILE       output .lic path (default: ./licenses/<name>.lic)

set -euo pipefail

IMAGE="${LICENSE_TOOLKIT_IMAGE:-utpl/license-toolkit:latest}"
LICENSE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() { sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

run_toolkit() {
    local keys="$1" outdir="$2"; shift 2
    docker run --rm -u "$(id -u):$(id -g)" \
        -v "$keys:/keys:ro" -v "$outdir:/out" \
        "$IMAGE" bash -c "$*"
}

key_dir() {
    local dir="$LICENSE_DIR/$1/keys"
    [[ -d "$dir" ]] || { echo "Unknown env '$1' (expected dev or prod)" >&2; exit 1; }
    echo "$dir"
}

info_cmd='licenseInfo -l /keys/license_encryption_key.bin -p /keys/license_ed25519_public_key.der -i "/out/$LIC"'

if [[ "${1:-}" == "info" ]]; then
    [[ -n "${2:-}" ]] || usage 1
    env="${3:-dev}"
    file="$(realpath "$2")"
    run_toolkit "$(key_dir "$env")" "$(dirname "$file")" "LIC='$(basename "$file")'; $info_cmd"
    exit
fi

duration="1h"; env="dev"; name=""; sessions=1; start=""; offline=false; out=""

while getopts "d:e:n:c:s:Oo:h" opt; do
    case "$opt" in
        d) duration="$OPTARG" ;;
        e) env="$OPTARG" ;;
        n) name="$OPTARG" ;;
        c) sessions="$OPTARG" ;;
        s) start="$OPTARG" ;;
        O) offline=true ;;
        o) out="$OPTARG" ;;
        h) usage ;;
        *) usage 1 ;;
    esac
done

if [[ ! "$duration" =~ ^([0-9]+)([smhd]?)$ ]]; then
    echo "Invalid duration '$duration' (use e.g. 90s, 30m, 1h, 7d)" >&2
    exit 1
fi
case "${BASH_REMATCH[2]}" in
    s|"") seconds=$(( BASH_REMATCH[1] )) ;;
    m)    seconds=$(( BASH_REMATCH[1] * 60 )) ;;
    h)    seconds=$(( BASH_REMATCH[1] * 3600 )) ;;
    d)    seconds=$(( BASH_REMATCH[1] * 86400 )) ;;
esac

keys="$(key_dir "$env")"
start="${start:-$(date -u +%Y-%m-%dT%H:%M:%S)}"
name="${name:-$duration license}"
out="${out:-licenses/${name// /_}.lic}"

mkdir -p "$(dirname "$out")"
outdir="$(realpath "$(dirname "$out")")"
lic="$(basename "$out")"
lsr="${lic%.lic}.lsr"

run_toolkit "$keys" "$outdir" "
    set -e
    LIC='$lic'
    licenseGenerator -l /keys/license_encryption_key.bin -s '$start' -T $seconds \
        -O $offline -c $sessions -n '$name' -o '/out/$lsr' >/dev/null
    licenseSigner -p /keys/license_ed25519_private_key.der -i '/out/$lsr' -o \"/out/\$LIC\" >/dev/null
    rm -f '/out/$lsr'
    $info_cmd
"

echo
echo "Created $outdir/$lic ($env keys, times in UTC)"
