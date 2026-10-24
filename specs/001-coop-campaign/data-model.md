# Data Model: Co-op campaign and lobby

## Co-op campaign marker (`coop_campaign.toml`, in the host's slot folder)

| Field | Type | Notes |
| --- | --- | --- |
| `format` | int | 1 |
| `campaign_id` | GUID string | created at Start |
| `name` | string | defaults to "Josh & Calvin" |
| `created` | ISO time | |
| `host_name`, `host_xuid` | string, hex | online identity (derived from PC name, as today) |
| `partner_name`, `partner_xuid` | string, hex | the only player allowed to join |
| `p2_gender` | `M`/`F` | chosen in the lobby |
| `childhood` | `skip` (later: `sparrow_rose`) | |
| `xp_rule` | `earner_style_partner_general_v1` | used by feature 002 |
| `mod_version` | string | minimum version to join |

Written only after both players are in the world and the first save succeeded.

## Player 2 hero file (`coop_player2.txt`, in the host's slot folder)

One `COOPHERO:v2` line (format defined in `specs/002-player2-kit/data-model.md`) plus
`saveuid=<hex>` matching the slot's `saveuid.bin` at the time of the save. Written in the same
save event, atomically (temp file + rename). On load, a mismatched `saveuid` means the file is
from another save: the newest backup with a matching `saveuid` is used, or player 2 starts from
the campaign's starting kit with a warning.

## Lobby

| Field | Values |
| --- | --- |
| `kind` | `new` (creating a campaign) or `continue` (campaign waiting for its partner) |
| `campaign_id` | for `continue` |
| `host_name`, `partner_name` | |
| `p2_gender`, `childhood` | choices |
| `host_ready`, `partner_ready` | bool |
| `state` | see below |

Host states: `Hosting` → `PartnerJoined` → `BothReady` → `Starting` → `InWorld` → (`PartnerLost` →
`Saving` → `Hosting`).

Partner states: `Browsing` → `Joined` → `Ready` → `WaitingForHostWorld` → `Joining` → `InWorld` →
(`HostLost` → `Browsing`).

## Shared purse

The host hero's gold. Player 2's gold is held at 0; every player-2 gold gain is added to the host
purse (P5 decides how).

## Partner backup

`<exe dir>\coop-backups\<campaign_id>\<yyyymmdd-hhmmss>.zip` with the slot folder and a
`manifest.txt` of file hashes. The last 10 backups per campaign are kept.
