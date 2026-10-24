#!/usr/bin/env python3
"""coop_launch20.py - feature 001 rig launch 20 (about 15 min, mod-a host, mod-b partner):
  T021a  host picks Female in the lobby: the campaign starts from coop_template_female
         (tools/make_female_template.py); the host must load, read Gender F and look female.
  T028a/b the partner's join is timed into the host's pause menu and its save: held off
         while busy, then completes; the host survives.
Evidence: research/001-evidence/launch20/ (under D:/Fable2-LAN).
"""

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402
import coop_launch19 as L  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\launch20"


def main() -> int:
    L.OUT = OUT
    os.makedirs(OUT, exist_ok=True)
    note, expect, count, grep_log, shots = L.note, L.expect, L.count, L.grep_log, L.shots
    a = coop_rig.Instance("a")
    a.start()
    b = coop_rig.Instance("b")
    b.start()
    note("INFO", f"mod-a pid {a.pid} log {a.log}; mod-b pid {b.pid} log {b.log}")
    L.past_title(a, "a")
    L.past_title(b, "b")
    L.open_coop(a, "a")
    L.open_coop(b, "b")
    time.sleep(3)
    if L.select_row(b, "b", "Join TestA"):
        b.press("A", 250)
        expect(b, "b: joins TestA's lobby", r"joining TestA's lobby as partner", 0, 10)
    time.sleep(2)
    b.press("X", 250)
    time.sleep(3)
    if L.select_row(a, "a", "Your hero"):
        a.press("A", 250)  # Male -> Female
        time.sleep(1.5)
    a.press("X", 250)
    time.sleep(3)
    b.cmd({"cmd": "cvar", "name": "coop_partner_join_delay_ms", "value": "20000"})
    a.cmd({"cmd": "cvar", "name": "coop_join_gate_log", "value": "true"})
    L.select_row(a, "a", "Start")
    a.press("A", 250)
    expect(a, "a: Start accepted (female host)", r"host start: campaign .* host hero female", 0, 10)
    expect(a, "T021a: slot made from coop_template_female", r"starter save: created .* from .*coop_template_female", 0, 10)
    for l in grep_log(a, r"starter save: created|hero gender", 3):
        note("INFO", l[l.find("[coop]"):][:220])
    expect(a, "T021a: the female starter loads", r"\[content\] create root=Save file=Hero\d+ type=00000001 flags=3", 0, 40)
    if not expect(a, "a: host world up", r"host world up \(starter save", 0, 240):
        note("FAIL", "host world not up (female starter rejected?)")
        shots(a, "1_a_stuck")
        a.stop()
        b.stop()
        with open(os.path.join(OUT, "launch20_results.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(L.results) + "\n")
        return 1
    note("INFO", f"T021a: switch call made: {count(a, r'hero gender: ') > 0} (none expected with the female starter)")

    # --- T028a/b: pause menu right away, a save 30 s later; the partner tries at +20 s.
    a.press("Start", 250)
    note("INFO", "T028a: host pause menu open")
    time.sleep(30)
    shots(a, "2_a_paused")
    a.press("Up", 250)
    time.sleep(1.5)
    a.press("A", 250)
    time.sleep(2)
    a.press("A", 250)
    time.sleep(2.5)
    a.press("A", 250)
    note("INFO", "T028a: host saving")
    time.sleep(10)
    for _ in range(2):
        a.press("B", 250)
        time.sleep(1)
    held = grep_log(a, r"join held off", 10)
    note("PASS" if held else "FAIL", f"T028a: joins held off while busy ({len(held)})")
    for l in held[-4:]:
        note("INFO", l[l.find("[joingate]"):][:200])
    if expect(a, "T028a: the held join completes afterwards (player 2 spawned)", r"henchman spawned", 0, 120):
        time.sleep(10)
        note("PASS" if not grep_log(a, r"Unhandled guest|access violation", 3) else "FAIL", "T028a: host survived")
    for l in grep_log(a, r"hero capture waits|host busy|host not busy", 8):
        note("INFO", l[l.find("[coop]"):][:200])

    # --- T021a: what the host is now.
    n = count(a, r"\[coop\] reparse")
    L.console(a, "luafile coop_capture2")
    expect(a, "capture2 ran", r"\[coop\] reparse", n, 20)
    hero = [l for l in grep_log(a, r"\[lua\] COOPHERO:v2", 4)]
    g = re.search(r"\|g=([MF])\|", hero[-2] if len(hero) >= 2 else (hero[-1] if hero else ""))
    note("PASS" if g and g.group(1) == "F" else "FAIL", f"T021a: host hero reads gender {g.group(1) if g else '?'}")
    shots(a, "3_a_host_back")
    a.press("StkRx", 1400)  # swing the camera round to the hero's front
    time.sleep(2)
    shots(a, "3_a_host_front")
    shots(b, "3_b_partner_view")

    with open(os.path.join(OUT, "launch20_results.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(L.results) + "\n")
    with open(os.path.join(OUT, "launch20_a_log_extract.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(grep_log(a, r"\[coop\]|\[joingate\]|\[content\]|XSession|henchman", 400)))
    a.stop()
    b.stop()
    note("INFO", "both closed; rig off")
    return 0


if __name__ == "__main__":
    sys.exit(main())
