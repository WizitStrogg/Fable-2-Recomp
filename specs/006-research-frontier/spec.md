# Feature Specification: Research frontier

**Feature Branch**: `coop/006-research-frontier`

**Created**: 2026-10-23

**Status**: Draft (experiments only; nothing here is promised)

**Input**: Items that might never work, each tried as a time-boxed experiment.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Either player can host the campaign (Priority: P1)

Calvin can host the campaign when Josh's PC is unavailable, with Josh joining as player 2. The
world save holds Josh's hero as the game's hero, so this needs swapping the two heroes' data
(including families) when the host changes.

### User Story 2 - Explore different regions at the same time (Priority: P2)

When the players go to different regions, the session splits into two worlds and merges when they
meet again, reconciling world changes made apart.

### User Story 3 - A third player (Priority: P3)

Three heroes in one campaign (the engine is built around two player slots).

## Requirements *(mandatory)*

- **FR-001**: Each experiment starts with a written hypothesis and a time box, approved by Josh.
- **FR-002**: Results, including failures, are recorded in this feature's research.md.

## Success Criteria *(mandatory)*

- **SC-001**: Each experiment ends with a clear yes or no and evidence.

## Assumptions

- After 004; uses its split-authority channel.
