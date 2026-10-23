# Implementation Plan: Co-op campaign and lobby

**Branch**: `coop/001-coop-campaign` | **Date**: 2026-10-23 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/001-coop-campaign/spec.md`

## Summary

Add a dedicated co-op campaign: New Game offers Solo or Co-op; Co-op opens a lobby overlay where
the partner joins, picks a gender, and the host picks "skip to adulthood"; Start creates a new save
slot with a campaign marker and brings both into the world. The campaign only loads with both
players. Player 2's hero is saved in the campaign slot in the same save event as the world, gold
is one shared purse, and the partner receives a backup after each session. Technical approach
([research.md](research.md)): intercept the main menu's New Game and Continue, a `rex::ui` lobby
overlay over the existing LAN lobby channel, the game's own gameflow skip, a hook on the game's
save routine, and the v13 player-2 apply path extended to a full profile. Seven proofs first.

## Technical Context

**Language/Version**: C++20 (recomp app, rexglue-sdk), Lua 5.1 (game VM via the recomp Lua runner)

**Primary Dependencies**: rexglue-sdk (LAN lobby, discovery, join-via-presence, `rex::ui`,
cvars, sockets), Fable-2-Recomp (function overrides, `fable2_coop_*.h`), game Lua API

**Storage**: Host save slot `saves\<XUID>\4D5307F1\00000001\HeroNNN\` plus `coop_campaign.toml` and
`coop_player2.txt`; partner backups in `<exe dir>\coop-backups\<campaign id>\`

**Testing**: Two-instance rig (`mod-a` host 127.0.0.1, `mod-b` partner 127.0.0.2), remote-control
server (`tools/fable2_control.py`) to drive the front end, `tools\gamectl.ps1`, captures and
`tools/coop_diff.py`, slot hashes for the "solo saves unchanged" check

**Target Platform**: Windows 10/11 x64, two PCs on LAN or ZeroTier

**Project Type**: Game mod (static recompilation app + runtime library)

**Performance Goals**: Lobby discovery under 10 s; main menu to both in world under 2 minutes;
save hook adds under 50 ms to a save

**Constraints**: Joiner runs no game Lua while visiting; game Lua only on the game thread; solo
saves never written; everything behind `coop_campaign` (default off)

**Scale/Scope**: 2 players per campaign; any number of campaigns per pair

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | How this plan complies | Status |
| --- | --- | --- |
| I. Proven before shipped | Proofs P1 to P7 first; each story ends at a rig checkpoint with evidence in `research\001-evidence\`; two-PC steps in `COOP-TESTS.txt` | Pass |
| II. Saves are sacred | Co-op writes only campaign slots; solo slots hashed before and after every rig run; player 2 written in the same save event as the world | Pass |
| III. Safe installs | Install script checks for a running `fable_2.exe`; `mod` install and friend zip after Gate 2 | Pass |
| IV. Opt-in and reversible | `coop_campaign` cvar default off; Solo path is the untouched original handler | Pass |
| V. One owner per state | Host owns world, story and purse; player 2's hero stored by the host in the campaign | Pass |
| VI. Smallest hook | Overlay instead of new Gameface screens; the game's own gameflow skip; one save-routine hook | Pass |
| VII. Autonomy with checkpoints | Gate 1 after this plan; Gate 2 for the v14 release | Pass |

Re-check after Phase 1 design: passes.

## Project Structure

### Documentation (this feature)

```text
specs/001-coop-campaign/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── lobby-protocol.md
│   └── campaign-files.md
├── checklists/requirements.md
└── tasks.md
```

### Source Code (repository root)

```text
Fable-2-Recomp/
├── src/core/
│   ├── fable2_coop_frontend.h    # NEW: New Game / Continue intercept, Solo pass-through
│   ├── fable2_coop_lobby.h       # NEW: lobby overlay (rex::ui) and lobby state machine
│   ├── fable2_coop_campaign.h    # NEW: marker, slot detection, save hook, player-2 file, purse
│   ├── fable2_coop_backup.h      # NEW: zip + send/receive campaign backups
│   ├── fable2_coop_hero.h        # existing: player-2 apply; extend to full profile
│   └── fable2_hooks.cpp          # existing: register new modules
├── data/scripts/recomp/
│   ├── coop_lib.lua              # existing: CoopHero.Capture/Apply; v2 profile
│   └── coop_campaign.lua         # NEW: skip to adulthood, purse helpers
└── tools/coop_diff.py            # NEW (shared with 002)

thirdparty/rexglue-sdk/
└── src/kernel/xam/xam_lan_lobby.cpp, xam_session.cpp   # lobby advertisement fields, join-from-menu if P3 needs it
```

**Structure Decision**: Game-side modules beside the existing `fable2_coop_*.h`; SDK changes only
for lobby advertisement and, if P3 requires, joining from the front end.

## Releases

| Release | Contains | User stories |
| --- | --- | --- |
| v14 | Co-op campaign: lobby, skip to adulthood, both-players-only, player 2 persists, shared purse | US1 to US4 |
| v14.1 | Backup to partner | US5 |

## Risks

- P3 fails and the fallback "lobby save" path is fragile: adds about a week.
- P4b: the save routine is hard to hook cleanly: fall back to writing player 2 on a timer and on
  quit, and accept that a crash can lose up to a minute of player 2 progress (needs Josh's OK).
- P2 shows the skip misses state a real childhood sets: add scripted defaults; Rose/Sparrow
  childhood play comes later (feature 005).

## Complexity Tracking

No constitution violations to justify.
