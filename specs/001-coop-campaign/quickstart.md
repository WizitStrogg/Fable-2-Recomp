# Quickstart: Co-op campaign and lobby

## Rig run (Claude, autonomous)

1. Build `coop/001-coop-campaign` (`build.cmd -release fable_2`); check no `fable_2.exe` runs;
   install into `D:\Fable2-LAN\mod-a` and `mod-b`; set `coop_campaign = true` in both.
2. Hash every solo slot in both instances (`tools\hash_slots.ps1`, task T004).
3. Start both (`tools\gamectl.ps1 start a` / `start b`); drive the front end with
   `tools/fable2_control.py` (remote-control server on 127.0.0.1:8791 / 8792).
4. A: New Game → Co-op. B: New Game → Co-op → join A. B: Female, Ready. A: Skip to adulthood,
   Ready, Start.
5. Both in world: screenshot both windows to `research\001-evidence\`; capture player 2 on A.
6. Play scripted actions (B picks up gold and an item), save on A, quit both, reload, compare
   captures and purse.
7. Re-hash solo slots: must be identical.

## Two-PC test (Josh and Calvin, after v14)

1. Both on v14; F6 → tick "Co-op campaign" → Save to config; restart the game.
2. Josh: New Game → Co-op. Calvin: New Game → Co-op → pick Josh's lobby → choose gender → Ready.
   Josh: Skip to adulthood → Ready → Start.
3. Play, save, quit. Another day: Josh Continue → the campaign → waits; Calvin Continue → Co-op →
   join. Follow `COOP-TESTS.txt` v14 steps.

## Turning it off

Untick "Co-op campaign" in F6. New Game and Continue behave as the normal game; campaign slots
stay on disk and appear in Continue but cannot be entered while the box is off.
