# Fable II Co-op Constitution

The rules every co-op change in this repository follows, whoever makes it: Josh, or Claude
working autonomously on Josh's PC. Where a spec, plan or task disagrees with this file, this
file wins until it is amended.

## Core Principles

### I. Proven Before Shipped (NON-NEGOTIABLE)

- No release (mod install, friend zip, `vN` tag) without evidence from the two-instance test
  rig: screenshots and log excerpts saved under `D:\Fable2-LAN\research\<feature>-evidence\`.
- Every claim in a report is labelled **verified** (with the evidence path) or **unverified**.
  "Should work" is never reported as working.
- A feature counts as done only when its spec's acceptance scenarios pass on the rig, and
  the scenarios that need two real PCs are handed to Josh as a numbered test sheet
  (`D:\Fable2-LAN\COOP-TESTS.txt`).
- Josh has rejected four untested camera versions in the past. That is the reason for this
  principle; it is not negotiable for schedule.

### II. Saves Are Sacred (NON-NEGOTIABLE)

- Co-op play happens only in dedicated co-op campaign saves. Co-op code never writes to a
  player's solo saves.
- Development and rig runs use copied saves only (`D:\Fable2-LAN\mod-a`, `mod-b`, `mod-c`).
  Never write to `D:\Fable II PC EDITION\saves` or to the `D:\Fable2-LAN\mod\saves`
  junction during development.
- Before any change that can write a save, back up the slot to
  `D:\Fable2-LAN\backups\<timestamp>-<reason>\`.
- Player 2's hero is stored in the campaign folder next to the world save and written in
  the same save event, so the two can never disagree. Direct editing of `mainsave.bin` needs
  an explicit plan entry and a round-trip test (unpack, repack, load, diff).
- Every feature that touches hero data proves with a diff that the campaign save and player
  2's hero file are exactly as expected after a session: nothing lost, nothing duplicated,
  nothing leaked from one hero into the other, and the players' solo saves unchanged.

### III. Safe Installs

- Check that no `fable_2.exe` is running immediately before copying anything into an
  install folder (`mod`, `mod-a`, `mod-b`, `test-build*`).
- Never run the normal game (`D:\Fable II PC EDITION`) and the mod at the same time.
- Installs to Josh's real `mod` folder and new friend zips are release steps and need
  Josh's approval (see Approval Gates).

### IV. Opt-in and Reversible

- Every new co-op behaviour sits behind a cvar that defaults to off until it is verified,
  and is exposed in the F6 Co-op menu once verified.
- Turning the cvar off restores the previous behaviour exactly. A partner without the
  feature (older mod, or unmodded) must still be able to play plain henchman co-op.
- Release notes name the cvars and the F6 boxes to tick on both PCs.

### V. One Owner per Piece of State

- A co-op campaign is shared from the first minute to the last: both heroes always have the
  same story progress, and gold is one shared purse (property is a joint venture).
- The host (the player who created the campaign) owns the shared world: loaded region, NPCs,
  enemies, quests, story, doors, chests, shop stock, gold, time and weather.
- Each hero owns their own stats, XP, abilities, inventory, look, family and dog. (Tier 1: the
  host simulates player 2 and stores it in the campaign. Tier 2 onward: player 2's PC runs
  its own hero.)
- Any state with two writers needs a written rule in the spec for who wins, and a desync
  check on the rig.
- XP rule: whoever earns XP keeps it in the style they used (strength, skill, will); the
  partner receives the same amount as general XP; quest and general XP are split 50/50.

### VI. Smallest Hook That Works

Prefer, in this order: the game's own Lua API (run through the recomp Lua runner), a
recomp function override (strong symbol, `docs/hotfunc_overrides.md`), a mid-asm hook
(`fable_2_manifest.toml`), a data patch, and last a change to the game's network protocol.
Every game function we hook or call is recorded with its address and role in the feature's
`research.md`, so the next session does not rediscover it.

### VII. Autonomous Work, Human Checkpoints

Claude works autonomously on Josh's PC: research, code, builds, rig runs, evidence,
commits to feature branches, and test sheets. Josh is needed only for:

- **Review**: reading a feature's spec and plan before implementation starts, and a short
  release summary before each release.
- **Approvals**: the gates listed below.
- **Testing**: two-PC play tests with Calvin, which the rig cannot replace.

Claude never waits idle on Josh: while a gate is open it continues on work that does not
depend on it, and asks each question once, with a recommendation.

## Approval Gates

Josh's explicit approval (a message in the project thread) is required before:

1. Starting implementation of a feature (after spec + plan + tasks are written).
2. Installing into `D:\Fable2-LAN\mod` or building a friend zip for Calvin.
3. Any write to a real save (solo or co-op campaign), including Calvin's.
4. Spending money or creating accounts (a cloud server, a domain, paid services).
5. Pushing to a branch other than a feature branch, opening pull requests to upstream
   (himdo or rexglue), or anything published outside Josh's forks.
6. Deleting files outside scratch folders (`D:\Fable2-LAN\tmp`, `out`, rig instances).
7. Cutting a feature, or a scope change that alters a spec's user stories.

## Technical Constraints

- Two repositories: `WizitStrogg/Fable-2-Recomp` (game-side code, Lua scripts, specs) and
  `WizitStrogg/rexglue-sdk` (runtime, networking, LAN lobby), the latter as the submodule
  `thirdparty/rexglue-sdk`. Base branch for both: `systemlink`.
- Windows build: `build.cmd -release fable_2` with LLVM clang + Ninja and `vcvars64` from
  `E:\DevTools`. New `rexruntime.def` exports use names from the Windows `.obj`
  (`llvm-nm`) and sit mid-file so the Fable-2-Recomp thirdparty patches still apply.
- Josh's PC has `NoDefaultCurrentDirectoryInExePath=1`: scripts use `%~dp0` or `.\`.
- The joiner runs no game Lua while visiting the host's world. Joiner-side logic is
  native C++ (see `research\05-client-camera.md`).
- Game Lua runs only on the game thread (the v7 crash: Lua from the render thread corrupts
  the VM). All injected Lua goes through the recomp Lua runner's game-thread drain.

## Development Workflow

- One Spec Kit feature per tier slice under `specs/NNN-name/`; feature branches
  `coop/NNN-name` in both repositories, merged into `systemlink` after Josh's release
  approval.
- Order inside a feature: proofs (yes/no rig experiments) first, then foundations, then
  user stories in priority order, each ending at a checkpoint with evidence.
- Releases continue the existing numbering (`v14`, `v15`, ...). Each release has: rig
  evidence folder, updated `COOP-TESTS.txt` steps, install in `mod` (gated), friend zip
  (gated), and a one-paragraph summary for Josh.
- Status goes to the project thread at milestones only: a result, a blocker, or a question.

## Amendment log

- 1.1.0 (2026-10-23): Josh chose a dedicated co-op campaign (start and finish together, shared
  story and gold, fixed host). Principles II and V rewritten; XP rule added.

## Governance

This constitution supersedes other practices for co-op work. Amendments are proposed by
Claude or Josh in the project thread, need Josh's approval, and bump the version: MAJOR
for a removed or reversed principle, MINOR for a new principle or gate, PATCH for wording.
Every `/speckit-plan` run fills its Constitution Check against this file.

**Version**: 1.1.0 | **Ratified**: 2026-10-23 | **Last Amended**: 2026-10-23
