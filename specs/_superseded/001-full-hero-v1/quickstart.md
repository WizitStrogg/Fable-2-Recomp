# Quickstart: Full hero, host-run

How to build, run and verify this feature. Claude follows the rig part autonomously; Josh and
Calvin follow the two-PC part after a release.

## Build

```cmd
cd /d D:\Fable2-LAN\Fable-2-Recomp
git checkout coop/001-full-hero
git -C thirdparty\rexglue-sdk checkout coop/001-full-hero
call E:\DevTools\...\vcvars64.bat
build.cmd -release fable_2
```

## Rig run (one PC, two instances, copied saves)

1. Confirm no `fable_2.exe` is running, then copy the build into `D:\Fable2-LAN\mod-a` (host,
   127.0.0.1) and `mod-b` (joiner, 127.0.0.2) with `tools\make_test_instances.ps1`.
2. In both `fable_2.toml` set `coop_full_hero = true`, `coop_menu = true`, `coop_ledger = true`.
3. Start both with `tools\gamectl.ps1 start a` / `start b`; load each instance's own save.
4. On B, before joining: `luafile coop_capture` and keep the log line as the expected profile.
5. Join B to A (F9 on B). On A: `luafile coop_capture` on player 2 (`coop_apply status`).
6. Diff: `python tools\coop_diff.py <B own capture> <A player-2 capture>` → no differences
   except `rev`.
7. Story checks per user story (see tasks.md checkpoints). Save screenshots of both windows to
   `D:\Fable2-LAN\research\001-evidence\` with the task id in the file name.
8. Leave, reload B's own save, capture, and diff against `baseline + expected deltas`.
9. Diff A's save item count and A's hero capture against the pre-run values.

## Two-PC test (Josh and Calvin, after a release)

1. Both PCs on the same release (F6 shows the version).
2. On both: F6, tick "Partners appear as their own hero", "Full hero", "Co-op menu",
   "Save my progress from co-op", then Save to config.
3. Follow the numbered steps for this release in `D:\Fable2-LAN\COOP-TESTS.txt` and report
   which steps passed.

## Turning it off

Untick the boxes in F6 (or set the three cvars to false). Play falls back to v13 behaviour.
Ledgers already on disk stay in `saves\coop\` and are not applied while `coop_ledger` is off.
