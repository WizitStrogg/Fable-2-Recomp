# Feature Specification: Co-op campaign and lobby

**Feature Branch**: `coop/001-coop-campaign`

**Created**: 2026-10-23

**Status**: Ready for review

**Input**: Josh (2026-10-23): "I would like the campaign to be a dedicated Co-op campaign from the
start. By starting and finishing together, we can treat both players as always having the same
story progress... Both players will also share a money pool... we could present an option to just
skip the beginning of the game... we would need to adjust the lobby system by adding a TRUE lobby
system to the game. Players join by selecting new game and are presented with a custom set of
options (Solo or Co-op). If coop is selected, it should prepare a lobby for each player to connect
to and once connected to, it creates a save game that is only playable with both players from
that point forward."

**Revision (Josh, 2026-10-24, after the first two-PC session)**: "Below the Continue bar, please
add another option labeled Co-op. It is critical that this option follows the same template as
the 5 other options... After coop is selected, I would like a lobby menu system to appear. This
will prompt the user to either create a new lobby and select a Co-op save game or create a new
campaign (This should load the save skipping the childhood intro). Once a lobby is created, the
partner should see and be able to join that lobby and everyone queue up to start. After everyone
is queued, the host should be able to start the game at which time it should load on the host PC
and then connect the partner afterwards. I am looking to have the co-op experience limited to the
new co-op selection in the menu." New Game and Continue are the stock game's again; user stories
1-2 and FR-001/002/006/007/008/014-016 below follow this revision.

## Context

Today (v13) co-op means dropping into someone's solo world as the game's henchman. This feature
adds a second kind of game: a **co-op campaign**. It is created together from New Game, it is
always played together, both heroes share its story and its gold, and player 2's hero lives inside
the campaign. Solo games and the game's normal drop-in co-op are unchanged.

Decisions taken with Josh (2026-10-23), recorded in the constitution v1.1.0:

- The player who creates the campaign always hosts it. A backup copy goes to the partner after
  every session. (Letting either player host is a later research item, feature 006.)
- Childhood: "skip to adulthood" ships first. Playing childhood as Sparrow and Rose is feature 005.
- Gold is one shared purse; property is joint.
- XP rule (implemented in feature 002): the earner keeps XP in the style they used; the partner
  gets the same amount as general XP; quest and general XP split 50/50.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Start a co-op campaign together (Priority: P1)

Josh chooses Co-op on the main menu (below Continue), then "Host: new co-op campaign". A lobby
opens and waits. Calvin chooses Co-op, sees "Join Josh's co-op campaign" at the top of the list
and joins it. Calvin picks his hero's gender; Josh picks the childhood option
(skip to adulthood). Both press Ready, Josh presses Start, and both arrive in the world at the
start of adulthood: Josh as player 1, Calvin as his own new hero.

**Why this priority**: This is the entry point of the whole co-op experience Josh described.

**Independent Test**: On the rig, A creates a campaign from New Game, B joins the lobby, both
ready, A starts. Both instances end up in the world at the adult start, B's hero has the chosen
gender, and a new save slot on A carries the co-op campaign marker. A's and B's existing solo
saves are byte-identical to before.

**Acceptance Scenarios**:

1. **Given** both games are at the main menu, **Then** it shows New Game, Continue, Co-op,
   Downloadable Content, Language, Subtitles, Co-op drawn and animated like the others; New Game
   and Continue are the stock game's.
2. **Given** Josh hosts a lobby, **When** Calvin chooses Co-op, **Then** Calvin sees Josh's lobby
   by name, once (even over two shared networks), within 10 seconds and can join it.
3. **Given** both are in the lobby, **When** Calvin picks Female and Josh picks "Skip to
   adulthood", **Then** both lobby screens show both choices and both Ready states.
4. **Given** both are ready, **When** Josh presses Start, **Then** both arrive in the world at the
   adult start, with Calvin's hero female and controlled by Calvin.
5. **Given** either player leaves the lobby before Start, **Then** no save is created.

---

### User Story 2 - The campaign is only played together (Priority: P1)

Josh continues the campaign from Co-op -> "Host: continue a co-op save"; its lobby waits for
Calvin and never enters the world alone. Calvin rejoins from Co-op ("Continue Josh's campaign with
you"). If either player drops mid-session, the host saves and returns to the main menu with a
message.

**Why this priority**: Josh's rule that the save is only playable with both players is what makes
shared story and shared gold work without any syncing.

**Independent Test**: On the rig, A loads the campaign with B absent: A waits in the lobby and
cannot enter the world. B joins: both enter. Kill B mid-session: A saves and returns to the lobby
within 10 seconds.

**Acceptance Scenarios**:

1. **Given** a co-op campaign save, **When** Josh loads it without Calvin, **Then** a lobby says
   "Waiting for Calvin" and offers Back; the world is not entered.
2. **Given** Josh is waiting, **When** Calvin chooses Continue, then Co-op, and joins, **Then**
   both enter the world where they last saved.
3. **Given** a session in progress, **When** Calvin's game exits or disconnects, **Then** Josh's
   game saves and returns to the lobby, waiting for Calvin.
4. **Given** a third person with the mod, **When** they see Josh's lobby, **Then** they cannot
   join a campaign that belongs to Josh and Calvin.

---

### User Story 3 - Player 2's hero carries on (Priority: P1)

Everything about Calvin's hero (look, gender, stats, XP, ability levels, items, equipped weapons
and clothes, titles) is kept in the campaign and is exactly the same at the start of the next
session.

**Why this priority**: Without it, player 2 restarts every session.

**Independent Test**: On the rig, B's hero gains XP, picks up an item and changes clothes; A
saves; both quit; next session B's hero matches the capture taken at the save (diff), including
after a crash of A just after saving.

**Acceptance Scenarios**:

1. **Given** Calvin's hero gained 2,000 XP and a sword, **When** the host saves and both play
   again later, **Then** Calvin's hero has that XP and that sword.
2. **Given** the host's game crashes, **When** the campaign is loaded again, **Then** both heroes
   are exactly as they were at the last save (world and player 2 never disagree).

---

### User Story 4 - One shared purse (Priority: P2)

Gold either hero picks up, earns or is rewarded goes into one purse that both see on screen.

**Why this priority**: Josh's joint-venture design; it also removes the need to track each
hero's gold.

**Independent Test**: B picks up 100 gold in A's world: the shared total rises by 100 on both
screens and in the save.

**Acceptance Scenarios**:

1. **Given** the shared purse holds 500, **When** Calvin picks up 100 gold, **Then** both screens
   show 600.
2. **Given** a quest reward of 1,000, **When** the quest completes, **Then** the purse rises by
   1,000 once (not once per hero).

---

### User Story 5 - The partner keeps a backup (Priority: P3)

After every session, a copy of the campaign goes to Calvin's PC, so the campaign survives if
Josh's PC fails.

**Independent Test**: after a rig session, B holds a copy of A's campaign slot whose files hash
equal to A's.

**Acceptance Scenarios**:

1. **Given** a session ended, **When** both games are still running, **Then** Calvin's PC holds
   the latest campaign copy within 1 minute and F6 shows its date.

### Edge Cases

- Both players press Start or Ready at the same moment: the host's Start is the only one that
  counts.
- The partner runs an older mod version: the lobby shows "Calvin needs v14" and Start is disabled.
- The host loses the connection during the first load: no campaign marker is written until both
  are in the world; a half-created slot is removed or marked abandoned.
- The game's normal drop-in co-op (henchman) into a co-op campaign: refused; only the campaign's
  partner can join it.
- Player 2 dies or is knocked out: normal game rules for player 2; recorded at the next save.
- The host's purse goes negative through a script (shop or fine): same rules as the game's own
  purse.
- Story moments where the game hides player 2 (ghost mode): unchanged in this feature (feature 005
  revisits them).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The main menu MUST have a native "Co-op" entry directly below Continue, built by the
  game's own entry code (same look, focus, highlight, animation, sound); the entries below move
  down. New Game and Continue MUST behave exactly as the unmodded game.
- **FR-002**: Co-op MUST open a hub that lists the lobbies on the network (one row per player,
  even when heard over several networks) to join, then "Host: continue a co-op save" (when there
  is one) and "Host: new co-op campaign", then Back (to the main menu, focus on Co-op).
- **FR-003**: The lobby MUST let the partner choose gender, let the host choose the childhood
  option, show both players' choices and Ready states, and let only the host Start.
- **FR-004**: Start MUST create a new save slot on the host that carries a co-op campaign marker
  (campaign id, host and partner identities, options), and bring both players into the world.
- **FR-005**: "Skip to adulthood" MUST start the campaign at the beginning of adulthood using the
  game's own gameflow skip, with the adult starter state the story expects.
- **FR-006**: A co-op save MUST only be hosted from the hub; its lobby MUST wait for the
  campaign's partner and MUST NOT enter the world without them. Start loads it through the
  game's own Continue (the main menu's "Load Game").
- **FR-007**: The partner MUST be able to rejoin from Co-op.
- **FR-008**: If either player leaves or disconnects during a session, the host MUST save and
  return to the main menu with a message; the campaign continues from the hub.
- **FR-009**: Player 2's complete hero MUST be stored in the campaign slot and written in the same
  save event as the world save; it MUST be applied to player 2 when they enter the world.
- **FR-010**: All gold earned by either hero MUST go to one shared purse shown on both screens.
- **FR-011**: After each session the host MUST send a copy of the campaign slot to the partner.
- **FR-012**: Solo saves and the game's normal drop-in co-op into solo saves MUST be unaffected.
- **FR-013**: All of the above MUST sit behind a cvar (`coop_campaign`, default off until
  verified), shown as an F6 box; the friend's play-coop.cmd turns it on, so no F-keys are needed.
- **FR-014**: Players MUST be identified by their online XUID, never by display name; a blank
  gamertag shows the PC's name.
- **FR-015**: Start MUST be enabled only with a seated, ready partner. The host's go MUST be
  acknowledged by the partner and re-sent until then; either side MUST show a clear message and
  return to the main menu if the join does not happen within 2 minutes or the other side leaves.

### Key Entities

- **Co-op campaign**: a save slot on the host plus a marker naming the campaign, its two players
  and its options.
- **Lobby**: the pre-game meeting point where the partner joins and both choose options.
- **Player 2 hero file**: player 2's complete hero, saved inside the campaign slot.
- **Shared purse**: the campaign's single gold total.

## Success Criteria *(mandatory)*

- **SC-001**: From both games at the main menu to both heroes in the world takes under 2 minutes
  and no console commands.
- **SC-002**: In 5 of 5 rig runs, a reloaded campaign restores both heroes exactly (diff of player
  2's capture; host hero capture), including one run with a crash right after saving.
- **SC-003**: The players' solo saves are byte-identical before and after all rig runs.
- **SC-004**: Josh and Calvin create a campaign on two PCs and play two sessions on different days
  with no lost progress.

## Assumptions

- Both players run the same mod version with `coop_campaign` on.
- The host is always the player who created the campaign.
- Player 2's full menu, inventory management, ability levelling and the XP split come in feature
  002; in this feature player 2 plays with what the henchman can already do, and keeps what it
  earns.
- Discovery works as today (LAN and ZeroTier); feature 003 adds internet discovery and server
  backups.
