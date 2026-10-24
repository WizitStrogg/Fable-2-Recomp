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
        try:
            ImageGrab.grab(bbox=rect(self.hwnd), all_screens=True).save(p)
        except Exception as e:  # noqa: BLE001  (launch of 2026-10-24: a PIL encode error ended the run)
            note("INFO", f"{self.who}: screenshot {name} failed: {e}")
            Image.new("RGB", (4, 4)).save(p)
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
        # One press only: a retry that lands on the menu starts the stock New Game
        # (launch of 2026-10-24, the error path).
        n = self.count(r"title passed")
        self.press("E")
        self.wait("title A passes to the menu", r"title passed", n, 8)
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


def setup_tomls():
    """Josh's fable_2.toml for the host; Calvin's case for the partner: no
    gamertag line and no coop_campaign (the friend's play-coop.cmd turns it on).
    Two networks each: loopback plus the relay's second address."""
    src = open(os.path.join(ROOT, "mod", "fable_2.toml"), encoding="utf-8").read().splitlines()

    def make(folder, me, peers, tag, drop):
        over = {"network_interface": f"'{me}'", "lan_peers": f"'{peers}'", "network_isolate": "true",
                "fullscreen": "false", "window_width": "1270", "window_height": "714", "resolution_scale": "1"}
        if tag:
            over["profile_gamertag"] = f"'{tag}'"
        out, seen = [], set()
        for line in src:
            m = re.match(r"\s*([A-Za-z0-9_]+)\s*=", line)
            k = m.group(1) if m else None
            if k in drop:
                continue
            if k in over:
                out.append(f"{k} = {over[k]}")
                seen.add(k)
            else:
                out.append(line)
        out += [f"{k} = {v}" for k, v in over.items() if k not in seen]
        with open(os.path.join(ROOT, folder, "fable_2.toml"), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(out) + "\n")

    if "--fps" in sys.argv:  # the graphics thread's stencil-copy build (rexglue-sdk ec757ef)
        src += ["readback_memexport = false", "shared_memory_preserve_gpu_writes = true"]
    make("mod-a", "127.0.0.1", "127.0.0.2,127.0.0.5", "TestA", set())
    make("mod-b", "127.0.0.2", "127.0.0.1,127.0.0.4", None, {"profile_gamertag", "coop_campaign"})
    note("INFO", "tomls: host = Josh's (+ loopback, 2 networks); partner = no gamertag, no coop_campaign")


def join_phase(a, b, tag):
    """After the host's Start: world up, go acknowledged, cover, player 2 spawns."""
    ok = a.wait(f"{tag}: host world up", r"host world up", a.n_world, 300)
    ok &= b.wait(f"{tag}: partner got the go", r"coop_go received", b.n_go, 90)
    ok &= a.wait(f"{tag}: host saw the partner's acknowledgement", r"acknowledged the go", a.n_ack, 60)
    ok &= a.wait(f"{tag}: join approved", r"\[joingate\] join approved", a.n_appr, 120)
    k, stock, t = 0, 0, time.time()
    while a.count(r"henchman spawned") <= a.n_spawn and time.time() - t < 90:
        for g in (a, b):
            p = g.shot(f"{tag}_join_{k:02d}_{g.who}.png")
            stock += cover_metrics(p)[0] > 0.05
        k += 1
        time.sleep(1.0)
    note("PASS" if stock == 0 else "FAIL", f"{tag}: stock-screen frames {stock} of {2 * k}")
    ok &= a.wait(f"{tag}: player 2 spawned", r"henchman spawned", a.n_spawn, 60)
    time.sleep(4)
    a.shot(f"{tag}_host_world.png")
    b.shot(f"{tag}_partner_world.png")
    note("PASS" if ok and stock == 0 else "FAIL", f"=== {tag}: partner joined the host's campaign")
    return ok


def mark(a, b):
    a.n_world, a.n_ack = a.count(r"host world up"), a.count(r"acknowledged the go")
    a.n_appr, a.n_spawn = a.count(r"\[joingate\] join approved"), a.count(r"henchman spawned")
    b.n_go = b.count(r"coop_go received")


def both_ready_and_start(a, b, tag):
    b.press("X")
    time.sleep(2)
    a.select("Ready")
    if a.rows() and not any(r[0] == "Ready: Yes" for r in a.rows()):
        a.press("X")
    time.sleep(3)
    a.shot(f"{tag}_host_both_ready.png")
    mark(a, b)
    a.select("Start")
    n = a.count(r"host start:")
    a.press("E")
    return a.wait(f"{tag}: Start starts the campaign", r"host start:", n, 10)


def partner_leaves(a, b, tag):
    b.stop()
    a.wait(f"{tag}: host saves and waits in the campaign lobby", r"back at the front end, waiting", a.count(
        r"back at the front end, waiting"), 240)
    time.sleep(5)
    a.shot(f"{tag}_host_waiting_lobby.png")


def run_multi():
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start()
    b.start()
    try:
        a.past_title()
        b.past_title()
        # --- attempt 1: New Game -> Co-op on both -----------------------------
        n = a.count(r"\[lobby\] open host")
        a.press("E")
        a.wait("a1: New Game opens Solo / Co-op", r"\[lobby\] open chooser", 0, 10)
        time.sleep(1.5)
        a.select("Co-op")
        a.press("E")
        a.wait("a1: host's Co-op opens its lobby (no game heard)", r"\[lobby\] open host", n, 10)
        time.sleep(6)
        a.shot("a1_host_lobby.png")
        a.select("Start")
        n = a.count(r"host start:")
        a.press("E")
        time.sleep(2)
        note("PASS" if a.count(r"host start:") == n else "FAIL", "a1: Start refused with no partner")
        b.press("E")
        b.wait("a1: partner New Game opens Solo / Co-op", r"\[lobby\] open chooser", 0, 10)
        time.sleep(1.5)
        b.select("Co-op")
        n = b.count(r"showing the list")
        b.press("E")
        b.wait("a1: partner's Co-op shows the games to join (not a host lobby)", r"showing the list", n, 10)
        time.sleep(4)
        joins = [r for r in b.rows() if r[0].startswith("Join TestA")]
        note("PASS" if len(joins) == 1 else "FAIL", f"a1: TestA listed once on two networks ({len(joins)} row(s))")
        b.shot("a1_partner_list.png")
        n = b.count(r"joining TestA's lobby as partner")
        b.select("Join TestA")
        b.press("E")
        b.wait("a1: partner joins TestA's lobby", r"joining TestA's lobby as partner", n, 10)
        time.sleep(4)
        a.select("Ready")
        a.press("X")
        time.sleep(2)
        a.select("Start")
        n = a.count(r"host start:")
        a.press("E")
        time.sleep(2)
        note("PASS" if a.count(r"host start:") == n else "FAIL", "a1: Start refused while the partner is not ready")
        a.shot("a1_host_partner_not_ready.png")
        b.shot("a1_partner_in_lobby.png")
        if both_ready_and_start(a, b, "a1"):
            join_phase(a, b, "a1")
        # --- attempt 2: the partner leaves; restarts; Continue ------------------
        partner_leaves(a, b, "a2")
        b = Game("mod-b", "partner")
        b.start()
        b.past_title()
        b.press("Down")
        time.sleep(1.2)
        n = b.count(r"is waiting for this player")
        b.press("E")
        b.wait("a2: partner's Continue finds the campaign waiting for him", r"is waiting for this player", n, 10)
        b.wait("a2: partner joins it", r"continue: joining TestA's campaign", 0, 15)
        time.sleep(3)
        b.shot("a2_partner_continue_joined.png")
        if both_ready_and_start(a, b, "a2"):
            join_phase(a, b, "a2")
        # --- attempt 3: the partner leaves; restarts; New Game -> Co-op -> Continue row
        partner_leaves(a, b, "a3")
        b = Game("mod-b", "partner")
        b.start()
        b.past_title()
        b.press("E")
        b.wait("a3: partner New Game opens Solo / Co-op", r"\[lobby\] open chooser", 0, 10)
        time.sleep(1.5)
        b.select("Co-op")
        n = b.count(r"showing the list")
        b.press("E")
        b.wait("a3: partner's Co-op shows the games", r"showing the list", n, 10)
        time.sleep(4)
        rows = [r for r in b.rows() if r[0].startswith("Continue TestA")]
        note("PASS" if len(rows) == 1 else "FAIL", f"a3: TestA's campaign listed once ({len(rows)} row(s))")
        b.shot("a3_partner_list.png")
        n = b.count(r"joining TestA's lobby as partner")
        b.select("Continue TestA")
        b.press("E")
        b.wait("a3: partner joins the campaign lobby", r"joining TestA's lobby as partner", n, 10)
        time.sleep(3)
        if both_ready_and_start(a, b, "a3"):
            join_phase(a, b, "a3")
        # --- error path: the host leaves while the partner waits ----------------
        partner_leaves(a, b, "err")
        b = Game("mod-b", "partner")
        b.start()
        b.past_title()
        b.press("Down")
        time.sleep(1.2)
        b.press("E")
        b.wait("err: partner joined the waiting campaign", r"continue: joining TestA's campaign", 0, 15)
        time.sleep(3)
        n = b.count(r"partner: the host left the lobby")
        a.select("Back")
        a.press("E")
        b.wait("err: partner is told the host left", r"partner: the host left the lobby", n, 30)
        time.sleep(1)
        b.shot("err_partner_host_left.png")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def open_coop_entry(g, tag):
    """Main menu (cursor on New Game): Down twice to the native Co-op entry
    (New Game, Continue, Co-op, ...), screenshot its highlight, A -> the hub."""
    g.press("Down")
    time.sleep(0.6)
    g.shot(f"{tag}_{g.who}_menu_on_continue.png")
    g.press("Down")
    for i in range(5):  # the highlight moving onto Co-op
        g.shot(f"{tag}_{g.who}_menu_to_coop_{i}.png")
        time.sleep(0.12)
    time.sleep(0.4)
    g.shot(f"{tag}_{g.who}_menu_on_coop.png")
    n = g.count(r"\[coop\] main menu: Co-op chosen")
    g.press("E")
    ok = g.wait(f"{tag}: Co-op entry opens the hub", r"\[coop\] main menu: Co-op chosen", n, 10)
    time.sleep(2)
    g.shot(f"{tag}_{g.who}_hub.png")
    return ok


def host_new_campaign(a, tag):
    a.select("Host: new co-op campaign")
    n = a.count(r"\[lobby\] open host kind=new")
    a.press("E")
    return a.wait(f"{tag}: Host: new co-op campaign opens the host lobby", r"\[lobby\] open host kind=new", n, 10)


def partner_joins(b, prefix, tag):
    time.sleep(4)
    rows = [r for r in b.rows() if r[0].startswith(prefix)]
    note("PASS" if len(rows) == 1 else "FAIL", f"{tag}: '{prefix}' listed once on two networks ({len(rows)} row(s))")
    b.shot(f"{tag}_partner_hub_list.png")
    n = b.count(r"joining TestA's lobby as partner")
    b.select(prefix)
    b.press("E")
    return b.wait(f"{tag}: partner joins TestA's lobby", r"joining TestA's lobby as partner", n, 10)


def run_menu():
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_menu.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start()
    b.start()
    try:
        a.past_title()
        b.past_title()
        note("PASS" if a.count(r"main menu: Co-op entry added") else "FAIL", "host: native Co-op entry added")
        a.shot("m_host_main_menu.png")
        # B from the hub returns to the main menu with Co-op still focused: A again reopens the hub.
        open_coop_entry(a, "m0")
        n = a.count(r"hub closed, back to the main menu")
        a.press("B")
        a.wait("m0: B from the hub returns to the main menu", r"hub closed, back to the main menu", n, 10)
        time.sleep(1.5)
        a.shot("m0_host_back_on_coop.png")
        n = a.count(r"main menu: Co-op chosen")
        a.press("E")
        a.wait("m0: focus stayed on Co-op (A reopens the hub)", r"main menu: Co-op chosen", n, 10)
        time.sleep(1.5)
        # --- m1: host a new campaign, partner joins from the hub ---------------
        host_new_campaign(a, "m1")
        time.sleep(4)
        a.shot("m1_host_lobby.png")
        a.select("Start")
        n = a.count(r"host start:")
        a.press("E")
        time.sleep(2)
        note("PASS" if a.count(r"host start:") == n else "FAIL", "m1: Start refused with no partner")
        open_coop_entry(b, "m1")
        partner_joins(b, "Join TestA", "m1")
        time.sleep(3)
        a.select("Ready")
        a.press("X")
        time.sleep(2)
        a.select("Start")
        n = a.count(r"host start:")
        a.press("E")
        time.sleep(2)
        note("PASS" if a.count(r"host start:") == n else "FAIL", "m1: Start refused while the partner is not ready")
        if both_ready_and_start(a, b, "m1"):
            a.wait("m1: host loads through the game's own Load Game", r"main menu: Load Game", 0, 20)
            join_phase(a, b, "m1")
        # --- m2: partner leaves; host continues the co-op save; partner rejoins --
        b.stop()
        n = a.count(r"host: back at the front end")
        a.wait("m2: host saves, returns to the main menu", r"host: back at the front end", n, 240)
        time.sleep(3)
        a.shot("m2_host_message.png")
        time.sleep(12)
        a.past_title()  # the quit lands on the title ("Press A to start")
        open_coop_entry(a, "m2")
        a.select("Host: continue a co-op save")
        n = a.count(r"\[lobby\] open saves")
        a.press("E")
        a.wait("m2: the hub lists the co-op saves", r"\[lobby\] open saves", n, 10)
        time.sleep(1.5)
        a.shot("m2_host_saves.png")
        n = a.count(r"\[lobby\] open host kind=continue")
        a.press("E")
        a.wait("m2: the save's lobby opens", r"\[lobby\] open host kind=continue", n, 10)
        b = Game("mod-b", "partner")
        b.start()
        b.past_title()
        open_coop_entry(b, "m2")
        partner_joins(b, "Continue TestA", "m2")
        if both_ready_and_start(a, b, "m2"):
            join_phase(a, b, "m2")
        # --- error path: the host leaves its lobby while the partner waits --------
        b.stop()
        n = a.count(r"host: back at the front end")
        a.wait("err: host back at the main menu", r"host: back at the front end", n, 240)
        time.sleep(15)
        a.past_title()
        open_coop_entry(a, "err")
        host_new_campaign(a, "err")
        b = Game("mod-b", "partner")
        b.start()
        b.past_title()
        open_coop_entry(b, "err")
        partner_joins(b, "Join TestA", "err")
        time.sleep(2)
        n = b.count(r"partner: the host left the lobby")
        a.select("Back")
        a.press("E")
        b.wait("err: partner is told the host left", r"partner: the host left the lobby", n, 30)
        time.sleep(1)
        b.shot("err_partner_host_left.png")
        # --- stock New Game (no chooser) -------------------------------------------
        b.press("B")
        time.sleep(2)
        b.stop()
        b = Game("mod-b", "partner")
        b.start()
        b.past_title()
        n = b.count(r"\[lobby\] open")
        b.press("E")
        time.sleep(25)
        b.shot("stock_new_game.png")
        note("PASS" if b.count(r"\[lobby\] open") == n else "FAIL", "New Game is the stock game's (no co-op panel)")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def run_menu2():
    """From m2 on: continue the campaign save m1 made in mod-a's scratch saves,
    the partner rejoins; then the host-left message; then stock Continue."""
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_menu2.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start()
    b.start()
    try:
        a.past_title()
        b.past_title()
        # --- m2: host continues the co-op save; the partner rejoins -----------------
        open_coop_entry(a, "m2")
        a.select("Host: continue a co-op save")
        n = a.count(r"\[lobby\] open saves")
        a.press("E")
        a.wait("m2: the hub lists the co-op saves", r"\[lobby\] open saves", n, 10)
        time.sleep(1.5)
        a.shot("m2_host_saves.png")
        n = a.count(r"\[lobby\] open host kind=continue")
        a.press("E")
        a.wait("m2: the save's lobby opens", r"\[lobby\] open host kind=continue", n, 10)
        time.sleep(3)
        a.shot("m2_host_lobby.png")
        open_coop_entry(b, "m2")
        partner_joins(b, "Continue TestA", "m2")
        if both_ready_and_start(a, b, "m2"):
            a.wait("m2: host loads through the game's own Load Game", r"main menu: Load Game", 0, 20)
            join_phase(a, b, "m2")
        # --- err: the host leaves its lobby while the partner waits ---------------------
        b.stop()
        n = a.count(r"host: back at the front end")
        a.wait("err: host back at the main menu", r"host: back at the front end", n, 240)
        time.sleep(15)
        a.past_title()
        open_coop_entry(a, "err")
        host_new_campaign(a, "err")
        b = Game("mod-b", "partner")
        b.start()
        b.past_title()
        open_coop_entry(b, "err")
        partner_joins(b, "Join TestA", "err")
        time.sleep(2)
        n = b.count(r"partner: the host left the lobby")
        a.select("Back")
        a.press("E")
        b.wait("err: partner is told the host left", r"partner: the host left the lobby", n, 30)
        time.sleep(1)
        b.shot("err_partner_host_left.png")
        # --- stock Continue: the partner's newest solo save loads, no co-op panel ------
        b.press("B")
        time.sleep(2)
        b.stop()
        b = Game("mod-b", "partner")
        b.start()
        b.past_title()
        b.press("Down")
        time.sleep(1)
        n = b.count(r"\[lobby\] open")
        b.press("E")
        time.sleep(30)
        b.shot("stock_continue.png")
        note("PASS" if b.count(r"\[lobby\] open") == n else "FAIL", "Continue is the stock game's (no co-op panel)")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def run_err():
    """The host leaves its lobby while the partner waits: the partner is told."""
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_err.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start()
    b.start()
    try:
        a.past_title()
        b.past_title()
        open_coop_entry(a, "err")
        host_new_campaign(a, "err")
        open_coop_entry(b, "err")
        partner_joins(b, "Join TestA", "err")
        time.sleep(2)
        n = b.count(r"partner: the host left the lobby")
        a.select("Back")
        a.press("E")
        b.wait("err: partner is told the host left", r"partner: the host left the lobby", n, 30)
        time.sleep(1)
        b.shot("err_partner_host_left.png")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def run_final():
    """Smoke test of the release build: the Co-op row, the host-left message,
    then a join from the hub to player 2 spawned (no restarts)."""
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_final.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start()
    b.start()
    try:
        a.past_title()
        b.past_title()
        note("PASS" if a.count(r"main menu: Co-op entry added") else "FAIL", "final: native Co-op entry added")
        open_coop_entry(a, "final")
        host_new_campaign(a, "final")
        open_coop_entry(b, "final")
        partner_joins(b, "Join TestA", "final_err")
        time.sleep(2)
        # The host leaves its lobby: the partner is told and stays in the hub.
        n = b.count(r"partner: the host left the lobby")
        a.select("Back")
        a.press("E")
        b.wait("final: partner is told the host left", r"partner: the host left the lobby", n, 30)
        time.sleep(1)
        b.shot("final_partner_host_left.png")
        # Back in the hub: host a new campaign again; the partner joins from the list.
        time.sleep(2)
        host_new_campaign(a, "final")
        partner_joins(b, "Join TestA", "final")
        if both_ready_and_start(a, b, "final"):
            a.wait("final: host loads through the game's own Load Game", r"main menu: Load Game", 0, 20)
            join_phase(a, b, "final")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def main():
    os.makedirs(OUT, exist_ok=True)
    mode = sys.argv[1] if len(sys.argv) > 1 else "coop"
    {"coop": run_coop, "solo": run_solo, "multi": run_multi, "menu": run_menu, "menu2": run_menu2, "err": run_err, "final": run_final}[mode]()
    with open(os.path.join(OUT, f"results_{mode}.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(results) + "\n")
    return 0 if not any(r.startswith("FAIL") for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
