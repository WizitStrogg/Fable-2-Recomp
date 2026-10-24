---

description: "Task list for 001 Full hero, host-run"
---

# Tasks: Full hero, host-run

**Input**: Design documents from `/specs/001-full-hero/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: Every user story ends with a rig verification task that produces evidence
(constitution Principle I). Paths are relative to `D:\Fable2-LAN\Fable-2-Recomp` unless they start
with `D:\`.

**Organization**: Tasks are grouped by user story so each story can be proven and released on its
own.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: US1 to US5 from spec.md
- **[JOSH]**: needs Josh (approval or two-PC test). Everything else is autonomous.
- When a task is done, tick it and append `→ result, evidence path`.

---

## Phase 1: Setup

**Purpose**: Branches, evidence folders, tooling

- [ ] T001 Create branch `coop/001-full-hero` from `systemlink` in `Fable-2-Recomp` and in `thirdparty/rexglue-sdk`; push both to the WizitStrogg forks
- [ ] T002 [P] Create `D:\Fable2-LAN\research\001-evidence\` and a `README.txt` index of evidence files
- [ ] T003 [P] Write `tools/coop_diff.py`: parse two `COOPHERO` lines (v1 or v2), print field differences, exit code 1 on difference
- [ ] T004 [P] Add a rig helper `D:\Fable2-LAN\tools\rig_cycle.ps1`: check no game running, install build into mod-a/mod-b, start both, wait for load, run a list of console commands, capture both windows
- [ ] T005 Back up the rig saves (`mod-a\saves`, `mod-b\saves`) to `D:\Fable2-LAN\backups\<time>-001-start\`

---

## Phase 2: Proofs and foundations (blocking)

**Purpose**: Answer research.md P1 to P6, then build the pieces every story uses

**⚠️ CRITICAL**: No user story starts until the proofs are recorded in research.md. A failed proof
switches to its fallback; only a failed fallback is raised to Josh.

### Proofs (each: rig experiment, evidence, result written into research.md)

- [ ] T006 [P] Proof P1: publish values of 1, 2, 4, 8, 16 KB on key `probe` and read them on the peer; time one `CoopHero.Capture` of a 100+ item hero on the game thread
- [ ] T007 [P] Proof P2: on A, `Stats.SetHeroAbilityLevel(player2, Fireball, 1 then 3)`; B casts each; capture both screens and damage numbers
- [ ] T008 [P] Proof P3: on A, `Money.Modify(player2, 1000)`; check whether A's own gold changes
- [ ] T009 Proof P4: find the native use-item action (`strxref.py` on `GUI_SCREEN_INVENTORY_*`, function trace while drinking a potion on A); call it for player 2 with a health potion; record function address and signature
- [ ] T010 [P] Proof P5: B joins A with known gains (pick up gold and XP), leaves; unpack B's `herosave.bin` and `mainsave.bin` with `research\f2save.py`; list exactly which fields the game wrote
- [ ] T011 [P] Proof P6: on a copied B save, apply a hand-written ledger (+1000 Will XP, +1 Rusty Cleaver, -1 health potion, Fireball 2) with a draft `coop_ledger_apply.lua`; save, reload, capture, diff
- [ ] T012 Write the proof results and chosen fallbacks into `specs/001-full-hero/research.md`; post one milestone message to the project thread

### Foundations

- [ ] T013 Extend `CoopHero.Capture` / `Serialize` / `Parse` in `data/scripts/recomp/coop_lib.lua` to `COOPHERO:v2` (data-model.md), keeping v1 output for v13 partners
- [ ] T014 [P] Add cvars `coop_full_hero`, `coop_menu`, `coop_ledger` (default false) and the `proto` key publisher in `src/core/fable2_coop_hero.h`
- [ ] T015 Implement the command channel in `src/core/fable2_coop_channel.h` per `contracts/coop-channel.md` (seq queue, ack, parts for large values per P1, busy retry)
- [ ] T016 Implement host snapshots: capture player 2 after each applied command and every 2 s, publish `p2state` with `rev`, in `src/core/fable2_coop_hero.h`
- [ ] T017 Rig check of the foundations: B sends 50 `refresh` commands while A is busy loading; all acked once, in order; `p2state.rev` strictly increases (log excerpt to evidence)

**Checkpoint**: proofs answered, channel and snapshots proven on the rig

---

## Phase 3: User Story 1 - Your hero arrives complete (Priority: P1) 🎯 MVP

**Goal**: Player 2 on the host is the joiner's complete hero; the host is untouched

**Independent Test**: B's own capture vs A's player-2 capture: no differences; A's hero and save unchanged

- [ ] T018 [US1] Extend `CoopHero.Apply` to v2: XP pools, ability levels, titles (with `AddUnlockedTitle` suppressed as in v13), gold per P3 result
- [ ] T019 [US1] Inventory reconcile in `data/scripts/recomp/coop_actions.lua`: make player 2's items equal the profile's list (add missing, remove extras), skip unknown record ids with a notice
- [ ] T020 [US1] Equip weapons and clothing after reconcile; re-apply after every player-2 respawn (existing spawn epoch in `fable2_coop_hero.h`)
- [ ] T021 [US1] Version fallback: if the partner's `proto` lacks `full_hero`, use the v13 path only
- [ ] T022 [US1] Rig verification, 3 runs: `coop_diff.py` B-own vs A-player-2 (no differences); A hero and A save item count unchanged; B casts its own Fireball level; screenshots and diffs to evidence
- [ ] T023 [US1] Rig verification of the fallback: B on v13 joins A on the new build; v13 behaviour, no errors

**Checkpoint**: US1 proven on the rig (not released yet; it ships with US2)

---

## Phase 4: User Story 2 - Your progress comes home (Priority: P1)

**Goal**: Every change to player 2 is saved to the joiner's own save, once, crash-safe

**Independent Test**: gains in A's world appear in B's own save after reload; a killed session still applies; nothing applied twice

- [ ] T024 [US2] Ledger writer in `src/core/fable2_coop_ledger.h`: `meta.txt`, `baseline.txt`, `herosave-before.bin` at join; atomic `latest.txt` on each `p2state.rev`; `closed` at session end (contracts/ledger.md)
- [ ] T025 [US2] Ledger apply in `data/scripts/recomp/coop_ledger_apply.lua` + trigger after own-save load in `fable2_coop_ledger.h`: backup slot, compute deltas, reconcile with the game's write-back (P5), apply, re-capture, write `applied.txt`, move folder
- [ ] T026 [US2] Neutralise or reconcile the game's own visitor gold/XP write-back per P5 (function override only if reconcile cannot work)
- [ ] T027 [US2] Rig verification: session with XP, gold, item pickup and morality change; reload B; diff expected vs actual (3 runs)
- [ ] T028 [US2] Rig verification: kill B mid-session; reload; ledger applied up to the last `rev`; load again, nothing applied twice
- [ ] T029 [US2] Rig verification: A's save diff unchanged across all runs above
- [ ] T030 [US2] F6 menu boxes "Full hero" and "Save my progress from co-op"; update `COOP-TESTS.txt` with v14 steps; write release summary
- [ ] T031 [JOSH] Gate 2 for v14: Josh approves install into `D:\Fable2-LAN\mod` and friend zip `Fable2-LAN-friend-v14.zip`
- [ ] T032 Install v14 (no game running), build friend zip, post release message with evidence paths
- [ ] T033 [JOSH] Josh and Calvin run the v14 steps of `COOP-TESTS.txt`; fix findings as new tasks here

**Checkpoint**: v14 released (US1 + US2)

---

## Phase 5: User Story 3 - Manage your kit from your own menu (Priority: P2)

**Goal**: Co-op menu on the joiner: inventory, equipment, consumables, without pausing

**Independent Test**: B equips a sword and drinks a potion from the menu; visible on both screens within 1 s; ledger records both

- [ ] T034 [P] [US3] Co-op menu overlay in `src/core/fable2_coop_menu.h`: tabs Inventory, Equipment, Hero; reads `p2state`; key F7 and pad Back+Y (cvars); pad navigation if `rex::ui` lacks it
- [ ] T035 [P] [US3] Host handlers in `src/core/fable2_coop_actions.h` + `coop_actions.lua`: `equip`, `unequip`, `dye`, `hair`
- [ ] T036 [US3] Host handler `use` via the native use-item action found in P4 (or the scripted fallback)
- [ ] T037 [US3] Rejection reasons shown in the menu (`questitem`, `noitem`, `busy`)
- [ ] T038 [US3] Rig verification: equip hat, swap sword, drink potion while hurt, eat food (fat change); timings under 1 s; both screens; ledger contents; A's world keeps running while the menu is open
- [ ] T039 [US3] F6 box "Co-op menu"; `COOP-TESTS.txt` v15 steps; release summary
- [ ] T040 [JOSH] Gate 2 for v15
- [ ] T041 Install v15 and build friend zip; release message
- [ ] T042 [JOSH] Two-PC test of v15

**Checkpoint**: v15 released (US3)

---

## Phase 6: User Story 4 - Level up your own abilities (Priority: P2)

**Goal**: Spend own XP on abilities with the game's costs and limits

**Independent Test**: B learns Fireball 2; Will XP drops by the game's cost; player 2 casts level 2; B's save has it after the session

- [ ] T043 [US4] Read the game's ability cost and max-level tables (the abilities screen's data, `GUI_SCREEN_ABILITIES_*` handler `sub_8259EF38`; `GetHeroAbilityMaximumLevel`) and document them in research.md
- [ ] T044 [US4] Abilities tab in the co-op menu: per ability current level, next cost, pool, Learn button
- [ ] T045 [US4] Host handler `learn`: check cost and limit, deduct XP from player 2, set level; reasons `noxp`, `maxlevel`
- [ ] T046 [US4] Rig verification: learn Fireball 2 and a Strength ability; cost matches the game's own screen for the same hero in single player; combat shows the new level; ledger and reload correct

**Checkpoint**: US4 proven (ships in v16 with US5)

---

## Phase 7: User Story 5 - Your body looks right (Priority: P3)

**Goal**: Player 2 shows the joiner's fat, strength, height, tattoos

**Independent Test**: max-fat hero is visibly fat on both screens

- [ ] T047 [US5] Find where hero morph weights reach the mesh (start from `plans/hero-dog-realtime-texture-morphing.md` and the `Stats.ModifyFat` path); trigger it for player 2 after apply
- [ ] T048 [US5] Rig verification: Calvin-style max-fat hero side by side with its own world; tattoos if available
- [ ] T049 [US4] [US5] `COOP-TESTS.txt` v16 steps; release summary
- [ ] T050 [JOSH] Gate 2 for v16
- [ ] T051 Install v16 and build friend zip; release message
- [ ] T052 [JOSH] Two-PC test of v16

**Checkpoint**: v16 released (US4 + US5)

---

## Phase 8: Polish

- [ ] T053 [P] Update `FUNCTION_NAMES.md` with every game function this feature hooks or calls
- [ ] T054 [P] Update `D:\Fable2-LAN\friend-package\README` and the F6 help text
- [ ] T055 Two-hour stability run on the rig with both players active (no crash, no errors, both saves diffed)
- [ ] T056 Merge `coop/001-full-hero` into `systemlink` in both repos after Josh's approval of v16 [JOSH]
- [ ] T057 Update `specs/README.md` status and the estimate for 002

---

## Dependencies & Execution Order

- Phase 1 → Phase 2 (proofs, then foundations) → US1 → US2 → release v14.
- US3 and US4 both need v14's channel, snapshots and ledger; US3 before US4 because US4 reuses
  the menu. US5 is independent of US3/US4 after US1 and can run in parallel with them.
- Gates: T031, T040, T050, T056 wait for Josh; while waiting, continue with the next story's
  non-release tasks.
- Josh's two-PC tests (T033, T042, T052) can overlap with the next phase; their findings become
  new tasks inserted before the next release.

## Parallel Opportunities

- T002, T003, T004 together.
- Proofs T006, T007, T008, T010, T011 together (separate rig runs); T009 needs a trace session.
- T034 (menu UI) and T035 (host handlers) together.
