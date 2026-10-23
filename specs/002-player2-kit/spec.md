# Feature Specification: Player 2's full kit

**Feature Branch**: `coop/002-player2-kit`

**Created**: 2026-10-23

**Status**: Spec ready. Plan and tasks are generated (`/speckit-plan`, `/speckit-tasks`) once 001
ships, so they build on 001's proven campaign, save hook and player-2 profile.

**Input**: Josh (2026-10-23): each player has "their full pause menu (the current solution doesn't
allow the guest user to level up their own abilities), ... ability to equip/unequip items with full
inventory". Josh's XP rule: "the exp should be split 50/50 as a net, but it should still be doled
out based on gameplay. If P1 is playing as a warrior and P2 is a spellcaster, we don't want the
warrior getting 50% of the spellcasting EXP."

## Context

In a co-op campaign (feature 001) the host simulates player 2 and stores its hero in the campaign.
This feature gives player 2 everything player 1 has for managing a hero: inventory, equipment,
consumables and ability levelling, through a co-op menu on player 2's own PC, plus fair XP for both
heroes. The game's own full pause menu for player 2 comes with feature 004.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Fair XP for both heroes (Priority: P1)

When Calvin kills enemies with spells, he gets Will XP; Josh, fighting with a sword at the same
time, gets Strength XP for his kills. Each also receives the same amount as general XP for the
other's kills, so neither falls behind. Quest XP and general orbs are split 50/50.

**Why this priority**: Without it, player 2's progression does not match its play style, and the
levelling in story 3 has nothing fair to spend.

**Independent Test**: On the rig, A kills 5 enemies with melee and B kills 5 with Fireball. Strength
XP goes only to A, Will XP only to B, and each receives the other's amount as general XP; totals
are equal. The XP award log matches the screens.

**Acceptance Scenarios**:

1. **Given** B kills an enemy with Fireball worth 100 Will XP solo, **When** the orbs are
   collected, **Then** B gains 100 Will XP and A gains 100 general XP.
2. **Given** a quest reward of 2,000 general XP, **When** the quest completes, **Then** each hero
   gains 1,000.
3. **Given** a full session, **When** the XP award log is summed, **Then** both heroes' total XP
   earned is equal.

---

### User Story 2 - Manage your kit from your own menu (Priority: P1)

Calvin opens a co-op menu on his own PC (a key and a pad button) and sees his inventory and
equipment. He equips and unequips weapons and clothes, changes hairstyle and dyes, and uses
potions, food, drink and augments. The change shows on both screens within a second, and the
shared world keeps running while the menu is open.

**Why this priority**: The first thing Josh asked for that the henchman cannot do.

**Independent Test**: On the rig, B equips a different sword and drinks a health potion from the
menu; both screens show the sword; player 2 heals on A; the potion count drops by one.

**Acceptance Scenarios**:

1. **Given** B is in the campaign, **When** B selects a hat and presses Equip, **Then** the hat
   appears on player 2 on both screens within 1 second.
2. **Given** B is hurt, **When** B uses a health potion, **Then** player 2 heals and the count drops
   by one.
3. **Given** B selects a quest item, **When** B presses Use, **Then** the menu says why not and
   nothing changes.
4. **Given** the menu is open, **When** enemies attack, **Then** neither game pauses.

---

### User Story 3 - Level up your own abilities (Priority: P1)

From the co-op menu Calvin spends his own Strength, Skill, Will and general XP on abilities, with
the same costs and limits as the game's abilities screen. New levels take effect immediately and
are saved in the campaign.

**Why this priority**: Named by Josh as the clearest shortfall today.

**Independent Test**: B learns Fireball level 2; B's Will XP drops by the game's cost; player 2
casts level 2 Fireball; after save and reload it is still level 2.

**Acceptance Scenarios**:

1. **Given** enough Will XP, **When** B learns Fireball 2, **Then** XP drops by the game's cost and
   player 2 casts level 2.
2. **Given** not enough XP, **When** B tries, **Then** the menu says so and nothing changes.

---

### User Story 4 - Your body looks right (Priority: P3)

Player 2 shows its fat, strength, height and age-related body shape, tattoos and scars, not only
clothes and hair.

**Independent Test**: player 2 at maximum fat is visibly fat on both screens.

### Edge Cases

- Both heroes hit the same enemy: the killing blow decides the earner and the type.
- XP from orbs that are not tied to a kill (breaking objects, general orbs): split 50/50.
- The dog's kills (feature 005): count as its owner's, general type.
- A command arrives during a cutscene or loading: queued and run after.
- An item the menu offers was removed by the world (stolen, used in a quest): rejected with a
  reason, the menu refreshes.
- Player 2 is knocked out with the menu open: menu closes; normal game rules for player 2.

## Requirements *(mandatory)*

- **FR-001**: Typed XP (strength, skill, will) MUST go to the hero whose action earned it; the
  partner MUST receive the same amount as general XP.
- **FR-002**: Quest and general XP MUST be split 50/50.
- **FR-003**: Every XP award MUST be logged (source, earner, type, amounts) for verification.
- **FR-004**: Player 2 MUST have a co-op menu on their own PC, opened by a key and a pad button,
  that never pauses either game.
- **FR-005**: The menu MUST show player 2's inventory, equipment, XP pools and ability levels as
  they currently are on the host.
- **FR-006**: Player 2 MUST be able to equip, unequip, dye, change hairstyle and use consumables.
- **FR-007**: Player 2 MUST be able to learn ability levels with their own XP at the game's costs
  and limits.
- **FR-008**: Commands MUST travel over a versioned, acknowledged channel, applied at most once
  (`contracts/coop-channel.md`).
- **FR-009**: Player 2's body shape MUST reflect fat, strength and height (story 4).
- **FR-010**: All changes MUST be persisted through 001's campaign save hook.

## Success Criteria *(mandatory)*

- **SC-001**: Over a 30-minute rig session with mixed combat, the XP log shows equal totals for
  both heroes and no typed XP given to the wrong hero.
- **SC-002**: An equip or use command shows on both screens within 1 second.
- **SC-003**: Player 2 can join, learn an ability, equip an item, use a potion and save with no
  console commands and no help from player 1.
- **SC-004**: Josh and Calvin complete the release's `COOP-TESTS.txt` steps on two PCs.

## Assumptions

- Feature 001 is released: campaign, save hook, player-2 profile v2.
- Shops for player 2 (spending from the shared purse) and the game's native pause menu are feature
  004.
- "Same amount as general XP" keeps each hero's progression close to a solo play-through; if
  play-testing shows levelling is too fast, the partner share becomes a cvar (for example 50%).
