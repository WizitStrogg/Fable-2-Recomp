// fable_2 - co-op: player 2 opens chests, doors and levers (item 7 of the live
// session of 2026-10-27; path 2b).
//
// The joiner is a thin client: its game runs no chest logic at all and sends
// the host no use for player 2 (coop_use_probe, rig 2026-10-27: on the
// partner's A not one chest or use function ran on either PC; on the host's
// own A, CECChest's open ran on the host). So player 2's use goes to the host
// as a record of our own, and the host uses the object for player 2 the
// game's own way:
//   chest   the stock use handler OnActionUseTakeItems, player 2 the user
//           (contents to the hero by its co-op rule; quest chests go on)
//   door    Door.SetOpenForEntity(door, player 2, true), Door.SetOpen(door, true, true)
//   lever   Switch.Use(switch, player 2)            (OnActionUseSwitch's call)
// The object is found on the host by kind and position (both copies place the
// level's objects alike): the nearest of that kind within 2.5 m.
//
// Record (lobby data "coop_wuse", published by the joiner):
//   COOPWUSE:v1|n=<nonce>|kind=<chest|door|switch>|x=<x>|y=<y>|z=<z>
// Testing: coop_world_use = "<kind>:<x>,<y>,<z>" runs the host's part on the
// host itself.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/kernel/xam/lan_lobby.h>
#include <rex/logging.h>

#include "fable2_coop_busy.h"
#include "fable2_coop_camera.h"
#include "fable2_coop_frontend.h"
#include "fable2_coop_hero.h"
#include "fable2_coop_kit.h"
#include "fable2_coop_lobby.h"
#include "fable2_f5_lua.h"
#include "fable2_lua_console.h"

REXCVAR_DEFINE_STRING(coop_world_use, "", "Network",
                      "Testing (host): open a chest/door/lever for player 2 now: <kind>:<x>,<y>,<z>");

namespace fable2::coopworlduse {

namespace lobby = rex::kernel::xam::lobby;

// The host's part: find the object and open it for player 2 (Lua, host).
inline std::string host_chunk(const std::string& kind, float x, float y, float z) {
  char pos[96];
  std::snprintf(pos, sizeof(pos), "CVector3(%.3f, %.3f, %.3f)", x, y, z);
  std::string c;
  c += "local ok, e = pcall(function()\n";
  c += "  local kind, p, p2 = '" + kind + "', " + pos + ", GetPlayerHenchman()\n";
  c += "  local s = SearchTools.StartNewSearch('object')\n";
  c += "  SearchTools.FilterWithinDistanceOfPos(s, p, 2.5)\n";
  c += "  if kind == 'chest' then SearchTools.FilterWithEC(s, Chest.GetECType())\n";
  c += "  elseif kind == 'door' then SearchTools.FilterWithEC(s, Door.GetECType()) end\n";
  c += "  local best, bd = nil, 1e9\n";
  c += "  for _, o in pairs(SearchTools.GetSearchResults(s)) do\n";
  c += "    if kind ~= 'switch' or Switch.IsAvailable(o) then\n";
  c += "      local d = (o:GetPosition() - p):GetLength()\n";
  c += "      if d < bd then best, bd = o, d end\n";
  c += "    end\n";
  c += "  end\n";
  c += "  if not best then return 'no ' .. kind .. ' there' end\n";
  // A chest: the stock use handler OnActionUseTakeItems with player 2 as the
  // user, run as a coroutine the host drives (drive_chunk). It gives the
  // contents to the hero when a henchman is there (its own co-op rule: the
  // caravan chest's starter weapons must reach the host's hero for QC060 to
  // go on), the gold through Money.Modify (then the shared purse), and keeps
  // the locked and silver key checks. Chest.Open alone opened only the lid.
  // The canned chest camera would turn the host's view: off while it runs.
  c += "  if kind == 'chest' then\n";
  c += "    if Chest.IsOpen(best) then return 'chest already open' end\n";
  c += "    if CoopWUse then return 'chest: a use is still running' end\n";
  c += "    if not OnActionUseTakeItems then Chest.Open(best) return 'chest opened (lid only)' end\n";
  c += "    CoopWUseCam = {CannedActionCamera.SwitchOn, CannedActionCamera.SwitchOff}\n";
  c += "    CannedActionCamera.SwitchOn = function() end\n";
  c += "    CannedActionCamera.SwitchOff = function() end\n";
  c += "    CoopWUseChest = best\n";
  c += "    CoopWUse = coroutine.create(function() OnActionUseTakeItems:Create(best, p2):Update() end)\n";
  c += "    return 'chest: player 2 uses it (stock take items)'\n";
  c += "  elseif kind == 'door' then\n";
  c += "    if Door.IsLocked(best) then return 'door locked' end\n";
  // SetOpenForEntity alone only lets the door open for player 2 (it stayed
  // shut, rig 2026-10-27); SetOpen opens it now, as the quest triggers do.
  c += "    Door.SetOpenForEntity(best, p2, true)\n";
  c += "    Door.SetOpen(best, true, true)\n";
  c += "    return 'door opened for player 2 (open ' .. tostring(Door.IsOpen(best)) .. ')'\n";
  c += "  else\n";
  c += "    Switch.Use(best, p2)\n";
  c += "    return 'lever used by player 2'\n";
  c += "  end\n";
  c += "end)\n";
  c += "print('[coop] world use " + kind + ": ' .. tostring(ok and e or ('error ' .. tostring(e))))\n";
  return c;
}

// Resumes the chest's use coroutine (host, every frame while it runs); when it
// ends, the canned camera comes back. lid: the open action set on player 2
// (driven from the other PC) may never play, and the handler waits for the
// lid: open it, the handler then gives the contents.
inline std::string drive_chunk(bool lid) {
  std::string c;
  if (lid) {
    c += "if CoopWUseChest and not Chest.IsOpen(CoopWUseChest) then Chest.Open(CoopWUseChest) "
         "print('[coop] world use: lid opened for player 2') end\n";
  }
  c += "if CoopWUse then\n";
  c += "  if coroutine.status(CoopWUse) == 'suspended' then\n";
  c += "    local ok, e = coroutine.resume(CoopWUse)\n";
  c += "    if not ok then print('[coop] world use error: ' .. tostring(e)) end\n";
  c += "  end\n";
  c += "  if coroutine.status(CoopWUse) == 'dead' then\n";
  c += "    CoopWUse, CoopWUseChest = nil, nil\n";
  c += "    if CoopWUseCam then CannedActionCamera.SwitchOn, CannedActionCamera.SwitchOff = CoopWUseCam[1], CoopWUseCam[2] end\n";
  c += "    CoopWUseCam = nil\n";
  c += "    print('[coop] world use done')\n";
  c += "  end\n";
  c += "end\n";
  return c;
}

inline bool valid_kind(const std::string& k) { return k == "chest" || k == "door" || k == "switch"; }

inline std::atomic<bool> g_driving{false};
inline int64_t g_drive_since_ms = 0;
inline bool g_lid_sent = false;
inline void run_on_host(const std::string& kind, float x, float y, float z, const char* why) {
  if (!valid_kind(kind)) return;
  REXSYS_INFO("[coop] world use {} at {:.2f} {:.2f} {:.2f} ({})", kind, x, y, z, why);
  fable2::luaconsole::RunChunkOrKeep(host_chunk(kind, x, y, z));
  if (kind == "chest") {
    g_driving = true;
    g_drive_since_ms = fable2::f5lua::steady_ms();
    g_lid_sent = false;
  }
}

// Host, every frame while a chest use runs (one chunk queued at a time).
inline void drive_tick(int64_t now) {
  if (!g_driving.load() || fable2::f5lua::g_queued.load() > 0) return;
  const bool lid = !g_lid_sent && now - g_drive_since_ms > 3000;
  if (lid) g_lid_sent = true;
  if (now - g_drive_since_ms > 60000) {  // gave up: put the camera back
    g_driving = false;
    REXSYS_WARN("[coop] world use: the chest use did not finish in 60 s");
    fable2::luaconsole::RunChunk(
        "if CoopWUseCam then CannedActionCamera.SwitchOn, CannedActionCamera.SwitchOff = CoopWUseCam[1], "
        "CoopWUseCam[2] end CoopWUse, CoopWUseChest, CoopWUseCam = nil, nil, nil");
    return;
  }
  fable2::luaconsole::RunChunk(drive_chunk(lid));
}

// Testing trigger: coop_world_use = "<kind>:<x>,<y>,<z>" (host).
inline std::string g_last_test;
inline void test_tick() {
  const std::string v = REXCVAR_GET(coop_world_use);
  if (v == g_last_test) return;
  g_last_test = v;
  const auto colon = v.find(':');
  float x = 0, y = 0, z = 0;
  if (colon == std::string::npos || std::sscanf(v.c_str() + colon + 1, "%f,%f,%f", &x, &y, &z) != 3) return;
  run_on_host(v.substr(0, colon), x, y, z, "coop_world_use");
}

// --- the joiner's part --------------------------------------------------------
// A campaign partner in the host's world (the purse mirror's host is set at
// the go and cleared on leaving).
inline bool joiner_in_campaign() {
  return fable2::coopfrontend::g_purse_host_xuid.load() != 0 && lobby::GetLobbyStatus().joined_host_ip != 0;
}

// The joiner's Lua, on the partner's A: the nearest closed chest within 2.5 m
// of player 2 and in front of it (anything within 0.8 m counts), printed for
// the host. Only calls proven on the joiner: the chest search by component,
// positions, the facing (rig 2026-10-27). A distance-filtered search, the door
// search and the GUI calls (movie checks, the hint box) each crashed it once
// or ran just before a crash (guest reads of 0x5 / 0x3A000008), so doors and
// the "A: Open" hint wait until each is proven on its own.
inline std::string joiner_chunk() {
  std::string c;
  c += "local ok, err = pcall(function()\n";
  c += "  local me = GetPlayerHenchman()\n";
  c += "  if not me or not me:IsAlive() then return 'no player 2' end\n";
  c += "  local p, f = me:GetPosition(), Physics.GetFacingVector(me)\n";
  c += "  local best, bd, seen, near = nil, 1e9, 0, 1e9\n";
  c += "  local s = SearchTools.GetSearchResults(SearchTools.FilterWithEC(SearchTools.StartNewSearch('object'), Chest.GetECType()))\n";
  c += "  for _, o in pairs(s) do\n";
  c += "    local d = o:GetPosition() - p\n";
  c += "    local dist = d:GetLength()\n";
  c += "    seen = seen + 1\n";
  c += "    if dist < near then near = dist end\n";
  c += "    if dist <= 2.5 and (dist <= 0.8 or (d:GetX() * f:GetX() + d:GetY() * f:GetY()) / dist >= 0.3) and\n";
  c += "       dist < bd and not Chest.IsOpen(o) then best, bd = o, dist end\n";
  c += "  end\n";
  c += "  if not best then return string.format('nothing to use (%d chests, nearest %.1f m)', seen, near) end\n";
  c += "  local q = best:GetPosition()\n";
  c += "  print(string.format('COOPWUSE pick chest %.3f %.3f %.3f', q:GetX(), q:GetY(), q:GetZ()))\n";
  c += "end)\n";
  c += "if not ok or err then print('[coop] world use (partner): ' .. tostring(err)) end\n";
  return c;
}

// The partner's A (input thread), counted only in the world with no menu up;
// the game still gets the press: pickups and attacks keep the stock path (and
// at a chest the stock A does nothing on the joiner).
// Not gated on the kit's menu flag: the join screens raise it and nothing
// lowers it (rig 2026-10-27: no A ever counted). An A in a menu is dropped
// instead by time: its pick runs only after the unpause (scripts wait while
// paused) and comes back late.
inline std::atomic<bool> g_a_pressed{false};
inline std::atomic<int64_t> g_a_ms{0};
constexpr int64_t kPickFreshMs = 1500;
inline uint16_t g_prev_buttons = 0;
inline uint16_t (*g_prev_pad_hook)(uint16_t) = nullptr;
inline uint16_t pad_hook(uint16_t buttons) {
  constexpr uint16_t kPadA = 0x1000;
  const uint16_t game = g_prev_pad_hook ? g_prev_pad_hook(buttons) : buttons;
  if ((game & kPadA) && !(g_prev_buttons & kPadA)) {
    g_a_ms = fable2::f5lua::steady_ms();
    g_a_pressed = true;
  }
  g_prev_buttons = game;
  return game;
}

// "COOPWUSE pick <kind> <x> <y> <z>" (the joiner's Lua) -> the record.
inline void on_pick_line(std::string_view line) {
  if (fable2::f5lua::steady_ms() - g_a_ms.load() > kPickFreshMs) {
    REXSYS_INFO("[coop] world use: a late pick dropped (the A was in a menu)");
    return;
  }
  char kind[16] = {};
  float x = 0, y = 0, z = 0;
  if (std::sscanf(std::string(line).c_str(), "COOPWUSE pick %15s %f %f %f", kind, &x, &y, &z) != 4) return;
  char rec[160];
  std::snprintf(rec, sizeof(rec), "COOPWUSE:v1|n=%lld|kind=%s|x=%.3f|y=%.3f|z=%.3f",
                static_cast<long long>(fable2::f5lua::steady_ms()), kind, x, y, z);
  lobby::PublishData("coop_wuse", rec);
  REXSYS_INFO("[coop] world use: asked the host to use the {} at {:.2f} {:.2f} {:.2f}", kind, x, y, z);
}

// Only once player 2's creature has been on this PC for kSettleMs and the
// game is not busy (movie, cutscene, load): the join screens' own A presses
// ran the pick before player 2 existed, and a chunk run while the world was
// still settling crashed the partner (read of guest 0x5, rig 2026-10-27).
constexpr int64_t kSettleMs = 15000;
inline uint32_t g_p2_seen = 0;
inline int64_t g_p2_since_ms = 0;
inline bool joiner_settled(int64_t now) {
  const uint32_t p2 = fable2::coopcam::g_p2_entity.load();
  if (p2 != g_p2_seen) {
    g_p2_seen = p2;
    g_p2_since_ms = now;
  }
  // Not coopbusy::Busy(): its probe does not run while visiting (it always said
  // "no recent probe" here, rig 2026-10-27). Saving and a fresh level load are
  // known on this PC; movies and cutscenes are checked in the chunks.
  const int64_t loaded = fable2::f5lua::g_load_at_ms.load();
  return p2 && now - g_p2_since_ms > kSettleMs && !fable2::f5lua::saving() &&
         !(loaded && now - loaded < kSettleMs);
}

inline void joiner_tick(int64_t now) {
  if (!joiner_in_campaign() || !joiner_settled(now)) {
    g_a_pressed = false;
    return;
  }
  if (g_a_pressed.exchange(false)) {
    REXSYS_INFO("[coop] world use: the partner's A, looking for a chest");
    fable2::luaconsole::RunChunk(joiner_chunk());
  }
}

// --- the host's part ----------------------------------------------------------
// The partner's latest coop_wuse record; one seen when player 2 arrived is old.
inline std::string g_seen_record;
inline uint32_t g_seen_epoch = ~0u;
inline void host_tick() {
  if (!fable2::coophero::g_campaign_on.load()) return;
  const auto status = lobby::GetLobbyStatus();
  if (!status.in_world || status.joined_host_ip != 0) return;
  uint64_t pxuid = 0;
  uint32_t pip = 0;
  {
    std::lock_guard<std::mutex> l(fable2::coopfrontend::S().m);
    pxuid = fable2::coopfrontend::S().partner_xuid;
    pip = fable2::coopfrontend::S().partner_ip;
  }
  std::string rec;
  for (const auto& d : lobby::GetPeerData("coop_wuse")) {
    if (pxuid && d.xuid ? d.xuid != pxuid : d.ip != pip) continue;
    if (d.value.rfind("COOPWUSE:v1|", 0) == 0) rec = d.value;
  }
  const uint32_t epoch = fable2::coophero::g_spawn_epoch.load();
  if (epoch != g_seen_epoch) {
    g_seen_epoch = epoch;
    g_seen_record = rec;
    return;
  }
  if (rec.empty() || rec == g_seen_record) return;
  g_seen_record = rec;
  auto field = [&](const char* key) {
    const std::string k = std::string("|") + key + "=";
    const auto at = rec.find(k);
    if (at == std::string::npos) return std::string();
    const auto end = rec.find('|', at + k.size());
    return rec.substr(at + k.size(), end == std::string::npos ? std::string::npos : end - at - k.size());
  };
  run_on_host(field("kind"), std::strtof(field("x").c_str(), nullptr), std::strtof(field("y").c_str(), nullptr),
              std::strtof(field("z").c_str(), nullptr), "the partner's A");
}

inline int64_t g_next_tick_ms = 0;
inline void (*g_prev_frame_hook)() = nullptr;
inline void OnFrame() {
  if (g_prev_frame_hook) g_prev_frame_hook();
  const int64_t now = fable2::f5lua::steady_ms();
  if (fable2::f5lua::ready()) drive_tick(now);
  if (now < g_next_tick_ms && !g_a_pressed.load()) return;
  g_next_tick_ms = now + 250;
  if (!fable2::f5lua::ready()) return;
  test_tick();
  joiner_tick(now);
  host_tick();
}

// Chains onto the per-frame hook and the game's pad hook (main.cpp includes
// this after the modules that set them, so their static init has already run).
inline bool Install() {
  g_prev_frame_hook = fable2::f5lua::g_frame_hook;
  fable2::f5lua::g_frame_hook = &OnFrame;
  g_prev_pad_hook = fable2::cooplobby::g_game_pad_hook;
  fable2::cooplobby::g_game_pad_hook = &pad_hook;
  fable2::luaconsole::AddLineHandler("COOPWUSE pick", on_pick_line);
  fable2::luaconsole::AddLineHandler("[coop] world use done", [](std::string_view) { g_driving = false; });
  return true;
}
inline const bool g_installed = Install();

}  // namespace fable2::coopworlduse
