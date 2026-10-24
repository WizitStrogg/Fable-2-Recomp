# Fable II true co-op: program roadmap

This folder holds the Spec Kit features that take the co-op mod from "the joiner is a
henchman" (v13) to "two separate experiences in one shared world". The rules for all of
them are in `.specify/memory/constitution.md`.

Background documents (read these before planning a new feature):

- Feasibility study: <https://claude.ai/artifact/6EHRhMNTUEftKm6pdn2Sx2>
- Unlimited-time vision and feature catalogue: <https://claude.ai/artifact/8Lv4XdojSY9w29Pq7KkcSz>
- Research notes on Josh's PC: `D:\Fable2-LAN\research\01..05-*.md`, decompiled scripts in
  `research\lua\`, tools `strxref.py`, `fnstrings.py`, `luabind_map.tsv`.

## Features

| # | Feature | Tier | Status | Depends on | Size (estimate) |
| --- | --- | --- | --- | --- | --- |
| 001 | [Co-op campaign and lobby](001-coop-campaign/spec.md) | 1 | Implemented on `coop/001-coop-campaign` (both repos), proven on the rig (launches 16-19, evidence in `research-evidence\`); v14 waits for Gate 2 (install, friend zip, merge) and a two-PC test | v13 | done in 1 week of sessions |
| 002 | [Player 2's full kit and XP split](002-player2-kit/spec.md) | 1 | Spec and research ready; plan and tasks after 001 ships | 001 | 4 to 6 weeks |
| 003 | [Co-op server: find, relay, backups](003-coop-server/spec.md) | 2 | Spec draft | none (can run beside 001) | 2 to 4 weeks |
| 004 | [Split authority: your PC runs your hero](004-split-authority/spec.md) | 2 | Spec draft; plan after its proofs | 001, 002, 003 | 3 to 5 months |
| 005 | [Two heroes, one story](005-two-heroes-one-story/spec.md) | 3 | Spec draft | 002, 004 | 3 to 6 months |
| 006 | [Research frontier](006-research-frontier/spec.md) | 4 | Spec draft (experiments only) | 004 | open-ended |

Sizes are estimates, not measurements. Each feature starts with yes/no proofs that can
change its size, and the estimate is updated in this table after them.

**Design basis (Josh, 2026-10-23, constitution 1.1.0):** co-op is a dedicated campaign created
together from New Game and only played together. Story progress is always shared, gold is one
purse, the creator always hosts, and XP follows the earner's style with the partner getting the
same amount as general XP. Solo saves and the game's normal drop-in co-op are untouched. Earlier
specs built on syncing personal saves are in `_superseded/`.

## How a feature moves (Spec Kit flow)

1. `/speckit-specify` writes `spec.md` (what and why, user stories, acceptance scenarios).
2. `/speckit-clarify` only if the spec has open `[NEEDS CLARIFICATION]` items that need Josh.
3. `/speckit-plan` writes `plan.md`, `research.md`, `data-model.md`, `contracts/`,
   `quickstart.md`, with the Constitution Check filled in.
4. `/speckit-tasks` writes `tasks.md`.
5. **Gate 1: Josh reviews spec + plan + tasks and says go.**
6. `/speckit-implement` works through `tasks.md` on the feature branch, ticking tasks and
   saving evidence. Each user story ends at a checkpoint that is proven on the rig.
7. **Gate 2: Josh approves the release** (install in `mod`, friend zip), then plays it with
   Calvin using the steps in `COOP-TESTS.txt`.
8. Findings from Josh's test go back into the spec as fixes or new tasks.

## Autonomy runbook (how Claude works on Josh's PC)

Claude works through a Remote Control session on WizitPC in `D:\Fable2-LAN`. One session
works on one feature at a time.

**Start of every session**

1. Read `.specify/memory/constitution.md`, this file, and the current feature's `tasks.md`.
2. `git status` in `Fable-2-Recomp` and `thirdparty/rexglue-sdk`; confirm the feature
   branch `coop/NNN-name` is checked out in both.
3. Check that no `fable_2.exe` is running before touching any install folder.

**Work loop**

1. Take the first unticked task whose dependencies are done.
2. Do it. For code: build with `build.cmd -release fable_2`, install into the rig instances
   (`mod-a` host, `mod-b` joiner), run the rig with `tools\gamectl.ps1`, and capture
   evidence into `research\NNN-evidence\`.
3. Tick the task in `tasks.md` with a one-line result and evidence path. Commit to the
   feature branch with a message naming the task id (for example `T020: lobby new flow`).
4. At each checkpoint, post one milestone message to the project thread: what is now
   proven, with evidence paths, and what is next.

**When stuck**

- A proof fails: record it in `research.md`, try the fallback named in the plan, and only
  then raise it to Josh with a recommendation.
- Two hours without progress on one task: stop, write down what was tried, and post a
  short blocker message with a proposed next step.
- Never work around a gate. While waiting on Josh, continue with tasks that do not depend
  on the answer.

**What Josh is asked for**

| When | What Josh does | Typical time |
| --- | --- | --- |
| Feature planned | Read spec + plan summary, reply "go" or comments | 15 to 30 min |
| Release ready | Approve install and friend zip | 2 min |
| After a release | Play the `COOP-TESTS.txt` steps with Calvin, report what broke | 30 to 60 min |
| Money or accounts (feature 003) | Approve and create the account | 15 min |

## Branches and versions

- Feature branches: `coop/001-coop-campaign`, `coop/002-player2-kit`, ... in both
  `Fable-2-Recomp` and `rexglue-sdk`, branched from `systemlink`.
- Releases continue from v13: v14, v15, ... Each feature's tasks name which release a user
  story ships in.
- Spec Kit feature detection: set `SPECIFY_FEATURE=001-coop-campaign` (or run the scripts
  with the feature directory), since the branch names carry a `coop/` prefix.
