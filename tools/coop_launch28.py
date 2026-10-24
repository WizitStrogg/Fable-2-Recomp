#!/usr/bin/env python3
"""coop_launch28.py - feature 001 rig launch 28 (about 15 min, mod-a host + mod-b partner):
item (b) step 2: the game's own join screens answered in code (coop_join_screen_skip: the
screens' action readers get "accept" (36) directly; no pad presses), twice:
  join 1 in the campaign, then the partner leaves (P6), both restart, Continue, join 2.
Checks per join: skip injections on both PCs, no pad answers ("A for the join screen"),
player 2 spawned, time from approval to spawn. The cover stays up as usual.
Evidence: research/001-evidence/menus/launch28/.
"""

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402
import coop_launch19 as L  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\menus\launch28"


def ts(line):
    m = re.search(r"\d\d:(\d\d):(\d\d)\.(\d{3})\]", line)
    return int(m.group(1)) * 60 + int(m.group(2)) + int(m.group(3)) / 1000 if m else None


def main() -> int:
    L.OUT = OUT
    os.makedirs(OUT, exist_ok=True)
    note, expect, count, grep_log = L.note, L.expect, L.count, L.grep_log

    def start_pair():
        a = coop_rig.Instance("a")
        a.start()
        b = coop_rig.Instance("b")
        b.start()
        note("INFO", f"mod-a pid {a.pid} log {a.log}; mod-b pid {b.pid} log {b.log}")
        L.past_title(a, "a")
        L.past_title(b, "b")
        return a, b

    def lobby_and_join(a, b, tag, nolayout=False):
        a.press("Down", 250)
        time.sleep(1.5)
        a.press("A", 250)
        expect(a, f"{tag}: Continue opens the campaign lobby", r"continue: Hero\d+ is campaign", 0, 10)
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
        for r in (a, b):
            r.cmd({"cmd": "cvar", "name": "coop_join_screen_skip", "value": "true"})
            r.cmd({"cmd": "cvar", "name": "coop_join_screen_probe", "value": "true"})
            r.cmd({"cmd": "cvar", "name": "coop_join_screen_nolayout", "value": "true" if nolayout else "false"})
        L.select_row(a, "a", "Start")
        a.press("A", 250)
        expect(a, f"{tag}: Start continues the campaign", r"host start: campaign .* continuing", 0, 10)
        expect(a, f"{tag}: host world up", r"host world up \(starter save", 0, 240)
        expect(a, f"{tag}: join approved", r"\[joingate\] join approved", 0, 90)
        # What the game itself draws under the cover: the renderer's own frame
        # (no overlay), about every second until player 2 spawns.
        import numpy as np
        from PIL import Image
        stock = 0
        shots_dir = os.path.join(OUT, f"{tag}_game_frames")
        os.makedirs(shots_dir, exist_ok=True)
        t0 = time.time()
        k = 0
        while count(a, r"henchman spawned") == 0 and time.time() - t0 < 90:
            for r, who in ((a, "a"), (b, "b")):
                p = os.path.join(shots_dir, f"{k:03d}_{who}.png")
                try:
                    r.shot(p)
                    im = np.asarray(Image.open(p).convert("RGB")).astype(int)
                    rr, gg, bb = im[..., 0], im[..., 1], im[..., 2]
                    parch = ((rr > 170) & (gg > 150) & (bb > 100) & (rr - bb < 110) & (rr - bb > 20)).mean()
                    stock += parch > 0.05
                except Exception:  # noqa: BLE001
                    pass
            k += 1
            time.sleep(0.8)
        note("INFO" if not nolayout else ("PASS" if stock == 0 else "FAIL"),
             f"{tag}: game frames under the cover with stock-screen parchment: {stock} of {2 * k}")
        ok = expect(a, f"{tag}: player 2 spawned (screens answered in code)", r"henchman spawned", 0, 30)
        time.sleep(4)
        for r, who in ((a, "host"), (b, "partner")):
            skips = grep_log(r, r"\[joinscreen\] skip:", 50)
            pads = grep_log(r, r"A for the join screen", 50)
            note("PASS" if skips else "FAIL", f"{tag}: {who} screens answered in code ({len(skips)} accepts)")
            note("PASS" if not pads else "FAIL", f"{tag}: {who} pressed no pad buttons for them ({len(pads)})")
        appr = grep_log(a, r"\[joingate\] join approved", 1)
        spawn = grep_log(a, r"henchman spawned", 1)
        if appr and spawn and ts(appr[0]) is not None and ts(spawn[0]) is not None:
            note("INFO", f"{tag}: approval -> spawn {ts(spawn[0]) - ts(appr[0]):.1f} s")
        for r, who in ((a, "a"), (b, "b")):
            with open(os.path.join(OUT, f"{tag}_joinscreen_{who}.txt"), "w", encoding="utf-8") as f:
                f.write("\n".join(l[l.find("["):] for l in grep_log(r, r"\[joinscreen\]|join approved|join cover|henchman spawned|A for the join", 300)) + "\n")
        return ok

    a, b = start_pair()
    ok1 = lobby_and_join(a, b, "join1")
    # The partner leaves: the host saves and goes back to the campaign lobby (P6).
    b.stop()
    expect(a, "P6: host back at the front end", r"back at the front end", 0, 120)
    a.stop()
    a, b = start_pair()
    ok2 = lobby_and_join(a, b, "join2")
    # Join 3 (experiment): the same, without building the screens' layout at all.
    b.stop()
    expect(a, "P6 (2): host back at the front end", r"back at the front end", 0, 120)
    a.stop()
    a, b = start_pair()
    ok3 = lobby_and_join(a, b, "join3_nolayout", nolayout=True)
    for r, who in ((a, "host"), (b, "partner")):
        n = len(grep_log(r, r"layout build skipped", 10))
        note("INFO", f"join3_nolayout: {who} layout builds skipped: {n}")
    note("PASS" if ok3 else "FAIL", "join3_nolayout: player 2 spawned with no layout built")
    with open(os.path.join(OUT, "launch28_results.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(L.results) + "\n")
    a.stop()
    b.stop()
    note("INFO", "both closed; rig off")
    return 0 if ok1 and ok2 else 1


if __name__ == "__main__":
    sys.exit(main())
