"""coop_kit_input.py - routes the 002 launch scripts' presses through the game's
test input source (FABLE2_TEST_INPUT=1: <copy>\\test_input.txt, read and emptied
by the game every 50 ms and fed through its own input filter), so presses no
longer depend on which window is in front. Testing only; play-coop.cmd never
sets the variable.

  import coop_kit_input; coop_kit_input.install()   # before Game.start()

Each press goes to the copy whose window was last focused (e2e.focus) or, for
Game.press, to that copy. A press is written only once the file is empty (the
game has taken the previous one); a file the game never empties (an older
build) falls back to the keyboard for the rest of the run.
"""

import os
import time

import coop_ship_e2e as e2e

PAD = {"E": "A", "B": "B", "X": "X", "Escape": "Start", "Up": "Up", "Down": "Down",
       "Left": "Left", "Right": "Right"}
_games = {}  # hwnd -> folder
_target = [None]
_fallback = [False]
_orig_key, _orig_focus, _orig_start, _orig_press = e2e.key, e2e.focus, e2e.Game.start, e2e.Game.press
_orig_shot = e2e.Game.shot


def _send(folder, name, hold_ms):
    if _fallback[0] or not folder or name not in PAD:
        return False
    path = os.path.join(folder, "test_input.txt")
    t = time.time()
    while os.path.exists(path) and os.path.getsize(path) > 0:
        if time.time() - t > 10:  # long enough for a loading screen
            open(path, "w").close()  # drop the stale press so it cannot fire later on top of the keyboard's
            e2e.note("INFO", f"test input: {path} not consumed in 10 s; keyboard from here on")
            _fallback[0] = True
            return False
        time.sleep(0.05)
    with open(path, "a", encoding="ascii") as f:
        f.write(f"press {PAD[name]} {max(60, hold_ms)}\n")
    time.sleep((max(60, hold_ms) + 150) / 1000.0)
    return True


def _key(name, hold_ms=60):
    if not _send(_target[0], name, hold_ms):
        _orig_key(name, hold_ms)


def _focus(hwnd):
    # Still brought to the front: screenshots and the typed console need it.
    _target[0] = _games.get(hwnd, _target[0])
    _orig_focus(hwnd)


def _start(self):
    _orig_start(self)
    _games[self.hwnd] = self.dir


def _shot(self, name):
    # A screenshot focuses its window but must not move the press target
    # (L2 of 15:18Z: after a.shot() the partner's B presses went to the host).
    keep = _target[0]
    try:
        return _orig_shot(self, name)
    finally:
        _target[0] = keep


def _press(self, name, *args):
    _target[0] = self.dir
    if not _send(self.dir, name, 60):
        _orig_press(self, name, *args)


def install():
    os.environ["FABLE2_TEST_INPUT"] = "1"
    e2e.key, e2e.focus = _key, _focus
    e2e.Game.start, e2e.Game.press, e2e.Game.shot = _start, _press, _shot
