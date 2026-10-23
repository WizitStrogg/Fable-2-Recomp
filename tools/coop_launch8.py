#!/usr/bin/env python3
"""coop_launch8.py - feature 001 rig launch 8: mod-a (host, tracing) and mod-b
(partner), each started once, one session; each step PASS/FAIL/INFO, a failure
is noted and the run goes on.
  1 lobby with both (host hero set to Female), both ready
  2 Start -> stock New Game -> girl hero card -> childhood -> skip to QC060 (P2)
    -> coop_go -> b joins -> player 2 spawns
  3 T017 capture2   4 P4b save trace   5 P6 partner loss
Evidence: research/001-evidence/launch8/ (under D:/Fable2-LAN).
"""

import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\launch8"
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


VIEW_RE = re.compile(r"\[lobby\] view: (.*)$")


def view_rows(r):
    """Rows of the last panel view in r's log: [(label, enabled, selected)]."""
    views = [m.group(1) for l in coop_rig.read_shared(r.log).splitlines() if (m := VIEW_RE.search(l))]
    if not views:
        return []
    rows = []
    for part in views[-1].split("|"):
        if part.startswith("R") and len(part) >= 3:
            rows.append((part[3:], part[1] == "1", part[2] == "1"))
    return rows


def select_row(r, who, label_prefix, tries=8):
    """Moves the panel cursor to the row starting with label_prefix."""
    for _ in range(tries):
        rows = view_rows(r)
        target = next((i for i, x in enumerate(rows) if x[0].startswith(label_prefix)), None)
        cur = next((i for i, x in enumerate(rows) if x[2]), None)
        if target is None or cur is None:
            time.sleep(1)
            continue
        if target == cur:
            return True
        r.press("Down" if target > cur else "Up", 250)
        time.sleep(1.2)
    note("FAIL", f"{who}: could not select '{label_prefix}' (rows {view_rows(r)})")
    return False


def main() -> int:
    os.makedirs(OUT, exist_ok=True)
    a = coop_rig.Instance("a")
    a.start(trace=True)
    b = coop_rig.Instance("b")
    b.start()
    note("INFO", f"mod-a pid {a.pid} port {a.port} log {a.log}; mod-b pid {b.pid} port {b.port} log {b.log}")
    past_title(a, "a")
    past_title(b, "b")

    # --- 1 lobby with both players, host hero female ------------------------------------
    open_coop(a, "a")
    open_coop(b, "b")
    time.sleep(3)
    if select_row(b, "b", "Join TestA"):
        n = count(b, r"joining TestA's lobby as partner")
        b.press("A", 250)
        expect(b, "b: joins TestA's lobby as partner", r"joining TestA's lobby as partner", n)
    time.sleep(2)
    b.press("X", 250)  # ready
    time.sleep(3)
    if select_row(a, "a", "Your hero"):
        a.press("A", 250)  # Male -> Female
        time.sleep(1.5)
    a.press("X", 250)  # ready
    time.sleep(3)
    shots(a, "1_a_lobby_both_ready")
    shots(b, "1_b_lobby_seated_ready")
    for l in grep_log(a, r"\[lobby\] view")[-1:] + grep_log(b, r"\[lobby\] view")[-1:]:
        note("INFO", l[l.find("[lobby]"):][:300])

    # --- 2 Start: stock New Game -> hero card -> childhood -> skip -> go --------------------
    select_row(a, "a", "Start")
    n_go_b = count(b, r"coop_go received")
    n_join_b = count(b, r"partner: in the host's world")
    n = count(a, r"\[coop\] host start")
    a.press("A", 250)
    expect(a, "a: Start accepted", r"\[coop\] host start", n)
    expect(a, "a: Start's A passed to the stock New Game", r"A passed to the stock New Game", 0, 10)
    time.sleep(25)
    shots(a, "2_a_hero_cards")
    expect(a, "a: hero card picked from the lobby (girl)", r"picking the girl hero card", 0, 30)
    time.sleep(10)
    shots(a, "2_a_after_card")
    expect(a, "a: skip wrapper installed", r"skip: wrapper installed", 0, 120)
    for l in grep_log(a, r"skip: wrapper installed|redirected to QC060", 2):
        note("INFO", l[l.find("[lua]"):][:200])
    expect(a, "a: new game start redirected to QC060", r"redirected to QC060", 0, 60)
    expect(a, "a: QC060 New Beginning started", r"QC060_NewBeginning Starting", 0, 240)
    # QC060 opening: autosave, "NewBeginnings" movie, the co-op screen. A on
    # the host every 20 s moves the movie / co-op screen on (a no-op otherwise).
    for i in range(9):
        time.sleep(20)
        shots(a, f"2_a_qc060_{(i + 1) * 20:03d}s")
        if count(a, r"host world up") and count(b, r"partner: join attempt"):
            break
        a.press("A", 250)
    n = count(a, r"P2STATE\|")
    console(a, "luafile coop_p2_state")
    expect(a, "P2: state after the skip", r"P2STATE\|", n, 20)
    expect(a, "a: host world up, coop_go published", r"host world up", 0, 120)
    expect(b, "b: coop_go received, joining", r"coop_go received", n_go_b, 30)
    spawned = count(a, r"henchman spawned")
    if not expect(a, "player 2 spawned in a's world", r"henchman spawned", spawned, 330):
        # Leave both running for a partner-only retry (coop_partner_retry.py).
        with open(os.path.join(OUT, "launch8_results.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(results) + "\n")
        for l in grep_log(b, r"partner: (go|join attempt|in the host|no join)", 12):
            note("INFO", l[l.find("[coop]"):][:200])
        note("INFO", f"stopped before P6; mod-a pid {a.pid} and mod-b pid {b.pid} left running")
        return 1
    expect(a, "a: session sees player 2", r"player 2 is in the world", 0, 30)
    expect(b, "b: in the host's world", r"partner: in the host's world", n_join_b, 30)
    time.sleep(10)
    shots(a, "2_a_world_with_p2")
    shots(b, "2_b_in_a_world")

    # --- 3 T017 -------------------------------------------------------------------------------
    n = count(a, r"\[coop\] reparse")
    console(a, "luafile coop_capture2")
    expect(a, "T017: capture2 ran", r"\[coop\] reparse", n, 20)
    for l in grep_log(a, r"capture2|reparse|COOPHERO:v2", 8):
        note("INFO", l[l.find("[lua]"):][:500])

    # --- 4 P4b: trace a pause-menu save ---------------------------------------------------
    s0 = a.snap(os.path.join(OUT, "4_save_summary_before.txt"))
    n = count(a, r"\[content\] close root=Save")
    note("INFO", f"P4b: closes before the save: {n}")
    a.press("Start", 250)
    time.sleep(3)
    a.press("Up", 250)
    time.sleep(1.5)
    a.press("A", 250)
    time.sleep(2)
    a.press("A", 250)
    time.sleep(2.5)
    shots(a, "4_save_confirm")
    a.press("A", 250)
    expect(a, "P4b: save closed the slot", r"\[content\] close root=Save", n, 30)
    time.sleep(3)
    s1 = a.snap(os.path.join(OUT, "4_save_summary_after.txt"))
    with open(os.path.join(OUT, "4_save_trace_diff.txt"), "w", encoding="utf-8") as f:
        for new, d, name in coop_rig.diff_summaries(s0, s1, 400):
            f.write(f"{'NEW ' if new else '    '}{d:>9}  {name}\n")
    a.press("B", 250)
    time.sleep(1)
    a.press("B", 250)
    time.sleep(2)
    for l in grep_log(a, r"\[content\]", 4):
        note("INFO", l[l.find("[content]"):][:200])

    # --- 5 P6: partner loss -----------------------------------------------------------------
    b.stop()
    note("INFO", "P6: mod-b closed")
    expect(a, "P6: a detects the partner loss", r"partner .* lost", 0, 30)
    expect(a, "P6: autosave ran", r"AutoSaveGame: ", 0, 15)
    expect(a, "P6: autosave wrote the slot", r"\[content\] close root=Save", 0, 20)
    expect(a, "P6: QuitToFrontEnd ran", r"QuitToFrontEnd: ", 0, 30)
    for l in grep_log(a, r"AutoSaveGame: |QuitToFrontEnd: ", 2):
        note("INFO", l[l.find("[lua]"):][:200])
    time.sleep(20)
    shots(a, "5_a_after_loss")
    expect(a, "P6: a back at the front end, continue lobby", r"back at the front end", 0, 90)
    time.sleep(3)
    shots(a, "5_a_continue_lobby")

    with open(os.path.join(OUT, "launch8_results.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(results) + "\n")
    with open(os.path.join(OUT, "launch8_a_log_extract.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(grep_log(a, r"\[coop\]|\[lobby\] (open|host|join|coop_go)|\[content\]|P2STATE|QC0|Skipping|XSession|henchman|LAN lobby", 500)))
    with open(os.path.join(OUT, "launch8_b_log_extract.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(grep_log(b, r"\[coop\]|\[lobby\] (open|join|coop_go)|\[content\]|XSession|LAN lobby", 300)))
    a.stop()
    note("INFO", "both closed; rig off")
    return 0


if __name__ == "__main__":
    sys.exit(main())
