#!/usr/bin/env python3
"""Helpers of the Gate A run of a split channel (tests/manual/mxl/worker-gate-a.sh).

    gate_a.py relay   <listen port> <out.ts> <seconds> <port> [<port> ...]   copy a UDP stream to several ports and to a file
    gate_a.py capture <port> <out.ts> <seconds>                              record a UDP stream with the arrival time of every datagram
    gate_a.py cues    <source.ts> <splice udp port> <count> <interval s> <log>
                                                                             send splice_insert cues for known source pictures
    gate_a.py analyse <dir> <delay ms> <name> [<name> ...]                   judge <dir>/<name>.ts against <dir>/source.ts and p.ts

The source picture carries its own frame number (16 blocks across the top) and flashes its lower part for 2 frames each second, with a beep.
So every output picture says which source picture it is: a lost or repeated one is seen directly, and so is the picture a splice lands on.
"""
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time

TICKS = 90000
FRAME = 3600            # 25 fps in 90 kHz ticks


def udp(port):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
    s.bind(("127.0.0.1", port))
    s.settimeout(0.5)
    return s


def record(listen, path, seconds, forward):
    s, out = udp(listen), open(path, "wb")
    idx = open(path + ".idx", "w")
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    offset, end = 0, None
    deadline = time.monotonic() + seconds + 60          # nothing at all arriving must not wait for ever
    while time.monotonic() < (end or deadline):
        try:
            data = s.recv(65536)
        except socket.timeout:
            continue
        now = time.monotonic()
        if end is None:
            end = now + seconds
        for p in forward:
            tx.sendto(data, ("127.0.0.1", p))
        out.write(data)
        out.flush()
        idx.write("%d %.6f\n" % (offset, now))
        offset += len(data)


def pes_pts(packet):
    """(pid, pts) of a TS packet that starts a video PES, else None."""
    if len(packet) != 188 or packet[0] != 0x47 or not packet[1] & 0x40:
        return None
    pid = ((packet[1] & 0x1F) << 8) | packet[2]
    afc = (packet[3] >> 4) & 3
    if not afc & 1:
        return None
    at = 4 + (1 + packet[4] if afc & 2 else 0)
    p = packet[at:at + 14]
    if len(p) < 14 or p[0:3] != b"\x00\x00\x01" or not 0xE0 <= p[3] <= 0xEF or not p[7] & 0x80:
        return None
    b = p[9:14]
    return pid, ((b[0] >> 1) & 7) << 30 | b[1] << 22 | (b[2] >> 1) << 15 | b[3] << 7 | b[4] >> 1


def pts_offsets(path):
    """[(pts, byte offset)] of every video PES start."""
    out, offset = [], 0
    with open(path, "rb") as f:
        while True:
            chunk = f.read(188 * 4096)
            if not chunk:
                return out
            for i in range(0, len(chunk) - 187, 188):
                r = pes_pts(chunk[i:i + 188])
                if r:
                    out.append((r[1], offset + i))
            offset += len(chunk)


def last_pts(path):
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        f.seek(max(0, (size // 188 - 3000) * 188))
        chunk = f.read()
    best = None
    for i in range(0, len(chunk) - 187, 188):
        r = pes_pts(chunk[i:i + 188])
        if r:
            best = r[1] if best is None else max(best, r[1])
    return best


def cues(source, port, count, interval, log):
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    out = open(log, "w")
    for k in range(count):
        time.sleep(interval)
        now = last_pts(source)
        if now is None:
            continue
        splice = now + 150 * FRAME          # a picture of the source, 6 s ahead
        event = 1000 + k
        xml = ('<?xml version="1.0" encoding="UTF-8"?><tsduck><splice_information_table>'
               '<splice_insert splice_event_id="%d" unique_program_id="1" out_of_network="%s" pts_time="%d"/>'
               '</splice_information_table></tsduck>') % (event, "true" if k % 2 == 0 else "false", splice)
        tx.sendto(xml.encode(), ("127.0.0.1", port))
        out.write("%d %d\n" % (event, splice))
        out.flush()


def pictures(path):
    """[(pts, frame number or None, flash luma)] of every decoded picture, in presentation order."""
    graph = ("[0:v]showinfo=checksum=0,split[a][b];[a]crop=iw:trunc(ih*20/108):0:0,scale=16:1:flags=area,format=gray[c];"
             "[b]crop=iw:trunc(ih/12):0:ih-trunc(ih/12),scale=1:1:flags=area,format=gray[d];[c][d]hstack[o]")
    err = open(path + ".showinfo", "w+")
    p = subprocess.Popen(["ffmpeg", "-hide_banner", "-nostats", "-copyts", "-i", path, "-an", "-filter_complex", graph, "-map", "[o]",
                          "-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "gray", "-"], stdout=subprocess.PIPE, stderr=err)
    raw = p.stdout.read()
    p.wait()
    err.seek(0)
    pts = [int(m.group(1)) for m in (re.search(r"\] n: *\d+ pts: *(\d+) ", line) for line in err) if m]
    err.close()
    os.remove(path + ".showinfo")
    result = []
    for i in range(min(len(pts), len(raw) // 17)):
        row = raw[i * 17:i * 17 + 17]
        clean = all(v < 80 or v > 170 for v in row[:16])
        number = sum(1 << b for b in range(16) if row[b] > 128) if clean else None
        result.append((pts[i], number, row[16]))
    # the number is 16 bits: carry it on past 65535
    wraps, previous, unwrapped = 0, None, []
    for t, n, y in result:
        if n is not None:
            if previous is not None and n < previous - 32768:
                wraps += 1
            previous = n
            n += wraps * 65536
        unwrapped.append((t, n, y))
    return unwrapped


def continuity(frames, skip):
    """Lost and repeated source pictures after the first `skip` output frames."""
    lost = repeated = unreadable = back = 0
    previous = None
    for _, n, _ in frames[skip:]:
        if n is None:
            unreadable += 1
            continue
        if previous is not None:
            if n == previous:
                repeated += 1
            elif n > previous + 1:
                lost += n - previous - 1
            elif n < previous:
                back += 1
        previous = n
    gaps = sum(1 for (a, _, _), (b, _, _) in zip(frames[skip:], frames[skip + 1:]) if b - a > FRAME * 3 // 2)
    return {"frames": len(frames) - skip, "lost": lost, "repeated": repeated, "backwards": back, "unreadable": unreadable, "pts_gaps": gaps}


def beeps(path):
    log = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-copyts", "-i", path, "-vn", "-af", "silencedetect=noise=-35dB:d=0.3", "-f", "null", "-"],
                         capture_output=True, text=True).stderr
    return [float(m) for m in re.findall(r"silence_end: ([0-9.]+)", log)]


def lipsync(path, frames):
    """[(time s, flash minus beep ms)] for every flash with a beep within 0.45 s."""
    flashes, bright = [], False
    for t, _, y in frames:
        if y > 120 and not bright:
            flashes.append(t / TICKS)
        bright = y > 120
    b, pairs, j = beeps(path), [], 0
    for f in flashes:
        while j + 1 < len(b) and abs(b[j + 1] - f) <= abs(b[j] - f):
            j += 1
        if b and abs(b[j] - f) < 0.45:
            pairs.append((f, (f - b[j]) * 1000))
    return pairs


def splices(path):
    out = subprocess.run([sys.executable, "-I", os.path.join(os.path.dirname(os.path.abspath(__file__)), "scte_events.py"), path],
                         capture_output=True, text=True).stdout
    return {int(l.split()[0]): int(l.split()[3]) for l in out.splitlines() if l.strip()}


def median(values):
    v = sorted(values)
    return v[len(v) // 2] if v else None


def quantile(values, q):
    v = sorted(values)
    return v[min(len(v) - 1, int(q * len(v)))] if v else None


def arrivals(path, frames):
    """{source frame number: arrival time of the first output picture showing it}"""
    index = [line.split() for line in open(path + ".idx")]
    offsets = [int(a) for a, _ in index]
    times = [float(b) for _, b in index]
    import bisect
    when = {}
    for pts, offset in pts_offsets(path):
        when.setdefault(pts, times[bisect.bisect_right(offsets, offset) - 1])
    result = {}
    for t, n, _ in frames:
        if n is not None and t in when:
            result.setdefault(n, when[t])
    return result


def analyse(directory, delay_ms, names):
    path = lambda n: os.path.join(directory, n + ".ts")
    source = pictures(path("source"))
    number_at = {t: n for t, n, _ in source if n is not None}
    authored = {}
    for line in open(os.path.join(directory, "cues.log")):
        event, pts = map(int, line.split())
        authored[event] = number_at.get(pts)
    everything = {"source": continuity(source, 25)}
    data = {}
    for name in ["p"] + names:
        frames = pictures(path(name))
        data[name] = (frames, arrivals(path(name), frames), lipsync(path(name), frames), splices(path(name)))
    failed = []
    print("RESULT source: %s" % json.dumps(everything["source"]))
    p_frames, p_arr, p_lip, p_cues = data["p"]
    for name in ["p"] + names:
        frames, arr, lip, found = data[name]
        c = continuity(frames, 250)                 # the first 10 s of a capture are start-up
        print("RESULT %s pictures: %s" % (name, json.dumps(c)))
        offsets = [o for _, o in lip[10:]]
        minutes = {}
        for t, o in lip[10:]:
            minutes.setdefault(int((t - lip[10][0]) // 60), []).append(o)
        per_minute = [median(v) for _, v in sorted(minutes.items()) if len(v) >= 10]
        print("RESULT %s flash minus beep: median %.0f ms, per minute %.0f to %.0f ms, %d pairs" % (name, median(offsets), min(per_minute), max(per_minute), len(offsets)))
        errors, missing = [], []
        for event, intended in sorted(authored.items()):
            if intended is None:
                continue
            if event not in found:
                missing.append(event)
                continue
            splice = found[event]
            before = [n for t, n, _ in frames if n is not None and splice - 10 * FRAME < t <= splice]
            if before:
                errors.append((event, before[-1] - intended))
            else:
                missing.append(event)
        within = sum(1 for _, e in errors if abs(e) <= 1)
        print("RESULT %s cues: %d sent, %d found, %d within one picture of the authored one; pictures off: %s; missing: %s"
              % (name, len(authored), len(errors), within, " ".join("%+d" % e for _, e in errors), missing or "none"))
        if name == "p":
            continue
        if c["lost"] or c["pts_gaps"] or c["backwards"]:
            failed.append("%s: lost %d pictures, %d gaps, %d steps back (Gate A: 0 drops)" % (name, c["lost"], c["pts_gaps"], c["backwards"]))
        diff = median(offsets) - median([o for _, o in p_lip[10:]])
        print("RESULT %s lip-sync against in-process: %+.0f ms" % (name, diff))
        if abs(diff) > 40:
            failed.append("%s: lip-sync %+.0f ms from in-process (limit 40 ms)" % (name, diff))
        added = [(arr[n] - p_arr[n]) * 1000 for n in arr if n in p_arr and n > min(arr) + 250]
        print("RESULT %s arrival against in-process, same source picture: median %+.0f ms, 99%% %+.0f ms, largest %+.0f ms (%d pictures)"
              % (name, median(added), quantile(added, 0.99), max(added), len(added)))
        if quantile(added, 0.99) > delay_ms + 80:
            failed.append("%s: added latency %.0f ms (limit %d ms)" % (name, quantile(added, 0.99), delay_ms + 80))
        if missing or within < len(errors):
            failed.append("%s: %d of %d cues within one picture, %d missing" % (name, within, len(authored), len(missing)))
    for f in failed:
        print("FAIL: " + f)
    return 1 if failed else 0


if __name__ == "__main__":
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "relay":
        record(int(args[0]), args[1], float(args[2]), [int(p) for p in args[3:]])
    elif cmd == "capture":
        record(int(args[0]), args[1], float(args[2]), [])
    elif cmd == "cues":
        cues(args[0], int(args[1]), int(args[2]), float(args[3]), args[4])
    elif cmd == "analyse":
        sys.exit(analyse(args[0], int(args[1]), args[2:]))
    else:
        sys.exit("unknown command " + cmd)
