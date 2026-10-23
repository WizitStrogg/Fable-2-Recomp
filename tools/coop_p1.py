#!/usr/bin/env python3
"""coop_p1.py - proof P1 (feature 001): which guest functions run when A is
pressed on New Game and on Continue at the main menu.

Runs mod-c with function tracing (summary only), reaches the main menu, moves
to the row, takes a call-count snapshot, presses A, snapshots again; an idle
window of the same length is the noise baseline. Two runs (New Game, Continue),
the game is restarted between them. Output and screenshots in
D:\\Fable2-LAN\\research\\001-evidence\\proofs\\P1\\.

  python tools/coop_p1.py [--inst c]
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\proofs\P1"
WINDOW_S = 8


def run(inst_name: str, row: str) -> dict:
    rig = coop_rig.Instance(inst_name)
    rig.start(trace=True)
    try:
        if not rig.to_main_menu(240):
            raise RuntimeError("main menu not reached: " + rig.state())
        time.sleep(5)
        # Clamp to the top row (New Game), then step down to the wanted row.
        for _ in range(5):
            rig.press("Up")
            time.sleep(0.6)
        if row == "continue":
            rig.press("Down")
            time.sleep(0.8)
        rig.shot(os.path.join(OUT, f"{row}_0_menu.png"))
        idle0 = rig.snap(os.path.join(OUT, f"{row}_idle0.txt"))
        time.sleep(WINDOW_S)
        idle1 = rig.snap(os.path.join(OUT, f"{row}_idle1.txt"))
        before = rig.snap(os.path.join(OUT, f"{row}_before.txt"))
        rig.press("A")
        time.sleep(WINDOW_S)
        after = rig.snap(os.path.join(OUT, f"{row}_after.txt"))
        rig.shot(os.path.join(OUT, f"{row}_1_after_A.png"))
        time.sleep(10)
        rig.shot(os.path.join(OUT, f"{row}_2_later.png"))
        return {"idle": (idle0, idle1), "press": (before, after), "log": rig.log}
    finally:
        rig.stop()
        time.sleep(3)


def write_diff(name: str, files: dict):
    idle = {n: d for _, d, n in coop_rig.diff_summaries(*files["idle"], top=100000)}
    rows = coop_rig.diff_summaries(*files["press"], top=100000)
    out = os.path.join(OUT, f"{name}_diff.txt")
    with open(out, "w", encoding="utf-8") as f:
        f.write(f"# A on {name}: calls in {WINDOW_S}s after the press minus an idle {WINDOW_S}s window\n")
        f.write("# NEW = never called before the press; ratio = press calls / idle calls\n")
        for new, d, n in rows:
            base = idle.get(n, 0)
            if not new and d <= base * 1.5 + 2:
                continue  # same rate as idle: background noise
            f.write(f"{'NEW ' if new else '    '}{d:>9} (idle {base:>7})  {n}\n")
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--inst", default="c")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    results = {}
    for row in ("newgame", "continue"):
        print(f"run: {row}", flush=True)
        results[row] = run(a.inst, row)
        print("  diff:", write_diff(row, results[row]), "log:", results[row]["log"], flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
