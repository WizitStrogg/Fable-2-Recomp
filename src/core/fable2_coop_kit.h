// fable_2 - co-op kit (feature 002, slice 1): player 2's own inventory on the
// partner's PC, through the game's own pause menu
// (research\002-evidence\inventory-plan.md, specs/002-player2-kit).
//
// The joiner is a thin client: its player-2 creature is a local copy, the
// game's inventory screens read and change only that copy, and no stock packet
// carries an item action. So:
//   1. the joiner gets the full pause wheel (Items, Weapons, Clothing): the
//      "local player is the henchman" test sub_82336610 answers no while the
//      pause menu is being opened (sub_82583340);
//   2. the copy is corrected to the host's kit_state with the game's own
//      native inventory and appearance calls (no Lua on the joiner);
//   3. the screens' actions become COOPCMD records the host applies.
//
// This file holds the parts launch 1 proves: the wheel switch, the native
// probe of the copy, and native add/remove on it (console, joiner):
//   coop_kit_probe              log player 2's entity, its component table and
//                               every inventory stack (record id, count)
//   coop_kit_add <hex> [n]      add n of a record to the copy's inventory
//   coop_kit_remove <hex> [n]   remove n of a record from it
// All of it is behind coop_kit (off): with it off nothing here runs.
//
// Native calls (static reading, research\002-evidence\inventory-plan.md):
//   Inventory component = EC type 13 (Inventory.GetECType, sub_821F3C20);
//   stacks: list at inv+12, sentinel *(inv+16), node+0 next, node+8 stack,
//   stack+4 record id, stack+12 count (GetNumberOfItems sub_8261C0B8,
//   find-by-id sub_8261B978);
//   add:    sub_8261A058(r3 = 16-byte out, r4 = inventory, r5 = record id),
//           out[0] released with sub_82741BD8(r3 = out[0], r4 = &out[0]);
//   remove: sub_8261B1A0(r3 = inventory, r4 = record id), one per call.

#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>
#include <rex/kernel/xam/lan_lobby.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "fable2_coop_busy.h"
#include "fable2_coop_camera.h"
#include "fable2_coop_campaign.h"
#include "fable2_coop_frontend.h"
#include "fable2_coop_hero.h"
#include "fable2_coop_kit_proto.h"
#include "fable2_f5_lua.h"
#include "fable2_lua_console.h"

REXCVAR_DEFINE_BOOL(coop_kit, false, "Network",
                    "Co-op campaign: the partner manages player 2's own inventory from the game's "
                    "pause menu (experimental)");
REXCVAR_DEFINE_BOOL(coop_kit_mirror, false, "Network",
                    "Co-op kit (partner): keep player 2's inventory copy equal to the host's "
                    "(off until proven on the rig)");
REXCVAR_DEFINE_BOOL(coop_kit_mirror_clothing, true, "Network",
                    "Co-op kit (partner): the mirror also adds owned clothing missing from the copy "
                    "(never removes it)");
REXCVAR_DEFINE_BOOL(coop_kit_mirror_weapons, true, "Network",
                    "Co-op kit (partner): the mirror also puts player 2's equipped melee and ranged "
                    "weapons in the copy's inventory, so the Weapons page lists them");
// Launch 2: each screen action and each page redirect on its own switch.
REXCVAR_DEFINE_BOOL(coop_kit_send_wear, true, "Network",
                    "Co-op kit (partner): wear / take off on the Clothing page is sent to the host");
REXCVAR_DEFINE_BOOL(coop_kit_send_use, true, "Network",
                    "Co-op kit (partner): use (potions, food) on the Items page is sent to the host");
REXCVAR_DEFINE_STRING(coop_kit_send_line, "", "Network",
                      "Co-op kit test (partner): set to '<equip|wear|unwear|use> <record hex>' to send it "
                      "to the host once (the coop_kit_send command, as a setting the test input can set)");
REXCVAR_DEFINE_BOOL(coop_kit_xp_probe, false, "Network",
                    "Testing (partner): log player 2's components with a float array at +16 (experience probe), once");
REXCVAR_DEFINE_BOOL(coop_kit_unequip, true, "Network",
                    "Co-op kit (partner): Y on the Weapons page takes the carried weapon back into the bag (001eq)");
REXCVAR_DEFINE_BOOL(coop_kit_equip_local, true, "Network",
                    "Co-op kit (partner): an equip sent to the host also runs on the copy with the page's own call (001eq)");
REXCVAR_DEFINE_BOOL(coop_kit_mirror_worn, true, "Network",
                    "Co-op kit (partner): wear on the copy what the host wears on player 2 (001eq)");
REXCVAR_DEFINE_BOOL(coop_kit_send_equip, true, "Network",
                    "Co-op kit (partner): equip on the Weapons page is sent to the host");
// Off: L4 of 17:14Z, the henchman quit question opened from the full wheel did
// not close on B (stuck, host paused). With it off the Save row does nothing.
REXCVAR_DEFINE_BOOL(coop_kit_redirect_save, false, "Network",
                    "Co-op kit (partner): the wheel's Save row opens the henchman quit instead (experimental)");
REXCVAR_DEFINE_BOOL(coop_kit_redirect_abilities, true, "Network",
                    "Co-op kit (partner): the wheel's Abilities row opens the henchman abilities");
REXCVAR_DEFINE_BOOL(coop_kit_redirect_kick, true, "Network",
                    "Co-op kit (partner): the wheel's Kick row does nothing");
REXCVAR_DEFINE_BOOL(coop_kit_start_closes, true, "Network",
                    "Co-op kit (partner): Start closes the full pause wheel (B is pressed until it closes)");
REXCVAR_DEFINE_BOOL(coop_kit_log_events, false, "Network",
                    "Co-op kit test (partner): log every pause-menu event (type, name) at info level");
REXCVAR_DEFINE_BOOL(coop_kit_no_host_pause, false, "Network",
                    "Co-op kit (partner): opening Items/Weapons/Clothing does not pause the host's game "
                    "(off: the stock pause; untested)");

extern "C" void sub_8261A058(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8261B1A0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82741BD8(PPCContext& ctx, uint8_t* base);

namespace fable2::coopkit {

namespace ta = fable2::textappend;

constexpr uint32_t kInventoryEC = 13;
constexpr uint32_t kAppearanceEC = 15;

// The joiner side is on while visiting a co-op campaign with coop_kit.
inline bool joiner_active() {
  return REXCVAR_GET(coop_kit) && REXCVAR_GET(coop_campaign) && fable2::coopcam::g_joined.load();
}

inline void be32(uint8_t* base, uint32_t a, uint32_t v) {
  base[a] = uint8_t(v >> 24);
  base[a + 1] = uint8_t(v >> 16);
  base[a + 2] = uint8_t(v >> 8);
  base[a + 3] = uint8_t(v);
}

// --- the copy's components and stacks ------------------------------------------------

struct Component {
  uint32_t type, ptr;
};
inline std::vector<Component> components(const uint8_t* base, uint32_t entity) {
  std::vector<Component> out;
  if (!entity || !ta::rdable2(base, entity + 72, 8)) return out;
  const uint32_t b = ta::load_be(base, entity + 72), e = ta::load_be(base, entity + 76);
  for (uint32_t p = b; p < e && p - b < 8 * 512 && ta::rdable2(base, p, 8); p += 8)
    out.push_back({ta::load_be(base, p), ta::load_be(base, p + 4)});
  return out;
}
inline uint32_t component(const uint8_t* base, uint32_t entity, uint32_t type) {
  for (const auto& c : components(base, entity))
    if (c.type == type) return c.ptr;
  return 0;
}

struct Stack {
  uint32_t node, stack, id, count;
};
// Walks the inventory's stack list (same walk as GetNumberOfItems). Stops at
// 1024 nodes or an unreadable pointer instead of trapping.
inline std::vector<Stack> stacks(const uint8_t* base, uint32_t inv, bool* ok = nullptr) {
  std::vector<Stack> out;
  if (ok) *ok = false;
  if (!inv || !ta::rdable2(base, inv + 12, 8)) return out;
  const uint32_t sentinel = ta::load_be(base, inv + 16);
  if (!ta::rdable2(base, sentinel, 4)) return out;
  uint32_t node = ta::load_be(base, sentinel);
  for (int n = 0; node != sentinel && n < 1024; ++n) {
    if (!ta::rdable2(base, node, 12)) return out;
    const uint32_t st = ta::load_be(base, node + 8);
    if (!ta::rdable2(base, st, 16)) return out;
    out.push_back({node, st, ta::load_be(base, st + 4), ta::load_be(base, st + 12)});
    node = ta::load_be(base, node);
  }
  if (ok) *ok = node == sentinel;
  return out;
}

inline std::map<std::string, uint32_t> counts(const std::vector<Stack>& ss) {
  std::map<std::string, uint32_t> m;
  for (const auto& s : ss) m[IdToHex(s.id)] += s.count;
  return m;
}

// --- guest calls ----------------------------------------------------------------------

// A 0x100-byte frame below the current guest stack pointer (backchain kept);
// returns the address of a 0x40-byte scratch area inside it.
inline uint32_t push_frame(PPCContext& ctx, uint8_t* base) {
  const uint32_t old = ctx.r1.u32;
  const uint32_t sp = (old - 0x100) & ~0xFu;
  be32(base, sp, old);
  ctx.r1.u64 = sp;
  return sp + 0x80;
}

inline bool native_add(PPCContext& ctx, uint8_t* base, uint32_t inv, uint32_t id) {
  const uint32_t saved_r1 = ctx.r1.u32;
  const uint32_t out = push_frame(ctx, base);
  for (int i = 0; i < 4; ++i) be32(base, out + 4 * i, 0);
  ctx.r3.u64 = out;
  ctx.r4.u64 = inv;
  ctx.r5.u64 = id;
  sub_8261A058(ctx, base);
  const uint32_t handle = ta::load_be(base, out);
  if (handle) {
    ctx.r3.u64 = handle;
    ctx.r4.u64 = out;
    sub_82741BD8(ctx, base);
  }
  ctx.r1.u64 = saved_r1;
  return handle != 0;
}

inline void native_remove(PPCContext& ctx, uint8_t* base, uint32_t inv, uint32_t id) {
  const uint32_t saved_r1 = ctx.r1.u32;
  push_frame(ctx, base);
  ctx.r3.u64 = inv;
  ctx.r4.u64 = id;
  sub_8261B1A0(ctx, base);
  ctx.r1.u64 = saved_r1;
}

// --- launch 1 probes (native tasks on the game thread) ------------------------------------

inline void log_probe(PPCContext& /*ctx*/, uint8_t* base, const char* why) {
  const uint32_t entity = fable2::coopcam::g_p2_entity.load();
  const auto comps = components(base, entity);
  std::string types;
  for (const auto& c : comps) types += std::to_string(c.type) + " ";
  const uint32_t inv = component(base, entity, kInventoryEC);
  const uint32_t app = component(base, entity, kAppearanceEC);
  const bool avail = entity && ta::rdable2(base, entity + 36, 4) && ((ta::load_be(base, entity + 36) >> 13) & 1);
  REXSYS_INFO("[coopkit] probe ({}): p2 entity 0x{:08X} joined={} components {}: [{}]", why, entity,
              fable2::coopcam::g_joined.load(), comps.size(), types);
  REXSYS_INFO("[coopkit] probe: inventory(13) 0x{:08X} available-bit={} appearance(15) 0x{:08X}", inv, avail,
              app);
  if (inv && ta::rdable2(base, inv, 32)) {
    std::string head;
    for (int i = 0; i < 8; ++i) {
      char w[12];
      std::snprintf(w, sizeof(w), "%08X ", ta::load_be(base, inv + 4 * i));
      head += w;
    }
    REXSYS_INFO("[coopkit] probe: inventory head {}", head);
  }
  bool ok = false;
  const auto ss = stacks(base, inv, &ok);
  uint32_t total = 0;
  for (const auto& s : ss) total += s.count;
  REXSYS_INFO("[coopkit] probe: {} stacks, {} items, list {}", ss.size(), total, ok ? "complete" : "INCOMPLETE");
  for (const auto& s : ss) REXSYS_INFO("[coopkit]   stack 0x{:08X} rec {} x{}", s.stack, IdToHex(s.id), s.count);
}

inline void CmdProbe(std::string_view) {
  fable2::f5lua::request_native([](PPCContext& ctx, uint8_t* base) { log_probe(ctx, base, "console"); });
}

struct AddArgs {
  uint32_t id = 0;
  int n = 1;
  bool ok = false;
};
inline AddArgs parse_args(std::string_view args) {
  AddArgs a;
  const std::string s = fable2::luaconsole::trim(args);
  const size_t sp = s.find(' ');
  const std::string hex = s.substr(0, sp);
  if (!IsRecordHex(hex)) {
    REXSYS_WARN("[coopkit] expected an 8-digit upper-case record id, got '{}'", hex);
    return a;
  }
  a.id = HexToId(hex);
  if (sp != std::string::npos) a.n = std::max(1, std::atoi(s.c_str() + sp + 1));
  a.ok = true;
  return a;
}

inline void CmdAdd(std::string_view args) {
  const AddArgs a = parse_args(args);
  if (!a.ok) return;
  if (!REXCVAR_GET(coop_kit)) {
    REXSYS_WARN("[coopkit] coop_kit is off");
    return;
  }
  fable2::f5lua::request_native([a](PPCContext& ctx, uint8_t* base) {
    const uint32_t inv = component(base, fable2::coopcam::g_p2_entity.load(), kInventoryEC);
    if (!inv) {
      REXSYS_WARN("[coopkit] add: no inventory on player 2");
      return;
    }
    const uint32_t before = counts(stacks(base, inv))[IdToHex(a.id)];
    REXSYS_INFO("[coopkit] add {} x{} to inventory 0x{:08X} (has {}): calling sub_8261A058", IdToHex(a.id), a.n,
                inv, before);
    int made = 0;
    for (int i = 0; i < a.n; ++i) made += native_add(ctx, base, inv, a.id) ? 1 : 0;
    const uint32_t after = counts(stacks(base, inv))[IdToHex(a.id)];
    REXSYS_INFO("[coopkit] add {}: {} handle(s) returned, count {} -> {}", IdToHex(a.id), made, before, after);
  });
}

inline void CmdRemove(std::string_view args) {
  const AddArgs a = parse_args(args);
  if (!a.ok) return;
  if (!REXCVAR_GET(coop_kit)) {
    REXSYS_WARN("[coopkit] coop_kit is off");
    return;
  }
  fable2::f5lua::request_native([a](PPCContext& ctx, uint8_t* base) {
    const uint32_t inv = component(base, fable2::coopcam::g_p2_entity.load(), kInventoryEC);
    if (!inv) {
      REXSYS_WARN("[coopkit] remove: no inventory on player 2");
      return;
    }
    const uint32_t before = counts(stacks(base, inv))[IdToHex(a.id)];
    REXSYS_INFO("[coopkit] remove {} x{} from inventory 0x{:08X} (has {}): calling sub_8261B1A0", IdToHex(a.id),
                a.n, inv, before);
    for (int i = 0; i < a.n; ++i) native_remove(ctx, base, inv, a.id);
    const uint32_t after = counts(stacks(base, inv))[IdToHex(a.id)];
    REXSYS_INFO("[coopkit] remove {}: count {} -> {}", IdToHex(a.id), before, after);
  });
}

// --- the full pause wheel on the joiner -----------------------------------------------------

inline thread_local bool t_in_pause_open = false;
inline std::atomic<int> g_wheel_logged{0};

// --- the channel (contracts/coop-channel.md) ------------------------------------------------
// Host: applies the partner's kit_cmd once each, in order, with CoopKit.Apply on
// the henchman (game thread, through the Lua runner); publishes kit_ack and
// kit_state. Joiner: publishes its queue as kit_cmd, takes kit_ack and
// kit_state, and corrects its player-2 copy to kit_state (native, below).

namespace lobby = rex::kernel::xam::lobby;

constexpr int64_t kTickMs = 250;
constexpr int64_t kSnapshotEveryMs = 2000;
constexpr int64_t kRetryMs = 1000;
constexpr int64_t kGiveUpMs = 30000;

inline std::mutex& mtx() {
  static std::mutex m;
  return m;
}

struct HostSide {
  HostApplier applier;
  std::optional<Command> running;  // the command whose Lua is queued or retried
  int64_t first_try_ms = 0, next_try_ms = 0;
  bool lua_queued = false;
  int64_t lua_queued_ms = 0;
  // kit_state.rev only increases on the partner, so the published rev is
  // rev_base (the wall clock at start) + rev: a host that restarts while the
  // partner's game keeps running publishes above its old revs. rev itself
  // stays small because it goes through the game's Lua, whose numbers are
  // floats (a rev of 1.79e9 came back rounded to steps of 128, and the
  // partner ignored every state in between: 95133ef's proof).
  uint32_t rev = 0;
  uint32_t rev_base = static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
  int64_t next_snapshot_ms = 0;
  bool snapshot_queued = false;
  int64_t snapshot_queued_ms = 0;
  std::string last_state;
  int64_t last_tick_ms = 0, paused_since_ms = 0;
  WeaponRecMap weapon_recs;
};
struct JoinerSide {
  JoinerQueue queue{std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count())};
  std::string published;
  std::optional<KitState> state;
  uint32_t mirrored_rev = 0;
  uint32_t last_ack_rev = 0;
};
inline HostSide& host() {
  static HostSide h;
  return h;
}
inline JoinerSide& joiner() {
  static JoinerSide j;
  return j;
}

inline int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

inline std::string read_kit_lib() {
  const auto file = fable2::f5lua::g_data_root / "data" / "scripts" / "recomp" / "coop_kit.lua";
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    REXSYS_ERROR("[coopkit] {} not found", file.string());
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return "CoopKitQuiet = true\n" + ss.str() + "\nCoopKitQuiet = nil\n";
}

inline bool host_active(const lobby::LobbyStatus& s) {
  return REXCVAR_GET(coop_kit) && REXCVAR_GET(coop_campaign) && s.in_world && s.joined_host_ip == 0;
}

// Host: queue the snapshot (its COOPKIT line comes back through OnStateLine).
inline void queue_snapshot(HostSide& h) {
  const std::string lib = read_kit_lib();
  if (lib.empty()) return;
  ++h.rev;
  std::string chunk = lib;
  chunk += "local hench = GetPlayerHenchman()\n";
  chunk += "if hench and hench:IsAlive() then print(CoopKit.Snapshot(hench, " + std::to_string(h.rev) + ")) end\n";
  if (fable2::luaconsole::RunChunk(chunk)) {
    h.snapshot_queued = true;
    h.snapshot_queued_ms = now_ms();
  }
}

// Host: queue CoopKit.Apply for the running command (result via OnResultLine).
inline void queue_apply(HostSide& h) {
  const std::string lib = read_kit_lib();
  if (lib.empty() || !h.running) return;
  const Command& c = *h.running;
  std::string chunk = lib;
  chunk += "local hench = GetPlayerHenchman()\n";
  // Apply once per command even if this chunk runs again (L2 15:44Z: queued
  // during a pause, one use ran 7 times): a final result is kept and returned.
  std::string key;  // the sid comes from the network: only letters and digits go into the Lua text
  for (char ch : c.sid)
    if (std::isalnum(static_cast<unsigned char>(ch))) key += ch;
  key += ":" + std::to_string(c.seq);
  chunk += "CoopKitDone = CoopKitDone or {}\n";
  chunk += "local res = CoopKitDone['" + key + "']\n";
  chunk += "if not res then\n";
  chunk += "  res = 'no:busy'\n";
  chunk += "  if hench and hench:IsAlive() then res = CoopKit.Apply(hench, '" + std::string(OpName(c.op)) + "', '" +
           c.rec + "') end\n";
  chunk += "  if res ~= 'no:pending' and res ~= 'no:busy' then CoopKitDone['" + key + "'] = res end\n";
  chunk += "end\n";
  chunk += "print('COOPKITRES|" + std::to_string(c.seq) + "|' .. tostring(res))\n";
  h.lua_queued = fable2::luaconsole::RunChunk(chunk);
  h.lua_queued_ms = now_ms();
}

// A queued chunk whose line never came back (launch 1: one snapshot was lost
// while console chunks were queued, which held the acks for 2 minutes) is
// given up after this long and queued again.
constexpr int64_t kChunkLostMs = 5000;

// The host's game is paused (L2: the partner's stock item pages pause both
// games, "User has paused the game"): the game VM runs no queued Lua, so a
// queued chunk is waiting, not lost. While paused the give-up and lost-chunk
// clocks stand still and no new snapshot is queued; commands apply the moment
// the partner closes the menu (decision A, 2026-10-25).
// 3 s: console chunks queued in a burst made 0-4 s "pauses" at 1.5 s (L2 of 15:18Z).
constexpr int64_t kStalledAfterMs = 3000;
inline bool lua_stalled() {
  return fable2::f5lua::queued_count() > 0 &&
         fable2::f5lua::steady_ms() - fable2::f5lua::g_last_run_ms > kStalledAfterMs;
}

// The other PC of this campaign: the host listens only to its partner and the
// partner only to its host (two co-op pairs on one network would otherwise
// apply each other's commands). Matched by XUID or address, as 001's purse and
// go records are: the host is heard once per shared network, with one address
// each. Neither known yet = everyone, as before.
struct Peer {
  uint64_t xuid = 0;
  uint32_t ip = 0;
};
inline bool from_peer(const lobby::PeerData& d, const Peer& p) {
  if (!p.xuid && !p.ip) return true;
  return (p.xuid && d.xuid == p.xuid) || (p.ip && d.ip == p.ip);
}
inline Peer campaign_partner() {
  namespace cf = fable2::coopfrontend;
  Peer p;
  p.xuid = cf::g_partner_xuid.load();
  std::lock_guard<std::mutex> l(cf::S().m);
  p.ip = cf::S().partner_ip;
  return p;
}
inline Peer campaign_host(const lobby::LobbyStatus& s) {
  const uint64_t x = fable2::coopfrontend::g_purse_host_xuid.load();
  return {x > 1 ? x : 0, s.joined_host_ip};  // 1 = joined, host XUID not known
}

inline void host_tick(const lobby::LobbyStatus& s, const Peer& partner, int64_t now) {
  auto& h = host();
  const int64_t dt = h.last_tick_ms ? now - h.last_tick_ms : 0;
  h.last_tick_ms = now;
  if (lua_stalled()) {
    if (!h.paused_since_ms) {
      h.paused_since_ms = now;
      REXSYS_INFO("[coopkit] host: game paused (Lua waiting); commands wait for the unpause");
    }
    h.first_try_ms += dt;
    h.next_try_ms += dt;
    h.lua_queued_ms += dt;
    h.snapshot_queued_ms += dt;
    return;
  }
  if (h.paused_since_ms) {
    REXSYS_INFO("[coopkit] host: game resumed after {} s", (now - h.paused_since_ms) / 1000);
    h.paused_since_ms = 0;
  }
  if (h.snapshot_queued && now - h.snapshot_queued_ms > kChunkLostMs) {
    REXSYS_WARN("[coopkit] host: snapshot rev {} did not come back; queuing a new one", h.rev);
    h.snapshot_queued = false;
    h.next_snapshot_ms = 0;
  }
  if (h.lua_queued && now - h.lua_queued_ms > kChunkLostMs) {
    h.lua_queued = false;
    if (h.running && h.running->op == Op::kUse) {
      // Using is not idempotent (a second run drinks a second potion): never re-run it.
      REXSYS_WARN("[coopkit] host: command {} (use) result did not come back; not retried", h.running->seq);
      h.applier.Done(h.running->seq, "no:fail");
      h.running.reset();
      h.next_snapshot_ms = 0;
    } else {
      REXSYS_WARN("[coopkit] host: command {} result did not come back; trying again",
                  h.running ? h.running->seq : 0);
      h.next_try_ms = now;
    }
  }
  // The partner's newest command list.
  const lobby::PeerData* best = nullptr;
  std::vector<lobby::PeerData> cmds;
  for (auto& d : lobby::GetPeerData("kit_cmd"))
    if (from_peer(d, partner)) cmds.push_back(std::move(d));
  for (const auto& d : cmds)
    if (!best || d.seconds_since_received < best->seconds_since_received) best = &d;
  if (!h.running && best) {
    if (auto c = h.applier.Next(ParseCommandList(best->value))) {
      h.running = c;
      h.first_try_ms = now;
      h.next_try_ms = now;
      REXSYS_INFO("[coopkit] host: command {} {} {} from {}", c->seq, OpName(c->op), c->rec, best->gamertag);
    }
  }
  if (h.running && !h.lua_queued && now >= h.next_try_ms) {
    if (const std::string why = fable2::coopbusy::Busy(); !why.empty()) {
      h.next_try_ms = now + kRetryMs;  // contract rule 3: wait while busy
    } else {
      queue_apply(h);
    }
    if (now - h.first_try_ms > kGiveUpMs) {
      REXSYS_WARN("[coopkit] host: command {} gave up after {} s", h.running->seq, kGiveUpMs / 1000);
      h.applier.Done(h.running->seq, "no:busy");
      h.running.reset();
      h.next_snapshot_ms = 0;
    }
  }
  const bool partner_here = !cmds.empty() || !lobby::GetPeerData("proto").empty();
  if (partner_here && !h.snapshot_queued && now >= h.next_snapshot_ms) {
    h.next_snapshot_ms = now + kSnapshotEveryMs;
    queue_snapshot(h);
  }
  (void)s;
}

// Host: "COOPKITRES|<seq>|<res>" from queue_apply's chunk.
inline void OnResultLine(std::string_view line) {
  std::lock_guard<std::mutex> l(mtx());
  auto& h = host();
  h.lua_queued = false;
  const auto p = Split(line, '|');
  if (p.size() < 3 || !h.running || ToU32(std::string(p[1])) != h.running->seq) return;
  const std::string res(p[2]);
  const int64_t now = now_ms();
  if ((res == "no:pending" || res == "no:busy") && now - h.first_try_ms < kGiveUpMs) {
    h.next_try_ms = now + kRetryMs;  // queued game action: read back again shortly
    return;
  }
  REXSYS_INFO("[coopkit] host: command {} -> {}", h.running->seq, res == "no:pending" ? "no:busy" : res);
  if (res == "ok" && h.running->op == Op::kEquip) h.weapon_recs.OnEquipOk(h.running->rec);
  h.applier.Done(h.running->seq, res == "no:pending" ? "no:busy" : res);
  h.running.reset();
  h.next_snapshot_ms = 0;  // a fresh kit_state with the result
}

// Host: the snapshot line. Publishes kit_state and the ack that goes with it.
inline void OnStateLine(std::string_view line) {
  std::lock_guard<std::mutex> l(mtx());
  auto& h = host();
  h.snapshot_queued = false;
  auto k = ParseKitState(line);
  if (!k || k->rev == 0) return;  // rev 0 = a console `luafile coop_kit`, not the channel's
  k->rev += h.rev_base;
  h.weapon_recs.Translate(*k);
  const std::string value = Format(*k);
  if (value != h.last_state) {
    h.last_state = value;
    lobby::PublishData("kit_state", value);
  }
  lobby::PublishData("kit_ack", Format(h.applier.MakeAck(k->rev)));
}

// --- joiner: the mirror ------------------------------------------------------------

// Corrects the copy's inventory (component 13) to the state's non-clothing
// items. Clothing is worn through the host's own replication, so it is never
// removed here; with coop_kit_mirror_clothing, owned clothing missing from the
// copy is added (L1b: the copy lists its worn clothes as inventory stacks, and
// a hat given on the host never reached the Clothing page).
// 001eq (b1, wear): the joiner's own state decides its player 2's look (the host's
// wear on player 2 is reset by it, fable2_coop_hero.h dress_player2), so what the
// host wears on player 2 is put on the copy here with the core appearance add
// sub_8256FC80(appearance, record id): every record newly in kit_state's worn list
// since the last mirror (all of it after a new spawn). Taking off is its mirror:
// AppearanceModifierManager.RemoveItemOfType (binding sub_8256FFC0) turns the type
// name into a record id and calls sub_82570000(appearance, record id), so every
// record that left the worn list is taken off the copy with that call.
extern "C" void sub_82570000(PPCContext& ctx, uint8_t* base);
inline uint32_t g_worn_epoch = ~0u;
inline std::vector<std::string> g_worn_applied;
inline void mirror_worn(PPCContext& ctx, uint8_t* base, const KitState& want) {
  if (!REXCVAR_GET(coop_kit_mirror_worn)) return;
  const uint32_t epoch = fable2::coophero::g_spawn_epoch.load();
  if (epoch != g_worn_epoch) {
    g_worn_epoch = epoch;
    g_worn_applied.clear();
  }
  // The appearance manager component (EC 15) from the copy's component table, the
  // object the AppearanceModifierManager bindings get; the vtable scan of the entity's
  // first 0x400 bytes (fable2_coop_hero.h) found nothing on this copy (proof run
  // 2026-10-28: "appearance 0x00000000" every revision) and stays the fallback.
  const uint32_t p2 = fable2::coopcam::g_p2_entity.load();
  uint32_t appearance = component(base, p2, kAppearanceEC);
  if (!appearance) appearance = fable2::coophero::find_appearance(base, p2);
  static uint32_t s_logged_rev = 0;
  if (want.rev != s_logged_rev) {  // 001eq proof: why a worn change did not reach the copy
    s_logged_rev = want.rev;
    const uint32_t vt = appearance && ta::rdable2(base, appearance, 8) ? ta::load_be(base, appearance) : 0;
    const uint32_t back = appearance && ta::rdable2(base, appearance, 8) ? ta::load_be(base, appearance + 4) : 0;
    REXSYS_INFO("[coopkit] worn mirror rev {}: appearance 0x{:08X} (vtable 0x{:08X}, +4 0x{:08X}, entity 0x{:08X}), "
                "host wears {}, applied {}", want.rev, appearance, vt, back, p2, want.worn.size(), g_worn_applied.size());
    if (appearance && vt != fable2::coophero::kAppearanceVtable) {
      REXSYS_WARN("[coopkit] worn mirror: component 15 is not the appearance object (vtable 0x{:08X}); not wearing", vt);
    }
  }
  if (!appearance || !ta::rdable2(base, appearance, 4) || ta::load_be(base, appearance) != fable2::coophero::kAppearanceVtable)
    return;
  int worn = 0, off = 0;
  for (const auto& rec : g_worn_applied) {
    if (std::find(want.worn.begin(), want.worn.end(), rec) != want.worn.end()) continue;
    ctx.r3.u64 = appearance;
    ctx.r4.u64 = HexToId(rec);
    sub_82570000(ctx, base);
    ++off;
  }
  for (const auto& rec : want.worn) {
    if (std::find(g_worn_applied.begin(), g_worn_applied.end(), rec) != g_worn_applied.end()) continue;
    ctx.r3.u64 = appearance;
    ctx.r4.u64 = HexToId(rec);
    sub_8256FC80(ctx, base);
    ++worn;
  }
  g_worn_applied = want.worn;
  if (worn || off)
    REXSYS_INFO("[coopkit] mirror rev {}: wore {}, took off {} item(s) on the copy ({} worn on the host)", want.rev,
                worn, off, want.worn.size());
}

inline void mirror_now(PPCContext& ctx, uint8_t* base, const KitState& want) {
  const uint32_t inv = component(base, fable2::coopcam::g_p2_entity.load(), kInventoryEC);
  if (!inv) return;
  bool complete = false;
  const auto have = counts(stacks(base, inv, &complete));
  if (!complete) {
    REXSYS_WARN("[coopkit] mirror: the copy's inventory list did not read back whole; skipped");
    return;
  }
  const KitState inv_only = WantedInventory(want, REXCVAR_GET(coop_kit_mirror_weapons));
  std::map<std::string, uint32_t> have_inv;
  for (const auto& [rec, n] : have) {
    const auto it = std::find_if(want.items.begin(), want.items.end(), [&](const Item& i) { return i.rec == rec; });
    if (it == want.items.end() || it->category.rfind("CLOTHING", 0) != 0) have_inv[rec] = n;
  }
  int adds = 0, removes = 0;
  for (const auto& ch : MirrorDiff(inv_only, have_inv)) {
    const uint32_t id = HexToId(ch.rec);
    for (int i = 0; i < ch.delta && i < 64; ++i) adds += native_add(ctx, base, inv, id) ? 1 : 0;
    for (int i = 0; i < -ch.delta && i < 64; ++i, ++removes) native_remove(ctx, base, inv, id);
  }
  if (REXCVAR_GET(coop_kit_mirror_clothing))
    for (const auto& i : want.items)
      if (i.category.rfind("CLOTHING", 0) == 0 && !have.count(i.rec))
        adds += native_add(ctx, base, inv, HexToId(i.rec)) ? 1 : 0;
  if (adds || removes)
    REXSYS_INFO("[coopkit] mirror rev {}: +{} -{} on the copy's inventory", want.rev, adds, removes);
  mirror_worn(ctx, base, want);
}

// 001eq (b2): the copy's weapon slots and taken-off clothes are not mirrored (no
// known native), so the partner is told what the host did with a toast.
extern "C" void rex_ui_set_toast(const char* text, int ms);
inline std::map<uint32_t, Op> g_sent_ops;  // seq -> op, for the ack toast
// 001up: player 2's experience from the host's kit_state, for the Experience
// binding thunk below (the game thread reads them; joiner_tick writes them).
inline std::atomic<bool> g_p2_xp_valid{false};
inline std::atomic<int> g_p2_xp[4] = {0, 0, 0, 0}, g_p2_xpt[4] = {0, 0, 0, 0};
constexpr uint16_t kPadY = 0x8000;
inline std::atomic<bool> g_weapons_page{false};
inline std::atomic<int> g_unequip_pending{0};  // -1: Y pressed on the Weapons page (pad_hook)
inline void ack_toast(const Result& r) {
  const auto it = g_sent_ops.find(r.seq);
  if (it == g_sent_ops.end()) return;
  const Op op = it->second;
  g_sent_ops.erase(it);
  std::string text;
  if (r.res != "ok") text = ReasonText(r.res);
  else if (op == Op::kEquip) text = "Equipped (on the host)";
  else if (op == Op::kUnwear) text = "Taken off (on the host)";
  else if (op == Op::kUnequipWeapon) text = "Unequipped";
  else if (op == Op::kWear) text = "Worn";
  else return;
  rex_ui_set_toast(text.c_str(), 2500);
}

inline std::string g_send_line_seen;
inline int64_t g_unequip_hint_ms = 0;
inline void joiner_tick(const Peer& host_pc, int64_t now) {
  auto& j = joiner();
  if (g_weapons_page.load() && j.state && REXCVAR_GET(coop_kit_unequip)) {
    const bool armed = !j.state->melee.empty() || !j.state->ranged.empty();
    if (armed && now > g_unequip_hint_ms) {
      rex_ui_set_toast("Y: Unequip weapon", 1500);
      g_unequip_hint_ms = now + 1000;
    }
    if (g_unequip_pending.exchange(0) == -1) {
      if (!j.state->melee.empty() || !j.state->ranged.empty()) {
        const bool melee = !j.state->melee.empty();
        const char* code = melee ? "00000001" : "00000002";
        const uint32_t seq = j.queue.Add(Op::kUnequipWeapon, code);
        g_sent_ops[seq] = Op::kUnequipWeapon;
        REXSYS_INFO("[coopkit] partner: command {} unequipw {} (Y on the Weapons page: the {} weapon)", seq, code,
                    melee ? "melee" : "ranged");
      } else {
        rex_ui_set_toast("No weapon equipped", 1500);
      }
    }
  }
  if (const std::string line = REXCVAR_GET(coop_kit_send_line); line != g_send_line_seen) {
    g_send_line_seen = line;
    std::istringstream in(line);
    std::string op_s, hex;
    in >> op_s >> hex;
    if (const auto op = ParseOp(op_s); op && hex.size() == 8) {
      const uint32_t seq = j.queue.Add(*op, hex);
      g_sent_ops[seq] = *op;
      REXSYS_INFO("[coopkit] partner: command {} {} {} (coop_kit_send_line)", seq, op_s, hex);
    }
  }
  const std::string pub = j.queue.Published();
  if (pub != j.published) {
    j.published = pub;
    lobby::PublishData("kit_cmd", pub);
  }
  for (const auto& d : lobby::GetPeerData("kit_ack")) {
    if (!from_peer(d, host_pc)) continue;
    if (auto a = ParseAck(d.value))
      for (const auto& r : j.queue.OnAck(*a)) {
        REXSYS_INFO("[coopkit] command {}: {} ({})", r.seq, r.res, ReasonText(r.res));
        ack_toast(r);
      }
  }
  for (const auto& d : lobby::GetPeerData("kit_state")) {
    if (!from_peer(d, host_pc)) continue;
    auto k = ParseKitState(d.value);
    if (!k || k->rev == 0 || (j.state && k->rev <= j.state->rev)) continue;
    j.state = *k;
    if (k->has_xp) {
      for (int i = 0; i < 4; ++i) {
        g_p2_xp[i] = k->xp[i];
        g_p2_xpt[i] = k->xpt[i];
      }
      g_p2_xp_valid = true;
    }
  }
  if (j.state && j.state->rev != j.mirrored_rev && REXCVAR_GET(coop_kit_mirror)) {
    j.mirrored_rev = j.state->rev;
    const KitState want = *j.state;
    fable2::f5lua::request_native([want](PPCContext& ctx, uint8_t* base) { mirror_now(ctx, base, want); });
  }
}

// Joiner: a screen action on an item becomes a command.
inline void SendCommand(Op op, uint32_t id) {
  std::lock_guard<std::mutex> l(mtx());
  const uint32_t seq = joiner().queue.Add(op, IdToHex(id));
  g_sent_ops[seq] = op;
  REXSYS_INFO("[coopkit] partner: command {} {} {}", seq, OpName(op), IdToHex(id));
}

// --- Start closes the partner's full wheel ------------------------------------------------
// The partner's Start opens the wheel (sub_82846078 -> the builder), but a second
// Start does not close our full wheel the way it closes the stock henchman wheel
// (perf thread 16:13: the host stayed paused; l2_try2 10:00:36: it rebuilt the
// wheel). B closes it at every level, so while the wheel is up a new Start is
// kept from the game and B is pressed until the pause screen exits.
//   up:   the forced full wheel (builder), the pause screen entered (sub_82583340),
//         a page's PauseGame (sub_82336D70);
//   down: the pause screen's exit (sub_825832B0, which calls UnpauseGame).
// A B that brings no menu event within 1.2 s means the wheel was not up after
// all: the flag drops and the Start is given back.
constexpr uint16_t kPadStart = 0x0010, kPadB = 0x2000;
constexpr int kStartBs = 6;
inline std::atomic<bool> g_menu_up{false}, g_mask_start{false};
// The partner's own pause wheel (and every page opened from it), from its
// builder to the pause screen's exit only: unlike g_menu_up, no pause of the
// host's game raises it (fable2_coop_worlduse.h keeps the partner's A for its
// menus out of the world).
inline std::atomic<bool> g_wheel_open{false};
inline std::atomic<int> g_b_left{0};
inline std::atomic<int64_t> g_menu_event_ms{0}, g_next_b_ms{0}, g_close_from_ms{0};
inline uint16_t g_prev_pad = 0;
inline void menu_event(bool up, const char* what) {
  if (up) g_menu_up = true;
  else g_menu_up = false;
  g_menu_event_ms = fable2::f5lua::steady_ms();
  REXSYS_INFO("[coopkit] partner menu: {}", what);  // once per menu action: not capped
}
// Input thread: pad 0 as the game will see it (fable2_coop_lobby.h).
// 001eq: the stock Weapons page has no unequip. While the partner's Weapons page is
// open, Y takes the carried weapon back into the bag: the melee one first, then the
// ranged one (the host's Carrying.ReturnEntityToInventory, CoopKit.Apply
// "unequipw"); the page's own Y is not passed on. A toast shows the hint.
inline uint16_t pad_hook(uint16_t buttons) {
  const uint16_t pressed = buttons & ~g_prev_pad;
  g_prev_pad = buttons;
  if (g_weapons_page.load() && joiner_active() && REXCVAR_GET(coop_kit_unequip)) {
    if (pressed & kPadY) g_unequip_pending = -1;  // which weapon: decided with the kit state
    buttons &= ~kPadY;
  }
  if (!(buttons & kPadStart)) g_mask_start = false;  // kept away until released
  if (!REXCVAR_GET(coop_kit_start_closes) || !joiner_active()) return buttons;
  if ((pressed & kPadStart) && g_menu_up.load() && g_b_left.load() == 0) {
    g_mask_start = true;
    g_close_from_ms = fable2::f5lua::steady_ms();
    g_next_b_ms = 0;
    g_b_left = kStartBs;
    REXSYS_INFO("[coopkit] partner: Start on the open full wheel -> B until it closes");
  }
  if (g_mask_start.load()) buttons &= ~kPadStart;
  return buttons;
}
// The partner chose Quit on its wheel (sub_8240EAA0 below): once the wheel is
// down, it leaves for its front end with the game's own QuitToFrontEnd; the
// host sees the partner go (the partner-lost path: it saves and waits in the
// campaign's lobby).
inline std::atomic<bool> g_leave_pending{false};
inline void leave_tick() {
  if (!g_leave_pending.load() || g_menu_up.load() || g_b_left.load() > 0) return;
  if (fable2::luaconsole::RunChunk(
          "print('[coop] partner leaves: QuitToFrontEnd ' .. tostring(pcall(Debug.QuitToFrontEnd)))\n")) {
    g_leave_pending = false;
    // The host would see a clean leave only as a still-talking peer: tell it
    // (fable2_coop_frontend.h, kHostInWorld: it saves and goes to the lobby).
    const std::string rec = "COOPLEAVE:v1|hxuid=" +
                            fable2::cooplobby::XuidStr(fable2::coopfrontend::g_purse_host_xuid.load()) +
                            "|n=" + std::to_string(fable2::f5lua::steady_ms());
    lobby::PublishData("coop_leave", rec);
    REXSYS_INFO("[coopkit] partner: wheel closed, leaving the host's game ({})", rec);
  }
}
// Render thread, every frame.
inline void start_close_tick() {
  leave_tick();
  if (g_b_left.load() <= 0) return;
  const int64_t now = fable2::f5lua::steady_ms();
  if (!g_menu_up.load()) {
    REXSYS_INFO("[coopkit] partner: full wheel closed by Start ({} B)", kStartBs - g_b_left.load());
    g_b_left = 0;
    return;
  }
  if (g_b_left.load() < kStartBs && now - g_close_from_ms.load() > 1200 &&
      g_menu_event_ms.load() < g_close_from_ms.load()) {
    REXSYS_WARN("[coopkit] partner: B brought no menu event; the wheel was not up: Start given back");
    g_menu_up = false;
    g_b_left = 0;
    fable2::cooplobby::InjectPress(kPadStart, 0, 80);
    return;
  }
  if (now < g_next_b_ms.load()) return;
  if (g_b_left.fetch_sub(1) <= 0) return;
  if (g_b_left.load() == 0 && g_menu_up.load())
    REXSYS_WARN("[coopkit] partner: {} B and the full wheel is still up", kStartBs);
  fable2::cooplobby::InjectPress(kPadB, 0, 80);
  g_next_b_ms = now + 400;
}

inline int64_t g_next_tick_ms = 0;
inline void (*g_prev_frame_hook)() = nullptr;
// 001eq probe (rig): which of player 2's components holds its experience. The
// Experience binding's Get (sub_8251C068) reads the float at *(component + 16) +
// type * 4; this logs every component of the copy whose +16 points at readable
// memory, with its first six floats, so a known value set on the host shows the type.
inline void log_xp_components(PPCContext& /*ctx*/, uint8_t* base) {
  const uint32_t entity = fable2::coopcam::g_p2_entity.load();
  for (const auto& c : components(base, entity)) {
    if (!c.ptr || !ta::rdable2(base, c.ptr + 16, 4)) continue;
    const uint32_t arr = ta::load_be(base, c.ptr + 16);
    if (!arr || !ta::rdable2(base, arr, 24)) continue;
    float f[6];
    for (int i = 0; i < 6; ++i) {
      const uint32_t v = ta::load_be(base, arr + 4 * i);
      std::memcpy(&f[i], &v, 4);
    }
    REXSYS_INFO("[coopkit] xp probe: type {} comp 0x{:08X} +16 -> 0x{:08X}: {:.1f} {:.1f} {:.1f} {:.1f} {:.1f} {:.1f}", c.type,
                c.ptr, arr, f[0], f[1], f[2], f[3], f[4], f[5]);
  }
}

inline void OnFrame() {
  if (g_prev_frame_hook) g_prev_frame_hook();
  if (REXCVAR_GET(coop_kit_xp_probe)) {
    rex::cvar::SetFlagByName("coop_kit_xp_probe", "false");
    fable2::f5lua::request_native(&log_xp_components);
  }
  if (!REXCVAR_GET(coop_kit)) return;
  start_close_tick();
  const int64_t now = now_ms();
  if (now < g_next_tick_ms) return;
  g_next_tick_ms = now + kTickMs;
  const lobby::LobbyStatus s = lobby::GetLobbyStatus();
  if (!s.networking_enabled) return;
  const bool as_host = host_active(s), as_joiner = !as_host && joiner_active();
  if (!as_host && !as_joiner) return;
  const Peer peer = as_host ? campaign_partner() : campaign_host(s);  // before mtx(): S().m is the front end's
  // 001eq: solo with coop_campaign on (Josh's toml) is "in the world, not joined"
  // too; the host side ran a Lua snapshot every 5-7 s for nobody (graphics thread,
  // 2026-10-28). Only a hosted co-op campaign with a known partner snapshots.
  if (as_host && (!fable2::coophero::g_campaign_on.load() || (!peer.xuid && !peer.ip))) return;
  std::lock_guard<std::mutex> l(mtx());
  if (as_host) host_tick(s, peer, now);
  else joiner_tick(peer, now);
}

inline void CmdSend(std::string_view args) {
  const std::string a = fable2::luaconsole::trim(args);
  const size_t sp = a.find(' ');
  const auto op = ParseOp(a.substr(0, sp));
  const std::string hex = sp == std::string::npos ? "" : fable2::luaconsole::trim(a.substr(sp + 1));
  if (!op || !IsRecordHex(hex)) {
    REXSYS_WARN("[coopkit] usage: coop_kit_send equip|wear|unwear|use <record hex>");
    return;
  }
  SendCommand(*op, HexToId(hex));
}

// Runs at static init, after fable2_coop_hero.h's Install (main.cpp order):
// chains its frame hook.
inline bool Install() {
  fable2::luaconsole::AddLineHandler("COOPKITRES|", OnResultLine);
  fable2::luaconsole::AddLineHandler("COOPKIT:v1", OnStateLine);
  g_prev_frame_hook = fable2::f5lua::g_frame_hook;
  fable2::f5lua::g_frame_hook = &OnFrame;
  fable2::cooplobby::g_game_pad_hook = &pad_hook;
  return true;
}
inline const bool g_installed = Install();

}  // namespace fable2::coopkit

REXCVAR_DEFINE_COMMAND_ARGS(coop_kit_send, fable2::coopkit::CmdSend, "Network",
                            "Co-op kit test (partner): send equip|wear|unwear|use <record hex> to the host");

REXCVAR_DEFINE_COMMAND_ARGS(coop_kit_probe, fable2::coopkit::CmdProbe, "Network",
                            "Co-op kit test: log player 2's components and inventory on this PC");
REXCVAR_DEFINE_COMMAND_ARGS(coop_kit_add, fable2::coopkit::CmdAdd, "Network",
                            "Co-op kit test: add <record hex> [n] to player 2's inventory copy on this PC");
REXCVAR_DEFINE_COMMAND_ARGS(coop_kit_remove, fable2::coopkit::CmdRemove, "Network",
                            "Co-op kit test: remove <record hex> [n] from player 2's inventory copy on this PC");

// Pause menu open (r3 = the pause screen). While it runs, the joiner in a
// co-op campaign is not "the henchman" (sub_82336610 below), so it builds the
// full wheel.
extern "C" void __imp__sub_82583340(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82583340(PPCContext& ctx, uint8_t* base) {
  const bool on = fable2::coopkit::joiner_active();
  fable2::coopkit::t_in_pause_open = on;
  if (on && fable2::coopkit::g_wheel_logged.fetch_add(1) < 20)
    REXSYS_INFO("[coopkit] pause menu: full wheel for player 2");
  if (on) fable2::coopkit::menu_event(true, "pause screen entered (sub_82583340)");
  __imp__sub_82583340(ctx, base);
  fable2::coopkit::t_in_pause_open = false;
  if (on) fable2::f5lua::request_native([](PPCContext& c, uint8_t* b) { fable2::coopkit::log_probe(c, b, "pause"); });
}

// --- the stock screens' item actions on the joiner -------------------------------------------
// sub_825A5A40(r3 = screen, r4 = &handle, r5 = entity) is the screen's "A on an
// item": handle -> the inventory stack (+4 record id); it switches on the
// item's category to equip (sub_825A6020, weapons), use (sub_825A60F8) or wear
// (sub_825A71A0, clothing, hair, tattoos). On the joiner in a campaign each of
// those becomes a command to the host and the local change is skipped; the host
// applies it and its kit_state / replication update the copy.
namespace fable2::coopkit {
inline thread_local uint32_t t_screen_item = 0;  // record id while sub_825A5A40 runs on the joiner
inline bool worn_on_host(uint32_t id) {
  std::lock_guard<std::mutex> l(mtx());
  const auto& st = joiner().state;
  if (!st) return false;
  return std::find(st->worn.begin(), st->worn.end(), IdToHex(id)) != st->worn.end();
}
// The item of a leaf call: r4 = &handle, handle -> stack, stack+4 = record id
// (the same convention as sub_825A5A40; L2 15:44Z: the Clothing page calls
// sub_825A71A0 from its own handler, not through sub_825A5A40).
inline uint32_t item_of(const uint8_t* base, uint32_t r4) {
  namespace ta = fable2::textappend;
  if (!ta::rdable2(base, r4, 4)) return 0;
  const uint32_t stack = ta::load_be(base, r4);
  return stack && ta::rdable2(base, stack + 4, 4) ? ta::load_be(base, stack + 4) : 0;
}

// The leaf call on the joiner: send instead of acting. True = skipped. With
// the action's switch off the stock local change runs (launch 1 behaviour).
// Only calls from a screen are taken: through sub_825A5A40 (t_screen_item) or
// from the Clothing page's handler sub_825A7E98 (t_in_screen); other callers
// (the appearance manager's own sub_8257EC78 / sub_8257F0A0) are logged and
// left alone.
inline thread_local bool t_in_screen = false;
inline std::atomic<int> g_other_logged{0};
inline bool screen_action(PPCContext& ctx, uint8_t* base, const char* action, bool enabled) {
  if (!enabled || !joiner_active()) return false;
  if (!t_screen_item && !t_in_screen) {
    if (g_other_logged.fetch_add(1) < 20)
      REXSYS_INFO("[coopkit] partner: {} of {} outside the item screens (lr 0x{:08X}); left local", action,
                  IdToHex(item_of(base, ctx.r4.u32)), static_cast<uint32_t>(ctx.lr));
    return false;
  }
  const uint32_t id = t_screen_item ? t_screen_item : item_of(base, ctx.r4.u32);
  if (!id) return false;
  // One press can reach the leaf twice (L2 16:00Z: sub_8257F0A0 wore the hat
  // twice per A): the same action on the same item within 500 ms is one command.
  static std::string last_action;
  static uint32_t last_id = 0;
  static int64_t last_ms = 0;
  const int64_t now = now_ms();
  if (id == last_id && last_action == action && now - last_ms < 500) {
    ctx.r3.u64 = 0;
    return true;
  }
  last_action = action;
  last_id = id;
  last_ms = now;
  t_screen_item = 0;  // one command per press
  if (const auto op = OpForScreenAction(action, worn_on_host(id))) SendCommand(*op, id);
  ctx.r3.u64 = 0;
  return true;
}
}  // namespace fable2::coopkit

extern "C" void __imp__sub_825A5A40(PPCContext& ctx, uint8_t* base);
extern "C" void sub_825A5A40(PPCContext& ctx, uint8_t* base) {
  namespace ta = fable2::textappend;
  uint32_t id = 0;
  if (fable2::coopkit::joiner_active() && ta::rdable2(base, ctx.r4.u32, 4)) {
    const uint32_t stack = ta::load_be(base, ctx.r4.u32);
    if (stack && ta::rdable2(base, stack + 4, 4)) id = ta::load_be(base, stack + 4);
  }
  fable2::coopkit::t_screen_item = id;
  __imp__sub_825A5A40(ctx, base);
  fable2::coopkit::t_screen_item = 0;
}
// Callers of sub_825A71A0 with their own handle. L2 16:00Z: A on the hat on the
// Clothing page wears it through sub_8257F0A0 (lr 0x8257F29C / 0x8257F468).
// On the joiner its wear is player 2's, which the host owns either way.
extern "C" void __imp__sub_825A7E98(PPCContext& ctx, uint8_t* base);
extern "C" void sub_825A7E98(PPCContext& ctx, uint8_t* base) {
  const bool was = fable2::coopkit::t_in_screen;
  fable2::coopkit::t_in_screen = fable2::coopkit::joiner_active();
  __imp__sub_825A7E98(ctx, base);
  fable2::coopkit::t_in_screen = was;
}
extern "C" void __imp__sub_8257F0A0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8257F0A0(PPCContext& ctx, uint8_t* base) {
  const bool was = fable2::coopkit::t_in_screen;
  fable2::coopkit::t_in_screen = fable2::coopkit::joiner_active();
  __imp__sub_8257F0A0(ctx, base);
  fable2::coopkit::t_in_screen = was;
}
extern "C" void __imp__sub_825A6020(PPCContext& ctx, uint8_t* base);
extern "C" void sub_825A6020(PPCContext& ctx, uint8_t* base) {
  // 001eq: the host still owns player 2 (the command is sent), and with
  // coop_kit_equip_local the page's own equip also runs on the copy, so the
  // partner's Weapons page and hands show the weapon (the copy's weapon slots
  // were never mirrored: Calvin's page kept showing the old sword).
  const PPCContext saved = ctx;
  if (fable2::coopkit::screen_action(ctx, base, "equip", REXCVAR_GET(coop_kit_send_equip))) {
    if (!REXCVAR_GET(coop_kit_equip_local)) return;
    ctx = saved;
    REXSYS_INFO("[coopkit] partner: equip also run on the copy (the page's own call)");
  }
  __imp__sub_825A6020(ctx, base);
}
extern "C" void __imp__sub_825A60F8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_825A60F8(PPCContext& ctx, uint8_t* base) {
  if (fable2::coopkit::screen_action(ctx, base, "use", REXCVAR_GET(coop_kit_send_use))) return;
  __imp__sub_825A60F8(ctx, base);
}
extern "C" void __imp__sub_825A71A0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_825A71A0(PPCContext& ctx, uint8_t* base) {
  if (fable2::coopkit::screen_action(ctx, base, "wear", REXCVAR_GET(coop_kit_send_wear))) return;
  __imp__sub_825A71A0(ctx, base);
}

// Pause wheel page events (r4 = event: +0 -> name string object whose +0 is
// the C string, +4 = event type; 140/141 = highlight up/down, 300 = page
// chosen). The dispatcher sends a name containing "Henchman" to the henchman
// handler sub_824129C0, anything else to the full one sub_82412378. On the
// joiner a chosen page is routed by RoutePage: Abilities and the save row are
// renamed (for this call only) to the henchman pages, Kick is dropped.
namespace fable2::coopkit {
constexpr uint32_t kHenchmanAbilitiesStr = 0x820B065C;  // "HenchmanAbilities" (image)
constexpr uint32_t kHenchmanQuitStr = 0x820B0680;       // "HenchmanQuit" (image)
constexpr uint32_t kPageChosen = 300;
constexpr uint32_t kSaveRow = 301;  // the Save row's event, name "Saving"
inline thread_local bool t_full_page_open = false;
}  // namespace fable2::coopkit

// sub_82336D70(r3 = screen manager) is the game's PauseGame: every pause page
// calls it as it opens (sub_82412378 and the henchman sub_824129C0 alike); it
// raises PAUSE_GAME, which pauses the host too. With coop_kit_no_host_pause the
// joiner's stock full-wheel pages skip it (option B, kept switchable).
extern "C" void __imp__sub_82336D70(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82336D70(PPCContext& ctx, uint8_t* base) {
  if (fable2::coopkit::t_full_page_open && REXCVAR_GET(coop_kit_no_host_pause) && fable2::coopkit::joiner_active()) {
    REXSYS_INFO("[coopkit] partner page opened without the game pause (coop_kit_no_host_pause)");
    return;
  }
  if (fable2::coopkit::joiner_active()) fable2::coopkit::menu_event(true, "PauseGame (sub_82336D70)");
  __imp__sub_82336D70(ctx, base);
}

// sub_82336F98(r3 = screen manager) is the game's UnpauseGame (GUI:OnExit());
// logged only. sub_825832B0 is the pause screen's exit: it calls UnpauseGame and
// is the "down" signal of the partner's wheel (Start closes it, above).
extern "C" void __imp__sub_82336F98(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82336F98(PPCContext& ctx, uint8_t* base) {
  if (fable2::coopkit::joiner_active()) REXSYS_INFO("[coopkit] partner menu: UnpauseGame (sub_82336F98)");
  __imp__sub_82336F98(ctx, base);
}
extern "C" void __imp__sub_825832B0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_825832B0(PPCContext& ctx, uint8_t* base) {
  __imp__sub_825832B0(ctx, base);
  fable2::coopkit::g_wheel_open = false;
  fable2::coopkit::g_weapons_page = false;
  if (fable2::coopkit::joiner_active()) fable2::coopkit::menu_event(false, "pause screen exit (sub_825832B0)");
}

extern "C" void __imp__sub_8240EAA0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8240EAA0(PPCContext& ctx, uint8_t* base) {
  namespace ta = fable2::textappend;
  namespace ck = fable2::coopkit;
  if (!ck::joiner_active() || !ta::rdable2(base, ctx.r4.u32, 8)) return __imp__sub_8240EAA0(ctx, base);
  const uint32_t ev = ctx.r4.u32, type = ta::load_be(base, ev + 4), obj = ta::load_be(base, ev);
  const uint32_t str = obj && ta::rdable2(base, obj, 4) ? ta::load_be(base, obj) : 0;
  char name[64] = {};
  if (str && ta::rdable2(base, str, sizeof(name) - 1))
    std::memcpy(name, reinterpret_cast<const void*>(ta::host_addr(base, str)), sizeof(name) - 1);
  for (char& c : name)
    if (c && (c < 32 || c > 126)) c = '?';
  // The Save row sends type 301 "Saving", which the dispatcher ignores (L3 of
  // 16:38Z: A on Save did nothing on the partner, so its full wheel had no way
  // to quit). It becomes a page choice of "HenchmanQuit" for this call.
  if (type == ck::kSaveRow && str && std::strcmp(name, "Saving") == 0 && REXCVAR_GET(coop_kit_redirect_save)) {
    REXSYS_INFO("[coopkit] pause page 'Saving' (Save row) chosen: henchman quit");
    ck::be32(base, ev + 4, ck::kPageChosen);
    ck::be32(base, obj, ck::kHenchmanQuitStr);
    __imp__sub_8240EAA0(ctx, base);
    ck::be32(base, obj, str);
    ck::be32(base, ev + 4, type);
    return;
  }
  // The wheel's Quit row (Kick added as "HenchmanQuit", sub_824083C0 below):
  // the stock henchman quit question opened from the full wheel takes no input
  // (rig 2026-10-27, and L4 of the Save redirect), so the partner leaves our
  // way: the wheel is closed (B, as for Start) and then QuitToFrontEnd.
  if (type == ck::kPageChosen && str && std::strcmp(name, "HenchmanQuit") == 0) {
    REXSYS_INFO("[coopkit] partner: Quit chosen on the wheel: closing it, then leaving");
    ck::g_close_from_ms = fable2::f5lua::steady_ms();
    ck::g_next_b_ms = 0;
    ck::g_b_left = ck::kStartBs;
    ck::g_leave_pending = true;
    return;
  }
  if (type != ck::kPageChosen || !str) {
    if (REXCVAR_GET(coop_kit_log_events))
      REXSYS_INFO("[coopkit] pause page event type {} name '{}'", type, name);
    else
      REXSYS_DEBUG("[coopkit] pause page event type {} name '{}'", type, name);
    return __imp__sub_8240EAA0(ctx, base);
  }
  const ck::PageRoute route = ck::RoutePage(
      name, {REXCVAR_GET(coop_kit_redirect_save), REXCVAR_GET(coop_kit_redirect_abilities),
             REXCVAR_GET(coop_kit_redirect_kick)});
  static const char* kRouteNames[] = {"as is", "henchman abilities", "henchman quit", "ignored"};
  REXSYS_INFO("[coopkit] pause page '{}' chosen: {}", name, kRouteNames[static_cast<int>(route)]);
  ck::g_weapons_page = std::strcmp(name, "Weapons") == 0;
  if (route == ck::PageRoute::kSwallow) return;
  if (route == ck::PageRoute::kPass) {
    ck::t_full_page_open = true;  // sub_82336D70 below: the stock pause, unless coop_kit_no_host_pause
    __imp__sub_8240EAA0(ctx, base);
    ck::t_full_page_open = false;
    return;
  }
  // Point the event's name object at the henchman string for this call, then put it back.
  ck::be32(base, obj, route == ck::PageRoute::kHenchmanQuit ? ck::kHenchmanQuitStr : ck::kHenchmanAbilitiesStr);
  __imp__sub_8240EAA0(ctx, base);
  ck::be32(base, obj, str);
}

// Wheel builder (r3 = pause menu, r4 = 1 full / 0 henchman). Launch 1: the
// partner's Start does not go through sub_82583340 (its caller
// sub_82846078 always passes 0), so the partner's choice is made here.
namespace fable2::coopkit {
inline thread_local bool t_joiner_wheel = false;  // the full wheel is being built for the joiner
}  // namespace fable2::coopkit

extern "C" void __imp__sub_8240F040(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8240F040(PPCContext& ctx, uint8_t* base) {
  const bool forced = (ctx.r4.u32 & 0xFF) == 0 && fable2::coopkit::joiner_active();
  if (forced) {
    if (fable2::coopkit::g_wheel_logged.fetch_add(1) < 20)
      REXSYS_INFO("[coopkit] pause menu: full wheel for player 2 (builder)");
    fable2::coopkit::menu_event(true, "full wheel built (sub_8240F040)");
    fable2::coopkit::g_wheel_open = true;
    ctx.r4.u64 = 1;
  }
  fable2::coopkit::t_joiner_wheel = forced;
  __imp__sub_8240F040(ctx, base);
  fable2::coopkit::t_joiner_wheel = false;
}

// Wheel AddEntry(r3 = menu, r4 = &name, r5 = &label, r6, r7, r8 = 0, r9 = 300,
// r10, stack +84), used by both builders: the host's sub_82842860 and the
// henchman's sub_828436A8. In the joiner's full wheel (above) the host-only
// rows are not the partner's: the Save row ("Saving": save and quit the host's
// game) and its rows (parent "Saving") are left out, and Kick becomes the
// henchman's own Quit row, name "HenchmanQuit" with the stock
// GUI_SCREEN_PAUSE_HENCHMAN_QUIT label, exactly what sub_828436A8 adds.
// Choosing it closes the wheel and leaves (sub_8240EAA0 above) (live session
// 2026-10-27: Calvin had Kick, which did nothing, and no way to leave).
extern "C" void __imp__sub_824083C0(PPCContext& ctx, uint8_t* base);
extern "C" void ConstructRefCounted_8222CF18(PPCContext& ctx, uint8_t* base);
extern "C" void DestructRefCounted_82214F08(PPCContext& ctx, uint8_t* base);
extern "C" void ReleaseGlobalRef_821C6868(PPCContext& ctx, uint8_t* base);
extern "C" void ProcessAndProcessAndProcess1726_82301D40(PPCContext& ctx, uint8_t* base);  // text lookup
extern "C" void sub_824083C0(PPCContext& ctx, uint8_t* base) {
  namespace ta = fable2::textappend;
  namespace ck = fable2::coopkit;
  if (!ck::t_joiner_wheel) return __imp__sub_824083C0(ctx, base);
  auto slot_text = [&](uint32_t slot, char (&out)[32]) {
    const uint32_t buf = slot && ta::rdable2(base, slot, 4) ? ta::load_be(base, slot) : 0;
    const uint32_t str = buf && ta::rdable2(base, buf, 4) ? ta::load_be(base, buf) : 0;
    if (str && ta::rdable2(base, str, sizeof(out) - 1))
      std::memcpy(out, reinterpret_cast<const void*>(ta::host_addr(base, str)), sizeof(out) - 1);
  };
  char name[32] = {}, parent[32] = {};
  slot_text(ctx.r4.u32, name);
  slot_text(ctx.r7.u32, parent);
  if (std::strcmp(name, "Saving") == 0 || std::strcmp(parent, "Saving") == 0) {
    REXSYS_INFO("[coopkit] partner wheel: Save row '{}' left out", name);
    return;
  }
  if (std::strcmp(name, "Kick") != 0) {
    REXSYS_DEBUG("[coopkit] partner wheel row '{}'", name);
    return __imp__sub_824083C0(ctx, base);
  }
  // Kick -> HenchmanQuit: a name, an id and a label in a scratch frame below the caller's stack.
  constexpr uint32_t kHenchmanQuitId = 0x820DD114;  // "GUI_SCREEN_PAUSE_HENCHMAN_QUIT" (image)
  const PPCContext in = ctx;
  const uint32_t sp = (in.r1.u32 - 0x200) & ~0xFu;
  for (uint32_t i = 0; i < 0x100; i += 4) ck::be32(base, sp + i, 0);
  ck::be32(base, sp, in.r1.u32);  // back chain
  const uint32_t new_name = sp + 0xA0, id = sp + 0xA4, label = sp + 0xA8;
  ctx.r1.u64 = sp;
  ctx.r3.u64 = new_name;
  ctx.r4.u64 = ck::kHenchmanQuitStr;
  ctx.r5.s64 = -1;
  ConstructRefCounted_8222CF18(ctx, base);
  ctx.r3.u64 = id;
  ctx.r4.u64 = kHenchmanQuitId;
  ctx.r5.s64 = -1;
  ConstructRefCounted_8222CF18(ctx, base);
  ctx.r3.u64 = label;
  ctx.r4.u64 = id;  // consumed by the lookup
  ProcessAndProcessAndProcess1726_82301D40(ctx, base);
  ctx = in;
  ctx.r1.u64 = sp;  // its frame below our slots; its stack argument copied
  ck::be32(base, sp + 84, ta::load_be(base, in.r1.u32 + 84));
  ctx.r4.u64 = new_name;
  ctx.r5.u64 = label;
  __imp__sub_824083C0(ctx, base);
  ctx.r3.u64 = label;
  ReleaseGlobalRef_821C6868(ctx, base);
  ctx.r3.u64 = new_name;
  DestructRefCounted_82214F08(ctx, base);
  ctx = in;
  REXSYS_INFO("[coopkit] partner wheel: Kick row is the henchman's Quit");
}

// r3 = player manager; returns 1 when multiplayer is active and the local
// player record is the henchman (+4 == 1).
extern "C" void __imp__sub_82336610(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82336610(PPCContext& ctx, uint8_t* base) {
  if (fable2::coopkit::t_in_pause_open) {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_82336610(ctx, base);
}

// 001up: the Experience binding thunk (sub_8251ED18: r3 = the binding, its function
// at +8; r4 = the entity; r5 = the type) looks the entity's Experience component up
// and calls the function (Get sub_8251C068, ...); without one it returns 0. The
// partner's copy of player 2 has none, so its Abilities page showed 0 XP. On the
// partner, a Get on player 2's copy is answered from the host's kit_state instead,
// with no engine call on the copy. Every other call costs one atomic load and a
// compare (the thunk serves every Lua experience call, in solo too).
extern "C" void __imp__sub_8251ED18(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8251ED18(PPCContext& ctx, uint8_t* base) {
  const uint32_t entity = ctx.r4.u32;
  const uint32_t p2 = fable2::coopcam::g_p2_entity.load(std::memory_order_relaxed);
  if (entity && entity == p2) {
    namespace ck = fable2::coopkit;
    namespace ta = fable2::textappend;
    const uint32_t bind = ctx.r3.u32, type = ctx.r5.u32;
    const uint32_t fn = ta::rdable2(base, bind + 8, 4) ? ta::load_be(base, bind + 8) : 0;
    if (fn == 0x8251C068 && type < 4 && ck::g_p2_xp_valid.load() && ck::joiner_active()) {
      ctx.r3.u64 = static_cast<uint32_t>(ck::g_p2_xp[type].load());
      return;
    }
  }
  __imp__sub_8251ED18(ctx, base);
}
