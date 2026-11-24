#!/usr/bin/env python3
"""Decode the grains of an MXL SCTE-35 data flow, independently of the engine's C++ (tests/tests/mxl/programme/07-scte-data).

The flow is read with GStreamer, which gives ST 2038 buffers, one per grain:

    gst-launch-1.0 -v mxlsrc data-flow-id=<id> domain=<dir> ! identity silent=false ! filesink location=anc.bin > gst.log

`identity` prints the size and time of every buffer, which is how the stream is cut back into grains.

    mxl_cues.py anc.bin gst.log     one JSON object per grain: {"n", "pts", "bytes", "cues": [...], "error"}
    mxl_cues.py check grains.jsonl acquired.txt <reader start> <reader stop> [period ns]
                                    checks the decoded flow against the sections the engine read (see check()). acquired.txt has
                                    "HH:MM:SS.mmm <base64 section>" per line, the times from the engine's log; the reader times
                                    are HH:MM:SS.mmm too.

A cue is {"countdown": milliseconds to the splice, "hex": the section, "crc": true when the section's CRC-32 is right}.
The format (see transcoder/lib/utpl/include/utpl/transcoder/mxlcarrier.h): ANC packets of DID 0x58 / SDID 0x01 (placeholders,
not registered), user data 'N' 'S' 2, total, index, countdown (int32, milliseconds), section length (u16), then a piece of the section.
"""
import json
import re
import sys

DID, SDID = 0x58, 0x01


def parity_ok(word):
    b8, b9 = (word >> 8) & 1, (word >> 9) & 1
    return b8 == bin(word & 0xFF).count("1") % 2 and b9 == (0 if b8 else 1)


def crc32_mpeg(data):
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    return crc


class Bits:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def has(self, n):
        return self.pos + n <= len(self.data) * 8

    def get(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.data[self.pos >> 3] >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v

    def align(self):
        self.pos = (self.pos + 7) & ~7


def decode(buf):
    """-> (cues, error). cues: list of {"countdown", "section" (bytes)}."""
    r, cues, error = Bits(buf), [], None
    cur, total, nxt, length, countdown = b"", 0, 0, 0, 0
    while r.has(6 + 1 + 11 + 12 + 30 + 10):
        r.get(6), r.get(1), r.get(11), r.get(12)
        did, sdid, count = r.get(10), r.get(10), r.get(10)
        if not (parity_ok(did) and parity_ok(sdid) and parity_ok(count)):
            return cues, "bad parity in an ANC header"
        n = count & 0xFF
        if not r.has(10 * n + 10):
            return cues, "truncated ANC packet"
        s = (did & 0x1FF) + (sdid & 0x1FF) + (count & 0x1FF)
        udw = []
        for _ in range(n):
            w = r.get(10)
            if not parity_ok(w):
                return cues, "bad parity in a user data word"
            s += w & 0x1FF
            udw.append(w & 0xFF)
        cs = r.get(10)
        s &= 0x1FF
        if cs != (s | (0 if (s >> 8) & 1 else 0x200)):
            return cues, "bad ANC checksum"
        r.align()
        if (did & 0xFF, sdid & 0xFF) != (DID, SDID):
            continue
        if len(udw) < 11 or bytes(udw[:3]) != b"NS\x02":
            return cues, "unknown cue header"
        t, i = udw[3], udw[4]
        c = int.from_bytes(bytes(udw[5:9]), "big", signed=True)
        ln = (udw[9] << 8) | udw[10]
        if i == 0:
            if nxt != total:
                return cues, "cue started inside another"
            cur, total, nxt, length, countdown = b"", t, 0, ln, c
        elif i != nxt or t != total or c != countdown:
            return cues, "fragment out of order"
        cur += bytes(udw[11:])
        nxt = i + 1
        if nxt == total:
            if len(cur) != length:
                return cues, "cue length differs from its header"
            cues.append({"countdown": countdown, "section": cur})
            cur, total, nxt = b"", 0, 0
    if nxt != total:
        error = "buffer ends inside a cue"
    return cues, error


def seconds(text):
    h, m, s = text.split(":")
    return int(h) * 3600 + int(m) * 60 + float(s)


def check(grains_file, acquired_file, start, stop, period):
    """Prints RESULT lines and FAIL lines; returns the number of failures."""
    import base64
    grains = [json.loads(line) for line in open(grains_file) if line.strip()]
    timed = []
    for line in open(acquired_file):
        if line.strip():
            when, b64 = line.split(None, 1)
            timed.append((seconds(when), base64.b64decode(b64.strip()).hex()))
    acquired = [h for _, h in timed]
    fails = []

    def fail(message):
        fails.append(message)
        print("FAIL: " + message)

    if not grains:
        fail("no grain was read from the data flow")
        return len(fails)

    for g in grains:
        if g.get("error"):
            fail("grain %s (pts %s): %s" % (g["n"], g.get("pts"), g["error"]))
            break
    bad_crc = sum(1 for g in grains for c in g["cues"] if not c["crc"])
    if bad_crc:
        fail("%d cue(s) with a wrong CRC-32" % bad_crc)

    pts = [g["pts"] for g in grains]
    holes = sum(1 for a, b in zip(pts, pts[1:]) if b - a != period)
    with_cues = sum(1 for g in grains if g["cues"])
    print("RESULT %d grains read over %.1f s, %d with cues, %d gaps or odd spacings between consecutive grains" % (
        len(grains), (pts[-1] - pts[0]) / 1e9, with_cues, holes))
    if holes:
        fail("%d place(s) where consecutive grains are not %d ns apart (a grain missing from the flow)" % (holes, period))

    seen = {}
    for g in grains:
        for c in g["cues"]:
            seen.setdefault(c["hex"], []).append((g["pts"], c["countdown"]))
    print("RESULT %d distinct cues in the flow; the engine read %d sections in all" % (len(seen), len(acquired)))

    for h in seen:
        if h not in acquired:
            fail("a cue in the flow is not a section the engine read: " + h[:60])

    # What the engine read while the reader was running, leaving the last second out (a cue read just before the reader stopped
    # may not have reached a grain the reader saw).
    t0, t1 = seconds(start), seconds(stop)
    expected = [h for t, h in timed if t0 <= t <= t1 - 1.0]
    missing = [h for h in expected if h not in seen]
    print("RESULT %d of %d sections read by the engine while the reader ran are in the flow" % (len(expected) - len(missing), len(expected)))
    if missing:
        fail("%d section(s) the engine read are not in the flow, first: %s" % (len(missing), missing[0][:60]))
    if len(expected) < 3:
        fail("fewer than 3 sections to check: the source delivered too few while the reader ran")

    # The same section can be sent more than once (a heartbeat, an operator's repeat): a gap of over a second between two
    # appearances starts a new run, which is a new cue with its own splice time.
    repeats = []
    for h, appearances in seen.items():
        appearances.sort()
        runs, run = [], [appearances[0]]
        for a in appearances[1:]:
            if a[0] - run[-1][0] > 1000000000:
                runs.append(run)
                run = []
            run.append(a)
        runs.append(run)
        for run in runs:
            # While it counts down, every copy gives the same splice time. After the splice frame the cue stays a few
            # frames at countdown 0, so those copies are not compared; the first of them must be on the splice frame.
            counting = [(p, c) for p, c in run if c > 0]
            splice = [p + c * 1000000 for p, c in counting]
            if splice and max(splice) - min(splice) > 1000000:
                fail("a cue has splice times %.2f ms apart across its copies (limit 1 ms)" % ((max(splice) - min(splice)) / 1e6))
            elif splice:
                first_zero = [p for p, c in run if c == 0]
                if first_zero and not (min(splice) - 1000000 <= first_zero[0] <= max(splice) + period + 1000000):
                    fail("a cue's first countdown-0 copy is %.0f ms from its splice time" % ((first_zero[0] - min(splice)) / 1e6))
            for (p1, c1), (p2, c2) in zip(run, run[1:]):
                if p2 - p1 == period and c2 != 0 and abs((c1 - c2) - period // 1000000) > 1:
                    fail("a cue's countdown went from %d to %d ms in consecutive grains (%d ms apart)" % (c1, c2, period // 1000000))
                    break
            repeats.append(len(run))
    if repeats:
        print("RESULT each cue is in %d to %d grains (median %d)" % (min(repeats), max(repeats), sorted(repeats)[len(repeats) // 2]))
        if min(repeats) < 2:
            fail("a cue was written in a single grain only")
    return len(fails)


def main():
    if len(sys.argv) >= 6 and sys.argv[1] == "check":
        period = int(sys.argv[6]) if len(sys.argv) > 6 else 40000000
        sys.exit(1 if check(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], period) else 0)
    stream = open(sys.argv[1], "rb").read()
    log = open(sys.argv[2], errors="replace").read()
    # last-message = chain   ******* (identity0:sink) (4096 bytes, dts: none, pts: 0:00:01.234567890, ...
    buffers = re.findall(r"\((\d+) bytes, dts: [^,]*, pts: (\d+):(\d+):(\d+)\.(\d+)", log)
    pos = 0
    for n, (size, h, m, s, ns) in enumerate(buffers):
        size = int(size)
        chunk = stream[pos:pos + size]
        pos += size
        pts = ((int(h) * 60 + int(m)) * 60 + int(s)) * 10**9 + int(ns)
        cues, error = decode(chunk)
        print(json.dumps({
            "n": n, "pts": pts, "bytes": size, "error": error,
            "cues": [{"countdown": c["countdown"], "hex": c["section"].hex(), "crc": crc32_mpeg(c["section"]) == 0} for c in cues],
        }))
    if pos != len(stream):
        print(json.dumps({"n": -1, "error": "the log accounts for %d of %d bytes" % (pos, len(stream)), "cues": []}))


if __name__ == "__main__":
    main()
