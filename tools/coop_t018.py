#!/usr/bin/env python3
"""coop_t018.py - rig check for T018 (New Game -> Solo / Co-op).

Two runs on one instance (default mod-b, coop_campaign = true in its fable_2.toml):
  solo:  A on New Game -> chooser -> A on Solo -> the stock New Game (hero cards)
  coop:  A on New Game -> chooser -> Down, A on Co-op -> host lobby panel
Screenshots (renderer frame and whole window, which includes the panel) and the
log lines go to D:\\Fable2-LAN\\research\\001-evidence\\T018\\.

  python tools/coop_t018.py [--inst b] [--only solo|coop]
"""

import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\T018"
GAMECTL = r"D:\Fable2-LAN\tools\gamectl.ps1"


def window_shot(pid: int, path: str):
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", GAMECTL,
                    "sshot", str(pid), path], capture_output=True)


def both(r, name: str):
    r.shot(os.path.join(OUT, name + "_frame.png"))
    window_shot(r.pid, os.path.join(OUT, name + "_window.png"))


def run(inst: str, mode: str) -> list:
    r = coop_rig.Instance(inst)
    r.start()
    try:
        if not r.to_main_menu(240) or not r.ensure_menu_list():
            raise RuntimeError("main menu not reached")
        time.sleep(2)
        both(r, f"{mode}_0_menu")
        r.press("A", 250)
        time.sleep(2.5)
        both(r, f"{mode}_1_chooser")
        if mode == "coop":
            r.press("Down", 250)
            time.sleep(1)
        r.press("A", 250)
        time.sleep(4)
        both(r, f"{mode}_2_after_choice")
        if mode == "solo":
            for t in (10, 25):
                time.sleep(t - 4 if t == 10 else 15)
                both(r, f"{mode}_3_{t}s")
        log = coop_rig.read_shared(r.log)
        return [l for l in log.splitlines() if re.search(r"\[coop\]|\[lobby\]|\[content\] create", l)]
    finally:
        r.stop()
        time.sleep(3)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--inst", default="b")
    ap.add_argument("--only", choices=["solo", "coop"])
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    for mode in ([a.only] if a.only else ["solo", "coop"]):
        lines = run(a.inst, mode)
        with open(os.path.join(OUT, f"{mode}_log.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        print(f"== {mode}")
        print("\n".join(lines[-12:]), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
