#!/usr/bin/env python3
"""coop_launch1.py - feature 001 rig launch 1 (mod-b, one session, left open):
T018: title -> New Game -> chooser -> Solo -> hero cards -> B -> New Game ->
chooser -> Co-op -> host lobby -> B (chooser) -> B (menu).
Every step is recorded (PASS/FAIL) and the run continues past a failure.
Evidence: D:\\Fable2-LAN\\research\\001-evidence\\T018\\launch1_*.
"""

import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\T018"
G = r"D:\Fable2-LAN\tools\gamectl.ps1"
results = []


def sshot(r, name):
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", G, "sshot",
                    str(r.pid), os.path.join(OUT, f"launch1_{name}_screen.png")], capture_output=True)
    r.shot(os.path.join(OUT, f"launch1_{name}_frame.png"))


def log_count(r, pattern):
    return len(re.findall(pattern, coop_rig.read_shared(r.log)))


def expect(r, step, pattern, before, timeout=10):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if log_count(r, pattern) > before:
            results.append(f"PASS {step}")
            print("PASS", step, flush=True)
            return True
        time.sleep(0.5)
    results.append(f"FAIL {step} (no log line /{pattern}/ within {timeout}s)")
    print("FAIL", step, flush=True)
    return False


def main() -> int:
    os.makedirs(OUT, exist_ok=True)
    r = coop_rig.Instance("b")
    r.start()
    print("mod-b pid", r.pid, "port", r.port, "log", r.log, flush=True)
    deadline = time.time() + 240
    while time.time() < deadline and r.state() not in ("PressAScreen", "MainMenu", "MainMenuMovie"):
        time.sleep(2)
    time.sleep(5)
    sshot(r, "00_title")

    n = log_count(r, r"title passed")
    r.press("A", 250)
    if not expect(r, "title A passes to the menu", r"title passed", n, 8):
        r.press("A", 250)
        expect(r, "title A passes to the menu (2nd try)", r"title passed", n, 8)
    time.sleep(4)
    sshot(r, "01_menu")

    n = log_count(r, r"\[lobby\] open chooser")
    r.press("A", 250)
    expect(r, "A on New Game opens the chooser", r"\[lobby\] open chooser", n)
    time.sleep(1.5)
    sshot(r, "02_chooser")

    n = log_count(r, r"injected A passed")
    r.press("A", 250)
    expect(r, "Solo resumes the stock New Game", r"injected A passed", n)
    # The stock New Game: storage step, then the boy/girl hero cards.
    time.sleep(25)
    sshot(r, "03_solo_hero_cards")

    r.press("B", 250)
    time.sleep(5)
    sshot(r, "04_back_to_menu")

    n = log_count(r, r"\[lobby\] open chooser")
    r.press("A", 250)
    expect(r, "A on New Game opens the chooser again", r"\[lobby\] open chooser", n)
    time.sleep(1.5)
    r.press("Down", 250)
    time.sleep(1.5)
    sshot(r, "05_chooser_coop_selected")
    n = log_count(r, r"\[lobby\] open host")
    r.press("A", 250)
    expect(r, "Co-op opens the host lobby", r"\[lobby\] open host", n)
    time.sleep(2.5)
    sshot(r, "06_coop_lobby")

    n = log_count(r, r"\[lobby\] open chooser")
    r.press("B", 250)
    expect(r, "B in the lobby returns to the chooser", r"\[lobby\] open chooser", n)
    time.sleep(1.5)
    r.press("B", 250)
    time.sleep(2)
    sshot(r, "07_back_to_menu")

    lines = [l for l in coop_rig.read_shared(r.log).splitlines()
             if re.search(r"\[coop\]|\[lobby\]|\[content\] create", l)]
    with open(os.path.join(OUT, "launch1_log.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(results) + "\n\n" + "\n".join(lines) + "\n")
    print("\n".join(results))
    print("left open: mod-b pid", r.pid, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
