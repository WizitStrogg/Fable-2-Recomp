#!/usr/bin/env python3
"""coop_stability.py - feature 001 T044: stability run. mod-a hosts a new co-op
campaign (starter save), mod-b joins, then both heroes keep moving near the
Gypsy camp for --minutes (default 120). Every 5 min: both processes alive,
partner still in the world, no crash line in either log, working-set memory
recorded. Every 30 min: a pause-menu save on the host. Screenshots every 15 min.
Stops early (cleanly, results written) when D:/Fable2-LAN/tmp/stop-stability
exists, so the PC can be handed back.
Evidence: research/001-evidence/stability/ (under D:/Fable2-LAN).
  python tools/coop_stability.py [--minutes 120]
"""

import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402
import coop_launch19 as L  # noqa: E402  (shared helpers: note, expect, shots, ...)

OUT = r"D:\Fable2-LAN\research\001-evidence\stability"
STOP = r"D:\Fable2-LAN\tmp\stop-stability"
CRASH_RE = r"Unhandled guest|access violation|FATAL|Assertion failed|crash dump"


def working_set_mb(pid):
    r = subprocess.run(["powershell", "-NoProfile", "-Command",
                        f"(Get-Process -Id {pid} -ErrorAction SilentlyContinue).WorkingSet64"],
                       capture_output=True, text=True)
    try:
        return int(r.stdout.strip()) // (1024 * 1024)
    except ValueError:
        return -1


def alive(pid):
    return working_set_mb(pid) >= 0


def walk(r, step):
    # A small square near the camp; no A (talk/use), so no dialogs open.
    for button, hold in (("StickUp", 2200), ("StickLeft", 700), ("StickDown", 2200), ("StickRight", 700)):
        r.press(button, hold)
        time.sleep(hold / 1000 + 0.3)
    if step % 3 == 0:
        r.press("X", 150)  # a swing now and then


def pause_save(a):
    a.press("Start", 250)
    time.sleep(3)
    a.press("Up", 250)
    time.sleep(1.5)
    a.press("A", 250)
    time.sleep(2)
    a.press("A", 250)
    time.sleep(2.5)
    a.press("A", 250)
    time.sleep(8)
    a.press("B", 250)
    time.sleep(1)
    a.press("B", 250)
    time.sleep(2)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--minutes", type=float, default=120)
    args = ap.parse_args()
    L.OUT = OUT
    os.makedirs(OUT, exist_ok=True)
    if os.path.exists(STOP):
        os.remove(STOP)
    note = L.note

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
    time.sleep(2)
    b.press("X", 250)
    time.sleep(3)
    a.press("X", 250)
    time.sleep(3)
    L.select_row(a, "a", "Start")
    a.press("A", 250)
    L.expect(a, "a: Start accepted", r"\[coop\] host start", 0, 10)
    L.expect(a, "a: host world up", r"host world up \(starter save", 0, 240)
    if not L.expect(a, "player 2 in the world", r"henchman spawned", 0, 180):
        note("FAIL", "no join: stability run not started")
        a.stop()
        b.stop()
        return 1

    start = time.time()
    end = start + args.minutes * 60
    next_check = start
    next_shot = start
    next_save = start + 30 * 60
    step = 0
    failed = None
    mem = []
    while time.time() < end and not failed:
        if os.path.exists(STOP):
            note("INFO", "stop file found: ending early")
            break
        now = time.time()
        if now >= next_check:
            next_check = now + 5 * 60
            mins = (now - start) / 60
            wa, wb = working_set_mb(a.pid), working_set_mb(b.pid)
            mem.append((round(mins, 1), wa, wb))
            note("INFO", f"{mins:6.1f} min: a {wa} MB, b {wb} MB")
            if wa < 0 or wb < 0:
                failed = f"process gone at {mins:.1f} min (a {wa}, b {wb})"
                break
            for r, who in ((a, "a"), (b, "b")):
                bad = L.grep_log(r, CRASH_RE, 3)
                if bad:
                    failed = f"{who} log: {bad[-1][:200]}"
            if L.count(a, r"partner .* lost") > 0:
                failed = failed or f"partner lost at {mins:.1f} min"
        if now >= next_shot:
            next_shot = now + 15 * 60
            tag = f"{int((now - start) / 60):03d}min"
            L.shots(a, f"a_{tag}")
            L.shots(b, f"b_{tag}")
        if now >= next_save:
            next_save = now + 30 * 60
            n = L.count(a, r"player 2 file written")
            pause_save(a)
            L.expect(a, f"save at {int((now - start) / 60)} min wrote player 2", r"player 2 file written", n, 20)
        try:
            walk(a, step)
            walk(b, step)
        except OSError as e:
            failed = f"control connection lost: {e}"
        step += 1

    mins = (time.time() - start) / 60
    if failed:
        note("FAIL", f"stability: {failed}")
    note("PASS" if not failed else "FAIL", f"stability: {mins:.1f} min of {args.minutes:.0f} with both players")
    with open(os.path.join(OUT, "memory.tsv"), "w", encoding="utf-8") as f:
        f.write("minutes\ta_MB\tb_MB\n" + "".join(f"{m}\t{x}\t{y}\n" for m, x, y in mem))
    with open(os.path.join(OUT, "stability_results.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(L.results) + "\n")
    a.stop()
    b.stop()
    note("INFO", "both closed; rig off")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
