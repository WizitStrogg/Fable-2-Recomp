# Contract: lobby protocol

Transport: lobby keyed data (`rex::kernel::xam::lobby::PublishData` / `GetPeerData`), latest
value per peer, on every adapter (LAN and ZeroTier).

## Keys

| Key | Publisher | Value |
| --- | --- | --- |
| `proto` | both | `COOPPROTO:2|campaign` plus other features (shared with 002) |
| `coop_lobby` | host | `COOPLOBBY:v1|kind=new|host=Josh|gender2=F|childhood=skip|hready=0|state=Hosting` (`kind=continue|cid=<guid>|partner=Calvin` when a campaign waits) |
| `coop_seat` | partner | `COOPSEAT:v1|host=<host name>|cid=<guid or new>|gender=F|ready=1` |
| `coop_go` | host | `COOPGO:v1|cid=<guid>|seq=<n>` when the host's world is ready for the partner to join |

## Rules

1. The partner joins a lobby by publishing `coop_seat` naming the host. The host accepts the first
   seat for a `new` lobby, or only the campaign's partner for a `continue` lobby, and echoes the
   partner's name in `coop_lobby`.
2. Choices are owned: the partner owns `gender`, the host owns `childhood`. Each side shows the
   other's latest value.
3. Only the host starts. Start is enabled when both are ready and both `proto` lists include
   `campaign` with the same campaign format version.
4. After Start, the host loads the world and publishes `coop_go`; the partner then joins through
   join-via-presence. `seq` increases on every new attempt so a stale `coop_go` is ignored.
5. Leaving the lobby clears the publisher's key. A seat or lobby not refreshed for 10 s is gone.
