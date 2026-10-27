// fable_2 - co-op: player 2 opens chests, doors and levers (item 7 of the live
// session of 2026-10-27; path 2b).
//
// The joiner is a thin client: its game runs no chest logic at all and sends
// the host no use for player 2 (coop_use_probe, rig 2026-10-27: on the
// partner's A not one chest or use function ran on either PC; on the host's
// own A, CECChest's open ran on the host). So player 2's use goes to the host
// as a record of our own, and the host opens the object with the game's own
// bindings (the ones the quest scripts use):
//   chest   Chest.Open(chest)                       (the open takes no user)
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

#include <cstdint>
#include <cstdio>
#include <string>

#include <rex/cvar.h>
#include <rex/kernel/xam/lan_lobby.h>
#include <rex/logging.h>

#include "fable2_coop_frontend.h"
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
  c += "  if kind == 'chest' then\n";
  c += "    if Chest.IsOpen(best) then return 'chest already open' end\n";
  c += "    if Chest.IsLocked(best) then return 'chest locked' end\n";
  c += "    Chest.Open(best)\n";
  c += "    return 'chest opened (open ' .. tostring(Chest.IsOpen(best)) .. ')'\n";
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

inline bool valid_kind(const std::string& k) { return k == "chest" || k == "door" || k == "switch"; }

inline void run_on_host(const std::string& kind, float x, float y, float z, const char* why) {
  if (!valid_kind(kind)) return;
  REXSYS_INFO("[coop] world use {} at {:.2f} {:.2f} {:.2f} ({})", kind, x, y, z, why);
  fable2::luaconsole::RunChunkOrKeep(host_chunk(kind, x, y, z));
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

inline int64_t g_next_tick_ms = 0;
inline void (*g_prev_frame_hook)() = nullptr;
inline void OnFrame() {
  if (g_prev_frame_hook) g_prev_frame_hook();
  const int64_t now = fable2::f5lua::steady_ms();
  if (now < g_next_tick_ms) return;
  g_next_tick_ms = now + 250;
  if (!fable2::f5lua::ready()) return;
  test_tick();
}

// Chains onto the per-frame hook (main.cpp includes this after the modules
// that set it, so their static init has already run).
inline bool Install() {
  g_prev_frame_hook = fable2::f5lua::g_frame_hook;
  fable2::f5lua::g_frame_hook = &OnFrame;
  return true;
}
inline const bool g_installed = Install();

}  // namespace fable2::coopworlduse
