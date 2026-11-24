#!/usr/bin/env python3
"""The splice_insert events on one PID of a transport stream, one line per event id (the first copy):

    scte_events.py <file.ts> [pid]    ->  "<event id> <out_of_network> <unique_program_id> <splice time in 90 kHz ticks>"

The splice time is pts_time + pts_adjustment, modulo 2^33, as a receiver reads it. Needs tstables (TSDuck).
"""
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


def number(text):
    return int(text.replace(",", ""), 0)


def main():
    ts = sys.argv[1]
    pid = sys.argv[2] if len(sys.argv) > 2 else "500"
    xml = subprocess.run(["tstables", "--pid", pid, "--tid", "0xFC", "--max-tables", "2000", "--xml-output", "-", ts],
                         capture_output=True, text=True).stdout
    seen = {}
    for table in ET.fromstring(xml).iter("splice_information_table"):
        adjustment = number(table.get("pts_adjustment", "0"))
        for insert in table.iter("splice_insert"):
            if insert.get("splice_event_cancel") == "true" or insert.get("pts_time") is None:
                continue
            event = number(insert.get("splice_event_id"))
            if event not in seen:
                seen[event] = (insert.get("out_of_network"), number(insert.get("unique_program_id", "0")),
                               (number(insert.get("pts_time")) + adjustment) % (1 << 33))
    for event in sorted(seen):
        print(event, *seen[event])


if __name__ == "__main__":
    main()
