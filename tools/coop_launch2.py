#!/usr/bin/env python3
"""coop_launch2.py - feature 001 rig launch 2: mod-a started fresh (tracing on),
mod-b already open at the main menu. One session, steps in order, each recorded
PASS/FAIL/INFO; a failure is noted and the run goes on.

  T018 co-op half (a): title -> New Game -> chooser -> Co-op -> host lobby -> B -> B
  a: Continue into its world (host)
  P3 (b): F9 at the main menu (join without loading a save); fallback Continue + F9
  P5 (a): luafile coop_p5 twice (both purses, +100 to player 2)
  P6: close mod-b, trace diff and log on a
  P2 (a): quit to the menu, New Game -> Solo -> hero card -> skip to QC060, state before/after
Evidence: D:\\Fable2-LAN\\research\\001-evidence\\launch2\\.
"""

import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\launch2"
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


def grep_log(r, pattern, limit=40):
    return [l for l in coop_rig.read_shared(r.log).splitlines() if re.search(pattern, l)][-limit:]


def wait_world(r, before_close, timeout=240):
    ok = expect(r, f"{r.name}: save loaded", r"\[content\] close root=Save", before_close, timeout)
    time.sleep(30)
    return ok


def main() -> int:
    os.makedirs(OUT, exist_ok=True)
    b = coop_rig.Instance("b").attach()
    a = coop_rig.Instance("a")
    a.start(trace=True)
    note("INFO", f"mod-a pid {a.pid} port {a.port} log {a.log}; mod-b pid {b.pid} port {b.port} log {b.log}")

    # --- T018 co-op half on a -------------------------------------------------------
    deadline = time.time() + 240
    while time.time() < deadline and a.state() not in ("PressAScreen", "MainMenu", "MainMenuMovie"):
        time.sleep(2)
    time.sleep(5)
    n = count(a, r"title passed")
    a.press("A", 250)
    if not expect(a, "a: title A passes to the menu", r"title passed", n, 8):
        a.press("A", 250)
        expect(a, "a: title A passes (2nd try)", r"title passed", n, 8)
    time.sleep(4)
    n = count(a, r"\[lobby\] open chooser")
    a.press("A", 250)
    expect(a, "a: A on New Game opens the chooser", r"\[lobby\] open chooser", n)
    time.sleep(1.5)
    a.press("Down", 250)
    time.sleep(1.5)
    shots(a, "t018_a_chooser_coop")
    n = count(a, r"\[lobby\] open host")
    a.press("A", 250)
    expect(a, "a: Co-op opens the host lobby", r"\[lobby\] open host", n)
    time.sleep(3)
    shots(a, "t018_a_host_lobby")
    n = count(a, r"\[lobby\] open chooser")
    a.press("B", 250)
    expect(a, "a: B in the lobby returns to the chooser", r"\[lobby\] open chooser", n)
    time.sleep(1.5)
    a.press("B", 250)
    time.sleep(2)
    shots(a, "t018_a_back_to_menu")

    # --- a: Continue into its world ---------------------------------------------------
    n = count(a, r"\[content\] close root=Save")
    a.press("Down", 250)
    time.sleep(1.2)
    a.press("A", 250)
    wait_world(a, n)
    shots(a, "a_world")

    # --- P3: b joins from the main menu --------------------------------------------------
    b_join = count(b, r"LAN lobby: joining")
    a_join = count(a, r"XSessionJoinRemote")
    b.shot(os.path.join(OUT, "p3_b_menu_before_frame.png"))
    ps("key", b.pid, "F9")
    joined = expect(a, "P3: b joins from the main menu (a sees XSessionJoinRemote)",
                    r"XSessionJoinRemote", a_join, 60)
    note("INFO", f"P3: b log 'joining' lines new: {count(b, r'LAN lobby: joining') - b_join}")
    shots(b, "p3_b_after_f9_menu")
    if not joined:
        note("INFO", "P3: fallback - b Continues into its own world, then F9")
        n = count(b, r"\[content\] close root=Save")
        b.press("Down", 250)
        time.sleep(1.2)
        b.press("A", 250)
        wait_world(b, n)
        ps("key", b.pid, "F9")
        expect(a, "P3 fallback: b joins from its world", r"XSessionJoinRemote", a_join, 90)
    # Accept prompts on both until player 2 spawns (as tools\rig_run.ps1 does).
    spawned_before = count(a, r"henchman spawned")
    for _ in range(6):
        b.press("A", 300)
        time.sleep(3)
        a.press("A", 300)
        time.sleep(3)
        if count(a, r"henchman spawned") > spawned_before:
            break
    expect(a, "P3: player 2 spawned in a's world", r"henchman spawned", spawned_before, 90)
    time.sleep(10)
    shots(a, "p3_a_joined")
    shots(b, "p3_b_joined")

    # --- P5: gold ---------------------------------------------------------------------------
    n = count(a, r"P5\|")
    console(a, "luafile coop_p5")
    expect(a, "P5: first purse readout", r"P5\|", n, 15)
    time.sleep(8)
    shots(a, "p5_a_after_give")
    shots(b, "p5_b_after_give")
    console(a, "luafile coop_p5")
    time.sleep(5)
    for l in grep_log(a, r"P5\|"):
        note("INFO", l[l.find("P5|"):])

    # --- P6: partner loss ---------------------------------------------------------------------
    before = a.snap(os.path.join(OUT, "p6_summary_before.txt"))
    lines_before = len(coop_rig.read_shared(a.log).splitlines())
    b.stop()
    note("INFO", "P6: mod-b closed (taskkill)")
    time.sleep(25)
    after = a.snap(os.path.join(OUT, "p6_summary_after.txt"))
    shots(a, "p6_a_after_partner_loss")
    new_lines = coop_rig.read_shared(a.log).splitlines()[lines_before:]
    with open(os.path.join(OUT, "p6_a_log_after_kill.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(l for l in new_lines if not re.search(r"vsync present|APC|WAIT_REG_MEM", l)))
    with open(os.path.join(OUT, "p6_trace_diff.txt"), "w", encoding="utf-8") as f:
        for new, d, name in coop_rig.diff_summaries(before, after, 300):
            f.write(f"{'NEW ' if new else '    '}{d:>9}  {name}\n")
    note("INFO", f"P6: {len(new_lines)} log lines after the kill; trace diff written")

    # --- P2: skip to adulthood on a -------------------------------------------------------------
    a.press("Start", 250)
    time.sleep(3)
    shots(a, "p2_pause")
    a.press("Up", 250)
    time.sleep(1)
    a.press("A", 250)  # Save -> expands Save and Continue / Save and Quit / Quit and Don't Save
    time.sleep(1.5)
    a.press("Down", 250)
    time.sleep(1)
    a.press("Down", 250)
    time.sleep(1)
    shots(a, "p2_quit_selected")
    a.press("A", 250)
    time.sleep(2)
    shots(a, "p2_quit_confirm")
    a.press("A", 250)  # confirm
    time.sleep(40)
    shots(a, "p2_after_quit")
    n_title, n_ch = count(a, r"title passed"), count(a, r"\[lobby\] open chooser")
    a.press("A", 250)
    time.sleep(3)
    if count(a, r"title passed") > n_title:
        note("INFO", "P2: the quit returned to the title; pressing A again for the chooser")
        time.sleep(3)
        a.press("A", 250)
    expect(a, "P2: chooser after quitting to the menu", r"\[lobby\] open chooser", n_ch, 8)
    time.sleep(1.5)
    a.press("A", 250)  # Solo
    time.sleep(25)
    shots(a, "p2_hero_cards")
    a.press("A", 250)  # first (boy) card
    time.sleep(5)
    shots(a, "p2_after_card")
    a.press("A", 250)  # confirm, if asked
    time.sleep(120)
    shots(a, "p2_childhood_start")
    n = count(a, r"P2STATE\|")
    console(a, "luafile coop_p2_state")
    expect(a, "P2: state before the skip", r"P2STATE\|", n, 15)
    n = count(a, r"P2SKIP\|requested")
    console(a, "luafile coop_p2_skip")
    expect(a, "P2: skip requested", r"P2SKIP\|requested", n, 15)
    for t in (30, 90, 180):
        time.sleep(t - (0 if t == 30 else prev))
        prev = t
        shots(a, f"p2_after_skip_{t}s")
    n = count(a, r"P2STATE\|")
    console(a, "luafile coop_p2_state")
    expect(a, "P2: state after the skip", r"P2STATE\|", n, 15)
    for l in grep_log(a, r"P2STATE\||P2SKIP\||Skipping QC|Skipping gameflow|QC060"):
        note("INFO", l[30:330])

    with open(os.path.join(OUT, "launch2_results.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(results) + "\n")
    with open(os.path.join(OUT, "launch2_a_log_extract.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(grep_log(a, r"\[coop\]|\[lobby\]|\[content\]|P2|P5|XSession|henchman|LAN lobby", 400)))
    a.stop()
    note("INFO", "mod-a closed; rig off")
    return 0


if __name__ == "__main__":
    sys.exit(main())
