# Implementation Plan: Full hero, host-run

**Branch**: `coop/001-full-hero` | **Date**: 2026-10-23 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/001-full-hero/spec.md`

## Summary

Player 2 in the host's world becomes the joiner's complete hero (stats, XP, abilities, every
item, gold), the joiner manages it from a co-op menu on their own PC, and a session ledger writes
every change back to the joiner's save exactly once. Technical approach (see
[research.md](research.md)): the joiner captures a v2 hero profile in its own world; the host
applies it to player 2 with the game's Lua API; commands and player-2 snapshots travel over the
existing LAN lobby keyed-data channel with sequence numbers; the ledger is "baseline + latest
snapshot", applied as deltas on the joiner's next load. Six yes/no proofs run first.

## Technical Context

**Language/Version**: C++20 (recomp app and rexglue-sdk runtime, LLVM clang on Windows); Lua 5.1
(the game's VM, run through the recomp Lua runner)

**Primary Dependencies**: rexglue-sdk (`rexruntime.dll`: LAN lobby, xam, `rex::ui` overlay,
cvars), Fable-2-Recomp app (`src/core/fable2_coop_*.h`, function overrides), game Lua API
(`FABLE2_LUA_API.md`)

**Storage**: Files next to the save: `saves\coop\ledger-<partner>-<time>\baseline.txt`,
`latest.txt`, `applied.txt`; backups in `D:\Fable2-LAN\backups\`

**Testing**: Two-instance rig on Josh's PC (`D:\Fable2-LAN\mod-a` host at 127.0.0.1, `mod-b`
joiner at 127.0.0.2, cvar `network_isolate`), driven by `D:\Fable2-LAN\tools\gamectl.ps1`;
captures with `luafile coop_capture`; save diffs with `research\f2save.py`; a new
`tools\coop_diff.py` for profile diffs. Two-PC acceptance by Josh and Calvin via `COOP-TESTS.txt`.

**Target Platform**: Windows 10/11 x64, two PCs on LAN or ZeroTier

**Project Type**: Game mod (static recompilation app + runtime library)

**Performance Goals**: Command result visible on both screens within 1 s; player-2 capture under
2 ms on the game thread; no frame-time regression above 1 ms average with the feature on

**Constraints**: Joiner runs no game Lua while visiting (joiner-side logic is C++ or overlay
UI); game Lua only on the game thread; saves never written outside the ledger apply; every
behaviour behind cvars, default off

**Scale/Scope**: 2 players; hero inventories up to a few hundred items; sessions up to several
hours

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | How this plan complies | Status |
| --- | --- | --- |
| I. Proven before shipped | Proofs P1 to P6 first; each user story ends at a rig checkpoint with evidence in `research\001-evidence\`; two-PC steps added to `COOP-TESTS.txt` | Pass |
| II. Saves are sacred | Rig uses copied saves; ledger applied through Lua on load after a slot backup; host save diffed every run; direct save edits only if P6 fails, with approval | Pass |
| III. Safe installs | Install script checks for running `fable_2.exe`; `mod` install and friend zip only after Gate 2 | Pass |
| IV. Opt-in and reversible | cvars `coop_full_hero`, `coop_menu`, `coop_ledger`, default off; v13 fallback when partner protocol < 2 | Pass |
| V. One owner per state | Host simulates player 2 and is the writer of its state; the joiner only sends requests; the ledger is written only from host snapshots | Pass |
| VI. Smallest hook | Lua API for apply, capture and abilities; one native call for "use item" (P4) only because no Lua API exists | Pass |
| VII. Autonomy with checkpoints | Gate 1 after this plan; Gate 2 per release (v14, v15, v16) | Pass |

Re-check after Phase 1 design: still passes. The co-op menu runs on the joiner without game Lua
(overlay reading `p2state`), consistent with the joiner constraint.

## Project Structure

### Documentation (this feature)

```text
specs/001-full-hero/
├── spec.md              # Feature spec
├── plan.md              # This file
├── research.md          # Phase 0: decisions D1-D9, proofs P1-P6
├── data-model.md        # Phase 1: profile v2, command, snapshot, ledger
├── quickstart.md        # Phase 1: how to run and verify on the rig and on two PCs
├── contracts/
│   ├── coop-channel.md  # Keys, message formats, versioning on the lobby channel
│   └── ledger.md        # Ledger files and apply rules
├── checklists/
│   └── requirements.md  # Spec quality checklist
└── tasks.md             # Phase 2: task list
```

### Source Code (repository root)

```text
Fable-2-Recomp/
├── src/core/
│   ├── fable2_coop_hero.h        # existing: profile publish/apply; extend to v2 + snapshots
│   ├── fable2_coop_channel.h     # NEW: seq/ack command queue over lobby keyed data
│   ├── fable2_coop_ledger.h      # NEW: baseline/latest files, apply on load, backups
│   ├── fable2_coop_menu.h        # NEW: co-op menu overlay (rex::ui), pad + keyboard
│   ├── fable2_coop_actions.h     # NEW: host-side command handlers (Lua chunks + native use-item)
│   └── fable2_hooks.cpp          # existing: register new modules
├── data/scripts/recomp/
│   ├── coop_lib.lua              # existing: CoopHero.Capture/Apply; extend to v2
│   ├── coop_actions.lua          # NEW: Equip/Unequip/LearnAbility/Reconcile helpers
│   └── coop_ledger_apply.lua     # NEW: apply deltas in the joiner's own world
└── tools/
    └── coop_diff.py              # NEW: diff two COOPHERO v2 captures

thirdparty/rexglue-sdk/ (rexglue-sdk, branch coop/001-full-hero)
└── src/kernel/xam/xam_lan_lobby.cpp   # only if P1 shows the value size limit must change

D:\Fable2-LAN\ (not in git)
├── tools\gamectl.ps1, make_test_instances.ps1   # rig control (existing)
├── research\001-evidence\                        # screenshots, logs, diffs
└── COOP-TESTS.txt                                # two-PC test steps for Josh and Calvin
```

**Structure Decision**: Game-side logic stays in Fable-2-Recomp as header modules beside the
existing `fable2_coop_*.h`, Lua helpers in `data/scripts/recomp/`. The SDK changes only if
Proof P1 requires it.

## Releases

| Release | Contains | User stories |
| --- | --- | --- |
| v14 | Complete hero on player 2 + ledger (gains come home) | US1, US2 |
| v15 | Co-op menu: inventory, equip, consumables | US3 |
| v16 | Ability levelling + body shape | US4, US5 |

## Risks

- P2 or P4 fail: ability levels or consumables need native work; adds about a week each.
- P5 shows the game writes gold/XP into `mainsave.bin` in a way the ledger cannot reconcile:
  neutralise the game's write-back for those fields (function override on the write-back path).
- Lobby value size too small for a 300-item snapshot: parts, or move snapshots to a dedicated
  socket early (pulls some of 003/004's channel work forward).

## Complexity Tracking

No constitution violations to justify.
