# Feature Specification: Two heroes, one story

**Feature Branch**: `coop/005-two-heroes-one-story`

**Created**: 2026-10-23

**Status**: Draft (after 001, 002 and 004)

**Input**: Josh (2026-10-23): "it doesn't matter which player initiates a quest, it will count for
both players... unique events like getting married also need to have a clear plan for how both
players will be able to engage in the activity separately... The simplest solution might be to
have P1 be sparrow and P2 be the sister... having both be an option would be great."

## Context

In a co-op campaign the story and gold are already shared (feature 001), so every quest counts for
both. What remains is letting player 2 *drive* the story as fully as player 1: start quests and
conversations, take part in story scenes, marry and raise a family, own a dog, and play childhood
as Rose. 166 game scripts assume a single hero (`GetPlayerHero()`), and player 2 is hidden as a
ghost in 27 places across 13 quest scripts; this feature works through them.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Either player can start quests and conversations (Priority: P1)

Calvin talks to a quest giver and accepts the quest; it is the campaign's quest, and both heroes
get the reward per the XP rule. Story choices in conversations are made by whoever is talking.

**Independent Test**: B starts and completes a side quest in a rig campaign; the quest is complete
in the campaign; rewards follow the XP rule and the shared purse.

### User Story 2 - Both heroes in story scenes (Priority: P1)

Player 2 is no longer turned into a ghost during story moments; both heroes stand in cutscenes and
take part in story fights.

**Independent Test**: play QC130 (camp initiation) with both heroes; no ghosting; the scene
completes; review log of every `SetHenchmanGhostMode` call kept or removed.

### User Story 3 - Each hero marries and has a family (Priority: P1)

Each hero courts, proposes to and marries their own spouse, separately. Weddings work for either
hero. Children belong to the hero who had them. Any house the campaign owns can be either couple's
home. Spouses react to their own hero (gifts, neglect, divorce).

**Independent Test**: on the rig, B marries a villager while A is married to another; both
marriages hold across save and reload; each spouse recognises only their own hero.

**Acceptance Scenarios**:

1. **Given** both heroes court the same villager, **When** one proposes first, **Then** that hero
   marries them and the other's courtship ends normally.
2. **Given** B is married, **When** B and their spouse have a child, **Then** the child is B's and
   appears in B's family pages.
3. **Given** a jointly owned house, **When** either hero sets it as their family home, **Then**
   their spouse moves in; both families may share a house only if the game allows it (otherwise
   the menu says so).

### User Story 4 - Each hero has their own dog (Priority: P2)

Each hero has a dog that follows them, fights, finds treasure and learns tricks from its own hero.
(The game's dog is a single global, `GetDog()`, used in 63 scripts.)

### User Story 5 - The story's solo moments, together (Priority: P2)

The places where the story is written for one hero get a co-op version:

- **The Spire**: both heroes are taken to the Spire together and live its years together.
- **The final choice** (the sacrifice at the end): both players vote; if they disagree, a second
  vote; if still split, the host decides. If the outcome restores loved ones, each hero gets their
  own family and dog back.
- Other single-hero moments found while playing through are listed in research.md with a plan each.

### User Story 6 - Childhood as Sparrow and Rose (Priority: P3)

The lobby offers "Play childhood": player 1 is Sparrow, player 2 is Rose. Rose's story ends in
childhood, so at that point player 2 continues as their own adult hero from the adult start.

**Independent Test**: B controls Rose through the childhood quests (`Player.ChangePlayerEntityType`
and the debug `ControlCreature` path suggest control of another creature is possible); at the
transition B becomes their adult hero; the campaign continues.

### User Story 7 - Jobs for both (Priority: P3)

Either hero can work a job (blacksmith, bartender, woodcutter); wages go to the shared purse. Today
the job scripts hide player 2.

### Edge Cases

- A story choice made by one player that the other disagrees with: the active speaker decides;
  only the final choice uses a vote.
- Scripts that cannot work for player 2 fall back to player 1, listed in research.md.

## Requirements *(mandatory)*

- **FR-001**: The game MUST know which hero is acting for quest givers, conversations and choices
  (a contextual "acting hero" behind `GetPlayerHero()` where safe).
- **FR-002**: Every `Multiplayer.SetHenchmanGhostMode` call MUST be reviewed and removed where both
  heroes can take part safely.
- **FR-003**: Family functions MUST work per hero (`PlayerFamily.Marry(hero, villager)` already
  takes the hero as an argument; proof needed for player 2).
- **FR-004**: A second dog bound to player 2 MUST be supported.
- **FR-005**: Each single-hero story moment MUST have a written co-op plan before implementation.

## Success Criteria *(mandatory)*

- **SC-001**: A full chapter played with player 2 leading half of the quests, no ghosting where
  removed, campaign correct after reload.
- **SC-002**: Two marriages and two dogs active for a 1-hour session with no script errors.

## Assumptions

- The largest content-testing effort: each quest needs a play-through on the rig.
- Any cut (for example a partial dog) needs Josh's approval (gate 7).
