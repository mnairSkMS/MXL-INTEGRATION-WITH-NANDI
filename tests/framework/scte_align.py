#!/usr/bin/env python3
"""Where each splice_insert falls against the picture, in the source and in two outputs of the same programme.

    scte_align.py <source.ts> <in-process.ts> <through-mxl.ts> [tolerance ms]

The source is a black picture that flashes white for 2 frames at the start of every second ("scte-sync"). For each event id on PID 500 of
each file, the offset is the splice time minus the time of the latest flash frame at or before it (both in the file's own PTS, so each
file's own timeline is used), modulo one second, because a flash can be missed by a capture. The source's offset is the truth: where its
author put the splice against the picture. The file read through MXL must be within the tolerance of it (default 45 ms: one frame at
25 fps, which is the Gate A limit, and 5 ms for the conversion of times). The in-process output is reported for information: the
muxer places a splice by the source's own times, while the picture follows the engine's clock, so on a source whose clock differs from the
engine's the two drift apart (seen: 80 ms in 24 s). Prints RESULT lines and FAIL lines; exits 1 on a failure.
"""
import re
import subprocess
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def events(ts):
    out = subprocess.run([sys.executable, "-I", os.path.join(HERE, "scte_events.py"), ts], capture_output=True, text=True).stdout
    return {int(l.split()[0]): int(l.split()[3]) for l in out.splitlines() if l.strip()}


def flashes(ts):
    """PTS (90 kHz ticks) of the first frame of every flash: a frame bright after a dark one."""
    out = subprocess.run(["ffmpeg", "-v", "error", "-copyts", "-i", ts, "-an", "-vf", "scale=32:18,signalstats,metadata=print:file=-", "-f", "null", "-"],
                         capture_output=True, text=True).stdout
    frames, pts = [], None
    for line in out.splitlines():
        m = re.search(r"frame:\d+\s+pts:(\d+)", line)
        if m:
            pts = int(m.group(1))
        m = re.search(r"lavfi\.signalstats\.YAVG=([0-9.]+)", line)
        if m and pts is not None:
            frames.append((pts, float(m.group(1))))
    starts, previous_bright = [], False
    for p, y in frames:
        bright = y > 120
        if bright and not previous_bright:
            starts.append(p)
        previous_bright = bright
    return starts


def offsets(ts):
    f = flashes(ts)
    result = {}
    for event, splice in events(ts).items():
        before = [p for p in f if p <= splice]
        if before:
            result[event] = (splice - before[-1]) / 90.0   # ms
    return result, len(f)


def main():
    tolerance = float(sys.argv[4]) if len(sys.argv) > 4 else 45.0
    (truth, ft), (inproc, fa), (mxl, fb) = offsets(sys.argv[1]), offsets(sys.argv[2]), offsets(sys.argv[3])
    print("RESULT flashes found: source %d, in-process %d, through MXL %d" % (ft, fa, fb))
    if not truth:
        print("FAIL: no event found in the source")
        sys.exit(1)

    def wrap(x):
        return (x + 500) % 1000 - 500

    values = sorted(v % 1000 for v in truth.values())
    reference = values[len(values) // 2]
    print("RESULT the source puts its %d events %.1f ms after a flash (the largest spread between them is %.1f ms)" % (len(truth), reference, values[-1] - values[0]))

    fails, worst, worst_inproc, compared = 0, 0.0, 0.0, 0
    for e in sorted(mxl):
        d = wrap(mxl[e] - reference)
        worst = max(worst, abs(d))
        compared += 1
        line = "RESULT event %d: through MXL %+6.1f ms from the source's placement" % (e, d)
        if e in inproc:
            di = wrap(inproc[e] - reference)
            worst_inproc = max(worst_inproc, abs(di))
            line += "; in-process %+6.1f ms" % di
        print(line)
        if abs(d) > tolerance:
            fails += 1
    print("RESULT %d events; through MXL the largest distance from the source's placement is %.1f ms (limit %.0f ms); in-process %.1f ms" % (compared, worst, tolerance, worst_inproc))
    if compared < 5:
        print("FAIL: only %d events to compare" % compared)
        fails += 1
    if fails:
        print("FAIL: %d event(s) are more than %.0f ms from where the source put them" % (fails, tolerance))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
