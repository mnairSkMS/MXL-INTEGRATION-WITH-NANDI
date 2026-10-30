#!/usr/bin/env python3
"""Analyse captured MPEG-TS files. Runs inside the transcoder image, which ships ffprobe and tsp.

Usage: analyze_capture.py <capture-dir> --fps N [--jobs N]

<capture-dir>/wall.json maps each capture file name to the wall-clock seconds it covered. Files
named seg-*.ts are HLS segments: they have no wall duration and are checked for frame count only.
Writes one JSON document to stdout.
"""
import argparse
import concurrent.futures as cf
import json
import os
import re
import subprocess
import sys

TS_HZ = 90000


def probe(path):
    proc = subprocess.run(
        ["ffprobe", "-v", "error",
         "-show_entries", "stream=index,codec_type,codec_name,width,height:packet=stream_index,pts,duration",
         "-of", "json", path],
        capture_output=True, text=True)
    if proc.returncode != 0:
        return None, proc.stderr.strip()[-500:]
    return json.loads(proc.stdout or "{}"), None


def tsduck(path):
    proc = subprocess.run(["tsp", "-I", "file", path, "-P", "continuity", "-P", "pcrverify", "-O", "drop"],
                          capture_output=True, text=True)
    lines = (proc.stderr or "").splitlines()
    cc = [l for l in lines if "continuity:" in l and "missing" in l.lower()]
    pcr_summary = [l for l in lines if "pcrverify:" in l]
    pcr_errors = 0
    for line in pcr_summary:
        match = re.search(r"([\d,]+)\s+with\s+jitter", line)
        if match:
            pcr_errors += int(match.group(1).replace(",", ""))
    return {"cc_errors": len(cc), "pcr_jitter_errors": pcr_errors,
            "tsduck_tail": [l for l in lines if "pcrverify:" in l or "continuity:" in l][-6:]}


def series(packets, index):
    pts, durs = [], []
    for pkt in packets:
        if pkt.get("stream_index") != index or pkt.get("pts") is None:
            continue
        pts.append(int(pkt["pts"]))
        if pkt.get("duration") is not None:
            durs.append(int(pkt["duration"]))
    return sorted(pts), durs


def gaps(pts, nominal):
    missing = dups = 0
    for a, b in zip(pts, pts[1:]):
        delta = b - a
        if delta == 0:
            dups += 1
        elif delta > 1.5 * nominal:
            missing += round(delta / nominal) - 1
    return missing, dups


def analyse(path, wall, fps):
    name = os.path.basename(path)
    result = {"file": name, "bytes": os.path.getsize(path), "wall_s": wall}
    data, err = probe(path)
    if data is None:
        result["error"] = f"ffprobe failed: {err}"
        return result
    streams = {s["index"]: s for s in data.get("streams", [])}
    packets = data.get("packets", [])
    video = next((s for s in streams.values() if s.get("codec_type") == "video"), None)
    audio = next((s for s in streams.values() if s.get("codec_type") == "audio"), None)
    if video is None:
        result["error"] = "no video stream"
        return result
    vpts, _ = series(packets, video["index"])
    nominal = TS_HZ / fps
    result.update({
        "video_codec": video.get("codec_name"), "width": video.get("width"), "height": video.get("height"),
        "video_frames": len(vpts),
    })
    if vpts:
        span = (vpts[-1] - vpts[0]) / TS_HZ + 1 / fps
        missing, dups = gaps(vpts, nominal)
        result.update({"video_span_s": round(span, 3), "missing_frames": missing, "duplicate_frames": dups})
        if wall:
            result["video_fps_wall"] = round(len(vpts) / wall, 3)
            result["video_media_rate"] = round(span / wall, 4)
    if audio is not None:
        apts, adurs = series(packets, audio["index"])
        result["audio_codec"] = audio.get("codec_name")
        result["audio_packets"] = len(apts)
        if len(apts) > 1:
            adur = sorted(adurs)[len(adurs) // 2] if adurs else min(b - a for a, b in zip(apts, apts[1:]) if b > a)
            aspan = (apts[-1] - apts[0] + adur) / TS_HZ
            amissing, _ = gaps(apts, adur)
            result.update({"audio_span_s": round(aspan, 3), "audio_gaps": amissing,
                           "audio_gap_ms": round(amissing * adur / TS_HZ * 1000, 1)})
            if wall:
                result["audio_media_rate"] = round(aspan / wall, 4)
    if wall:
        result["bitrate_kbps"] = round(result["bytes"] * 8 / wall / 1000, 1)
    result.update(tsduck(path))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("capdir")
    parser.add_argument("--fps", type=float, required=True)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    walls = {}
    wall_file = os.path.join(args.capdir, "wall.json")
    if os.path.exists(wall_file):
        with open(wall_file) as handle:
            walls = json.load(handle)
    files = sorted(f for f in os.listdir(args.capdir) if f.endswith(".ts"))
    with cf.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [pool.submit(analyse, os.path.join(args.capdir, f), walls.get(f), args.fps) for f in files]
        results = [f.result() for f in futures]
    json.dump(results, sys.stdout)


if __name__ == "__main__":
    main()
