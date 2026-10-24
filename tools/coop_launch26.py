#!/usr/bin/env python3
"""coop_launch26.py - feature 001 rig launch 26 (about 12 min, mod-a host + mod-b partner):
  1 the menu-style lobby: screenshots of the stock main menu and of both lobbies
    (host's campaign lobby, partner seated), for a side-by-side with the stock menu;
  2 the join cover: both windows captured from the desktop every 200 ms from the host's
    go until 5 s after player 2 spawns; every frame is checked for the game's own co-op
    join screens (the cover must be up from before they exist until after they close).
The windows are moved side by side (mod-a at x 0, mod-b at x 1290) for the desktop capture.
Evidence: research/001-evidence/menus/launch26/ (under D:/Fable2-LAN).
"""

import ctypes
import ctypes.wintypes as wt
import os
import sys
import threading
import time

from PIL import ImageGrab

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_rig  # noqa: E402
import coop_launch19 as L  # noqa: E402

OUT = r"D:\Fable2-LAN\research\001-evidence\menus\launch26"
user32 = ctypes.windll.user32


def hwnd_for(pid):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, _):
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(h) and user32.GetWindowTextLengthW(h) > 0:
            found.append(h)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def place(pid, x):
    h = hwnd_for(pid)
    if h:
        user32.SetWindowPos(h, 0, x, 0, 0, 0, 0x0001 | 0x0004 | 0x0010)  # NOSIZE|NOZORDER|NOACTIVATE
    return h


def rect(h):
    r = wt.RECT()
    user32.GetWindowRect(h, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


class Capture(threading.Thread):
    def __init__(self, ha, hb, folder):
        super().__init__(daemon=True)
        self.ha, self.hb, self.folder = ha, hb, folder
        self.stop_flag = False
        self.n = 0
        os.makedirs(folder, exist_ok=True)

    def run(self):
        t0 = time.time()
        while not self.stop_flag:
            t = time.time()
            for who, h in (("a", self.ha), ("b", self.hb)):
                try:
                    ImageGrab.grab(bbox=rect(h), all_screens=True).save(
                        os.path.join(self.folder, f"{self.n:04d}_{int((t - t0) * 1000):06d}ms_{who}.png"))
                except Exception:  # noqa: BLE001
                    pass
            self.n += 1
            time.sleep(max(0.0, 0.2 - (time.time() - t)))


def main() -> int:
    L.OUT = OUT
    os.makedirs(OUT, exist_ok=True)
    note, expect, count, grep_log, shots = L.note, L.expect, L.count, L.grep_log, L.shots
    a = coop_rig.Instance("a")
    a.start()
    b = coop_rig.Instance("b")
    b.start()
    note("INFO", f"mod-a pid {a.pid} log {a.log}; mod-b pid {b.pid} log {b.log}")
    L.past_title(a, "a")
    L.past_title(b, "b")
    ha, hb = place(a.pid, 0), place(b.pid, 1290)
    time.sleep(1)
    ImageGrab.grab(bbox=rect(ha), all_screens=True).save(os.path.join(OUT, "1_stock_main_menu.png"))

    # Host's campaign lobby, partner seated.
    a.press("Down", 250)
    time.sleep(1.5)
    a.press("A", 250)
    expect(a, "host: Continue opens the campaign lobby", r"continue: Hero\d+ is campaign", 0, 10)
    time.sleep(2)
    ImageGrab.grab(bbox=rect(ha), all_screens=True).save(os.path.join(OUT, "2_lobby_host_waiting.png"))
    L.open_coop(b, "b")
    time.sleep(2)
    ImageGrab.grab(bbox=rect(hb), all_screens=True).save(os.path.join(OUT, "2_chooser_partner.png"))
    if L.select_row(b, "b", "Join TestA"):
        b.press("A", 250)
    time.sleep(2)
    b.press("X", 250)
    time.sleep(2)
    a.press("X", 250)
    time.sleep(2)
    ImageGrab.grab(bbox=rect(ha), all_screens=True).save(os.path.join(OUT, "3_lobby_host_both_ready.png"))
    ImageGrab.grab(bbox=rect(hb), all_screens=True).save(os.path.join(OUT, "3_lobby_partner_ready.png"))
    L.select_row(a, "a", "Start")
    a.press("A", 250)
    expect(a, "Start continues the campaign", r"host start: campaign .* continuing", 0, 10)

    # The join, captured every 200 ms on both PCs from the go to after the spawn.
    expect(a, "host world up (go published)", r"host world up \(starter save", 0, 240)
    cap = Capture(ha, hb, os.path.join(OUT, "join_frames"))
    cap.start()
    expect(a, "join approved", r"\[joingate\] join approved", 0, 90)
    expect(a, "host cover up at the approval", r"join cover up", 0, 5)
    expect(b, "partner cover up at the go", r"join cover up", 0, 5)
    ok = expect(a, "player 2 spawned", r"henchman spawned", 0, 120)
    expect(a, "host cover down after the spawn", r"join cover down", 0, 10)
    expect(b, "partner cover down after the spawn", r"join cover down", 0, 10)
    time.sleep(5)
    cap.stop_flag = True
    cap.join(5)
    note("INFO", f"join capture: {cap.n} frames per PC at ~200 ms")
    # Every frame from the first cover frame on: the cover (dark leather) or the
    # world, never the game's co-op screens (mostly bright parchment).
    import numpy as np
    from PIL import Image
    folder = os.path.join(OUT, "join_frames")
    with open(os.path.join(OUT, "join_frames_check.txt"), "w", encoding="utf-8") as f:
        for who in ("a", "b"):
            seen_cover, bad, kinds = False, [], []
            for name in sorted(n for n in os.listdir(folder) if n.endswith(f"_{who}.png")):
                im = np.asarray(Image.open(os.path.join(folder, name)).convert("RGB")).astype(int)
                body = im[40:-10, 10:-10]
                rr, gg, bb = body[..., 0], body[..., 1], body[..., 2]
                dark = ((rr < 70) & (gg < 55) & (bb < 40)).mean()
                parch = ((rr > 170) & (gg > 150) & (bb > 100) & (rr - bb < 110) & (rr - bb > 20)).mean()
                mean = body.mean()
                # Thresholds from launch 26's first frames: the cover is ~99 % dark
                # leather at mean ~27; load transitions are black (mean ~0); world
                # frames have <= 0.6 % parchment, every stock co-op frame >= 8 %.
                kind = ("cover" if dark > 0.9 and 15 < mean < 40 else "black" if mean < 8
                        else "STOCK?" if parch > 0.05 else "world/other")
                seen_cover |= kind == "cover"
                if seen_cover and kind == "STOCK?":
                    bad.append(name)
                kinds.append(kind)
                f.write(f"{name} dark={dark:.3f} parchment={parch:.3f} {kind}\n")
            note("PASS" if seen_cover and not bad else "FAIL",
                 f"{who}: no stock co-op screen in any frame from the cover on "
                 f"({kinds.count('cover')} cover frames, {len(bad)} suspect: {bad[:3]})")
    for l in grep_log(a, r"join cover|join approved|henchman spawned|A for the join", 12):
        note("INFO", "a: " + l[l.find("["):][:160])
    for l in grep_log(b, r"join cover|henchman spawned|A for the join|in the host's world", 12):
        note("INFO", "b: " + l[l.find("["):][:160])
    with open(os.path.join(OUT, "launch26_results.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(L.results) + "\n")
    a.stop()
    b.stop()
    note("INFO", "both closed; rig off")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
