#!/usr/bin/env python3
"""coop_ship_e2e.py - v14 ship-build end-to-end check (no rig scripting).

The ship build has no remote control server, so this drives the two windows the
way a player does: OS-level keyboard/mouse input (SendInput) through the game's
own keyboard map, and desktop screenshots. Both copies (mod-a host, mod-b
partner) hold the SHIP files, are started through their play-coop.cmd (the host
the one in Josh's mod folder, the partner the friend package's), and use Josh's
fable_2.toml with only the network (loopback), window and gamertag lines
changed. Saves are the rig's own (mod-a/mod-b\\saves).

Josh's keyboard map: E = A, B = B, arrows = d-pad, left mouse = X.

  python coop_ship_e2e.py coop    host and partner: New Game -> Co-op -> lobby,
                                  join, both ready, Start, cover, player 2 spawns
  python coop_ship_e2e.py solo    host only: New Game -> Solo -> the hero cards
Evidence: research/001-evidence/menus/ship-e2e/.
"""

import ctypes
import ctypes.wintypes as wt
import glob
import os
import re
import subprocess
import sys
import time

import numpy as np
import psutil
from PIL import Image, ImageGrab

ROOT = r"D:\Fable2-LAN"
OUT = os.path.join(ROOT, r"research\001-evidence\menus\ship-e2e")
user32 = ctypes.windll.user32
results = []


def note(kind, msg):
    line = f"{kind:4} {msg}"
    results.append(line)
    print(line, flush=True)


# --- OS-level input -----------------------------------------------------------
class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD), ("time", wt.DWORD),
                ("dwExtraInfo", ctypes.c_size_t)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class _U(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("mi", MOUSEINPUT), ("pad", ctypes.c_byte * 32)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wt.DWORD), ("u", _U)]


SCAN = {"E": (0x12, False), "B": (0x30, False), "Down": (0x50, True), "Up": (0x48, True)}


def _send(inp):
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))


def key(name, hold_ms=150):
    sc, ext = SCAN[name]
    flags = 0x0008 | (0x0001 if ext else 0)  # KEYEVENTF_SCANCODE (| EXTENDEDKEY)
    for up in (False, True):
        i = INPUT(type=1)
        i.u.ki = KEYBDINPUT(0, sc, flags | (0x0002 if up else 0), 0, 0)
        _send(i)
        if not up:
            time.sleep(hold_ms / 1000)


def click(hwnd, hold_ms=150):
    l, t, r, b = rect(hwnd)
    user32.SetCursorPos((l + r) // 2, (t + b) // 2)
    time.sleep(0.1)
    for f in (0x0002, 0x0004):  # LEFTDOWN, LEFTUP
        i = INPUT(type=0)
        i.u.mi = MOUSEINPUT(0, 0, 0, f, 0, 0)
        _send(i)
        if f == 0x0002:
            time.sleep(hold_ms / 1000)


def windows_of(pid):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, _):
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(h):
            found.append(h)
        return True

    user32.EnumWindows(cb, 0)
    return found


def rect(hwnd):
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def focus(hwnd):
    user32.ShowWindow(hwnd, 9)  # SW_RESTORE
    user32.keybd_event(0x12, 0, 0, 0)  # Alt lets SetForegroundWindow through
    user32.SetForegroundWindow(hwnd)
    user32.keybd_event(0x12, 0, 2, 0)
    time.sleep(0.4)
    return user32.GetForegroundWindow() == hwnd


# --- one game copy --------------------------------------------------------------
class Game:
    def __init__(self, folder, who):
        self.dir = os.path.join(ROOT, folder)
        self.who = who
        self.proc = None
        self.hwnd = None
        self.log = None

    def start(self):
        exe = os.path.join(self.dir, "fable_2.exe").lower()
        if any((p.info["exe"] or "").lower() == exe for p in psutil.process_iter(["exe"])):
            raise SystemExit(f"{exe} already runs")
        t0 = time.time()
        subprocess.Popen(["cmd", "/c", os.path.join(self.dir, "play-coop.cmd")], cwd=self.dir,
                         creationflags=subprocess.CREATE_NEW_CONSOLE)
        while time.time() - t0 < 30 and not self.proc:
            for p in psutil.process_iter(["exe", "cmdline"]):
                if (p.info["exe"] or "").lower() == exe:
                    self.proc = p
            time.sleep(0.5)
        note("INFO", f"{self.who}: pid {self.proc.pid} cmdline {' '.join(self.proc.info['cmdline'][1:])}")
        while time.time() - t0 < 60 and not self.hwnd:
            w = windows_of(self.proc.pid)
            self.hwnd = w[0] if w else None
            time.sleep(0.5)
        while time.time() - t0 < 60 and not self.log:
            logs = [f for f in glob.glob(os.path.join(self.dir, "logs", "*.log")) if os.path.getmtime(f) >= t0 - 1]
            self.log = max(logs, key=os.path.getmtime) if logs else None
            time.sleep(0.5)
        note("INFO", f"{self.who}: log {self.log}")

    def lines(self):
        with open(self.log, encoding="utf-8", errors="replace") as f:
            return f.read().splitlines()

    def count(self, pat):
        return sum(1 for l in self.lines() if re.search(pat, l))

    def wait(self, what, pat, n0, timeout=20):
        t = time.time()
        while time.time() - t < timeout:
            if self.count(pat) > n0:
                note("PASS", f"{self.who}: {what}")
                return True
            time.sleep(0.5)
        note("FAIL", f"{self.who}: {what} (no '{pat}' in {timeout} s)")
        return False

    def press(self, name):
        focus(self.hwnd)
        if name == "X":
            click(self.hwnd)
        else:
            key(name)
        time.sleep(0.3)

    def shot(self, name):
        focus(self.hwnd)
        time.sleep(0.3)
        p = os.path.join(OUT, name)
        ImageGrab.grab(bbox=rect(self.hwnd), all_screens=True).save(p)
        note("INFO", f"{self.who}: screenshot {p}")
        return p

    def rows(self):
        views = [l for l in self.lines() if "[lobby] view:" in l]
        if not views:
            return []
        parts = views[-1].split("[lobby] view: ", 1)[1].split("|")
        return [(p[3:], p[1] == "1", p[2] == "1") for p in parts if p.startswith("R") and len(p) > 3]

    def select(self, prefix, tries=10):
        for _ in range(tries):
            r = self.rows()
            tgt = next((i for i, x in enumerate(r) if x[0].startswith(prefix)), None)
            cur = next((i for i, x in enumerate(r) if x[2]), None)
            if tgt is not None and cur is not None:
                if tgt == cur:
                    return True
                self.press("Down" if tgt > cur else "Up")
            time.sleep(1.0)
        note("FAIL", f"{self.who}: no row '{prefix}' ({self.rows()})")
        return False

    def stop(self):
        if self.proc and self.proc.is_running():
            self.proc.terminate()
            self.proc.wait(15)

    def past_title(self):
        self.wait("menu detected in the ship build ([state] -> PressAScreen/MainMenu)",
                  r"\[state\].*-> (PressAScreen|MainMenu)", 0, 240)
        time.sleep(5)
        for _ in range(3):
            n = self.count(r"title passed")
            self.press("E")
            if self.wait("title A passes to the menu", r"title passed", n, 8):
                break
        time.sleep(4)


def cover_metrics(p):
    im = np.asarray(Image.open(p).convert("RGB")).astype(int)
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    parch = ((r > 170) & (g > 150) & (b > 100) & (r - b < 110) & (r - b > 20)).mean()
    return parch, im.mean()


def chooser_then_coop(g, tag):
    n = g.count(r"\[lobby\] open chooser")
    g.press("E")
    ok = g.wait("New Game opens Solo / Co-op", r"\[lobby\] open chooser", n, 10)
    time.sleep(1.5)
    g.shot(f"{tag}_1_new_game_solo_coop.png")
    g.select("Co-op")
    n = g.count(r"\[lobby\] open host")
    g.press("E")
    ok &= g.wait("Co-op opens the co-op lobby", r"\[lobby\] open host", n, 10)
    time.sleep(3)
    return ok


def run_coop():
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start()
    b.start()
    try:
        a.past_title()
        b.past_title()
        chooser_then_coop(a, "host")
        a.shot("host_2_lobby.png")
        chooser_then_coop(b, "partner")
        time.sleep(3)
        if b.select("Join TestA"):
            n = b.count(r"joining TestA's lobby as partner")
            b.press("E")
            b.wait("Join TestA's co-op campaign", r"joining TestA's lobby as partner", n, 10)
        time.sleep(3)
        b.shot("partner_3_joined_lobby.png")
        b.press("X")
        time.sleep(3)
        a.press("X")
        time.sleep(3)
        a.shot("host_4_both_ready.png")
        a.select("Start")
        n = a.count(r"host start:")
        a.press("E")
        a.wait("Start starts the campaign", r"host start:", n, 10)
        a.wait("host world up", r"host world up", 0, 240)
        a.wait("join approved", r"\[joingate\] join approved", 0, 120)
        k, stock = 0, 0
        t = time.time()
        while a.count(r"henchman spawned") == 0 and time.time() - t < 90:
            for g in (a, b):
                p = g.shot(f"join_{k:03d}_{g.who}.png")
                parch, mean = cover_metrics(p)
                stock += parch > 0.05
            k += 1
            time.sleep(1.0)
        note("PASS" if stock == 0 else "FAIL", f"join: stock-screen frames {stock} of {2 * k}")
        a.wait("player 2 spawned", r"henchman spawned", 0, 60)
        time.sleep(4)
        a.shot("host_5_world.png")
        b.shot("partner_5_world.png")
        for g in (a, b):
            pads = g.count(r"A for the join screen")
            note("INFO", f"{g.who}: auto answers under the cover: {pads}")
    finally:
        a.stop()
        b.stop()


def run_solo():
    a = Game("mod-a", "host")
    a.start()
    try:
        a.past_title()
        n = a.count(r"\[lobby\] open chooser")
        a.press("E")
        a.wait("New Game opens Solo / Co-op", r"\[lobby\] open chooser", n, 10)
        time.sleep(1.5)
        a.select("Solo")
        n = a.count(r"A passed to the stock New Game")
        a.press("E")
        a.wait("Solo passes to the stock New Game", r"A passed to the stock New Game", n, 10)
        time.sleep(25)
        a.shot("solo_hero_cards.png")
    finally:
        a.stop()


def main():
    os.makedirs(OUT, exist_ok=True)
    mode = sys.argv[1] if len(sys.argv) > 1 else "coop"
    run_coop() if mode == "coop" else run_solo()
    with open(os.path.join(OUT, f"results_{mode}.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(results) + "\n")
    return 0 if not any(r.startswith("FAIL") for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
