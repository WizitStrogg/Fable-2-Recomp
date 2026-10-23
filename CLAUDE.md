# Fable II recomp: notes for Claude

## Co-op work (System Link mod)

Co-op features are planned with Spec Kit:

- Rules, approval gates and autonomy: `.specify/memory/constitution.md` (read it first, every session).
- Roadmap, feature status and the autonomy runbook: `specs/README.md`.
- Current feature: `specs/001-coop-campaign/` (spec, plan, research, tasks). Work through `tasks.md`
  in order, tick tasks with a result and evidence path, commit to `coop/001-coop-campaign` in this
  repo and in `thirdparty/rexglue-sdk`.
- Spec Kit skills: `/speckit-specify`, `/speckit-plan`, `/speckit-tasks`, `/speckit-implement`,
  `/speckit-analyze`. Set `SPECIFY_FEATURE=001-coop-campaign` because branches carry a `coop/` prefix.

Never skip the constitution's non-negotiables: proven on the rig before shipping, copied saves
only, no game running during installs, Josh approves releases.

## Workspace on Josh's PC

- `D:\Fable2-LAN\` : rig instances `mod-a` (host) and `mod-b` (joiner), Josh's real install `mod`
  (gated), research notes and evidence `research\`, rig tools `tools\`, backups `backups\`,
  friend zips `friend-package\`, two-PC test sheet `COOP-TESTS.txt`.
- Build: `build.cmd -release fable_2` after `vcvars64` from `E:\DevTools`.
