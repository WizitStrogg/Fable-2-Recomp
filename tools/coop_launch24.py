#!/usr/bin/env python3
"""coop_launch24.py - feature 001 rig launch 24 (about 10 min, mod-a host, mod-b partner):
T034 in-world gold, the game's own way: a collectible ObjectInventoryGoldBag_100 made with
Debug.CreateEntityAtEntitysPosition at player 2 (the Crucible's reward call), collected by the
partner stepping off and back onto it with A; then the same for the host. Checks the bag is
gone (collected) and the shared purse rose by 100 each time on both purses.
Continues mod-a's newest campaign.
Evidence: research/001-evidence/launch24/ (under D:/Fable2-LAN).
"""

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402
import coop_launch19 as L  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\launch24"


def main() -> int:
    L.OUT = OUT
    os.makedirs(OUT, exist_ok=True)
    note, expect, count, grep_log, shots, console = L.note, L.expect, L.count, L.grep_log, L.shots, L.console

    def lua_line(script, pattern, tag):
        n = count(a, pattern)
        console(a, f"luafile {script}")
        expect(a, tag, pattern, n, 15)
        lines = grep_log(a, pattern, 1)
        line = lines[-1][lines[-1].find("[coop]"):] if lines else ""
        note("INFO", line[:160])
        return line

    def purse(tag):
        line = lua_line("coop_purse_read", r"purse read: ", f"{tag}: purse read")
        m = re.search(r"host (-?\d+), player 2 (-?\d+)", line)
        return (int(m.group(1)), int(m.group(2))) if m else (None, None)

    def collect(r, who):
        # Step off the bag and back onto it, pressing A on the way back.
        for button, hold in (("StickDown", 500), ("StickUp", 550)):
            r.press(button, hold)
            time.sleep(hold / 1000 + 0.4)
        for _ in range(3):
            r.press("A", 300)
            time.sleep(1.5)
        time.sleep(3)
        return "true" not in lua_line("coop_gold_alive", r"gold bag alive: ", f"{who}: bag state")

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
    L.select_row(a, "a", "Start")
    a.press("A", 250)
    expect(a, "Start continues the campaign", r"host start: campaign .* continuing", 0, 10)
    expect(a, "host world up", r"host world up \(starter save", 0, 240)
    if not expect(a, "player 2 joins", r"henchman spawned", 0, 150):
        with open(os.path.join(OUT, "launch24_results.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(L.results) + "\n")
        a.stop(); b.stop()
        return 1
    time.sleep(12)

    h0, p0 = purse("start")
    console(a, "luafile coop_gold_for_p2")
    lua_line("coop_gold_drop", r"gold bag for ", "bag for player 2")
    shots(b, "1_b_bag_dropped")
    took = collect(b, "partner")
    h1, p1 = purse("after the partner's pickup")
    note("PASS" if took else "FAIL", "T034: the partner collected the bag")
    note("PASS" if h0 is not None and h1 == h0 + 100 and p1 == h1 else "FAIL",
         f"T034: partner's 100 gold -> shared total {h0} -> {h1} (player 2 {p1})")
    console(a, "luafile coop_gold_for_host")
    lua_line("coop_gold_drop", r"gold bag for ", "bag for the host")
    took = collect(a, "host")
    h2, p2 = purse("after the host's pickup")
    note("PASS" if took else "FAIL", "T034: the host collected the bag")
    note("PASS" if h1 is not None and h2 == h1 + 100 and p2 == h2 else "FAIL",
         f"T034: host's 100 gold -> shared total {h1} -> {h2} (player 2 {p2})")
    for l in grep_log(a, r"\[coop\] purse:", 6):
        note("INFO", l[l.find("[coop]"):][:200])
    shots(a, "2_a_after")
    shots(b, "2_b_after")

    with open(os.path.join(OUT, "launch24_results.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(L.results) + "\n")
    a.stop()
    b.stop()
    note("INFO", "both closed; rig off")
    return 0


if __name__ == "__main__":
    sys.exit(main())
