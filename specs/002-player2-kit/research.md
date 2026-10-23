# Research: Player 2's full kit

Phase 0 for feature 002. The full plan is generated after 001 ships; these decisions and proofs
are its starting point. The earlier ledger-based version is in `../_superseded/001-full-hero-v1/`.

## What is already known (evidence)

| Fact | Source |
| --- | --- |
| Joiner runs no game Lua while visiting; host simulates player 2 | `D:\Fable2-LAN\research\05-client-camera.md` |
| Host can add items to player 2 and make it wear them; replicates to the joiner | v13 evidence `research\v13-evidence\` |
| Henchman pause menu is a separate three-page branch (`sub_824129C0`, called from `sub_8240EAA0`) | static trace on the game image |
| Lua APIs on any entity: `Inventory.GetNumberOfItems`, `GetItemByIndex`, `AddItemOfType`, `RemoveItemOfType`; `Experience.Get`, `Modify`, `GetTotalEarned`; `Stats.GetHeroAbilityLevel`, `SetHeroAbilityLevel`, `GetHeroAbilityMaximumLevel`; `WeaponSetManager.EquipWeaponOfType`; `AppearanceModifierManager.AddAndReplaceItemOfRecordID`, `RemoveItemOfType` | `FABLE2_LUA_API.md`, `coop_lib.lua` |
| No Lua API to use/consume an item | `FABLE2_LUA_API.md` search |
| Abilities screen strings handled in `sub_8259EF38` (`GUI_SCREEN_ABILITIES_LEARN`) | `strxref.py` |
| Host sets henchman XP share (`Multiplayer.SetHenchmanExperienceValue`); orb tuning bindings (`SetExperienceOrbAbilityXPMultiplier`, `SetExperienceOrbGeneralXPMultiplier`, absorb distances) | `research\02-hench-lua-rewards.md`, `luabind_map.tsv` |

## Decisions

### D1. Command transport: lobby keyed data with sequence numbers

Commands ride `PublishData`/`GetPeerData`: the joiner publishes all unacknowledged commands with
increasing `seq`; the host applies each once, in order, and publishes `ack`. Values over the
per-value limit are split into parts. See `contracts/coop-channel.md`. A dedicated real-time
channel comes with features 003/004.

### D2. Player-2 state: host snapshots mirrored to the joiner's menu

The host captures player 2 after each applied command and every 2 seconds and publishes `p2state`
with a revision. Polling a snapshot needs no hooks on every inventory or XP change.

### D3. XP routing

- **Decision**: Intercept the point where the game credits XP from collected orbs and quest rewards
  to a hero, and re-route per the rule: typed XP to the earner, the same amount as general XP to the
  partner; general and quest XP split 50/50. Log every award (`COOPXP:v1`).
- **Open**: **Proof X1**: find the credit function (trace while player 1 and player 2 each collect
  strength and will orbs; `Experience.Modify` callers; orb absorb path) and check whether player 2's
  orbs already credit player 2's own pools or only the host's henchman share.
- **Open**: **Proof X2**: does the game drop typed orbs per killer already (strength orbs for the
  sword kill, will orbs for the spell kill)? If yes, routing is only "give the partner the general
  copy"; if no, the earner must be derived from the killing blow.

### D4. Consumables

Find the native action the inventory screen calls to use an item and call it for player 2 on the
host's game thread (**Proof C1**: trace while drinking a potion). Fallback: scripted effects for
common consumables.

### D5. Ability levelling

Read the game's own cost and max-level tables (`sub_8259EF38` path, `GetHeroAbilityMaximumLevel`)
so the co-op menu charges exactly what the native screen charges (**Proof A1**). **Proof A2**:
`Stats.SetHeroAbilityLevel` on player 2 changes the real spell level and damage on the host.

### D6. The co-op menu

A recomp overlay (`rex::ui`, like F6) on the joiner, key F7 and pad Back+Y, tabs Inventory,
Equipment, Abilities, Hero; reads `p2state`, sends commands; never pauses. The native pause menu is
feature 004's job.

### D7. Body morphs

v13 found fat and strength stats arrive but the body does not change. Find where morph weights
reach the mesh (start from `plans/hero-dog-realtime-texture-morphing.md`) and trigger it for
player 2.

## Proof list

| Proof | Question | If it fails |
| --- | --- | --- |
| P1 | Channel value size and capture cost | Parts; event-driven capture |
| X1 | Where XP is credited to a hero | Award XP ourselves on kill events |
| X2 | Are typed orbs already per killer | Derive the earner from the killing blow |
| A1 | Native ability costs | Hard-code the costs from the game data |
| A2 | Player 2 ability levels are real | Find player 2's ability table |
| C1 | Native use-item works for player 2 | Scripted effects |
