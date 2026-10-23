# Contract: session ledger

Files and lifecycle are in `../data-model.md`. This contract fixes the apply rules.

## Writing (joiner, during a session)

1. At join: write `meta.txt`, `baseline.txt` (own capture made in own world, before the join),
   and copy `herosave.bin` to `herosave-before.bin`.
2. On each new `p2state.rev`: write `latest.tmp`, flush, rename over `latest.txt`. The rename is
   the commit point; a crash leaves either the previous or the new snapshot, never a partial one.
3. At session end (leave, disconnect, exit): append `closed=<time>` to `meta.txt`. A missing
   `closed` line means a crash; the ledger is still applied from `latest.txt`.

## Applying (joiner, own world, after its save loads, before any join)

1. Find ledgers for this save slot without `applied.txt`, oldest first.
2. Back up the save slot to `D:\Fable2-LAN\backups\<time>-ledger-apply\` (or `<exe dir>\backups\`
   on Calvin's PC).
3. Compute deltas `latest - baseline`:
   - XP pools: `delta_xp[i]`, minus what the game's own write-back already added (compare the
     current `herosave.bin` with `herosave-before.bin`, Proof P5).
   - Gold: same reconcile as XP.
   - Items: per record id, `count_latest - count_baseline`; positive adds, negative removes.
   - Ability levels, titles, active title, dyes, hair, tattoos, worn and wielded items: set to the
     `latest` values.
   - Morality, purity, renown, fat, strength: `Modify` by the delta.
4. Apply with `coop_ledger_apply.lua` on the game thread. Re-capture and compare with the
   expected result; log every difference.
5. Write `applied.txt` with the hash of `latest.txt` and the result, then move the folder to
   `saves\coop\applied\`. The game's next normal save persists the result.

## Guarantees

- Applied at most once (hash in `applied.txt`; a ledger whose hash is already recorded is
  skipped).
- Never applied to a different save slot than the one in `meta.txt`.
- Never touches the host's saves.
- Turning `coop_ledger` off stops writing and applying; existing ledgers stay on disk.
