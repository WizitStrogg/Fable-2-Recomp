#!/usr/bin/env python3
"""coop_launch25.py - feature 001 rig launch 25 (about 15 min, mod-a host + mod-b partner,
both traced): find the code behind the game's own co-op join screens so a campaign can
skip them. The join screens are stepped by hand (coop_join_screens_auto off): a snapshot
of both call summaries after each step, screenshots, and per-step diffs of functions that
ran only then:
  S0 idle in the world (baseline)      S1 join approved, screens up
  S2 partner A (accept look)           S3 host A (accept split)
  S4 partner A (continue without a save -> Yes)   S5.. more A until player 2 spawns
Evidence: research/001-evidence/menus/launch25/ (under D:/Fable2-LAN).
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402
import coop_launch19 as L  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\menus\launch25"


def main() -> int:
    L.OUT = OUT
    os.makedirs(OUT, exist_ok=True)
    note, expect, count, grep_log, shots = L.note, L.expect, L.count, L.grep_log, L.shots

    a = coop_rig.Instance("a")
    a.start(trace=True)
    b = coop_rig.Instance("b")
    b.start(trace=True)
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
    b.cmd({"cmd": "cvar", "name": "coop_partner_join_delay_ms", "value": "25000"})
    L.select_row(a, "a", "Start")
    a.press("A", 250)
    expect(a, "Start continues the campaign", r"host start: campaign .* continuing", 0, 10)
    expect(a, "host world up", r"host world up \(starter save", 0, 240)
    time.sleep(3)

    snaps = {}

    def snap(tag):
        for r, who in ((a, "a"), (b, "b")):
            snaps[(tag, who)] = r.snap(os.path.join(OUT, f"{tag}_{who}_summary.txt"))
        shots(a, f"{tag}_a")
        shots(b, f"{tag}_b")

    # S0 idle baseline: two snapshots 8 s apart give the idle call rates.
    snap("S0a")
    time.sleep(8)
    snap("S0b")
    if not expect(a, "join approved (screens up)", r"\[joingate\] join approved", 0, 90):
        note("FAIL", "no join; stopping")
    time.sleep(4)
    snap("S1")
    steps = [("S2", b, "partner A (accept look)"), ("S3", a, "host A (accept split)"),
             ("S4", b, "partner A (continue without a save)")]
    for tag, r, what in steps:
        r.press("A", 250)
        time.sleep(4)
        snap(tag)
        note("INFO", f"{tag}: {what}")
    n = 5
    while count(a, r"henchman spawned") == 0 and n < 9:
        b.press("A", 250)
        time.sleep(2)
        a.press("A", 250)
        time.sleep(3)
        snap(f"S{n}")
        note("INFO", f"S{n}: extra A on both")
        n += 1
    expect(a, "player 2 spawned", r"henchman spawned", 0, 30)

    # Per-step diffs: functions that ran in a step and not in the idle window.
    idle = {who: {name: d for _, d, name in coop_rig.diff_summaries(snaps[("S0a", who)], snaps[("S0b", who)], 100000)}
            for who in ("a", "b")}
    order = ["S0b", "S1", "S2", "S3", "S4"] + [f"S{i}" for i in range(5, n)]
    with open(os.path.join(OUT, "step_diffs.txt"), "w", encoding="utf-8") as f:
        for prev, cur in zip(order, order[1:]):
            for who in ("a", "b"):
                if (prev, who) not in snaps or (cur, who) not in snaps:
                    continue
                rows = coop_rig.diff_summaries(snaps[(prev, who)], snaps[(cur, who)], 100000)
                new = [(d, name) for _, d, name in rows if idle[who].get(name, 0) == 0 and d > 0]
                f.write(f"== {prev} -> {cur} on {who}: {len(new)} functions not seen idle\n")
                for d, name in sorted(new, key=lambda x: -x[0])[:120]:
                    f.write(f"  {d:>8}  {name}\n")
    note("INFO", "step_diffs.txt written")
    with open(os.path.join(OUT, "launch25_results.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(L.results) + "\n")
    a.stop()
    b.stop()
    note("INFO", "both closed; rig off")
    return 0


if __name__ == "__main__":
    sys.exit(main())
