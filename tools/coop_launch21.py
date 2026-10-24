#!/usr/bin/env python3
"""coop_launch21.py - feature 001 rig launch 21 (mod-a only, about 10 min): T021a,
which way makes the loaded host hero female. Continue loads mod-a's starter-based
slot Hero008 (the save list narrowed to it with the rig cvar coop_rig_save_filter;
the campaign hook switched off after the title so Continue just loads it). Then, one
at a time until one works, each followed by a gender read and screenshots:
  1 Player.ChangePlayerEntityType (the sex-change potion's way), 12 s to act
  2 Debug.ChangePlayerEntityType
  3 Gender.Set
If one works: pause-menu save, quit to the front end, Continue again: still female?
World-only Lua runs only after the load (launch 21a ran queued scripts at the front
end and crashed on QuitToFrontEnd there).
Evidence: research/001-evidence/launch21/ (under D:/Fable2-LAN).
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402
import coop_launch19 as L  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\launch21"
LOADED = r"\[content\] create root=Save file=Hero008 type=00000001 flags=3"


def main() -> int:
    L.OUT = OUT
    os.makedirs(OUT, exist_ok=True)
    note, expect, count, grep_log, shots, console = L.note, L.expect, L.count, L.grep_log, L.shots, L.console
    a = coop_rig.Instance("a")
    a.start()
    note("INFO", f"mod-a pid {a.pid} log {a.log}")

    def finish(code):
        with open(os.path.join(OUT, "launch21_results.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(L.results) + "\n")
        a.stop()
        note("INFO", "mod-a closed; rig off")
        return code

    def read(tag):
        n = count(a, r"gender read: ")
        console(a, "luafile coop_g_read")
        expect(a, f"{tag}: gender read", r"gender read: ", n, 15)
        lines = grep_log(a, r"gender read: ", 1)
        line = lines[-1][lines[-1].find("gender read"):] if lines else ""
        note("INFO", f"{tag}: {line[:200]}")
        return "Gender.Get=F" in line

    def load(tag, first):
        if first:
            L.past_title(a, "a")  # the campaign hook logs the title pass
        else:
            deadline = time.time() + 120
            while time.time() < deadline and a.state() not in ("PressAScreen", "MainMenu", "MainMenuMovie"):
                time.sleep(2)
            time.sleep(5)
            a.press("A", 250)
            time.sleep(4)
        a.cmd({"cmd": "cvar", "name": "coop_campaign", "value": "false"})
        a.cmd({"cmd": "cvar", "name": "coop_rig_save_filter", "value": "Hero008"})
        time.sleep(1)
        a.press("Down", 250)  # New Game -> Continue
        time.sleep(1.5)
        n = count(a, LOADED)
        a.press("A", 250)
        if not expect(a, f"{tag}: Continue loads Hero008", LOADED, n, 40):
            shots(a, f"{tag}_no_load")
            return False
        time.sleep(45)
        return True

    if not load("0", True):
        return finish(1)
    female = read("0 loaded")
    shots(a, "0_loaded")
    method = None
    for tag, script in (("1 player", "coop_g_try_player"), ("2 debug", "coop_g_try_debug"), ("3 set", "coop_g_try_set")):
        if female:
            break
        console(a, f"luafile {script}")
        time.sleep(12)
        for l in grep_log(a, r"gender try", 1):
            note("INFO", l[l.find("[coop]"):][:200])
        female = read(tag)
        shots(a, tag.replace(" ", "_"))
        if female:
            method = tag
    note("PASS" if female else "FAIL", f"T021a: hero female in game (method {method})")

    if female:
        n = count(a, r"\[content\] close root=Save")
        a.press("Start", 250); time.sleep(3); a.press("Up", 250); time.sleep(1.5)
        a.press("A", 250); time.sleep(2); a.press("A", 250); time.sleep(2.5); a.press("A", 250)
        expect(a, "saved", r"\[content\] close root=Save", n, 30)
        time.sleep(4)
        a.press("B", 250); time.sleep(1); a.press("B", 250); time.sleep(2)
        console(a, "luafile coop_quit")  # only in the world
        time.sleep(25)
        if load("reload", False):
            again = read("after reload")
            shots(a, "4_after_reload")
            note("PASS" if again else "FAIL", "T021a: female after save + reload")
    return finish(0)


if __name__ == "__main__":
    sys.exit(main())
