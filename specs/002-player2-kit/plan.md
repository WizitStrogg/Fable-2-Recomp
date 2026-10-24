# Implementation Plan: Player 2's full kit

**Branch**: `coop/002-player2-kit` (from `coop/001-coop-campaign` once 001 is merged) | **Date**: 2026-10-24 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/002-player2-kit/spec.md`

**Status**: Ready for Josh's review (Gate 1). No 002 implementation before Josh says go.

## Summary

Give player 2 what player 1 has for managing a hero, inside a co-op campaign: fair XP (typed XP
to the earner, the same amount as general XP to the partner, quest and general XP split 50/50),
inventory and equipment management, consumables, and ability levelling, all from player 2's own
PC, plus a body that shows fat, strength and height. Josh's direction (2026-10-23/24): patch the
game's own code instead of working around it. So XP is routed at the game's own credit function
(strong override, like 001's join gate), and player 2's menus reuse the game's native inventory
and abilities screens (their data and cost tables, driven for player 2) instead of a Lua-only
imitation. Six proofs first ([research.md](research.md)).

What 001 proved and 002 builds on:

| 001 piece | Use in 002 |
| --- | --- |
| Strong override of a weak `sub_` alias with campaign-only behaviour (`fable2_coop_joingate.h`) | XP credit override, use-item and ability-learn calls |
| Queued game actions take only on a free hero; retry until the state reads right (T021a gender switch) | equip, use and learn commands are retried and confirmed by a read-back, not by a return value |
| Busy detector (`fable2_coop_busy.h`: save, load, movie, cutscene, fade, stale probe) | commands wait while busy (contract rule 3) |
| Save hook + `coop_player2.txt` with saveuid and per-save stamp (T030/T031) | player 2's kit, XP and abilities persist through it unchanged |
| Profile v2 capture/apply (`tools/coop_lua/lib.lua`) | `p2state` snapshot for the menu |
| One shared purse (1 Hz delta sync with `Money.Set`) | unchanged; shops for player 2 stay in feature 004 |
| Rig: three instances, remote pad (sticks, buttons), console `luafile`, launch scripts with PASS/FAIL | all rig proofs and verification |

## Technical Context

**Language/Version**: C++20 (recomp app, rexglue-sdk), Lua 5.1 (game VM through the recomp Lua runner)

**Primary Dependencies**: rexglue-sdk (LAN lobby keyed data, `rex::ui` overlay, cvars), Fable-2-Recomp
(function overrides, `fable2_coop_*.h`), generated recomp code (`generated/default/*.cpp`) for static
analysis, game Lua API

**Storage**: unchanged from 001 (campaign slot, `coop_player2.txt` now carries the full kit, XP pools and
ability levels); XP audit lines in the host log and `coop_xp_log.txt` in the campaign slot

**Testing**: the 001 rig (mod-a host, mod-b partner, mod-c spare) driven by `tools/coop_launch*.py`; XP
and kit checks by `luafile` reads and `COOPHERO:v2` diffs (`tools/coop_diff.py`); function-trace diffs
(`FABLE2_FUNC_TRACE=1`) to find the native credit, use-item and learn paths

**Target Platform**: Windows 10/11 x64, two PCs on LAN or ZeroTier

**Project Type**: Game mod (static recompilation app + runtime library)

**Performance Goals**: an equip or use shows on both screens within 1 s (SC-002); XP routing adds no
visible delay to orb pickup

**Constraints**: joiner runs no game Lua while visiting; game Lua only on the game thread; every change
behind `coop_campaign` and the `kit` protocol feature; solo saves and v13 drop-in co-op unchanged

## Constitution Check

| Principle | How 002 complies |
| --- | --- |
| Co-op is a dedicated campaign | everything runs only in a campaign session (`coop_campaign` + partner has `kit` in `proto`) |
| Solo saves untouched | 002 writes only player 2's data in the campaign slot through 001's save hook |
| XP rule (constitution V) | US1: typed XP to the earner, same amount as general XP to the partner, quest/general 50/50, every award logged |
| Proven on the rig before release | each user story ends at a rig checkpoint with evidence; Gate 2 before install |
| Direct patching preferred (Josh) | native credit function and native screens' data are overridden/reused; Lua only where the game itself uses Lua |

## Project Structure

### Documentation (this feature)

```text
specs/002-player2-kit/
├── spec.md          # what and why (ready)
├── research.md      # decisions D1-D7, proofs (updated 2026-10-24)
├── data-model.md    # COOPHERO:v2, COOPCMD:v1, COOPACK:v1, COOPXP:v1
├── contracts/
│   └── coop-channel.md
├── plan.md          # this file
└── tasks.md         # /speckit-tasks output
```

### Source Code (repository root)

```text
src/core/
├── fable2_coop_xp.h          # NEW: override of the native XP credit (D3), routing + COOPXP log
├── fable2_coop_kit.h         # NEW: host side of the command channel: apply equip/unequip/use/learn/dye/hair,
│                             #      read back, ack; p2state publisher
├── fable2_coop_kitmenu.h     # NEW: joiner side: command queue, p2state reader, menu model
├── fable2_coop_morph.h       # NEW (US4): body morph refresh for player 2
├── fable2_coop_hero.h        # extend: v2 fields dye/hair/tat/titles
└── fable2_coop_frontend.h    # extend: start/stop the kit channel with the campaign session
thirdparty/rexglue-sdk/src/ui/overlay/
└── coop_kit_panel.cpp        # NEW: the joiner's co-op menu (F7 / Back+Y): Inventory, Equipment,
                              #      Abilities, Hero tabs; never pauses
tools/coop_lua/lib.lua        # extend capture/apply (v2 fields)
tools/coop_launch3x.py        # rig proofs and verification runs
```

## Releases

| Version | Content | Stories |
| --- | --- | --- |
| v15 | Fair XP + XP log | US1 |
| v15.1 | Co-op menu: inventory, equipment, consumables, abilities | US2, US3 |
| v15.2 | Body morphs | US4 |

## Risks

| Risk | Mitigation |
| --- | --- |
| The XP credit point is shared by many callers (orbs, quests, scripts) | X1 traces each source; the override routes by caller-supplied type and entity, logs every call; campaign-only |
| Native use-item needs the inventory screen's context | C1 finds the action below the screen; fallback: scripted effects per consumable class |
| Ability costs live in data the menu cannot read | A1 reads the native screen's cost path; fallback: costs from the game data tables |
| Commands from the joiner during cutscenes | busy detector + contract rule 3 (retry, then `no:busy`) |
| Joiner-side overlay cannot show game icons | text-first menu (names from the game's string table); icons in a later release |

## Complexity Tracking

No constitution violations. The co-op menu is an overlay (not the native pause menu) until feature
004 gives player 2 the game's own pause menu.
