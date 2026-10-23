# Research: Co-op campaign and lobby

Phase 0. Decisions with reasons, and the yes/no proofs that run first (`tasks.md` Phase 2). Proof
results are written back into this file.

## What is already known (evidence)

| Fact | Source |
| --- | --- |
| Main menu is a Gameface screen; items are structural (`frontendmainmenu.{fac,bgf}`); remote-control server (`127.0.0.1:8791`, `tools/fable2_control.py`) reports `MainMenu` state; function-call tracing can diff call counts between menu actions | `plans/main-menu-mod-item.md` |
| No-IP discovery on every adapter, lobby keyed data (`PublishData`/`GetPeerData`), F9 join-via-presence, F6 Co-op overlay (`rex::ui`) | rexglue-sdk `systemlink` (SDK `560df55`, `ca07498`), memory notes |
| Game auto-hosts an XSession (flags `0x12F`, 1+1 slots) on entering a world when signed in to "LIVE"; Options > Game has Online Orbs and Auto Joinable | memory notes 2026-10-23 |
| Player 2 can be a `CreatureHero`/`CreatureHeroFemale` of a chosen gender (record selector override `sub_8231EEE0`); clothes, hair and items applied by the host replicate | v13, `src/core/fable2_coop_hero.h`, `coop_lib.lua` |
| Gameflow skip: `GameflowBase:SetGameflowPosition(pos)` walks positions forward and runs each core quest's "Skipping QCxxx" branch; the QC060 skip gives adult starter weapons, unlocks Sparrow + 18 titles, `Age.SetAge(18)`, expressions; `Debug.SetHeroLevel` is a no-op in retail | `research\03-story-sync.md` section 4 |
| Save layout: `saves/<XUID>/4D5307F1/00000001/HeroNNN/` with `mainsave.bin`, `herosave.bin`, `chaptersave.bin` (slot card), `saveuid.bin`; Lua state root `SaveLoadSystem.GetSaveTable()` | `research\01-coop-research.md` section 1 |
| Network loss handling exists in the engine (`CNetworkManager::HandleLostConnection`) | game image strings |
| Henchman gold/XP shares are set by the host (`Multiplayer.SetHenchmanGoldValue`, `SetHenchmanExperienceValue`) | `research\02-hench-lua-rewards.md` section 3 |
| Family API takes the hero as an argument (`PlayerFamily.Marry(hero, villager)`) | decompiled scripts (relevant to feature 005) |

## Decisions

### D1. Lobby as a recomp overlay over the front end

- **Decision**: Hook the main menu's New Game and Continue activation. New Game shows a small
  Solo / Co-op choice; Co-op opens a lobby overlay (same `rex::ui` layer as F6, with pad
  navigation). Solo passes through to the original handler untouched.
- **Why**: The Gameface menu is data-driven from binary layouts; adding native screens is far more
  work than an overlay, and the F6 overlay already exists.
- **Open**: **Proof P1**: find the activation functions for New Game and Continue (function-trace
  diff while pressing A on each, per `plans/main-menu-mod-item.md`) and show we can intercept and
  later resume each.

### D2. Lobby protocol on the existing lobby channel

- **Decision**: Lobby state travels as lobby keyed data (`coop_lobby` key, see
  `contracts/lobby-protocol.md`). The host advertises an open lobby (new campaign) or a waiting
  campaign (campaign id + expected partner). The partner's lobby list shows both kinds.
- **Why**: Discovery and keyed data already work across Ethernet and ZeroTier.

### D3. How the partner gets into the world

- **Decision**: After the host's world is loaded and its session is up, the partner's game joins
  it automatically through the existing join-via-presence path (the F9 code).
- **Open**: **Proof P3**: can the partner join from the front end, without first loading a solo
  save? On Xbox the game allowed joining from an invite at the menu; our F9 path has only been
  used from inside a world. **Fallback**: the partner's game silently loads a bundled minimal
  "lobby" save from a hidden slot, then joins (the partner's own solo saves are never loaded or
  written).

### D4. Skip to adulthood

- **Decision**: Start the game's own New Game on the host, then, once the gameflow is running,
  call `Gameflow:SetGameflowPosition(ScriptEnum.DebugQC060)` (forward skip). The game's own skip
  branches give the adult starter state.
- **Open**: **Proof P2**: run it on the rig from a fresh New Game; compare the result with a
  normal play-through reaching QC060 (position, age, titles, weapons, world layers, chapter card).
  Note anything the skip misses (for example childhood choices that affect Bowerstone) and set
  defaults for them.

### D5. Campaign marker and slot

- **Decision**: A new campaign is a normal save slot on the host plus a marker file
  `coop_campaign.toml` inside the slot folder (see `data-model.md`). The marker is written only
  after both players are in the world and the first save succeeds.
- **Open**: **Proof P4a**: how the game picks the slot folder for a new game (`HeroNNN`), and how
  to read the marker when the Continue list or load screen selects a slot.

### D6. Player 2 persistence in the same save event

- **Decision**: Hook the game's save routine on the host. In the same save event, capture player
  2 (`CoopHero.Capture`, profile v2, see feature 002's data model) and write
  `coop_player2.txt` into the slot with an atomic replace, stamped with the save's `saveuid`.
  On load, apply it to player 2 when they spawn (v13 apply path, extended).
- **Why**: Writing both in one save event means the world and player 2 can never disagree, even
  after a crash (constitution Principle II).
- **Open**: **Proof P4b**: find the save routine (the `SaveLoadSystem` / `GetSaveTable` call path
  and the slot writer), hook after a successful write, and check autosaves and manual saves both
  trigger it.

### D7. Shared purse

- **Decision**: The host hero's purse is the campaign's purse. Gold player 2 earns (pickups,
  rewards, the host-set henchman share) is redirected into the host's purse; player 2's own gold
  is kept at zero.
- **Open**: **Proof P5**: where player 2's gold goes today when it picks up coins in the host's
  world, and whether `Multiplayer.SetHenchmanGoldValue(0)` plus a pickup redirect (or a
  `Money.Modify(host, n)` on each player-2 gold event) gives one exact total.

### D8. Disconnects

- **Decision**: On losing the partner, the host saves (through the normal save path, so player 2
  is captured) and returns both games to the lobby.
- **Open**: **Proof P6**: hook point for partner loss (`CNetworkManager::HandleLostConnection` or
  the session member-left notification) and a safe way to trigger a save and return to the front
  end.

### D9. Refuse strangers

- **Decision**: A campaign session only accepts the campaign's partner (online XUID derived from
  the PC name, as today). The game's drop-in co-op into a campaign is disabled by making the
  session non-joinable to anyone else (`SetSessionJoinable` / session flags) and by the lobby's
  partner check.

### D10. Backup to the partner

- **Decision**: After the host's last save of a session, zip the slot folder and send it over a
  one-shot TCP transfer in rexruntime to the partner, who stores it under
  `<exe dir>\coop-backups\<campaign id>\<time>.zip`.
- **Open**: **Proof P7**: slot size (Josh's solo save is about 420 KB of `mainsave.bin` plus
  small files) and transfer over ZeroTier.

## Proof list

| Proof | Question | Pass criteria | If it fails |
| --- | --- | --- | --- |
| P1 | Intercept New Game and Continue | Overlay shown; original flow resumes on Solo | Add a separate main-menu item (plans/main-menu-mod-item.md) |
| P2 | Skip to adulthood from a fresh New Game | State matches a normal QC060 start | Patch missing pieces by script after the skip |
| P3 | Join from the front end | Partner enters host world without loading a solo save | Bundled minimal lobby save |
| P4a/b | Slot creation and the save routine | Marker readable at load; hook fires on every save | Write player 2 on a timer plus on quit (weaker) |
| P5 | Shared purse | One exact total after pickups and rewards by both | Mirror player 2's gold into the host purse every second |
| P6 | Partner loss | Host saves and returns to lobby within 10 s | Host pauses with a "waiting" overlay instead |
| P7 | Backup transfer | Hash-equal copy on the partner | Manual copy instructions |
