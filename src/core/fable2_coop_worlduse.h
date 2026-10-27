// fable_2 - co-op: player 2 opens chests, doors and levers (item 7 of the live
// session of 2026-10-27; path 2b).
//
// The joiner is a thin client: its game runs no chest logic at all and sends
// the host no use for player 2 (coop_use_probe, rig 2026-10-27: on the
// partner's A not one chest or use function ran on either PC; on the host's
// own A, CECChest's open ran on the host). So player 2's use goes to the host
// as a record of our own, and the host uses the object for player 2 the
// game's own way:
//   chest   the lid and the giving part of the stock use handler
//           OnActionUseTakeItems (contents to the hero by its co-op rule;
//           quest chests go on); player 2 itself is not moved or animated
//   door    Door.SetOpenForEntity(door, player 2, true), Door.SetOpen(door, true, true)
//   lever   Switch.Use(switch, player 2)            (OnActionUseSwitch's call;
//           not reachable from the partner's A yet)
// The partner's A says only that (the joiner runs no Lua for it: its pick
// chunks crashed it); the host finds the nearest closed chest or door within
// 2.5 m in front of its own copy of player 2.
//
// Record (lobby data "coop_wuse", published by the joiner):
//   COOPWUSE:v1|n=<nonce>|kind=near
// Testing: coop_world_use = "<kind>:<x>,<y>,<z>" runs the host's part on the
// host itself (kind chest, door or switch: the nearest of that kind to the spot).

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
  c += "  local best, bd = nil, 1e9\n";
  // "near": the partner pressed A; what it can use is found here, from the
  // host's own copy of player 2 (position, facing): the nearest closed chest
  // or door within 2.5 m in front of it (anything within 0.8 m counts).
  c += "  if kind == 'near' then\n";
  c += "    if not p2 or not p2:IsAlive() then return 'no player 2' end\n";
  // A cutscene or movie: the world is not the players' (the joiner cannot ask
  // safely; its GUI calls were among the chunks that crashed it).
  c += "    if (GUI.IsFullCutscenePlaying and GUI.IsFullCutscenePlaying()) or (GUI.IsMoviePlaying and GUI.IsMoviePlaying()) then\n";
  c += "      return 'a cutscene or movie is playing: nothing used'\n";
  c += "    end\n";
  c += "    p = p2:GetPosition()\n";
  c += "    local f = Physics.GetFacingVector(p2)\n";
  c += "    local function pick(ec, closed, k)\n";
  c += "      local s = SearchTools.StartNewSearch('object')\n";
  c += "      SearchTools.FilterWithinDistanceOfPos(s, p, 2.5)\n";
  c += "      SearchTools.FilterWithEC(s, ec)\n";
  c += "      for _, o in pairs(SearchTools.GetSearchResults(s)) do\n";
  c += "        local d = o:GetPosition() - p\n";
  c += "        local dist = d:GetLength()\n";
  c += "        if closed(o) and dist < bd and (dist <= 0.8 or (d:GetX() * f:GetX() + d:GetY() * f:GetY()) / dist >= 0.3) then\n";
  c += "          best, bd, kind = o, dist, k\n";
  c += "        end\n";
  c += "      end\n";
  c += "    end\n";
  c += "    pick(Chest.GetECType(), function(o) return not Chest.IsOpen(o) end, 'chest')\n";
  c += "    pick(Door.GetECType(), function(o) return not Door.IsOpen(o) and not Door.IsLocked(o) end, 'door')\n";
  c += "    if not best then return 'nothing to use in front of player 2' end\n";
  c += "  else\n";
  c += "    local s = SearchTools.StartNewSearch('object')\n";
  c += "    SearchTools.FilterWithinDistanceOfPos(s, p, 2.5)\n";
  c += "    if kind == 'chest' then SearchTools.FilterWithEC(s, Chest.GetECType())\n";
  c += "    elseif kind == 'door' then SearchTools.FilterWithEC(s, Door.GetECType()) end\n";
  c += "    for _, o in pairs(SearchTools.GetSearchResults(s)) do\n";
  c += "      if kind ~= 'switch' or Switch.IsAvailable(o) then\n";
  c += "        local d = (o:GetPosition() - p):GetLength()\n";
  c += "        if d < bd then best, bd = o, d end\n";
  c += "      end\n";
  c += "    end\n";
  c += "    if not best then return 'no ' .. kind .. ' there' end\n";
  c += "  end\n";
  // A chest: the giving part of the stock use handler OnActionUseTakeItems,
  // run as a coroutine the host drives (drive_chunk): the lid, then the items
  // to the hero and the gold through Money.Modify (then the shared purse).
  // The hero, by the handler's own co-op rule ("a henchman is there: the hero
  // receives"); the caravan chest's starter melee must reach the host's hero
  // for QC060 to go on. Not the whole handler: it moves player 2 and sets its
  // open-chest action and interaction mode, which reached the joiner's copy
  // and crashed it (rig 2026-10-27, a guest write 110 ms after). Chest.Open
  // alone opens only the lid. Not done: the handler's theft check on owned chests.
  c += "  if kind == 'chest' then\n";
  c += "    if Chest.IsOpen(best) then return 'chest already open' end\n";
  c += "    if Chest.IsLocked(best) then return 'chest locked' end\n";
  c += "    if CoopWUse then return 'chest: a use is still running' end\n";
  // Someone's chest (the stock handler's theft test: an owner building that is
  // not the hero's, or rented): refused until the theft check is carried over;
  // no silent theft and no crime pinned on the host.
  c += "    if OwnedEntity.IsAvailable(best) then\n";
  c += "      local owner = OwnedEntity.GetOwner(best)\n";
  c += "      if owner and owner:IsAlive() and Building.IsAvailable(owner) and\n";
  c += "         (not PlayerProperties.IsOwnedByPlayer(GetPlayerHero(), owner) or PlayerProperties.GetIsRentalProperty(GetPlayerHero(), owner)) then\n";
  c += "        return 'chest belongs to someone: not opened (taking from it is not supported yet)'\n";
  c += "      end\n";
  c += "    end\n";
  c += "    CoopWUse = coroutine.create(function()\n";
  c += "      Chest.Open(best)\n";
  c += "      local n = 0\n";
  c += "      while not Chest.IsOpen(best) and n < 600 do n = n + 1 coroutine.yield() end\n";
  c += "      local hero = GetPlayerHero()\n";
  c += "      local items, gold = 0, 0\n";
  c += "      if Inventory.IsAvailable(hero) and Inventory.IsAvailable(best) then\n";
  c += "        for i = 0, Inventory.GetNumberOfItems(best) - 1 do\n";
  c += "          local added = Inventory.AddItem(hero, Inventory.GetItemByIndex(best, i))\n";
  c += "          items = items + 1\n";
  c += "          GUI.DisplayReceivedItem(added)\n";
  c += "          while GUI.IsUIActive() do coroutine.yield() end\n";
  c += "        end\n";
  c += "        Inventory.Clear(best)\n";
  c += "      end\n";
  c += "      if Money.IsAvailable(hero) and Money.IsAvailable(best) then\n";
  c += "        gold = Money.Get(best)\n";
  c += "        Money.Modify(hero, gold)\n";
  c += "        Money.Set(best, 0)\n";
  c += "      end\n";
  c += "      print('[coop] world use: chest gave ' .. items .. ' item(s) and ' .. gold .. ' gold to the hero')\n";
  c += "    end)\n";
  c += "    return 'chest: opening it for player 2'\n";
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

// Resumes the chest's coroutine (host, every frame while it runs).
inline std::string drive_chunk() {
  std::string c;
  c += "if CoopWUse then\n";
  c += "  if coroutine.status(CoopWUse) == 'suspended' then\n";
  c += "    local ok, e = coroutine.resume(CoopWUse)\n";
  c += "    if not ok then print('[coop] world use error: ' .. tostring(e)) end\n";
  c += "  end\n";
  c += "  if coroutine.status(CoopWUse) == 'dead' then\n";
  c += "    CoopWUse = nil\n";
  c += "    print('[coop] world use done')\n";
  c += "  end\n";
  c += "end\n";
  return c;
}

inline bool valid_kind(const std::string& k) {
  return k == "near" || k == "chest" || k == "door" || k == "switch";
}

inline std::atomic<bool> g_driving{false};
inline int64_t g_drive_since_ms = 0;
inline void run_on_host(const std::string& kind, float x, float y, float z, const char* why) {
  if (!valid_kind(kind)) return;
  REXSYS_INFO("[coop] world use {} at {:.2f} {:.2f} {:.2f} ({})", kind, x, y, z, why);
  fable2::luaconsole::RunChunkOrKeep(host_chunk(kind, x, y, z));
  if (kind == "chest" || kind == "near") {  // a chest may be what "near" finds
    g_driving = true;
    g_drive_since_ms = fable2::f5lua::steady_ms();
  }
}

// Host, every frame while a chest use runs (one chunk queued at a time).
inline void drive_tick(int64_t now) {
  if (!g_driving.load() || fable2::f5lua::g_queued.load() > 0) return;
  if (now - g_drive_since_ms > 60000) {
    g_driving = false;
    REXSYS_WARN("[coop] world use: the chest use did not finish in 60 s");
    fable2::luaconsole::RunChunk("CoopWUse = nil");
    return;
  }
  fable2::luaconsole::RunChunk(drive_chunk());
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

// The partner's A (input thread). The joiner runs no Lua for it: every pick
// chunk run there after a real A next to a chest crashed it (guest reads of
// 0x5 and 0x3A000008 a few ms after the chunk, rig 2026-10-27), even ones that
// called nothing the harness had not run there safely. So the joiner only
// says "A" (kind=near) and the host finds what is in front of its own copy of
// player 2. The game still gets the press: pickups and attacks keep the stock
// path (at a chest the stock A does nothing on the joiner).
// An A while the partner's own pause wheel (or a page from it) is up is its
// menu's, read at the press (choosing Quit closes the wheel before the tick):
// the kit's g_wheel_open, from the wheel's builder to the pause screen's exit.
// Not the kit's g_menu_up: every pause of the host's game raises it and
// nothing lowers it (it blocked every A after the first chest, rig
// 2026-10-27). Cutscenes and movies are the host's to see (host_chunk).
inline std::atomic<bool> g_a_pressed{false};
inline uint16_t g_prev_buttons = 0;
inline uint16_t (*g_prev_pad_hook)(uint16_t) = nullptr;
inline uint16_t pad_hook(uint16_t buttons) {
  constexpr uint16_t kPadA = 0x1000;
  const uint16_t game = g_prev_pad_hook ? g_prev_pad_hook(buttons) : buttons;
  if ((game & kPadA) && !(g_prev_buttons & kPadA) && !fable2::coopkit::g_wheel_open.load()) g_a_pressed = true;
  g_prev_buttons = game;
  return game;
}

// Only once player 2's creature has been on this PC for kSettleMs, with no
// save running and no fresh level load (coopbusy::Busy() cannot say: its probe
// does not run while visiting).
constexpr int64_t kSettleMs = 15000;
inline uint32_t g_p2_seen = 0;
inline int64_t g_p2_since_ms = 0;
inline bool joiner_settled(int64_t now) {
  const uint32_t p2 = fable2::coopcam::g_p2_entity.load();
  if (p2 != g_p2_seen) {
    g_p2_seen = p2;
    g_p2_since_ms = now;
  }
  const int64_t loaded = fable2::f5lua::g_load_at_ms.load();
  return p2 && now - g_p2_since_ms > kSettleMs && !fable2::f5lua::saving() &&
         !(loaded && now - loaded < kSettleMs);
}
inline void joiner_tick(int64_t now) {
  if (!joiner_in_campaign()) fable2::coopkit::g_wheel_open = false;  // never left up by a lost session
  if (!g_a_pressed.exchange(false)) return;
  if (!joiner_in_campaign() || !joiner_settled(now)) return;
  char rec[96];
  std::snprintf(rec, sizeof(rec), "COOPWUSE:v1|n=%lld|kind=near", static_cast<long long>(now));
  lobby::PublishData("coop_wuse", rec);
  REXSYS_INFO("[coop] world use: the partner's A, the host looks in front of player 2 ({})", rec);
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
  fable2::luaconsole::AddLineHandler("[coop] world use done", [](std::string_view) { g_driving = false; });
  return true;
}
inline const bool g_installed = Install();

}  // namespace fable2::coopworlduse
