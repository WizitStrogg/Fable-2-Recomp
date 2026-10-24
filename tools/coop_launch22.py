#!/usr/bin/env python3
"""coop_launch22.py - feature 001 rig launch 22 (about 15 min, mod-a host, mod-b partner):
T021a end to end. The host picks Female in the lobby; the campaign start switches the
starter's boy with the game's own sex-change call, retrying until the hero reads
female, and only then lets the partner in. Checks: host reads F and looks female, the
partner joins, a pause-menu save, the partner leaves (P6), both restart, Continue the
campaign, the partner rejoins, and the host still reads F after the reload.
Evidence: research/001-evidence/launch22/ (under D:/Fable2-LAN).
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402
import coop_launch19 as L  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\launch22"


def main() -> int:
    L.OUT = OUT
    os.makedirs(OUT, exist_ok=True)
    note, expect, count, grep_log, shots, console = L.note, L.expect, L.count, L.grep_log, L.shots, L.console

    def read(r, tag):
        n = count(r, r"gender read: ")
        console(r, "luafile coop_g_read")
        expect(r, f"{tag}: gender read", r"gender read: ", n, 15)
        lines = grep_log(r, r"gender read: ", 1)
        line = lines[-1][lines[-1].find("gender read"):] if lines else ""
        note("INFO", f"{tag}: {line[:120]}")
        return "Gender.Get=F" in line

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
    L.select_row(a, "a", "Start")
    a.press("A", 250)
    expect(a, "a: Start accepted (female host)", r"host start: campaign .* host hero female", 0, 10)
    expect(a, "a: the starter loads", r"\[content\] create root=Save file=Hero\d+ type=00000001 flags=3", 0, 40)
    expect(a, "T021a: gender switch started", r"host start: hero gender M -> F", 0, 120)
    expect(a, "T021a: hero gender confirmed F before the partner is let in", r"host start: hero gender is F", 0, 180)
    for l in grep_log(a, r"hero gender", 6):
        note("INFO", l[l.find("[coop]") if "[coop]" in l else 0:][:160])
    expect(a, "a: host world up", r"host world up \(starter save", 0, 120)
    if not expect(a, "player 2 joins", r"henchman spawned", 0, 150):
        L.results.append("stopped: no join")
    time.sleep(8)
    female = read(a, "host in the campaign")
    note("PASS" if female else "FAIL", "T021a: host hero is female in the campaign")
    shots(a, "1_a_host_female")
    shots(b, "1_b_sees_host")

    # Save, partner leaves (P6: autosave, back to the front end).
    n = count(a, r"\[content\] close root=Save")
    a.press("Start", 250); time.sleep(3); a.press("Up", 250); time.sleep(1.5)
    a.press("A", 250); time.sleep(2); a.press("A", 250); time.sleep(2.5); a.press("A", 250)
    expect(a, "pause-menu save", r"\[content\] close root=Save", n, 30)
    time.sleep(4)
    a.press("B", 250); time.sleep(1); a.press("B", 250); time.sleep(2)
    b.stop()
    expect(a, "P6: partner loss -> back at the front end", r"back at the front end", 0, 120)

    # Restart both and continue the campaign.
    a.stop()
    a = coop_rig.Instance("a")
    a.start()
    b = coop_rig.Instance("b")
    b.start()
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
    expect(a, "host world up after reload", r"host world up \(starter save", 0, 240)
    expect(a, "player 2 rejoins", r"henchman spawned", 0, 150)
    time.sleep(8)
    again = read(a, "host after reload")
    note("PASS" if again else "FAIL", "T021a: host still female after save + reload")
    shots(a, "2_a_host_after_reload")
    shots(b, "2_b_after_reload")

    with open(os.path.join(OUT, "launch22_results.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(L.results) + "\n")
    a.stop()
    b.stop()
    note("INFO", "both closed; rig off")
    return 0


if __name__ == "__main__":
    sys.exit(main())
