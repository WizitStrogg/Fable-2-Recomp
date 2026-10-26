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


## Proof results (rig, 2026-10-23)

Evidence root: `D:\Fable2-LAN\research\001-evidence\` (below: `E\`). Rig: mod-a host 127.0.0.1, mod-b partner
127.0.0.2, rig build with the remote-control server (`FABLE2_RIG_REMOTE_CONTROL`).

| Proof | Result | Evidence |
| --- | --- | --- |
| P1 | **Verified.** Hook: `FrontMenu_PressBitDispatch` (0x82188F20), r5 = pressed mask (Up 1, Down 2, Back 32, A 0x1000, B 0x2000), called on press and on release. The menu opens on New Game; Continue loads the newest save directly (no list). Intercept and resume work: A on New Game is swallowed and the chooser opens; Solo injects a fresh A that the stock New Game takes (boy/girl hero cards). | `E\T018\launch1_02_chooser_screen.png`, `launch1_03_solo_hero_cards_frame.png`, `E\launch2\t018_a_host_lobby_screen.png`, `E\proofs\P1\` |
| P2 | **Not yet proven.** The run reached the pause menu instead of a fresh New Game (navigation error), so the skip ran on an adult save: `SetGameflowPosition(DebugQC060)` from position 13 restarted the gameflow, logged "Skipping QC085", "Skipping QC010", then "QC060_NewBeginning Starting", age 18.3. Shows the skip API works from the console; the fresh-New-Game comparison is still to do. | `E\launch2\launch2_results.txt`, `p2_after_skip_*` |
| P3 | **Verified.** mod-b at the main menu (no save loaded) pressed F9 and joined mod-a's world; player 2 spawned. mod-b's own save stayed byte-identical. No lobby-save fallback needed. | `E\launch2\p3_b_joined_frame.png`, `p3_a_joined_frame.png`, `E\hashes\mod-b-after-launch2-*.txt` |
| P4a | **Verified.** Loading opens the slot with root `Save`, `OPEN_EXISTING`; New Game creates the next free `HeroNNN` with `CREATE_ALWAYS` (flags 0x802) and writes a complete starting save within ~8 s, before the hero cards. | `E\proofs\RESULTS.md`, `mod-b\logs\fable_2_028.log` (13:44:18) |
| P4b | **Verified for manual save.** A save opens root `Save` with `OPEN_ALWAYS` (folder kept, a test file survived), writes mainsave/herosave/chaptersave/texturemorphs, then `XamContentClose` (after the last write). Hook point: that close. `saveuid.bin` is not rewritten per save (slot id, not save id), so `coop_player2.txt` needs its own per-save stamp. Autosave not yet observed. | `E\proofs\P4\a_slot_times.txt`, `a_0*.png` |
| P5 | **Partly.** Host Lua `Money.Modify(player2, 100)`: host +50 and player 2 +50 (the henchman gold share splits gold 50/50), player 2 has its own purse (`Money.Get` works on it). Pickups and quest rewards by player 2 not yet tested. | `E\launch2\launch2_results.txt` (P5 lines), `p5_*` |
| P6 | **Partly.** Killing mod-b: lobby "went quiet" ~3 s, game scripts notice ("Henchman was alive but is no longer"), host calls `XSessionLeaveRemote` ~3.5 s after the loss; host stays in its world, no crash. Hook point: `XSessionLeaveRemote` in the SDK. Saving and returning to the lobby on it not yet proven. | `E\launch2\p6_a_log_after_kill.txt`, `p6_trace_diff.txt`, `p6_a_after_partner_loss_frame.png` |
| P7 | **Verified on loopback** (127.0.0.1 -> 127.0.0.2): slot 850 KB -> zip 767 KB, all 7 hashes verified, tampered copy rejected, source unchanged. ZeroTier unverified (needs two PCs). | `E\proofs\P7\p7_run1.txt` |

Rig findings that change the plan:
- The remote pad needed XInput keystrokes: New Game's step after the storage selector reads keystrokes, not pad state (fixed in `src/input/remote_gamepad_driver.h`).
- The title prompt ("Press A to start") uses the same dispatch with the same registers while the state probe already says MainMenu, so the front end lets the first A of each front-end visit through.
- Game-side ImGui dialogs cannot link (ImGui is inside rexruntime.dll); the SDK draws a generic lobby panel from a text view (`rex_ui_set_lobby_view`).
- `saveuid` stamp in data-model.md: replace with a per-save stamp (decided when T030 is built).

## Rig launches 3-10 (2026-10-23): what each found

- Launch 3: panel's closing A leaked into the menu hook (reopened the chooser). Fixed: closing A passes to New Game (Start/Solo) or is swallowed (Back).
- Launch 4: skip waited on the state probe, which reports MainMenu inside the world. Fixed: script-load counter.
- Launch 5: partner's go callback ran under the lobby lock and re-entered it (partner game died). Fixed: callbacks run after unlocking.
- Launch 6: partner's join prompt A was caught by the front-end hook (started a Solo game). Fixed: hook passes presses during a join.
- Launch 7: skipping from inside the running childhood leaves QC060 on a black screen waiting for a Bower Lake load (Lua check: BowerLake not loaded, screen faded). Fixed: redirect the new game's own SetGameflowPosition(QC010) to QC060 (src/lua/coop_skip_childhood.lua), the path launch 2 took.
- Launch 8: the first wrapper run landed before the world's Lua state existed. Fixed: retry every 500 ms.
- Launch 9: skip verified ("Skipping QC010" -> "QC060_NewBeginning Starting"). Host crashed (write of guest 0x4EADC200) seconds after the partner's join reached it, during QC060's opening.
- Launch 10: host crashed during QC060's opening with no join at all (read of guest 0x0F), 0.4 s after the v13 own-hero auto-capture Lua ran; in launch 9 the same capture ran 6 s before that crash. So the launch 9 "join crash" was probably this capture, not the join. The capture is now paused during the campaign start (T028b makes it safe everywhere); T028a's rig proof must check whether a join during a cutscene is really unsafe.
- QC060's own script provides the save call for P6: AutoSaveGame(true, true); and the co-op unlock point: Multiplayer.DisplayCoopScreen() at the end of its opening (the go signal now waits for it).
- Launch 11: no host crash with the capture paused. But the redirect from the new game's own start (SetGameflowPosition(QC010) -> QC060) fires while the new game is still loading the childhood level; QC060 then asks for Bower Lake and the host sits on "Loading Bowerstone Old Town" for 15+ minutes (001-evidence\launch11\a_now.png). Together with launches 5-7 (skip from inside the loaded childhood: QC060 waits for a Bower Lake load that never starts) and launch 2 (restart skip from a fully loaded adult world reaches Bower Lake), no runtime skip point from a fresh New Game has worked yet. Options raised with the coordinator on 2026-10-23: (A, recommended) an adult-start template save made once on the rig and copied into the new campaign slot; (B) wait for the childhood level to finish loading, then force the gameflow restart path.

## Decision 2026-10-23: P2 -> fallback A (starter save)

Decided by the coordinator under Josh's "skip to adulthood" approval, after launches 4-11. A new co-op campaign starts from a template slot made once from a normal playthrough, saved when the adult hero wakes at the Gypsy camp (Josh plays the childhood once in mod-c, D:\Fable2-LAN\PLAY-CHILDHOOD.txt; tools\make_coop_template.ps1 turns it into D:\Fable2-LAN\coop-template). Start copies it into the next free HeroNNN of the host's XUID folder with a fresh saveuid.bin and a content header owned by that XUID (src/core/fable2_coop_template.h; header layout in src/core/fable2_coop_slot.h, generated headers are byte-identical to the game's own for Hero000 and Hero001, tools/coop_slot_test.cpp), then the stock Continue loads it (newest save). The lobby's host gender is applied with Player.ChangePlayerEntityType if it differs from the template (to be proven on the rig). The XamContentGetCreator fix in the SDK already reports the current user as creator of every existing save, so the copied slot loads as the host's own; still to prove on a second PC (other XUID) in the two-PC test. The gameflow skip stays behind coop_childhood_skip (off) for a later "play the childhood" option (feature 005).

## 2026-10-24: first two-PC session, and the native Co-op entry

- Josh (host) and Calvin share the home LAN (192.168.1.x) and ZeroTier (192.168.192.x), so each hears the other twice. The host seated Calvin as "User" (the LAN lobby's name for a blank gamertag) while Calvin's lobby looked for "" (his profile_gamertag), so he never saw himself seated and ignored the go (Calvin's fable_2_011.log 12:00:22: coop_go received on both addresses, no join). Fix: players matched by XUID carried in the records (hxuid / pxuid); names display only (gamertag, else the PC name); one row per XUID; the go is acknowledged (coop_ack) and re-sent every 15 s.
- The proof run on the ship build (tools/coop_ship_e2e.py multi, tools/coop_relay.py giving the rig a second network) joined once end to end; the SDK's per-address XUID was 0 on one of the two addresses, so identity now comes from the record itself (SenderXuid).
- The state probe reports MainMenu inside the world (rig and ship builds alike), so "back at the front end" cannot be detected from it; the host now returns to the main menu after a partner loss and continues through the hub.
- The main menu is a tree built by sub_826CA6E0 with one AddEntry (sub_826CB808(menu, name, label, icon, parent, 0, 2, *(menu+20))) per entry: New Game, Load Game (= Continue, with saves), Downloadable Content, Language, Subtitles (+ On/Off). Labels come from the text lookup sub_82301D40, which shows an unknown id as itself. Selections reach sub_826C81A0(menu, {+0 name, +4 type}), which compares the name with the stock entries. The Co-op entry is one more AddEntry after Load Game (src/core/fable2_coop_mainmenu.h); its selection opens the hub; Start loads the campaign save through the same handler with "Load Game".

## 2026-10-25: Josh's one-PC playtest (two windows)

- Shared purse: a chest of 150 gave the game's own co-op share +75 to the host and +75 to the henchman copy; the host's sync summed them correctly (3 -> 153 on the host for both purses, mod-b log 20:31:13), but Money.Set on the host's henchman copy does not reach the partner's own game, so the partner's screen showed 3 + 75. Fix: the host publishes the total (coop_purse) and the partner mirrors it into its own purse. The partner sends no deltas: everything it does with gold happens in the host's world and is already in the host's sum; it joins with "continue without choosing a save", so the mirrored total never reaches its own hero save.
- Joiner camera felt buggy/glitchy on the henchman in this one-PC playtest (both windows on one PC); needs further testing on two PCs (open, T057).

## 2026-10-26: world gold, partner loss, persistence (coop_ship_e2e batch1, savestress)

- Gold (T012): chests in the Gypsy-camp region have no gold component (Money.IsAvailable
  false on all 15); they hold items. Gold lying in the world goes through the game's
  OnActionUsePickUp (Money.Modify(picker, gold)). A spawned ObjectInventoryGoldBag (5 gold)
  picked up by the host with A: the game split it "host 3, player 2 2" and the purse code
  made it 5 on all three purses, once. Player 2's own gains are merged once too.
- Quest gold: QuestTracker.GiveRewards (what Hero_QuestCompletionSequence calls) paid 0
  gold for all 114 quest names. Quest gold is paid by the quest scripts themselves with
  Money.Modify(QuestManager.HeroEntity, n) (e.g. QC066's 100), the same path the luaq and
  purse proofs cover; closed as covered.
- P6 / T029 / T032: the partner killed mid-session x3 (the host saves, returns to the front
  end, continues through the hub, the partner rejoins); the host alone cannot start; a third
  copy is shown the campaign "not for you" and never seated; the host killed 4-8 s after a
  save x3 and continued. Items, look and purses kept.
- Save crash (found by T032): a console chunk (002's kit tick) ran 0.7 s into an in-world
  AutoSaveGame and the host crashed with a guest write fault; the half-written save crashed
  the next load. The game's save builds the Lua save table (sub_825B09D0 -> GetSaveTable)
  and our drain runs at luaD_call entry. Fix: saving() (save table being built or within
  3 s, or a Save content root open) holds every queued script and native task. savestress
  (30 saves, 002's kit publishing 89 times): 0 crashes, every save held the scripts.
- Weapon re-add (T032 round 2: 5 -> 7 items): the restore ran before player 2's weapon
  slots were filled, so the sword and pistol were added again as items. The restore now
  skips weapons; CoopHero.ApplyWeapons adds only what is still missing once a slot is
  filled (or on the last of 10 tries, 2 s apart).
- Evidence: research/001-evidence/menus/ship-e2e/ (b1_*, p6_*, t032_*, ss_*), mod-a/mod-b logs of 2026-10-26.
