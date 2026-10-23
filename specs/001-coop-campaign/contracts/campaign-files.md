# Contract: campaign files

1. A slot is a co-op campaign if and only if it contains a valid `coop_campaign.toml`.
2. The marker is written once, after the first successful save with both players in the world;
   later saves never rewrite it (except `mod_version` when a newer mod saves it).
3. `coop_player2.txt` is written after every successful save of the slot, with the slot's
   `saveuid`, by temp file + rename. It is never written outside a save event.
4. Loading a campaign slot without its partner present never enters the world.
5. Co-op code writes nothing outside campaign slots and `coop-backups\`.
6. A backup zip contains the whole slot folder and a `manifest.txt` of SHA-256 hashes; the
   receiver verifies the hashes before keeping it.
