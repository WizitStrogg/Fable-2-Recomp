# Data Model: Full hero, host-run

All records are single-line text (pipe-separated `key=value`, the style of the existing
`COOPHERO:v1` line) so they can travel over the lobby channel, be printed to the game log, and be
diffed by hand.

## Hero profile v2 (`COOPHERO:v2`)

Captured by `CoopHero.Capture(entity)` in `coop_lib.lua`. On the joiner it is captured in its
own world (baseline); on the host it is captured from player 2 (snapshots).

| Field | Type | Source (Lua) | Notes |
| --- | --- | --- | --- |
| `g` | `M`/`F` | gender | existing v1 |
| `mor`, `pur`, `ren` | number | `Stats.GetMorality/GetPurity/GetRenown` | existing v1 |
| `fat`, `str`, `skl`, `wil`, `hgt`, `age` | number | `Stats.Get*`, `Age.GetAge` | `hgt`, `age` new |
| `xp` | 4 numbers | `Experience.Get(entity, EXPERIENCE_GENERAL/STRENGTH/SKILL/WILL)` | unspent pools |
| `xpt` | 4 numbers | `Experience.GetTotalEarned` | for reconcile only |
| `ab` | 15 × `index:level` | `Stats.GetHeroAbilityLevel` | existing v1 had these |
| `gold` | integer | `Money` getter (P3 decides whose purse) | new |
| `items` | list of `recordhex:count` | `Inventory.GetNumberOfItems` + `GetItemByIndex` | new, sorted by record id |
| `worn` | list of `recordhex` | appearance manager | existing v1 |
| `weap` | melee and ranged `recordhex` | weapon set manager | existing v1 |
| `dye`, `hair`, `tat` | lists of `recordhex` | appearance manager | new |
| `titles` | list of title ids, active title | `Stats` title getters | new |
| `rev` | integer | capture counter | host snapshots only |

Validation: a profile is accepted only if `items` count matches the item total read in the same
capture, and every record id resolves (`GDB.GetRecord("#hex")`). Unknown ids (DLC the host lacks)
are kept in the profile but skipped on apply, with a notice.

## Co-op command (`COOPCMD:v1`)

| Field | Values |
| --- | --- |
| `seq` | integer, increases by 1 per command from this joiner |
| `op` | `equip`, `unequip`, `use`, `learn`, `dye`, `hair`, `refresh` |
| `rec` | record id hex (item ops) |
| `ab`, `lvl` | ability index and target level (`learn`) |
| `slot` | `melee`, `ranged`, `worn` (equip ops, when ambiguous) |

State: `queued` (on joiner) → `sent` (in published `cmd`) → `applied` or `rejected:<reason>`
(in host `ack`) → removed from the queue.

## Command result (`COOPACK:v1`)

| Field | Values |
| --- | --- |
| `upto` | highest `seq` applied |
| `res` | list of `seq:ok` or `seq:no:<reason>` for the last 16 commands |
| `rev` | `p2state` revision that includes the results |

Reasons: `noitem`, `noxp`, `maxlevel`, `questitem`, `busy` (cutscene or loading; retried), `fail`.

## Player-2 snapshot (`p2state`)

A `COOPHERO:v2` captured on the host from player 2, with `rev`. Published after every applied
command and every 2 seconds while a joiner is present. The joiner's menu shows the newest `rev`;
the ledger stores the newest `rev` to disk.

## Session ledger

Files under `<exe dir>\saves\coop\ledger-<host-name>-<yyyymmdd-hhmmss>\` on the joiner:

| File | Written | Content |
| --- | --- | --- |
| `meta.txt` | at join | joiner save slot, host name, protocol version, start time |
| `baseline.txt` | at join, once | joiner's own `COOPHERO:v2` captured in its own world before joining |
| `latest.txt` | each new `p2state` rev, atomic replace (write temp, rename) | newest snapshot |
| `herosave-before.bin` | at join | copy of the joiner's `herosave.bin`, for reconcile with the game's own write-back |
| `applied.txt` | after apply | hash of `latest.txt`, time, result summary |

Lifecycle: `open` (session running) → `closed` (session ended or game exited) → `applied`
(deltas applied to the joiner's save, folder moved to `saves\coop\applied\`). A folder with
`latest.txt` but no `applied.txt` is applied on the next load of the matching save slot.

## Relationships

- One joiner save slot → many ledgers over time, applied oldest first.
- One ledger → one baseline, one latest snapshot.
- One session → one command sequence; `seq` restarts at 1 per session.
