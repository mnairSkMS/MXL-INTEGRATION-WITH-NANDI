#!/usr/bin/env bash
# Build benchmark programme content into assets/media/queue using ffmpeg inside the transcoder image.
#
#   make-media.sh [real] [real4k] [synthetic]     (default: real synthetic)
#
# real       Xiph derf 1080p50 high-motion sequences (crowd_run, park_joy, ducks_take_off), streamed
#            from media.xiph.org (about 4.6 GB downloaded, nothing raw is stored) and encoded to
#            xiph-1080p50.mp4, xiph-1080p25.mp4 and xiph-1080p25-51.mp4 (5.1 audio). Use these for the spec.
# real4k     Xiph crowd_run_2160p50 (6.2 GB streamed) -> xiph-2160p25.mp4 for P3d/P3e.
# synthetic  testsrc2 + temporal noise, no download: stress-1080p25/1080p50/2160p25/1080p25-51.mp4.
#            A deliberately hard fallback when the Xiph server is unreachable; not real footage.
#
# Env: BENCH_IMAGE (default from ../image.txt), XIPH_MAX_FRAMES (truncate each sequence, for smoke tests).
set -euo pipefail
KIT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$KIT/assets/media/queue"
IMAGE="${BENCH_IMAGE:-$(cut -d' ' -f1 "$KIT/image.txt" 2>/dev/null || true)}"
[ -n "$IMAGE" ] || { echo "set BENCH_IMAGE or run setup/load-image.sh first" >&2; exit 1; }
XIPH="https://media.xiph.org/video/derf/y4m"
MAXF="${XIPH_MAX_FRAMES:-}"
TMP="$OUT/.tmp-media"
mkdir -p "$OUT" "$TMP"

ff() { docker run --rm -i --network none --user "$(id -u):$(id -g)" -v "$OUT:/out" --entrypoint ffmpeg "$IMAGE" -nostdin -hide_banner -loglevel error -y "$@"; }
MEZZ="-c:v libx264 -preset medium -crf 14 -pix_fmt yuv420p -profile:v high"
STEREO="-c:a aac -b:a 192k -ar 48000 -ac 2"

fetch_encode() { # name rate out.mp4 [extra vf]
  local name="$1" rate="$2" out="$3" vf="${4:-null}"
  local frames=(); [ -n "$MAXF" ] && frames=(-frames:v "$MAXF")
  echo "  streaming $name.y4m -> $out"
  curl -fsSL --retry 3 "$XIPH/$name.y4m" | ff -f yuv4mpegpipe -i pipe:0 "${frames[@]}" -vf "$vf" -r "$rate" $MEZZ -g "$rate" -an "/out/.tmp-media/$out" || {
    # curl exits with SIGPIPE when XIPH_MAX_FRAMES stops ffmpeg early; the output is still valid.
    [ -s "$TMP/$out" ] || { echo "download/encode of $name failed" >&2; return 1; }
  }
}

concat_with_audio() { # out.mp4 layout(stereo|5.1) parts...
  local out="$1" layout="$2"; shift 2
  local list="$TMP/${out}.txt"; : > "$list"
  for p in "$@"; do echo "file '/out/.tmp-media/$p'" >> "$list"; done
  local audio=(-f lavfi -i "sine=frequency=440:sample_rate=48000") amap=(-c:a aac -b:a 192k -ac 2)
  if [ "$layout" = "5.1" ]; then
    audio=(-f lavfi -i "aevalsrc=sin(440*2*PI*t)|sin(550*2*PI*t)|sin(660*2*PI*t)|0.1*sin(60*2*PI*t)|sin(770*2*PI*t)|sin(880*2*PI*t):s=48000:c=5.1")
    amap=(-c:a aac -b:a 384k)
  fi
  ff -f concat -safe 0 -i "/out/.tmp-media/${out}.txt" "${audio[@]}" -map 0:v -map 1:a -c:v copy "${amap[@]}" -shortest -movflags +faststart "/out/$out"
  echo "  wrote $OUT/$out"
}

real() {
  echo "real 1080p content (Xiph derf)"
  local seqs=(crowd_run_1080p50 park_joy_1080p50 ducks_take_off_1080p50) p50=() p25=()
  for s in "${seqs[@]}"; do
    fetch_encode "$s" 50 "$s-50.mp4"; p50+=("$s-50.mp4")
    ff -i "/out/.tmp-media/$s-50.mp4" -vf fps=25 $MEZZ -g 25 -an "/out/.tmp-media/$s-25.mp4"; p25+=("$s-25.mp4")
  done
  concat_with_audio xiph-1080p50.mp4 stereo "${p50[@]}"
  concat_with_audio xiph-1080p25.mp4 stereo "${p25[@]}"
  concat_with_audio xiph-1080p25-51.mp4 5.1 "${p25[@]}"
}

real4k() {
  echo "real 2160p content (Xiph derf crowd_run_2160p50, 6.2 GB stream)"
  fetch_encode crowd_run_2160p50 25 crowd_run_2160p25.mp4 "fps=25"
  concat_with_audio xiph-2160p25.mp4 stereo crowd_run_2160p25.mp4
}

synthetic() {
  echo "synthetic stress content"
  local src="testsrc2=size=%s:rate=%s,noise=alls=12:allf=t+u"
  # shellcheck disable=SC2059
  ff -f lavfi -i "$(printf "$src" 1920x1080 25)" -f lavfi -i "sine=frequency=440:sample_rate=48000" -t 60 $MEZZ -g 25 $STEREO /out/stress-1080p25.mp4
  # shellcheck disable=SC2059
  ff -f lavfi -i "$(printf "$src" 1920x1080 50)" -f lavfi -i "sine=frequency=440:sample_rate=48000" -t 60 $MEZZ -g 50 $STEREO /out/stress-1080p50.mp4
  # shellcheck disable=SC2059
  ff -f lavfi -i "$(printf "$src" 3840x2160 25)" -f lavfi -i "sine=frequency=440:sample_rate=48000" -t 30 $MEZZ -g 25 $STEREO /out/stress-2160p25.mp4
  ff -i /out/stress-1080p25.mp4 -f lavfi -i "aevalsrc=sin(440*2*PI*t)|sin(550*2*PI*t)|sin(660*2*PI*t)|0.1*sin(60*2*PI*t)|sin(770*2*PI*t)|sin(880*2*PI*t):s=48000:c=5.1" \
     -map 0:v -map 1:a -c:v copy -c:a aac -b:a 384k -shortest /out/stress-1080p25-51.mp4
  echo "  wrote stress-*.mp4 to $OUT"
}

targets=("$@"); [ ${#targets[@]} -eq 0 ] && targets=(real synthetic)
for t in "${targets[@]}"; do
  case "$t" in real|real4k|synthetic) "$t" ;; *) echo "unknown target $t" >&2; exit 1 ;; esac
done
rm -rf "$TMP"
ls -lh "$OUT"
