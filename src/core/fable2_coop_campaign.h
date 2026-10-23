// fable_2 - co-op campaign (feature 001, specs/001-coop-campaign).
//
// A co-op campaign is a save slot on the host with a coop_campaign.toml marker,
// only ever played by its two players. This header holds the switch and the
// protocol announcement; the lobby, front-end intercept, save hook and purse
// live in their own fable2_coop_*.h modules.
//
// Protocol announcement (contracts/lobby-protocol.md): every PC publishes the
// lobby key "proto" = "COOPPROTO:<n>|<feature>|..." listing the co-op features
// it runs, so a host can tell a partner on an older mod ("needs v14").
// With coop_campaign off nothing is announced and nothing changes.

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>
#include <rex/kernel/xam/lan_lobby.h>
#include <rex/logging.h>

#include "fable2_f5_lua.h"

REXCVAR_DEFINE_BOOL(coop_campaign, false, "Network",
                    "Co-op campaign: New Game offers Solo or Co-op, and a co-op campaign is "
                    "only played together (experimental)");

namespace fable2::coopcampaign {

namespace lobby = rex::kernel::xam::lobby;

// Protocol version of the "proto" list itself, and the campaign format a
// partner must speak to join (both sides must match, contract rule 3).
constexpr int kProtoVersion = 2;
constexpr std::string_view kCampaignFeature = "campaign";

// The "proto" value this PC announces, empty when no co-op feature is on.
inline std::string ProtoValue() {
  if (!REXCVAR_GET(coop_campaign)) return {};
  return "COOPPROTO:" + std::to_string(kProtoVersion) + "|" + std::string(kCampaignFeature);
}

// Features listed in a peer's "proto" value; empty when the peer announced
// none or speaks another protocol version.
inline std::vector<std::string> ParseProto(std::string_view value) {
  std::vector<std::string> out;
  const std::string head = "COOPPROTO:" + std::to_string(kProtoVersion);
  if (value.substr(0, head.size()) != head) return out;
  value.remove_prefix(head.size());
  while (!value.empty()) {
    if (value.front() == '|') value.remove_prefix(1);
    const size_t bar = value.find('|');
    const std::string_view item = value.substr(0, bar);
    if (!item.empty()) out.emplace_back(item);
    if (bar == std::string_view::npos) break;
    value.remove_prefix(bar);
  }
  return out;
}

// True if the player at `ip` announces the campaign feature.
inline bool PeerSupportsCampaign(uint32_t ip) {
  for (const auto& d : lobby::GetPeerData("proto")) {
    if (d.ip != ip) continue;
    for (const auto& f : ParseProto(d.value))
      if (f == kCampaignFeature) return true;
  }
  return false;
}

// Republishes "proto" whenever the announced value changes (cvar toggled).
// An empty value tells partners the feature went away; with the switch off
// from the start nothing is ever published.
inline std::string g_published_proto;
inline void proto_tick() {
  const std::string want = ProtoValue();
  if (want == g_published_proto) return;
  const lobby::LobbyStatus status = lobby::GetLobbyStatus();
  if (!status.networking_enabled) return;
  if (lobby::PublishData("proto", want)) {
    g_published_proto = want;
    REXSYS_INFO("[coop] proto: '{}'", want);
  }
}

inline int64_t g_next_tick_ms = 0;
inline void (*g_prev_frame_hook)() = nullptr;

inline void OnFrame() {
  if (g_prev_frame_hook) g_prev_frame_hook();
  const int64_t now = fable2::f5lua::steady_ms();
  if (now < g_next_tick_ms) return;
  g_next_tick_ms = now + 500;
  proto_tick();
}

// Chains onto the per-frame hook set by fable2_coop_hero.h (included earlier
// in main.cpp, so its static init has already run).
inline bool Install() {
  g_prev_frame_hook = fable2::f5lua::g_frame_hook;
  fable2::f5lua::g_frame_hook = &OnFrame;
  return true;
}
inline const bool g_installed = Install();

}  // namespace fable2::coopcampaign
