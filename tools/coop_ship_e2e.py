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
import math
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


SCAN = {"E": (0x12, False), "B": (0x30, False), "Down": (0x50, True), "Up": (0x48, True),
        "Grave": (0x29, False), "Enter": (0x1C, False), "Escape": (0x01, False)}


def type_text(text):
    """Unicode key events (WM_CHAR): text for the backtick console."""
    for ch in text:
        for up in (False, True):
            i = INPUT(type=1)
            i.u.ki = KEYBDINPUT(0, ord(ch), 0x0004 | (0x0002 if up else 0), 0, 0)  # KEYEVENTF_UNICODE
            _send(i)
        time.sleep(0.01)


def _send(inp):
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))


def key(name, hold_ms=60):
    # 60 ms: at 60 fps (the FPS SDK) a 150 ms hold crossed the menu's own
    # repeat and moved two rows per press (fps run, 2026-10-25).
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


class FocusLost(Exception):
    pass


def focus(hwnd):
    """Brings the game window to the front. Keys are only ever sent to it: if it
    cannot be brought forward (someone is using the PC), the run stops instead
    of typing into another window (purse run, 2026-10-25)."""
    for _ in range(5):
        user32.ShowWindow(hwnd, 9)  # SW_RESTORE
        user32.keybd_event(0x12, 0, 0, 0)  # Alt lets SetForegroundWindow through
        user32.SetForegroundWindow(hwnd)
        user32.keybd_event(0x12, 0, 2, 0)
        time.sleep(0.4)
        if user32.GetForegroundWindow() == hwnd:
            return True
        time.sleep(1.0)
    raise FocusLost("could not bring the game window to the front; is someone using the PC?")


# --- one game copy --------------------------------------------------------------
def window_image(hwnd):
    """The window's own rendered content (PrintWindow with PW_RENDERFULLCONTENT),
    or None if it comes back black."""
    try:
        gdi32 = ctypes.windll.gdi32
        r = wt.RECT()
        user32.GetClientRect(hwnd, ctypes.byref(r))
        w, h = r.right - r.left, r.bottom - r.top
        if w <= 0 or h <= 0:
            return None
        hdc = user32.GetDC(hwnd)
        mdc = gdi32.CreateCompatibleDC(hdc)
        bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
        old = gdi32.SelectObject(mdc, bmp)
        ok = user32.PrintWindow(hwnd, mdc, 3)  # PW_CLIENTONLY | PW_RENDERFULLCONTENT
        class BMI(ctypes.Structure):
            _fields_ = [("biSize", wt.DWORD), ("biWidth", ctypes.c_long), ("biHeight", ctypes.c_long),
                        ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                        ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                        ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]
        bmi = BMI(ctypes.sizeof(BMI), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
        buf = ctypes.create_string_buffer(w * h * 4)
        gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bmi), 0)
        gdi32.SelectObject(mdc, old)
        gdi32.DeleteObject(bmp)
        gdi32.DeleteDC(mdc)
        user32.ReleaseDC(hwnd, hdc)
        if not ok:
            return None
        img = Image.frombuffer("RGB", (w, h), buf, "raw", "BGRX", 0, 1)
        if max(img.convert("L").getextrema()) < 8:  # black: nothing composed
            return None
        return img
    except Exception:  # noqa: BLE001
        return None


class Game:
    def __init__(self, folder, who):
        self.test_input = False
        # Evidence shots from the game window itself (the menu walk's highlight
        # detection is tuned on screen grabs, so it stays off until after the join).
        self.window_capture = False
        self.dir = os.path.join(ROOT, folder)
        self.who = who
        self.proc = None
        self.hwnd = None
        self.log = None

    def start(self, env_extra=None):
        self.test_input = bool(env_extra and env_extra.get("FABLE2_TEST_INPUT"))
        if self.test_input:
            open(os.path.join(self.dir, "test_input.txt"), "w").close()
        exe = os.path.join(self.dir, "fable_2.exe").lower()
        if any((p.info["exe"] or "").lower() == exe for p in psutil.process_iter(["exe"])):
            raise SystemExit(f"{exe} already runs")
        t0 = time.time()
        env = dict(os.environ)
        env.update(env_extra or {})
        subprocess.Popen(["cmd", "/c", os.path.join(self.dir, "play-coop.cmd")], cwd=self.dir, env=env,
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

    def send(self, line, timeout=10):
        """One test-input command; waits until the game has read it."""
        path = os.path.join(self.dir, "test_input.txt")
        with open(path, "a", encoding="utf-8") as f:
            f.write(line + "\n")
        t = time.time()
        while time.time() - t < timeout:
            if os.path.getsize(path) == 0:
                return True
            time.sleep(0.05)
        note("FAIL", f"{self.who}: test input not read: {line}")
        return False

    def press(self, name):
        if self.test_input:
            # 60 ms: a 120 ms hold sometimes moved the menu two rows (boots,
            # 2026-10-25: Down, Down landed on Language, past Co-op).
            self.send(f"press {dict(E='A').get(name, name)} 60")  # E is A on Josh's keyboard map
            time.sleep(0.45)
            return
        focus(self.hwnd)
        if name == "X":
            click(self.hwnd)
        else:
            key(name)
        time.sleep(0.3)

    def shot(self, name):
        try:
            focus(self.hwnd)
        except FocusLost:
            pass
        time.sleep(0.3)
        # A screen grab shows whatever is in front (boots, 2026-10-25 21:52Z:
        # another program's window was captured as the host's menu).
        self.last_shot_in_front = user32.GetForegroundWindow() == self.hwnd
        if not self.last_shot_in_front:
            note("INFO", f"{self.who}: screenshot {name} taken with another window in front")
        p = os.path.join(OUT, name)
        try:
            # The game's own window content first (PrintWindow, PW_RENDERFULLCONTENT):
            # windows in front, like the Windows Security prompt, are not in it. A
            # black frame (no composition content) falls back to the screen grab.
            img = window_image(self.hwnd) if self.window_capture else None
            if img is None:
                img = ImageGrab.grab(bbox=rect(self.hwnd), all_screens=True)
            img.save(p)
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

    def past_title(self, blind=False):
        if blind:  # FABLE2_TEST_PROBE=blind: no state line will come; the stock title takes the A
            time.sleep(20)
            self.press("E")
            note("INFO", f"{self.who}: probe kept blind; title A pressed after 20 s")
            time.sleep(4)
            return
        # 30 s, then A regardless: a blind state probe (wt002-b fable_2_026.log,
        # 2026-10-25) left the title waiting for minutes; the FAIL line stays.
        self.wait("menu detected in the ship build ([state] -> PressAScreen/MainMenu)",
                  r"\[state\].*-> (PressAScreen|MainMenu)", 0, 30)
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


def setup_tomls(third=False):
    """Josh's fable_2.toml for the host; Calvin's case for the partner: no
    gamertag line and no coop_campaign (the friend's play-coop.cmd turns it on).
    Two networks each: loopback plus the relay's second address. third: mod-c,
    a stranger with the partner's setup on 127.0.0.3 (its own XUID: isolated
    copies seed it with their address), heard by the host."""
    src = open(os.path.join(ROOT, "mod", "fable_2.toml"), encoding="utf-8").read().splitlines()

    def make(folder, me, peers, tag, drop):
        over = {"network_interface": f"'{me}'", "lan_peers": f"'{peers}'", "network_isolate": "true",
                "fullscreen": "false", "window_width": "1270", "window_height": "714",
                "resolution_scale": "2" if (tag and "--scale2" in sys.argv) else "1"}
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
    make("mod-a", "127.0.0.1", "127.0.0.2,127.0.0.5" + (",127.0.0.3" if third else ""), "TestA", set())
    # Calvin's case: no gamertag, no coop_campaign, and no camera lines (the
    # defaults decide: coop_own_camera is on by default since 2026-10-24).
    make("mod-b", "127.0.0.2", "127.0.0.1,127.0.0.4", None,
         {"profile_gamertag", "coop_campaign", "coop_own_camera", "coop_camera_follow_partner"})
    if third:
        make("mod-c", "127.0.0.3", "127.0.0.1", None,
             {"profile_gamertag", "coop_campaign", "coop_own_camera", "coop_camera_follow_partner"})
    note("INFO", "tomls: host = Josh's (+ loopback, 2 networks); partner = no gamertag, no coop_campaign"
                 + ("; mod-c = a stranger on 127.0.0.3" if third else ""))


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


# --- the highlighted main-menu row --------------------------------------------
# The green A button marks the highlighted row; its label is compared with a
# crop of the Co-op row (tools/coop_e2e_ref/coop_row.png; "Co-op" reads the
# same in every language). Checked offline on 60 saved menu screenshots: 1.00 on
# Co-op, 0.07-0.18 on Continue. A Down sometimes moved two rows, so the walk
# looks after every press instead of counting them (boots, 2026-10-25).
REF_COOP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "coop_e2e_ref", "coop_row.png")
_ref_bits = None


def a_button_y(im):
    """Centre y of the green A button left of the menu (x 150-300), or None."""
    a = im[:, 150:300, :3].astype(int)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    rows = np.nonzero(((g > 140) & (g - r > 50) & (g - b > 60)).sum(axis=1) >= 6)[0]
    if len(rows) < 8:
        return None
    runs, start = [], rows[0]
    for p, q in zip(rows, rows[1:]):
        if q != p + 1:
            runs.append((start, p))
            start = q
    runs.append((start, rows[-1]))
    s, e = max(runs, key=lambda t: t[1] - t[0])
    return (s + e) // 2 if e - s >= 15 else None


def label_bits(crop):
    return (crop[..., :3].astype(int).mean(axis=2) > 190).astype(np.float32)


def bits_similarity(a, b):
    h = min(a.shape[0], b.shape[0])
    a, b = a[:h], b[:h]
    best = 0.0
    for dx in range(-6, 7):
        for dy in range(-3, 4):
            sa = np.roll(np.roll(a, dy, 0), dx, 1)
            union = ((sa + b) > 0).sum()
            best = max(best, (sa * b).sum() / union if union else 0.0)
    return best


def coop_highlight(path):
    """How much the highlighted row's label looks like "Co-op" (0-1), or -1 when
    no highlighted row is on screen (a loading screen, a panel)."""
    global _ref_bits
    if _ref_bits is None:
        _ref_bits = label_bits(np.asarray(Image.open(REF_COOP).convert("RGB")))
    im = np.asarray(Image.open(path).convert("RGB"))
    y = a_button_y(im)
    if y is None or y < 16:
        return -1.0
    return bits_similarity(label_bits(im[y - 16:y + 16, 255:520]), _ref_bits)


def menu_events(g, n0):
    """Test-only menu events since line n0: (type, entry)."""
    out = []
    for l in g.lines()[n0:]:
        m = re.search(r"main menu event type=(\d+) entry='([^']*)'", l)
        if m:
            out.append((int(m.group(1)), m.group(2)))
    return out


def open_coop_entry(g, tag):
    """Main menu: move the highlight onto the native Co-op entry, checking the
    highlighted row after every press (screenshot), then A -> the hub."""
    n_lines = len(g.lines())
    steps = []
    for i in range(10):
        sim, p = -1.0, None
        t_look = time.time()
        attempt = 0
        # No highlighted row yet: an animation (3 looks), or another window in
        # front (a UAC prompt, a remote session: up to 60 s, batch1 2026-10-26).
        while True:
            time.sleep(0.8)
            p = g.shot(f"{tag}_{g.who}_menu_walk_{i}_{attempt}.png")
            attempt += 1
            sim = coop_highlight(p) if g.last_shot_in_front else -1.0
            if sim >= 0 or (g.last_shot_in_front and attempt >= 3) or time.time() - t_look > 60:
                break
        steps.append(round(sim, 2))
        if sim >= 0.6:
            break
        if sim < 0:
            # Covered (the Windows Security prompt sits over the menu while the
            # game window still counts as in front): walk blind, checked by the log.
            note("INFO", f"{tag}: {g.who}: no highlighted row visible ({p}); blind walk")
            return open_coop_entry_blind(g, tag)
            note("FAIL", f"{tag}: {g.who}: no highlighted menu row on screen ({p})")
            return False
        # Down first (New Game/Continue are above Co-op); after 4, back up.
        g.press("Down" if i < 4 else "Up")
    else:
        note("FAIL", f"{tag}: {g.who}: the highlight never reached Co-op (similarity per step {steps})")
        return False
    note("INFO", f"{tag}: {g.who}: highlight on Co-op after {len(steps) - 1} press(es) (similarity {steps}); "
                 f"menu events {menu_events(g, n_lines)}")
    g.shot(f"{tag}_{g.who}_menu_on_coop.png")
    n = g.count(r"\[coop\] main menu: Co-op chosen")
    nd = g.count(r"XamShowDeviceSelectorUI")
    g.press("E")
    ok = g.wait(f"{tag}: Co-op entry opens the hub", r"\[coop\] main menu: Co-op chosen", n, 10)
    if not ok and g.count(r"XamShowDeviceSelectorUI") > nd:
        note("FAIL", f"{tag}: {g.who}: A with the highlight on Co-op opened the save picker (Continue) instead; "
                     f"menu events {menu_events(g, n_lines)}")
    time.sleep(2)
    g.shot(f"{tag}_{g.who}_hub.png")
    return ok


def open_coop_entry_blind(g, tag):
    """Another window covers the menu (the Windows Security prompt, 2026-10-27):
    Down to Co-op without looking (1 row with no saves, 2 with), A, and the log
    says whether Co-op was chosen; otherwise back to New Game and try the other."""
    nl = len(g.lines())
    has_saves = any("mainsave.bin" in f for _, _, f in os.walk(os.path.join(g.dir, "saves")))
    for downs in ((2, 1) if has_saves else (1, 2)):
        n = g.count(r"\[coop\] main menu: Co-op chosen")
        for _ in range(downs):
            g.press("Down")
            time.sleep(0.8)
        g.press("E")
        if g.wait(f"{tag}: Co-op entry opens the hub (blind walk, {downs} down)", r"\[coop\] main menu: Co-op chosen", n, 6):
            note("INFO", f"{tag}: {g.who}: menu events {menu_events(g, nl)}")
            time.sleep(2)
            return True
        for _ in range(2):
            g.press("B")
            time.sleep(1.2)
        for _ in range(4):
            g.press("Up")
            time.sleep(0.5)
    note("FAIL", f"{tag}: {g.who}: blind walk found no Co-op; menu events {menu_events(g, nl)}")
    return False


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
        # Case 2: the partner leaves; the host returns to "Press A to start", passes
        # the title with one A, continues the co-op save; the partner rejoins.
        b.stop()
        n = a.count(r"host: back at the front end")
        a.wait("final2: host saves and returns to the front end", r"host: back at the front end", n, 240)
        time.sleep(15)
        a.shot("final2_host_title.png")
        n = a.count(r"main menu: Co-op entry added")
        a.past_title()
        # The front end builds the menu some seconds after the title's A (17 s in
        # the run of 2026-10-24): steer only once the Co-op entry exists.
        a.wait("final2: main menu built with the Co-op entry", r"main menu: Co-op entry added", n, 60)
        time.sleep(3)
        open_coop_entry(a, "final2")
        a.select("Host: continue a co-op save")
        n = a.count(r"\[lobby\] open saves")
        a.press("E")
        a.wait("final2: the hub lists the co-op saves", r"\[lobby\] open saves", n, 10)
        time.sleep(1.5)
        a.shot("final2_host_saves.png")
        n = a.count(r"\[lobby\] open host kind=continue")
        a.press("E")  # the newest campaign: the one just played
        a.wait("final2: the save's lobby opens", r"\[lobby\] open host kind=continue", n, 10)
        b = Game("mod-b", "partner")
        b.start()
        b.past_title()
        open_coop_entry(b, "final2")
        partner_joins(b, "Continue TestA", "final2")
        n = a.count(r"main menu: Load Game")
        if both_ready_and_start(a, b, "final2"):
            a.wait("final2: host loads through the game's own Load Game", r"main menu: Load Game", n, 20)
            join_phase(a, b, "final2")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def wait_row(g, prefix, what, timeout=20):
    """Waits for a panel row (from the [lobby] view log lines) to appear."""
    t = time.time()
    while time.time() - t < timeout:
        if any(r[0].startswith(prefix) for r in g.rows()):
            note("PASS", f"{g.who}: {what}")
            return True
        time.sleep(0.5)
    note("FAIL", f"{g.who}: {what} (no row '{prefix}' in {timeout} s; rows {g.rows()})")
    return False


def menu_then_coop(g, tag, n_menu):
    """After a title A: wait until the front end has built the menu with the
    Co-op entry, then Down twice to Co-op and A -> the hub."""
    t0 = time.time()
    # 240 s: a slow boot must not fail a proof run (2026-10-28: a partner boot took
    # ~3 min while heavy file scans ran on the PC); the boot time is logged.
    g.wait(f"{tag}: main menu built with the Co-op entry", r"main menu: Co-op entry added", n_menu, 240)
    note("INFO", f"{tag}: {g.who}: menu after {time.time() - t0:.0f} s of waiting")
    time.sleep(2)  # the menu's slide-in
    return open_coop_entry(g, tag)


def run_last():
    """778069c, the unproven part: in the world once; the partner leaves; the host
    returns to the title; Co-op -> continue the co-op save; the partner rejoins."""
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_last.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start()
    b.start()
    try:
        for g in (a, b):
            n = g.count(r"main menu: Co-op entry added")
            g.past_title()
            g.n_menu = n
        menu_then_coop(a, "last", a.n_menu)
        a.shot("1_main_menu_coop_row_778069c.png")
        host_new_campaign(a, "last")
        menu_then_coop(b, "last", b.n_menu)
        wait_row(b, "Join TestA", "last: the host's lobby is listed")
        partner_joins(b, "Join TestA", "last")
        wait_row(a, "Start", "last: host lobby shows the partner")
        if not both_ready_and_start(a, b, "last"):
            return
        a.wait("last: host loads through the game's own Load Game", r"main menu: Load Game", 0, 20)
        if not join_phase(a, b, "last"):
            return
        # The partner leaves; the host saves and returns to the title.
        b.stop()
        n = a.count(r"host: back at the front end")
        a.wait("last2: host saves and returns to the front end", r"host: back at the front end", n, 240)
        n_menu = a.count(r"main menu: Co-op entry added")
        a.wait("last2: the title is up (front end menu state)", r"\[coop\] message:", 0, 5)
        time.sleep(10)  # the "left the campaign" card is up for 12 s; the title is under it
        a.shot("last2_host_title.png")
        a.past_title()
        menu_then_coop(a, "last2", n_menu)
        wait_row(a, "Host: continue a co-op save", "last2: the hub offers the co-op saves")
        a.select("Host: continue a co-op save")
        n = a.count(r"\[lobby\] open saves")
        a.press("E")
        a.wait("last2: the hub lists the co-op saves", r"\[lobby\] open saves", n, 10)
        a.shot("last2_host_saves.png")
        n = a.count(r"\[lobby\] open host kind=continue")
        a.press("E")  # the newest campaign: the one just played
        a.wait("last2: the save's lobby opens", r"\[lobby\] open host kind=continue", n, 10)
        b = Game("mod-b", "partner")
        b.start()
        n = b.count(r"main menu: Co-op entry added")
        b.past_title()
        menu_then_coop(b, "last2", n)
        wait_row(b, "Continue TestA", "last2: the campaign is listed for the partner")
        partner_joins(b, "Continue TestA", "last2")
        wait_row(a, "Start", "last2: host lobby shows the partner")
        n = a.count(r"main menu: Load Game")
        if both_ready_and_start(a, b, "last2"):
            a.wait("last2: host loads through the game's own Load Game", r"main menu: Load Game", n, 20)
            join_phase(a, b, "last2")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def run_start():
    """Josh, 2026-10-24: at the host's Start both PCs go straight to the joining
    card; the partner's camera follows its own hero with no setting changed."""
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_start.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start()
    b.start()
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        menu_then_coop(a, "start", a.n_menu)
        host_new_campaign(a, "start")
        menu_then_coop(b, "start", b.n_menu)
        wait_row(b, "Join TestA", "start: the host's lobby is listed")
        partner_joins(b, "Join TestA", "start")
        wait_row(a, "Start", "start: host lobby shows the partner")
        nb = b.count(r"started the campaign, waiting")
        if not both_ready_and_start(a, b, "start"):
            return
        t0 = time.time()
        b.wait("start: partner sees the host's Start", r"started the campaign, waiting", nb, 5)
        note("INFO", f"start: partner card {time.time() - t0:.1f} s after the host's Start was seen")
        b.shot("start_partner_card_at_start.png")
        a.shot("start_host_card_at_start.png")
        time.sleep(8)
        b.shot("start_partner_card_waiting.png")
        a.wait("start: host loads through the game's own Load Game", r"main menu: Load Game", 0, 20)
        join_phase(a, b, "start")
        cam = [l for l in b.lines() if "camera: own_camera=" in l]
        note("PASS" if cam and "own_camera=true" in cam[-1] else "FAIL",
             f"start: partner camera follows its own hero by default ({cam[-1].split('[coop] ')[-1] if cam else 'no camera line'})")
        time.sleep(3)
        a.shot("start_host_world_camera.png")
        b.shot("start_partner_world_camera.png")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def console(g, command):
    """Runs one console command in g's window (backtick console, Enter, close)."""
    if g.test_input and command.startswith("lua "):
        g.send(command)
        time.sleep(0.5)
        return
    focus(g.hwnd)
    key("Grave")
    time.sleep(0.6)
    type_text(command)
    time.sleep(0.2)
    key("Enter")
    time.sleep(0.4)
    key("Grave")
    time.sleep(0.3)


def read_purses(a, b, tag):
    """Host: its hero and its henchman copy; partner: its own purse (the one its
    screen shows). Returns (host, host's henchman copy, partner) or None."""
    # The print itself ("[lua] PURSECHK host 150 150"), not the test-input echo
    # of the command, which arrives first (boots, 2026-10-25: int('..Money...')).
    ph, pp = r"\[lua\] PURSECHK host -?\d", r"\[lua\] PURSECHK partner -?\d"
    na, nb = a.count(ph), b.count(pp)
    console(a, "lua print('PURSECHK host '..Money.Get(GetPlayerHero())..' '..Money.Get(GetPlayerHenchman()))")
    console(b, "lua print('PURSECHK partner '..Money.Get(GetPlayerHenchman()))")
    if not (a.wait(f"{tag}: host purses read", ph, na, 15) and
            b.wait(f"{tag}: partner purse read", pp, nb, 15)):
        return None
    h = [l for l in a.lines() if re.search(ph, l)][-1].split("PURSECHK host ")[1].split()
    p = [l for l in b.lines() if re.search(pp, l)][-1].split("PURSECHK partner ")[1].split()
    return int(h[0]), int(h[1]), int(p[0])


def check_purse(a, b, tag, expect):
    time.sleep(6)  # the host's 1 s sync, the partner's mirror (new total at once)
    v = read_purses(a, b, tag)
    ok = v is not None and v[0] == v[1] == v[2] and (expect is None or v[0] == expect)
    note("PASS" if ok else "FAIL", f"{tag}: host {v[0] if v else '?'}, host's player 2 {v[1] if v else '?'}, "
                                     f"partner's own {v[2] if v else '?'} (expected {expect} on all three)")
    a.shot(f"{tag}_host.png")
    b.shot(f"{tag}_partner.png")
    # What Josh judges by: the gold the game shows (the pause menu).
    for g in (a, b):
        g.press("Start") if g.test_input else (focus(g.hwnd), key("Escape"))
        time.sleep(1.5)
        g.shot(f"{tag}_{g.who}_gold_screen.png")
        g.press("Start") if g.test_input else key("Escape")
        time.sleep(1.0)
    return ok


def run_purse():
    """The shared purse on both screens: host +150 (the game's own split), a
    player-2 purchase (-100 on the host's henchman copy), the host spends 100."""
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_purse.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start()
    b.start()
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        menu_then_coop(a, "purse", a.n_menu)
        host_new_campaign(a, "purse")
        menu_then_coop(b, "purse", b.n_menu)
        wait_row(b, "Join TestA", "purse: the host's lobby is listed")
        partner_joins(b, "Join TestA", "purse")
        wait_row(a, "Start", "purse: host lobby shows the partner")
        if not both_ready_and_start(a, b, "purse") or not join_phase(a, b, "purse"):
            return
        time.sleep(8)
        v = read_purses(a, b, "purse0")
        if not v:
            return
        start = v[0]
        note("PASS" if v[0] == v[1] == v[2] else "FAIL", f"purse0: after the join all three equal ({v})")
        console(a, "lua Money.Modify(GetPlayerHero(), 150)")
        check_purse(a, b, "purse1_host_gains_150", start + 150)
        console(a, "lua Money.Modify(GetPlayerHenchman(), -100)")
        check_purse(a, b, "purse2_player2_spends_100", start + 50)
        console(a, "lua Money.Modify(GetPlayerHero(), -100)")
        check_purse(a, b, "purse3_host_spends_100", start - 50)
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def run_fps():
    """The FPS SDK (rexglue-sdk 033523e) with co-op: join, the shared purse,
    then ~2 min in the world with the host's FPS meter on (fps_meter.log)."""
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_fps.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_FPS_METER": "1"})
    b.start()
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        menu_then_coop(a, "fps", a.n_menu)
        host_new_campaign(a, "fps")
        menu_then_coop(b, "fps", b.n_menu)
        wait_row(b, "Join TestA", "fps: the host's lobby is listed")
        partner_joins(b, "Join TestA", "fps")
        wait_row(a, "Start", "fps: host lobby shows the partner")
        if not both_ready_and_start(a, b, "fps") or not join_phase(a, b, "fps"):
            return
        time.sleep(8)
        v = read_purses(a, b, "fps_purse0")
        if v:
            console(a, "lua Money.Modify(GetPlayerHero(), 150)")
            check_purse(a, b, "fps_purse1_host_gains_150", v[0] + 150)
        t0 = time.time()
        k = 0
        while time.time() - t0 < 120:
            for g in (a, b):
                g.shot(f"fps_world_{k:02d}_{g.who}.png")
            k += 1
            time.sleep(20)
        for g, who in ((a, "host"), (b, "partner")):
            alive = g.proc.is_running()
            note("PASS" if alive else "FAIL", f"fps: {who} still running after 2 min in the world")
        meter = os.path.join(a.dir, "fps_meter.log")
        rates = []
        if os.path.exists(meter):
            for l in open(meter, encoding="utf-8", errors="replace"):
                m = re.search(r"rate=([0-9.]+)", l)
                if m:
                    rates.append(float(m.group(1)))
        tail = rates[-24:]
        note("INFO", f"fps: host main-loop rate, last {len(tail)} x 5 s windows: "
                     f"min {min(tail) if tail else 0:.1f}, avg {sum(tail) / len(tail) if tail else 0:.1f}, "
                     f"max {max(tail) if tail else 0:.1f} ({meter})")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def run_combo():
    """Combined proof (camera build): join, the shared purse, ~2 min in the world
    with the host's FPS meter, and the partner's camera trace (hand-backs,
    per-frame camera jump). Input through the game's test input source."""
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_combo.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_FPS_METER": "1", "FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        menu_then_coop(a, "combo", a.n_menu)
        host_new_campaign(a, "combo")
        menu_then_coop(b, "combo", b.n_menu)
        wait_row(b, "Join TestA", "combo: the host's lobby is listed")
        partner_joins(b, "Join TestA", "combo")
        wait_row(a, "Start", "combo: host lobby shows the partner")
        if not both_ready_and_start(a, b, "combo") or not join_phase(a, b, "combo"):
            return
        time.sleep(8)
        v = read_purses(a, b, "combo_purse0")
        if v:
            console(a, "lua Money.Modify(GetPlayerHero(), 150)")
            check_purse(a, b, "combo_purse1_host_gains_150", v[0] + 150)
        # The partner's camera, traced for 30 s while the host acts (draws a
        # weapon / attacks / blocks: the host's camera FOV moves).
        n_trace = b.count(r"\[coopcam\] trace")
        n_hand = b.count(r"showing the host's camera")
        b.send("cvar coop_cam_trace true")
        t0 = time.time()
        k = 0
        while time.time() - t0 < 32:
            for btn in ("X", "Y", "B"):
                a.press(btn)
                time.sleep(1.5)
            for g in (a, b):
                g.shot(f"combo_cam_{k:02d}_{g.who}.png")
            k += 1
        lines = [l for l in b.lines() if "[coopcam] trace" in l][n_trace:]
        cams = []
        for l in lines:
            m = re.search(r"cam (-?[0-9.]+) (-?[0-9.]+) (-?[0-9.]+)", l)
            if m:
                cams.append(tuple(float(x) for x in m.groups()))
        jumps = [((c[0] - p[0]) ** 2 + (c[1] - p[1]) ** 2 + (c[2] - p[2]) ** 2) ** 0.5
                 for p, c in zip(cams, cams[1:])]
        hand = b.count(r"showing the host's camera") - n_hand
        note("PASS" if lines else "FAIL", f"combo: camera trace lines {len(lines)}")
        note("PASS" if hand == 0 else "FAIL", f"combo: hand-backs to the host's camera in 30 s: {hand}")
        if jumps:
            jumps_sorted = sorted(jumps)
            note("INFO", f"combo: partner camera per-frame move: max {jumps_sorted[-1]:.3f} m, "
                         f"p99 {jumps_sorted[int(len(jumps_sorted) * 0.99) - 1]:.3f} m, "
                         f"median {jumps_sorted[len(jumps_sorted) // 2]:.3f} m over {len(jumps)} frames")
        # ~2 min in the world for the FPS meter.
        t0 = time.time()
        while time.time() - t0 < 90:
            for g in (a, b):
                g.shot(f"combo_world_{k:02d}_{g.who}.png")
            k += 1
            time.sleep(20)
        for g, who in ((a, "host"), (b, "partner")):
            note("PASS" if g.proc.is_running() else "FAIL", f"combo: {who} still running (no crash)")
        meter = os.path.join(a.dir, "fps_meter.log")
        rates = []
        if os.path.exists(meter):
            for l in open(meter, encoding="utf-8", errors="replace"):
                m = re.search(r"rate=([0-9.]+)", l)
                if m:
                    rates.append(float(m.group(1)))
        tail = [r for r in rates[-30:] if r < 1000]
        note("INFO", f"combo: host main-loop rate in the world, last {len(tail)} x 5 s: "
                     f"min {min(tail) if tail else 0:.1f}, avg {sum(tail) / len(tail) if tail else 0:.1f}, "
                     f"max {max(tail) if tail else 0:.1f}")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def pct(values, q):
    v = sorted(values)
    return v[min(len(v) - 1, int(len(v) * q))] if v else 0.0


def run_camera():
    """The joiner camera under motion (camera-glitch-notes.md): the partner walks
    circles, the host sprints, aims and zooms, the partner's camera is traced for
    60 s. Reports hand-backs, the host FOVs seen, and the per-frame camera move
    against the partner's own movement."""
    import threading
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_camera.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        menu_then_coop(a, "cam", a.n_menu)
        host_new_campaign(a, "cam")
        menu_then_coop(b, "cam", b.n_menu)
        wait_row(b, "Join TestA", "cam: the host's lobby is listed")
        partner_joins(b, "Join TestA", "cam")
        wait_row(a, "Start", "cam: host lobby shows the partner")
        if not both_ready_and_start(a, b, "cam") or not join_phase(a, b, "cam"):
            return
        time.sleep(8)
        n_trace = b.count(r"\[coopcam\] trace")
        n_hand = b.count(r"showing the host's camera")
        stop = time.time() + 60

        def partner_walks():
            k = 0
            while time.time() < stop:
                ang = k * 0.785398  # 8 directions: a circle every 6 s
                b.send(f"stick L {math.cos(ang):.3f} {math.sin(ang):.3f} 800")
                time.sleep(0.75)
                k += 1

        def host_acts():
            while time.time() < stop:
                a.send("stick L 0 1 3000")      # walk forward ...
                a.send("press A 2500")           # ... holding A (sprint)
                time.sleep(3.2)
                a.send("trigger L 1 2500")       # LT
                time.sleep(2.7)
                a.send("press Y 2500")           # Y held (ranged aim)
                a.send("stick R 0 1 1500")       # look while aiming
                time.sleep(2.7)
                a.send("trigger R 1 2000")       # RT
                time.sleep(2.2)

        b.send("cvar coop_cam_trace true")
        threads = [threading.Thread(target=partner_walks), threading.Thread(target=host_acts)]
        for t in threads:
            t.start()
        time.sleep(31)
        b.send("cvar coop_cam_trace true")  # the trace stops itself after 30 s: a second 30 s
        for t in threads:
            t.join()
        time.sleep(2)
        b.shot("cam_partner_after.png")
        a.shot("cam_host_after.png")
        lines = [l for l in b.lines() if "[coopcam] trace" in l][n_trace:]
        rx = re.compile(r"dt ([0-9.]+) ms creature (\S+) (\S+) (\S+) target \S+ \S+ \S+ cam (\S+) (\S+) (\S+) "
                        r"host \S+ \S+ \S+ fov ([0-9.]+)")
        rows = []
        for l in lines:
            m = rx.search(l)
            if not m:
                continue
            tm = re.search(r"(\d\d):(\d\d):(\d\d)\.(\d{3})\]", l)
            t = int(tm.group(1)) * 3600 + int(tm.group(2)) * 60 + int(tm.group(3)) + int(tm.group(4)) / 1000
            if rows and t - rows[-1][0] > 0.5:  # the gap between the two 30 s trace windows: not a frame
                rows.append(None)
            rows.append((t,) + tuple(float(x) for x in m.groups()))
        # Drop the time column for the analysis below; None marks a window gap.
        rows = [r[1:] if r else None for r in rows]
        cam_moves, cre_moves, jerks, fovs = [], [], [], set()
        prev = None
        prev_move = None
        for r in rows:
            if r is None:  # window gap
                prev, prev_move = None, None
                continue
            fovs.add(round(r[7], 2))
            if prev:
                cm = math.dist(r[4:7], prev[4:7])
                cr = math.dist(r[1:4], prev[1:4])
                cam_moves.append(cm)
                cre_moves.append(cr)
                if prev_move is not None:
                    jerks.append(abs(cm - prev_move))
                prev_move = cm
            prev = r
        hand = b.count(r"showing the host's camera") - n_hand
        note("PASS" if len(rows) > 600 else "FAIL", f"cam: trace frames {sum(1 for r in rows if r)}")
        note("PASS" if hand == 0 else "FAIL", f"cam: hand-backs to the host's camera in 60 s: {hand}")
        note("INFO", f"cam: host FOVs seen by the partner: {sorted(fovs)}")
        note("INFO", f"cam: partner creature per-frame move p50 {pct(cre_moves, .5):.3f} p99 {pct(cre_moves, .99):.3f} "
                     f"max {max(cre_moves or [0]):.3f} m")
        note("INFO", f"cam: partner camera per-frame move p50 {pct(cam_moves, .5):.3f} p99 {pct(cam_moves, .99):.3f} "
                     f"max {max(cam_moves or [0]):.3f} m; frame-to-frame change p99 {pct(jerks, .99):.3f} "
                     f"max {max(jerks or [0]):.3f} m")
        moving = sum(1 for m in cre_moves if m > 0.001)
        note("PASS" if moving > 100 else "FAIL", f"cam: the partner walked ({moving} frames with movement)")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def unpause(a, b, tag):
    """Closes the partner's pause menu: Start, then B if the games stay paused
    (boots, 2026-10-25: with 002's full wheel the second Start left both paused).
    Resumed = the host's queued Lua runs again or coopkit logs the resume."""
    resumed = r"\[coopkit\] host: game resumed|\[lua\] ran scripts"
    for inp in ("Start", "B"):
        n = a.count(resumed)
        b.press(inp)
        time.sleep(3)
        if a.count(resumed) > n:
            note("PASS" if inp == "Start" else "INFO", f"{tag}: the partner's {inp} closed the pause")
            return inp
        note("INFO", f"{tag}: still paused 3 s after the partner's {inp}")
    b.shot(f"{tag}_unpause_failed_partner.png")
    a.shot(f"{tag}_unpause_failed_host.png")
    note("FAIL", f"{tag}: neither Start nor B closed the partner's pause")
    return None


def run_luaq(host_probe=None):
    """The Lua console slot fix (b422910 + 9cace5a): the join (start flow), a
    normal purse step, then the partner pauses for ~20 s while the host gains and
    spends gold. Pass: the same total on all purses after the pause, each change
    run exactly once, no chunk lost. host_probe: FABLE2_TEST_PROBE for the host."""
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_luaq.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start(dict({"FABLE2_TEST_INPUT": "1"}, **({"FABLE2_TEST_PROBE": host_probe} if host_probe else {})))
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title(blind=g is a and host_probe == "blind")
        menu_then_coop(a, "luaq", a.n_menu)
        host_new_campaign(a, "luaq")
        menu_then_coop(b, "luaq", b.n_menu)
        wait_row(b, "Join TestA", "luaq: the host's lobby is listed")
        partner_joins(b, "Join TestA", "luaq")
        wait_row(a, "Start", "luaq: host lobby shows the partner")
        nb = b.count(r"started the campaign, waiting")
        if not both_ready_and_start(a, b, "luaq"):
            return
        b.wait("luaq: partner's joining card at the host's Start", r"started the campaign, waiting", nb, 5)
        if not join_phase(a, b, "luaq"):
            return
        time.sleep(8)
        v = read_purses(a, b, "luaq_purse0")
        if not v:
            return
        start = v[0]
        # A normal step.
        a.send("lua Money.Modify(GetPlayerHero(), 150) print('LUAQ gain1')")
        check_purse(a, b, "luaq_purse1_gain_150", start + 150)
        time.sleep(3)  # the gold screens have closed (boots, 2026-10-25)
        # The pause variant: the partner's pause menu (it pauses both games) for
        # ~20 s while the host gains 150 and spends 100; then it closes.
        b.press("Start")
        time.sleep(2)
        b.shot("luaq_pause_partner.png")
        a.shot("luaq_pause_host.png")
        a.send("lua Money.Modify(GetPlayerHero(), 150) print('LUAQ gain2')")
        time.sleep(8)
        a.send("lua Money.Modify(GetPlayerHero(), -100) print('LUAQ spend2')")
        time.sleep(10)
        unpause(a, b, "luaq")
        expect = start + 150 + 150 - 100
        time.sleep(10)  # queued and kept chunks run after the pause; the sync and mirror follow
        v = read_purses(a, b, "luaq_after_pause")
        ok = v is not None and v[0] == v[1] == v[2] == expect
        note("PASS" if ok else "FAIL", f"luaq: after the pause host {v[0] if v else '?'}, host's player 2 "
                                         f"{v[1] if v else '?'}, partner's own {v[2] if v else '?'} (expected {expect})")
        a.shot("luaq_after_pause_host.png")
        b.shot("luaq_after_pause_partner.png")
        for tag in ("LUAQ gain1", "LUAQ gain2", "LUAQ spend2"):
            # The Lua print itself ("[lua] <text>"); the log also has the test-input
            # echo of the command and a "(service trap)" debug copy of the print.
            n = a.count(r"\[lua\] " + re.escape(tag))
            note("PASS" if n == 1 else "FAIL", f"luaq: '{tag}' ran {n} time(s) (expected exactly 1)")
        for g in (a, b):
            refused = g.count(r"not queued")
            kept = g.count(r"chunk kept until a console slot is free")
            queued = g.count(r"kept chunk queued")
            note("PASS" if kept == queued else "FAIL",
                 f"luaq: {g.who}: {refused} refused, {kept} kept, {queued} of them queued later "
                 f"({kept - queued} lost)")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def dcall_report(g, tag):
    """The luaD_call minute lines: calls, nCcalls reads (each a VirtualQuery)."""
    rows = [re.search(r"luaD_call (\d+) \(VM thread (\d+), outermost (\d+), nCcalls reads (\d+)\)", l)
            for l in g.lines() if "last minute: luaD_call" in l]
    rows = [tuple(int(x) for x in m.groups()) for m in rows if m]
    if not rows:
        note("FAIL", f"{tag}: {g.who}: no luaD_call minute lines with nCcalls reads")
        return
    busy = [r for r in rows if r[1] > 10000]
    note("INFO", f"{tag}: {g.who}: per minute (calls, VM thread, outermost, reads): {rows}")
    # 002 queues a COOPKIT chunk every ~0.5 s, so some reads remain: PASS at
    # <= 1% of the VM-thread calls in every busy minute.
    worst = max((r[3] / r[1] for r in busy), default=0.0)
    note("PASS" if busy and worst <= 0.01 else "FAIL",
         f"{tag}: {g.who}: nCcalls reads at most {worst:.2%} of VM-thread luaD_calls over {len(busy)} "
         f"busy minute(s) (before c405db4: one per VM-thread call, 162k-755k/min)")


def run_boots():
    """Boot-hang check (wt002-b fable_2_026.log): both copies cold-boot 4 times;
    each boot must reach a menu state on the probe, pass the title, build the
    menu with the Co-op entry and open the hub through the real menu. Then one
    luaq run (it includes the host's Start and the partner's join) with the
    luaD_call minute lines (nCcalls reads)."""
    setup_tomls()
    for i in range(1, 5):
        tag = f"boot{i}"
        # Boot 2 forces the stale latch on both copies (the 026 failure).
        env = dict({"FABLE2_TEST_INPUT": "1"}, **({"FABLE2_TEST_PROBE": "stale"} if i == 2 else {}))
        a, b = Game("mod-a", "host"), Game("mod-b", "partner")
        a.start(env)
        b.start(env)
        try:
            for g in (a, b):
                g.n_menu = g.count(r"main menu: Co-op entry added")
                g.past_title()
            for g in (a, b):
                menu_then_coop(g, tag, g.n_menu)
                stale = g.count(r"front-end value copy at .* went stale")
                blind = g.count(r"state probe has no state")
                note("INFO", f"{tag}: {g.who}: probe stale copies {stale}, menu fallback used {blind}")
                if i == 2:
                    forced = g.count(r"\[state\] test: the value copy at")
                    note("PASS" if forced and stale else "FAIL",
                         f"{tag}: {g.who}: forced stale copy re-hunted (forced {forced}, re-hunts {stale})")
        finally:
            for g in (a, b):
                g.stop()
        time.sleep(5)
    # The host's probe kept blind for the whole front end: the in_main_menu
    # fallback must still turn the host's Start into the game's Load Game.
    run_luaq(host_probe="blind")
    hosts = []
    for g in (Game("mod-a", "host"), Game("mod-b", "partner")):
        logs = sorted(glob.glob(os.path.join(g.dir, "logs", "*.log")), key=os.path.getmtime)
        g.log = logs[-1]
        hosts.append(g)
        dcall_report(g, "boots_luaq")
    a = hosts[0]
    checks = [("probe kept blind", r"\[state\] test: probe kept blind"),
              ("no state line after boot", None),
              ("menu fallback warning", r"state probe has no state"),
              ("Start loaded through Load Game", r"main menu: Load Game"),
              ("player 2 spawned", r"player 2 spawned|campaign player 2 queued")]
    for what, pat in checks:
        if pat is None:
            n = len([l for l in a.lines() if "[state]" in l and "boot=0ms" not in l and "test:" not in l
                     and "front-end scans" not in l])
            note("PASS" if n == 0 else "FAIL", f"boots_blind: host: {what} ({n} state change(s))")
        else:
            n = a.count(pat)
            note("PASS" if n else "FAIL", f"boots_blind: host: {what} ({n})")


def lua_lines(g, chunk, marker, timeout=15):
    """Runs one Lua line in g's game (test input) and returns the new
    "[lua] <marker> ..." lines it printed (text after the marker)."""
    pat = r"\[lua\] " + re.escape(marker)
    n = g.count(pat)
    g.send("lua " + chunk)
    t = time.time()
    while time.time() - t < timeout and g.count(pat) <= n:
        time.sleep(0.3)
    return [l.split("[lua] " + marker, 1)[1].strip() for l in g.lines() if re.search(pat, l)][n:]


# Chests in the host's region: the game's own search (as AIManager does it).
# WG keeps them for the next chunks (console chunks share the game's globals).
WG_FIND = ("local ok, e = pcall(function() local h = GetPlayerHero(); local hp = h:GetPosition(); "
           "WG = SearchTools.GetSearchResults(SearchTools.FilterWithEC(SearchTools.StartNewSearch('object'), "
           "Chest.GetECType())); print('WGN ' .. #WG); for i, c in pairs(WG) do local p = c:GetPosition(); "
           "print(string.format('WGCHEST %d x=%.1f y=%.1f z=%.1f d=%.1f gold=%d open=%s locked=%s keys=%d', i, "
           "p:GetX(), p:GetY(), p:GetZ(), (p - hp):GetLength(), Money.IsAvailable(c) and Money.Get(c) or -1, "
           "tostring(Chest.IsOpen(c)), tostring(Chest.IsLocked(c)), Chest.GetNumberOfSilverKeysNeeded(c) or 0)) "
           "end end); if not ok then print('WGN error ' .. tostring(e)) end")


WG_SPAWN_GOLD = ("local ok, e = pcall(function() print('WGGOLD template ObjectInventoryGoldBag exists=' .. "
                 "tostring(GDB.RecordExists('ObjectInventoryGoldBag'))); WGG = WGG or {}; "
                 "for _, who in ipairs({GetPlayerHero(), GetPlayerHenchman()}) do local p = who:GetPosition(); "
                 "local b = Debug.CreateEntityAtPosition('ObjectInventoryGoldBag', 'wggoldbag', "
                 "CVector3(p:GetX() + 1.5, p:GetY(), p:GetZ())); WGG[#WGG + 1] = b; "
                 "print(string.format('WGGOLD %d x=%.1f y=%.1f z=%.1f d=1.5 gold=%d spawned', #WGG, p:GetX() + 1.5, "
                 "p:GetY(), p:GetZ(), Money.IsAvailable(b) and Money.Get(b) or -1)) end end); "
                 "if not ok then print('WGGOLD error ' .. tostring(e)) end")

WG_FIND_GOLD = ("local ok, e = pcall(function() local hp = GetPlayerHero():GetPosition(); "
                "WGG = {}; for _, o in pairs(SearchTools.GetSearchResults(SearchTools.StartNewSearch('object'))) do "
                "if Money.IsAvailable(o) and Money.Get(o) > 0 and not Chest.IsAvailable(o) then "
                "WGG[#WGG + 1] = o; local p = o:GetPosition(); "
                "print(string.format('WGGOLD %d x=%.1f y=%.1f z=%.1f d=%.1f gold=%d', #WGG, p:GetX(), p:GetY(), "
                "p:GetZ(), (p - hp):GetLength(), Money.Get(o))) end end print('WGGOLD done ' .. #WGG) end); "
                "if not ok then print('WGGOLD error ' .. tostring(e)) end")


def wg_dismiss(a, b, g, tag):
    """The game's "received" boxes wait for A and hold every script (coopkit:
    "game paused (Lua waiting)"). A until the host's console answers again."""
    for k in range(12):
        if lua_lines(a, f"print('WGFREE {k}')", "WGFREE", 4):
            note("INFO", f"{tag}: scripts run again after {k} A press(es)")
            return True
        g.press("E")
        time.sleep(1.5)
    note("FAIL", f"{tag}: the host's scripts still wait after 12 A presses")
    return False


def wg_teleport(a, idx, actor, side):
    """Host Lua: put the host's hero or player 2 1.2 m from chest idx on one of
    four sides, facing it (Physics.TeleportToPosition / SetFacingVector, as the
    game's own behaviours do)."""
    who = "GetPlayerHero()" if actor == "hero" else "GetPlayerHenchman()"
    off = [(1.2, 0), (-1.2, 0), (0, 1.2), (0, -1.2)][side]
    return lua_lines(a, (
        f"local ok, e = pcall(function() local c = WG[{idx}]; local e = {who}; local p = c:GetPosition(); "
        f"local st = CVector3(p:GetX() + {off[0]}, p:GetY() + {off[1]}, p:GetZ()); "
        f"Physics.TeleportToPosition(e, st); local f = p - st; f:Normalise(); Physics.SetFacingVector(e, f); "
        f"print('WGTP {actor} chest {idx} side {side}') end); if not ok then print('WGTP error ' .. tostring(e)) end"),
        "WGTP")


def wg_chest_state(a, idx):
    out = lua_lines(a, f"local c = WG[{idx}]; print('WGSTATE open=' .. tostring(Chest.IsOpen(c)) .. "
                       f"' gold=' .. tostring(Money.IsAvailable(c) and Money.Get(c) or 0))", "WGSTATE")
    return out[-1] if out else "open=? gold=0"


def wg_pickup(a, b, g, idx, actor, gold, tag, start):
    """g picks up gold object idx with A (OnActionUsePickUp: Money.Modify(the
    picker, gold))."""
    for side in range(4):
        off = [(0.8, 0), (-0.8, 0), (0, 0.8), (0, -0.8)][side]
        who = "GetPlayerHero()" if actor == "hero" else "GetPlayerHenchman()"
        lua_lines(a, (f"local o = WGG[{idx}]; local e = {who}; local p = o:GetPosition(); "
                      f"local st = CVector3(p:GetX() + {off[0]}, p:GetY() + {off[1]}, p:GetZ()); "
                      f"Physics.TeleportToPosition(e, st); local f = p - st; f:Normalise(); "
                      f"Physics.SetFacingVector(e, f); print('WGTP pickup {idx} side {side}')"), "WGTP")
        time.sleep(1.5)
        before = read_purses(a, b, f"{tag}_before_A_side{side}")
        g.press("E")
        time.sleep(4)
        wg_dismiss(a, b, g, tag)
        gone = lua_lines(a, f"print('WGPICK ' .. tostring(WGG[{idx}]:IsAlive()))", "WGPICK", 8)
        if gone and "false" in gone[-1]:
            after = read_purses(a, b, f"{tag}_after_A")
            note("PASS", f"{tag}: {g.who} picked up gold object {idx} with A (side {side}; value {gold})")
            note("INFO", f"{tag}: purses right before the A {before}, right after {after}")
            if gold < 0 and after:
                note("INFO", f"{tag}: the bag's value was unknown; the host's purse moved by {after[0] - start}")
                gold = after[0] - start
            check_purse(a, b, tag, start + gold)
            return start + gold
    note("FAIL", f"{tag}: {g.who} could not pick up gold object {idx}")
    return start


def wg_open_chest(a, b, g, idx, actor, tag, start, unknown=False):
    """g (host or partner) opens chest idx with its own A, from the side where
    the open works; then all three purses must hold start + the chest's gold."""
    gold = int(re.search(r"gold=(-?\d+)", wg_chest_state(a, idx)).group(1))
    for side in range(4):
        wg_teleport(a, idx, actor, side)
        time.sleep(1.5)
        g.shot(f"{tag}_{g.who}_at_chest_side{side}.png")
        before = read_purses(a, b, f"{tag}_before_A_side{side}")
        g.press("E")
        time.sleep(6)  # the open animation, the take-items script, the reward box
        st = wg_chest_state(a, idx)
        if "open=true" in st:
            note("PASS", f"{tag}: {g.who} opened chest {idx} with A (side {side}; {st})")
            g.shot(f"{tag}_{g.who}_chest_open.png")
            wg_dismiss(a, b, g, tag)
            after = read_purses(a, b, f"{tag}_after_A")
            note("INFO", f"{tag}: purses right before the A {before}, right after {after} (chest gold {gold})")
            # unknown: the chest holds a gold item, not a gold component; the
            # caller compares the change on the three purses.
            return check_purse(a, b, tag, None if unknown else start + gold), start + gold
    note("FAIL", f"{tag}: {g.who} could not open chest {idx} from any side")
    return False, start


def run_worldgold():
    """Spec 001 open item (T012/P5): one gold purse when gold comes from the
    world. The host and then the partner open a real chest with A (the game's
    OnActionUseTakeItems moves the gold: Money.Modify(the opener, gold)); then a
    quest reward through QuestTracker.GiveRewards, the call the game's own quest
    completion sequence makes. Pass: all three purses equal the expected total
    after each event (so each change counted once)."""
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_worldgold.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        menu_then_coop(a, "worldgold", a.n_menu)
        host_new_campaign(a, "worldgold")
        menu_then_coop(b, "worldgold", b.n_menu)
        wait_row(b, "Join TestA", "worldgold: the host's lobby is listed")
        partner_joins(b, "Join TestA", "worldgold")
        wait_row(a, "Start", "worldgold: host lobby shows the partner")
        if not both_ready_and_start(a, b, "worldgold") or not join_phase(a, b, "worldgold"):
            return
        time.sleep(8)
        worldgold_events(a, b)
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def wg_partner_pose(b):
    """The partner's own hero position and facing, from the partner's Lua (its
    copy owns them). Returns (x, y, z, fx, fy) or None."""
    out = lua_lines(b, ("local ok, e = pcall(function() local me = GetPlayerHenchman(); local p = me:GetPosition(); "
                        "local f = Physics.GetFacingVector(me); print(string.format('WGPOSE %.3f %.3f %.3f %.3f %.3f', "
                        "p:GetX(), p:GetY(), p:GetZ(), f:GetX(), f:GetY())) end); "
                        "if not ok then print('WGPOSE error ' .. tostring(e)) end"), "WGPOSE", 10)
    if not out or "error" in out[-1]:
        note("INFO", f"worldgold: partner pose {out}")
        return None
    return tuple(float(x) for x in out[-1].split()[:5])


def wg_place_before_p2(a, b, table, idx, dist):
    """Host Lua (the host owns world objects): move object table[idx] dist m in
    front of the partner's hero, by the partner's own position and facing."""
    pose = wg_partner_pose(b)
    if not pose:
        return False
    x, y, z, fx, fy = pose
    n = math.hypot(fx, fy) or 1.0
    px, py = x + fx / n * dist, y + fy / n * dist
    out = lua_lines(a, (
        f"local ok, e = pcall(function() Physics.TeleportToPosition({table}[{idx}], CVector3({px:.3f}, {py:.3f}, {z:.3f})); "
        f"print(string.format('WGPLACE %s %d at %.1f %.1f, {dist} m in front of the partner', '{table}', {idx}, "
        f"{px:.3f}, {py:.3f})) end); if not ok then print('WGPLACE error ' .. tostring(e)) end"), "WGPLACE", 10)
    note("INFO", f"worldgold: partner at ({x:.1f}, {y:.1f}) facing ({fx:.2f}, {fy:.2f}); {out}")
    return bool(out) and "error" not in out[-1]


def wg_partner_goto(b, xyz, kind, side):
    """The partner's own Lua (its copy owns its position: a teleport from the
    host's side does not stick, batch1 2026-10-26): find the gold object or
    chest nearest the host-reported spot and stand 0.8-1.2 m from it, facing it."""
    flt = "Money.IsAvailable(o)" if kind == "gold" else "Chest.IsAvailable(o)"
    r = 0.8 if kind == "gold" else 1.2
    ox, oy = [(r, 0), (-r, 0), (0, r), (0, -r)][side]
    x, y, z = xyz
    out = lua_lines(b, (
        f"local ok, e = pcall(function() local me = GetPlayerHenchman(); local best, bd = nil, 1e9; "
        f"for _, o in pairs(SearchTools.GetSearchResults(SearchTools.StartNewSearch('object'))) do "
        f"if {flt} then local d = (o:GetPosition() - CVector3({x}, {y}, {z})):GetLength(); "
        f"if d < bd then best, bd = o, d end end end; "
        f"if not best or bd > 3 then print('WGPTP none ' .. tostring(bd)) return end; local p = best:GetPosition(); "
        f"local st = CVector3(p:GetX() + {ox}, p:GetY() + {oy}, p:GetZ()); Physics.TeleportToPosition(me, st); "
        f"local f = p - st; f:Normalise(); Physics.SetFacingVector(me, f); "
        f"print(string.format('WGPTP ok %s side {side} (%.1f m from the spot)', '{kind}', bd)) end); "
        f"if not ok then print('WGPTP error ' .. tostring(e)) end"), "WGPTP", 15)
    note("INFO", f"worldgold: partner goes to the {kind} at {xyz}, side {side}: {out}")
    return bool(out) and out[-1].startswith("ok")


def wg_partner_pickup(a, b, idx, xyz, tag, start, gold=5):
    for side, dist in enumerate((1.0, 0.7, 1.3, 0.5)):
        if not wg_place_before_p2(a, b, "WGG", idx, dist):
            continue
        time.sleep(1.5)
        b.shot(f"{tag}_partner_before_A_{side}.png")
        before = read_purses(a, b, f"{tag}_before_A_side{side}")
        b.press("E")
        time.sleep(3)
        if lua_lines(a, f"print('WGPICK ' .. tostring(WGG[{idx}]:IsAlive()))", "WGPICK", 8)[-1:] == ["true"]:
            # Not picked up from there: walk onto it (a short push forward), A again.
            b.send("stick L 0 1 400")
            time.sleep(0.8)
            b.press("E")
            time.sleep(3)
        wg_dismiss(a, b, b, tag)
        gone = lua_lines(a, f"print('WGPICK ' .. tostring(WGG[{idx}]:IsAlive()))", "WGPICK", 8)
        if gone and "false" in gone[-1]:
            after = read_purses(a, b, f"{tag}_after_A")
            note("PASS", f"{tag}: the partner picked up gold object {idx} with its own A (side {side})")
            note("INFO", f"{tag}: purses right before the A {before}, right after {after}")
            if before and after:
                # Judged by the shared total: the host's purse and the partner's
                # mirror each move once, by the bag's gold.
                same = after[0] - before[0] == after[1] - before[1] == after[2] - before[2] == gold
                note("PASS" if same else "FAIL",
                     f"{tag}: host purse, host's player 2 and the partner's mirror each +{gold} once "
                     f"({before} -> {after})")
                check_purse(a, b, tag, before[0] + gold)
                return after[0]
            return start
    note("FAIL", f"{tag}: the partner could not pick up gold object {idx}")
    return start


def wg_partner_chest(a, b, idx, xyz, tag, start):
    for side, dist in enumerate((1.3, 1.0, 1.6, 0.8)):
        if not wg_place_before_p2(a, b, "WG", idx, dist):
            continue
        time.sleep(1.5)
        before = read_purses(a, b, f"{tag}_before_A_side{side}")
        b.press("E")
        time.sleep(6)
        st = wg_chest_state(a, idx)
        if "open=true" in st:
            note("PASS", f"{tag}: the partner opened chest {idx} with its own A (side {side})")
            b.shot(f"{tag}_partner_chest_open.png")
            wg_dismiss(a, b, b, tag)
            after = read_purses(a, b, f"{tag}_after_A")
            note("INFO", f"{tag}: purses right before the A {before}, right after {after}")
            if before and after:
                same = after[0] - before[0] == after[1] - before[1] == after[2] - before[2]
                note("PASS" if same else "FAIL", f"{tag}: the same change on all three purses ({before} -> {after}; "
                                                 f"the gold bag's value, applied once)")
                check_purse(a, b, tag, after[0])
                return after[0]
            return start
    note("FAIL", f"{tag}: the partner could not open chest {idx} from any side")
    return start


def wg_quest_reward(a, b, total):
    """QuestTracker.GiveRewards (the call the game's own quest completion makes)
    over the game's quest names until one pays gold."""
    names = [n.strip() for n in open(os.path.join(ROOT, "research", "quest_names.txt"), encoding="utf-8")
             if n.strip()]
    lst = ", ".join(f"'{n}'" for n in names)
    q = lua_lines(a, (
        f"local h = GetPlayerHero(); local tried = 0; for _, n in ipairs({{{lst}}}) do tried = tried + 1; "
        f"local before = Money.Get(h); local ok, e = pcall(QuestTracker.GiveRewards, h, n); "
        f"local d = Money.Get(h) - before; if d > 0 then print('WGQUEST paid ' .. n .. ' delta=' .. d .. "
        f"' tried=' .. tried) return end end print('WGQUEST none tried=' .. tried)"), "WGQUEST", 60)
    note("INFO", f"worldgold: quest reward {q}")
    paid = [int(m.group(1)) for m in (re.search(r"paid \S+ delta=(\d+)", l) for l in q) if m]
    if not paid:
        note("FAIL", "worldgold: no quest's reward paid gold through QuestTracker.GiveRewards")
        return total
    check_purse(a, b, "worldgold_quest_reward", total + paid[-1])
    return total + paid[-1]


def wg_partner_self_goto(b, xyz, side, r=1.2):
    """The partner's own Lua puts its own hero r m from a world spot and turns it
    to face the spot (its copy owns its position; objects the host spawns or
    moves are not in the partner's world: gold2 2026-10-26)."""
    x, y, z = xyz
    ox, oy = [(r, 0), (-r, 0), (0, r), (0, -r)][side]
    out = lua_lines(b, (
        f"local ok, e = pcall(function() local me = GetPlayerHenchman(); "
        f"local st = CVector3({x + ox:.3f}, {y + oy:.3f}, {z:.3f}); Physics.TeleportToPosition(me, st); "
        f"local f = CVector3({x:.3f}, {y:.3f}, {z:.3f}) - st; f:Normalise(); Physics.SetFacingVector(me, f); "
        f"print('WGSELF ok side {side}') end); if not ok then print('WGSELF error ' .. tostring(e)) end"), "WGSELF", 10)
    return bool(out) and out[-1].startswith("ok")


def worldgold_events(a, b, host_parts=True):
    """World gold in a joined campaign. host_parts: the host picks up a gold bag
    and opens a chest with A. Always: the partner opens a real level chest
    stocked with a gold bag, standing there by its own teleport, with its own A.
    The partner looks on while the host opens its chest (does it show open on
    the partner's copy?). Pass: the host's purse, the host's player 2 and the
    partner's mirror each change once, by the same amount. Returns the total."""
    v = read_purses(a, b, "worldgold_purse0")
    if not v:
        return None
    total = v[0]
    chests = lua_lines(a, WG_FIND, "WGCHEST", 20)
    cand = []
    for c in chests:
        m = re.match(r"(\d+) x=(-?[\d.]+) y=(-?[\d.]+) z=(-?[\d.]+) d=([\d.]+) gold=(-?\d+) open=(\w+) "
                     r"locked=(\w+) keys=(\d+)", c)
        if m and m.group(7) == "false" and m.group(8) == "false" and m.group(9) == "0":
            cand.append((float(m.group(5)), int(m.group(1)),
                         (float(m.group(2)), float(m.group(3)), float(m.group(4)))))
    cand.sort()  # nearest first
    if len(cand) < 2:
        note("FAIL", f"worldgold: fewer than 2 closed, unlocked chests here ({len(cand)})")
        return total
    # The host opens chest 1 while the partner stands by it and looks.
    c1 = cand[0]
    if wg_partner_self_goto(b, c1[2], 0, 2.5):
        time.sleep(2)
        b.shot("worldgold_observe_host_chest_before_partner.png")
    ok, total = wg_open_chest(a, b, a, c1[1], "hero", "worldgold_host_chest", total)
    time.sleep(2)
    b.shot("worldgold_observe_host_chest_after_partner.png")
    note("INFO", "worldgold: observation shots of the host's chest on the partner's copy: "
                 "worldgold_observe_host_chest_before/after_partner.png")
    if host_parts:
        golds = lua_lines(a, WG_SPAWN_GOLD, "WGGOLD", 20)
        piles = [re.match(r"(\d+) .*gold=(-?\d+)", gl) for gl in golds]
        piles = [(int(m.group(1)), int(m.group(2))) for m in piles if m]
        if piles:
            b.shot("worldgold_observe_host_bag_before_partner.png")
            total = wg_pickup(a, b, a, piles[0][0], "hero", piles[0][1], "worldgold_host_pickup", total)
            b.shot("worldgold_observe_host_bag_after_partner.png")
    # The partner opens chest 2 (stocked with a gold bag by the host) itself.
    c2 = cand[1]
    stocked = lua_lines(a, (f"local ok, e = pcall(Inventory.AddItemOfType, WG[{c2[1]}], 'ObjectInventoryGoldBag'); "
                            f"print('WGSTOCK ' .. tostring(ok) .. ' items=' .. "
                            f"tostring(Inventory.GetNumberOfItems(WG[{c2[1]}])))"), "WGSTOCK", 10)
    note("INFO", f"worldgold: chest {c2[1]} at {c2[2]} stocked with a gold bag: {stocked}")
    for side in range(4):
        if not wg_partner_self_goto(b, c2[2], side):
            continue
        time.sleep(1.5)
        b.shot(f"worldgold_partner_chest_at_side{side}.png")
        before = read_purses(a, b, f"worldgold_partner_chest_before_A_side{side}")
        st = ""
        # SetFacingVector does not turn a player-controlled hero: a short tap
        # of the stick in each of 8 directions turns it, then A.
        for k in range(9):
            if k:
                ang = (k - 1) * math.pi / 4
                b.send(f"stick L {math.cos(ang):.3f} {math.sin(ang):.3f} 120")
                time.sleep(0.5)
            b.send("press A 200")
            time.sleep(3)
            st = wg_chest_state(a, c2[1])
            if "open=true" in st:
                note("INFO", f"worldgold_partner_chest: opened after {k} stick tap(s)")
                break
        if "open=true" not in st:
            continue
        note("PASS", f"worldgold_partner_chest: the partner opened a real chest with its own A (side {side})")
        b.shot("worldgold_partner_chest_open.png")
        wg_dismiss(a, b, b, "worldgold_partner_chest")
        after = read_purses(a, b, "worldgold_partner_chest_after_A")
        if before and after:
            d = [after[i] - before[i] for i in range(3)]
            note("PASS" if d[0] == d[1] == d[2] else "FAIL",
                 f"worldgold_partner_chest: host purse, host's player 2 and the partner's mirror each changed by "
                 f"{d} ({before} -> {after}; the gold bag's value, once)")
            check_purse(a, b, "worldgold_partner_chest", after[0])
            total = after[0]
        return total
    note("FAIL", "worldgold_partner_chest: the partner could not open the chest from any side")
    return total


def host_has_partner(a):
    views = [l for l in a.lines() if "[lobby] view:" in l]
    return bool(views) and "(waiting for" not in views[-1]


def host_continue_lobby(a, tag):
    """The host at the title: Co-op -> Host: continue a co-op save -> the newest."""
    n_menu = a.count(r"main menu: Co-op entry added")
    a.past_title()
    menu_then_coop(a, tag, n_menu)
    wait_row(a, "Host: continue a co-op save", f"{tag}: the hub offers the co-op saves")
    a.select("Host: continue a co-op save")
    n = a.count(r"\[lobby\] open saves")
    a.press("E")
    a.wait(f"{tag}: the hub lists the co-op saves", r"\[lobby\] open saves", n, 10)
    n = a.count(r"\[lobby\] open host kind=continue")
    a.press("E")  # the newest campaign: the one just played
    return a.wait(f"{tag}: the save's lobby opens", r"\[lobby\] open host kind=continue", n, 10)


def partner_continue_join(a, b, tag):
    """A fresh partner copy joins the host's continue lobby; both start; join."""
    n = b.count(r"main menu: Co-op entry added")
    b.past_title()
    menu_then_coop(b, tag, n)
    wait_row(b, "Continue TestA", f"{tag}: the campaign is listed for the partner")
    partner_joins(b, "Continue TestA", tag)
    wait_row(a, "Start", f"{tag}: host lobby shows the partner")
    return both_ready_and_start(a, b, tag) and join_phase(a, b, tag)


def check_kept(a, b, tag, ref, total):
    """Player 2's items and look and the three purses against the reference."""
    time.sleep(10)
    now = inv_dump(a, tag)
    b.shot(f"{tag}_partner_look.png")
    if ref and now:
        grown = {r: (ref[0].get(r, 0), c) for r, c in now[0].items() if c != ref[0].get(r, 0)}
        lost = {r: (c, 0) for r, c in ref[0].items() if r not in now[0]}
        note("PASS" if not grown and not lost and now[1] == ref[1] else "FAIL",
             f"{tag}: player 2's items kept ({ref[1]} -> {now[1]}; changed: {grown or 'none'}"
             f"{'; missing: ' + str(lost) if lost else ''})")
        note("PASS" if now[2] == ref[2] else "FAIL", f"{tag}: player 2's look kept ({ref[2]} -> {now[2]})")
    v = read_purses(a, b, f"{tag}_purse")
    note("PASS" if v and v[0] == v[1] == v[2] == total else "FAIL",
         f"{tag}: purses {v} (expected {total} on all three)")
    return now or ref


def run_batch1():
    """One PC session for spec 001's open items (coordinator, 2026-10-26):
    world gold (T012); the partner killed mid-session x3: the host saves and
    returns to the menu, the partner rejoins through the hub (T013 P6, T029);
    the host alone cannot start; a third copy cannot take the seat (T029); the
    host killed right after a save x3, then continued (T032).
    `batch1 --from p6|t032` skips the finished phases after a fresh join."""
    phases = ["gold", "gold2", "p6", "t032"]  # gold2: the partner's gold and the quest reward only
    if "--phases" in sys.argv:
        run = set(sys.argv[sys.argv.index("--phases") + 1].split(","))
    else:
        first = sys.argv[sys.argv.index("--from") + 1] if "--from" in sys.argv else "gold"
        run = set(p for p in phases[phases.index(first):] if p != "gold2") if first in phases else set()
    if not run or not run <= set(phases):
        raise SystemExit(f"--from/--phases must name phases from {phases}")
    note("INFO", f"batch1: phases {[p for p in phases if p in run]}")
    setup_tomls(third=True)
    relay_log = open(os.path.join(OUT, "relay_batch1.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    c = None
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        menu_then_coop(a, "b1", a.n_menu)
        host_new_campaign(a, "b1")
        menu_then_coop(b, "b1", b.n_menu)
        wait_row(b, "Join TestA", "b1: the host's lobby is listed")
        partner_joins(b, "Join TestA", "b1")
        wait_row(a, "Start", "b1: host lobby shows the partner")
        if not both_ready_and_start(a, b, "b1") or not join_phase(a, b, "b1"):
            return
        time.sleep(8)
        # 1. World gold (T012).
        if "gold" in run or "gold2" in run:
            total = worldgold_events(a, b, host_parts="gold" in run)
        else:
            v = read_purses(a, b, "b1_purse0")
            total = v[0] if v and v[0] == v[1] == v[2] else None
        if total is None:
            return
        ref = inv_dump(a, "b1_ref")
        # 2. The partner killed mid-session, three times (T013 P6, T029).
        for k in ((1, 2, 3) if "p6" in run else ()):
            tag = f"p6_{k}"
            b.stop()  # a kill, not a quit
            n = a.count(r"host: back at the front end")
            if not a.wait(f"{tag}: partner killed -> host saves and returns to the front end",
                          r"host: back at the front end", n, 240):
                return
            time.sleep(10)
            if not host_continue_lobby(a, tag):
                return
            if k == 1:
                # The host alone cannot start the co-op save.
                rows = [r[0] for r in a.rows()]
                n = a.count(r"\[lobby\] host start")
                if any(r.startswith("Start") for r in rows):
                    a.select("Start")
                    a.press("E")
                time.sleep(8)
                note("PASS" if a.count(r"\[lobby\] host start") == n and not host_has_partner(a) else "FAIL",
                     f"{tag}: the host alone cannot start the co-op save (rows {rows})")
            if k == 2:
                # A third copy (a stranger) cannot take the partner's seat.
                c = Game("mod-c", "stranger")
                c.start({"FABLE2_TEST_INPUT": "1"})
                n = c.count(r"main menu: Co-op entry added")
                c.past_title()
                menu_then_coop(c, f"{tag}_stranger", n)
                time.sleep(6)
                seen = [r[0] for r in c.rows()]
                row = next((r for r in seen if "TestA" in r), None)
                note("INFO", f"{tag}: the stranger's hub rows {seen}")
                if row:
                    c.select(row)
                    c.press("E")
                    time.sleep(15)
                views = [l for l in a.lines() if "[lobby] view:" in l]
                note("PASS" if not host_has_partner(a) else "FAIL",
                     f"{tag}: the stranger was not seated in the host's lobby "
                     f"({views[-1].split('[lobby] view: ', 1)[1][:160] if views else 'no view'})")
                c.stop()
                c = None
            b = Game("mod-b", "partner")
            b.start({"FABLE2_TEST_INPUT": "1"})
            if not partner_continue_join(a, b, tag):
                return
            ref = check_kept(a, b, tag, ref, total)
        # 3. The host killed right after a save, three times (T032). The
        # reference purse is read again first: player 2 can pick up gold on its
        # own meanwhile (001pl2 run: +1, shared once on all three).
        if "t032" in run:
            v = read_purses(a, b, "t032_purse_ref")
            if v and v[0] == v[1] == v[2]:
                total = v[0]
        for k in ((1, 2, 3) if "t032" in run else ()):
            tag = f"t032_{k}"
            out = lua_lines(a, "print('B1SAVE ' .. tostring(pcall(AutoSaveGame, true, true)))", "B1SAVE", 20)
            note("INFO", f"{tag}: save {out}")
            time.sleep(4 if k == 1 else 8)  # k=1: right after the save; then a little later
            a.stop()
            b.stop()
            time.sleep(5)
            a, b = Game("mod-a", "host"), Game("mod-b", "partner")
            a.start({"FABLE2_TEST_INPUT": "1"})
            b.start({"FABLE2_TEST_INPUT": "1"})
            if not host_continue_lobby(a, tag) or not partner_continue_join(a, b, tag):
                return
            ref = check_kept(a, b, tag, ref, total)
        for g in (a, b):
            menu = [l.split("main menu event ", 1)[1] for l in g.lines() if "main menu event " in l]
            note("INFO", f"b1: {g.who}: last menu events {menu[-12:]}")
    finally:
        for g in (a, b) + ((c,) if c else ()):
            g.stop()
        relay.kill()


def pause_menu_save(a, tag):
    """The host saves through the real pause menu: Start, Up to Save, A (A again
    for a confirmation), then closes the menu. Returns True when the game's save
    ran (a new script save table)."""
    n = a.count(r"\[lua\] saving: script save table")
    a.press("Start")
    time.sleep(1.5)
    p = a.shot(f"{tag}_pause_open.png")
    if cover_metrics(p)[0] < 0.05:  # no parchment: a quest card took the Start (savestress, 2026-10-26)
        time.sleep(3)
        a.press("Start")
        time.sleep(1.5)
    a.press("Up")
    time.sleep(0.8)
    a.shot(f"{tag}_pause_on_save.png")
    a.press("E")
    for _ in range(3):
        time.sleep(2.5)
        if a.count(r"\[lua\] saving: script save table") > n:
            break
        a.shot(f"{tag}_after_A.png")
        a.press("E")  # a confirmation box
    ok = a.wait(f"{tag}: the pause-menu Save ran the game's save", r"\[lua\] saving: script save table", n, 20)
    time.sleep(4)
    for _ in range(3):  # back to the world
        a.press("B")
        time.sleep(1.0)
    a.shot(f"{tag}_back_in_world.png")
    return ok


def run_savestress():
    """The in-world save crash (batch1 2026-10-26: a console chunk ran during an
    AutoSaveGame and the host crashed): host and partner in the world with
    feature 002's kit ticking, 25 AutoSaveGame at 1-6 s spacing and 5 saves
    through the pause menu. Pass: no crash, every save seen (script save table),
    console scripts held while saving, then the partner leaves, the host saves
    and continues the co-op save through the hub, and player 2's items, look and
    the purses are kept."""
    import random
    rnd = random.Random(26)
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_savestress.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        menu_then_coop(a, "ss", a.n_menu)
        host_new_campaign(a, "ss")
        menu_then_coop(b, "ss", b.n_menu)
        wait_row(b, "Join TestA", "ss: the host's lobby is listed")
        partner_joins(b, "Join TestA", "ss")
        wait_row(a, "Start", "ss: host lobby shows the partner")
        if not both_ready_and_start(a, b, "ss") or not join_phase(a, b, "ss"):
            return
        time.sleep(10)
        ref = inv_dump(a, "ss_ref")
        v = read_purses(a, b, "ss_purse0")
        total = v[0] if v else None
        n_tab = a.count(r"\[lua\] saving: script save table")
        n_hold = a.count(r"\[lua\] saving: holding")
        kit0 = a.count(r"publishing 'kit_state'")
        issued = 0
        for k in range(25):
            out = lua_lines(a, f"print('SSAVE {k} ' .. tostring(pcall(AutoSaveGame, true, true)))", "SSAVE", 30)
            issued += 1
            if a.count(r"Unhandled guest") or b.count(r"Unhandled guest"):
                note("FAIL", f"ss: a crash after AutoSaveGame #{k} ({out})")
                return
            time.sleep(rnd.uniform(1.0, 6.0))
        for k in range(5):
            pause_menu_save(a, f"ss_menu{k}")
            issued += 1
            if a.count(r"Unhandled guest") or b.count(r"Unhandled guest"):
                note("FAIL", f"ss: a crash after pause-menu save #{k}")
                return
        time.sleep(10)
        tables = a.count(r"\[lua\] saving: script save table") - n_tab
        holds = a.count(r"\[lua\] saving: holding") - n_hold
        kits = a.count(r"publishing 'kit_state'") - kit0
        crashes = a.count(r"Unhandled guest") + b.count(r"Unhandled guest")
        note("PASS" if crashes == 0 else "FAIL", f"ss: {issued} saves, {crashes} crash(es)")
        note("PASS" if tables >= issued else "FAIL", f"ss: {tables} script save table(s) for {issued} saves")
        note("PASS" if holds > 0 else "FAIL", f"ss: console scripts held while saving {holds} time(s) "
                                              f"(002's kit published {kits} times meanwhile)")
        v = read_purses(a, b, "ss_purse_before_leave")
        total = v[0] if v and v[0] == v[1] == v[2] else total
        # Every save loads: the partner leaves (the host saves and returns to the
        # front end), the host continues the co-op save, the partner rejoins.
        b.stop()
        n = a.count(r"host: back at the front end")
        if not a.wait("ss: partner gone -> host saves and returns to the front end",
                      r"host: back at the front end", n, 240):
            return
        time.sleep(10)
        b = Game("mod-b", "partner")
        b.start({"FABLE2_TEST_INPUT": "1"})
        if not host_continue_lobby(a, "ss_continue") or not partner_continue_join(a, b, "ss_continue"):
            return
        if total is not None:
            check_kept(a, b, "ss_continue", ref, total)
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def probe_cpu(g, tag, secs=20):
    """CPU share of the state probe's thread over secs (its OS thread id is the
    [tNNNN] prefix of its [state] lines; the probe is an unnamed std::thread)."""
    ids = re.findall(r"\[t(\d+)\] \[state\]", "\n".join(g.lines()))
    if not ids or not g.proc:
        note("INFO", f"{tag}: probe thread not found")
        return None
    tid = int(ids[-1])

    def cpu():
        for t in g.proc.threads():
            if t.id == tid:
                return t.user_time + t.system_time
        return None
    c0, t0 = cpu(), time.time()
    time.sleep(secs)
    c1, t1 = cpu(), time.time()
    if c0 is None or c1 is None:
        note("INFO", f"{tag}: probe thread {tid} not in the process")
        return None
    share = (c1 - c0) / (t1 - t0)
    note("INFO", f"{tag}: state probe thread t{tid} used {share:.1%} of a core over {secs} s")
    return share


def run_solowalk():
    """Solo play with the light probe (coop/001-probe-light): New Game into the
    world, the probe's front-end scans pause while the game's Lua runs; back at
    the front end (the game's own QuitToFrontEnd) they resume, the menu is
    detected again and the Co-op entry is built. No crash."""
    setup_tomls()
    a = Game("mod-a", "host")
    a.start({"FABLE2_TEST_INPUT": "1"})
    try:
        a.past_title()
        time.sleep(3)
        a.shot("solowalk_menu.png")
        n = a.count(r"front-end scans paused")
        a.press("E")  # New Game: the menu's first row, highlighted at the title pass
        for k in range(8):  # the hero cards come up ~20 s later: a 200 ms A picks the first
            time.sleep(10)
            if a.count(r"front-end scans paused") > n:
                break
            if k == 3:
                a.send("press Left 200")  # no card highlighted yet: pick one
                time.sleep(1)
            a.send("press A 200")
        ok = a.wait("solowalk: front-end scans paused in the world", r"front-end scans paused", n, 180)
        a.shot("solowalk_world.png")
        for _ in range(4):  # walk a little
            a.send("stick L 0 1 3000")
            time.sleep(4)
        a.shot("solowalk_walked.png")
        world = probe_cpu(a, "solowalk_world")
        nr = a.count(r"front-end scans resumed")
        nm = a.count(r"main menu: Co-op entry added")
        a.send("lua print('[coop] QuitToFrontEnd: ' .. tostring(pcall(Debug.QuitToFrontEnd)))")
        ok &= a.wait("solowalk: front-end scans resumed at the front end", r"front-end scans resumed", nr, 120)
        time.sleep(5)
        a.past_title()
        ok &= a.wait("solowalk: main menu built with the Co-op entry", r"main menu: Co-op entry added", nm, 60)
        a.shot("solowalk_back_at_menu.png")
        menu = probe_cpu(a, "solowalk_menu")
        if world is not None:
            note("PASS" if world < 0.05 else "FAIL",
                 f"solowalk: the probe thread in the world {world:.1%} of a core (001fps: ~17.5%), "
                 f"at the menu {menu if menu is None else f'{menu:.1%}'}")
        crashes = a.count(r"Unhandled guest")
        note("PASS" if ok and crashes == 0 else "FAIL", f"solowalk: paused/resumed and menu again, {crashes} crash(es)")
    finally:
        a.stop()


def lf_empty_boot():
    """An empty-saves cold boot (mod-c with only its profile): the menu builds
    with the Co-op entry right after New Game and the game keeps running."""
    import shutil
    saves = os.path.join(ROOT, "mod-c", "saves")
    bak = saves + ".livefix-bak"
    if os.path.exists(bak):
        raise SystemExit(f"{bak} exists: restore it first")
    os.rename(saves, bak)
    try:
        os.makedirs(saves)
        for keep in ("4D5307F1", "achievements"):
            if os.path.isdir(os.path.join(bak, keep)):
                shutil.copytree(os.path.join(bak, keep), os.path.join(saves, keep))
        c = Game("mod-c", "empty")
        c.start({"FABLE2_TEST_INPUT": "1"})
        try:
            n = c.count(r"main menu: Co-op entry added")
            c.past_title()
            ok = c.wait("empty saves: the menu builds with the Co-op entry", r"main menu: Co-op entry added", n, 90)
            time.sleep(15)
            crashed = c.count(r"Unhandled guest access violation") > 0 or not c.proc.is_running()
            note("FAIL" if crashed or not ok else "PASS", f"empty saves: no crash 15 s after the menu (alive={c.proc.is_running()})")
            c.shot("lf_empty_menu.png")
            if ok and not crashed:
                ok = open_coop_entry(c, "lf_empty")
                note("PASS" if ok else "FAIL", "empty saves: the Co-op entry opens the hub")
        finally:
            c.stop()
    finally:
        time.sleep(3)
        shutil.rmtree(saves, ignore_errors=True)
        os.rename(bak, saves)
        note("INFO", "empty saves: mod-c's saves restored")


def lf_session(female, gold_only=False):
    """Host (mod-a) + partner (mod-b) in a new campaign, the partner's seat male
    or female. Checks player 2's body on both PCs, then (male run only) the
    henchman gold share, camera watch / pitch / R3, a 6 min idle wage check,
    the partner's purse in its UI and both pause menus."""
    tag = "lf_f" if female else "lf_m"
    setup_tomls()
    with open(os.path.join(ROOT, "mod-b", "fable_2.toml"), "a", encoding="utf-8", newline="\n") as f:
        f.write("coop_cam_watch = true\n")
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        menu_then_coop(a, tag, a.n_menu)
        host_new_campaign(a, tag)
        menu_then_coop(b, tag, b.n_menu)
        wait_row(b, "Join TestA", f"{tag}: the host's lobby is listed")
        partner_joins(b, "Join TestA", tag)
        wait_row(a, "Start", f"{tag}: host lobby shows the partner")
        if female:
            b.select("Your hero")
            b.press("Right")
            time.sleep(2)
            note("INFO", f"{tag}: partner rows after the gender change {[r[0] for r in b.rows()]}")
        time.sleep(2)
        hv = [l for l in a.lines() if "[lobby] view:" in l][-1:]
        note("INFO", f"{tag}: host lobby view {hv[0][-260:] if hv else ''}")
        if not both_ready_and_start(a, b, tag) or not join_phase(a, b, tag):
            return
        want = "CreatureHeroFemale" if female else "CreatureHero "
        for g in (a, b):
            body = [l for l in g.lines() if "player 2 body:" in l][-1:]
            ok = bool(body) and want in body[0] + " " and "from the lobby" in body[0]
            note("PASS" if ok else "FAIL", f"{tag}: {g.who}: player 2 body {body[0][60:] if body else '(none)'}")
        start = [l for l in a.lines() if "host start:" in l][-1:]
        note("INFO", f"{tag}: {start[0][60:] if start else ''}")
        time.sleep(1)
        b.shot(f"{tag}_partner_right_after_spawn.png")
        a.shot(f"{tag}_host_right_after_spawn.png")
        time.sleep(6)
        b.shot(f"{tag}_partner_view.png")
        a.shot(f"{tag}_host_view.png")
        # Where the henchman gold share setter really lives (the plain call fails).
        probe = ("local t = {} for k, v in pairs(_G) do if type(v) == 'table' then "
                 "local ok, f = pcall(function() return v.SetHenchmanGoldValue end) "
                 "if ok and f then t[#t + 1] = k end end end "
                 "print('LFGOLD owners ' .. table.concat(t, ',')) "
                 "print('LFGOLD type ' .. type(Multiplayer) .. ' ' .. tostring(Multiplayer and Multiplayer.SetHenchmanGoldValue)) "
                 "local ok1, e1 = pcall(function() return Multiplayer.SetHenchmanGoldValue(0) end) "
                 "print('LFGOLD dot ' .. tostring(ok1) .. ' ' .. tostring(e1)) "
                 "local ok2, e2 = pcall(function() return Multiplayer:SetHenchmanGoldValue(0) end) "
                 "print('LFGOLD colon ' .. tostring(ok2) .. ' ' .. tostring(e2))")
        note("INFO", f"{tag}: gold probe {lua_lines(a, probe, 'LFGOLD', 15)}")
        if female:
            return
        if gold_only:
            lf_gold(a, tag)
            lf_partner_ui_gold(b, tag)
            return
        # Camera: the host walks while the partner holds still (the jitter);
        # the watch names whatever writes the partner's view.
        b.send("cvar coop_cam_trace true")
        for k in range(8):
            a.send("stick L 0 1 2500")
            time.sleep(1.2)
            b.shot(f"{tag}_jitter_{k}_partner.png")
            time.sleep(1.4)
        a.send("stick L 0 -1 2500")
        time.sleep(3)
        watch = [l[24:] for l in b.lines() if "[camwatch]" in l]
        note("INFO" if watch else "FAIL", f"{tag}: camera watch lines {len(watch)}: {watch[:12]}")
        # Pitch and R3 on the partner (the trace ends itself after 30 s: again).
        b.send("cvar coop_cam_trace false")
        time.sleep(0.5)
        b.send("cvar coop_cam_trace true")
        time.sleep(1)
        n_tr = len(b.lines())
        b.send("stick R 0 1 1200")
        time.sleep(1.5)
        b.shot(f"{tag}_pitch_up.png")
        b.send("stick R 0 -1 2400")
        time.sleep(2.6)
        b.shot(f"{tag}_pitch_down.png")
        n_r3 = b.count(r"R3: camera recentered")
        b.send("press RS 150")
        time.sleep(1.5)
        b.shot(f"{tag}_after_r3.png")
        note("PASS" if b.count(r"R3: camera recentered") > n_r3 else "FAIL", f"{tag}: R3 recenters the partner's camera")
        zs = []
        for l in b.lines()[n_tr:]:
            m = re.search(r"trace dt .* cam ([-\d.]+) ([-\d.]+) ([-\d.]+) host", l)
            if m:
                zs.append(float(m.group(3)))
        note("PASS" if zs and max(zs) - min(zs) > 1.0 else "FAIL",
             f"{tag}: right stick Y moves the partner's camera height ({min(zs, default=0):.2f}..{max(zs, default=0):.2f})")
        # Gold watch (coop_gold_watch): find player 2's gold by value, keep the
        # address that follows a second value, watch it through the idle.
        for k, v in enumerate((9871, 9872, 9873)):
            a.send(f"lua Money.Set(GetPlayerHenchman(), {v}) print('LFSET {v}')")
            time.sleep(3)
            a.send(f"cvar coop_gold_watch {'find' if k == 0 else 'pick'}:{v}")
            time.sleep(4 if k == 0 else 1.5)  # the first step scans the heap
        a.send("cvar coop_gold_watch watch")
        time.sleep(1.5)
        note("INFO", f"{tag}: gold watch {[l[24:240] for l in a.lines() if '[goldwatch]' in l][-4:]}")
        # 6 min idle: the stock henchman gold share must not pay player 2.
        n_p = len(a.lines())
        note("INFO", f"{tag}: idle 6 min (wage check)")
        time.sleep(360)
        ticks = [l[60:] for l in a.lines()[n_p:] if "purse: host" in l]
        note("INFO", f"{tag}: gold writes during the idle {[l[24:240] for l in a.lines()[n_p:] if '[goldwatch]' in l][:12]}")
        writes = [l for l in a.lines()[n_p:] if "[goldwatch] gold" in l]
        note("PASS" if not ticks and not writes else "FAIL",
             f"{tag}: player 2's gold unchanged in 6 min idle (purse lines {ticks[:3]}; gold writes {len(writes)})")
        # The partner's purse in its own UI, against the host's total.
        before = read_purses(a, b, f"{tag}_ui")
        b.send("press Start 200")
        time.sleep(2.5)
        b.shot(f"{tag}_partner_pause.png")
        b.send("press B 200")
        time.sleep(2)
        mir = [l[60:] for l in b.lines() if "purse: mirrored" in l or "purse: host total" in l][-3:]
        note("INFO", f"{tag}: purses {before}; partner mirror lines {mir}")
        # Both pause menus (Leave / Quit): screenshots of what each side has.
        a.send("press Start 200")
        time.sleep(2.5)
        a.shot(f"{tag}_host_pause.png")
        a.send("press Up 150")  # the cursor opens on Quests/Maps; Save is above
        time.sleep(1.2)
        a.send("press A 200")
        time.sleep(2.5)
        a.shot(f"{tag}_host_save_menu.png")
        for _ in range(3):
            a.send("press B 200")
            time.sleep(1.5)
        b.send("press Start 200")
        time.sleep(2.5)
        b.shot(f"{tag}_partner_pause2.png")
        # The partner's Kick row (the bottom one): what does it do on the joiner?
        for _ in range(6):
            b.send("press Down 150")
            time.sleep(0.9)
        b.shot(f"{tag}_partner_on_quit.png")
        na, nb = len(a.lines()), len(b.lines())
        b.send("press A 200")
        time.sleep(2.5)
        b.shot(f"{tag}_partner_kick_1.png")
        b.send("press A 200")
        time.sleep(8)
        b.shot(f"{tag}_partner_kick_2.png")
        a.shot(f"{tag}_host_after_partner_kick.png")
        keys = ("coop]", "joingate", "partner", "henchman", "XSession", "front end", "QuitToFrontEnd", "Unhandled")
        note("INFO", f"{tag}: host after the partner's Kick: {[l[24:200] for l in a.lines()[na:] if any(k in l for k in keys)][:15]}")
        note("INFO", f"{tag}: partner after its Kick: {[l[24:200] for l in b.lines()[nb:] if any(k in l for k in keys)][:15]}")
        note("INFO", f"{tag}: alive after the Kick: host {a.proc.is_running()}, partner {b.proc.is_running()}")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


OAK_SLOT = os.path.join(ROOT, "mod-a", "saves", "B13EBABEBABEBABE", "4D5307F1", "00000001", "Hero044")
OAK_CID = "65065FDB022E88AC"


def oak_join(a, b, tag):
    """The Oakfield copy (newest by its mainsave), hosted through Co-op ->
    continue; a fresh partner joins. True once player 2 spawned."""
    os.utime(os.path.join(OAK_SLOT, "mainsave.bin"))
    if not host_continue_lobby(a, tag):
        return False
    hv = [l for l in a.lines() if "[lobby] open host kind=continue" in l][-1:]
    ok = bool(hv) and OAK_CID in hv[0]
    note("PASS" if ok else "FAIL", f"{tag}: the Oakfield campaign's lobby ({hv[0][60:] if hv else ''})")
    if not ok:
        return False
    return partner_continue_join(a, b, tag)


def lf_level(a, b, tag):
    q = "print('LFLEVEL ' .. tostring(ScriptFunction.GetLevelName()))"
    for g in (a, b):
        out = lua_lines(g, q, "LFLEVEL", 15)
        note("PASS" if out and "ravenscar" in out[-1].lower() else "FAIL",
             f"{tag}: {g.who}: level {out[-1] if out else '(no answer)'} (Ravenscar = Oakfield)")


def lf_oakfield():
    """Josh's Oakfield save (mod-a Hero044, a co-op copy of his real Hero000):
    the partner joins in Oakfield; its Abilities and Weapons pages; it leaves
    from its own pause menu (the host carries on as designed, purse saved);
    then, joined again, the host's Save -> Save and Quit sends the partner back
    to its menu. Screenshots of the partner's view throughout."""
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_lf_oak.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        if not oak_join(a, b, "lf_oak"):
            return
        time.sleep(8)
        lf_level(a, b, "lf_oak")
        a.shot("lf_oak_host_world.png")
        b.shot("lf_oak_partner_world.png")
        purse0 = read_purses(a, b, "lf_oak_start")
        # The partner's wheel: rows, Abilities, Weapons.
        nb = len(b.lines())
        b.send("press Start 200")
        time.sleep(2.5)
        b.shot("lf_oak_partner_wheel.png")
        rows = [l[24:160] for l in b.lines()[nb:] if "partner wheel" in l or "full wheel" in l]
        note("PASS" if any("Kick row is the henchman's Quit" in r for r in rows) and any("Save row left out" in r for r in rows)
             else "FAIL", f"lf_oak: partner wheel built without Save and with Quit for Kick ({rows[:6]})")
        # The wheel opens on Abilities (row 1; Quests/Maps above it, Save left out).
        def page(downs, shot, want):
            n0 = len(b.lines())
            for _ in range(downs):
                b.send("press Down 150")
                time.sleep(1)
            b.send("press A 200")
            time.sleep(3)
            b.shot(shot)
            chosen = [l[24:160] for l in b.lines()[n0:] if "chosen" in l]
            note("PASS" if any(f"'{want}'" in c for c in chosen) else "FAIL", f"lf_oak: partner page {want}: {chosen[:2]}")
            for _ in range(4):  # back out and close the wheel
                b.send("press B 200")
                time.sleep(1.2)
            time.sleep(1.5)

        b.send("press B 200")
        time.sleep(1.5)
        for downs, shot, want in ((0, "lf_oak_partner_abilities.png", "Abilities"),
                                  (2, "lf_oak_partner_weapons.png", "Weapons")):
            b.send("press Start 200")
            time.sleep(2.5)
            page(downs, shot, want)
        # Leave: the bottom row, 6 below Abilities (Items, Weapons, Clothing, Logbook, Options, Quit).
        b.send("press Start 200")
        time.sleep(2.5)
        for _ in range(6):
            b.send("press Down 150")
            time.sleep(1)
        b.shot("lf_oak_partner_on_quit.png")
        na, nb = len(a.lines()), len(b.lines())
        b.send("press A 200")
        time.sleep(3)
        b.shot("lf_oak_partner_quit_question.png")  # the wheel closing; no question (fable2_coop_kit.h)
        t = time.time()
        fe = r"\[state\].*-> (MainMenu|MainMenuMovie|PressAScreen)"
        while time.time() - t < 120 and not any(re.search(fe, l) for l in b.lines()[nb:]):
            time.sleep(2)
        time.sleep(4)
        b.shot("lf_oak_partner_after_leave.png")
        a.shot("lf_oak_host_after_leave.png")
        keys = ("[coop]", "joingate", "lobby]", "XSession", "Unhandled", "coopkit")
        note("INFO", f"lf_oak: partner after Quit {[l[24:200] for l in b.lines()[nb:] if any(k in l for k in keys)][:14]}")
        note("INFO", f"lf_oak: host after the partner's Quit {[l[24:200] for l in a.lines()[na:] if any(k in l for k in keys)][:14]}")
        back = any(re.search(fe, l) for l in b.lines()[nb:])
        note("PASS" if back and b.proc.is_running() else "FAIL", "lf_oak: the partner's Quit takes it back to its main menu")
        host_ok = a.wait("lf_oak: host saves and returns to the front end after the partner left",
                         r"host: back at the front end", a.count(r"host: back at the front end"), 240)
        note("PASS" if host_ok and a.proc.is_running() else "FAIL", "lf_oak: the host carries on as designed after the partner left")
        # Joined again; then the host quits from Save -> Save and Quit.
        time.sleep(8)
        b.stop()
        b = Game("mod-b", "partner")
        b.start({"FABLE2_TEST_INPUT": "1"})
        a.stop()
        a = Game("mod-a", "host")
        a.start({"FABLE2_TEST_INPUT": "1"})
        if not oak_join(a, b, "lf_oak2"):
            return
        time.sleep(8)
        purse1 = read_purses(a, b, "lf_oak_rejoin")
        note("INFO", f"lf_oak: purses at the start {purse0}, after leaving and joining again {purse1}")
        na, nb = len(a.lines()), len(b.lines())
        a.send("press Start 200")
        time.sleep(2.5)
        a.send("press Up 150")
        time.sleep(1)
        a.send("press A 200")
        time.sleep(2.5)
        a.shot("lf_oak_host_save_menu.png")
        a.send("press Down 150")
        time.sleep(1)
        a.shot("lf_oak_host_on_save_and_quit.png")
        a.send("press A 200")
        time.sleep(3)
        a.shot("lf_oak_host_save_and_quit_1.png")
        a.send("press A 200")
        t = time.time()
        while time.time() - t < 150:
            if any(re.search(fe, l) for l in b.lines()[nb:]) and any(re.search(fe, l) for l in a.lines()[na:]):
                break
            time.sleep(2)
        time.sleep(4)
        b.shot("lf_oak_partner_after_host_quit.png")
        a.shot("lf_oak_host_after_quit.png")
        note("INFO", f"lf_oak: host after Save and Quit {[l[24:200] for l in a.lines()[na:] if any(k in l for k in keys)][:14]}")
        note("INFO", f"lf_oak: partner after the host quit {[l[24:200] for l in b.lines()[nb:] if any(k in l for k in keys)][:14]}")
        note("PASS" if any(re.search(fe, l) for l in b.lines()[nb:]) and b.proc.is_running() else "FAIL",
             "lf_oak: the host's Save and Quit returns the partner to its front end")
        note("PASS" if any(re.search(fe, l) for l in a.lines()[na:]) and a.proc.is_running() else "FAIL",
             "lf_oak: the host is back at its front end")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def lf_gold(a, tag, idle_s=300):
    """coop_gold_watch: player 2's gold found by value, then watched through
    an idle; the adds to it are logged with their caller."""
    for k, v in enumerate((9871, 9872, 9873)):
        a.send(f"lua Money.Set(GetPlayerHenchman(), {v}) print('LFSET {v}')")
        time.sleep(3)
        a.send(f"cvar coop_gold_watch {'find' if k == 0 else 'pick'}:{v}")
        time.sleep(4 if k == 0 else 1.5)
    a.send("cvar coop_gold_watch watch")
    time.sleep(1.5)
    n_p = len(a.lines())
    note("INFO", f"{tag}: gold watch {[l[24:240] for l in a.lines() if '[goldwatch]' in l][-4:]}; idle {idle_s} s")
    time.sleep(idle_s)
    adds = [l[24:240] for l in a.lines()[n_p:] if "[goldwatch] add" in l]
    note("PASS" if not adds else "FAIL", f"{tag}: adds to player 2's gold while idle: {adds[:8]}")



def lf_partner_ui_gold(b, tag):
    """Item 4: which creature's Money the partner's pause menu shows. The
    mirror sets GetPlayerHenchman(); the menu showed 0. Mark the partner's
    GetPlayerHero() with 4242 and look."""
    out = lua_lines(b, "print('LFUI hero ' .. tostring(Money.Get(GetPlayerHero())) .. ' hench ' .. "
                       "tostring(Money.Get(GetPlayerHenchman())))", "LFUI", 10)
    m = re.search(r"hero (\d+) hench (\d+)", out[-1]) if out else None
    note("PASS" if m and m.group(1) == m.group(2) else "FAIL",
         f"{tag}: the partner's own hero shows the shared purse (what its pause menu reads): {out}")
    b.send("lua Money.Set(GetPlayerHero(), 4242) print('LFUI set')")
    time.sleep(2)
    b.send("press Start 200")
    time.sleep(2.5)
    b.shot(f"{tag}_partner_pause_hero4242.png")
    for _ in range(3):
        b.send("press B 200")
        time.sleep(1.2)
    out = lua_lines(b, "print('LFUI hero ' .. tostring(Money.Get(GetPlayerHero())) .. ' hench ' .. "
                       "tostring(Money.Get(GetPlayerHenchman())))", "LFUI", 10)
    note("INFO", f"{tag}: partner purses after the mark {out} (screenshot {tag}_partner_pause_hero4242.png)")



def lf_chest_open(a):
    out = lua_lines(a, "print('LFOPEN ' .. tostring(LFC and Chest.IsOpen(LFC)))", "LFOPEN", 10)
    return out[-1] if out else "?"


def lf_use_try(g, a, other, who, tag):
    """g presses A facing the chest (twice if still closed: a tutorial line can
    take the first); the chest's state on the host before and after, and the
    probe lines of both PCs from then on."""
    na, nb = len(g.lines()), len(other.lines())
    before = lf_chest_open(a)
    for k in range(2):
        g.send("press A 200")
        time.sleep(5)
        g.shot(f"{tag}_{who}_after_A{k}.png")
        after = lf_chest_open(a)
        note("INFO", f"{tag}: {who} A #{k + 1}: chest open {before} -> {after}")
        if after == "true":
            break
    other.shot(f"{tag}_{who}_after_A_other.png")
    for x, name in ((g, who), (other, "other")):
        lines = [l[24:300] for l in x.lines()[na if x is g else nb:] if "[useprobe]" in l]
        note("INFO", f"{tag}: {who} pressed A; {name} PC useprobe: {lines[:30]}")


def lf_use():
    """Item 7, experiment 1 (coop_use_probe): the partner, then the host, press
    A at the same closed chest; where does the partner's use stop?"""
    tag = "lf_use"
    setup_tomls()
    for folder in ("mod-a", "mod-b"):
        with open(os.path.join(ROOT, folder, "fable_2.toml"), "a", encoding="utf-8", newline="\n") as f:
            f.write("coop_use_probe = true\n")
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        menu_then_coop(a, tag, a.n_menu)
        host_new_campaign(a, tag)
        menu_then_coop(b, tag, b.n_menu)
        wait_row(b, "Join TestA", f"{tag}: the host's lobby is listed")
        partner_joins(b, "Join TestA", tag)
        wait_row(a, "Start", f"{tag}: host lobby shows the partner")
        if not both_ready_and_start(a, b, tag) or not join_phase(a, b, tag):
            return
        time.sleep(10)
        # The closed chest nearest the host's hero.
        out = lua_lines(a, (
            "local ok, e = pcall(function() local h = GetPlayerHero(); local best, bd = nil, 1e9; "
            "for _, o in pairs(SearchTools.GetSearchResults(SearchTools.FilterWithEC("
            "SearchTools.StartNewSearch('object'), Chest.GetECType()))) do "
            "if not Chest.IsOpen(o) and not Chest.IsLocked(o) then local d = (o:GetPosition() - h:GetPosition()):GetLength(); "
            "if d < bd then best, bd = o, d end end end; "
            "if not best then print('LFCHEST none') return end; LFC = best; local p = best:GetPosition(); "
            "print(string.format('LFCHEST %.2f %.2f %.2f %.1f', p:GetX(), p:GetY(), p:GetZ(), bd)) end); "
            "if not ok then print('LFCHEST error ' .. tostring(e)) end"), "LFCHEST", 15)
        note("INFO", f"{tag}: nearest chest {out}")
        if not out or len(out[-1].split()) < 4:
            return
        x, y, z = (float(v) for v in out[-1].split()[:3])
        # The partner first, at the closed chest, from its own Lua (its copy owns its position).
        out = lua_lines(b, (
            f"local ok, e = pcall(function() local me = GetPlayerHenchman(); local best, bd = nil, 1e9; "
            f"for _, o in pairs(SearchTools.GetSearchResults(SearchTools.FilterWithEC("
            f"SearchTools.StartNewSearch('object'), Chest.GetECType()))) do "
            f"local d = (o:GetPosition() - CVector3({x}, {y}, {z})):GetLength(); "
            f"if d < bd then best, bd = o, d end end; "
            f"if not best or bd > 3 then print('LFPTP none ' .. tostring(bd)) return end; local p = best:GetPosition(); "
            f"local st = CVector3(p:GetX() + 1.2, p:GetY(), p:GetZ()); Physics.TeleportToPosition(me, st); "
            f"local f = p - st; f:Normalise(); Physics.SetFacingVector(me, f); "
            f"print(string.format('LFPTP ok (%.1f m from the host spot)', bd)) end); "
            f"if not ok then print('LFPTP error ' .. tostring(e)) end"), "LFPTP", 15)
        note("INFO", f"{tag}: partner to the chest: {out}")
        if out and out[-1].startswith("ok"):
            time.sleep(2)
            b.shot(f"{tag}_partner_at_chest.png")
            lf_use_try(b, a, a, "partner", tag)
        # Then the host at the same chest (its own copy: the teleport sticks).
        lua_lines(a, (f"local h = GetPlayerHero(); local st = CVector3({x + 1.2:.2f}, {y:.2f}, {z:.2f}); "
                      f"Physics.TeleportToPosition(h, st); local f = CVector3({x:.2f}, {y:.2f}, {z:.2f}) - st; "
                      f"f:Normalise(); Physics.SetFacingVector(h, f); print('LFHOSTAT ok')"), "LFHOSTAT", 10)
        time.sleep(3)
        a.shot(f"{tag}_host_at_chest.png")
        lf_use_try(a, a, b, "host", tag)
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()



def lf_start_campaign(tag, extra_toml=()):
    """Host (mod-a) + partner (mod-b) in a new campaign; (a, b, relay) or None."""
    setup_tomls()
    for folder in ("mod-a", "mod-b"):
        if extra_toml:
            with open(os.path.join(ROOT, folder, "fable_2.toml"), "a", encoding="utf-8", newline="\n") as f:
                f.write("".join(line + "\n" for line in extra_toml))
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    for g in (a, b):
        g.n_menu = g.count(r"main menu: Co-op entry added")
        g.past_title()
    menu_then_coop(a, tag, a.n_menu)
    host_new_campaign(a, tag)
    menu_then_coop(b, tag, b.n_menu)
    wait_row(b, "Join TestA", f"{tag}: the host's lobby is listed")
    partner_joins(b, "Join TestA", tag)
    wait_row(a, "Start", f"{tag}: host lobby shows the partner")
    if not both_ready_and_start(a, b, tag) or not join_phase(a, b, tag):
        return a, b, relay, False
    return a, b, relay, True


LF_FIND = {
    "chest": ("SearchTools.FilterWithEC(s, Chest.GetECType())",
              "not Chest.IsOpen(o) and not Chest.IsLocked(o)", "tostring(Chest.IsOpen(LFO.chest))"),
    "door": ("SearchTools.FilterWithEC(s, Door.GetECType())",
             "not Door.IsLocked(o) and not Door.IsOpen(o)", "tostring(Door.IsOpen(LFO.door))"),
    "switch": ("local _ = 0", "Switch.IsAvailable(o)", "'n/a'"),
}


def lf_find(a, kind, radius):
    """The nearest usable <kind> to the host's hero within radius: (x, y, z, d) or None."""
    filt, ok, _ = LF_FIND[kind]
    out = lua_lines(a, (
        "local ok, e = pcall(function() LFO = LFO or {}; local h = GetPlayerHero(); local s = SearchTools.StartNewSearch('object'); "
        f"SearchTools.FilterWithinDistanceOfPos(s, h:GetPosition(), {radius}); {filt}; local best, bd = nil, 1e9; "
        f"for _, o in pairs(SearchTools.GetSearchResults(s)) do if {ok} then "
        "local d = (o:GetPosition() - h:GetPosition()):GetLength(); if d < bd then best, bd = o, d end end end; "
        f"if not best then print('LFFIND none') return end; LFO.{kind} = best; local p = best:GetPosition(); "
        "print(string.format('LFFIND %.3f %.3f %.3f %.1f', p:GetX(), p:GetY(), p:GetZ(), bd)) end); "
        "if not ok then print('LFFIND error ' .. tostring(e)) end"), "LFFIND", 20)
    note("INFO", f"use1: nearest {kind}: {out}")
    if not out or len(out[-1].split()) < 4:
        return None
    return tuple(float(v) for v in out[-1].split()[:4])


def lf_chests(a, radius, n=3):
    """Up to n closed, unlocked chests nearest the host's hero: [(x, y, z, d)]."""
    out = lua_lines(a, (
        "local ok, e = pcall(function() local h = GetPlayerHero(); local s = SearchTools.StartNewSearch('object'); "
        f"SearchTools.FilterWithinDistanceOfPos(s, h:GetPosition(), {radius}); SearchTools.FilterWithEC(s, Chest.GetECType()); "
        "local t = {} for _, o in pairs(SearchTools.GetSearchResults(s)) do if not Chest.IsOpen(o) and not Chest.IsLocked(o) then "
        "t[#t + 1] = {o, (o:GetPosition() - h:GetPosition()):GetLength()} end end; "
        "table.sort(t, function(x, y) return x[2] < y[2] end); print('LFCH n ' .. #t); "
        f"for i = 1, math.min(#t, {n}) do local p = t[i][1]:GetPosition(); "
        "print(string.format('LFCH %.3f %.3f %.3f %.1f', p:GetX(), p:GetY(), p:GetZ(), t[i][2])) end end); "
        "if not ok then print('LFCH error ' .. tostring(e)) end"), "LFCH", 20)
    note("INFO", f"use1: chests {out}")
    return [tuple(float(v) for v in l.split()[:4]) for l in out if len(l.split()) == 4 and l[0] != "n"]


def lf_state(a, kind, x, y, z):
    """open state of the <kind> nearest (x, y, z), read on the host."""
    fn = "Chest.IsOpen" if kind == "chest" else "Door.IsOpen"
    ec = "Chest.GetECType()" if kind == "chest" else "Door.GetECType()"
    out = lua_lines(a, (
        f"local p = CVector3({x:.3f}, {y:.3f}, {z:.3f}); local s = SearchTools.StartNewSearch('object'); "
        f"SearchTools.FilterWithinDistanceOfPos(s, p, 2.5); SearchTools.FilterWithEC(s, {ec}); local o = SearchTools.GetNearestEntity(s, p); "
        f"print('LFST ' .. tostring(o and {fn}(o)))"), "LFST", 10)
    return out[-1] if out else "?"


def lf_exec(a, b, tag, kind, x, y, z, label):
    before = lf_state(a, kind, x, y, z)
    purse0 = read_purses(a, b, f"{tag}_{label}_before")
    n = len(a.lines())
    a.send(f"cvar coop_world_use {kind}:{x:.3f},{y:.3f},{z:.3f}")
    time.sleep(8)
    after = lf_state(a, kind, x, y, z)
    res = [l[60:220] for l in a.lines()[n:] if "[lua] [coop] world use" in l]
    purse1 = read_purses(a, b, f"{tag}_{label}_after")
    a.shot(f"{tag}_{label}_host.png")
    b.shot(f"{tag}_{label}_partner.png")
    note("PASS" if after == "true" else "FAIL",
         f"{tag}: {label}: open {before} -> {after}; executor {res}; purses {purse0} -> {purse1}")
    a.send("cvar coop_world_use none")
    time.sleep(1)
    return after == "true"


def lf_p2_to(b, x, y, z, kind):
    """The partner stands 1.2 m from the object (its own Lua; its copy owns its position)."""
    ec = "Chest.GetECType()" if kind == "chest" else "Door.GetECType()"
    out = lua_lines(b, (
        f"local ok, e = pcall(function() local me = GetPlayerHenchman(); local p = CVector3({x:.3f}, {y:.3f}, {z:.3f}); "
        f"local st = CVector3({x + 1.2:.3f}, {y:.3f}, {z:.3f}); Physics.TeleportToPosition(me, st); "
        "local f = p - st; f:Normalise(); Physics.SetFacingVector(me, f); print('LFP2 ok') end); "
        "if not ok then print('LFP2 error ' .. tostring(e)) end"), "LFP2", 15)
    return bool(out) and out[-1].startswith("ok")


def lf_use1():
    """Path 2b, step 1: the host's executor (coop_world_use) opens chests and a
    door for player 2, triggered on the host itself: first with player 2 away,
    then with player 2 standing at the object."""
    tag = "lf_use1"
    a, b, relay, ok = lf_start_campaign(tag)
    try:
        if not ok:
            return
        time.sleep(10)
        chests = lf_chests(a, 150)
        for i, (x, y, z, d) in enumerate(chests[:2]):
            if not lf_exec(a, b, tag, "chest", x, y, z, f"chest{i}_away"):
                if lf_p2_to(b, x, y, z, "chest"):
                    time.sleep(3)
                    lf_exec(a, b, tag, "chest", x, y, z, f"chest{i}_p2_near")
        door = lf_find(a, "door", 150)
        if door:
            x, y, z, d = door
            if not lf_exec(a, b, tag, "door", x, y, z, "door_away"):
                if lf_p2_to(b, x, y, z, "door"):
                    time.sleep(3)
                    lf_exec(a, b, tag, "door", x, y, z, "door_p2_near")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def lf_hero_has_starter(a):
    """QC060's own test (IsHeroPacking): the host's hero holds the adult starter melee."""
    out = lua_lines(a, "print('LFPACK ' .. tostring(Inventory.GetNumberOfItemsOfType(GetPlayerHero(), "
                       "Gameflow.WeaponNames.AdultStarterMelee)) .. ' ' .. tostring(Inventory.GetNumberOfItems(GetPlayerHero())))",
                    "LFPACK", 10)
    return out[-1] if out else "?"


def lf_use23():
    """Path 2b, steps 2 and 3: the partner presses A at the caravan quest chest
    (QC060), another chest and a door; the host uses each for player 2. Checks:
    the hint shows, the object opens, the contents reach the host's hero (the
    stock co-op rule) and QC060's test passes, the purses agree; the partner's
    A on nothing still works (no record)."""
    tag = "lf_use23"
    a, b, relay, ok = lf_start_campaign(tag)
    try:
        if not ok:
            return
        time.sleep(20)  # the partner acts only 15 s after player 2 arrives (fable2_coop_worlduse.h)
        chests = lf_chests(a, 150)
        door = lf_find(a, "door", 150)
        targets = [("chest", c[:3], f"chest{i}") for i, c in enumerate(chests[:2])]
        if door:
            targets.append(("door", door[:3], "door"))
        for kind, (x, y, z), label in targets:
            if not lf_p2_to(b, x, y, z, kind):
                note("FAIL", f"{tag}: {label}: the partner could not get to it")
                continue
            time.sleep(3.5)
            b.shot(f"{tag}_{label}_hint.png")
            before = lf_state(a, kind, x, y, z)
            pack0 = lf_hero_has_starter(a)
            purse0 = read_purses(a, b, f"{tag}_{label}_before")
            p2inv0 = inv_dump(a, f"{tag}_{label}_p2_before")
            na, nb = len(a.lines()), len(b.lines())
            b.send("press A 200")
            # A chest's items show the host a "received" box that waits for the
            # host player's A (the game pauses meanwhile): dismiss them.
            t = time.time()
            while time.time() - t < 40:
                if any("world use done" in l for l in a.lines()[na:]) or (
                        kind != "chest" and time.time() - t > 6):
                    break
                time.sleep(3)
                if any("game paused" in l for l in a.lines()[na:]) and not any(
                        "world use done" in l for l in a.lines()[na:]):
                    a.send("press A 200")
            time.sleep(3)
            b.shot(f"{tag}_{label}_partner_after.png")
            a.shot(f"{tag}_{label}_host_after.png")
            after = lf_state(a, kind, x, y, z)
            pack1 = lf_hero_has_starter(a)
            purse1 = read_purses(a, b, f"{tag}_{label}_after")
            p2inv1 = inv_dump(a, f"{tag}_{label}_p2_after")
            sent = [l[24:200] for l in b.lines()[nb:] if "world use" in l]
            done = [l[24:200] for l in a.lines()[na:] if "world use" in l]
            note("PASS" if after == "true" else "FAIL",
                 f"{tag}: {label}: partner's A -> open {before} -> {after}; partner {sent}; host {done}")
            note("INFO", f"{tag}: {label}: host hero starter melee / items {pack0} -> {pack1}; purses {purse0} -> {purse1}; "
                         f"player 2 items {p2inv0[1] if p2inv0 else '?'} -> {p2inv1[1] if p2inv1 else '?'}")
            if kind == "chest" and label != "chest0" and p2inv0 and p2inv1:
                # An ordinary chest: its items to player 2's own inventory, and the
                # kit's mirror carries them to the partner's copy mid-session.
                time.sleep(4)
                mirror = [l[24:200] for l in b.lines()[nb:] if "[coopkit] mirror rev" in l]
                gave = [l[60:220] for l in a.lines()[na:] if "gave" in l]
                note("PASS" if p2inv1[1] > p2inv0[1] or "0 item(s)" in " ".join(gave) else "FAIL",
                     f"{tag}: {label}: an ordinary chest's items went to player 2 ({p2inv0[1]} -> {p2inv1[1]} items; {gave})")
                note("PASS" if any(re.search(r"\+[1-9]", m) for m in mirror) or "0 item(s)" in " ".join(gave) else "FAIL",
                     f"{tag}: {label}: the kit's mirror put them on the partner's copy ({mirror[-2:]})")
            if label == "chest0":
                note("PASS" if pack1.split()[0] not in ("0", "?") else "FAIL",
                     f"{tag}: the caravan chest's starter weapon reached the host's hero (QC060 IsHeroPacking): {pack0} -> {pack1}")
        # The partner's A inside its own pause wheel: its menu's, not the world's.
        if chests:
            x, y, z = chests[0][:3]
            lf_p2_to(b, x, y, z, "chest")
            time.sleep(2)
        nb = len(b.lines())
        b.send("press Start 200")
        time.sleep(2.5)
        b.send("press A 200")
        time.sleep(3)
        b.shot(f"{tag}_partner_A_in_wheel.png")
        sent = [l[24:200] for l in b.lines()[nb:] if "the partner's A" in l]
        note("PASS" if not sent else "FAIL", f"{tag}: the partner's A in its own pause wheel sends nothing ({sent})")
        for _ in range(5):
            b.send("press B 200")
            time.sleep(1.2)
        time.sleep(2)
        # The partner's A at nothing: no record, nothing on the host.
        lua_lines(b, ("local me = GetPlayerHenchman(); local p = me:GetPosition(); "
                      "Physics.TeleportToPosition(me, CVector3(p:GetX() + 12, p:GetY() + 12, p:GetZ())); print('LFAWAY ok')"),
                  "LFAWAY", 10)
        time.sleep(2)
        na, nb = len(a.lines()), len(b.lines())
        b.send("press A 200")
        time.sleep(4)
        acted = [l[24:200] for l in a.lines()[na:] if "world use" in l and ("opening it" in l or "opened" in l)]
        said = [l[24:200] for l in a.lines()[na:] if "nothing to use" in l]
        note("PASS" if not acted else "FAIL",
             f"{tag}: the partner's A away from objects uses nothing on the host ({said or acted})")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()



def lf_xp_general(g, who):
    out = lua_lines(g, ("local ok, e = pcall(function() return math.floor(Experience.Get(" + who +
                        ", EExperienceType.EXPERIENCE_GENERAL)) end); print('LFXP ' .. tostring(ok and e or ('error ' .. tostring(e))))"),
                    "LFXP", 10)
    return out[-1] if out else "?"


def lf_host_pos(a):
    out = lua_lines(a, ("local p = GetPlayerHero():GetPosition(); "
                        "print(string.format('LFHP %.3f %.3f %.3f', p:GetX(), p:GetY(), p:GetZ()))"), "LFHP", 10)
    if not out or len(out[-1].split()) != 3:
        return None
    return tuple(float(v) for v in out[-1].split())


def lf_next():
    """001-next batch in Josh's Oakfield copy: the host's hero as the partner
    sees it (T-pose, idle and walking); the experience mirror; the 'A: Open'
    hint and the partner's A at a door/chest; a lever used for player 2 on the
    host; then the host's Save and Quit: the partner back at its front end (B
    watchdog; A: its chunks held during the teardown)."""
    tag = "lf_next"
    setup_tomls()
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        if not oak_join(a, b, tag):
            return
        time.sleep(20)  # past the partner's 15 s settle
        # --- T-pose: the host's hero on the partner's screen, idle then walking.
        hp = lf_host_pos(a)
        note("INFO", f"{tag}: host hero at {hp}")
        if hp:
            lua_lines(b, (f"local me = GetPlayerHenchman(); local h = CVector3({hp[0]:.3f}, {hp[1]:.3f}, {hp[2]:.3f}); "
                          f"local st = CVector3({hp[0] + 3.5:.3f}, {hp[1]:.3f}, {hp[2]:.3f}); Physics.TeleportToPosition(me, st); "
                          "local f = h - st; f:Normalise(); Physics.SetFacingVector(me, f); print('LFTP ok')"), "LFTP", 10)
            time.sleep(4)
            b.shot(f"{tag}_tpose_partner_idle.png")
            a.shot(f"{tag}_tpose_host_idle.png")
            a.send("stick L 0 1 2500")
            time.sleep(1.2)
            b.shot(f"{tag}_tpose_partner_walking.png")
            a.shot(f"{tag}_tpose_host_walking.png")
            time.sleep(3)
            b.shot(f"{tag}_tpose_partner_after_walk.png")
            note("INFO", f"{tag}: T-pose screenshots taken (idle, walking, after); host hero now at {lf_host_pos(a)}")
        # --- XP: the host gives player 2 experience; the partner's record follows.
        xp_b0, xp_h0 = lf_xp_general(b, "GetPlayerHero()"), lf_xp_general(a, "GetPlayerHenchman()")
        na, nb = len(a.lines()), len(b.lines())
        lua_lines(a, ("local ok, e = pcall(function() Experience.Modify(GetPlayerHenchman(), EExperienceType.EXPERIENCE_GENERAL, 250) end); "
                      "print('LFXPADD ' .. tostring(ok or e))"), "LFXPADD", 10)
        time.sleep(15)
        xp_b1, xp_h1 = lf_xp_general(b, "GetPlayerHero()"), lf_xp_general(a, "GetPlayerHenchman()")
        xa = [l[24:200] for l in a.lines()[na:] if "COOPXP" in l][:3]
        xb = [l[24:200] for l in b.lines()[nb:] if "xp" in l.lower() and "coop" in l.lower()][:3]
        try:
            ok = int(xp_b1) - int(xp_b0) == 250
        except ValueError:
            ok = False
        note("PASS" if ok else "FAIL", f"{tag}: XP: host's player 2 {xp_h0} -> {xp_h1}; partner's hero {xp_b0} -> {xp_b1}; host {xa}; partner {xb}")
        b.shot(f"{tag}_xp_partner.png")
        # (No partner-side probe of GetPlayerHenchman's experience: reading it
        # there crashed the joiner, guest read of 0x5, rig 2026-10-27 run 3.)
        hx = lua_lines(a, ("local t = EExperienceType.EXPERIENCE_GENERAL; print('LFXPH hero ' .. math.floor(Experience.Get(GetPlayerHero(), t)) .. "
                           "' hench ' .. math.floor(Experience.Get(GetPlayerHenchman(), t)))"), "LFXPH", 10)
        note("INFO", f"{tag}: XP on the host {hx}")
        # The partner's Abilities page (the wheel opens on it): which XP it shows.
        b.send("press Start 200")
        time.sleep(2.5)
        b.send("press A 200")
        time.sleep(3)
        b.shot(f"{tag}_xp_partner_abilities.png")
        b.send("press B 200")
        time.sleep(1.5)
        b.send("press B 200")
        time.sleep(2)
        # --- hint + A: a door and a chest near the host.
        targets = []
        door = lf_find(a, "door", 80)
        if door:
            targets.append(("door", door[:3]))
        ch = lf_chests(a, 80, 1)
        if ch:
            targets.append(("chest", ch[0][:3]))
        for kind, (x, y, z) in targets:
            na, nb = len(a.lines()), len(b.lines())
            if not lf_p2_to(b, x, y, z, kind):
                note("FAIL", f"{tag}: {kind}: the partner could not get to it")
                continue
            time.sleep(7)
            b.shot(f"{tag}_{kind}_hint.png")
            hints = [l[24:200] for l in a.lines()[na:] if "COOPWHINT" in l]
            note("PASS" if any(f"COOPWHINT:{kind}" in h for h in hints) else "FAIL",
                 f"{tag}: {kind}: the host's hint for player 2 {hints[-3:]} (partner screenshot {tag}_{kind}_hint.png)")
            before = lf_state(a, kind, x, y, z)
            na = len(a.lines())
            b.send("press A 200")
            t = time.time()
            while time.time() - t < 25:
                if kind != "chest" or any("world use done" in l for l in a.lines()[na:]):
                    break
                time.sleep(1)
            time.sleep(3)
            if kind == "chest" and any("game paused" in l for l in a.lines()[na:]):
                a.send("press A 200")
            after = lf_state(a, kind, x, y, z)
            done = [l[24:200] for l in a.lines()[na:] if "world use" in l][:4]
            b.shot(f"{tag}_{kind}_after_a.png")
            note("PASS" if after == "true" else "FAIL", f"{tag}: {kind}: partner's A -> open {before} -> {after}; host {done}")
            time.sleep(2)
            b.shot(f"{tag}_{kind}_hint_after_open.png")
        # The hint goes away from everything.
        lua_lines(b, ("local me = GetPlayerHenchman(); local p = me:GetPosition(); "
                      "Physics.TeleportToPosition(me, CVector3(p:GetX() + 12, p:GetY() + 12, p:GetZ())); print('LFAWAY ok')"), "LFAWAY", 10)
        time.sleep(4)
        last = [l[24:200] for l in a.lines() if "COOPWHINT" in l][-1:]
        b.shot(f"{tag}_hint_away.png")
        note("PASS" if last and "COOPWHINT:none" in last[0] else "FAIL", f"{tag}: the hint clears away from objects ({last})")
        # --- lever: used for player 2 on the host (not reachable from the A).
        sw = lf_find(a, "switch", 150)
        if sw:
            na = len(a.lines())
            a.send(f"cvar coop_world_use switch:{sw[0]:.3f},{sw[1]:.3f},{sw[2]:.3f}")
            time.sleep(6)
            res = [l[24:220] for l in a.lines()[na:] if "world use" in l][:4]
            a.shot(f"{tag}_lever_host.png")
            note("PASS" if any("lever used by player 2" in r for r in res) else "FAIL", f"{tag}: lever at {sw}: {res}")
            a.send("cvar coop_world_use none")
        else:
            note("INFO", f"{tag}: no lever (Switch) within 150 m of the host in Oakfield")
        # --- the host's Save and Quit: B + A on the partner.
        fe = r"\[state\].*-> (MainMenu|MainMenuMovie|PressAScreen)"
        na, nb = len(a.lines()), len(b.lines())
        a.send("press Start 200")
        time.sleep(2.5)
        a.send("press Up 150")
        time.sleep(1)
        a.send("press A 200")
        time.sleep(2.5)
        a.send("press Down 150")
        time.sleep(1)
        a.shot(f"{tag}_host_on_save_and_quit.png")
        a.send("press A 200")
        time.sleep(3)
        a.send("press A 200")
        t = time.time()
        while time.time() - t < 150:
            if any("chunk hold ended" in l for l in b.lines()[nb:]) and any(re.search(fe, l) for l in a.lines()[na:]):
                break
            time.sleep(2)
        time.sleep(6)
        b.shot(f"{tag}_partner_after_host_quit.png")
        a.shot(f"{tag}_host_after_quit.png")
        keys = ("XSession", "co-op session is ending", "chunk hold", "front end", "[state]", "restart", "Unhandled")
        pl = [l[24:200] for l in b.lines()[nb:] if any(k in l for k in keys)]
        note("INFO", f"{tag}: partner after the host's Save and Quit {pl[:16]}")
        note("INFO", f"{tag}: A: XSessionEnd on the partner {any('XSessionEnd' in l for l in pl)}; "
                     f"hold {[l for l in pl if 'session is ending' in l or 'chunk hold' in l][:3]}")
        note("PASS" if any(re.search(fe, l) for l in b.lines()[nb:]) and b.proc.is_running() else "FAIL",
             f"{tag}: the host's Save and Quit returns the partner to its front end")
        note("PASS" if not any("telling the player to restart" in l for l in b.lines()[nb:]) else "FAIL",
             f"{tag}: B watchdog: no black screen (no restart card)")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()



def lf_jn():
    """001jn proof (coordinator, 2026-10-28): the partner's stock join screen is
    answered by the partner itself while it is open (no manual A anywhere): a
    loopback Co-op continue join (Josh's Oakfield copy, mod-a host, mod-b partner)
    with the auto-accept and spawn lines and screenshots; then the host's own solo
    Continue still loads the world."""
    tag = "lf_jn"
    setup_tomls()
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        ok = oak_join(a, b, tag)
        bl = b.lines()
        opened = [l[24:200] for l in bl if "the game's join screen is open" in l]
        auto = [l[24:200] for l in bl if "auto-accepted stock join screen" in l]
        timed = [l[24:200] for l in bl if "partner: A for the join screen" in l]
        dropped = [l[24:200] for l in bl if "join screen is still open after 10 s" in l]
        spawn = [l[24:200] for l in a.lines() if "henchman spawned" in l or "player 2 body" in l]
        note("PASS" if ok and opened and auto and not timed else "FAIL",
             f"{tag}: partner answered its own join screen: open {opened[:1]}; auto-accept {auto[:4]}; "
             f"timed presses {timed[:2]} (expected none)")
        note("INFO", f"{tag}: spawn on the host {spawn[-2:]}; cover dropped by the 10 s timeout: {dropped[:1] or 'no'}")
        b.shot(f"{tag}_partner_in_world.png")
        a.shot(f"{tag}_host_in_world.png")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()
    # A new co-op campaign (the skip-childhood path), again with no manual A: the
    # partner's timed presses are gone, so this proves nothing else relied on them.
    a, b, relay, ok = lf_start_campaign(tag + "_new")
    try:
        bl = b.lines()
        opened = [l[24:200] for l in bl if "the game's join screen is open" in l]
        auto = [l[24:200] for l in bl if "auto-accepted stock join screen" in l]
        timed = [l[24:200] for l in bl if "partner: A for the join screen" in l]
        dropped = [l[24:200] for l in bl if "join screen is still open after 10 s" in l]
        note("PASS" if ok and opened and auto and not timed else "FAIL",
             f"{tag}_new: new campaign: partner answered its own join screen: open {opened[:1]}; "
             f"auto-accept {auto[:4]}; timed presses {timed[:2]} (expected none); cover timeout {dropped[:1] or 'no'}")
        time.sleep(4)
        b.shot(f"{tag}_new_partner_in_world.png")
        a.shot(f"{tag}_new_host_in_world.png")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()
    lf_jn_solo(tag)


def lf_jn_solo(tag="lf_jn"):
    """The host's solo Continue (the stock main menu's second row: New Game,
    Continue, Co-op) still loads a world."""
    setup_tomls()
    a = Game("mod-a", "host")
    a.start({"FABLE2_TEST_INPUT": "1"})
    try:
        n_menu = a.count(r"main menu: Co-op entry added")
        a.past_title()
        a.wait(f"{tag}: solo: main menu built", r"main menu: Co-op entry added", n_menu, 30)
        time.sleep(2)
        a.shot(f"{tag}_solo_menu.png")
        n = a.count(r"front-end scans paused \(the game's Lua is running\)")
        a.press("Down")  # New Game -> Continue
        time.sleep(1.0)
        a.shot(f"{tag}_solo_on_continue.png")
        a.press("E")  # Continue: the stock save picker when there are several saves
        time.sleep(12)  # the rig's 83 saves: the picker loads every card first
        a.shot(f"{tag}_solo_picker.png")
        a.press("E")  # the first card (the newest save)
        time.sleep(4)
        if not a.count(r"front-end scans paused \(the game's Lua is running\)") > n:
            a.press("E")  # a card's own confirm, if it asks
        ok = a.wait(f"{tag}: solo Continue loads the world (the game's Lua runs)",
                    r"front-end scans paused \(the game's Lua is running\)", n, 120)
        time.sleep(20)
        a.shot(f"{tag}_solo_world.png")
        crashes = a.count(r"Unhandled guest")
        note("PASS" if ok and crashes == 0 and a.proc.is_running() else "FAIL",
             f"{tag}: solo Continue: world loaded, {crashes} crash(es)")
    finally:
        a.stop()



EQ_SLOT_PY = r"C:\Users\josh_\AppData\Local\Temp\claude\D--VoiceBank\2c833dc8-b1f5-4a88-af94-d98cc46c47dc\scratchpad\oakfield_slot.py"


def eq_fresh_slot():
    """A fresh rig co-op copy of Josh's Oakfield save in mod-a (no coop_player2.txt,
    the newest save): (slot path, campaign id)."""
    out = subprocess.check_output([sys.executable, EQ_SLOT_PY], text=True)
    m = re.search(r"slot (\S+) header .* campaign (\w+)", out)
    note("INFO", f"eq: fresh slot {out.strip()}")
    return (m.group(1), m.group(2)) if m else (None, None)


def eq_host_lobby(a, tag, cid):
    if not host_continue_lobby(a, tag):
        return False
    hv = [l for l in a.lines() if "[lobby] open host kind=continue" in l][-1:]
    ok = bool(hv) and cid in hv[0]
    note("PASS" if ok else "FAIL", f"{tag}: the fresh Oakfield copy's lobby ({hv[0][60:] if hv else ''})")
    return ok


def lf_eqprobe():
    """001eq a2 probe: which controller field holds the partner's look-screen gender.
    The partner joins a fresh Oakfield copy with coop_join_screen_probe on and its
    own answers off; on its look screen the harness presses Right once (the gender
    row), then A until player 2 spawns. The probe lines go to the report."""
    tag = "lf_eqprobe"
    slot, cid = eq_fresh_slot()
    if not cid:
        note("FAIL", f"{tag}: no fresh slot")
        return
    setup_tomls()
    with open(os.path.join(ROOT, "mod-b", "fable_2.toml"), "a", encoding="utf-8", newline="\n") as f:
        f.write("coop_join_screen_probe = true\ncoop_join_screens_auto = false\n")
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        if not eq_host_lobby(a, tag, cid):
            return
        n = b.count(r"main menu: Co-op entry added")
        b.past_title()
        menu_then_coop(b, tag, n)
        wait_row(b, "Continue TestA", f"{tag}: the campaign is listed for the partner")
        partner_joins(b, "Continue TestA", tag)
        wait_row(a, "Start", f"{tag}: host lobby shows the partner")
        if not both_ready_and_start(a, b, tag):
            return
        nb = len(b.lines())
        t = time.time()
        while time.time() - t < 180 and not any("[joinscreen] open" in l for l in b.lines()[nb:]):
            time.sleep(1)
        opened = any("[joinscreen] open" in l for l in b.lines()[nb:])
        note("PASS" if opened else "FAIL", f"{tag}: the partner's join screen opened ({time.time() - t:.0f} s)")
        if not opened:
            return
        time.sleep(2.5)
        b.shot(f"{tag}_look_before.png")
        n1 = len(b.lines())
        b.send("press Right 200")
        time.sleep(2.5)
        b.shot(f"{tag}_look_after_right.png")
        right = [l[24:330] for l in b.lines()[n1:] if "[joinscreen]" in l]
        note("INFO", f"{tag}: probe lines after Right: {right[:8]}")
        n2 = len(b.lines())
        spawn0 = a.count(r"henchman spawned")
        for k in range(12):
            if a.count(r"henchman spawned") > spawn0:
                break
            b.send("press A 200")
            time.sleep(2.0)
            b.shot(f"{tag}_a{k:02d}.png")
        a.wait(f"{tag}: player 2 spawned", r"henchman spawned", spawn0, 60)
        note("INFO", f"{tag}: probe lines over the A presses: {[l[24:330] for l in b.lines()[n2:] if '[joinscreen]' in l][:20]}")
        note("INFO", f"{tag}: body {[l[24:200] for l in a.lines() if 'player 2 body' in l][-1:]}")
        time.sleep(5)
        b.shot(f"{tag}_partner_world.png")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()



def eq_page(b, downs, shot):
    """The partner's pause wheel -> the page `downs` below Abilities; a screenshot; back out."""
    b.send("press Start 200")
    time.sleep(2.5)
    for _ in range(downs):
        b.send("press Down 150")
        time.sleep(0.8)
    b.send("press A 200")
    time.sleep(2.5)
    b.shot(shot)
    b.send("press B 200")
    time.sleep(1.5)
    b.send("press B 200")
    time.sleep(2)


def lf_eq():
    """001eq proof (coordinator, 2026-10-28), one loopback join from a fresh
    Oakfield copy with no coop_player2.txt and no manual A:
      a2   the partner's join screen preset follows the lobby (male: index 4)
      a1   player 2 seeded with the template starter kit (no host clothes/weapons)
      look close-ups of player 2 on both screens
      b    partner equip / wear / unwear (kit commands): host result, the copy's
           mirror lines, the toast, a page screenshot after each
      R3   partner walks (coop_cam_trace on, NPC burst), stops, R3: camera behind
      chest a single A from a normal standing spot ('Opening the chest...')
      ids  BDFE364C's name; the POOR_MALE records' names and female counterparts"""
    tag = "lf_eq"
    slot, cid = eq_fresh_slot()
    if not cid:
        note("FAIL", f"{tag}: no fresh slot")
        return
    setup_tomls()
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        if not eq_host_lobby(a, tag, cid) or not partner_continue_join(a, b, tag):
            return
        bl, al = b.lines(), a.lines()
        preset = [l[24:200] for l in bl if "join screen preset set" in l]
        auto = [l[24:200] for l in bl if "auto-accepted stock join screen" in l]
        body = [l[24:200] for l in al if "player 2 body" in l][-1:]
        note("PASS" if preset and body and "selector index 4" in body[0] else "FAIL",
             f"{tag}: a2: partner preset {preset[:1]}; auto-accept {auto[:2]}; host {body}")
        seed = [l[24:220] for l in al if "player 2 seeded" in l or "no coop_player2.txt" in l]
        note("INFO", f"{tag}: a1: {seed[:3]}")
        time.sleep(12)
        kit = [l for l in a.lines() if "[lua] COOPKIT:v1" in l][-1:]
        worn = re.search(r"\|worn=([^|]*)", kit[0]).group(1) if kit else "?"
        melee = re.search(r"\|melee=([^|]*)", kit[0]).group(1) if kit else "?"
        want = {"BD511D47", "9D696C51", "CBD8841A", "DC6052C6", "331B2070"}
        note("PASS" if set(worn.split(",")) == want else "FAIL",
             f"{tag}: a1: player 2 wears the template starter kit: worn={worn} melee={melee}")
        # --- close-ups for Josh's report: player 2 4 m in front of the HOST's camera,
        # turned front / side / back to it; the partner's own camera orbited with the
        # right stick for back / side / front of its hero.
        cam = lua_lines(a, ("local c = ScriptFunction.GetCameraPosition(); local h = GetPlayerHero():GetPosition(); "
                            "print(string.format('LFEQCAM %.3f %.3f %.3f %.3f %.3f %.3f', c:GetX(), c:GetY(), c:GetZ(), "
                            "h:GetX(), h:GetY(), h:GetZ()))"), "LFEQCAM", 10)
        if cam and len(cam[-1].split()) == 6:
            cx, cy, cz, hx, hy, hz = (float(v) for v in cam[-1].split())
            dx, dy = hx - cx, hy - cy
            n = math.hypot(dx, dy) or 1
            dx, dy = dx / n, dy / n
            px, py = cx + dx * 4.5, cy + dy * 4.5  # in view, past the host's hero line
            for label, fx, fy in (("front", -dx, -dy), ("side", -dy, dx), ("back", dx, dy)):
                lua_lines(b, (f"local me = GetPlayerHenchman(); Physics.TeleportToPosition(me, CVector3({px:.3f}, {py:.3f}, {hz:.3f})); "
                              f"Physics.SetFacingVector(me, CVector3({fx:.3f}, {fy:.3f}, 0)); print('LFEQPOSE ok')"), "LFEQPOSE", 10)
                time.sleep(3)
                a.shot(f"{tag}_look_host_{label}.png")
            b.shot(f"{tag}_look_partner_back.png")
            b.send("stick R 1 0 700")
            time.sleep(1.5)
            b.shot(f"{tag}_look_partner_side.png")
            b.send("stick R 1 0 700")
            time.sleep(1.5)
            b.shot(f"{tag}_look_partner_front.png")
            b.send("press RS 200")
            time.sleep(1)
        mirror = [l[24:200] for l in b.lines() if "wore" in l and "on the copy" in l]
        note("INFO", f"{tag}: b1: the copy's worn mirror {mirror[:3]}")
        # --- ids: BDFE364C, the POOR_MALE names and female counterparts (host Lua)
        ids = lua_lines(a, (
            "local function nm(h) local ok, r = pcall(function() return GDB.GetRecord('#' .. h) end); if not ok or not r then return 'nil' end; "
            "for _, f in ipairs({'GetName', 'GetRecordName', 'GetTypeName'}) do local o, v = pcall(function() return r[f](r) end); "
            "if o and v then return f .. '=' .. tostring(v) end end; return tostring(r) end; "
            "for _, h in ipairs({'BDFE364C', '12613383', '98F97077', '3A153787', 'BD511D47', '9D696C51', 'CBD8841A', 'DC6052C6', '331B2070'}) do "
            "print('LFEQID ' .. h .. ' ' .. nm(h)) end"), "LFEQID", 15)
        note("INFO", f"{tag}: record names {ids}")
        fem = []
        for line in ids:
            m = re.search(r"=(\S+)$", line)
            if not m or not m.group(1).endswith(("M", "Male")):
                continue
            base_name = m.group(1)
            cands = [base_name[:-1] + "F"] if base_name.endswith("M") else []
            cands += [base_name.replace("Male", "Female")]
            for cand in cands:
                out = lua_lines(a, (f"local ok, r = pcall(function() return GDB.GetRecord('{cand}') end); "
                                    f"local id = ok and r and select(2, pcall(function() return r:GetID() end)); "
                                    f"print('LFEQFEM {cand} ' .. tostring(id))"), "LFEQFEM", 10)
                fem += out
        note("INFO", f"{tag}: female counterparts {fem}")
        # --- b: equip, wear, unwear through the kit (the pages' command path)
        lua_lines(a, ("local h = GetPlayerHenchman(); for _, x in ipairs({'3966741D', '5A04B90B'}) do "
                      "pcall(Inventory.AddItemOfType, h, GDB.GetRecord('#' .. x):GetID()) end; print('LFEQGIVE ok')"), "LFEQGIVE", 10)
        time.sleep(6)
        for op, rec, page, downs in (("equip", "3966741D", "weapons", 2), ("wear", "5A04B90B", "clothing", 3),
                                     ("unwear", "5A04B90B", "clothing", 3)):
            na, nb = len(a.lines()), len(b.lines())
            b.send(f"cvar coop_kit_send_line {op} {rec}")
            time.sleep(1.2)
            b.shot(f"{tag}_{op}_toast.png")
            time.sleep(5)
            host = [l[24:200] for l in a.lines()[na:] if "[coopkit] host: command" in l]
            part = [l[24:200] for l in b.lines()[nb:] if "[coopkit]" in l and ("command" in l or "mirror" in l)]
            note("PASS" if any("-> ok" in h for h in host) else "FAIL", f"{tag}: b: {op} {rec}: host {host[:2]}; partner {part[:4]}")
            b.shot(f"{tag}_{op}_world.png")
            eq_page(b, downs, f"{tag}_{op}_{page}_page.png")
        # --- R3: walk (cam trace + NPC burst), stop, R3
        b.send("cvar coop_cam_trace true")
        b.send("stick L 0 1 2500")
        for k in range(5):
            time.sleep(0.5)
            b.shot(f"{tag}_walk_{k}.png")
        time.sleep(2)
        nb = len(b.lines())
        b.send("press RS 200")
        time.sleep(2)
        b.shot(f"{tag}_r3.png")
        r3 = [l[24:200] for l in b.lines()[nb:] if "R3: camera recentered" in l]
        note("PASS" if r3 and "last walked" in r3[0] or (r3 and "walks" in r3[0]) else "FAIL", f"{tag}: R3 {r3[:1]} (screenshot {tag}_r3.png)")
        # --- chest: a single A from a normal standing spot
        ch = lf_chests(a, 120, 1)
        if ch:
            x, y, z = ch[0][:3]
            if lf_p2_to(b, x, y, z, "chest"):
                time.sleep(7)
                b.shot(f"{tag}_chest_hint.png")
                na = len(a.lines())
                b.send("press A 200")
                time.sleep(1.2)
                b.shot(f"{tag}_chest_opening.png")
                time.sleep(10)
                if any("game paused" in l for l in a.lines()[na:]):
                    a.send("press A 200")
                st = lf_state(a, "chest", x, y, z)
                uses = [l[24:200] for l in a.lines()[na:] if "world use" in l]
                note("PASS" if st == "true" else "FAIL", f"{tag}: chest: one A -> open {st}; host {uses[:3]}")
        else:
            note("INFO", f"{tag}: no closed chest within 120 m")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()



def eq_closeups(a, b, tag):
    """Player 2 four metres in front of the host's camera, turned front / side / back
    to it (host shots), and the partner's camera orbited around its own hero."""
    cam = lua_lines(a, ("local c = ScriptFunction.GetCameraPosition(); local h = GetPlayerHero():GetPosition(); "
                        "print(string.format('LFEQCAM %.3f %.3f %.3f %.3f %.3f %.3f', c:GetX(), c:GetY(), c:GetZ(), "
                        "h:GetX(), h:GetY(), h:GetZ()))"), "LFEQCAM", 10)
    if not cam or len(cam[-1].split()) != 6:
        note("FAIL", f"{tag}: close-ups: no host camera position")
        return
    cx, cy, cz, hx, hy, hz = (float(v) for v in cam[-1].split())
    dx, dy = hx - cx, hy - cy
    n = math.hypot(dx, dy) or 1
    dx, dy = dx / n, dy / n
    px, py = cx + dx * 4.0, cy + dy * 4.0
    for label, fx, fy in (("front", -dx, -dy), ("side", -dy, dx), ("back", dx, dy)):
        lua_lines(b, (f"local me = GetPlayerHenchman(); Physics.TeleportToPosition(me, CVector3({px:.3f}, {py:.3f}, {hz:.3f})); "
                      f"Physics.SetFacingVector(me, CVector3({fx:.3f}, {fy:.3f}, 0)); print('LFEQPOSE ok')"), "LFEQPOSE", 10)
        time.sleep(3)
        a.shot(f"{tag}_look_host_{label}.png")
    for k, label in enumerate(("back", "side", "front", "side2")):
        b.shot(f"{tag}_look_partner_{label}.png")
        b.send("stick R 1 0 450")
        time.sleep(1.5)
    b.send("press RS 200")
    time.sleep(1)


def lf_eqplay():
    """001eq gameplay proof (Josh, 2026-10-28: load-in, equip and unequip, spells):
      - a fresh Oakfield copy joined with no manual A (a1/a2), close-ups through the
        game window capture (the Windows Security prompt is not in it);
      - Weapons page: the partner equips a second sword with the page's own A
        (the command to the host and the page's own equip on the copy), page and
        world shots on both PCs; A again on the equipped weapon (unequip?);
      - Clothing: wear and take off a hat (host + the copy's mirror, now both ways);
      - Will: the partner holds B (both screens mid-cast), the Abilities page as the
        partner sees it, and buying an upgrade there (host and partner logs)."""
    tag = "lf_eqplay"
    slot, cid = eq_fresh_slot()
    if not cid:
        note("FAIL", f"{tag}: no fresh slot")
        return
    setup_tomls()
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        if not eq_host_lobby(a, tag, cid) or not partner_continue_join(a, b, tag):
            return
        body = [l[24:200] for l in a.lines() if "player 2 body" in l][-1:]
        note("PASS" if body and "selector index 4" in body[0] else "FAIL", f"{tag}: load-in: {body}")
        a.window_capture = b.window_capture = True
        time.sleep(12)
        eq_closeups(a, b, tag)
        # --- give player 2 a second sword, a pistol and a hat on the host
        lua_lines(a, ("local h = GetPlayerHenchman(); for _, x in ipairs({'3966741D', '3A153787', '5A04B90B'}) do "
                      "pcall(Inventory.AddItemOfType, h, GDB.GetRecord('#' .. x):GetID()) end; print('LFEQGIVE ok')"), "LFEQGIVE", 10)
        time.sleep(8)
        # --- Weapons page: open, list, A on the second entry, A on the equipped one
        na, nb = len(a.lines()), len(b.lines())
        b.send("press Start 200")
        time.sleep(2.5)
        for _ in range(2):
            b.send("press Down 150")
            time.sleep(0.8)
        b.send("press A 200")
        time.sleep(3)
        b.shot(f"{tag}_weapons_page_open.png")
        b.send("press Down 150")
        time.sleep(1)
        b.shot(f"{tag}_weapons_page_on_second.png")
        b.send("press A 200")
        time.sleep(3)
        b.shot(f"{tag}_weapons_page_after_equip.png")
        b.send("press A 200")  # A again on the same (now equipped) weapon: unequip?
        time.sleep(3)
        b.shot(f"{tag}_weapons_page_after_second_a.png")
        b.send("press B 200")
        time.sleep(1.5)
        b.send("press B 200")
        time.sleep(5)
        host = [l[24:200] for l in a.lines()[na:] if "[coopkit] host: command" in l]
        part = [l[24:200] for l in b.lines()[nb:] if "[coopkit]" in l and ("command" in l or "copy" in l)]
        crash = b.count(r"Unhandled guest")
        note("PASS" if any("-> ok" in h for h in host) and crash == 0 else "FAIL",
             f"{tag}: Weapons page equip: host {host[:4]}; partner {part[:6]}; partner crashes {crash}")
        a.shot(f"{tag}_after_equip_host.png")
        b.shot(f"{tag}_after_equip_partner.png")
        kit = [l for l in a.lines() if "[lua] COOPKIT:v1" in l][-1:]
        note("INFO", f"{tag}: host kit after the equip: {re.search(r'[|]melee=([^|]*)[|]ranged=([^|]*)', kit[0]).groups() if kit else '?'}")
        # --- Clothing: wear and take off the hat (kit command; the copy's mirror both ways)
        for op in ("wear", "unwear"):
            nb = len(b.lines())
            b.send(f"cvar coop_kit_send_line {op} 5A04B90B")
            time.sleep(7)
            mir = [l[24:200] for l in b.lines()[nb:] if "on the copy" in l or "command" in l]
            note("INFO", f"{tag}: {op} hat: partner {mir[:4]}")
            b.shot(f"{tag}_{op}_hat_partner.png")
            a.shot(f"{tag}_{op}_hat_host.png")
        # --- Will: what player 2 has, then hold B (the spell button)
        st = lua_lines(a, ("local h = GetPlayerHenchman(); local t = EExperienceType; "
                           "local function g(x) local o, v = pcall(Experience.Get, h, x); return o and math.floor(v) or -1 end; "
                           "local ab = {} for n, v in pairs(EHeroAbilityType) do if type(v) == 'number' then "
                           "local o, l = pcall(Stats.GetHeroAbilityLevel, h, v); if o and l and l > 0 and l <= 10 then ab[#ab + 1] = n .. '=' .. l end end end; "
                           "print('LFEQWILL xp g/s/k/w ' .. g(t.EXPERIENCE_GENERAL) .. '/' .. g(t.EXPERIENCE_STRENGTH) .. '/' .. g(t.EXPERIENCE_SKILL) .. '/' .. g(t.EXPERIENCE_WILL) .. ' abilities ' .. table.concat(ab, ','))"),
                     "LFEQWILL", 10)
        note("INFO", f"{tag}: Will: player 2 on the host {st}")
        nb, na = len(b.lines()), len(a.lines())
        b.send("press B 3000")
        time.sleep(1.5)
        b.shot(f"{tag}_will_cast_partner.png")
        a.shot(f"{tag}_will_cast_host.png")
        time.sleep(3)
        note("INFO", f"{tag}: Will: partner lines {[l[24:200] for l in b.lines()[nb:] if 'coop' in l.lower() or 'spell' in l.lower()][:6]}; "
                     f"host lines {[l[24:200] for l in a.lines()[na:] if 'spell' in l.lower() or 'will' in l.lower()][:6]}")
        # --- Abilities page as the partner sees it, and buying an upgrade
        na, nb = len(a.lines()), len(b.lines())
        b.send("press Start 200")
        time.sleep(2.5)
        b.send("press A 200")  # the wheel opens on Abilities
        time.sleep(3)
        b.shot(f"{tag}_abilities_page.png")
        b.send("press A 200")  # Strength (top row)
        time.sleep(2.5)
        b.shot(f"{tag}_abilities_strength.png")
        b.send("press A 200")  # buy the highlighted upgrade
        time.sleep(3)
        b.shot(f"{tag}_abilities_after_buy.png")
        for _ in range(3):
            b.send("press B 200")
            time.sleep(1.5)
        time.sleep(4)
        st2 = lua_lines(a, ("local h = GetPlayerHenchman(); local ab = {} for n, v in pairs(EHeroAbilityType) do if type(v) == 'number' then "
                            "local o, l = pcall(Stats.GetHeroAbilityLevel, h, v); if o and l and l > 0 and l <= 10 then ab[#ab + 1] = n .. '=' .. l end end end; "
                            "local o, x = pcall(Experience.Get, h, EExperienceType.EXPERIENCE_STRENGTH); "
                            "print('LFEQBUY str xp ' .. tostring(o and math.floor(x)) .. ' abilities ' .. table.concat(ab, ','))"), "LFEQBUY", 10)
        note("INFO", f"{tag}: upgrade: host after {st2}; partner lines "
                     f"{[l[24:200] for l in b.lines()[nb:] if 'coopkit' in l or 'abilit' in l.lower()][:6]}")
        note("INFO", f"{tag}: partner crashes {b.count(r'Unhandled guest')}, host crashes {a.count(r'Unhandled guest')}")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()



EQ_ARENA = (208.0, 72.0, 39.4)  # Oakfield forest by the caravan chest: no villagers


def eq_xp(a, who):
    """Experience (general, strength, skill, will) of the host's hero or player 2, read on the host."""
    ent = "GetPlayerHero()" if who == "hero" else "GetPlayerHenchman()"
    out = lua_lines(a, (f"local e = {ent}; local t = EExperienceType; local function g(x) local o, v = pcall(Experience.Get, e, x); "
                        "return o and math.floor(v) or -1 end; print('LFXP4 ' .. g(t.EXPERIENCE_GENERAL) .. ' ' .. g(t.EXPERIENCE_STRENGTH) "
                        ".. ' ' .. g(t.EXPERIENCE_SKILL) .. ' ' .. g(t.EXPERIENCE_WILL))"), "LFXP4", 10)
    return out[-1] if out else "?"


def eq_spawn_bandit(a, near, name):
    x, y, z = near
    out = lua_lines(a, (f"local ok, e = pcall(Debug.CreateEntityAtPosition, 'CreatureVillagerBandit', '{name}', CVector3({x:.2f}, {y:.2f}, {z:.2f})); "
                        f"print('LFBANDIT ' .. tostring(ok) .. ' ' .. tostring(e))"), "LFBANDIT", 10)
    return out[-1] if out else "?"


def eq_alive(a, name):
    out = lua_lines(a, (f"local s = SearchTools.StartNewSearch('object'); SearchTools.FilterWithinDistanceOfPos(s, GetPlayerHero():GetPosition(), 80); "
                        f"local n = 0; for _, o in pairs(SearchTools.GetSearchResults(s)) do local ok, nm = pcall(function() return o:GetName() end); "
                        f"if ok and nm == '{name}' and o:IsAlive() then n = n + 1 end end; print('LFALIVE ' .. n)"), "LFALIVE", 10)
    return out[-1] if out else "?"


def eq_page_path(b, downs, steps, shot_prefix):
    """Start -> the page `downs` below Abilities -> A; then each (key, shot) in steps."""
    b.send("press Start 200")
    time.sleep(2.5)
    for _ in range(downs):
        b.send("press Down 150")
        time.sleep(0.8)
    b.send("press A 200")
    time.sleep(3)
    for key, shot in steps:
        if key:
            b.send(f"press {key} 200")
            time.sleep(2.5)
        if shot:
            b.shot(f"{shot_prefix}_{shot}.png")


def eq_close_pages(b, n=4):
    for _ in range(n):
        b.send("press B 200")
        time.sleep(1.2)
    time.sleep(3)


def lf_eqxp():
    """001eq gameplay proof, part 2 (coordinator 2026-10-28 13:48Z): equip a sword and a
    pistol through the Weapons page and unequip with Y (both screens); the Clothing
    page preview; a released Will cast; XP: player 2 kills a bandit with the sword and
    one with Will, the host kills one (XP read on the host after each); then XP given
    on the host and a level bought on the partner's Abilities page."""
    tag = "lf_eqxp"
    slot, cid = eq_fresh_slot()
    if not cid:
        note("FAIL", f"{tag}: no fresh slot")
        return
    setup_tomls()
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        if not eq_host_lobby(a, tag, cid) or not partner_continue_join(a, b, tag):
            return
        a.window_capture = b.window_capture = True
        time.sleep(12)
        lua_lines(a, ("local h = GetPlayerHenchman(); for _, x in ipairs({'3966741D', '3A153787', '5A04B90B'}) do "
                      "pcall(Inventory.AddItemOfType, h, GDB.GetRecord('#' .. x):GetID()) end; print('LFEQGIVE ok')"), "LFEQGIVE", 10)
        time.sleep(8)
        kit0 = [l for l in a.lines() if "[lua] COOPKIT:v1" in l][-1:]
        # --- Weapons page: Melee -> the list -> the second sword -> A; then Ranged -> the pistol -> A
        na, nb = len(a.lines()), len(b.lines())
        eq_page_path(b, 2, [(None, "weapons_categories"), ("A", "melee_list"), ("Down", "melee_on_second"),
                            ("A", "melee_after_equip"), ("B", None), ("Down", "on_ranged"), ("A", "ranged_list"),
                            ("A", "ranged_after_equip")], tag)
        eq_close_pages(b)
        a.shot(f"{tag}_equip_world_host.png")
        b.shot(f"{tag}_equip_world_partner.png")
        host = [l[24:200] for l in a.lines()[na:] if "[coopkit] host: command" in l]
        part = [l[24:200] for l in b.lines()[nb:] if "[coopkit]" in l and ("command" in l or "copy" in l)]
        kit1 = [l for l in a.lines() if "[lua] COOPKIT:v1" in l][-1:]
        mr = lambda k: re.search(r"[|]melee=([^|]*)[|]ranged=([^|]*)", k[0]).groups() if k else "?"
        note("PASS" if sum("-> ok" in h for h in host) >= 2 else "FAIL",
             f"{tag}: equip via the page: host {host[:6]}; partner {part[:8]}; melee/ranged {mr(kit0)} -> {mr(kit1)}")
        # --- unequip: Y on the Weapons page
        na, nb = len(a.lines()), len(b.lines())
        eq_page_path(b, 2, [(None, "unequip_page_hint"), ("Y", "unequip_after_y")], tag)
        eq_close_pages(b)
        time.sleep(4)
        a.shot(f"{tag}_unequip_world_host.png")
        b.shot(f"{tag}_unequip_world_partner.png")
        host = [l[24:200] for l in a.lines()[na:] if "[coopkit] host: command" in l]
        part = [l[24:200] for l in b.lines()[nb:] if "unequipw" in l or "Unequip" in l]
        kit2 = [l for l in a.lines() if "[lua] COOPKIT:v1" in l][-1:]
        note("PASS" if any("unequipw" in h for h in host) and any("-> ok" in h for h in host) else "FAIL",
             f"{tag}: unequip (Y): host {host[:4]}; partner {part[:4]}; melee/ranged {mr(kit1)} -> {mr(kit2)}")
        # --- the Clothing page's 3D preview
        eq_page_path(b, 3, [(None, "clothing_page"), ("A", "clothing_list")], tag)
        eq_close_pages(b)
        # --- arena: both heroes in the forest, away from villagers
        x, y, z = EQ_ARENA
        lua_lines(a, f"Physics.TeleportToPosition(GetPlayerHero(), CVector3({x:.2f}, {y + 3:.2f}, {z:.2f})); print('LFAR ok')", "LFAR", 10)
        lua_lines(b, f"Physics.TeleportToPosition(GetPlayerHenchman(), CVector3({x:.2f}, {y:.2f}, {z:.2f})); print('LFAR ok')", "LFAR", 10)
        time.sleep(5)
        # --- Will released (tap B)
        b.send("press B 500")
        time.sleep(0.4)
        b.shot(f"{tag}_will_release_partner.png")
        a.shot(f"{tag}_will_release_host.png")
        time.sleep(2)
        # --- XP: player 2 kills with the sword (X), with Will (B), the host kills one (X)
        note("INFO", f"{tag}: XP before: player 2 {eq_xp(a, 'p2')}; host hero {eq_xp(a, 'hero')}")
        for label, attacker, key, hold, near in (("p2_sword", b, "X", 200, (x + 2, y, z)),
                                                 ("p2_will", b, "B", 1500, (x + 4, y, z)),
                                                 ("host_sword", a, "X", 200, (x + 2, y + 3, z))):
            nm = f"lfb_{label}"
            note("INFO", f"{tag}: {label}: spawn {eq_spawn_bandit(a, near, nm)}")
            time.sleep(3)
            for k in range(25):
                if eq_alive(a, nm).endswith(" 0"):
                    break
                attacker.send(f"press {key} {hold}")
                time.sleep(1.2 if key == "X" else 2.2)
                if k == 3:
                    a.shot(f"{tag}_{label}_fight_host.png")
                    b.shot(f"{tag}_{label}_fight_partner.png")
            time.sleep(6)  # orbs fly to whoever earned them
            note("INFO", f"{tag}: {label}: bandit alive {eq_alive(a, nm)}; XP after: player 2 {eq_xp(a, 'p2')}; host hero {eq_xp(a, 'hero')}")
            a.shot(f"{tag}_{label}_after_host.png")
            b.shot(f"{tag}_{label}_after_partner.png")
        # --- upgrade: give player 2 Strength XP on the host, buy Brutal Styles on the partner's page
        lua_lines(a, ("pcall(Experience.Modify, GetPlayerHenchman(), EExperienceType.EXPERIENCE_STRENGTH, 4000, false); print('LFGIVEXP ok')"),
                  "LFGIVEXP", 10)
        time.sleep(4)
        st0 = lua_lines(a, ("local h = GetPlayerHenchman(); local o, l = pcall(Stats.GetHeroAbilityLevel, h, EHeroAbilityType.HERO_ABILITY_STRENGTH_MELEE_WEAPONS); "
                            "print('LFLVL ' .. tostring(o and l))"), "LFLVL", 10)
        eq_page_path(b, 0, [(None, "abilities_before"), ("A", "strength_before"), ("A", "strength_after_buy"),
                            ("A", "strength_after_confirm")], tag)
        eq_close_pages(b)
        time.sleep(4)
        st1 = lua_lines(a, ("local h = GetPlayerHenchman(); local o, l = pcall(Stats.GetHeroAbilityLevel, h, EHeroAbilityType.HERO_ABILITY_STRENGTH_MELEE_WEAPONS); "
                            "print('LFLVL ' .. tostring(o and l))"), "LFLVL", 10)
        note("PASS" if st0 and st1 and st0[-1] != st1[-1] else "FAIL",
             f"{tag}: upgrade: player 2's Brutal Styles (melee weapons) level on the host {st0} -> {st1}; XP {eq_xp(a, 'p2')}")
        eq_page_path(b, 0, [(None, "abilities_after")], tag)
        eq_close_pages(b)
        note("INFO", f"{tag}: crashes: partner {b.count(r'Unhandled guest')}, host {a.count(r'Unhandled guest')}")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()



EQ_WEAP_LUA = ("local function wl(h) local t = {} for _, sl in ipairs({DummyObjects.HAND_RIGHT, DummyObjects.HAND_LEFT, "
               "DummyObjects.SHEATHE_BACK, DummyObjects.SHEATHE_RANGED_BACK, DummyObjects.SHEATHE_FRONT, DummyObjects.SHEATHE_HIP}) do "
               "local o, w = pcall(Carrying.GetEntityInSlot, h, sl); if o and w and w:IsAlive() then "
               "local ok, id = pcall(function() return w:GetRecord():GetID() end); t[#t + 1] = tostring(sl) .. '=' .. tostring(id) end end; "
               "return table.concat(t, ',') .. ' items=' .. tostring(Inventory.GetNumberOfItems(h)) end; ")


def lf_eqdiag():
    """001eq diagnostics (coordinator 2026-10-28 14:03Z), one launch:
      xp      host sets player 2 General 1234 / Strength 4321; the partner's native probe
              logs its copy's components with a float array at +16 (which type holds XP)
      unequip the Carrying call forms tried on the host, slots read back after each
      kill    a bandit spawned by player 2, the handle kept, the partner attacks with X
              until it is dead; XP on the host before / after; the same for the host
      pistol  the Weapons page walked to Ranged with a shot after every key"""
    tag = "lf_eqdiag"
    slot, cid = eq_fresh_slot()
    if not cid:
        note("FAIL", f"{tag}: no fresh slot")
        return
    setup_tomls()
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        if not eq_host_lobby(a, tag, cid) or not partner_continue_join(a, b, tag):
            return
        a.window_capture = b.window_capture = True
        time.sleep(12)
        # --- xp probe
        lua_lines(a, ("local h = GetPlayerHenchman(); local t = EExperienceType; "
                      "pcall(Experience.Modify, h, t.EXPERIENCE_GENERAL, 1234, false); pcall(Experience.Modify, h, t.EXPERIENCE_STRENGTH, 4321, false); "
                      "print('LFXPSET ok')"), "LFXPSET", 10)
        time.sleep(6)
        nb = len(b.lines())
        b.send("cvar coop_kit_xp_probe true")
        time.sleep(3)
        note("INFO", f"{tag}: host player 2 XP {eq_xp(a, 'p2')}; partner probe {[l[24:260] for l in b.lines()[nb:] if 'xp probe' in l]}")
        # --- unequip forms (host)
        forms = (("ReturnEntityToInventory(w)", "pcall(Carrying.ReturnEntityToInventory, w)"),
                 ("ReturnEntityToInventory(h, w)", "pcall(Carrying.ReturnEntityToInventory, h, w)"),
                 ("RemoveEntityFromSlot(h, slot) + Inventory.AddEntity(h, w)",
                  "pcall(Carrying.RemoveEntityFromSlot, h, sl); pcall(Inventory.AddEntity, h, w)"))
        for label, call in forms:
            out = lua_lines(a, (EQ_WEAP_LUA + "local h = GetPlayerHenchman(); local before = wl(h); local w, sl; "
                                "for _, s in ipairs({DummyObjects.HAND_RIGHT, DummyObjects.SHEATHE_BACK, DummyObjects.SHEATHE_HIP, DummyObjects.SHEATHE_FRONT}) do "
                                "local o, e = pcall(Carrying.GetEntityInSlot, h, s); if o and e and e:IsAlive() then w, sl = e, s break end end; "
                                f"if not w then print('LFUNEQ none: ' .. before) return end; local r1, r2 = {call}; "
                                f"print('LFUNEQ {label}: ' .. tostring(r1) .. ' ' .. tostring(r2) .. ' | before ' .. before .. ' | after ' .. wl(h))"),
                            "LFUNEQ", 10)
            note("INFO", f"{tag}: unequip {out}")
            time.sleep(2)
            lua_lines(a, ("local h = GetPlayerHenchman(); pcall(WeaponSetManager.SelectWeaponsFromInventory, h); print('LFRESEL ok')"), "LFRESEL", 10)
            time.sleep(3)
        a.shot(f"{tag}_after_unequip_tests_host.png")
        b.shot(f"{tag}_after_unequip_tests_partner.png")
        # --- kill XP: player 2 (X), then the host (X)
        x, y, z = EQ_ARENA
        lua_lines(a, f"Physics.TeleportToPosition(GetPlayerHero(), CVector3({x:.2f}, {y + 4:.2f}, {z:.2f})); print('LFAR ok')", "LFAR", 10)
        lua_lines(b, (f"local me = GetPlayerHenchman(); Physics.TeleportToPosition(me, CVector3({x:.2f}, {y:.2f}, {z:.2f})); "
                      f"Physics.SetFacingVector(me, CVector3(1, 0, 0)); print('LFAR ok')"), "LFAR", 10)
        time.sleep(5)
        for label, attacker, pos in (("p2", b, (x + 1.6, y, z)), ("host", a, (x + 1.6, y + 4, z))):
            before = (eq_xp(a, "p2"), eq_xp(a, "hero"))
            lua_lines(a, (f"local ok, e = pcall(Debug.CreateEntityAtPosition, 'CreatureVillagerBandit', 'lfb_{label}', "
                          f"CVector3({pos[0]:.2f}, {pos[1]:.2f}, {pos[2]:.2f})); LFB = ok and e or nil; print('LFBANDIT ' .. tostring(ok))"),
                      "LFBANDIT", 10)
            time.sleep(3)
            hits = 0
            for k in range(30):
                alive = lua_lines(a, "print('LFBA ' .. tostring(LFB and LFB:IsAlive()))", "LFBA", 6)
                if alive and alive[-1] == "false":
                    break
                attacker.send("press X 200")
                hits += 1
                time.sleep(1.0)
                if k == 4:
                    a.shot(f"{tag}_{label}_fight_host.png")
                    b.shot(f"{tag}_{label}_fight_partner.png")
            time.sleep(8)  # orbs
            after = (eq_xp(a, "p2"), eq_xp(a, "hero"))
            note("INFO", f"{tag}: kill by {label}: {hits} presses, bandit alive {alive}; XP player 2 {before[0]} -> {after[0]}; "
                         f"host hero {before[1]} -> {after[1]}")
            a.shot(f"{tag}_{label}_after_kill_host.png")
            b.shot(f"{tag}_{label}_after_kill_partner.png")
        # --- pistol walk
        b.send("press Start 200")
        time.sleep(2.5)
        for k, key in enumerate(("Down", "Down", "A", "Down", "A", "A")):
            b.send(f"press {key} 200")
            time.sleep(2.2)
            b.shot(f"{tag}_pistol_{k}_{key}.png")
        eq_close_pages(b)
        note("INFO", f"{tag}: pistol walk commands: host {[l[24:200] for l in a.lines() if 'host: command' in l][-3:]}")
        note("INFO", f"{tag}: crashes: partner {b.count(r'Unhandled guest')}, host {a.count(r'Unhandled guest')}")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()



def eq_kill_in_place(a, b, tag, label, attacker):
    """A bandit spawned 1.6 m in front of the attacker where it stands; the attacker
    faces it and presses X until it is dead (the handle kept); XP read on the host."""
    who = "GetPlayerHenchman()" if attacker is b else "GetPlayerHero()"
    before = (eq_xp(a, "p2"), eq_xp(a, "hero"))
    pos = lua_lines(a, (f"local e = {who}; local p = e:GetPosition(); local f = Physics.GetFacingVector(e); "
                        "local s = CVector3(p:GetX() + f:GetX() * 1.6, p:GetY() + f:GetY() * 1.6, p:GetZ()); "
                        f"local ok, b = pcall(Debug.CreateEntityAtPosition, 'CreatureVillagerBandit', 'lfb_{label}', s); "
                        "LFB = ok and b or nil; print(string.format('LFKPOS %s %.2f %.2f %.2f', tostring(ok), s:GetX(), s:GetY(), s:GetZ()))"),
                    "LFKPOS", 10)
    time.sleep(3)
    hits, alive = 0, ["?"]
    for k in range(40):
        alive = lua_lines(a, "print('LFBA ' .. tostring(LFB and LFB:IsAlive()))", "LFBA", 6)
        if alive and alive[-1] == "false":
            break
        attacker.send("press X 200")
        hits += 1
        time.sleep(0.9)
        if k == 3:
            a.shot(f"{tag}_{label}_fight_host.png")
            b.shot(f"{tag}_{label}_fight_partner.png")
    time.sleep(8)  # orbs
    after = (eq_xp(a, "p2"), eq_xp(a, "hero"))
    note("INFO", f"{tag}: kill by {label}: spawn {pos}; {hits} presses; bandit alive {alive}; XP player 2 {before[0]} -> {after[0]}; "
                 f"host hero {before[1]} -> {after[1]}")
    a.shot(f"{tag}_{label}_after_kill_host.png")
    b.shot(f"{tag}_{label}_after_kill_partner.png")


def lf_eqtrace():
    """001eq trace launch (coordinator 2026-10-28 17:02Z): load-in + the default weapon swap;
    what the partner's Abilities page reads for XP (coop_trace_xp, joiner); the host's own
    purchase traced (the learn path); Y-unequip with the working form; kills in place
    (player 2, then the host); the pistol through Ranged."""
    tag = "lf_eqtrace"
    slot, cid = eq_fresh_slot()
    if not cid:
        note("FAIL", f"{tag}: no fresh slot")
        return
    setup_tomls()
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        if not eq_host_lobby(a, tag, cid) or not partner_continue_join(a, b, tag):
            return
        a.window_capture = b.window_capture = True
        time.sleep(40)  # the seed's weapons step runs up to ~30 s after the spawn
        swap = [l[24:200] for l in a.lines() if "default henchman weapon" in l or "weapons: added" in l]
        note("INFO", f"{tag}: seed weapons {swap}; slots {lua_lines(a, EQ_WEAP_LUA + 'print(' + chr(39) + 'LFSL ' + chr(39) + ' .. wl(GetPlayerHenchman()))', 'LFSL', 10)}")
        b.shot(f"{tag}_loadin_partner.png")
        a.shot(f"{tag}_loadin_host.png")
        # --- the partner's Abilities page: what it reads for XP
        lua_lines(a, ("local h = GetPlayerHenchman(); local t = EExperienceType; "
                      "pcall(Experience.Modify, h, t.EXPERIENCE_GENERAL, 1111, false); pcall(Experience.Modify, h, t.EXPERIENCE_STRENGTH, 2222, false); "
                      "print('LFXPSET ok')"), "LFXPSET", 10)
        time.sleep(5)
        nb = len(b.lines())
        b.send("cvar coop_trace_xp true")
        eq_page_path(b, 0, [(None, "p_abilities"), ("A", "p_strength")], tag)
        eq_close_pages(b)
        b.send("cvar coop_trace_xp false")
        note("INFO", f"{tag}: partner page trace (host player 2 XP {eq_xp(a, 'p2')}): "
                     f"{[l[24:240] for l in b.lines()[nb:] if '[eqtrace]' in l][:40]}")
        # --- the host buys on its own page (the learn path)
        lua_lines(a, ("pcall(Experience.Modify, GetPlayerHero(), EExperienceType.EXPERIENCE_STRENGTH, 6000, false); print('LFHXP ok')"), "LFHXP", 10)
        lvl = ("local o, l = pcall(Stats.GetHeroAbilityLevel, GetPlayerHero(), EHeroAbilityType.HERO_ABILITY_STRENGTH_MELEE_WEAPONS); "
               "print('LFHLVL ' .. tostring(o and l))")
        l0 = lua_lines(a, lvl, "LFHLVL", 10)
        na = len(a.lines())
        a.send("cvar coop_trace_xp true")
        eq_page_path(a, 0, [(None, "h_abilities"), ("A", "h_strength"), ("A", "h_after_buy"), ("A", "h_after_confirm")], tag)
        eq_close_pages(a)
        a.send("cvar coop_trace_xp false")
        l1 = lua_lines(a, lvl, "LFHLVL", 10)
        note("INFO", f"{tag}: host purchase: Brutal Styles {l0} -> {l1}; XP {eq_xp(a, 'hero')}; trace "
                     f"{[l[24:240] for l in a.lines()[na:] if '[eqtrace]' in l][:60]}")
        # --- Y-unequip (the working form)
        na, nb = len(a.lines()), len(b.lines())
        eq_page_path(b, 2, [(None, "unequip_hint"), ("Y", "unequip_after_y")], tag)
        eq_close_pages(b)
        time.sleep(4)
        note("INFO", f"{tag}: unequip: host {[l[24:200] for l in a.lines()[na:] if 'host: command' in l][:3]}; slots "
                     f"{lua_lines(a, EQ_WEAP_LUA + 'print(' + chr(39) + 'LFSL ' + chr(39) + ' .. wl(GetPlayerHenchman()))', 'LFSL', 10)}")
        a.shot(f"{tag}_unequip_host.png")
        b.shot(f"{tag}_unequip_partner.png")
        # --- kills in place
        lua_lines(a, ("local h = GetPlayerHenchman(); pcall(Inventory.AddItemOfType, h, GDB.GetRecord('#3966741D'):GetID()); "
                      "for i = 0, Inventory.GetNumberOfItems(h) - 1 do local it = Inventory.GetItemByIndex(h, i); "
                      "local ok, id = pcall(function() return it:GetRecord():GetID() end); "
                      "if ok and tostring(id):upper():find('3966741D', 1, true) then pcall(WeaponSetManager.EquipWeapon, h, it) end end; print('LFARM ok')"),
                  "LFARM", 10)
        time.sleep(4)
        eq_kill_in_place(a, b, tag, "p2", b)
        eq_kill_in_place(a, b, tag, "host", a)
        # --- the pistol through Ranged
        lua_lines(a, ("pcall(Inventory.AddItemOfType, GetPlayerHenchman(), GDB.GetRecord('#3A153787'):GetID()); print('LFPIS ok')"), "LFPIS", 10)
        time.sleep(6)
        na = len(a.lines())
        eq_page_path(b, 2, [(None, "pistol_cat"), ("Down", "pistol_on_ranged"), ("A", "pistol_list"), ("A", "pistol_after_a")], tag)
        eq_close_pages(b)
        time.sleep(4)
        note("INFO", f"{tag}: pistol: host {[l[24:200] for l in a.lines()[na:] if 'host: command' in l][:3]}; slots "
                     f"{lua_lines(a, EQ_WEAP_LUA + 'print(' + chr(39) + 'LFSL ' + chr(39) + ' .. wl(GetPlayerHenchman()))', 'LFSL', 10)}")
        a.shot(f"{tag}_pistol_host.png")
        b.shot(f"{tag}_pistol_partner.png")
        note("INFO", f"{tag}: crashes: partner {b.count(r'Unhandled guest')}, host {a.count(r'Unhandled guest')}")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()



EQ_LOW_HP = ("local function low(e) local ok = pcall(Combat.SetDamageMultiplier, e, 100); "
             "return ok and 'Combat.SetDamageMultiplier 100' or 'none' end; ")


def eq_kill_one_hit(a, b, tag, label, attacker, key, hold):
    """A bandit 1.6 m in front of the attacker, its health set to 1 on the host, one
    attack (X sword or B Will); dead or not; XP of both read on the host."""
    who = "GetPlayerHenchman()" if attacker is b else "GetPlayerHero()"
    before = (eq_xp(a, "p2"), eq_xp(a, "hero"))
    pos = lua_lines(a, (EQ_LOW_HP + f"local e = {who}; local p = e:GetPosition(); local f = Physics.GetFacingVector(e); "
                        "local s = CVector3(p:GetX() + f:GetX() * 1.6, p:GetY() + f:GetY() * 1.6, p:GetZ()); "
                        f"local ok, bd = pcall(Debug.CreateEntityAtPosition, 'CreatureVillagerBandit', 'lfb_{label}', s); "
                        "LFB = ok and bd or nil; local how = LFB and low(LFB) or 'nil'; low(e); "
                        "print('LFK1 ' .. tostring(ok) .. ' health via ' .. how)"), "LFK1", 10)
    time.sleep(2.5)
    a.shot(f"{tag}_{label}_before_hit_host.png")
    for k in range(6):  # one attack; repeated only if the first did not connect
        attacker.send(f"press {key} {hold}")
        time.sleep(2.5 if key == "B" else 1.2)
        alive = lua_lines(a, "print('LFBA ' .. tostring(LFB and LFB:IsAlive()))", "LFBA", 6)
        if k == 0:
            a.shot(f"{tag}_{label}_hit_host.png")
            b.shot(f"{tag}_{label}_hit_partner.png")
        if alive and alive[-1] == "false":
            break
    time.sleep(8)  # orbs
    lua_lines(a, f"pcall(Combat.SetDamageMultiplier, {who}, 1); print('LFDM ok')", "LFDM", 6)
    after = (eq_xp(a, "p2"), eq_xp(a, "hero"))
    note("INFO", f"{tag}: kill by {label} ({key}): {pos}; attacks {k + 1}; bandit alive {alive}; "
                 f"XP player 2 {before[0]} -> {after[0]}; host hero {before[1]} -> {after[1]}")
    a.shot(f"{tag}_{label}_after_host.png")
    b.shot(f"{tag}_{label}_after_partner.png")


def lf_eqproof():
    """001eq proof launch (coordinator 2026-10-28 17:19Z): load-in + starter kit + close-ups;
    the hat on / off (copy mirrored both ways); sword and pistol equipped through the
    partner's pages; Y unequips the melee weapon, then the ranged one; a Will cast; R3;
    a single A at a chest; kill XP with one-hit bandits (player 2 sword, player 2 Will,
    the host)."""
    tag = "lf_eqproof"
    slot, cid = eq_fresh_slot()
    if not cid:
        note("FAIL", f"{tag}: no fresh slot")
        return
    setup_tomls()
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    slots_lua = EQ_WEAP_LUA + "print('LFSL ' .. wl(GetPlayerHenchman()))"
    try:
        if not eq_host_lobby(a, tag, cid) or not partner_continue_join(a, b, tag):
            return
        a.window_capture = b.window_capture = True
        body = [l[24:200] for l in a.lines() if "player 2 body" in l][-1:]
        note("PASS" if body and "selector index 4" in body[0] else "FAIL", f"{tag}: load-in: {body}")
        time.sleep(40)
        swap = [l[24:200] for l in a.lines() if "default henchman weapon" in l]
        kit = [l for l in a.lines() if "[lua] COOPKIT:v1" in l][-1:]
        worn = re.search(r"[|]worn=([^|]*)", kit[0]).group(1) if kit else "?"
        note("PASS" if swap and set(worn.split(",")) == {"BD511D47", "9D696C51", "CBD8841A", "DC6052C6", "331B2070"} else "FAIL",
             f"{tag}: starter kit: worn={worn}; {swap[:1]}; slots {lua_lines(a, slots_lua, 'LFSL', 10)}")
        eq_closeups(a, b, tag)
        # hat on / off
        lua_lines(a, ("local h = GetPlayerHenchman(); for _, x in ipairs({'5A04B90B', '3966741D', '3A153787'}) do "
                      "pcall(Inventory.AddItemOfType, h, GDB.GetRecord('#' .. x):GetID()) end; print('LFEQGIVE ok')"), "LFEQGIVE", 10)
        time.sleep(8)
        for op in ("wear", "unwear"):
            nb = len(b.lines())
            b.send(f"cvar coop_kit_send_line {op} 5A04B90B")
            time.sleep(7)
            mir = [l[24:200] for l in b.lines()[nb:] if "on the copy" in l]
            note("PASS" if any(("wore 1" in m) if op == "wear" else ("took off 1" in m) for m in mir) else "FAIL",
                 f"{tag}: hat {op}: the copy {mir[:2]}")
            a.shot(f"{tag}_hat_{op}_host.png")
            b.shot(f"{tag}_hat_{op}_partner.png")
        # sword (Melee) and pistol (Ranged) through the pages
        for label, steps in (("sword", [(None, "sword_cat"), ("A", "sword_list"), ("Down", "sword_on_second"), ("A", "sword_after")]),
                             ("pistol", [(None, "pistol_cat"), ("Down", "pistol_on_ranged"), ("A", "pistol_list"), ("A", "pistol_after")])):
            na = len(a.lines())
            eq_page_path(b, 2, steps, tag)
            eq_close_pages(b)
            time.sleep(4)
            host = [l[24:200] for l in a.lines()[na:] if "host: command" in l]
            note("PASS" if any("-> ok" in h for h in host) else "FAIL",
                 f"{tag}: {label} equip: host {host[:2]}; slots {lua_lines(a, slots_lua, 'LFSL', 10)}")
            a.shot(f"{tag}_{label}_equipped_host.png")
            b.shot(f"{tag}_{label}_equipped_partner.png")
        # unequip: Y twice (melee, then ranged)
        for k in (1, 2):
            na = len(a.lines())
            eq_page_path(b, 2, [(None, f"unequip{k}_hint"), ("Y", f"unequip{k}_after_y")], tag)
            eq_close_pages(b)
            time.sleep(4)
            host = [l[24:200] for l in a.lines()[na:] if "host: command" in l]
            note("PASS" if any("-> ok" in h for h in host) else "FAIL",
                 f"{tag}: unequip {k}: host {host[:2]}; slots {lua_lines(a, slots_lua, 'LFSL', 10)}")
            a.shot(f"{tag}_unequip{k}_host.png")
            b.shot(f"{tag}_unequip{k}_partner.png")
        # re-arm with the sword for the fights
        lua_lines(a, ("local h = GetPlayerHenchman(); for i = 0, Inventory.GetNumberOfItems(h) - 1 do local it = Inventory.GetItemByIndex(h, i); "
                      "local ok, id = pcall(function() return it:GetRecord():GetID() end); "
                      "if ok and tostring(id):upper():find('3966741D', 1, true) then pcall(WeaponSetManager.EquipWeapon, h, it) break end end; print('LFARM ok')"),
                  "LFARM", 10)
        time.sleep(4)
        # Will cast, R3
        b.send("press B 1500")
        time.sleep(0.8)
        b.shot(f"{tag}_will_partner.png")
        a.shot(f"{tag}_will_host.png")
        time.sleep(3)
        b.send("stick L 0 1 1500")
        time.sleep(2.5)
        nb = len(b.lines())
        b.send("press RS 200")
        time.sleep(1.5)
        b.shot(f"{tag}_r3_partner.png")
        note("INFO", f"{tag}: R3 {[l[24:200] for l in b.lines()[nb:] if 'R3:' in l][:1]}")
        # kill XP, one-hit bandits
        # Kill XP is checked by hand: scripted attacks auto-target the nearest creature
        # and hit player 2 (proof run 2, 2026-10-28, lf_eqproof_host_sword_hit_host.png).
        # a single A at a chest
        ch = lf_chests(a, 120, 1)
        if ch:
            x, y, z = ch[0][:3]
            if lf_p2_to(b, x, y, z, "chest"):
                time.sleep(7)
                b.shot(f"{tag}_chest_hint.png")
                na = len(a.lines())
                b.send("press A 200")
                time.sleep(1.2)
                b.shot(f"{tag}_chest_opening.png")
                time.sleep(10)
                if any("game paused" in l for l in a.lines()[na:]):
                    a.send("press A 200")
                st = lf_state(a, "chest", x, y, z)
                note("PASS" if st == "true" else "FAIL", f"{tag}: chest: one A -> open {st}; host {[l[24:200] for l in a.lines()[na:] if 'world use' in l][:3]}")
        note("INFO", f"{tag}: crashes: partner {b.count(r'Unhandled guest')}, host {a.count(r'Unhandled guest')}")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()



PERF_RIG_PS1 = r"C:\Users\josh_\AppData\Local\Temp\claude\D--VoiceBank\2c833dc8-b1f5-4a88-af94-d98cc46c47dc\scratchpad\rig_release.ps1"


def perf_one(rel, settle=30, window=90):
    """mod-a <- release `rel`; solo Continue (the stock save picker's first card: the
    same newest save every run, no input after); after `settle` s, the runtime's own
    5 s counters for `window` s: swaps, frames over 20/33/50 ms, longest frame."""
    out = subprocess.run(["powershell", "-NoProfile", "-File", PERF_RIG_PS1, "-Rel", rel], capture_output=True, text=True)
    note("INFO", f"perf {rel}: install {out.stdout.strip()[-200:]} {out.stderr.strip()[-200:]}")
    setup_tomls()
    if "--josh-res" in sys.argv:
        # Josh's own display settings (mod's toml), so throughput below the vsync cap shows.
        josh = {}
        for line in open(os.path.join(ROOT, "mod", "fable_2.toml"), encoding="utf-8"):
            k = line.split("=", 1)[0].strip()
            if k in ("fullscreen", "fullscreen_exclusive", "window_width", "window_height", "resolution", "resolution_scale",
                     "vsync", "frame_limit"):
                josh[k] = line.rstrip("\r\n")
        tp = os.path.join(ROOT, "mod-a", "fable_2.toml")
        lines = [josh.get(l.split("=", 1)[0].strip(), l.rstrip("\r\n")) for l in open(tp, encoding="utf-8")]
        open(tp, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
        note("INFO", f"perf {rel}: mod-a display = Josh's: {sorted(josh.values())}")
    a = Game("mod-a", "host")
    a.start({"FABLE2_TEST_INPUT": "1"})
    try:
        n_menu = a.count(r"main menu: Co-op entry added")
        a.past_title()
        a.wait(f"perf {rel}: main menu", r"main menu: Co-op entry added", n_menu, 240)
        time.sleep(2)
        n = a.count(r"front-end scans paused \(the game's Lua is running\)")
        a.press("Down")
        time.sleep(1.0)
        a.press("E")
        time.sleep(12)
        a.press("E")
        time.sleep(4)
        if not a.count(r"front-end scans paused \(the game's Lua is running\)") > n:
            a.press("E")
        if not a.wait(f"perf {rel}: the world loads", r"front-end scans paused \(the game's Lua is running\)", n, 180):
            return None
        time.sleep(settle)
        start = len(a.lines())
        time.sleep(window)
        rows = []
        for l in a.lines()[start:]:
            m = re.search(r"Guest-visible writes in 5 s: (\d+) swaps", l)
            f = re.search(r"frames over 20/33/50 ms (\d+)/(\d+)/(\d+), longest (\d+) ms", l)
            if m and f:
                rows.append((int(m.group(1)), int(f.group(1)), int(f.group(2)), int(f.group(3)), int(f.group(4))))
        if not rows:
            note("FAIL", f"perf {rel}: no counter lines")
            return None
        k = len(rows)
        res = (sum(r[0] for r in rows) / k, sum(r[1] for r in rows), sum(r[2] for r in rows), sum(r[3] for r in rows),
               max(r[4] for r in rows), k)
        a.shot(f"perf_{rel}.png")
        note("INFO", f"perf {rel}: swaps/5s {res[0]:.1f}; frames over 20/33/50 ms {res[1]}/{res[2]}/{res[3]}; longest {res[4]} ms; "
                     f"{k} windows; crashes {a.count(r'Unhandled guest')}")
        return res
    finally:
        a.stop()


def run_perfab():
    rels = sys.argv[sys.argv.index("--rels") + 1].split(",") if "--rels" in sys.argv else ["release-001nx", "release-001jn", "release-001eq", "release-001eq2", "release-001nx"]
    table = []
    for rel in rels:
        table.append((rel, perf_one(rel)))
    for rel, res in table:
        if res:
            note("INFO", f"PERF TABLE {rel:16s} swaps/5s {res[0]:6.1f}  >20ms {res[1]:4d}  >33ms {res[2]:3d}  >50ms {res[3]:3d}  longest {res[4]:4d} ms  ({res[5]} windows)")
        else:
            note("INFO", f"PERF TABLE {rel:16s} no data")



def lf_uptrace():
    """001up trace (rig build with FABLE2_RIG_TRACE): the partner's Abilities page with
    the XP redirect (host gives player 2 XP first; the page should now show it); then
    the host buys a level on its own page (its wheel opens on Quests/Maps: Down to
    Abilities) with coop_trace_xp on, so the callers (thunks) of the Modify core and
    the Stats functions are logged for learn-on-host."""
    tag = "lf_uptrace"
    slot, cid = eq_fresh_slot()
    if not cid:
        note("FAIL", f"{tag}: no fresh slot")
        return
    setup_tomls()
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        if not eq_host_lobby(a, tag, cid) or not partner_continue_join(a, b, tag):
            return
        a.window_capture = b.window_capture = True
        time.sleep(15)
        # player 2 gets XP on the host; the partner's page shows it (the native write)
        lua_lines(a, ("local h = GetPlayerHenchman(); local t = EExperienceType; "
                      "pcall(Experience.Modify, h, t.EXPERIENCE_GENERAL, 1500, false); pcall(Experience.Modify, h, t.EXPERIENCE_STRENGTH, 5000, false); "
                      "print('LFXPSET ok')"), "LFXPSET", 10)
        time.sleep(8)
        kit = [l for l in a.lines() if "[lua] COOPKIT:v1" in l][-1:]
        note("INFO", f"{tag}: host player 2 XP {eq_xp(a, 'p2')}; kit xp {re.search(r'[|]xp=([^|]*)', kit[0]).group(1) if kit and '|xp=' in kit[0] else '?'}")
        nb = len(b.lines())
        b.send("cvar coop_trace_xp true")
        eq_page_path(b, 0, [(None, "p_abilities"), ("A", "p_strength"), ("A", "p_after_learn")], tag)
        eq_close_pages(b)
        note("INFO", f"{tag}: partner trace while it pressed Learn: {[l[24:240] for l in b.lines()[nb:] if '[eqtrace]' in l][:30]}")
        note("INFO", f"{tag}: player 2 Brutal Styles on the host after the partner's Learn: "
                     f"{lua_lines(a, 'local o, l = pcall(Stats.GetHeroAbilityLevel, GetPlayerHenchman(), EHeroAbilityType.HERO_ABILITY_STRENGTH_MELEE_WEAPONS); print(' + chr(39) + 'LFLVL ' + chr(39) + ' .. tostring(o and l))', 'LFLVL', 10)}; "
                     f"XP {eq_xp(a, 'p2')}")
        # the host buys on its own page (the learn path's calls)
        lua_lines(a, "pcall(Experience.Modify, GetPlayerHero(), EExperienceType.EXPERIENCE_STRENGTH, 6000, false); print('LFHXP ok')", "LFHXP", 10)
        lvl = ("local o, l = pcall(Stats.GetHeroAbilityLevel, GetPlayerHero(), EHeroAbilityType.HERO_ABILITY_STRENGTH_MELEE_WEAPONS); "
               "print('LFHLVL ' .. tostring(o and l))")
        l0 = lua_lines(a, lvl, "LFHLVL", 10)
        na = len(a.lines())
        # wait until the partner's pause has ended (its pages pause both games)
        t = time.time()
        while time.time() - t < 40 and b.count(r"partner menu: PauseGame") > b.count(r"partner menu: UnpauseGame"):
            time.sleep(1)
        time.sleep(5)
        a.shot(f"{tag}_h_before_wheel.png")
        a.send("cvar coop_trace_xp true")
        a.send("press Start 200")
        time.sleep(2.5)
        a.shot(f"{tag}_h_wheel.png")
        for k, key in enumerate(("Down", "A", "A", "A", "A")):
            a.send(f"press {key} 200")
            time.sleep(2.5)
            a.shot(f"{tag}_h_{k}_{key}.png")
        eq_close_pages(a, 5)
        a.send("cvar coop_trace_xp false")
        l1 = lua_lines(a, lvl, "LFHLVL", 10)
        note("INFO", f"{tag}: host purchase: Brutal Styles {l0} -> {l1}; XP {eq_xp(a, 'hero')}")
        note("INFO", f"{tag}: host trace: {[l[24:240] for l in a.lines()[na:] if '[eqtrace]' in l][:60]}")
        note("INFO", f"{tag}: crashes: partner {b.count(r'Unhandled guest')}, host {a.count(r'Unhandled guest')}")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()



def lf_upproof():
    """001up proof: the partner's Abilities page shows player 2's experience (given on
    the host) and levels; a Learn there is applied to player 2 on the host (level up,
    experience spent); the page shows the learned level after it is reopened."""
    tag = "lf_upproof"
    slot, cid = eq_fresh_slot()
    if not cid:
        note("FAIL", f"{tag}: no fresh slot")
        return
    setup_tomls()
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    lvl = ("local o, l = pcall(Stats.GetHeroAbilityLevel, GetPlayerHenchman(), EHeroAbilityType.HERO_ABILITY_STRENGTH_MELEE_WEAPONS); "
           "print('LFLVL ' .. tostring(o and l))")
    try:
        if not eq_host_lobby(a, tag, cid) or not partner_continue_join(a, b, tag):
            return
        a.window_capture = b.window_capture = True
        time.sleep(15)
        l0 = lua_lines(a, lvl, "LFLVL", 10)
        lua_lines(a, ("local h = GetPlayerHenchman(); local t = EExperienceType; "
                      "pcall(Experience.Modify, h, t.EXPERIENCE_GENERAL, 1500, false); pcall(Experience.Modify, h, t.EXPERIENCE_STRENGTH, 5000, false); "
                      "print('LFXPSET ok')"), "LFXPSET", 10)
        x0 = eq_xp(a, 'p2')
        note("INFO", f"{tag}: host: player 2 Brutal Styles {l0}, XP {x0}")
        time.sleep(10)
        note("INFO", f"{tag}: partner mirror: {[l[24:200] for l in b.lines() if '[coopkit] abilities' in l][-3:]}")
        eq_page_path(b, 0, [(None, "p_abilities"), ("A", "p_strength"), ("A", "p_after_learn"), ("A", "p_after_continue")], tag)
        eq_close_pages(b)
        time.sleep(15)
        note("INFO", f"{tag}: partner commands: {[l[24:200] for l in b.lines() if re.search(r'coopkit.*(learn|xpspend|abilities|command [0-9]+: )', l)][-8:]}")
        l1 = lua_lines(a, lvl, "LFLVL", 10)
        x1 = eq_xp(a, 'p2')
        note("PASS" if l1 and l1[-1] != (l0[-1] if l0 else None) else "FAIL",
             f"{tag}: host: player 2 Brutal Styles {l0} -> {l1}; XP {x0} -> {x1}")
        # cycle 2 (coordinator 20:52Z: two full open / Learn / close cycles): more XP, Level 3
        lua_lines(a, ("pcall(Experience.Modify, GetPlayerHenchman(), EExperienceType.EXPERIENCE_STRENGTH, 20000, false); "
                      "print('LFXPSET2 ok')"), "LFXPSET2", 10)
        x2 = eq_xp(a, 'p2')
        time.sleep(10)
        eq_page_path(b, 0, [(None, "p_abilities2"), ("A", "p_strength2"), ("A", "p_after_learn2"), ("A", "p_after_continue2")], tag)
        eq_close_pages(b)
        time.sleep(15)
        l3 = lua_lines(a, lvl, "LFLVL", 10)
        x3 = eq_xp(a, 'p2')
        note("PASS" if l3 and l1 and l3[-1] != l1[-1] else "FAIL",
             f"{tag}: cycle 2: host: player 2 Brutal Styles {l1} -> {l3}; XP {x2} -> {x3}")
        time.sleep(6)
        eq_page_path(b, 0, [(None, "p_abilities3"), ("A", "p_strength3")], tag)
        eq_close_pages(b)
        note("INFO", f"{tag}: partner commands: {[l[24:200] for l in b.lines() if re.search(r'coopkit.*(learn|xpspend|abilities|command [0-9]+: )', l)][-12:]}")
        # coordinator 22:21Z: after the partner's last page close the host still works
        time.sleep(5)
        a.shot(f"{tag}_h_after_pages.png")
        t0 = time.time()
        ping = lua_lines(a, "print('LFPING ok')", "LFPING", 15)
        note("PASS" if ping else "FAIL", f"{tag}: host Lua answers after the partner's pages: {ping} ({time.time() - t0:.1f} s)")
        nk = b.count(r"coopkit\] command [0-9]+: ok")
        nall = b.count(r"coopkit\] command [0-9]+: ")
        b.send("cvar coop_kit_send_line unequipw 00000001")
        t = time.time()
        while time.time() - t < 20 and b.count(r"coopkit\] command [0-9]+: ") <= nall:
            time.sleep(0.5)
        note("PASS" if b.count(r"coopkit\] command [0-9]+: ok") > nk else "FAIL",
             f"{tag}: a kit command after the pages: {[l[24:160] for l in b.lines() if re.search(r'coopkit\] command [0-9]+: ', l)][-1:]}")
        sv = lua_lines(a, "print('LFSAVE ' .. tostring(pcall(AutoSaveGame, true, true)))", "LFSAVE", 30)
        time.sleep(10)
        note("PASS" if sv and sv[-1].startswith("true") and not a.count(r"Unhandled guest") else "FAIL",
             f"{tag}: host Save after the pages: {sv}; host crashes {a.count(r'Unhandled guest')}")
        note("INFO", f"{tag}: partner spend capture: {[l[24:200] for l in b.lines() if 'experience changed' in l or 'xpspend' in l][-6:]}")
        note("INFO", f"{tag}: crashes: partner {b.count(r'Unhandled guest')}, host {a.count(r'Unhandled guest')}")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()



def lf_upprobe():
    """001up: which Lua call on the partner's own hero record (GetPlayerHero()) is
    unsafe (proof pair 1 crashed after a read-only mirror pass). The abilities mirror
    is off; one probe at a time, each followed by an alive check."""
    tag = "lf_upprobe"
    slot, cid = eq_fresh_slot()
    if not cid:
        note("FAIL", f"{tag}: no fresh slot")
        return
    setup_tomls()
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    b.send("cvar coop_kit_abilities false")
    H = "local h = GetPlayerHero(); "
    L = ("local n = 0; for _, k in ipairs({'STRENGTH_MELEE_WEAPONS','STRENGTH_PHYSIQUE','STRENGTH_TOUGHNESS','SKILL_RANGED_WEAPONS',"
         "'SKILL_ACCURACY','SKILL_SPEED','WILL_LIGHTNING','WILL_FIREBALL','WILL_SLOW_TIME','WILL_SWORDS','WILL_VORTEX','WILL_CHAOS',"
         "'WILL_FORCE_PUSH','WILL_DEAD_RISING'}) do local o, v = pcall(Stats.GetHeroAbilityLevel, h, EHeroAbilityType['HERO_ABILITY_' .. k]); "
         "if o then n = n + math.floor(v or 0) end end; ")
    X = ("local s = 0; for _, t in ipairs({'GENERAL','STRENGTH','SKILL','WILL'}) do local o, v = pcall(Experience.Get, h, EExperienceType['EXPERIENCE_' .. t]); "
         "if o then s = s + math.floor(v or 0) end end; ")
    def setp(name, k, lv):
        return (name, f"local h = GetPlayerHero(); local a = EHeroAbilityType.HERO_ABILITY_{k}; local o = pcall(Stats.SetHeroAbilityLevel, h, a, {lv}); "
                      f"local o2, v = pcall(Stats.GetHeroAbilityLevel, h, a); print('LFPROBE {name} ' .. tostring(o) .. ' ' .. tostring(v))")
    # 001up level sync: the setter's one creature-touching branch is native ability 3 (Toughness)
    probes = [setp("melee2", "STRENGTH_MELEE_WEAPONS", 2), setp("accuracy2", "SKILL_ACCURACY", 2),
              setp("lightning2", "WILL_LIGHTNING", 2), setp("melee1", "STRENGTH_MELEE_WEAPONS", 1),
              setp("physique2", "STRENGTH_PHYSIQUE", 2), setp("toughness2", "STRENGTH_TOUGHNESS", 2)]
    try:
        if not eq_host_lobby(a, tag, cid) or not partner_continue_join(a, b, tag):
            return
        b.send("cvar coop_kit_abilities false")
        time.sleep(20)
        b.send("luafile coop_abilities")
        time.sleep(4)
        for name, chunk in probes:
            out = lua_lines(b, chunk, "LFPROBE " + name + " ", 10)
            time.sleep(3)
            dead = b.count(r"Unhandled guest")
            note("FAIL" if dead else "PASS", f"{tag}: probe {name}: {out}; partner crashes {dead}")
            if dead:
                break
        if False:
            b.send("cvar coop_kit_abilities true")
            time.sleep(20)
            note("INFO", f"{tag}: full mirror on: {[l[24:200] for l in b.lines() if 'abilities' in l and 'coopkit' in l][-3:]}; "
                         f"partner crashes {b.count(r'Unhandled guest')}")
        note("INFO", f"{tag}: crashes: partner {b.count(r'Unhandled guest')}, host {a.count(r'Unhandled guest')}")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()



UP_LEVELS = ("local h = GetPlayerHenchman(); local out = {}; for _, k in ipairs({'STRENGTH_MELEE_WEAPONS','STRENGTH_PHYSIQUE',"
             "'STRENGTH_TOUGHNESS','SKILL_RANGED_WEAPONS','SKILL_ACCURACY','SKILL_SPEED','WILL_LIGHTNING','WILL_FIREBALL',"
             "'WILL_SLOW_TIME','WILL_SWORDS','WILL_VORTEX','WILL_CHAOS','WILL_FORCE_PUSH','WILL_DEAD_RISING'}) do "
             "local o, v = pcall(Stats.GetHeroAbilityLevel, h, EHeroAbilityType['HERO_ABILITY_' .. k]); out[#out + 1] = tostring(o and v) end; "
             "print('LFLV14 ' .. table.concat(out, ','))")


def lf_uplevels():
    """001up proof (coordinator 22:34Z): a Brutal Styles Learn and a Will upgrade on the
    partner's page, applied to player 2 on the host; a Will cast on both screens; then
    the partner leaves and rejoins (the host saves and continues the co-op save): the
    partner's page shows player 2's levels, not the host hero's it reloaded."""
    tag = "lf_uplevels"
    slot, cid = eq_fresh_slot()
    if not cid:
        note("FAIL", f"{tag}: no fresh slot")
        return
    setup_tomls()
    relay_log = open(os.path.join(OUT, f"relay_{tag}.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        if not eq_host_lobby(a, tag, cid) or not partner_continue_join(a, b, tag):
            return
        a.window_capture = b.window_capture = True
        b.wait(f"{tag}: the Learn check is armed", r"Learn check armed", 0, 60)
        time.sleep(3)
        if b.count(r"Unhandled guest"):
            note("FAIL", f"{tag}: the partner crashed at the first mirror pass")
            return
        lua_lines(a, ("local h = GetPlayerHenchman(); local t = EExperienceType; "
                      "pcall(Experience.Modify, h, t.EXPERIENCE_GENERAL, 1500, false); pcall(Experience.Modify, h, t.EXPERIENCE_STRENGTH, 5000, false); "
                      "pcall(Experience.Modify, h, t.EXPERIENCE_WILL, 20000, false); print('LFXPSET ok')"), "LFXPSET", 10)
        lv0 = lua_lines(a, UP_LEVELS, "LFLV14", 10)
        hero0 = lua_lines(a, UP_LEVELS.replace("GetPlayerHenchman()", "GetPlayerHero()"), "LFLV14", 10)
        note("INFO", f"{tag}: player 2 levels {lv0}, XP {eq_xp(a, 'p2')}; host hero levels {hero0}")
        time.sleep(10)
        # Brutal Styles 1 -> 2
        eq_page_path(b, 0, [(None, "p_abilities"), ("A", "p_strength"), ("A", "p_learned"), ("A", "p_continue")], tag)
        eq_close_pages(b)
        time.sleep(12)
        # a Will ability: the Abilities list opens on Strength; two Downs, A opens Will's list
        eq_page_path(b, 0, [(None, "p_will_0"), ("Down", "p_will_1"), ("Down", "p_will_2"), ("A", "p_will_3"),
                            ("A", "p_will_learned"), ("A", "p_will_continue")], tag)
        eq_close_pages(b)
        time.sleep(15)
        lv1 = lua_lines(a, UP_LEVELS, "LFLV14", 10)
        note("PASS" if lv1 and lv0 and lv1 != lv0 else "FAIL",
             f"{tag}: player 2 levels after the partner's Learns {lv0} -> {lv1}; XP {eq_xp(a, 'p2')}")
        note("INFO", f"{tag}: partner: {[l[24:200] for l in b.lines() if re.search(r'coopkit.*(learn|xpspend|experience changed|command [0-9]+: )', l)][-10:]}")
        # a Will cast on both screens
        b.send("press B 1500")
        time.sleep(0.8)
        b.shot(f"{tag}_will_partner.png")
        a.shot(f"{tag}_will_host.png")
        time.sleep(4)
        # the partner leaves; the host saves, returns, continues the co-op save; the partner rejoins
        b.stop()
        n = a.count(r"host: back at the front end")
        a.wait(f"{tag}: host saves and returns to the menu", r"host: back at the front end", n, 240)
        time.sleep(15)
        # Both games restart and continue the co-op save (a later evening's rejoin). The
        # menu's Co-op entry can take minutes to come back after a quit to the front end.
        a.stop()
        a = Game("mod-a", "host")
        a.start({"FABLE2_TEST_INPUT": "1"})
        if not host_continue_lobby(a, tag + "_r"):
            return
        b = Game("mod-b", "partner")
        b.start({"FABLE2_TEST_INPUT": "1"})
        if not partner_continue_join(a, b, tag + "_r"):
            return
        a.window_capture = b.window_capture = True
        b.wait(f"{tag}_r: the Learn check is armed", r"Learn check armed", 0, 60)
        lv2 = lua_lines(a, UP_LEVELS, "LFLV14", 10)
        hero2 = lua_lines(a, UP_LEVELS.replace("GetPlayerHenchman()", "GetPlayerHero()"), "LFLV14", 10)
        note("INFO", f"{tag}: after the rejoin: player 2 levels {lv2}, host hero levels {hero2}")
        note("INFO", f"{tag}: partner mirror after the rejoin: {[l[24:220] for l in b.lines() if 'coopkit] abilities' in l][-4:]}")
        eq_page_path(b, 0, [(None, "r_p_abilities"), ("A", "r_p_strength")], tag)
        eq_close_pages(b)
        time.sleep(5)
        eq_page_path(b, 0, [("Down", None), ("Down", None), ("A", None)], tag)
        b.shot(f"{tag}_r_p_will.png")
        eq_close_pages(b)
        note("INFO", f"{tag}: crashes: partner {b.count(r'Unhandled guest')}, host {a.count(r'Unhandled guest')}")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def run_livefix():
    phases = sys.argv[sys.argv.index("--phases") + 1].split(",") if "--phases" in sys.argv else ["empty", "male", "female"]
    if "empty" in phases:
        lf_empty_boot()
    if "male" in phases:
        lf_session(False)
    if "female" in phases:
        lf_session(True)
    if "oak" in phases:
        lf_oakfield()
    if "gold" in phases:
        lf_session(False, gold_only=True)
    if "use" in phases:
        lf_use()
    if "use1" in phases:
        lf_use1()
    if "use23" in phases:
        lf_use23()
    if "next" in phases:
        lf_next()
    if "jn" in phases:
        lf_jn()
    if "jnsolo" in phases:
        lf_jn_solo()
    if "eqprobe" in phases:
        lf_eqprobe()
    if "eq" in phases:
        lf_eq()
    if "eqplay" in phases:
        lf_eqplay()
    if "eqxp" in phases:
        lf_eqxp()
    if "eqdiag" in phases:
        lf_eqdiag()
    if "eqtrace" in phases:
        lf_eqtrace()
    if "eqproof" in phases:
        lf_eqproof()
    if "uptrace" in phases:
        lf_uptrace()
    if "upproof" in phases:
        lf_upproof()
    if "upprobe" in phases:
        lf_upprobe()
    if "uplevels" in phases:
        lf_uplevels()


def run_dynsync():
    """Do objects spawned in the host's world reach the partner? A gold bag is
    spawned (Debug.CreateEntityAtPosition, the call the game's own quest scripts
    use for NPCs) straight at a spot 1 m in front of the partner's hero, by its
    own position and facing (a teleport of an existing object is not sent to
    the partner, gold2 2026-10-26). Screenshots of both copies; the partner
    presses A. Pass: the bag is gone on the host and the host purse, the host's
    player 2 and the partner's mirror each move once by the same amount."""
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_dynsync.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        menu_then_coop(a, "dyn", a.n_menu)
        host_new_campaign(a, "dyn")
        menu_then_coop(b, "dyn", b.n_menu)
        wait_row(b, "Join TestA", "dyn: the host's lobby is listed")
        partner_joins(b, "Join TestA", "dyn")
        wait_row(a, "Start", "dyn: host lobby shows the partner")
        if not both_ready_and_start(a, b, "dyn") or not join_phase(a, b, "dyn"):
            return
        time.sleep(8)
        for k, dist in enumerate((1.0, 0.7, 1.3)):
            pose = wg_partner_pose(b)
            if not pose:
                continue
            x, y, z, fx, fy = pose
            n = math.hypot(fx, fy) or 1.0
            px, py = x + fx / n * dist, y + fy / n * dist
            out = lua_lines(a, (
                f"local ok, e = pcall(function() DYNB = Debug.CreateEntityAtPosition('ObjectInventoryGoldBag', "
                f"'dynbag{k}', CVector3({px:.3f}, {py:.3f}, {z:.3f})); print('DYNSPAWN ' .. tostring(DYNB:IsAlive()) .. "
                f"' gold=' .. tostring(Money.IsAvailable(DYNB) and Money.Get(DYNB) or -1)) end); "
                f"if not ok then print('DYNSPAWN error ' .. tostring(e)) end"), "DYNSPAWN", 10)
            note("INFO", f"dyn: bag {k} spawned {dist} m in front of the partner ({px:.1f}, {py:.1f}): {out}")
            time.sleep(3)
            a.shot(f"dyn_bag{k}_host.png")
            b.shot(f"dyn_bag{k}_partner.png")
            before = read_purses(a, b, f"dyn_before_{k}")
            b.send("press A 200")
            time.sleep(4)
            wg_dismiss(a, b, b, f"dyn_{k}")
            gone = lua_lines(a, "print('DYNPICK ' .. tostring(DYNB:IsAlive()))", "DYNPICK", 8)
            after = read_purses(a, b, f"dyn_after_{k}")
            b.shot(f"dyn_bag{k}_partner_after.png")
            note("INFO", f"dyn: bag {k} alive after the partner's A: {gone}; purses {before} -> {after}")
            if gone and "false" in gone[-1] and before and after:
                d = [after[i] - before[i] for i in range(3)]
                note("PASS" if d[0] == d[1] == d[2] > 0 else "FAIL",
                     f"dyn: the partner picked up a host-spawned bag; each purse changed by {d} (once, the same)")
                return
        note("FAIL", "dyn: the partner did not pick up a host-spawned bag (see dyn_bag*_partner.png: is it there?)")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def run_luaq_kept():
    """The kept / retry path (9cace5a): while the partner's pause holds, the host
    queues 12 numbered markers and a gold change (13 one-shots, 8 console slots):
    some are refused and kept, then all run once, in order, after the pause."""
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_luaq_kept.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        menu_then_coop(a, "kept", a.n_menu)
        host_new_campaign(a, "kept")
        menu_then_coop(b, "kept", b.n_menu)
        wait_row(b, "Join TestA", "kept: the host's lobby is listed")
        partner_joins(b, "Join TestA", "kept")
        wait_row(a, "Start", "kept: host lobby shows the partner")
        if not both_ready_and_start(a, b, "kept") or not join_phase(a, b, "kept"):
            return
        time.sleep(8)
        v = read_purses(a, b, "kept_purse0")
        if not v:
            return
        start = v[0]
        n_refused = a.count(r"not queued")
        n_kept = a.count(r"chunk kept until a console slot is free")
        n_kq = a.count(r"kept chunk queued")
        b.press("Start")  # the partner's pause menu: both games pause, queued scripts wait
        time.sleep(2)
        b.shot("kept_pause_partner.png")
        # 13 one-shots in one read: 8 take the console slots, the rest are kept.
        lines = [f"lua print('LUAQ k{i}')" for i in range(1, 7)]
        lines.append("lua Money.Modify(GetPlayerHero(), 150) print('LUAQ kgold')")
        lines += [f"lua print('LUAQ k{i}')" for i in range(7, 13)]
        with open(os.path.join(a.dir, "test_input.txt"), "a", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        time.sleep(12)
        a.shot("kept_pause_host.png")
        b.press("Start")
        expect = start + 150
        time.sleep(15)
        v = read_purses(a, b, "kept_after_pause")
        ok = v is not None and v[0] == v[1] == v[2] == expect
        note("PASS" if ok else "FAIL", f"kept: after the pause host {v[0] if v else '?'}, host's player 2 "
                                         f"{v[1] if v else '?'}, partner's own {v[2] if v else '?'} (expected {expect})")
        a.shot("kept_after_pause_host.png")
        b.shot("kept_after_pause_partner.png")
        refused = a.count(r"not queued") - n_refused
        kept = a.count(r"chunk kept until a console slot is free") - n_kept
        queued = a.count(r"kept chunk queued") - n_kq
        note("PASS" if refused > 0 and kept > 0 else "FAIL",
             f"kept: the full queue refused chunks ({refused} refusals) and {kept} were kept")
        note("PASS" if kept == queued else "FAIL", f"kept: {queued} of {kept} kept chunks queued later ({kept - queued} lost)")
        # Each marker exactly once, in order (the Lua print itself: "[lua] LUAQ kN").
        order = [m.group(1) for l in a.lines() for m in [re.search(r"\[lua\] LUAQ (k\d+|kgold)$", l.rstrip())] if m]
        want = [f"k{i}" for i in range(1, 7)] + ["kgold"] + [f"k{i}" for i in range(7, 13)]
        once = all(order.count(w) == 1 for w in want)
        note("PASS" if once else "FAIL", f"kept: every marker ran exactly once ({len(order)} runs for {len(want)} markers)")
        note("PASS" if order == want else "FAIL", f"kept: in order: {' '.join(order)}")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def inv_dump(a, tag):
    """Player 2's inventory on the host (CoopHero.Capture: every item type and
    its count, plus the total). Returns ({record: count}, total) or None."""
    n = a.count(r"\[lua\] INVDUMP ")
    a.send("lua local h = GetPlayerHenchman() if h and h:IsAlive() and CoopHero then "
           "print('INVDUMP ' .. CoopHero.SerializeV2(CoopHero.Capture(h), 0)) else print('INVDUMP none') end")
    if not a.wait(f"{tag}: player 2's inventory dumped", r"\[lua\] INVDUMP ", n, 20):
        return None
    line = [l for l in a.lines() if "[lua] INVDUMP " in l][-1]
    m = re.search(r"\|items=([^|]*)", line)
    if not m:
        note("FAIL", f"{tag}: inventory dump without items ({line[-120:]})")
        return None
    items = {k: int(v) for k, v in (kv.split(":") for kv in m.group(1).split(",") if ":" in kv)}
    t = re.search(r"\|itemtotal=(\d+)", line)
    total = int(t.group(1)) if t else sum(items.values())
    note("INFO", f"{tag}: player 2 holds {len(items)} item types, {total} items")
    look = "|".join(f.group(0) for f in re.finditer(r"\|(g|worn|weap|hgt|age)=[^|]*", line))
    return items, total, look


def run_continue2():
    """Continuing a co-op save must not duplicate player 2's kit (feature 002's
    finding: CoopHero.Apply added every worn item and weapon again on each
    continue, 12 -> 20 items). Join, dump player 2's inventory; then twice: the
    partner leaves (the host saves and returns to the title), the host continues
    the co-op save through the hub, the partner rejoins, dump again."""
    setup_tomls()
    relay_log = open(os.path.join(OUT, "relay_continue2.log"), "w")
    relay = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                           "coop_relay.py")], stdout=relay_log, stderr=relay_log)
    a, b = Game("mod-a", "host"), Game("mod-b", "partner")
    a.start({"FABLE2_TEST_INPUT": "1"})
    b.start({"FABLE2_TEST_INPUT": "1"})
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        menu_then_coop(a, "c2", a.n_menu)
        host_new_campaign(a, "c2")
        menu_then_coop(b, "c2", b.n_menu)
        wait_row(b, "Join TestA", "c2: the host's lobby is listed")
        partner_joins(b, "Join TestA", "c2")
        wait_row(a, "Start", "c2: host lobby shows the partner")
        if not both_ready_and_start(a, b, "c2") or not join_phase(a, b, "c2"):
            return
        time.sleep(10)
        first = inv_dump(a, "c2_join")
        b.shot("c2_join_partner_look.png")
        v = read_purses(a, b, "c2_join_purse")
        for k in (1, 2):
            tag = f"c2_continue{k}"
            # The partner leaves: the host saves (player 2 into the campaign) and
            # returns to the title.
            b.stop()
            n = a.count(r"host: back at the front end")
            if not a.wait(f"{tag}: host saves and returns to the front end", r"host: back at the front end", n, 240):
                return
            n_menu = a.count(r"main menu: Co-op entry added")
            time.sleep(10)
            a.past_title()
            menu_then_coop(a, tag, n_menu)
            wait_row(a, "Host: continue a co-op save", f"{tag}: the hub offers the co-op saves")
            a.select("Host: continue a co-op save")
            n = a.count(r"\[lobby\] open saves")
            a.press("E")
            a.wait(f"{tag}: the hub lists the co-op saves", r"\[lobby\] open saves", n, 10)
            n = a.count(r"\[lobby\] open host kind=continue")
            a.press("E")  # the newest campaign: the one just played
            a.wait(f"{tag}: the save's lobby opens", r"\[lobby\] open host kind=continue", n, 10)
            b = Game("mod-b", "partner")
            b.start({"FABLE2_TEST_INPUT": "1"})
            n = b.count(r"main menu: Co-op entry added")
            b.past_title()
            menu_then_coop(b, tag, n)
            wait_row(b, "Continue TestA", f"{tag}: the campaign is listed for the partner")
            partner_joins(b, "Continue TestA", tag)
            wait_row(a, "Start", f"{tag}: host lobby shows the partner")
            if not both_ready_and_start(a, b, tag) or not join_phase(a, b, tag):
                return
            time.sleep(10)
            now = inv_dump(a, tag)
            b.shot(f"{tag}_partner_look.png")
            if first and now:
                grown = {r: (first[0].get(r, 0), c) for r, c in now[0].items() if c != first[0].get(r, 0)}
                lost = {r: (c, 0) for r, c in first[0].items() if r not in now[0]}
                note("PASS" if not grown and not lost and now[1] == first[1] else "FAIL",
                     f"{tag}: player 2's items unchanged ({first[1]} -> {now[1]}; changed: {grown or 'none'}"
                     f"{'; missing: ' + str(lost) if lost else ''})")
            v = read_purses(a, b, f"{tag}_purse")
            note("PASS" if v and v[0] == v[1] == v[2] else "FAIL", f"{tag}: purses equal ({v})")
    finally:
        for g in (a, b):
            g.stop()
        relay.kill()


def main():
    os.makedirs(OUT, exist_ok=True)
    mode = sys.argv[1] if len(sys.argv) > 1 else "coop"
    {"coop": run_coop, "solo": run_solo, "multi": run_multi, "menu": run_menu, "menu2": run_menu2, "err": run_err, "final": run_final, "last": run_last, "start": run_start, "purse": run_purse, "fps": run_fps, "combo": run_combo, "camera": run_camera, "luaq": run_luaq, "luaq_kept": run_luaq_kept, "continue2": run_continue2, "boots": run_boots, "worldgold": run_worldgold, "batch1": run_batch1, "savestress": run_savestress, "solowalk": run_solowalk, "dynsync": run_dynsync, "livefix": run_livefix, "perfab": run_perfab}[mode]()
    with open(os.path.join(OUT, f"results_{mode}.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(results) + "\n")
    return 0 if not any(r.startswith("FAIL") for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
