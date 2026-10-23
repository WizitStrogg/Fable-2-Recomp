---

description: "Task list for 001 Co-op campaign and lobby"
---

# Tasks: Co-op campaign and lobby

**Input**: Design documents from `/specs/001-coop-campaign/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: Every user story ends with a rig verification task that produces evidence
(constitution Principle I). Paths are relative to `D:\Fable2-LAN\Fable-2-Recomp` unless they start
with `D:\`.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: US1 to US5 from spec.md
- **[JOSH]**: needs Josh (approval or two-PC test). Everything else is autonomous.
- When a task is done, tick it and append `→ result, evidence path`.

---

## Phase 1: Setup

- [x] T001 Create branch `coop/001-coop-campaign` from `systemlink` in `Fable-2-Recomp` and `thirdparty/rexglue-sdk`; commit `.specify/`, `.claude/skills/speckit-*`, `CLAUDE.md` and `specs/` as the first commit; push both branches to the WizitStrogg forks → done 2026-10-23: branches pushed to WizitStrogg forks (Fable-2-Recomp 8a511f9; SDK fast-forwarded to systemlink tip e23c647, the v13 source, pointer commit 8bf0c41)
- [x] T002 [P] Create `D:\Fable2-LAN\research\001-evidence\` with a `README.txt` index → done: `D:\Fable2-LAN\research\001-evidence\README.txt`
- [x] T003 [P] Write `tools/coop_diff.py` (diff two `COOPHERO` lines, exit 1 on difference) → done: field-by-field diff of v1/v2 lines (numbers by value), exit 0/1/2; self-tested
- [x] T004 [P] Write `D:\Fable2-LAN\tools\hash_slots.ps1`: SHA-256 of every file in every save slot of an instance, to a dated text file; compare mode → done: `hash` and `compare` modes; compare fails only on changes to pre-existing HeroNNN slots (new slots and profile/achievement files are notes); self-tested
- [x] T005 [P] Rig front-end driving: confirm the remote-control server and `tools/fable2_control.py` work for both instances (separate ports), so lobby flows can run with no human input → done: control server in rig builds (FABLE2_RIG_REMOTE_CONTROL); mod-b 8791 + mod-a 8792 driven together in launch 2; remote pad now sends XInput keystrokes (New Game needed them); tools/coop_rig.py. Evidence `D:\Fable2-LAN\research\001-evidence\launch2\launch2_results.txt`
- [x] T006 Back up rig saves to `D:\Fable2-LAN\backups\<time>-001-start\`; hash all solo slots of mod-a and mod-b (baseline for SC-003) → done: `D:\Fable2-LAN\backups\20261023-122842-001-start\` (mod-a/b/c saves, 42 files); baselines `D:\Fable2-LAN\research\001-evidence\hashes\mod-{a,b,c}-baseline-20261023-122842.txt`

- [x] T006a Graphics fixes (Josh, 2026-10-23, via the graphics thread): once the coordinator passes the systemlink merge id of WizitStrogg/rexglue-sdk PR #1 (7e3 clamp, depth_bias_shader comma list, memexport readback fix), merge systemlink into `coop/001-coop-campaign` in `thirdparty/rexglue-sdk`, update the pointer; in Fable-2-Recomp seed `depth_bias_shader = '014F8A02DB7B19CA,03D91B35ACD56079'` with `depth_bias_shader_units = 64` (`src/core/fable_2_app.h` dog_fur_depth_bias seeding, default `fable_2.toml`); rebuild; confirm on the rig the cvars are present and the game starts → code done 2026-10-23: SDK merge ababe08 (systemlink 5efa114 into coop/001); seeding now both hashes with 64; no readback_memexport_fast seeding existed in the game; build OK. NOTE: Josh's installed mod/fable_2.toml sets depth_bias_shader = '014F8A02DB7B19CA' / units 16 explicitly, which overrides the seeding; update or remove those two lines at the gated v14 install. Rig check pending (first rig run)

---

## Phase 2: Proofs and foundations (blocking)

**⚠️ CRITICAL**: No user story starts until the proofs are recorded in research.md. A failed proof
switches to its fallback; only a failed fallback is raised to Josh.

### Proofs

- [x] T007 Proof P1: function-trace diff of A on New Game and on Continue at the main menu; intercept each and resume the original; screenshots → P1 verified: hook FrontMenu_PressBitDispatch 0x82188F20; intercept + resume proven (see research.md Proof results). `D:\Fable2-LAN\research\001-evidence\T018\`, `D:\Fable2-LAN\research\001-evidence\proofs\P1\`
- [ ] T008 [P] Proof P2: fresh New Game on A, then `Gameflow:SetGameflowPosition(ScriptEnum.DebugQC060)`; compare with a reference QC060 state (position, age, titles, weapons, layers, chapter card); list gaps → (open) P2 attempt ran on an adult save by a navigation error; skip API works (restart + QC060_NewBeginning started); fresh New Game run still needed. `D:\Fable2-LAN\research\001-evidence\launch2\`
- [x] T009 [P] Proof P3: B at the main menu joins A's world through join-via-presence without loading a save; else build the hidden "lobby save" fallback and prove it → P3 verified: B joined from the main menu with F9, no save loaded; B's save unchanged. `D:\Fable2-LAN\research\001-evidence\launch2\p3_b_joined_frame.png`
- [x] T010 [P] Proof P4a: where a new game's slot folder is created and how the load screen selects a slot; read a test marker file at selection time → P4a verified: load = root Save OPEN_EXISTING; New Game creates next HeroNNN with CREATE_ALWAYS and writes a starting save at once. `D:\Fable2-LAN\research\001-evidence\proofs\RESULTS.md`
- [ ] T011 Proof P4b: find and hook the game's save routine on A; log every manual save and autosave with the slot path; prove the hook fires after the write completes → (partly) manual save verified: OPEN_ALWAYS, folder kept, close after last write = hook point; autosave not yet observed. `D:\Fable2-LAN\research\001-evidence\proofs\P4\`
- [ ] T012 [P] Proof P5: B picks up gold and A completes a quest with a gold reward; record where player 2's gold goes; test `Multiplayer.SetHenchmanGoldValue(0)` plus redirect to `Money.Modify(host, n)` → (partly) Money.Modify(p2,100) -> host +50, p2 +50 (henchman share 50/50); pickups/quest rewards not yet tested. `D:\Fable2-LAN\research\001-evidence\launch2\`
- [ ] T013 [P] Proof P6: kill B mid-session; find the partner-loss hook; trigger a save and return to the front end on A → (partly) loss detected ~3.5 s, host calls XSessionLeaveRemote, stays in world; save + return to lobby not yet proven. `D:\Fable2-LAN\research\001-evidence\launch2\p6_a_log_after_kill.txt`
- [x] T014 [P] Proof P7: zip a slot, measure size, send it over a test TCP connection between A and B, verify hashes → P7 verified on loopback (ZeroTier unverified). `D:\Fable2-LAN\research\001-evidence\proofs\P7\p7_run1.txt`
- [ ] T015 Write all proof results and chosen fallbacks into `specs/001-coop-campaign/research.md`; post one milestone message to the project thread

### Foundations

- [x] T016 cvar `coop_campaign` (default false) and `proto` entry `campaign` in `src/core/fable2_coop_campaign.h` → done: cvar coop_campaign (default off) + proto COOPPROTO:2|campaign published (log line in `D:\Fable2-LAN\research\001-evidence\T018\launch1_log.txt`)
- [ ] T017 Extend `CoopHero.Capture`/`Apply` in `data/scripts/recomp/coop_lib.lua` to profile v2 (format in `specs/002-player2-kit/data-model.md`): XP pools, ability levels, items, worn and wielded, titles → (code done, rig check pending: luafile coop_capture2)
- [x] T018 Front-end intercept in `src/core/fable2_coop_frontend.h` per P1: New Game shows Solo / Co-op (Solo = original handler), Continue checks the selected slot for a marker → done, verified on the rig: New Game -> Solo / Co-op / Back; Solo -> stock hero cards; Co-op -> host lobby; B returns. `D:\Fable2-LAN\research\001-evidence\T018\launch1_02_chooser_screen.png`, `launch1_03_solo_hero_cards_frame.png`, `D:\Fable2-LAN\research\001-evidence\launch2\t018_a_host_lobby_screen.png`
- [ ] T019 Lobby overlay and state machine in `src/core/fable2_coop_lobby.h` per `contracts/lobby-protocol.md`, with pad and keyboard navigation → (partly) chooser and host lobby panel verified on the rig; partner side (seat, ready, coop_go) not yet run. `D:\Fable2-LAN\research\001-evidence\launch2\t018_a_host_lobby_screen.png`

**Checkpoint**: proofs answered; Solo still starts the normal game (rig screenshot)

---

## Phase 3: User Story 1 - Start a co-op campaign together (Priority: P1) 🎯 MVP

**Independent Test**: rig: A creates, B joins, both arrive at the adult start, B's gender as chosen, marker on A, solo slots unchanged

- [ ] T020 [US1] Lobby `new` flow: host advertises, partner lists and seats, gender and childhood choices, ready states, host-only Start (`fable2_coop_lobby.h`)
- [ ] T021 [US1] Start: host runs the original New Game, then the skip to adulthood (`data/scripts/recomp/coop_campaign.lua`) per P2, with defaults for anything the skip misses
- [ ] T022 [US1] Partner join after `coop_go` via the P3 path; player 2 spawned as `CreatureHero`/`CreatureHeroFemale` per the chosen gender (v13 selector override)
- [ ] T023 [US1] Write `coop_campaign.toml` after the first successful save with both in the world (`fable2_coop_campaign.h`)
- [ ] T024 [US1] Rig verification, 3 runs: full flow driven by `fable2_control.py`; screenshots of lobby and both worlds; marker contents; solo slot hashes unchanged

**Checkpoint**: US1 proven

---

## Phase 4: User Story 2 - Only played together (Priority: P1)

**Independent Test**: A alone waits in the lobby; B rejoins from Continue; B killed → A saves and returns to the lobby

- [ ] T025 [US2] Continue on a campaign slot opens the lobby in `continue` mode and waits for the named partner; Back returns to the menu
- [ ] T026 [US2] Partner's Continue → Co-op lists waiting campaigns for this partner; join flow as T022
- [ ] T027 [US2] Partner loss per P6: host saves, both return to the lobby; host loss: partner returns to the menu with a message
- [ ] T028 [US2] Refuse strangers: session not joinable by anyone but the partner; lobby rejects other seats
- [ ] T028a [US2] A join must never crash the host (coordinator, 2026-10-23): rig launch 9 crashed the host ("Unhandled guest access violation: write of guest 0x4EADC200", `mod-a\logs\fable_2_037.log` 16:10:18) when the partner joined during QC060's opening (autosave + movie + co-op screen). (1) Enable the AV probe log on the rig (`src/diagnostics/fable2_av_probe.h`, file output is commented out) and name the faulting guest function. (2) Host keeps its session non-joinable, or queues the join, while a movie, cutscene, loading screen or save runs (find the game's own state for these; candidates: GUI.IsScreenFullyFaded, the movie player, the [content] save bracket). (3) Rig proof: partner joins during a cutscene and during a save; host survives and the join completes afterwards. Applies to rejoins after a drop too.
- [ ] T029 [US2] Rig verification: A alone cannot enter; B rejoins; kill B mid-session (3 times); third instance `mod-c` cannot join

**Checkpoint**: US2 proven

---

## Phase 5: User Story 3 - Player 2's hero carries on (Priority: P1)

**Independent Test**: B's gains survive save, quit and reload, and a crash right after saving

- [ ] T030 [US3] Save hook (P4b): capture player 2 and write `coop_player2.txt` with `saveuid` by temp + rename (`fable2_coop_campaign.h`)
- [ ] T031 [US3] On load: read `coop_player2.txt`, check `saveuid`, apply to player 2 at spawn and after each respawn (extend `fable2_coop_hero.h`)
- [ ] T032 [US3] Rig verification, 5 runs: XP, item, clothing change; save; reload; diff captures; one run killing A right after the save

**Checkpoint**: US3 proven

---

## Phase 6: User Story 4 - One shared purse (Priority: P2)

**Independent Test**: B's pickup raises the shared total on both screens and in the save

- [ ] T033 [US4] Purse per P5: player 2's gold gains go to the host purse; player 2 gold held at 0; both HUDs show the total
- [ ] T034 [US4] Rig verification: pickups by both, a quest reward, a purchase by A; totals exact on both screens and after reload
- [ ] T035 [US1-4] F6 box "Co-op campaign"; `COOP-TESTS.txt` v14 steps; release summary with evidence paths
- [ ] T036 [JOSH] Gate 2 for v14: Josh approves install into `D:\Fable2-LAN\mod` and friend zip `Fable2-LAN-friend-v14.zip`
- [ ] T037 Install v14 (no game running), build friend zip, post release message
- [ ] T038 [JOSH] Josh and Calvin create a campaign and play two sessions on two PCs; findings become new tasks here

**Checkpoint**: v14 released (US1 to US4)

---

## Phase 7: User Story 5 - The partner keeps a backup (Priority: P3)

- [ ] T039 [US5] Backup sender and receiver per P7 and `contracts/campaign-files.md` (`src/core/fable2_coop_backup.h`); F6 shows the latest backup date
- [ ] T040 [US5] Rig verification: hash-equal copy on B after each session; last 10 kept
- [ ] T041 [JOSH] Gate 2 for v14.1; install and friend zip after approval

---

## Phase 8: Polish

- [ ] T042 [P] Update `FUNCTION_NAMES.md` with every hooked or called game function
- [ ] T043 [P] Update friend-package README and F6 help text
- [ ] T044 Two-hour stability run on the rig with both players active; solo slots re-hashed
- [ ] T045 [JOSH] Merge `coop/001-coop-campaign` into `systemlink` in both repos after approval
- [ ] T046 Update `specs/README.md`; run `/speckit-plan` and `/speckit-tasks` for 002 and post them for Gate 1

---

## Dependencies & Execution Order

- Phase 1 → Phase 2 → US1 → US2 → US3 → US4 → v14 release → US5.
- US3's save hook (T030) depends on P4b; it can be built in parallel with US2 once P4b passes.
- Gates (T036, T041, T045) wait for Josh; meanwhile continue with the next phase or with 002's
  planning.

## Parallel Opportunities

- T002 to T005 together; proofs T008, T009, T010, T012, T013, T014 as separate rig runs.
