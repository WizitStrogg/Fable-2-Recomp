#!/usr/bin/env python3
"""coop_launch23.py - feature 001 rig launch 23 (about 15 min, mod-a host, mod-b partner):
  T025  Back from a campaign's lobby: host (Continue -> lobby -> B -> menu, Continue works
        again); partner (seated -> B leaves the seat, B again -> back to the chooser).
  T034  in-world gold: a real 100-gold bag dropped at player 2's feet and picked up by the
        partner, then one at the host's feet picked up by the host: the shared purse rises
        by 100 each time on both purses.
Continues mod-a's newest campaign (launch 22's).
Evidence: research/001-evidence/launch23/ (under D:/Fable2-LAN).
"""

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402
import coop_launch19 as L  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\launch23"


def main() -> int:
    L.OUT = OUT
    os.makedirs(OUT, exist_ok=True)
    note, expect, count, grep_log, shots, console = L.note, L.expect, L.count, L.grep_log, L.shots, L.console

    def last_view(r):
        v = grep_log(r, r"\[lobby\] view", 1)
        return v[-1][v[-1].find("[lobby]"):] if v else ""

    def purse(tag):
        n = count(a, r"purse read: ")
        console(a, "luafile coop_purse_read")
        expect(a, f"{tag}: purse read", r"purse read: ", n, 15)
        m = re.search(r"purse read: host (-?\d+), player 2 (-?\d+)", grep_log(a, r"purse read: ", 1)[0])
        h, p = (int(m.group(1)), int(m.group(2))) if m else (None, None)
        note("INFO", f"{tag}: host {h}, player 2 {p}")
        return h, p

    a = coop_rig.Instance("a")
    a.start()
    b = coop_rig.Instance("b")
    b.start()
    note("INFO", f"mod-a pid {a.pid} log {a.log}; mod-b pid {b.pid} log {b.log}")
    L.past_title(a, "a")
    L.past_title(b, "b")

    # --- T018a discovery: the menu element's memory over Down x4 / Up x4 (log only) -----
    a.cmd({"cmd": "cvar", "name": "coop_menu_probe", "value": "true"})
    time.sleep(1)
    for button in ["Down"] * 4 + ["Up"] * 4:
        a.press(button, 200)
        time.sleep(1.3)
    a.cmd({"cmd": "cvar", "name": "coop_menu_probe", "value": "false"})
    probe = grep_log(a, r"\[menuprobe\]", 40)
    with open(os.path.join(OUT, "0_menuprobe.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(l[l.find("[menuprobe]"):] for l in probe) + "\n")
    note("INFO", f"T018a: {len(probe)} menu probe lines (0_menuprobe.txt)")
    time.sleep(2)

    # --- Back from the host's campaign lobby -------------------------------------------
    a.press("Down", 250)
    time.sleep(1.5)
    n = count(a, r"continue: Hero\d+ is campaign")
    a.press("A", 250)
    expect(a, "host: Continue opens the campaign lobby", r"continue: Hero\d+ is campaign", n, 10)
    time.sleep(3)
    shots(a, "1_a_campaign_lobby")
    n = count(a, r"campaign lobby closed, back to the menu")
    a.press("B", 250)
    expect(a, "T025: host Back -> the menu", r"campaign lobby closed, back to the menu", n, 8)
    time.sleep(3)
    shots(a, "1_a_menu_after_back")
    n = count(a, r"continue: Hero\d+ is campaign")
    a.press("A", 250)  # the cursor is still on Continue
    expect(a, "T025: Continue opens the lobby again after Back", r"continue: Hero\d+ is campaign", n, 10)
    time.sleep(3)

    # --- Back on the partner side ------------------------------------------------------
    L.open_coop(b, "b")
    time.sleep(3)
    if L.select_row(b, "b", "Join TestA"):
        n = count(b, r"joining TestA's lobby as partner")
        b.press("A", 250)
        expect(b, "partner seated in the campaign lobby", r"joining TestA's lobby as partner", n, 10)
    time.sleep(3)
    b.press("B", 250)
    time.sleep(3)
    note("PASS" if "Join TestA" in last_view(b) else "FAIL", "T025: partner B leaves the seat (lists the campaign again)")
    note("INFO", last_view(b)[:220])
    n = count(b, r"lobby closed, back to the chooser")
    b.press("B", 250)
    expect(b, "T025: partner B again -> the chooser", r"lobby closed, back to the chooser|open chooser", n, 8)
    time.sleep(3)
    shots(b, "2_b_chooser_after_back")
    # Back in: Co-op, join, ready.
    b.press("Down", 250)
    time.sleep(1.2)
    b.press("A", 250)
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
        with open(os.path.join(OUT, "launch23_results.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(L.results) + "\n")
        a.stop(); b.stop()
        return 1
    time.sleep(12)

    # --- In-world gold -------------------------------------------------------------------
    h0, p0 = purse("start")
    console(a, "luafile coop_gold_p2")
    time.sleep(2)
    for l in grep_log(a, r"gold drop", 1):
        note("INFO", l[l.find("[coop]"):][:120])
    for _ in range(3):
        b.press("A", 300)  # the partner picks the bag up
        time.sleep(2)
    time.sleep(4)
    h1, p1 = purse("after the partner's pickup")
    note("PASS" if h0 is not None and h1 == h0 + 100 and p1 == h1 else "FAIL",
         f"T034: partner picks up 100 gold -> shared total {h0} -> {h1} (player 2 {p1})")
    shots(a, "3_a_after_p2_pickup")
    shots(b, "3_b_after_p2_pickup")
    console(a, "luafile coop_gold_host")
    time.sleep(2)
    for _ in range(3):
        a.press("A", 300)
        time.sleep(2)
    time.sleep(4)
    h2, p2 = purse("after the host's pickup")
    note("PASS" if h1 is not None and h2 == h1 + 100 and p2 == h2 else "FAIL",
         f"T034: host picks up 100 gold -> shared total {h1} -> {h2} (player 2 {p2})")
    for l in grep_log(a, r"\[coop\] purse:", 6):
        note("INFO", l[l.find("[coop]"):][:200])
    shots(a, "4_a_after_host_pickup")
    shots(b, "4_b_after_host_pickup")

    with open(os.path.join(OUT, "launch23_results.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(L.results) + "\n")
    a.stop()
    b.stop()
    note("INFO", "both closed; rig off")
    return 0


if __name__ == "__main__":
    sys.exit(main())
