# Feature Specification: Full hero, host-run

**Feature Branch**: `coop/001-full-hero`

**Created**: 2026-10-23

**Status**: Ready for review

**Input**: User description: "True two-player co-op where each player keeps control of their own
character: their full pause menu (the guest can't level up abilities today), full inventory with
equip/unequip, their own progress. Tier 1 of the roadmap: player 2 in the host's world becomes the
joiner's real hero, the joiner manages it from their own menu, and everything gained goes back to the
joiner's own save."

## Context

Today (v13) the joiner appears in the host's world with their own look and basic stats, but
otherwise plays as the game's co-op henchman: a three-page menu (abilities view, config, quit),
no inventory, no way to spend XP, and only the game's own summary (gold, XP, renown) written back
to their save. The joining PC runs no game logic while visiting; the host simulates player 2.

In this feature the host still simulates player 2 (that changes in feature 004), but player 2
carries the joiner's complete hero, the joiner drives it through a co-op menu on their own PC, and
a session ledger brings every change home.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Your hero arrives complete (Priority: P1)

When Calvin joins Josh's world, the character he controls is his own hero in full: his stats,
morality, renown, titles, XP pools, ability levels, every item he owns, his equipped weapons and
clothes, and his gold. Nothing about Josh's hero or Josh's save changes.

**Why this priority**: Everything else (menus, levelling, saving progress) acts on this complete
hero. It also removes the most visible gap: the joiner fighting with abilities and weapons that
are not theirs.

**Independent Test**: On the rig, B joins A. A capture of player 2 on A matches B's own capture
taken in B's own world (field-by-field diff), and A's hero and A's save are unchanged.

**Acceptance Scenarios**:

1. **Given** B's hero has level 3 Fireball and 40,000 gold, **When** B joins A, **Then** player 2
   on A casts B's level 3 Fireball and B's co-op menu shows 40,000 gold.
2. **Given** B owns 104 items including 6 health potions and two swords, **When** B joins A,
   **Then** player 2 on A holds exactly those items and wields the sword B had equipped.
3. **Given** A's hero and save before the session, **When** B joins, plays 5 minutes and leaves,
   **Then** A's hero profile and A's save item count are identical to before.
4. **Given** B's partner runs v13 (older protocol), **When** they join, **Then** play falls back to
   v13 behaviour with a one-line notice, and nothing breaks.

---

### User Story 2 - Your progress comes home (Priority: P1)

Everything that happens to Calvin's hero in Josh's world is saved to Calvin's own save: XP earned,
gold found or spent, items picked up, used, bought or dropped, ability levels learned, morality
and purity changes, fat and strength changes from food. It survives a crash.

**Why this priority**: Without it, any progress made through stories 3 and 4 is lost when the
session ends. It is also the part where a mistake damages a real save, so it is built and proven
early.

**Independent Test**: On the rig, B joins A, earns XP, picks up an item, drinks a potion, then
leaves. On B's next load of its own save, B's hero shows exactly those changes (diff against the
pre-session capture plus the ledger). Repeat with B's game killed mid-session.

**Acceptance Scenarios**:

1. **Given** B earns 2,000 general XP and picks up a Rusty Cleaver in A's world, **When** B loads
   its own save afterwards, **Then** B's general XP is 2,000 higher and B owns one more Rusty
   Cleaver.
2. **Given** B's game is killed during the session, **When** B next loads its own save, **Then**
   everything recorded up to the last confirmed ledger entry is applied, and nothing is applied
   twice.
3. **Given** the ledger was already applied, **When** B loads its save again, **Then** nothing is
   applied a second time.
4. **Given** the game's own co-op reward write-back (gold/XP/renown summary) also runs, **When**
   B's ledger is applied, **Then** no gold or XP is counted twice.
5. **Given** B's save slot before applying, **When** the ledger is applied, **Then** a backup of the
   slot exists in `backups\`.

---

### User Story 3 - Manage your kit from your own menu (Priority: P2)

Calvin opens a co-op menu on his own PC (a key and a pad button) and sees his inventory, gold and
equipment. He can equip and unequip weapons and clothes, change hairstyle and dyes, and use
potions, food, drink and augments. The change happens to his hero in Josh's world and is visible
on both screens within a second.

**Why this priority**: This is the first thing Josh asked for that the henchman cannot do. It
depends on story 1 (the items must be there) and story 2 (changes must be saved).

**Independent Test**: On the rig, B opens the co-op menu, equips a different sword and drinks a
health potion. Both screenshots show the new sword on player 2; player 2's health rises on A; B's
inventory count drops by one potion; the ledger records both.

**Acceptance Scenarios**:

1. **Given** B is in A's world, **When** B selects a hat and presses Equip, **Then** the hat
   appears on player 2 on both screens within 1 second.
2. **Given** B is hurt, **When** B uses a health potion from the menu, **Then** player 2 heals on
   A and the potion count drops by one in B's menu.
3. **Given** B selects an item the host's game rejects (for example a quest item), **When** B
   presses Use, **Then** the menu shows why and nothing changes.
4. **Given** B has the menu open, **When** enemies attack, **Then** A's game keeps running (the
   menu never pauses the shared world).

---

### User Story 4 - Level up your own abilities (Priority: P2)

From the co-op menu Calvin spends his own Strength, Skill, Will and general XP on abilities, with
the same costs and limits as the game's own abilities screen. New levels take effect in combat
immediately and are saved to his save.

**Why this priority**: Named by Josh as the clearest shortfall of the current mod.

**Independent Test**: On the rig, B learns Fireball level 2 with Will XP; B's Will XP drops by the
game's cost; player 2's Fireball on A is level 2; after the session, B's own save has Fireball 2.

**Acceptance Scenarios**:

1. **Given** B has enough Will XP for Fireball level 2, **When** B learns it, **Then** the XP drops
   by the game's own cost and player 2 casts level 2 Fireball.
2. **Given** B lacks the XP, **When** B tries to learn it, **Then** the menu says so and nothing
   changes.
3. **Given** B learned an ability, **When** B loads its own save after the session, **Then** the
   ability level is there.

---

### User Story 5 - Your body looks right (Priority: P3)

Calvin's hero shows his own fat, strength, height and age-related body shape (and tattoos and
scars where possible), not only his clothes and hair.

**Why this priority**: Cosmetic, and v13 already shows clothes and hair.

**Independent Test**: B's hero at maximum fat appears visibly fat on both screens.

**Acceptance Scenarios**:

1. **Given** B's hero is at maximum fat, **When** B joins A, **Then** player 2 is visibly fat on
   both screens (side-by-side screenshot with B's own world).

### Edge Cases

- The joiner leaves, crashes or loses the connection mid-command: the command either completes and
  is in the ledger, or does not happen at all.
- The host saves while player 2 holds the joiner's items: those items must never enter the host's
  save (player 2's inventory is not part of the host hero's save; verified by diff).
- Items the host's game does not know (DLC the host lacks): skipped with a notice, kept in the
  joiner's save, never lost.
- Quest items given to player 2 by the host's story: recorded but not added to the joiner's save
  unless the item is a normal inventory item (open question for 002).
- Player 2 is knocked out: XP loss and scars follow the game's rules for player 2 and are recorded
  in the ledger.
- The joiner joins a different host next time with an unapplied ledger: the ledger is applied first
  (on load of their own save, before joining), so it is never lost.
- Both PCs run different mod versions: the co-op protocol carries a version; mismatched features
  turn off for that session.
- The game's own henchman reward write-back runs at the end of the session: the ledger owns gold
  and XP while this feature is on, so the game's write-back for those fields is neutralised or
  reconciled (to be decided by proof P5).
- A command arrives while the host is in a cutscene or loading: it waits in a queue and runs after.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The joiner's PC MUST capture its complete hero in its own world before joining:
  stats, morality, purity, renown, titles, four XP pools, all 15 ability levels, every inventory
  item with count, equipped weapons, clothing, hair, dyes, tattoos, gold, body morphs.
- **FR-002**: The host MUST apply the complete hero to player 2 when the joiner arrives, and again
  after every player 2 respawn, without changing the host's own hero or save.
- **FR-003**: The two PCs MUST exchange co-op commands over a versioned, acknowledged channel:
  every command has an id, is applied at most once, and returns a result.
- **FR-004**: The joiner MUST be able to open a co-op menu on their own PC with a key and a pad
  button, without pausing either game.
- **FR-005**: The co-op menu MUST show the joiner's inventory, gold, equipped items, XP pools and
  ability levels as they currently are on the host.
- **FR-006**: The joiner MUST be able to equip and unequip weapons and clothing, apply hairstyles
  and dyes, and use consumables from the menu.
- **FR-007**: The joiner MUST be able to learn ability levels with their own XP at the game's costs
  and limits.
- **FR-008**: Every change to player 2's hero MUST be written to a session ledger on the joiner's
  PC as it happens, flushed to disk before the command is acknowledged.
- **FR-009**: On the joiner's next load of their own save, the ledger MUST be applied exactly once,
  after a backup of the save slot, and then archived.
- **FR-010**: Gold and XP MUST NOT be counted twice between the ledger and the game's own co-op
  reward write-back.
- **FR-011**: Every behaviour in this feature MUST sit behind cvars that default to off, with F6
  menu boxes once verified, and MUST fall back to v13 behaviour with an older partner.
- **FR-012**: Player 2's body shape MUST reflect the joiner's fat, strength and height (story 5).

### Key Entities

- **Hero profile (v2)**: everything FR-001 lists, captured in the joiner's own world; replaces the
  v1 `COOPHERO` line (stats, look, worn and wielded items only).
- **Co-op command**: a request from the joiner (equip, unequip, use, learn ability, refresh) with
  an id, a target item or ability, and a result (applied, rejected with reason).
- **Player 2 state mirror**: what the joiner's menu shows; sent by the host after every change.
- **Session ledger**: an append-only record on the joiner's PC of every change to the joiner's hero
  during a session, applied once to the joiner's save.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: After a session, a field-by-field diff of the joiner's hero (pre-session capture +
  ledger vs. post-load capture) shows zero unexpected differences across 3 rig runs, including one
  run killed mid-session.
- **SC-002**: After a session, the host's hero and save item count are identical to before in 3
  out of 3 rig runs.
- **SC-003**: An equip or use command shows its result on both screens within 1 second on the rig.
- **SC-004**: The joiner can complete the full loop (join, learn an ability, equip an item, use a
  potion, leave, reload) with no console commands and no help from the host player.
- **SC-005**: Josh and Calvin complete the `COOP-TESTS.txt` steps for this feature on two PCs with
  no save damage.

## Assumptions

- The host still simulates player 2 in this feature; the joiner's own native pause menu is out of
  scope until feature 004. The co-op menu is a recomp overlay (like F6), styled to be readable with
  a pad.
- Player 2 has the full hero component set once it is spawned as `CreatureHero` (v13 shows it owns
  and wears items); proof P2 confirms ability levels act on it.
- Shops, trading, gifts between heroes, the dog and story are out of scope (features 004, 005, 002).
- Both players run this feature's mod version for the new behaviour; otherwise v13 behaviour.
- Calvin is reachable over ZeroTier as today; the server (003) is not required.
