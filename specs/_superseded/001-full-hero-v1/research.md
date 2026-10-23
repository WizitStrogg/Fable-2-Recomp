# Research: Full hero, host-run

Phase 0 of the plan. Each decision records what was chosen, why, and what was rejected. Items
marked **Proof Pn** are yes/no rig experiments that run first in `tasks.md` (Phase 2); their
result is written back into this file.

## What is already known (evidence)

| Fact | Source |
| --- | --- |
| Joiner runs no game Lua while visiting; host simulates player 2 | `D:\Fable2-LAN\research\05-client-camera.md` |
| Player 2 can be a `CreatureHero`/`CreatureHeroFemale` (record selector override `sub_8231EEE0`) | v13, commit `2149999`, `src/core/fable2_coop_hero.h` |
| Host can add items to player 2 (`Inventory.AddItemOfType`) and make it wear them; it replicates to the joiner | v13 evidence `research\v13-evidence\` |
| Host's save item count unchanged after a session with player 2 holding items | v13 rig run (104 items both before and after) |
| Henchman pause menu is a separate three-page branch (`HenchmanAbilities`, `HenchmanConfig`, `HenchmanQuit`) built by `sub_824129C0`, called from pause builder `sub_8240EAA0` | `research\strxref.py` / `fnstrings.py` on the game image |
| `AddUnlockedTitle` (`sub_8249F8A0`) must be skipped while co-op scripts run, or the host gets titles and pop-ups | v13 |
| LAN lobby keyed data: `rex::kernel::xam::lobby::PublishData(key, value)` / `GetPeerData(key)`, latest value per peer, carried in UDP beacons on port 3075 | rexglue-sdk `xam_lan_lobby.cpp`, SDK `ca07498` |
| Lua runner drains queued chunks on the game thread after `lua_resume` (`sub_82277B88`) | `src/core/fable2_lua_console.h`, memory notes v10 |
| Visitor save picker and reward write-back: `sub_8247AB80` (pick save), `sub_824EB910` (load herosave), HeroSave serialiser `sub_824E4780`, disconnect handling `sub_8230A2F0` / `sub_82321268` | `research\02-hench-lua-rewards.md` |
| Lua APIs available on any entity: `Inventory.GetNumberOfItems`, `GetItemByIndex`, `AddItemOfType`, `RemoveItemOfType`, `GetNumberOfItemsOfType`; `Experience.Get`, `Modify`; `Money.Modify`; `Stats.Get*/Modify*`, `GetHeroAbilityLevel`, `SetHeroAbilityLevel`; `WeaponSetManager.EquipWeaponOfType`; `AppearanceModifierManager.AddAndReplaceItemOfRecordID`, `RemoveItemOfType` | `FABLE2_LUA_API.md`, `coop_lib.lua` |
| No Lua API found for "use/consume an item" (potion, food) | `FABLE2_LUA_API.md` search; needs the native inventory-screen action |

## Decisions

### D1. Command transport: reuse the lobby keyed-data channel with sequence numbers

- **Decision**: Commands ride the existing `PublishData`/`GetPeerData` channel. The joiner
  publishes key `cmd` holding all unacknowledged commands, each with a monotonically
  increasing `seq`. The host applies each `seq` once, in order, and publishes key `ack` with the
  highest applied `seq` and per-command results. The joiner drops acknowledged commands.
- **Why**: Latest-value-per-key semantics plus sequence numbers give at-most-once, in-order
  delivery without a new socket, and the channel already works over Ethernet and ZeroTier.
- **Rejected**: A new TCP or GameNetworkingSockets connection now (deferred to feature 003/004,
  where a real channel is needed for hero state at frame rate); piggybacking on the game's own
  net messages (undocumented ids, Principle VI).
- **Open**: Maximum value size per beacon. **Proof P1** measures it; values larger than the limit
  are split into parts (`key#i/n`), as `coop_story_capture` already does with 1,500-character
  parts.

### D2. Player 2 state: host snapshots, joiner mirrors

- **Decision**: The host captures player 2 with an extended `CoopHero.Capture` (profile v2) after
  every applied command and every 2 seconds while the joiner is present, and publishes it as key
  `p2state` with a revision number. The joiner's co-op menu displays the latest `p2state`.
- **Why**: Polling a snapshot needs no hooks into the game's item, XP or gold events (Principle VI)
  and automatically catches changes the joiner did not cause (orbs, pickups, rewards, damage).
- **Rejected**: Hooking every inventory/XP/gold change in C++ (dozens of call sites, fragile).
- **Open**: Capture cost with 100+ items. **Proof P1** times one capture; if it exceeds 2 ms on the
  game thread, capture items only after inventory-changing events and stats every 2 seconds.

### D3. Session ledger = baseline + latest confirmed snapshot

- **Decision**: On join, the joiner stores its own pre-join capture as the **baseline**. Every new
  `p2state` revision is written to disk (atomic replace) as the **latest**. On the joiner's next
  load of its own save, the apply step computes deltas (latest minus baseline) and applies them:
  XP pools and gold by `Experience.Modify` / `Money.Modify`, items by add/remove of the count
  difference, ability levels and titles by setting them, morality, purity, fat and strength by
  `Stats.Modify*`. Then the ledger is archived with a hash so it is never applied twice.
- **Why**: Deltas against a baseline are idempotent to compute, survive a crash (the last written
  snapshot is the state to restore), and never need the game's save format.
- **Rejected**: An event log of individual changes (order-sensitive, misses changes we do not
  hook); editing `mainsave.bin` directly (Principle II; only if Proof P6 shows deltas cannot be
  applied through Lua).
- **Open**: Whether the joiner's own save can change between baseline and apply (it cannot while
  the joiner is visiting; the apply runs before the next join).

### D4. Applying the full hero on the host

- **Decision**: Extend `CoopHero.Apply` (`data/scripts/recomp/coop_lib.lua`) to profile v2: set XP
  pools, ability levels, gold display value, titles (with `AddUnlockedTitle` suppressed as in
  v13), then reconcile player 2's inventory to the profile's item list (add missing, remove
  extras), then equip weapons and clothing.
- **Why**: Builds on the v13 path, which is proven.
- **Open**: **Proof P2**: does `Stats.SetHeroAbilityLevel` on player 2 change player 2's real
  spell level and combat stats on the host (cast Fireball, compare damage and visuals)?
  **Proof P3**: is player 2's gold a separate purse (`Money.Modify(player2)`), or does the game
  have one shared purse? If shared, gold is mirrored in the menu and ledger only.

### D5. Consumables (potions, food, drink, augments)

- **Decision**: Find the native action the game's inventory screen calls to use an item, and call
  it for player 2 on the host's game thread.
- **Why**: There is no Lua API for using an item; the native action applies the game's effects
  (healing, fat, drunkenness, morality) correctly.
- **Open**: **Proof P4**: locate the function (trace `GUI_SCREEN_INVENTORY_*` handlers with
  `strxref.py`, then a function trace while drinking a potion on the rig) and show it heals
  player 2 when called with player 2's entity. **Fallback**: apply effects by script for the
  common items (health potions via `Health`, food via `Stats.ModifyFat`) and remove the item.

### D6. The game's own visitor reward write-back

- **Decision**: Leave the game's write-back on, and make the ledger reconcile against it: the
  apply step reads the joiner's `herosave.bin` gold and XP after the session and subtracts what
  the game already wrote back before applying deltas.
- **Open**: **Proof P5**: after a session with known gains, diff the joiner's `herosave.bin` and
  `mainsave.bin` hero block (`f2save.py`) to see exactly what the game writes. If the game writes
  nothing to `mainsave.bin`, the reconcile is against `herosave.bin` only.

### D7. Where the apply step runs

- **Decision**: In the joiner's own world, right after its save has loaded, through the same
  load-time hook that `auto_capture` (`fable2_coop_hero.h`) already uses, before any new join.
- **Open**: **Proof P6**: apply a hand-written ledger (+1,000 Will XP, +1 Rusty Cleaver, -1 health
  potion, Fireball 2) on the rig to a copied save; save, reload, capture, diff.

### D8. The co-op menu

- **Decision**: A recomp overlay (same UI layer as the F6 Co-op menu, `rex::ui`), opened with a
  configurable key (default F7) and a pad combo (default Back + Y), with tabs Inventory, Equipment,
  Abilities, Hero. It never pauses the game. It reads `p2state` and sends commands.
- **Why**: The joiner's PC has no local hero data while visiting, so the game's own menu cannot
  show it in Tier 1. A native menu is feature 004's job.
- **Rejected**: Forcing the game's full pause menu on the joiner now (likely shows the replicated
  entity with no inventory; revisit in 004 Proof).
- **Open**: Whether `rex::ui` supports pad navigation already (F6 is mouse-first); if not,
  add pad navigation as a task.

### D9. Body morphs (story 5)

- **Decision**: Investigate after stories 1 to 4. v13 found the fat and strength stats arrive but
  the body does not change; likely the morph weights are baked when the creature mesh is built.
- **Open**: Find where the hero's morph weights are pushed to the mesh (the recomp's
  `plans/hero-dog-realtime-texture-morphing.md` is related) and trigger it for player 2.

## Proof list (run first, see tasks.md Phase 2)

| Proof | Question | Pass criteria | If it fails |
| --- | --- | --- | --- |
| P1 | Channel size limit and capture cost | Largest single value delivered intact; one capture under 2 ms | Split values into parts; event-driven capture |
| P2 | Player 2 ability levels are real | Player 2 casts the set level (visual and damage differ) | Find the ability table player 2 uses; may need a native setter |
| P3 | Player 2 has its own purse | `Money.Modify(player2)` changes player 2's gold, not the host's | Gold lives only in the menu and ledger |
| P4 | Native "use item" works on player 2 | Potion heals player 2 when called for player 2 | Scripted effects for common consumables |
| P5 | What the game writes back to the visitor's save | Exact fields written are listed | Neutralise the write-back for gold/XP |
| P6 | Ledger deltas apply cleanly through Lua | Post-apply capture equals expected | Direct save editing with round-trip test (needs approval) |
