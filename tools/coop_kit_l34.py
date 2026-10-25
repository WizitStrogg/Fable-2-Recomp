#!/usr/bin/env python3
"""coop_kit_l34.py - feature 002 launches 3 and 4 (research/002-evidence/inventory-plan.md).

  python tools/coop_kit_l34.py l3   end to end, the actions by buttons only
  python tools/coop_kit_l34.py l4   host saves, both restart, continue: kit kept

L3 (evidence research/002-evidence/l3/): the console is used only to give player 2
the test kit and to read the host's snapshot; every action is a button press.
  - neither game pauses while the other has its menu open (two screenshots 3 s apart differ)
  - partner's Save row -> the henchman quit (cancelled with B, never confirmed)
  - Abilities -> henchman abilities, Kick -> nothing, Options opens as is
  - drink a potion and wear the hat from the stock pages with the host's menu open too
L4 (evidence research/002-evidence/l4/): player 2's kit changes through the stock
pages, the HOST saves from its own pause menu (Save row, the test copy's slot in
wt002-a\\saves, never a real save), both quit and restart, the host continues
the campaign and the partner rejoins. Checks, in the v13 style (104 = 104):
  host's live item count for player 2 before the save
    == items= total in coop_player2.txt after the save
    == host's live count after the continue
  and the copy on the partner is mirrored back to the same list.
Same copies as launches 1-2 (wt002-a host, wt002-b partner); never mod-a/mod-b.
"""

import os
import re
import shutil
import sys
import time

from PIL import Image, ImageChops

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_ship_e2e as e2e  # noqa: E402
import coop_kit_l2 as l2  # noqa: E402  (installs the test input source first)
import coop_kit_l1 as l1  # noqa: E402

note, console, focus = e2e.note, e2e.console, e2e.focus
key, down, open_wheel, close_menus, open_page = l2.key, l2.down, l2.open_wheel, l2.close_menus, l2.open_page
ROW, ITEMS_ROW, CLOTHING_ROW = l2.ROW, l2.ITEMS_ROW, l2.CLOTHING_ROW
last, lua, snapshot, probe, hex_of = l1.last, l1.lua, l1.snapshot, l1.probe, l1.hex_of


def out_dir(name):
    e2e.OUT = os.path.join(e2e.ROOT, rf"research\002-evidence\{name}")
    os.makedirs(e2e.OUT, exist_ok=True)


def moving(g, tag):
    """True when two screenshots of g, 3 s apart, differ (the game is not paused)."""
    p1 = g.shot(f"{tag}_t0.png")
    time.sleep(3)
    p2 = g.shot(f"{tag}_t3.png")
    try:
        box = ImageChops.difference(Image.open(p1).convert("RGB"), Image.open(p2).convert("RGB")).getbbox()
    except Exception as e:  # noqa: BLE001
        note("INFO", f"{tag}: compare failed: {e}")
        return False
    return box is not None


def start_and_join(a, b, tag, host_row="Host: new co-op campaign"):
    for g in (a, b):
        g.n_menu = g.count(r"main menu: Co-op entry added")
        l2.past_title(g)
    e2e.menu_then_coop(a, tag, a.n_menu)
    if host_row.startswith("Host: new"):
        e2e.host_new_campaign(a, tag)
    else:
        a.select(host_row)
        a.press("E")
        time.sleep(3)
        a.shot(f"{tag}_host_save_list.png")
        rows = a.rows()
        note("INFO", f"{tag}: host save list {rows}")
        if rows:
            a.select(rows[0][0][:12])
        n = a.count(r"\[lobby\] open host kind=continue")
        a.press("E")
        a.wait(f"{tag}: the campaign save opens its lobby", r"\[lobby\] open host kind=continue", n, 15)
    e2e.menu_then_coop(b, tag, b.n_menu)
    e2e.wait_row(b, "Join TestA", f"{tag}: the host's lobby is listed")
    e2e.partner_joins(b, "Join TestA", tag)
    e2e.wait_row(a, "Start", f"{tag}: host lobby shows the partner")
    return e2e.both_ready_and_start(a, b, tag) and e2e.join_phase(a, b, tag)


def give_kit(a, b):
    for name in (l1.POTION, l1.POTION, l1.FOOD, l1.HAT, l1.KEY_ITEM) + l2.SWORDS:
        console(a, f"lua Inventory.AddItemOfType(GetPlayerHenchman(), '{name}')")
        time.sleep(0.5)
    console(b, "coop_kit_mirror true")
    time.sleep(6)


def host_count(a, tag):
    """Player 2's live item count on the host (Inventory.GetNumberOfItems, the
    same pass coop_player2.txt's items= is written from)."""
    line = lua(a, "print('KITN '..Inventory.GetNumberOfItems(GetPlayerHenchman()))", "KITN",
               f"{tag}: host counts player 2's inventory")
    m = re.search(r"KITN (\d+)", line or "")
    return int(m.group(1)) if m else None


def tree_hashes(root):
    """Relative path -> sha256 for every file under root."""
    import hashlib
    out = {}
    for d, _, files in os.walk(root):
        for f in files:
            p = os.path.join(d, f)
            with open(p, "rb") as fh:
                out[os.path.relpath(p, root)] = hashlib.sha256(fh.read()).hexdigest()
    return out


def press_entry(a, b, tag, row, op):
    return l2.press_a_on_entry(a, b, tag, row, op)


# --- L3 -----------------------------------------------------------------------------------

def run_l3():
    out_dir("l3")
    a, b = e2e.Game("wt002-a", "host"), e2e.Game("wt002-b", "partner")
    a.start()
    b.start()
    try:
        if not start_and_join(a, b, "l3"):
            return
        time.sleep(6)
        give_kit(a, b)
        snap = snapshot(a, "l3a")
        items = snap[1] if snap else {}
        potion, hat = hex_of(items, "ITEMS_POTIONS"), hex_of(items, "CLOTHING_HATS")

        # Neither game pauses for the other's menu.
        open_wheel(b, "l3m_partner")
        note("PASS" if moving(a, "l3m_host_while_partner_menu") else "FAIL",
             "l3m: the host's game keeps running while the partner's menu is open")
        close_menus(b, 0)
        focus(a.hwnd)
        key("Escape")
        time.sleep(2)
        note("PASS" if moving(b, "l3m_partner_while_host_menu") else "FAIL",
             "l3m: the partner's game keeps running while the host's menu is open")
        focus(a.hwnd)
        key("Escape")
        time.sleep(2)

        # Wheel rows that are not item pages.
        b_saves = os.path.join(e2e.ROOT, r"wt002-b\saves")
        saves0 = tree_hashes(b_saves)
        for page, want, tag, backs in (("Save", "henchman quit", "l3s", 1), ("Abilities", "henchman abilities", "l3ab", 1),
                                       ("Kick", "ignored", "l3kick", 0), ("Options", "as is", "l3opt", 1)):
            n = b.count(r"\[coopkit\] pause page '")
            open_wheel(b, tag)
            down(ROW[page])
            b.shot(f"{tag}_on_row.png")
            key("E")
            ok = b.wait(f"{tag}: {page} chosen on the partner", r"\[coopkit\] pause page '", n, 8)
            line = last(b, r"\[coopkit\] pause page '") if ok else ""
            routed = ok and f"chosen: {want}" in line
            note("PASS" if routed else "FAIL", f"{tag}: {page} -> {line.split('] ', 1)[-1] if line else 'no event'}")
            if page == "Save" and not routed:
                key("B")  # the stock save menu may be open: back out at once, never confirm
                time.sleep(1)
            time.sleep(2.5)
            b.shot(f"{tag}_after.png")  # Save: the henchman quit question; B cancels it, A is never pressed
            close_menus(b, backs)
        note("PASS" if not any("[content] close root=Save" in l for l in b.lines()) else "FAIL",
             "l3s: the partner never wrote a save")
        saves1 = tree_hashes(b_saves)
        changed = sorted(p for p in set(saves0) | set(saves1) if saves0.get(p) != saves1.get(p))
        note("PASS" if not changed else "FAIL", f"l3s: wt002-b\\saves unchanged (changed: {changed})")

        # Item actions by buttons, with the host's own menu open at the same time.
        focus(a.hwnd)
        key("Escape")
        time.sleep(1.5)
        before = (items.get(potion) or (0,))[0] if potion else 0
        open_page(b, "l3p", "Items", ITEMS_ROW["Potions"])
        _, ack_p = press_entry(a, b, "l3p", 0, "use")
        close_menus(b, 2)
        open_page(b, "l3h", "Clothing", CLOTHING_ROW["Headgear"])
        _, ack_h = press_entry(a, b, "l3h", 0, "wear")
        close_menus(b, 2)
        focus(a.hwnd)
        key("Escape")  # host's menu closed again
        time.sleep(3)
        snap = snapshot(a, "l3z")
        after = (snap[1].get(potion) or (0,))[0] if snap and potion else 0
        worn = l2.field(snap, "worn").split(",")
        note("PASS" if ack_p and ": ok" in ack_p and after == before - 1 else "FAIL",
             f"l3p: potion by buttons with both menus open: {before} -> {after} ({ack_p and ack_p.split('] ', 1)[-1]})")
        note("PASS" if ack_h and ": ok" in ack_h and hat in worn else "FAIL",
             f"l3h: hat by buttons: worn={','.join(worn)} ({ack_h and ack_h.split('] ', 1)[-1]})")
        a.shot("l3z_host_end.png")
        b.shot("l3z_partner_end.png")
    finally:
        stop_and_keep(a, b, "")


# --- L4 -----------------------------------------------------------------------------------

def slot_of(a):
    for l in reversed(a.lines()):
        m = re.search(r"starter save: created (.*?) for XUID", l)
        if m:
            return m.group(1)
    return None


def p2file_total(slot):
    p = os.path.join(slot or "", "coop_player2.txt")
    if not os.path.exists(p):
        return None, ""
    text = open(p, encoding="utf-8", errors="replace").read()
    m = re.search(r"\|items=([^|]*)", text)
    total = sum(int(n) for _, n in re.findall(r"([0-9A-Fa-f]+):(\d+)", m.group(1))) if m else 0
    return total, text


def host_save(a, tag):
    """The host's own pause menu: Save row (row 0) -> Save -> confirm."""
    n = a.count(r"\[content\] close root=Save")
    nf = a.count(r"player 2 file written")
    focus(a.hwnd)
    key("Escape")
    time.sleep(2)
    key("Up")
    time.sleep(1)
    a.shot(f"{tag}_host_on_save.png")
    for i in range(3):
        key("E")
        time.sleep(2.5)
        a.shot(f"{tag}_host_save_{i}.png")
    ok = a.wait(f"{tag}: the host's save closed the slot", r"\[content\] close root=Save", n, 40)
    a.wait(f"{tag}: player 2 file written at the save", r"player 2 file written", nf, 30)
    time.sleep(2)
    key("Escape")
    time.sleep(2)
    return ok


def run_l4():
    out_dir("l4")
    backup = os.path.join(e2e.OUT, "saves-before")
    for side in ("wt002-a", "wt002-b"):
        dst = os.path.join(backup, side)
        if os.path.exists(dst):
            sys.exit(f"{dst} already exists: keep the earlier backup, move it away first")
        shutil.copytree(os.path.join(e2e.ROOT, side, "saves"), dst)
    note("INFO", f"l4: saves backed up to {backup}")
    a, b = e2e.Game("wt002-a", "host"), e2e.Game("wt002-b", "partner")
    a.start()
    b.start()
    try:
        if not start_and_join(a, b, "l4"):
            return
        slot = slot_of(a)
        note("INFO", f"l4: campaign slot {slot}")
        time.sleep(6)
        give_kit(a, b)
        snap = snapshot(a, "l4a")
        items = snap[1] if snap else {}
        potion, hat = hex_of(items, "ITEMS_POTIONS"), hex_of(items, "CLOTHING_HATS")
        # Change the kit by buttons: drink one potion, wear the hat.
        open_page(b, "l4p", "Items", ITEMS_ROW["Potions"])
        press_entry(a, b, "l4p", 0, "use")
        close_menus(b, 2)
        open_page(b, "l4h", "Clothing", CLOTHING_ROW["Headgear"])
        press_entry(a, b, "l4h", 0, "wear")
        close_menus(b, 2)
        time.sleep(3)
        before = snapshot(a, "l4b")
        n0 = host_count(a, "l4b")
        note("INFO", f"l4b: before the save: live count {n0}, kit {before[0][:400] if before else None}")

        host_save(a, "l4s")
        total, text = p2file_total(slot)
        if text:
            with open(os.path.join(e2e.OUT, "l4_coop_player2_after_save.txt"), "w", encoding="utf-8") as f:
                f.write(text)
        # items= is one pass over Inventory.GetItemByIndex (tools/coop_lua/lib.lua), so it equals the live count.
        note("PASS" if total is not None and n0 is not None and total == n0 else "FAIL",
             f"l4s: coop_player2.txt items total {total} = live inventory count {n0}")
        if before:
            for what, h in (("potion", potion), ("hat", hat)):
                if h:
                    note("PASS" if h in text else "FAIL", f"l4s: coop_player2.txt holds the {what} {h}")
    finally:
        stop_and_keep(a, b, "_session1")

    # Restart both; the host continues the campaign, the partner rejoins.
    a, b = e2e.Game("wt002-a", "host"), e2e.Game("wt002-b", "partner")
    a.start()
    b.start()
    try:
        if not start_and_join(a, b, "l4c", host_row="Host: continue a co-op save"):
            return
        a.wait("l4c: player 2 restored from coop_player2.txt", r"player 2 restored from coop_player2", 0, 40)
        time.sleep(8)
        n1 = host_count(a, "l4c")
        note("PASS" if n0 is not None and n1 == n0 else "FAIL",
             f"l4c: player 2's item count kept across save and continue: {n0} = {n1}")
        after = snapshot(a, "l4c")
        if before and after:
            want = {h: v[0] for h, v in before[1].items()}
            got = {h: v[0] for h, v in after[1].items()}
            diff = {h: (want.get(h), got.get(h)) for h in set(want) | set(got) if want.get(h) != got.get(h)}
            note("PASS" if not diff else "FAIL", f"l4c: kit list kept (differences {diff})")
            note("PASS" if hat and hat in l2.field(after, "worn").split(",") else "FAIL",
                 f"l4c: the hat is still worn ({l2.field(after, 'worn')})")
        console(b, "coop_kit_mirror true")
        time.sleep(8)
        have = probe(b, "l4c") or {}
        if after:
            want = l2_wanted(after)
            got = {h: n for h, n in have.items() if h in want}
            note("PASS" if got == want else "FAIL", f"l4c: the partner's copy mirrored again: {got} vs {want}")
        open_page(b, "l4c_items", "Items", ITEMS_ROW["Potions"])
        close_menus(b, 2)
        a.shot("l4z_host_end.png")
        b.shot("l4z_partner_end.png")
    finally:
        stop_and_keep(a, b, "_session2")


def l2_wanted(snap):
    """The copy's wanted inventory for a snapshot: non-clothing items plus the equipped weapons."""
    want = {h: v[0] for h, v in snap[1].items() if not v[1].startswith("CLOTHING")}
    for w in (l2.field(snap, "melee"), l2.field(snap, "ranged")):
        if w and w not in want:
            want[w] = 1
    return want


def stop_and_keep(a, b, suffix):
    for g in (a, b):
        g.stop()
    for g in (a, b):
        if g.log:
            shutil.copy(g.log, os.path.join(e2e.OUT, f"{g.who}{suffix}.log"))


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "l3":
        run_l3()
    elif mode == "l4":
        run_l4()
    else:
        print(__doc__)
        sys.exit(2)
