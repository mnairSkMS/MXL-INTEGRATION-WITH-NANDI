#!/usr/bin/env python3
"""The distinct SCTE-35 command sections on one PID of a transport stream.

    scte_sections.py compare <a.ts> <b.ts> <source-picture offset limit> <event> [<event> ...]

splice_null and other commands are left out. Two sections are "the same cue" when their bytes agree with pts_adjustment and CRC_32
blanked, which is what a muxer is allowed to change. One more difference is allowed in <b>, and reported: a pts_time of exactly 0.
The reader of an MXL data flow sets it so (scheduler::withSpliceTimeZeroed, against a mpegtsmux bug), which leaves pts_time plus
pts_adjustment, the splice time a receiver reads, unchanged. The picture the splice lands on is read from the picture itself (the
frame number of the barcode source, see gate_a.py).
<event> is what must be in both streams: "insert:<id>:<out|in>" or "signal:<segmentation event id>". Needs TSDuck and ffmpeg.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gate_a  # noqa: E402

NAMES = {5: "insert", 6: "signal"}


def parse(section):
    """(key, splice ticks or None, blanked bytes, offset of splice_time or None) of a splice_insert or time_signal, else None."""
    command = section[13]
    if command not in NAMES:
        return None
    adjustment = (section[4] & 1) << 32 | int.from_bytes(section[5:9], "big")
    at, pts, key, field = 14, None, None, None
    if command == 5:
        event = int.from_bytes(section[at:at + 4], "big")
        cancelled = section[at + 4] & 0x80
        at += 5
        key = "insert:%d:cancel" % event
        if not cancelled:
            flags = section[at]
            at += 1
            key = "insert:%d:%s" % (event, "out" if flags & 0x80 else "in")
            program, immediate = flags & 0x40, flags & 0x10
            if program and not immediate and section[at] & 0x80:
                field = at
                pts = (section[at] & 1) << 32 | int.from_bytes(section[at + 1:at + 5], "big")
    else:
        if section[at] & 0x80:
            field = at
            pts = (section[at] & 1) << 32 | int.from_bytes(section[at + 1:at + 5], "big")
        # the segmentation_descriptor's event id: descriptor tag 2, "CUEI", then the id
        found = section.find(b"CUEI")
        key = "signal:%d" % int.from_bytes(section[found + 4:found + 8], "big") if found > 0 else "signal:none"
    blanked = bytearray(section[:-4])
    blanked[4] &= 0xFE
    blanked[5:9] = b"\x00\x00\x00\x00"
    splice = None if pts is None else (pts + adjustment) % (1 << 33)
    return key, splice, bytes(blanked), field


def sections(ts):
    """{key: (splice ticks, blanked bytes, splice_time offset)} of the first copy of every command section on PID 500."""
    out = ts + ".sections"
    if os.path.exists(out):
        os.remove(out)
    subprocess.run(["tstables", "--pid", "500", "--tid", "0xFC", "--binary-output", out, ts], capture_output=True, check=False)
    data = open(out, "rb").read() if os.path.exists(out) else b""
    result, at = {}, 0
    while at + 3 <= len(data):
        length = 3 + ((data[at + 1] & 0x0F) << 8 | data[at + 2])
        parsed = parse(data[at:at + length])
        if parsed:
            result.setdefault(parsed[0], parsed[1:])
        at += length
    return result


def same_but_zeroed_time(a, b, field):
    """True when b is a with the 33-bit pts_time at <field> set to 0 and nothing else changed."""
    if field is None or len(a) != len(b):
        return False
    zeroed = bytearray(a)
    zeroed[field] &= 0xFE
    zeroed[field + 1:field + 5] = b"\x00\x00\x00\x00"
    return bytes(zeroed) == b


def picture_at(frames, splice):
    before = [n for t, n, _ in frames if n is not None and splice - 10 * gate_a.FRAME < t <= splice]
    return before[-1] if before else None


def compare(a_path, b_path, limit, wanted):
    a, b = sections(a_path), sections(b_path)
    a_frames, b_frames = gate_a.pictures(a_path), gate_a.pictures(b_path)
    failed = []
    print("RESULT sections in the main engine's output: %s" % (" ".join(sorted(a)) or "none"))
    print("RESULT sections in the worker's output:      %s" % (" ".join(sorted(b)) or "none"))
    for key in wanted:
        if key not in a or key not in b:
            failed.append("%s is missing from %s" % (key, " and ".join(n for n, s in (("the main output", a), ("the worker's output", b)) if key not in s)))
            continue
        (a_splice, a_bytes, a_field), (b_splice, b_bytes, _) = a[key], b[key]
        if a_bytes == b_bytes:
            same = "identical"
        elif same_but_zeroed_time(a_bytes, b_bytes, a_field):
            same = "identical apart from pts_time, 0 in the worker's"
        else:
            same = "DIFFERENT"
        pa = None if a_splice is None else picture_at(a_frames, a_splice)
        pb = None if b_splice is None else picture_at(b_frames, b_splice)
        off = None if pa is None or pb is None else pb - pa
        print("RESULT %s: bytes %s; splice on source picture %s (main), %s (worker), difference %s"
              % (key, same, pa, pb, off))
        if same == "DIFFERENT":
            print("RESULT %s main:   %s" % (key, a_bytes.hex()))
            print("RESULT %s worker: %s" % (key, b_bytes.hex()))
            failed.append("%s: the section's bytes differ" % key)
        if off is None:
            failed.append("%s: the picture at the splice was not found" % key)
        elif abs(off) > limit:
            failed.append("%s: the worker's splice is %+d pictures from the main output's (limit %d)" % (key, off, limit))
    for f in failed:
        print("FAIL: " + f)
    return 1 if failed else 0


if __name__ == "__main__":
    if len(sys.argv) < 6 or sys.argv[1] != "compare":
        sys.exit(__doc__)
    sys.exit(compare(sys.argv[2], sys.argv[3], int(sys.argv[4]), sys.argv[5:]))
