#!/usr/bin/env python3
"""coop_kit_dryrun.py - runs the 002 launch scripts' step sequence against a
simulated pair of games, offline, so a script-level mistake does not cost a
launch (2026-10-25: several runs were lost to the harness).

  python tools/coop_kit_dryrun.py l23    (or l2, l3, l4)

The simulation models what the launches showed: the partner's full wheel
(Save, Quests/Maps, Abilities, Items, Weapons, Clothing, Logbook, Options, Kick;
no wrap; opens on Quests/Maps), its pages and lists, B back one level, Start
closing the menu; any partner page pauses the host (stock); commands are
applied by the host when it is not paused; the host's console (snapshots, lua)
does not answer while it is paused; typing into the partner's console while its
menu is open does nothing. It is a model, not the game: it catches order and
count mistakes in the scripts, not game behaviour.
"""

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_ship_e2e as e2e  # noqa: E402
import coop_kit_l2 as l2  # noqa: E402
import coop_kit_l1 as l1  # noqa: E402
import coop_kit_l34 as l34  # noqa: E402

WHEEL = ["Save", "Map", "Abilities", "Items", "Weapons", "Clothing", "Logbook", "Options", "Kick"]
LISTS = {"Items": ["Miscellaneous", "Books", "Food", "Potions"], "Weapons": ["Melee", "Ranged"],
         "Clothing": ["Outfits", "Headgear", "Coats"]}
POTION, HAT, KEY, MELEE0, SWORD2 = "68DC87BD", "C0F0F31B", "4AB4C31A", "98F97077", "0D43EAE7"


class World:
    def __init__(self):
        self.menu = []  # partner: [] closed, ["wheel", row], ["list", page, row], ["entries", page, cat, row], ["page", name]
        self.paused = False
        self.queue = []  # commands waiting for the host
        self.seq = 0
        self.items = {POTION: [2, "ITEMS_POTIONS"], HAT: [1, "CLOTHING_HATS"], KEY: [1, "QUEST_ITEM"],
                      SWORD2: [1, "WEAPONS"], "E150911F": [1, "WEAPONS"]}
        self.melee, self.worn, self.coop_kit = MELEE0, ["BD511D47"], True
        self.target = None

    def entries(self, page, cat):
        if page == "Weapons" and cat == "Melee":
            return [("equip", MELEE0), ("equip", "E150911F"), ("equip", SWORD2)]
        if page == "Clothing" and cat == "Headgear":
            return [(None, None), ("wear", HAT)]
        if page == "Items" and cat == "Potions":
            return [("use", POTION)] if self.items[POTION][0] else []
        if page == "Items" and cat == "Miscellaneous":
            return [(None, KEY)]  # the stock screen offers no action on a quest item
        return []


W = World()


class FakeGame:
    def __init__(self, folder, who):
        self.who, self.dir, self.hwnd, self.log, self.log_lines = who, folder, 1 if who == "host" else 2, "x", []
        GAMES[who] = self

    def start(self): pass
    def stop(self): pass
    def lines(self): return list(self.log_lines)
    def count(self, pat): return sum(1 for l in self.log_lines if re.search(pat, l))

    def wait(self, what, pat, n0, timeout=20):
        tick()
        ok = self.count(pat) > n0
        e2e.note("PASS" if ok else "FAIL", f"{self.who}: {what}" + ("" if ok else f" (no '{pat}')"))
        return ok

    def shot(self, name): return name
    def press(self, name): key(name)
    def rows(self): return []
    def say(self, text): self.log_lines.append(f"[info] [sys] [coopkit] {text}")


GAMES = {}


def host(): return GAMES["host"]
def partner(): return GAMES["partner"]


def set_pause(p):
    if p and not W.paused:
        host().say("host: game paused (Lua waiting); commands wait for the unpause")
    if not p and W.paused:
        host().say("host: game resumed after 3 s")
    W.paused = p
    tick()


def tick():
    """The host applies waiting commands while it is not paused; the partner gets the acks."""
    while W.queue and not W.paused:
        seq, op, rec = W.queue.pop(0)
        res = "ok"
        if op == "use":
            if W.items[rec][1].startswith("QUEST"):
                res = "no:questitem"
            else:
                W.items[rec][0] -= 1
        elif op == "equip":
            W.items[rec][0] -= 1
            W.items.setdefault(W.melee, [0, "WEAPONS"])[0] += 1
            W.melee = rec
        elif op == "wear":
            W.worn.append(rec)
        elif op == "unwear":
            W.worn.remove(rec)
        partner().say(f"command {seq}: {res} (Done)")


def key(name, hold_ms=60):
    if W.target != "partner":
        return  # the host's own presses are not modelled
    m = W.menu
    if not m:
        if name == "Escape":
            W.menu = ["wheel", 1]
            partner().say("pause menu: full wheel for player 2 (builder)")
        return
    kind = m[0]
    if name == "Escape":  # Start closes the whole menu
        W.menu = []
        set_pause(False)
        return
    if kind == "wheel":
        if name in ("Up", "Down"):
            m[1] = max(0, min(len(WHEEL) - 1, m[1] + (1 if name == "Down" else -1)))
        elif name == "B":
            W.menu = []
        elif name == "E":
            page = WHEEL[m[1]]
            route = {"Abilities": "henchman abilities", "Kick": "ignored", "Save": "henchman quit"}.get(page, "as is")
            partner().say(f"pause page '{page}' chosen: {route}")
            if page == "Kick":
                return
            W.menu = ["list", page, 0] if page in LISTS else ["page", page]
            set_pause(True)
    elif kind == "list":
        if name in ("Up", "Down"):
            m[2] = max(0, min(len(LISTS[m[1]]) - 1, m[2] + (1 if name == "Down" else -1)))
        elif name == "B":
            W.menu = ["wheel", WHEEL.index(m[1])]
            set_pause(False)
        elif name == "E":
            W.menu = ["entries", m[1], LISTS[m[1]][m[2]], 0]
    elif kind == "entries":
        ents = W.entries(m[1], m[2])
        if name in ("Up", "Down"):
            m[3] = max(0, min(max(0, len(ents) - 1), m[3] + (1 if name == "Down" else -1)))
        elif name == "B":
            W.menu = ["list", m[1], LISTS[m[1]].index(m[2])]
        elif name == "E" and ents and W.coop_kit:
            op, rec = ents[m[3]]
            if op == "wear" and rec in W.worn:
                op = "unwear"
            if op:
                W.seq += 1
                partner().say(f"partner: command {W.seq} {op} {rec}")
                W.queue.append((W.seq, op, rec))
    elif kind == "page":
        if name == "B":
            W.menu = ["wheel", WHEEL.index(m[1]) if m[1] in WHEEL else 1]
            set_pause(False)


def focus(hwnd):
    W.target = "host" if hwnd == 1 else "partner"


def console(g, command):
    if g.who == "partner" and W.menu:
        e2e.note("FAIL", f"dryrun: console '{command}' typed into the partner's open menu (ignored by the game)")
        return
    if g.who == "partner" and command.startswith("coop_kit "):
        W.coop_kit = command.endswith("true")


def snapshot(a, tag):
    if W.paused:
        e2e.note("FAIL", f"host: {tag}: host snapshot of player 2 (COOPKIT) (host paused: no answer)")
        return None
    items = {h: (v[0], v[1], "") for h, v in W.items.items() if v[0] > 0}
    rec = f"COOPKIT:v1|rev=1|melee={W.melee}|ranged=|worn={','.join(W.worn)}|items="
    return rec, items


def lua(g, chunk, tag, what, timeout=15):
    if W.paused:
        e2e.note("FAIL", f"host: {what} (host paused: no answer)")
        return None
    if "GetNumberOfItems" in chunk:
        return f"KITN {sum(v[0] for v in W.items.values())}"
    return f"{tag} 100"


def probe(b, tag):
    return {h: v[0] for h, v in W.items.items() if v[0] > 0} | {W.melee: 1}


def last(g, pat):
    hits = [l for l in g.lines() if re.search(pat, l)]
    return hits[-1] if hits else None


def install():
    e2e.Game = FakeGame
    e2e.key = key
    e2e.focus = focus
    e2e.console = console
    time.sleep = lambda s: None
    for m in (l1, l2, l34):
        for name, fn in (("key", key), ("focus", focus), ("console", console), ("snapshot", snapshot),
                         ("lua", lua), ("probe", probe), ("last", last)):
            if hasattr(m, name):
                setattr(m, name, fn)
    l2.past_title = lambda g: True
    for name in ("menu_then_coop", "host_new_campaign", "wait_row", "partner_joins"):
        setattr(e2e, name, lambda *a, **k: True)
    e2e.both_ready_and_start = lambda *a, **k: True
    e2e.join_phase = lambda *a, **k: True
    l34.start_and_join = lambda *a, **k: True
    l34.moving = lambda g, tag: not W.paused
    l34.tree_hashes = lambda root: {}
    l34.out_dir = lambda name: None
    l34.stop_and_keep = lambda *a, **k: None
    l34.host_save = lambda a, tag: True
    l34.p2file_total = lambda slot: (sum(v[0] for v in W.items.values()), "")
    os.makedirs = lambda *a, **k: None
    l2.shutil.copy = lambda *a, **k: None
    e2e.OUT = os.path.join(os.environ.get("TEMP", "."), "coop_kit_dryrun")
    os.environ["FABLE2_DRYRUN"] = "1"


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "l23"
    install()
    import builtins
    real_open = builtins.open

    def fake_open(p, *a, **k):  # results.txt and copies go nowhere
        if "002-evidence" in str(p) and ("w" in (a[0] if a else k.get("mode", "r"))):
            return real_open(os.devnull, *a, **k)
        return real_open(p, *a, **k)

    builtins.open = fake_open
    {"l2": lambda: l2.run(), "l23": l34.run_l23, "l3": l34.run_l3, "l4": l34.run_l4}[mode]()
    fails = [r for r in e2e.results if r.startswith("FAIL")]
    print(f"\n=== dry run {mode}: {len(fails)} FAIL(s); partner menu at the end: {W.menu or 'closed'}; "
          f"host paused: {W.paused}")
    for f in fails:
        print("  " + f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
