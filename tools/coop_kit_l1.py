#!/usr/bin/env python3
"""coop_kit_l1.py - feature 002 launch 1: the proofs for player 2's inventory
through the pause menu (research/002-evidence/inventory-plan.md, U1-U5, P1, P-EQ).

Ship build in the 002 copies D:\\Fable2-LAN\\wt002-a (host) and wt002-b (partner),
made by tools\\make_002_instances.ps1; never mod-a/mod-b. Driven like
coop_ship_e2e.py (OS keyboard input, desktop screenshots), one network.

  python tools/coop_kit_l1.py
Evidence: research/002-evidence/l1/ (screenshots, results_l1.txt, copies of both logs).
"""

import os
import re
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_ship_e2e as e2e  # noqa: E402

e2e.OUT = os.path.join(e2e.ROOT, r"research\002-evidence\l1")
note, console, key, focus = e2e.note, e2e.console, e2e.key, e2e.focus

# Test items (record names from the game's own scripts).
POTION = "ObjectInventoryPotionHealth"
FOOD = "ObjectInventoryFoodAndDrinkCrunchyChick"
HAT = "ObjectClothingHatMascotM"
KEY_ITEM = "ObjectInventorySilverKey"


def last(g, pat):
    hits = [l for l in g.lines() if re.search(pat, l)]
    return hits[-1] if hits else None


def lua(g, chunk, tag, what, timeout=15):
    """Runs `lua <chunk>` that prints a line starting with tag; returns that line."""
    n = g.count(re.escape(tag))
    console(g, "lua " + chunk)
    if g.wait(what, re.escape(tag), n, timeout):
        return last(g, re.escape(tag))
    return None


def snapshot(a, tag):
    n = a.count(r"COOPKIT:v1")
    console(a, "luafile coop_kit")
    if not a.wait(f"{tag}: host snapshot of player 2 (COOPKIT)", r"COOPKIT:v1", n, 20):
        return None
    line = last(a, r"COOPKIT:v1")
    rec = line[line.index("COOPKIT:v1"):]
    note("INFO", f"{tag}: kit_state {len(rec)} bytes: {rec[:600]}")
    items = {}
    m = re.search(r"\|items=([^|]*)", rec)
    for part in (m.group(1).split(",") if m and m.group(1) else []):
        p = part.split(":")
        if len(p) >= 4:
            items[p[0]] = (int(p[1]), p[2], p[3])
    return rec, items


def probe(b, tag):
    n = b.count(r"\[coopkit\] probe: \d+ stacks")
    console(b, "coop_kit_probe")
    if not b.wait(f"{tag}: partner probe of its player-2 copy", r"\[coopkit\] probe: \d+ stacks", n, 15):
        return None
    lines = b.lines()
    start = max(i for i, l in enumerate(lines) if re.search(r"\[coopkit\] probe \(console\)", l))
    out = {}
    for l in lines[start:]:
        m = re.search(r"\[coopkit\]\s+stack 0x[0-9A-F]+ rec ([0-9A-F]{8}) x(\d+)", l)
        if m:
            out[m.group(1)] = out.get(m.group(1), 0) + int(m.group(2))
    for l in lines[start:start + 4]:
        note("INFO", f"{tag}: {l.split('] ', 1)[-1][:300]}")
    return out


def hex_of(items, cat_prefix):
    return next((h for h, v in items.items() if v[1].startswith(cat_prefix)), None)


def pause_wheel(b, tag):
    n = b.count(r"\[coopkit\] pause menu: full wheel")
    focus(b.hwnd)
    key("Escape")
    ok = b.wait(f"{tag}: partner's pause menu asks for the full wheel", r"\[coopkit\] pause menu: full wheel", n, 10)
    time.sleep(2.0)
    b.shot(f"{tag}_partner_wheel.png")
    # Walk the wheel: each Right moves to the next page; A on each, screenshot, B back.
    for i in range(8):
        key("Right" if "Right" in e2e.SCAN else "Down")
        time.sleep(0.8)
        b.shot(f"{tag}_partner_wheel_{i}.png")
    key("Escape")
    time.sleep(1.5)
    return ok


def run():
    os.makedirs(e2e.OUT, exist_ok=True)
    e2e.SCAN.setdefault("Right", (0x4D, True))
    e2e.SCAN.setdefault("Left", (0x4B, True))
    a, b = e2e.Game("wt002-a", "host"), e2e.Game("wt002-b", "partner")
    a.start()
    b.start()
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        e2e.menu_then_coop(a, "l1", a.n_menu)
        e2e.host_new_campaign(a, "l1")
        e2e.menu_then_coop(b, "l1", b.n_menu)
        e2e.wait_row(b, "Join TestA", "l1: the host's lobby is listed")
        e2e.partner_joins(b, "Join TestA", "l1")
        e2e.wait_row(a, "Start", "l1: host lobby shows the partner")
        if not e2e.both_ready_and_start(a, b, "l1") or not e2e.join_phase(a, b, "l1"):
            return
        time.sleep(10)

        # --- U5 / P1: what player 2 holds on each side before any change ---------------------
        snap = snapshot(a, "l1a")
        have = probe(b, "l1a")
        if snap and have is not None:
            host_items = {h: v[0] for h, v in snap[1].items()}
            note("INFO", f"l1a: host lists {len(host_items)} records, partner copy {len(have)} records")
            same = {h for h in host_items if have.get(h) == host_items[h]}
            note("INFO", f"U5: records with the same count on both sides: {len(same)} of {len(host_items)}")

        # --- give player 2 test items on the host -----------------------------------------
        for name in (POTION, POTION, FOOD, HAT, KEY_ITEM):
            console(a, f"lua Inventory.AddItemOfType(GetPlayerHenchman(), '{name}')")
            time.sleep(0.5)
        time.sleep(3)
        snap = snapshot(a, "l1b")
        have = probe(b, "l1b") or {}
        if not snap:
            return
        items = snap[1]
        potion = hex_of(items, "ITEMS_POTIONS")
        hat = hex_of(items, "CLOTHING_HATS") or hex_of(items, "CLOTHING")
        keyitem = hex_of(items, "ITEMS_KEY")
        note("INFO", f"l1b: potion {potion}, hat {hat}, key item {keyitem}")
        if potion:
            note("INFO", f"U5: after the host's AddItemOfType, partner copy has potion x{have.get(potion, 0)} "
                         f"(host x{items[potion][0]})")
        a.shot("l1b_host_after_give.png")
        b.shot("l1b_partner_after_give.png")

        # --- U4: the full wheel on the partner ----------------------------------------------
        pause_wheel(b, "l1c")

        # --- U1/U2: native add and remove on the partner's copy ------------------------------
        if potion:
            before = have.get(potion, 0)
            n = b.count(r"\[coopkit\] add .*count")
            console(b, f"coop_kit_add {potion} 1")
            b.wait("U1: native add returned (no crash)", r"\[coopkit\] add .*count", n, 15)
            added = last(b, r"\[coopkit\] add .*count") or ""
            note("INFO", "U1: " + added.split("] ", 1)[-1])
            after = (probe(b, "l1d") or {}).get(potion, 0)
            note("PASS" if after == before + 1 else "FAIL", f"U1/U2: potion on the copy {before} -> {after} (want +1)")
            n = b.count(r"\[coopkit\] remove .*count")
            console(b, f"coop_kit_remove {potion} 1")
            b.wait("native remove returned", r"\[coopkit\] remove .*count", n, 15)
            back = (probe(b, "l1e") or {}).get(potion, 0)
            note("PASS" if back == before else "FAIL", f"U1: potion on the copy back to {back} (want {before})")
            b.shot("l1e_partner_after_native.png")
            pause_wheel(b, "l1f")  # the Items page after the native calls

        # --- P-EQ: the host applies commands to player 2 -----------------------------------
        hp = lua(a, "print('KITHP '..Health.Get(GetPlayerHenchman())..' '..Health.GetMax(GetPlayerHenchman()))",
                 "KITHP", "host reads player 2's health")
        console(a, "lua Health.Modify(GetPlayerHenchman(), -30, GetPlayerHero())")
        time.sleep(1)
        for op, h in (("use", potion), ("wear", hat), ("use", keyitem), ("unwear", hat), ("wear", hat)):
            if not h:
                continue
            r = lua(a, f"print('KITRES {op} {h} '..CoopKit.Apply(GetPlayerHenchman(), '{op}', '{h}'))",
                    f"KITRES {op} {h}", f"P-EQ: host applies {op} {h}")
            note("INFO", f"P-EQ: {(r or '').split('KITRES ', 1)[-1]}")
            time.sleep(2)
            a.shot(f"l1g_host_{op}_{h}.png")
            b.shot(f"l1g_partner_{op}_{h}.png")
        hp2 = lua(a, "print('KITHP '..Health.Get(GetPlayerHenchman())..' '..Health.GetMax(GetPlayerHenchman()))",
                  "KITHP", "host reads player 2's health again")
        note("INFO", f"P-EQ: health {hp and hp.split('KITHP ')[1]} -> (hurt 30, potion) {hp2 and hp2.split('KITHP ')[1]}")
        # --- P-CH: a command over the channel (partner console -> host apply -> ack) ---------
        if potion:
            na, nb = a.count(r"\[coopkit\] host: command \d+ ->"), b.count(r"\[coopkit\] command \d+: ")
            console(b, f"coop_kit_send use {potion}")
            a.wait("P-CH: host applied the partner's command", r"\[coopkit\] host: command \d+ ->", na, 40)
            b.wait("P-CH: partner got the ack", r"\[coopkit\] command \d+: ", nb, 40)
            note("INFO", "P-CH: host " + (last(a, r"\[coopkit\] host: command \d+ ->") or "").split("] ", 1)[-1])
            note("INFO", "P-CH: partner " + (last(b, r"\[coopkit\] command \d+: ") or "").split("] ", 1)[-1])

        # --- mirror: the partner's copy follows kit_state ------------------------------------
        console(b, "coop_kit_mirror true")
        time.sleep(8)
        snap = snapshot(a, "l1h")
        time.sleep(5)
        have = probe(b, "l1h") or {}
        if snap:
            want = {h: v[0] for h, v in snap[1].items() if not v[1].startswith("CLOTHING")}
            got = {h: n for h, n in have.items() if h in want or h not in snap[1]}
            note("PASS" if got == want else "FAIL", f"mirror: copy {got} vs host {want}")
        pause_wheel(b, "l1i")  # the Items page with the mirrored inventory
    finally:
        for g in (a, b):
            g.stop()
        for g in (a, b):
            if g.log:
                shutil.copy(g.log, os.path.join(e2e.OUT, f"{g.who}.log"))


def page_names(b, n0):
    return [re.search(r"name '([^']*)'", l).group(1) for l in b.lines()[n0:] if "[coopkit] pause page event" in l]


def run_wheel():
    """L1b: the full wheel on the partner, its page names, and the Items,
    Weapons and Clothing pages with the mirrored inventory."""
    os.makedirs(e2e.OUT, exist_ok=True)
    e2e.SCAN.setdefault("Right", (0x4D, True))
    e2e.SCAN.setdefault("Left", (0x4B, True))
    a, b = e2e.Game("wt002-a", "host"), e2e.Game("wt002-b", "partner")
    a.start()
    b.start()
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        e2e.menu_then_coop(a, "w", a.n_menu)
        e2e.host_new_campaign(a, "w")
        e2e.menu_then_coop(b, "w", b.n_menu)
        e2e.wait_row(b, "Join TestA", "w: the host's lobby is listed")
        e2e.partner_joins(b, "Join TestA", "w")
        e2e.wait_row(a, "Start", "w: host lobby shows the partner")
        if not e2e.both_ready_and_start(a, b, "w") or not e2e.join_phase(a, b, "w"):
            return
        time.sleep(8)
        for name in (POTION, POTION, FOOD, HAT, KEY_ITEM):
            console(a, f"lua Inventory.AddItemOfType(GetPlayerHenchman(), '{name}')")
            time.sleep(0.5)
        console(b, "coop_kit_mirror true")
        n = b.count(r"\[coopkit\] mirror rev")
        b.wait("w: the mirror corrected the partner's copy", r"\[coopkit\] mirror rev", n, 20)
        time.sleep(3)
        probe(b, "w0")
        # The wheel: open, walk down every row (each highlight logs its page name).
        nl = len(b.lines())
        focus(b.hwnd)
        key("Escape")
        b.wait("w: partner's pause menu builds the full wheel", r"\[coopkit\] pause menu: full wheel", 0, 10)
        time.sleep(2)
        b.shot("w1_partner_wheel.png")
        for i in range(10):
            key("Down")
            time.sleep(0.9)
            b.shot(f"w1_partner_wheel_row{i}.png")
        names = page_names(b, nl)
        note("INFO", f"w1: page events on the partner: {names}")
        key("Escape")
        time.sleep(2)
        # Open each item page by walking to it. L1b: highlights only log
        # MenuScrollUp/Down, so rows are counted. The wheel does not wrap; Up x10
        # stops on Save (row 0). Rows: Save, Quests/Maps, Abilities, Items,
        # Weapons, Clothing, Logbook, Options, Kick.
        for target, row in (("Items", 3), ("Weapons", 4), ("Clothing", 5)):
            focus(b.hwnd)
            key("Escape")
            time.sleep(2)
            for _ in range(10):
                key("Up")
                time.sleep(0.3)
            for _ in range(row):
                key("Down")
                time.sleep(0.6)
            b.shot(f"w2_partner_wheel_on_{target.lower()}.png")
            key("E")
            time.sleep(3)
            b.shot(f"w2_partner_{target.lower()}.png")
            key("Down")
            time.sleep(1)
            b.shot(f"w2_partner_{target.lower()}_2.png")
            note("PASS", f"w2: partner opened the {target} page (screenshot w2_partner_{target.lower()}.png)")
            key("B")
            time.sleep(1.5)
            key("Escape")
            time.sleep(2)
        a.shot("w3_host_end.png")
        b.shot("w3_partner_end.png")
    finally:
        for g in (a, b):
            g.stop()
        for g in (a, b):
            if g.log:
                shutil.copy(g.log, os.path.join(e2e.OUT, f"{g.who}.log"))


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "wheel":
        e2e.OUT = os.path.join(e2e.ROOT, "research", "002-evidence", "l1b")
        run_wheel()
        name = "results_l1b.txt"
    else:
        run()
        name = "results_l1.txt"
    with open(os.path.join(e2e.OUT, name), "w", encoding="utf-8") as f:
        f.write(chr(10).join(e2e.results) + chr(10))
    return 0 if not any(r.startswith("FAIL") for r in e2e.results) else 1


if __name__ == "__main__":
    sys.exit(main())
