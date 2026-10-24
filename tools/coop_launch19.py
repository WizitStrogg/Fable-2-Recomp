#!/usr/bin/env python3
"""coop_launch19.py - feature 001 rig launch 19: mod-a (host, tracing) and mod-b
(partner), each started once, one session; each step PASS/FAIL/INFO, a failure
is noted and the run goes on.
  1 lobby with both (host hero set to Female), both ready
  2 Start -> stock New Game -> girl hero card -> childhood -> skip to QC060 (P2)
    -> coop_go -> b joins -> player 2 spawns
  3 T017 capture2   4 P4b save (campaign marker kept)   5 P6 partner loss
  T030/T031 player 2 file written at saves and restored on continue; 7 T027 host loss
  T028: mod-c (TestC, a stranger) presses F9 the moment the host world is up; refused
  6 T025/T026: a restarts, Continue on the campaign save opens its lobby, b restarts
    and finds it, both ready, Start reloads the same slot, b rejoins
Evidence: research/001-evidence/launch19/ (under D:/Fable2-LAN).
"""

import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\launch19"
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


def tree_hash(root):
    import hashlib
    h = hashlib.sha256()
    for d, _, files in sorted(os.walk(root)):
        for name in sorted(files):
            p = os.path.join(d, name)
            h.update(p.encode())
            with open(p, "rb") as f:
                h.update(f.read())
    return h.hexdigest()[:16]


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
    c = coop_rig.Instance("c")
    c_slot = r"D:\Fable2-LAN\mod-c\saves"
    c_hash0 = tree_hash(c_slot)
    c.start()
    note("INFO", f"mod-c pid {c.pid} log {c.log}; mod-c saves hash {c_hash0}")
    note("INFO", f"mod-a pid {a.pid} port {a.port} log {a.log}; mod-b pid {b.pid} port {b.port} log {b.log}")
    past_title(a, "a")
    past_title(b, "b")
    past_title(c, "c")

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
    b.cmd({"cmd": "cvar", "name": "coop_partner_join_delay_ms", "value": "60000"})
    select_row(a, "a", "Start")
    n_go_b = count(b, r"coop_go received")
    n_join_b = count(b, r"partner: in the host's world")
    n = count(a, r"\[coop\] host start")
    a.press("A", 250)
    expect(a, "a: Start accepted", r"\[coop\] host start", n)
    expect(a, "a: starter save slot created", r"starter save: created", 0, 10)
    for l in grep_log(a, r"starter save: created|host start: campaign", 2):
        note("INFO", l[l.find("[coop]"):][:220])
    slot = None
    for l in grep_log(a, r"starter save: created", 1):
        m = re.search(r"created (.*?) for XUID", l)
        slot = m.group(1) if m else None
    marker = os.path.join(slot, "coop_campaign.toml") if slot else ""
    note("PASS" if marker and os.path.exists(marker) else "FAIL", f"a: campaign marker written ({marker})")
    cid = None
    if marker and os.path.exists(marker):
        txt = open(marker, encoding="utf-8").read()
        m = re.search(r'campaign_id = "([0-9A-F]+)"', txt)
        cid = m.group(1) if m else None
        with open(os.path.join(OUT, "2_marker.toml"), "w", encoding="utf-8") as f:
            f.write(txt)
    expect(a, "a: Continue loads the starter slot", r"\[content\] create root=Save file=Hero\d+ type=00000001 flags=3", 0, 30)
    time.sleep(40)
    shots(a, "2_a_starter_loaded")
    expect(a, "a: hero gender applied", r"hero gender: true", 0, 60)
    n = count(a, r"P2STATE\|")
    console(a, "luafile coop_p2_state")
    expect(a, "starter state", r"P2STATE\|", n, 20)
    for l in grep_log(a, r"\[lua\] P2STATE", 1):
        note("INFO", l[l.find("P2STATE"):][:300])
    shots(a, "2_a_adult_world")
    expect(a, "a: host world up, coop_go published", r"host world up \(starter save", 0, 120)
    ps("key", c.pid, "F9")  # T028: a stranger tries to join the campaign
    note("INFO", "T028: c pressed F9")
    time.sleep(4)
    expect(a, "T028: the stranger's join was refused", r"join from [0-9A-F]+ refused: this campaign's partner|join held off", 0, 40)
    for l in grep_log(a, r"refused: this campaign|join held off|friends check", 6):
        note("INFO", l[l.find("[joingate]"):][:200])
    for l in grep_log(c, r"LAN lobby|XSession", 6):
        note("INFO", "c: " + l[l.find("["):][:200])
    shots(c, "2_c_after_refusal")
    c.stop()
    c_hash1 = tree_hash(c_slot)
    note("PASS" if c_hash1 == c_hash0 else "FAIL", f"mod-c saves untouched ({c_hash1})")
    expect(b, "b: coop_go received, joining", r"coop_go received", n_go_b, 60)
    # Phase 1: gates logged, nothing forced; the partner retries every 15 s.
    a.cmd({"cmd": "cvar", "name": "coop_join_gate_log", "value": "true"})
    time.sleep(35)
    joined1 = count(a, r"XSessionJoinRemote") > 0
    note("INFO", f"phase 1 (no forcing): joined={joined1}")
    for l in grep_log(a, r"\[joingate\]", 24):
        note("INFO", l[l.find("[joingate]"):][:330])
    # The game's join screens are accepted by the mod itself now (both sides press A).
    for l in grep_log(a, r"\[joingate\] join approved|\[coop\] host: A for", 8):
        note("INFO", l[l.find("["):][:200])
    spawned = 0
    if not expect(a, "player 2 (TestB) still joins after the refusal", r"henchman spawned", spawned, 150):
        with open(os.path.join(OUT, "launch19_results.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(results) + "\n")
        note("INFO", f"stopped; mod-a pid {a.pid} and mod-b pid {b.pid} left running")
        return 1
    expect(a, "T031a: player 2 seeded from the host's starter profile", r"player 2 seeded from the host", 0, 30)
    time.sleep(10)
    shots(a, "2_a_camp_with_p2")
    shots(b, "2_b_camp")

    # --- 3 T017 -------------------------------------------------------------------------------
    n = count(a, r"\[coop\] reparse")
    console(a, "luafile coop_capture2")
    expect(a, "T017: capture2 ran", r"\[coop\] reparse", n, 20)
    for l in grep_log(a, r"capture2|reparse|COOPHERO:v2", 8):
        note("INFO", l[l.find("[lua]"):][:500])


    # --- T034 shared purse ------------------------------------------------------------------
    for l in grep_log(a, r"busy probe uses|\[coop\] host (busy|not busy)|join held off|capture waits|purse:", 10):
        note("INFO", l[l.find("[coop]") if "[coop]" in l else l.find("["):][:200])
    def purse_read():
        n = count(a, r"purse read: ")
        console(a, "luafile coop_purse_read")
        expect(a, "purse read", r"purse read: ", n, 15)
        m = re.search(r"purse read: host (-?\d+), player 2 (-?\d+)", grep_log(a, r"purse read: ", 1)[0])
        return (int(m.group(1)), int(m.group(2))) if m else (None, None)
    h0, p0 = purse_read()
    note("INFO", f"T034: before host {h0}, player 2 {p0}")
    console(a, "luafile coop_purse_add")
    time.sleep(5)
    h1, p1 = purse_read()
    note("INFO", f"T034: after +100 via player 2 and -30 by the host: host {h1}, player 2 {p1}")
    want = h0 + 70 if h0 is not None else None
    note("PASS" if h1 == want and p1 == want else "FAIL", f"T034: one shared total {want} on both purses")
    for l in grep_log(a, r"purse", 6):
        note("INFO", l[l.find("[coop]") if "[coop]" in l else 0:][:220])
    shots(a, "3_a_purse")
    shots(b, "3_b_purse")

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
        note("INFO", l[l.find("[content]"):][:330])
    note("PASS" if marker and os.path.exists(marker) else "FAIL", "P4b: campaign marker kept by the game's save")
    expect(a, "T030: player 2 file written at the pause-menu save", r"player 2 file written \(capture", 0, 20)
    p2file = os.path.join(slot, "coop_player2.txt") if slot else ""
    if p2file and os.path.exists(p2file):
        with open(os.path.join(OUT, "4_coop_player2_after_save.txt"), "w", encoding="utf-8") as f:
            f.write(open(p2file, encoding="utf-8").read())

    # --- 5 P6: partner loss -----------------------------------------------------------------
    b.stop()
    note("INFO", "P6: mod-b closed")
    expect(a, "P6: a detects the partner loss", r"partner .* lost", 0, 30)
    expect(a, "P6: autosave ran", r"AutoSaveGame: ", 0, 15)
    expect(a, "P6: autosave wrote the slot", r"\[content\] close root=Save", 0, 20)
    expect(a, "P6: QuitToFrontEnd ran", r"QuitToFrontEnd: ", 0, 30)
    expect(a, "T030: player 2 file written at the partner-loss autosave", r"player 2 file written", 1, 5)
    for l in grep_log(a, r"player 2 file|campaign saved", 6):
        note("INFO", l[l.find("[coop]"):][:200])
    for l in grep_log(a, r"AutoSaveGame: |QuitToFrontEnd: ", 2):
        note("INFO", l[l.find("[lua]"):][:330])
    time.sleep(20)
    shots(a, "5_a_after_loss")
    expect(a, "P6: a back at the front end, continue lobby", r"back at the front end", 0, 90)
    time.sleep(3)
    shots(a, "5_a_continue_lobby")

    # --- 6 T025/T026: continue the campaign ------------------------------------------------
    a.stop()
    note("INFO", "6: a closed; restarting both")
    a = coop_rig.Instance("a")
    a.start()
    b = coop_rig.Instance("b")
    b.start()
    past_title(a, "a")
    past_title(b, "b")
    a.press("Down", 250)  # New Game -> Continue
    time.sleep(1.5)
    n = count(a, r"continue: Hero\d+ is campaign")
    a.press("A", 250)
    if expect(a, "T025: Continue on the campaign save opens its lobby", r"continue: Hero\d+ is campaign", n, 10):
        for l in grep_log(a, r"continue: Hero", 1):
            note("INFO", l[l.find("[coop]"):][:220])
    time.sleep(3)
    shots(a, "6_a_campaign_lobby")
    open_coop(b, "b")
    time.sleep(3)
    shots(b, "6_b_lists_campaign")
    if select_row(b, "b", "Join TestA"):
        n = count(b, r"joining TestA's lobby as partner")
        b.press("A", 250)
        expect(b, "T026: b joins the waiting campaign", r"joining TestA's lobby as partner", n)
    time.sleep(2)
    b.press("X", 250)
    time.sleep(3)
    a.press("X", 250)
    time.sleep(3)
    for l in grep_log(a, r"\[lobby\] view")[-1:] + grep_log(b, r"\[lobby\] view")[-1:]:
        note("INFO", l[l.find("[lobby]"):][:300])
    select_row(a, "a", "Start")
    n = count(a, r"\[coop\] host start")
    n_go_b = count(b, r"coop_go received")
    a.press("A", 250)
    expect(a, "T025: Start continues the campaign", r"host start: campaign .* continuing", n, 10)
    for l in grep_log(a, r"host start: campaign", 1):
        note("INFO", l[l.find("[coop]"):][:220])
    if cid:
        note("PASS" if any(cid in l for l in grep_log(a, r"host start: campaign", 1)) else "FAIL",
             f"T025: same campaign id {cid}")
    expect(a, "T025: the campaign slot loads", r"\[content\] create root=Save file=Hero\d+ type=00000001 flags=3", 0, 40)
    expect(a, "T025: host world up", r"host world up \(starter save", 0, 180)
    expect(b, "T025: b gets coop_go", r"coop_go received", n_go_b, 60)
    n = count(a, r"henchman spawned")
    expect(a, "T025: player 2 rejoins", r"henchman spawned", n, 120)
    expect(a, "T031: player 2 restored from coop_player2.txt", r"player 2 restored from coop_player2", 0, 30)
    time.sleep(4)
    h2, p2 = purse_read()
    note("PASS" if want is not None and h2 == want and p2 == want else "FAIL",
         f"T034: after reload host {h2}, player 2 {p2} (saved total {want})")
    for l in grep_log(a, r"player 2: restoring|player 2 restored|COOPHERO:v2", 3):
        note("INFO", l[l.find("["):][:300])
    time.sleep(8)
    shots(a, "6_a_continued_with_p2")
    shots(b, "6_b_continued")
    note("PASS" if marker and os.path.exists(marker) else "FAIL", "marker still present at the end")

    # --- 7 T027: host loss, as the partner sees it -----------------------------------------
    a.stop()
    note("INFO", "7: a (host) killed")
    time.sleep(45)
    shots(b, "7_b_after_host_loss")
    for l in grep_log(b, r"\[coop\]|XSession|LAN lobby", 12):
        note("INFO", l[l.find("["):][:200])
    note("INFO", f"7: b state {b.state()}")

    with open(os.path.join(OUT, "launch19_results.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(results) + "\n")
    with open(os.path.join(OUT, "launch19_a_log_extract.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(grep_log(a, r"\[coop\]|\[lobby\] (open|host|join|coop_go)|\[content\]|P2STATE|QC0|Skipping|XSession|henchman|LAN lobby", 500)))
    with open(os.path.join(OUT, "launch19_b_log_extract.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(grep_log(b, r"\[coop\]|\[lobby\] (open|join|coop_go)|\[content\]|XSession|LAN lobby", 300)))
    a.stop()
    b.stop()
    note("INFO", "both closed; rig off")
    return 0


if __name__ == "__main__":
    sys.exit(main())
