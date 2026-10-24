#!/usr/bin/env python3
"""coop_launch3.py - feature 001 rig launch 3: mod-a (host, tracing) and mod-b
(partner) started once, one session. Steps in order, each recorded
PASS/FAIL/INFO; a failure is noted and the run goes on.

  1 lobby with both: a New Game -> Co-op (host lobby); b New Game -> Co-op ->
    "Join TestA's co-op campaign"; b ready; a ready; a Start -> a's world ->
    coop_go -> b joins (JoinLanPeer) -> player 2 spawns
  2 T017: luafile coop_capture2 on a (v2 capture + reparse)
  3 P4b: trace snapshots around a pause-menu "Save and Continue" on a
  4 P6: close mod-b -> a detects the loss, QuitToFrontEnd, continue lobby
  5 P2: a New Game -> Solo -> hero card -> skip to QC060 (state before/after)
Evidence: D:\\Fable2-LAN\\research\\001-evidence\\launch3\\.
"""

import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\launch3"
G = r"D:\Fable2-LAN\tools\gamectl.ps1"
results = []


def note(kind, text):
    results.append(f"{kind} {text}")
    print(kind, text, flush=True)


def ps(*args):
    return subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", G,
                           *map(str, args)], capture_output=True, text=True).stdout.strip()


def shots(r, name):
    try:
        r.shot(os.path.join(OUT, f"{name}_frame.png"))
    except Exception as e:  # noqa: BLE001
        note("INFO", f"{name}: renderer shot failed: {e}")
    ps("sshot", r.pid, os.path.join(OUT, f"{name}_screen.png"))


def count(r, pattern):
    return len(re.findall(pattern, coop_rig.read_shared(r.log)))


def expect(r, step, pattern, before, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if count(r, pattern) > before:
            note("PASS", step)
            return True
        time.sleep(0.5)
    note("FAIL", f"{step} (no /{pattern}/ in {timeout:.0f}s)")
    return False


def console(r, command):
    ps("key", r.pid, "Oem3")
    time.sleep(1)
    ps("type", r.pid, command)
    time.sleep(1)
    ps("key", r.pid, "Oem3")
    time.sleep(1)


def grep_log(r, pattern, limit=60):
    return [l for l in coop_rig.read_shared(r.log).splitlines() if re.search(pattern, l)][-limit:]


def past_title(r, who):
    deadline = time.time() + 240
    while time.time() < deadline and r.state() not in ("PressAScreen", "MainMenu", "MainMenuMovie"):
        time.sleep(2)
    time.sleep(5)
    n = count(r, r"title passed")
    r.press("A", 250)
    if not expect(r, f"{who}: title A passes to the menu", r"title passed", n, 8):
        r.press("A", 250)
        expect(r, f"{who}: title A passes (2nd try)", r"title passed", n, 8)
    time.sleep(4)


def open_coop(r, who):
    n = count(r, r"\[lobby\] open chooser")
    r.press("A", 250)
    expect(r, f"{who}: chooser", r"\[lobby\] open chooser", n)
    time.sleep(1.5)
    r.press("Down", 250)
    time.sleep(1.2)
    n = count(r, r"\[lobby\] open host")
    r.press("A", 250)
    expect(r, f"{who}: co-op lobby", r"\[lobby\] open host", n)
    time.sleep(3)


def main() -> int:
    os.makedirs(OUT, exist_ok=True)
    a = coop_rig.Instance("a")
    a.start(trace=True)
    b = coop_rig.Instance("b")
    b.start()
    note("INFO", f"mod-a pid {a.pid} port {a.port} log {a.log}; mod-b pid {b.pid} port {b.port} log {b.log}")
    past_title(a, "a")
    past_title(b, "b")

    # --- 1 lobby with both players ---------------------------------------------------------
    open_coop(a, "a")
    open_coop(b, "b")
    time.sleep(3)  # b's lobby list refreshes (4 Hz tick, lobby datagrams every few seconds)
    shots(b, "1_b_lobby_list")
    n = count(b, r"joining TestA's lobby as partner")
    b.press("A", 250)  # row 0: "Join TestA's co-op campaign"
    expect(b, "b: joins TestA's lobby as partner", r"joining TestA's lobby as partner", n)
    time.sleep(2)
    b.press("X", 250)  # ready
    time.sleep(3)
    a.press("X", 250)  # ready
    time.sleep(3)
    shots(a, "1_a_lobby_both_ready")
    shots(b, "1_b_lobby_seated_ready")
    for l in grep_log(a, r"\[lobby\] view")[-2:] + grep_log(b, r"\[lobby\] view")[-2:]:
        note("INFO", l[l.find("[lobby]"):][:260])
    # Host rows now: Childhood, Ready, Start, Back -> Start is two down.
    a.press("Down", 250)
    time.sleep(1)
    a.press("Down", 250)
    time.sleep(1)
    n = count(a, r"\[coop\] host start")
    a.press("A", 250)
    expect(a, "a: Start accepted", r"\[coop\] host start", n)
    n_go = count(a, r"host world up")
    expect(a, "a: host world up, coop_go published", r"host world up", n_go, 240)
    n = count(b, r"coop_go received")
    expect(b, "b: coop_go received, joining", r"coop_go received", n, 30)
    spawned = count(a, r"henchman spawned")
    for _ in range(8):
        if count(a, r"henchman spawned") > spawned:
            break
        b.press("A", 300)
        time.sleep(4)
    expect(a, "player 2 spawned in a's world", r"henchman spawned", spawned, 120)
    expect(a, "a: session sees player 2", r"player 2 is in the world", 0, 30)
    time.sleep(10)
    shots(a, "1_a_world_with_p2")
    shots(b, "1_b_in_a_world")

    # --- 2 T017 ---------------------------------------------------------------------------------
    n = count(a, r"\[coop\] reparse")
    console(a, "luafile coop_capture2")
    expect(a, "T017: capture2 ran", r"\[coop\] reparse", n, 20)
    for l in grep_log(a, r"capture2|reparse|COOPHERO:v2", 8):
        note("INFO", l[l.find("[lua]"):][:400])

    # --- 3 P4b: trace a pause-menu save -------------------------------------------------------
    s0 = a.snap(os.path.join(OUT, "3_save_summary_before.txt"))
    n = count(a, r"\[content\] close root=Save")
    a.press("Start", 250)
    time.sleep(3)
    a.press("Up", 250)
    time.sleep(1.5)
    a.press("A", 250)  # Save -> submenu, "Save and Continue" selected
    time.sleep(2)
    a.press("A", 250)  # Save and Continue
    time.sleep(2.5)
    shots(a, "3_save_confirm")
    a.press("A", 250)  # Yes
    expect(a, "P4b: save closed the slot", r"\[content\] close root=Save", n, 30)
    time.sleep(3)
    s1 = a.snap(os.path.join(OUT, "3_save_summary_after.txt"))
    with open(os.path.join(OUT, "3_save_trace_diff.txt"), "w", encoding="utf-8") as f:
        for new, d, name in coop_rig.diff_summaries(s0, s1, 400):
            f.write(f"{'NEW ' if new else '    '}{d:>9}  {name}\n")
    a.press("B", 250)
    time.sleep(1)
    a.press("B", 250)
    time.sleep(2)
    for l in grep_log(a, r"\[content\]", 6):
        note("INFO", l[l.find("[content]"):][:200])

    # --- 4 P6: partner loss -------------------------------------------------------------------
    n_lost = count(a, r"partner .* lost")
    b.stop()
    note("INFO", "P6: mod-b closed")
    expect(a, "P6: a detects the partner loss", r"partner .* lost", n_lost, 30)
    expect(a, "P6: QuitToFrontEnd ran", r"QuitToFrontEnd: true", 0, 15)
    time.sleep(20)
    shots(a, "4_a_after_loss")
    back = expect(a, "P6: a back at the front end, continue lobby", r"back at the front end", 0, 90)
    time.sleep(3)
    shots(a, "4_a_continue_lobby")

    # --- 5 P2 on a -----------------------------------------------------------------------------
    if back:
        a.press("B", 250)  # continue lobby -> chooser
        time.sleep(1.5)
        a.press("B", 250)  # chooser -> menu
        time.sleep(2)
    else:
        console(a, "luafile coop_quit")
        time.sleep(45)
    n_title, n_ch = count(a, r"title passed"), count(a, r"\[lobby\] open chooser")
    a.press("A", 250)
    time.sleep(3)
    if count(a, r"title passed") > n_title:
        note("INFO", "P2: at the title first; A again for the chooser")
        time.sleep(3)
        a.press("A", 250)
    if expect(a, "P2: chooser", r"\[lobby\] open chooser", n_ch, 8):
        time.sleep(1.5)
        a.press("A", 250)  # Solo
        time.sleep(25)
        shots(a, "5_hero_cards")
        a.press("A", 250)  # boy card
        time.sleep(5)
        a.press("A", 250)  # confirm, if asked
        time.sleep(120)
        shots(a, "5_childhood")
        n = count(a, r"P2STATE\|")
        console(a, "luafile coop_p2_state")
        expect(a, "P2: state before the skip", r"P2STATE\|", n, 15)
        n = count(a, r"P2SKIP\|requested")
        console(a, "luafile coop_p2_skip")
        expect(a, "P2: skip requested", r"P2SKIP\|requested", n, 15)
        time.sleep(30)
        shots(a, "5_after_skip_30s")
        time.sleep(60)
        shots(a, "5_after_skip_90s")
        time.sleep(90)
        shots(a, "5_after_skip_180s")
        n = count(a, r"P2STATE\|")
        console(a, "luafile coop_p2_state")
        expect(a, "P2: state after the skip", r"P2STATE\|", n, 15)
    for l in grep_log(a, r"\[lua\] (P2STATE|P2SKIP)|Skipping|NewBeginning", 20):
        note("INFO", l[l.find("[lua]"):][:400])

    with open(os.path.join(OUT, "launch3_results.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(results) + "\n")
    with open(os.path.join(OUT, "launch3_a_log_extract.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(grep_log(a, r"\[coop\]|\[lobby\]|\[content\]|P2|XSession|henchman|LAN lobby", 500)))
    with open(os.path.join(OUT, "launch3_b_log_extract.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(grep_log(b, r"\[coop\]|\[lobby\]|\[content\]|XSession|LAN lobby", 300)))
    a.stop()
    note("INFO", "both closed; rig off")
    return 0


if __name__ == "__main__":
    sys.exit(main())
