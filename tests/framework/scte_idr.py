#!/usr/bin/env python3
"""Whether the video has a keyframe where each splice_insert of a transport stream says it splices.

    scte_idr.py <file.ts> [pid]

For every event id on the PID (scte_events.py: splice time = pts_time + pts_adjustment) it prints
    IDR <event id> <ms from the splice to the nearest video keyframe, negative = the keyframe is before the splice>
and a last line
    IDR-SUMMARY <events> <events with a keyframe within one frame (45 ms)>
Times are the file's own PTS, so the two sides of the comparison are on one clock. Needs ffprobe and tstables.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    ts = sys.argv[1]
    pid = sys.argv[2] if len(sys.argv) > 2 else "500"
    out = subprocess.run([sys.executable, "-I", os.path.join(HERE, "scte_events.py"), ts, pid], capture_output=True, text=True).stdout
    events = {int(l.split()[0]): int(l.split()[3]) for l in out.splitlines() if l.strip()}
    probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "packet=pts,flags", "-of", "csv=p=0", ts],
                           capture_output=True, text=True).stdout
    keys = []
    for line in probe.splitlines():
        parts = line.split(",")
        if len(parts) >= 2 and parts[0].isdigit() and "K" in parts[1]:
            # ffprobe prints the pts in the stream's time base (1/90000 for MPEG-TS)
            keys.append(int(parts[0]))
    good = 0
    for event, splice in sorted(events.items()):
        if not keys:
            print("IDR", event, "none")
            continue
        nearest = min(keys, key=lambda k: abs(k - splice))
        ms = (nearest - splice) / 90.0
        good += abs(ms) <= 45.0
        print("IDR", event, "%.1f" % ms)
    print("IDR-SUMMARY", len(events), good)


if __name__ == "__main__":
    main()
