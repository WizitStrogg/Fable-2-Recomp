#!/usr/bin/env python3
"""coop_kit_l2.py - feature 002 launch 2: player 2's inventory through the
partner's own pause menu, end to end, with real button presses (no console
commands for the actions themselves).

  python tools/coop_kit_l2.py
Evidence: research/002-evidence/l2/ (screenshots, results, copies of both logs).

On the partner's stock pages: drink a potion (Items > Potions), wear the hat
(Clothing > Headgear), swap to a second sword (Weapons > Melee), try the silver
key (Items > Miscellaneous; the host must refuse it), Abilities (henchman
abilities) and Kick (ignored). Each result is checked on the host (snapshot of
the henchman) and on the partner (ack line, copy probe, screenshots).
The Save row is never touched. Same copies as launch 1: wt002-a (host) and
wt002-b (partner), never mod-a/mod-b.
"""

import os
import re
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_ship_e2e as e2e  # noqa: E402
import coop_kit_l1 as l1  # noqa: E402

e2e.OUT = os.path.join(e2e.ROOT, r"research\002-evidence\l2")
note, console, focus = e2e.note, e2e.console, e2e.focus
last, lua, snapshot, probe, hex_of = l1.last, l1.lua, l1.snapshot, l1.probe, l1.hex_of

# Second sword candidates (record names from the game's scripts); the first one
# the host's snapshot lists as a weapon is used. "#<hex>" = a record by id.
SWORDS = ("ObjectWeaponSpecialBarbarian", "ObjectWeaponRoyalSceptre", "ObjectWeaponSpecialGood",
          "ObjectInventoryWeaponSword")

# Wheel rows (L1b): the wheel does not wrap and opens on Quests/Maps.
ROW = {"Save": 0, "Map": 1, "Abilities": 2, "Items": 3, "Weapons": 4, "Clothing": 5,
       "Logbook": 6, "Options": 7, "Kick": 8}
# Sub-page rows (L1b screenshots).
ITEMS_ROW = {"Miscellaneous": 0, "Books": 1, "Food": 2, "Potions": 3}
CLOTHING_ROW = {"Outfits": 0, "Headgear": 1}
WEAPONS_ROW = {"Melee": 0, "Ranged": 1}


def key(name, hold_ms=60):
    # 60 ms presses: longer holds can start the menus' held-direction repeat.
    e2e.key(name, hold_ms)


def down(n, pause=0.5):
    for _ in range(n):
        key("Down")
        time.sleep(pause)


def open_wheel(b, tag):
    focus(b.hwnd)
    key("Escape")
    time.sleep(2)
    for _ in range(10):  # to Save (row 0); the wheel clamps, never wraps
        key("Up")
        time.sleep(0.25)
    b.shot(f"{tag}_wheel.png")


def close_menus(b, backs):
    for _ in range(backs):
        key("B")
        time.sleep(1.0)
    key("Escape")
    time.sleep(2)


def open_page(b, tag, page, sub_row):
    """Wheel -> page -> sub-page list, cursor on the first entry."""
    open_wheel(b, tag)
    down(ROW[page])
    key("E")
    time.sleep(2.5)
    down(sub_row)
    key("E")
    time.sleep(2.5)
    b.shot(f"{tag}_list.png")


def press_a_on_entry(a, b, tag, row, want_op=None):
    """A on the list entry `row`; returns (command line, ack line) from the partner's log."""
    nc = b.count(r"\[coopkit\] partner: command \d+ ")
    na = b.count(r"\[coopkit\] command \d+: ")
    down(row)
    b.shot(f"{tag}_on_entry.png")
    key("E")
    sent = b.wait(f"{tag}: partner's A sent a command", r"\[coopkit\] partner: command \d+ ", nc, 8)
    cmd = last(b, r"\[coopkit\] partner: command \d+ ") if sent else None
    if cmd and want_op and f" {want_op} " not in cmd:
        note("FAIL", f"{tag}: sent {cmd.split('] ', 1)[-1]} (want {want_op})")
    ack = None
    if sent and b.wait(f"{tag}: partner got the host's answer", r"\[coopkit\] command \d+: ", na, 40):
        ack = last(b, r"\[coopkit\] command \d+: ")
        note("INFO", f"{tag}: {cmd.split('] ', 1)[-1]} -> {ack.split('] ', 1)[-1]}")
    time.sleep(2)
    a.shot(f"{tag}_host.png")
    b.shot(f"{tag}_partner.png")
    return cmd, ack


def rec_of(cmd):
    m = re.search(r"command \d+ \w+ ([0-9A-F]{8})", cmd or "")
    return m.group(1) if m else None


def field(snap, name):
    m = re.search(rf"\|{name}=([^|]*)", snap[0] if snap else "")
    return m.group(1) if m else ""


def run():
    os.makedirs(e2e.OUT, exist_ok=True)
    a, b = e2e.Game("wt002-a", "host"), e2e.Game("wt002-b", "partner")
    a.start()
    b.start()
    try:
        for g in (a, b):
            g.n_menu = g.count(r"main menu: Co-op entry added")
            g.past_title()
        e2e.menu_then_coop(a, "l2", a.n_menu)
        e2e.host_new_campaign(a, "l2")
        e2e.menu_then_coop(b, "l2", b.n_menu)
        e2e.wait_row(b, "Join TestA", "l2: the host's lobby is listed")
        e2e.partner_joins(b, "Join TestA", "l2")
        e2e.wait_row(a, "Start", "l2: host lobby shows the partner")
        if not e2e.both_ready_and_start(a, b, "l2") or not e2e.join_phase(a, b, "l2"):
            return
        time.sleep(8)

        # --- give player 2 the test kit on the host ------------------------------------------
        for name in (l1.POTION, l1.POTION, l1.FOOD, l1.HAT, l1.KEY_ITEM) + SWORDS:
            console(a, f"lua Inventory.AddItemOfType(GetPlayerHenchman(), '{name}')")
            time.sleep(0.5)
        console(a, "lua Health.Modify(GetPlayerHenchman(), -30, GetPlayerHero())")
        console(b, "coop_kit_mirror true")
        time.sleep(6)
        snap = snapshot(a, "l2a")
        if not snap:
            return
        items = snap[1]
        potion = hex_of(items, "ITEMS_POTIONS")
        hat = hex_of(items, "CLOTHING_HATS")
        keyitem = hex_of(items, "QUEST") or hex_of(items, "ITEMS_KEY")
        melee0 = field(snap, "melee")
        sword2 = next((h for h, v in items.items() if v[1].startswith("WEAPONS") and h != melee0), None)
        note("INFO", f"l2a: potion {potion}, hat {hat}, key {keyitem}, melee {melee0}, second sword {sword2}")
        note("PASS" if sword2 else "FAIL", f"l2a: player 2 holds a second weapon ({sword2})")
        hp0 = lua(a, "print('KITHP '..Health.Get(GetPlayerHenchman()))", "KITHP", "l2a: host reads player 2's health")
        time.sleep(4)
        have = probe(b, "l2a") or {}
        for what, h in (("potion", potion), ("hat", hat), ("equipped sword", melee0), ("second sword", sword2)):
            if h:
                note("PASS" if have.get(h, 0) >= 1 else "FAIL", f"l2a: the partner's copy holds the {what} {h}")

        # --- Weapons > Melee lists both swords; A swaps to the second -------------------------
        open_page(b, "l2w", "Weapons", WEAPONS_ROW["Melee"])
        b.shot("l2w_melee_list.png")
        cmd, ack = press_a_on_entry(a, b, "l2w0", 0, "equip")
        if rec_of(cmd) == melee0:  # the first entry was the sword already in hand: take the next
            cmd, ack = press_a_on_entry(a, b, "l2w1", 1, "equip")
        close_menus(b, 2)
        snap = snapshot(a, "l2w")
        melee1 = field(snap, "melee")
        note("PASS" if ack and ": ok" in ack and melee1 == rec_of(cmd) and melee1 != melee0 else "FAIL",
             f"l2w: swap from the stock Weapons page: melee {melee0} -> {melee1} (sent {rec_of(cmd)})")
        a.shot("l2w_host_after.png")
        b.shot("l2w_partner_after.png")

        # --- Clothing > Headgear lists the hat; A wears it -----------------------------------
        open_page(b, "l2h", "Clothing", CLOTHING_ROW["Headgear"])
        note("INFO", "l2h: Headgear list screenshot l2h_list.png (the hat must be listed)")
        cmd, ack = press_a_on_entry(a, b, "l2h", 0, "wear")
        close_menus(b, 2)
        snap = snapshot(a, "l2h")
        note("PASS" if hat and hat in field(snap, "worn").split(",") else "FAIL",
             f"l2h: the host's player 2 wears the hat {hat} (worn={field(snap, 'worn')})")
        a.shot("l2h_host_after.png")
        b.shot("l2h_partner_after.png")

        # --- Items > Potions; A drinks one ----------------------------------------------------
        before = (snap[1].get(potion) or (0,))[0] if snap and potion else 0
        open_page(b, "l2p", "Items", ITEMS_ROW["Potions"])
        cmd, ack = press_a_on_entry(a, b, "l2p", 0, "use")
        close_menus(b, 2)
        snap = snapshot(a, "l2p")
        after = (snap[1].get(potion) or (0,))[0] if snap and potion else 0
        hp1 = lua(a, "print('KITHP '..Health.Get(GetPlayerHenchman()))", "KITHP", "l2p: host reads player 2's health")
        note("PASS" if ack and ": ok" in ack and after == before - 1 else "FAIL",
             f"l2p: potion drunk from the stock Items page: {before} -> {after} on the host, "
             f"health {hp0 and hp0.split('KITHP ')[1]} -> {hp1 and hp1.split('KITHP ')[1]}")
        time.sleep(4)
        copy = (probe(b, "l2p") or {}).get(potion, 0)
        note("PASS" if copy == after else "FAIL", f"l2p: the partner's copy follows: potion x{copy} (host x{after})")

        # --- Items > Miscellaneous; A on the silver key is refused ------------------------------
        open_page(b, "l2k", "Items", ITEMS_ROW["Miscellaneous"])
        cmd, ack = press_a_on_entry(a, b, "l2k", 0, "use")
        close_menus(b, 2)
        note("PASS" if ack and "no:questitem" in ack else "FAIL",
             f"l2k: the host refused the quest item ({ack and ack.split('] ', 1)[-1]})")

        # --- Abilities -> henchman abilities; Kick ignored -----------------------------------
        for page, want, tag in (("Abilities", "henchman abilities", "l2ab"), ("Kick", "ignored", "l2kick")):
            n = b.count(r"\[coopkit\] pause page '")
            open_wheel(b, tag)
            down(ROW[page])
            b.shot(f"{tag}_on_row.png")
            key("E")
            ok = b.wait(f"{tag}: {page} chosen on the partner", r"\[coopkit\] pause page '", n, 8)
            line = last(b, r"\[coopkit\] pause page '") if ok else ""
            note("PASS" if ok and f"'{page}' chosen: {want}" in line else "FAIL",
                 f"{tag}: {page} -> {line.split('] ', 1)[-1] if line else 'no event'}")
            time.sleep(2.5)
            b.shot(f"{tag}_after.png")
            a.shot(f"{tag}_host.png")
            close_menus(b, 1 if page == "Abilities" else 0)

        # --- pickup by player 2 -------------------------------------------------------------
        note("INFO", "pickup: not scripted (needs player 2 walked onto an item); left for the hands-on test")
        a.shot("l2z_host_end.png")
        b.shot("l2z_partner_end.png")
    finally:
        for g in (a, b):
            g.stop()
        for g in (a, b):
            if g.log:
                shutil.copy(g.log, os.path.join(e2e.OUT, f"{g.who}.log"))


if __name__ == "__main__":
    run()
