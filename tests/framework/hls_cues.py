#!/usr/bin/env python3
"""The cue tags of two HLS outputs of the same programme (the barcode source, see gate_a.py), segment by segment.

    hls_cues.py <in-process directory> <worker directory> <limit in pictures>

For every segment carrying EXT-X-CUE-OUT or EXT-X-CUE-IN, the source picture the segment starts with is read from the picture. The two
playlists must carry the same tags in the same order, each on a segment starting within <limit> pictures of the other's (the test passes one
segment length, 50 pictures: HLS places a cue no more precisely than that). The engine as received writes a return to network twice, on two
adjacent segments, when it falls within a quarter segment after a segment start (playlist.cpp); such a pair counts as one tag with two
candidate segments, matching when either is within <limit>, and is reported. Needs ffmpeg.
"""
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gate_a  # noqa: E402


def media_playlist(directory):
    """The variant playlist whose segments hold the picture."""
    for path in sorted(glob.glob(os.path.join(directory, "**", "*.m3u8"), recursive=True)):
        text = open(path).read()
        if "#EXTINF" not in text:
            continue
        uris = [l.strip() for l in text.splitlines() if l.strip() and not l.startswith("#")]
        if uris and gate_a.pictures(os.path.join(os.path.dirname(path), uris[0])):
            return path
    sys.exit("FAIL: no media playlist with a picture under " + directory)


def tagged(directory):
    """([(tag, first picture of the segment)], segments listed)"""
    path = media_playlist(directory)
    result, pending, count, last_in, doubled = [], [], 0, None, 0
    for line in open(path):
        line = line.strip()
        if line.startswith("#EXT-X-CUE-OUT-CONT"):
            continue
        if line.startswith("#EXT-X-CUE-OUT") or line.startswith("#EXT-X-CUE-IN"):
            pending.append(line.split(":")[0][len("#EXT-X-"):])
        elif line and not line.startswith("#"):
            count += 1
            if pending:
                frames = gate_a.pictures(os.path.join(os.path.dirname(path), line))
                numbers = [n for _, n, _ in frames if n is not None]
                for tag in pending:
                    position = numbers[0] if numbers else None
                    if tag == "CUE-IN" and last_in == count - 1 and result:
                        doubled += 1
                        result[-1][1].append(position)
                    else:
                        result.append((tag, [position]))
                    if tag == "CUE-IN":
                        last_in = count
                pending = []
    return result, count, doubled


def main(in_process, worker, limit):
    (a, na, da), (b, nb, db) = tagged(in_process), tagged(worker)
    print("RESULT CUE-IN written twice on adjacent segments (one tag, two candidate segments): %d in process, %d on the worker" % (da, db))
    show = lambda tags: ", ".join("%s on the segment starting at picture %s" % (t, " or ".join(str(p) for p in ps)) for t, ps in tags) or "none"
    print("RESULT in process (%d segments): %s" % (na, show(a)))
    print("RESULT worker     (%d segments): %s" % (nb, show(b)))
    failed = []
    if [t for t, _ in a] != [t for t, _ in b]:
        failed.append("the tags differ: %s in process, %s on the worker" % ([t for t, _ in a], [t for t, _ in b]))
    else:
        def gap(xs, ys):   # the smallest signed distance between a candidate of the worker and one of the in-process
            d = [y - x for x in xs for y in ys if x is not None and y is not None]
            return min(d, key=abs) if d else None
        off = [gap(x, y) for (_, x), (_, y) in zip(a, b)]
        print("RESULT worker's tagged segments against in-process, pictures: %s" % off)
        if any(d is None or abs(d) > limit for d in off):
            failed.append("a tagged segment starts more than %d pictures from the in-process one (or a tag has no picture): %s" % (limit, off))
    if not a:
        failed.append("no cue tag in process")
    for f in failed:
        print("FAIL: " + f)
    return 1 if failed else 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1], sys.argv[2], int(sys.argv[3])))
