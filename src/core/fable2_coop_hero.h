// fable_2 - "bring your own hero" and story sharing for System Link co-op.
//
// The co-op partner appears in the host's world as a henchman. This module
// turns that henchman into the partner's own hero by running CoopHero.Apply
// (data/scripts/recomp/coop_lib.lua) on it with the partner's profile line
//   COOPHERO:v1|g=..|mor=..|...|worn=<record ids>|weap=<record ids>
// and shares the host's story state with the joiner.
//
// Data travels over the LAN lobby's keyed data channel
// (rex::kernel::xam::lobby::PublishData / GetPeerData):
//   "hero"  = a player's latest COOPHERO line (printed by coop_capture)
//   "story" = the host's COOPSTORY parts, joined (printed by
//             coop_story_capture)
//
// With coop_own_hero on:
//   * every PC runs coop_capture once its save is loaded (and every few
//     minutes after), which publishes its "hero";
//   * the host (in its own world, not in someone else's) applies the most
//     recent partner "hero" to the henchman once the henchman has spawned,
//     again for every new revision and after every respawn.
// The joiner always keeps the host's latest "story" in
// saves/coop_story_pending.txt (not applied yet).
//
// Console: `coop_apply` applies the partner's hero now, `coop_apply own`
// applies this PC's own profile (local-view fallback / self test), or
// `coop_apply <COOPHERO line>`.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/kernel/xam/lan_lobby.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "fable2_f5_lua.h"
#include "fable2_lua_console.h"

REXCVAR_DEFINE_BOOL(coop_own_hero, false, "Network",
                    "Co-op partners appear as their own hero instead of the premade "
                    "henchman (experimental)");
REXCVAR_DEFINE_STRING(coop_partner_profile, "", "Network",
                      "Override: a COOPHERO line to apply to the henchman instead of the "
                      "profile received from the partner (for testing)");
REXCVAR_DEFINE_INT32(coop_apply_delay_ms, 1500, "Network",
                     "Delay between the henchman spawning and applying the partner's hero");
REXCVAR_DEFINE_STRING(coop_story_sync, "ask", "Network",
                      "After playing in someone else's game, bring their story progress into "
                      "your own save: 'ask', 'always' or 'never' (forward only)");
REXCVAR_DEFINE_STRING(coop_story_pending, "", "Network",
                      "Read-only: the story waiting to be brought into this save "
                      "('<host> position <n>'), empty if none");
REXCVAR_DEFINE_BOOL(coop_own_camera, false, "Network",
                    "Each player's camera follows only their own hero in co-op, like the "
                    "single-player camera (experimental)");
REXCVAR_DEFINE_BOOL(coop_camera_follow_partner, false, "Network",
                    "When in someone else's game, point this PC's camera at your own "
                    "character (use only if the camera follows the host's hero)");
REXCVAR_DEFINE_INT32(coop_leash_outdoor_m, 0, "Network",
                     "Host only: how far apart co-op players may be outdoors, in metres "
                     "(0 = game default, at most 50)");
REXCVAR_DEFINE_INT32(coop_leash_indoor_m, 0, "Network",
                     "Host only: how far apart co-op players may be indoors, in metres "
                     "(0 = game default, at most 20)");

namespace fable2::coophero {

namespace lobby = rex::kernel::xam::lobby;

using Clock = std::chrono::steady_clock;
inline int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch())
      .count();
}

inline std::mutex& mtx() {
  static std::mutex m;
  return m;
}

// --- state (guarded by mtx unless atomic) ----------------------------------
inline std::string& own_profile() {
  static std::string s;
  return s;
}
inline std::map<int, std::string>& story_parts() {
  static std::map<int, std::string> m;
  return m;
}
inline std::atomic<int64_t> g_spawned_at_ms{0};  // last henchman spawn
inline std::atomic<uint32_t> g_spawn_epoch{0};   // increments per spawn

// Host-side apply bookkeeping (game thread only).
inline uint32_t g_applied_rev = 0;
inline uint32_t g_applied_ip = 0;
inline uint32_t g_applied_epoch = 0;
// Joiner-side story bookkeeping (game thread only).
inline uint32_t g_story_rev = 0;
inline uint32_t g_story_ip = 0;
// Auto-capture timing (game thread only).
inline int64_t g_ready_since_ms = 0;
inline int64_t g_next_capture_ms = 0;  // 0 = no capture scheduled
inline int64_t g_next_check_ms = 0;
// Which world we are in (host: 1, joined: host ip, else 0); a change is a
// session event (load into a world, join, leave).
inline uint64_t g_world_key = ~0ull;
inline bool g_world_changed = false;

constexpr int64_t kFirstCaptureDelayMs = 20'000;
constexpr int64_t kCheckEveryMs = 500;
// Game script loads (level loads) come in bursts: act once they settle.
constexpr int64_t kLoadSettleMs = 5'000;

// --- helpers ---------------------------------------------------------------

inline std::string read_lib() {
  const auto file = fable2::f5lua::g_data_root / "data" / "scripts" / "recomp" / "coop_lib.lua";
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    REXSYS_ERROR("[coop] {} not found", file.string());
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Queues CoopHero.Apply(<henchman>, <profile>). Returns false if nothing was
// queued (no usable profile, no library).
inline bool ApplyToHenchman(const std::string& profile, const std::string& why) {
  if (profile.rfind("COOPHERO:v1|", 0) != 0) {
    REXSYS_WARN("[coop] no usable hero profile to apply ({})", why);
    return false;
  }
  const std::string lib = read_lib();
  if (lib.empty()) return false;
  const std::string eq = fable2::luaconsole::long_bracket_level(profile);
  std::string chunk = lib;
  chunk += "\nlocal hench = GetPlayerHenchman()\n";
  chunk += "if not hench or not hench:IsAlive() then print(\"[coop] apply: no live henchman\") return end\n";
  chunk += "local profile = CoopHero.Parse([" + eq + "[" + profile + "]" + eq + "])\n";
  chunk += "if not profile then print(\"[coop] apply: bad profile\") return end\n";
  chunk += "CoopHero.Apply(hench, profile)\n";
  chunk += "print(\"[coop] apply done\")\n";
  return fable2::luaconsole::RunChunk(chunk);
}

inline bool is_host(const lobby::LobbyStatus& s) { return s.in_world && s.joined_host_ip == 0; }

inline std::filesystem::path pending_story_file() {
  return rex::filesystem::GetExecutableFolder() / "saves" / "coop_story_pending.txt";
}

inline std::string timestamp() {
  const std::time_t t = std::time(nullptr);
  char buf[32] = {};
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

// --- line handlers (run on the game thread from the print() capture) ----------

inline void OnHeroLine(std::string_view line) {
  {
    std::lock_guard<std::mutex> l(mtx());
    own_profile().assign(line);
  }
  if (REXCVAR_GET(coop_own_hero) && lobby::PublishData("hero", std::string(line))) {
    REXSYS_INFO("[coop] published this hero ({} bytes)", line.size());
  }
}

// "COOPSTORY:v1|part=<i>/<n>|<fields>"
inline void OnStoryLine(std::string_view line) {
  const auto p = line.find("|part=");
  if (p == std::string_view::npos) return;
  int i = 0, n = 0;
  if (std::sscanf(std::string(line.substr(p + 6, 16)).c_str(), "%d/%d", &i, &n) != 2 || i < 1 ||
      n < 1 || i > n) {
    return;
  }
  const auto body_at = line.find('|', p + 6);
  const std::string body =
      body_at == std::string_view::npos ? std::string() : std::string(line.substr(body_at + 1));
  std::string joined;
  {
    std::lock_guard<std::mutex> l(mtx());
    auto& parts = story_parts();
    if (i == 1) parts.clear();
    parts[i] = body;
    if (static_cast<int>(parts.size()) != n) return;
    joined = "COOPSTORY:v1";
    for (const auto& [k, v] : parts) joined += "|" + v;
    parts.clear();
  }
  const lobby::LobbyStatus status = lobby::GetLobbyStatus();
  if (!is_host(status)) {
    REXSYS_INFO("[coop] story captured ({} bytes); not shared (only the host shares its story)",
                joined.size());
    return;
  }
  if (lobby::PublishData("story", joined)) {
    REXSYS_INFO("[coop] published story ({} bytes)", joined.size());
  }
}

// --- per frame (game thread) ---------------------------------------------------

inline void auto_capture(int64_t now) {
  if (!fable2::f5lua::ready()) {
    g_ready_since_ms = 0;
    return;
  }
  // Capture once after the game finished loading, and again on each world
  // change (joining someone, back to your own world). No periodic re-runs.
  if (g_ready_since_ms == 0) {
    g_ready_since_ms = now;
    g_next_capture_ms = now + kFirstCaptureDelayMs;
  } else if (g_world_changed) {
    g_next_capture_ms = now + kFirstCaptureDelayMs;
  }
  if (g_next_capture_ms == 0 || now < g_next_capture_ms) return;
  g_next_capture_ms = 0;
  fable2::luaconsole::RunFile("coop_capture.lua");
}

inline void host_apply(const lobby::LobbyStatus& status, int64_t now) {
  if (!is_host(status)) return;
  const int64_t spawned = g_spawned_at_ms.load();
  if (spawned == 0 || now - spawned < REXCVAR_GET(coop_apply_delay_ms)) return;
  const uint32_t epoch = g_spawn_epoch.load();

  const std::string override_profile = REXCVAR_GET(coop_partner_profile);
  if (!override_profile.empty()) {
    if (g_applied_epoch == epoch && g_applied_ip == 0 && g_applied_rev == 1) return;
    g_applied_epoch = epoch;
    g_applied_ip = 0;
    g_applied_rev = 1;
    if (ApplyToHenchman(override_profile, "coop_partner_profile")) {
      REXSYS_INFO("[coop] applied hero from coop_partner_profile");
    }
    return;
  }

  // Most recently heard partner hero.
  const auto heroes = lobby::GetPeerData("hero");
  const lobby::PeerData* best = nullptr;
  for (const auto& h : heroes) {
    if (!best || h.seconds_since_received < best->seconds_since_received) best = &h;
  }
  if (!best) return;
  if (best->revision == g_applied_rev && best->ip == g_applied_ip && epoch == g_applied_epoch) {
    return;
  }
  g_applied_rev = best->revision;
  g_applied_ip = best->ip;
  g_applied_epoch = epoch;
  if (ApplyToHenchman(best->value, "hero from " + best->gamertag)) {
    REXSYS_INFO("[coop] applied hero from {} rev {}", best->gamertag, best->revision);
  }
}

// Story position inside a COOPSTORY value ("...|pos=<n>|..."), -1 if absent.
inline int story_pos(std::string_view value) {
  const auto p = value.find("|pos=");
  if (p == std::string_view::npos) return -1;
  return std::atoi(std::string(value.substr(p + 5, 8)).c_str());
}

inline void set_pending_label(const std::string& label) {
  rex::cvar::SetFlagByName("coop_story_pending", label);
}

inline void joiner_story(const lobby::LobbyStatus& status) {
  if (status.joined_host_ip == 0) return;
  if (REXCVAR_GET(coop_story_sync) == "never") return;
  for (const auto& s : lobby::GetPeerData("story")) {
    if (s.ip != status.joined_host_ip) continue;
    if (s.revision == g_story_rev && s.ip == g_story_ip) return;
    g_story_rev = s.revision;
    g_story_ip = s.ip;
    std::error_code ec;
    std::filesystem::create_directories(pending_story_file().parent_path(), ec);
    std::ofstream out(pending_story_file(), std::ios::binary | std::ios::trunc);
    if (!out) {
      REXSYS_ERROR("[coop] cannot write {}", pending_story_file().string());
      return;
    }
    const int pos = story_pos(s.value);
    out << "host=" << s.gamertag << "\n"
        << "host_address=" << s.address << "\n"
        << "host_pos=" << pos << "\n"
        << "received=" << timestamp() << "\n"
        << "revision=" << s.revision << "\n"
        << s.value << "\n";
    set_pending_label(s.gamertag + " position " + std::to_string(pos));
    REXSYS_INFO("[coop] saved {}'s story (position {}, rev {}) to {}", s.gamertag, pos,
                s.revision, pending_story_file().string());
    return;
  }
}

inline void CmdStoryDiscard(std::string_view) {
  std::error_code ec;
  std::filesystem::remove(pending_story_file(), ec);
  set_pending_label("");
  REXSYS_INFO("[coop] pending story discarded");
}

inline void CmdStoryApply(std::string_view) {
  // Applying needs this PC's own save loaded and the forward-only check
  // against its Gameflow position (research/03-story-sync.md); not built yet.
  REXSYS_WARN("[coop] bringing the host's story into this save is not available yet; "
              "the pending story is kept");
}

// Shows a pending story left from an earlier session (F6 reads the cvar).
inline void load_pending_label() {
  std::ifstream in(pending_story_file());
  if (!in) return;
  std::string line, host, pos;
  while (std::getline(in, line)) {
    if (line.rfind("host=", 0) == 0) host = line.substr(5);
    if (line.rfind("host_pos=", 0) == 0) pos = line.substr(9);
  }
  if (!host.empty()) set_pending_label(host + " position " + pos);
}

// --- camera and leash (research/04-camera.md) -------------------------------
//
// Applied through Lua on each PC while it is in a world (hosting, or in
// someone else's game): re-run only when the settings change, on a world
// change, and after a level load (the game reloading its scripts can reset
// the camera tables). The chunk is idempotent: it keeps the game's original
// functions and wraps them.

inline std::string g_camera_signature;  // settings last applied ("" = never / restored)
inline uint32_t g_camera_load_epoch = 0;   // game script-load epoch last applied after
inline uint32_t g_camera_spawn_epoch = 0;  // henchman spawn epoch last applied after

inline std::string camera_chunk(bool own_camera, bool follow_partner, int outdoor, int indoor) {
  std::string c;
  c += "if type(CameraManager) ~= 'table' or type(SACCamera) ~= 'table' then\n";
  c += "  print('[coop] camera scripts are not reachable from this Lua state') return\n";
  c += "end\n";
  c += "CameraManager.IgnoreMultiplayerCameras = " + std::string(own_camera ? "true" : "false") + "\n";
  c += "SACCamera.__CoopOrigLeash = SACCamera.__CoopOrigLeash or SACCamera.SetMaxCoopDistance\n";
  c += "local orig_leash = SACCamera.__CoopOrigLeash\n";
  c += "local outdoor, indoor = " + std::to_string(outdoor) + ", " + std::to_string(indoor) + "\n";
  c += "if outdoor > 0 or indoor > 0 then\n";
  c += "  SACCamera.SetMaxCoopDistance = function(self)\n";
  c += "    orig_leash(self)\n";
  c += "    local d = self.IsInsideBuilding and indoor or outdoor\n";
  c += "    if d > 0 then Debug.SetLimitCharacterDistance(d) end\n";
  c += "  end\n";
  c += "else SACCamera.SetMaxCoopDistance = orig_leash end\n";
  c += "CameraManager.__CoopOrigFrameUpdate = CameraManager.__CoopOrigFrameUpdate or CameraManager.FrameUpdate\n";
  c += "local orig_update = CameraManager.__CoopOrigFrameUpdate\n";
  if (follow_partner) {
    c += "CameraManager.FrameUpdate = function(self, player)\n";
    c += "  local mine = GetPlayerHenchman()\n";
    c += "  if mine and mine:IsAlive() then player = mine end\n";
    c += "  return orig_update(self, player)\n";
    c += "end\n";
  } else {
    c += "CameraManager.FrameUpdate = orig_update\n";
  }
  return c;
}

inline void camera_tick(const lobby::LobbyStatus& status, int64_t now) {
  const bool in_world = status.in_world || status.joined_host_ip != 0;
  const bool host = is_host(status);
  const bool own_camera = REXCVAR_GET(coop_own_camera);
  const bool follow_partner = REXCVAR_GET(coop_camera_follow_partner) && !host;
  const int outdoor = host ? std::clamp(REXCVAR_GET(coop_leash_outdoor_m), 0, 50) : 0;
  const int indoor = host ? std::clamp(REXCVAR_GET(coop_leash_indoor_m), 0, 20) : 0;
  const bool any = own_camera || follow_partner || outdoor > 0 || indoor > 0;
  if (!in_world || !fable2::f5lua::ready()) return;
  if (!any && g_camera_signature.empty()) return;  // nothing to do, nothing to undo
  const std::string signature = std::to_string(own_camera) + std::to_string(follow_partner) +
                                "/" + std::to_string(outdoor) + "/" + std::to_string(indoor);
  const uint32_t load_epoch = fable2::f5lua::g_load_epoch.load();
  const uint32_t spawn_epoch = g_spawn_epoch.load();
  const bool loaded = load_epoch != g_camera_load_epoch &&
                      now - fable2::f5lua::g_load_at_ms.load() >= kLoadSettleMs;
  const bool spawned = spawn_epoch != g_camera_spawn_epoch;
  if (signature == g_camera_signature && !g_world_changed && !loaded && !spawned) return;
  if (load_epoch != g_camera_load_epoch && !loaded) return;  // load still settling
  g_camera_load_epoch = load_epoch;
  g_camera_spawn_epoch = spawn_epoch;
  REXSYS_INFO("[coop] camera: own_camera={} follow_partner={} leash={}m/{}m ({})", own_camera,
              follow_partner, outdoor, indoor,
              signature != g_camera_signature ? "settings"
              : g_world_changed               ? "world"
              : loaded                        ? "level load"
                                              : "spawn");
  fable2::luaconsole::RunChunk(camera_chunk(own_camera, follow_partner, outdoor, indoor));
  g_camera_signature = any ? signature : std::string();
}

inline bool g_pending_label_loaded = false;

inline void OnFrame() {
  if (!g_pending_label_loaded) {
    g_pending_label_loaded = true;
    load_pending_label();
  }
  const int64_t now = now_ms();
  if (now < g_next_check_ms) return;
  g_next_check_ms = now + kCheckEveryMs;
  const lobby::LobbyStatus status = lobby::GetLobbyStatus();
  if (!status.networking_enabled) return;
  const uint64_t world_key = status.joined_host_ip != 0 ? status.joined_host_ip
                             : status.in_world          ? 1ull
                                                        : 0ull;
  g_world_changed = world_key != g_world_key;
  g_world_key = world_key;
  joiner_story(status);
  camera_tick(status, now);
  if (!REXCVAR_GET(coop_own_hero)) return;
  auto_capture(now);
  host_apply(status, now);
}

// --- console ---------------------------------------------------------------

inline void CmdCoopApply(std::string_view args) {
  const std::string arg = fable2::luaconsole::trim(args);
  if (arg == "own") {
    std::string own;
    {
      std::lock_guard<std::mutex> l(mtx());
      own = own_profile();
    }
    ApplyToHenchman(own, "own profile");
    return;
  }
  if (!arg.empty() && arg != "partner") {
    ApplyToHenchman(arg, "profile from the console");
    return;
  }
  const std::string override_profile = REXCVAR_GET(coop_partner_profile);
  if (!override_profile.empty()) {
    ApplyToHenchman(override_profile, "coop_partner_profile");
    return;
  }
  const auto heroes = lobby::GetPeerData("hero");
  const lobby::PeerData* best = nullptr;
  for (const auto& h : heroes) {
    if (!best || h.seconds_since_received < best->seconds_since_received) best = &h;
  }
  if (!best) {
    REXSYS_WARN("[coop] no partner hero received yet");
    return;
  }
  ApplyToHenchman(best->value, "hero from " + best->gamertag);
}

// Installs the line handlers and the per-frame hook. Runs at static init
// (this header is included once, from main.cpp).
inline bool Install() {
  fable2::luaconsole::AddLineHandler("COOPHERO:", OnHeroLine);
  fable2::luaconsole::AddLineHandler("COOPSTORY:", OnStoryLine);
  fable2::f5lua::g_frame_hook = &OnFrame;
  return true;
}
inline const bool g_installed = Install();

}  // namespace fable2::coophero

REXCVAR_DEFINE_COMMAND_ARGS(coop_story_apply, fable2::coophero::CmdStoryApply, "Network",
                            "Bring the pending story from the co-op host into this save");
REXCVAR_DEFINE_COMMAND_ARGS(coop_story_discard, fable2::coophero::CmdStoryDiscard, "Network",
                            "Forget the pending story from the co-op host");
REXCVAR_DEFINE_COMMAND_ARGS(coop_apply, fable2::coophero::CmdCoopApply, "Network",
                            "Apply a hero to the co-op henchman: no argument = the partner's, "
                            "'own' = this PC's last coop_capture, or a COOPHERO line");

// Henchman spawn (see research/02-hench-lua-rewards.md): note the time, then
// let the original run.
extern "C" void __imp__sub_8231FA68(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8231FA68(PPCContext& ctx, uint8_t* base) {
  __imp__sub_8231FA68(ctx, base);
  REXSYS_INFO("[coop] henchman spawned");
  fable2::coophero::g_spawn_epoch.fetch_add(1);
  fable2::coophero::g_spawned_at_ms.store(fable2::coophero::now_ms());
}
