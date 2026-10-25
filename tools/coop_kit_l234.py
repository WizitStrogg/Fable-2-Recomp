#!/usr/bin/env python3
"""coop_kit_l234.py - feature 002: L2 + L3 (one session), then L4, back to back.

  python tools/coop_kit_l234.py

L4 runs unless something core failed in L2: the join, the mirror (the partner's
copy holding the kit), the potion, the hat, or the host staying paused. The
sword-swap check and L3 do not stop it (coordinator, 2026-10-25 16:34Z).
Evidence: research/002-evidence/l2, l3, l4 and l234_run.log.
"""

import os
import subprocess
import sys

ROOT = r"D:\Fable2-LAN"
EV = os.path.join(ROOT, r"research\002-evidence")
HERE = os.path.dirname(os.path.abspath(__file__))
CORE = ("partner joined the host's campaign", "the partner's copy holds the potion",
        "the partner's copy holds the hat", "l2p:", "l2h:", "the host did not resume")


def run(mode, log):
    with open(log, "w", encoding="utf-8") as f:
        return subprocess.call([sys.executable, os.path.join(HERE, "coop_kit_l34.py"), mode], stdout=f,
                               stderr=subprocess.STDOUT, cwd=os.path.dirname(HERE))


def main():
    print("l23 ...", flush=True)
    run("l23", os.path.join(EV, "l23_run.log"))
    res_file = os.path.join(EV, "l2", "results.txt")
    results = open(res_file, encoding="utf-8").read().splitlines() if os.path.exists(res_file) else []
    joined = any("PASS === l2: partner joined the host's campaign" in r for r in results)
    core = [r for r in results if r.startswith("FAIL") and any(c in r for c in CORE)]
    if not joined or core:
        print(f"L4 skipped: joined={joined}, core FAILs: {core}", flush=True)
        return 1
    print("l4 ...", flush=True)
    rc = run("l4", os.path.join(EV, "l4_run.log"))
    print(f"l4 exit {rc}", flush=True)
    return rc


if __name__ == "__main__":
    sys.exit(main())
