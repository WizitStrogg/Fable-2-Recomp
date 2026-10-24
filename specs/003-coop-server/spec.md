# Feature Specification: Co-op server (find, relay, backups)

**Feature Branch**: `coop/003-coop-server`

**Created**: 2026-10-23

**Status**: Draft (plan after Josh approves a hosting choice; approval gate 4)

**Input**: Unlimited-time vision: a small server of our own to find each other without ZeroTier,
connect through home routers, and keep campaign backups.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Find and join by name over the internet (Priority: P1)

Calvin sees Josh's co-op lobby (feature 001) from anywhere on the internet, without ZeroTier, and
joins it.

**Independent Test**: Two instances on different networks (rig instance B behind a phone hotspot)
see each other's co-op lobby and join; ZeroTier off on both.

**Acceptance Scenarios**:

1. **Given** both PCs are online without ZeroTier, **When** both start the mod, **Then** each sees
   the other in F6 within 10 seconds.
2. **Given** a direct connection is impossible (strict NAT), **When** they join, **Then** traffic
   goes through the relay and the session works, with the relay shown in F6.

### User Story 2 - Campaign backups on the server (Priority: P2)

After every co-op session, the campaign slot is backed up to the server (in addition to the copy
on the partner's PC from feature 001) and can be restored from F6 by either player.

### User Story 3 - Private pairing (Priority: P1)

Only people Josh has paired with (a pairing code entered once) can see his lobbies or join him.

### Edge Cases

- Server down: LAN and ZeroTier discovery keep working as today.
- Version mismatch between partners: shown in F6 before joining.

## Requirements *(mandatory)*

- **FR-001**: A rendezvous service that pairs players by a code and exchanges their addresses.
- **FR-002**: UDP hole punching between the PCs, with a relay fallback.
- **FR-003**: The game's System Link traffic (rexglue-sdk sockets) MUST run over that connection
  transparently.
- **FR-004**: Campaign backups uploaded and listed per campaign; restore needs a confirmation.
- **FR-005**: No personal data beyond a chosen name, pairing codes and saves; saves visible only to
  their owner.

## Success Criteria *(mandatory)*

- **SC-001**: Join works between two different home networks without ZeroTier in 5 of 5 tries.
- **SC-002**: Added latency through the relay under 30 ms when relayed within the same country.

## Assumptions

- Hosting choice needs Josh's approval (gate 4): a small cloud VM or a free tier. A candidate
  networking library is Valve's open-source GameNetworkingSockets (NAT traversal, relay) or ENet
  plus a custom STUN/relay; decided in this feature's research.
- Can be built alongside 001 and 002 since it only touches the SDK's network layer.
