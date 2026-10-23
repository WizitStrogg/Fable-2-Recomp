#!/usr/bin/env python3
"""coop_diff.py - compare two COOPHERO profile lines field by field.

Each argument is a COOPHERO line, or a file (game log, coop_player2.txt, capture)
whose last COOPHERO line is used. Prints every field that differs and exits 1 on
any difference, 0 when the profiles match, 2 when a profile cannot be read.

Examples:
  python tools/coop_diff.py before.txt after.txt
  python tools/coop_diff.py mod-a/logs/fable_2.log "COOPHERO:v2|g=F|..."
  python tools/coop_diff.py a.txt b.txt --ignore rev,xpt
"""

import argparse
import os
import re
import sys

LINE_RE = re.compile(r"COOPHERO:(v\d+)\|([^\r\n]*)")


def read_profile(arg: str):
    """Returns (version, {field: value}) from a line or the last line in a file."""
    text = arg
    if os.path.isfile(arg):
        with open(arg, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    matches = LINE_RE.findall(text)
    if not matches:
        return None
    version, body = matches[-1]
    fields = {}
    for part in body.split("|"):
        if "=" in part:
            k, v = part.split("=", 1)
            fields[k.strip()] = v.strip()
    return version, fields


def norm(value: str):
    """Numbers compare by value (1.0000 == 1), lists keep their order."""
    try:
        return float(value)
    except ValueError:
        return value


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("a", help="COOPHERO line or file")
    p.add_argument("b", help="COOPHERO line or file")
    p.add_argument("--ignore", default="", help="comma-separated fields to skip")
    args = p.parse_args()

    pa, pb = read_profile(args.a), read_profile(args.b)
    for name, prof in (("a", pa), ("b", pb)):
        if prof is None:
            print(f"error: no COOPHERO line in {name}", file=sys.stderr)
            return 2

    ignore = {f for f in args.ignore.split(",") if f}
    (va, fa), (vb, fb) = pa, pb
    diffs = []
    if va != vb:
        diffs.append(f"version: {va} != {vb}")
    for key in sorted(set(fa) | set(fb)):
        if key in ignore:
            continue
        x, y = fa.get(key), fb.get(key)
        if x is None or y is None:
            diffs.append(f"{key}: {'missing' if x is None else x!r} != {'missing' if y is None else y!r}")
        elif norm(x) != norm(y):
            diffs.append(f"{key}: {x!r} != {y!r}")

    if diffs:
        print(f"DIFFERENT ({len(diffs)} fields)")
        for d in diffs:
            print("  " + d)
        return 1
    print(f"SAME ({len(fa)} fields, {va})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
