# Feature Specification: Story sync

**Feature Branch**: `coop/002-story-sync`

**Created**: 2026-10-23

**Status**: Ready for planning (after 001's ledger is released)

**Input**: Josh (2026-10-23): "both play the story together in one world, each from their own
save, both saves advancing." Josh chose: sync quests and story only; the host makes story
choices and both saves get the outcome; each hero keeps own homes, money, family, items; sync is
optional and forward-only, so players at different story points can still play together.

## Context

The design is already researched in `D:\Fable2-LAN\research\03-story-sync.md`: capture
(`coop_story_capture.lua`, shipped), an allow-list of Gameflow choice fields, the game's own
"skip to gameflow position" mechanism (`GameflowBase:SetGameflowPosition`) to advance the
joiner's world, and `QuestTracker.SetAsCompleted` for side quests. v13 already stores the host's
story in `saves\coop_story_pending.txt` on the joiner, but never applies it. This feature applies
it, using 001's ledger apply hook and backups.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Story progress counts for both (Priority: P1)

After Calvin helps Josh finish a main story quest in Josh's world, Calvin's own save is at the same
story point next time he loads it, with Josh's choices.

**Independent Test**: On the rig, A is one core quest ahead of B. B visits A, then loads its own
save and accepts the update: B's `PositionInGameflow` equals A's, the world layers of the skipped
quest are active in B's world, B's hero (stats, items, gold) is unchanged apart from additive
titles.

**Acceptance Scenarios**:

1. **Given** A is at QC140 and B at QC130, **When** B loads after the session and accepts, **Then**
   B is at QC140 with A's choice fields, and B's running QC130 is completed.
2. **Given** B is ahead of A, **When** B loads, **Then** nothing changes (forward-only).
3. **Given** `coop_story_sync = ask`, **When** B loads, **Then** B sees a prompt naming the quests
   that will be marked done, and can decline.

### User Story 2 - Side quests done together count for both (Priority: P2)

Side quests completed in the host's world are marked complete in the joiner's save.

**Independent Test**: A completes a side quest with B present; B's own save shows it complete.

### User Story 3 - Side-quest world changes follow (Priority: P3)

Where a side quest changes the world (a house unlocked, an NPC moved), the joiner's world gets the
same change, from per-quest replay tables.

### Edge Cases

- Joiner below QC060 (child): sync refused, with a reason.
- QC140 skip converts the joiner's babies to children (irreversible, story-consistent): stated in
  the prompt.
- The joiner's in-progress core quest is terminated by the skip: stated in the prompt.
- A ledger (001) and a story update are both pending: ledger first, then story, each with its own
  backup.

## Requirements *(mandatory)*

- **FR-001**: The joiner MUST receive the host's story state at session end and store it pending.
- **FR-002**: On load of the joiner's save, the pending update MUST apply only forward, only
  allow-listed choice fields, after a backup, with `ask`/`always`/`never` modes.
- **FR-003**: The joiner's hero MUST be unchanged by the apply except for additive titles and
  expressions, verified by a before/after capture diff.
- **FR-004**: Side quests completed on the host MUST be marked complete on the joiner.

## Success Criteria *(mandatory)*

- **SC-001**: 5 rig runs across different chapters: joiner's story position and choice fields
  equal the host's; hero diff shows only additive titles.
- **SC-002**: Josh and Calvin play one full chapter together and both saves continue normally.

## Assumptions

- 001's ledger apply hook and backup routine are available.
- Optional-quest world replay (US3) may be partial; quests without a replay table are marked
  complete only.
