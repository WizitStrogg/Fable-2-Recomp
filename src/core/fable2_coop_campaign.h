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
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
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

// --- campaign marker (coop_campaign.toml, data-model.md) ----------------------

struct Marker {
  int format = 1;
  std::string campaign_id, name, created;
  std::string host_name, host_xuid, partner_name, partner_xuid;
  std::string p2_gender = "M";  // M / F
  std::string childhood = "skip";
  std::string xp_rule = "earner_style_partner_general_v1";
  std::string mod_version;
};

inline std::string toml_quote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out.push_back('\\');
    if (c == '\n' || c == '\r') continue;
    out.push_back(c);
  }
  return out + "\"";
}

inline std::string FormatMarker(const Marker& m) {
  std::string s = "# Fable II co-op campaign marker (feature 001). Written by the game; do not edit.\n";
  s += "format = " + std::to_string(m.format) + "\n";
  auto kv = [&](const char* k, const std::string& v) { s += std::string(k) + " = " + toml_quote(v) + "\n"; };
  kv("campaign_id", m.campaign_id);
  kv("name", m.name);
  kv("created", m.created);
  kv("host_name", m.host_name);
  kv("host_xuid", m.host_xuid);
  kv("partner_name", m.partner_name);
  kv("partner_xuid", m.partner_xuid);
  kv("p2_gender", m.p2_gender);
  kv("childhood", m.childhood);
  kv("xp_rule", m.xp_rule);
  kv("mod_version", m.mod_version);
  return s;
}

// Reads the flat key = "value" lines FormatMarker writes. Returns nothing
// unless format and campaign_id are present (contract: a slot is a campaign
// if and only if it has a valid marker).
inline std::optional<Marker> ParseMarker(std::string_view text) {
  std::map<std::string, std::string> kv;
  while (!text.empty()) {
    const size_t nl = text.find('\n');
    std::string_view line = text.substr(0, nl);
    text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
    if (line.empty() || line.front() == '#') continue;
    const size_t eq = line.find(" = ");
    if (eq == std::string_view::npos) continue;
    std::string key(line.substr(0, eq));
    std::string_view v = line.substr(eq + 3);
    std::string val;
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
      v = v.substr(1, v.size() - 2);
      for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) ++i;
        val.push_back(v[i]);
      }
    } else {
      val = std::string(v);
    }
    kv[key] = val;
  }
  if (kv["format"] != "1" || kv["campaign_id"].empty()) return std::nullopt;
  Marker m;
  m.campaign_id = kv["campaign_id"];
  m.name = kv["name"];
  m.created = kv["created"];
  m.host_name = kv["host_name"];
  m.host_xuid = kv["host_xuid"];
  m.partner_name = kv["partner_name"];
  m.partner_xuid = kv["partner_xuid"];
  m.p2_gender = kv["p2_gender"] == "F" ? "F" : "M";
  m.childhood = kv["childhood"];
  m.xp_rule = kv["xp_rule"];
  m.mod_version = kv["mod_version"];
  return m;
}

// Writes <dir>/<name> through a temp file and a rename, so a crash leaves
// either the old file or the new one, never half of one (contract rule 3).
inline bool WriteFileAtomic(const std::filesystem::path& dir, const std::string& name,
                            const std::string& text) {
  std::error_code ec;
  const auto tmp = dir / (name + ".tmp");
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    out.flush();
    if (!out) return false;
  }
  std::filesystem::rename(tmp, dir / name, ec);  // replaces an existing file on Windows
  if (ec) {
    REXSYS_ERROR("[coop] could not write {}: {}", (dir / name).string(), ec.message());
    std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

inline std::optional<Marker> ReadMarker(const std::filesystem::path& dir) {
  std::ifstream in(dir / "coop_campaign.toml", std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream text;
  text << in.rdbuf();
  return ParseMarker(text.str());
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
