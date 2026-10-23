# Data Model: Player 2's full kit

All records are single-line text (pipe-separated `key=value`, the style of the existing
`COOPHERO:v1` line) so they can travel over the lobby channel, be printed to the game log, and be
diffed by hand.

## Hero profile v2 (`COOPHERO:v2`)

Captured by `CoopHero.Capture(entity)` in `coop_lib.lua` from player 2 on the host. Stored in the
campaign slot as `coop_player2.txt` (feature 001) and published as `p2state` for the joiner's
menu. Gold is not part of the profile: the campaign has one shared purse (feature 001).

| Field | Type | Source (Lua) | Notes |
| --- | --- | --- | --- |
| `g` | `M`/`F` | gender | existing v1 |
| `mor`, `pur`, `ren` | number | `Stats.GetMorality/GetPurity/GetRenown` | existing v1 |
| `fat`, `str`, `skl`, `wil`, `hgt`, `age` | number | `Stats.Get*`, `Age.GetAge` | `hgt`, `age` new |
| `xp` | 4 numbers | `Experience.Get(entity, EXPERIENCE_GENERAL/STRENGTH/SKILL/WILL)` | unspent pools |
| `xpt` | 4 numbers | `Experience.GetTotalEarned` | for reconcile only |
| `ab` | 15 × `index:level` | `Stats.GetHeroAbilityLevel` | existing v1 had these |
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
command and every 2 seconds while a joiner is present. The joiner's menu shows the newest `rev`.

## Relationships

- One campaign → one player 2 hero file, rewritten at every save (feature 001).
- One session → one command sequence; `seq` restarts at 1 per session.

## XP award (`COOPXP:v1`, host log line, also kept in the campaign for audit)

| Field | Values |
| --- | --- |
| `src` | `kill`, `quest`, `orb`, `other` |
| `earner` | `p1`, `p2`, `both` |
| `type` | `strength`, `skill`, `will`, `general` |
| `amount` | XP the game would award solo |
| `p1`, `p2` | what each hero received |

Rule (constitution V): typed XP goes to the earner in its type; the partner receives the same
amount as general XP. General and quest XP: each hero receives half.
