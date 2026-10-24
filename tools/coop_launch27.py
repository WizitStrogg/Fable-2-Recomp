#!/usr/bin/env python3
"""coop_launch27.py - feature 001 rig launch 27 (about 10 min, mod-a host + mod-b partner):
item (b) step 1. The game's own co-op join screens are stepped by hand (auto answers off)
with the join-screen controller probe on (coop_join_screen_probe): which GUI events
(id, phase) and state flags each press produces, on each PC, in order:
  host A (accept the split), partner A (accept the look), partner A (continue without a
  save), then more A on both until player 2 spawns.
The join cover stays up as usual. Evidence: research/001-evidence/menus/launch27/.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402
import coop_launch19 as L  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\menus\launch27"


def main() -> int:
    L.OUT = OUT
    os.makedirs(OUT, exist_ok=True)
    note, expect, count, grep_log = L.note, L.expect, L.count, L.grep_log

    a = coop_rig.Instance("a")
    a.start()
    b = coop_rig.Instance("b")
    b.start()
    note("INFO", f"mod-a pid {a.pid} log {a.log}; mod-b pid {b.pid} log {b.log}")
    L.past_title(a, "a")
    L.past_title(b, "b")
    a.press("Down", 250)
    time.sleep(1.5)
    a.press("A", 250)
    expect(a, "Continue opens the campaign lobby", r"continue: Hero\d+ is campaign", 0, 10)
    time.sleep(3)
    L.open_coop(b, "b")
    time.sleep(3)
    if L.select_row(b, "b", "Join TestA"):
        b.press("A", 250)
    time.sleep(2)
    b.press("X", 250)
    time.sleep(3)
    a.press("X", 250)
    time.sleep(3)
    for r in (a, b):
        r.cmd({"cmd": "cvar", "name": "coop_join_screens_auto", "value": "false"})
        r.cmd({"cmd": "cvar", "name": "coop_join_screen_probe", "value": "true"})
    L.select_row(a, "a", "Start")
    a.press("A", 250)
    expect(a, "Start continues the campaign", r"host start: campaign .* continuing", 0, 10)
    expect(a, "host world up", r"host world up \(starter save", 0, 240)
    if not expect(a, "join approved", r"\[joingate\] join approved", 0, 90):
        note("FAIL", "no join")
    time.sleep(5)

    def mark(tag):
        for r in (a, b):
            r.cmd({"cmd": "cvar", "name": "coop_rig_save_filter", "value": ""})  # harmless log marker
        note("INFO", f"--- {tag} at {time.strftime('%H:%M:%S')}")

    steps = [(a, "host A (accept the split)"), (b, "partner A (accept the look)"),
             (b, "partner A (continue without a save)")]
    for r, what in steps:
        mark(what)
        r.press("A", 250)
        time.sleep(4)
    n = 0
    while count(a, r"henchman spawned") == 0 and n < 4:
        mark("extra A on both")
        b.press("A", 250)
        time.sleep(2)
        a.press("A", 250)
        time.sleep(3)
        n += 1
    expect(a, "player 2 spawned", r"henchman spawned", 0, 30)
    for r, who in ((a, "a"), (b, "b")):
        with open(os.path.join(OUT, f"joinscreen_{who}.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(l[l.find("["):] for l in grep_log(r, r"\[joinscreen\]|A for the join|henchman spawned|join approved|join cover", 400)) + "\n")
    with open(os.path.join(OUT, "launch27_results.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(L.results) + "\n")
    a.stop()
    b.stop()
    note("INFO", "both closed; rig off")
    return 0


if __name__ == "__main__":
    sys.exit(main())
