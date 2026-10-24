# Contract: co-op messages on the LAN lobby channel

Transport: `rex::kernel::xam::lobby::PublishData(key, value)` and `GetPeerData(key)` in
rexglue-sdk (`xam_lan_lobby.cpp`). Each peer publishes the latest value per key; peers read the
latest value from each other peer. There is no delivery guarantee per message, so every message
below is designed to be safe when re-read, re-sent or skipped.

## Keys

| Key | Published by | Value | When |
| --- | --- | --- | --- |
| `hero` | every PC | `COOPHERO:v1` line (existing, used only for v13-style drop-in co-op into solo saves) | as in v13 |
| `proto` | every PC | `COOPPROTO:<n>` with supported features, e.g. `COOPPROTO:2|campaign|kit|menu` | always |
| `cmd` | joiner | all unacknowledged `COOPCMD:v1` records, joined by `;` | on every new command, until acked |
| `ack` | host | `COOPACK:v1` | after applying commands |
| `p2state` | host | `COOPHERO:v2` of player 2 with `rev` | after each applied command, and every 2 s while a joiner is present |

Values longer than the measured per-value limit (Proof P1) are split into `key#i/n` keys; the
reader waits until all parts with the same `rev` are present.

## Versioning

- A feature is used only if both PCs list it in `proto`. Missing `proto` means a v13 partner:
  both sides behave as v13 (henchman + v1 look).
- Format versions are in the record prefix (`COOPHERO:v2`). Readers ignore records with a higher
  major version and show a notice once.

## Rules

1. The host applies `cmd` records strictly in `seq` order, each `seq` at most once per session.
   A gap (missing `seq`) stops processing until it arrives.
2. The joiner keeps every command until `ack.upto >= seq`, then drops it.
3. A command that cannot run now (cutscene, loading, player 2 not spawned) is not acked; the host
   retries it every second for up to 30 s, then acks it as `no:busy`.
4. `p2state.rev` only increases. The joiner ignores a lower or equal `rev`.
5. The joiner never writes any save; player 2 is saved by the host in the campaign slot.
6. All host-side Lua runs on the game thread through the recomp Lua runner.
