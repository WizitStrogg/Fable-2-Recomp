#!/usr/bin/env python3
"""coop_partner_retry.py - partner-side retry against a host that is already in
its adult world (mod-a left running by a launch): start mod-b only, New Game ->
Co-op -> join TestA's lobby -> coop_go -> join -> T017 capture of player 2 ->
P6 (close mod-b, watch the host). mod-a is left running unless --close-host.

  python tools/coop_partner_retry.py <tag> [--close-host]
Evidence: research/001-evidence/<tag>/ under D:/Fable2-LAN.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_launch7 as L  # noqa: E402  (shared helpers: note, shots, expect, console, select_row, ...)
import coop_rig  # noqa: E402


def main() -> int:
    tag = sys.argv[1]
    close_host = "--close-host" in sys.argv
    L.OUT = os.path.join(r"D:\Fable2-LAN\research\001-evidence", tag)
    os.makedirs(L.OUT, exist_ok=True)
    a = coop_rig.Instance("a").attach()
    b = coop_rig.Instance("b")
    b.start()
    L.note("INFO", f"mod-a (running) pid {a.pid} port {a.port}; mod-b pid {b.pid} port {b.port} log {b.log}")
    L.past_title(b, "b")
    L.open_coop(b, "b")
    time.sleep(4)
    n_go = L.count(b, r"coop_go received")
    n_in = L.count(b, r"partner: in the host's world")
    if L.select_row(b, "b", "Join TestA"):
        n = L.count(b, r"joining TestA's lobby as partner")
        b.press("A", 250)
        L.expect(b, "b: joins TestA's lobby as partner", r"joining TestA's lobby as partner", n)
    L.expect(b, "b: coop_go received, joining", r"coop_go received", n_go, 30)
    spawned = L.count(a, r"henchman spawned")
    # No presses on b here: during the join the front end passes them to the
    # stock menu (launch 7: an A started a New Game on b). The partner retries
    # the join itself until the host's game is joinable.
    L.expect(a, "player 2 spawned in a's world", r"henchman spawned", spawned, 330)
    L.expect(a, "a: session sees player 2", r"player 2 is in the world", 0, 30)
    L.expect(b, "b: in the host's world", r"partner: in the host's world", n_in, 30)
    time.sleep(10)
    L.shots(a, "a_world_with_p2")
    L.shots(b, "b_in_a_world")

    n = L.count(a, r"capture2 player2")
    L.console(a, "luafile coop_capture2")
    L.expect(a, "T017: capture2 ran", r"capture2 player2", n, 20)
    for l in L.grep_log(a, r"capture2|reparse|COOPHERO:v2", 8):
        L.note("INFO", l[l.find("[lua]"):][:500])

    n_lost = L.count(a, r"partner .* lost")
    n_q = L.count(a, r"QuitToFrontEnd: ")
    n_back = L.count(a, r"back at the front end")
    b.stop()
    L.note("INFO", "P6: mod-b closed")
    L.expect(a, "P6: a detects the partner loss", r"partner .* lost", n_lost, 30)
    L.expect(a, "P6: QuitToFrontEnd ran", r"QuitToFrontEnd: ", n_q, 15)
    for l in L.grep_log(a, r"QuitToFrontEnd", 2):
        L.note("INFO", l[l.find("[lua]"):][:200])
    time.sleep(20)
    L.shots(a, "a_after_loss")
    L.expect(a, "P6: a back at the front end, continue lobby", r"back at the front end", n_back, 90)
    time.sleep(3)
    L.shots(a, "a_continue_lobby")

    with open(os.path.join(L.OUT, "results.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(L.results) + "\n")
    with open(os.path.join(L.OUT, "a_log_extract.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(L.grep_log(a, r"\[coop\]|\[lobby\] (open|host|join|coop_go)|\[content\]|XSession|henchman|LAN lobby", 300)))
    with open(os.path.join(L.OUT, "b_log_extract.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(L.grep_log(b, r"\[coop\]|\[lobby\] (open|join|coop_go)|XSession|LAN lobby", 300)))
    if close_host:
        a.stop()
        L.note("INFO", "mod-a closed; rig off")
    else:
        L.note("INFO", f"mod-a left running (pid {a.pid})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
