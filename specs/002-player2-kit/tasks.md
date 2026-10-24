# Tasks: Player 2's full kit

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/coop-channel.md](contracts/coop-channel.md)

**Status**: Ready for Josh's review (Gate 1). Nothing below is started.

**Evidence**: `D:\Fable2-LAN\research\002-evidence\`. Each task is ticked with its result and an
evidence path; one commit per task id.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency)
- **[Story]**: US1 fair XP, US2 kit menu, US3 abilities, US4 body
- **[JOSH]**: needs Josh

---

## Phase 1: Setup

- [ ] T101 Create `coop/002-player2-kit` from the merged 001 (`systemlink` after T045) in both repos
- [ ] T102 [P] `D:\Fable2-LAN\research\002-evidence\` with a README index; back up rig saves; hash solo slots
- [ ] T103 [P] cvar gate: `proto` feature `kit`; everything below runs only when both PCs list it and a campaign is hosted

## Phase 2: Proofs (blocking)

- [ ] T104 Proof X1: function-trace diff while the host and then player 2 collect strength, skill and will orbs and a quest gives XP; name the native XP credit function, its arguments (entity, type, amount) and its callers
- [ ] T105 [P] Proof X2: does the game already drop typed orbs per killer (sword kill by A: strength orbs; Fireball kill by B: will orbs)? Record what player 2's pools receive today
- [ ] T106 [P] Proof C1: trace the native inventory screen's use-item action (health potion) on the host; call it for player 2's entity on the game thread; read back health and the potion count
- [ ] T107 [P] Proof A1: the abilities screen's learn path (`sub_8259EF38` area): cost per level, max level, the XP pool charged; call it for player 2; read back level and XP
- [ ] T108 [P] Proof A2: a level learned on player 2 changes the real spell (Fireball 2 damage/area) on the host
- [ ] T109 [P] Proof P1: `p2state` size with a full inventory against the lobby value limit (60 KB); split into parts if needed
- [ ] T110 Write proof results and chosen fallbacks into `research.md`; milestone message

## Phase 3: User Story 1 - Fair XP (P1) 🎯 v15

- [ ] T111 [US1] `src/core/fable2_coop_xp.h`: campaign-only strong override of the X1 credit function: typed XP to the earner, the same amount as general XP to the partner; general and quest XP 50/50; `COOPXP:v1` line per award (data-model.md)
- [ ] T112 [US1] Earner when the orb is not tied to a pickup by the earner (X2 result): the killing blow's entity; objects and general orbs 50/50
- [ ] T113 [US1] XP audit: sum the session's `COOPXP` lines into `coop_xp_log.txt` in the campaign slot at each save (through 001's save hook)
- [ ] T114 [US1] Rig verification: A 5 melee kills, B 5 Fireball kills, one quest reward; strength XP only to A, will XP only to B, equal totals; log matches the screens; after save + reload unchanged

## Phase 4: User Story 2 - Kit menu (P1) v15.1

- [ ] T115 [US2] Channel per `contracts/coop-channel.md`: joiner `cmd` queue (seq, resend until acked), host apply-once in order, `ack`; `p2state` after each command and every 2 s (`fable2_coop_kit.h`, `fable2_coop_kitmenu.h`)
- [ ] T116 [US2] Host apply: equip/unequip (weapon set manager, appearance manager), dye, hair; each confirmed by reading player 2 back and retried while busy or not yet applied (001 T021a lesson), then acked `ok` or `no:<reason>`
- [ ] T117 [US2] Host apply: `use` through the C1 native action; quest items rejected with a reason
- [ ] T118 [US2] Joiner overlay `coop_kit_panel.cpp` (F7, pad Back+Y): Inventory, Equipment, Hero tabs from `p2state`; never pauses; item names from the game's string table
- [ ] T119 [US2] Rig verification: B equips a different sword and a hat, drinks a health potion, tries a quest item; both screens within 1 s; counts right; save + reload keeps it

## Phase 5: User Story 3 - Abilities (P1) v15.1

- [ ] T120 [US3] `learn` command through the A1 native learn path (costs and limits from the game); Abilities tab shows pools, levels, costs
- [ ] T121 [US3] Rig verification: B learns Fireball 2 with enough Will XP (XP drops by the game's cost, player 2 casts level 2), is refused without enough XP; after save + reload still level 2

## Phase 6: User Story 4 - Body (P3) v15.2

- [ ] T122 [US4] Proof M1: where morph weights reach the mesh (fat, strength, height, age); trigger it for player 2 after apply (`fable2_coop_morph.h`)
- [ ] T123 [US4] Rig verification: player 2 at max fat is visibly fat on both screens

## Phase 7: Release

- [ ] T124 Update `FUNCTION_NAMES.md`, F6 help, friend README, `COOP-TESTS.txt` steps for v15
- [ ] T125 30-minute mixed-combat rig session: SC-001 (equal XP totals, no typed XP to the wrong hero)
- [ ] T126 [JOSH] Gate 2 for v15 / v15.1 / v15.2: install and friend zip after approval
- [ ] T127 [JOSH] Josh and Calvin play the COOP-TESTS steps on two PCs (SC-004)

---

## Dependencies & Execution Order

- 001 merged (T045) before T101. Proofs (Phase 2) before their stories: X1/X2 → US1; C1 → T117;
  A1/A2 → US3; M1 → US4. T115 (channel) before T116-T121.
- US1 (v15) can ship alone. US2 and US3 ship together (v15.1). US4 is independent.

## Parallel Opportunities

T105-T109 are independent proofs on the rig (one batched run where possible). T118 (overlay) can
be built beside T116/T117 once the channel (T115) is defined.
