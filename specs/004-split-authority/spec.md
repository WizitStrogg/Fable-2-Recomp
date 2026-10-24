# Feature Specification: Split authority (your PC runs your hero)

**Feature Branch**: `coop/004-split-authority`

**Created**: 2026-10-23

**Status**: Draft. Plan only after the proofs below; size and approach depend on them.

**Input**: Unlimited-time vision, Tier 2: the host owns the shared world; each PC owns its own
hero. The joining PC stops being a viewer and runs its own hero, so the game's own pause menu,
abilities screen, shops and HUD work for it, and combat feels local.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Your own real pause menu (Priority: P1)

Calvin presses Start in the campaign and gets the game's full pause menu for his own hero:
inventory, abilities with Learn and Discard, equipment, expressions, logbook, stats. Josh keeps
playing while it is open.

**Independent Test**: B opens the native pause menu during a fight in A's world, equips an item
and learns an ability; A's game is not paused; changes show on both screens.

### User Story 2 - Combat that feels local (Priority: P1)

Calvin's movement, swings, blocks and spells start instantly on his PC; the host confirms hits on
enemies.

**Independent Test**: with 150 ms artificial delay on the link, B's hero responds to input in the
same frame; enemy hits register on A.

### User Story 3 - Shops, trading, gifts and pub games for the joiner (Priority: P2)

Calvin buys from a shop with the campaign's shared purse while Josh shops elsewhere; heroes can
give items to each other; Calvin can play pub games.

### User Story 4 - Drop in and drop out (Priority: P2)

Either player can join or leave at any time without restarting; the leaving hero's progress is
saved immediately.

### Edge Cases

- The two PCs disagree (a hit seen on one only): host wins for the world, joiner wins for its own
  hero's input; a desync checker on the rig logs every disagreement.
- Menus must never pause the shared world (open question: how the game pauses in online co-op).

## Requirements *(mandatory)*

- **FR-001**: The joiner's PC MUST hold its own hero's full data locally while visiting and run the
  hero's systems (inventory, abilities, equipment, menus) against it.
- **FR-002**: The joiner's hero state MUST be sent to the host at frame rate over our own channel
  (from 003), and the host's proxy of player 2 MUST follow it.
- **FR-003**: World interactions by the joiner (hits, doors, chests, pickups, shop purchases) MUST
  be requests the host confirms.
- **FR-004**: The game's own full pause menu MUST be enabled for player 2 instead of the henchman
  menu (`sub_8240EAA0` → `sub_824129C0` branch).
- **FR-005**: Player 2's hero MUST still be saved in the campaign in the same save event as the
  world (feature 001): the joiner's PC sends its hero state to the host at each save.

## Proofs to run before planning

1. What the joiner's PC keeps about its own hero while visiting (inventory and stat components on
   its local player-2 entity).
2. Forcing the full pause-menu branch on the joiner: what shows, what crashes.
3. Whether player 2's movement is already client-driven (v12 found movement follows the joiner's
   own view) and which messages carry it.
4. The game's entity network ids (`NetUID`/`LocalUID`) and the hit message (`Hit entity %s Local
   UID ... NetUID`): can the joiner originate them?
5. How pausing works in online co-op.

## Success Criteria *(mandatory)*

- **SC-001**: Both players open their own full pause menu at the same time in the same fight, and
  neither game pauses.
- **SC-002**: A 2-hour session with two full heroes on the rig with zero unresolved desyncs.

## Assumptions

- Depends on 001 (campaign, save hook), 002 (player-2 kit) and 003 (a real-time channel).
- The largest risk in the roadmap; the proofs may change the approach.
