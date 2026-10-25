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
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
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
REXCVAR_DEFINE_BOOL(coop_kit_send_equip, true, "Network",
                    "Co-op kit (partner): equip on the Weapons page is sent to the host");
REXCVAR_DEFINE_BOOL(coop_kit_redirect_save, true, "Network",
                    "Co-op kit (partner): the wheel's Save row opens the henchman quit instead");
REXCVAR_DEFINE_BOOL(coop_kit_redirect_abilities, true, "Network",
                    "Co-op kit (partner): the wheel's Abilities row opens the henchman abilities");
REXCVAR_DEFINE_BOOL(coop_kit_redirect_kick, true, "Network",
                    "Co-op kit (partner): the wheel's Kick row does nothing");
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
  uint32_t rev = 0;
  int64_t next_snapshot_ms = 0;
  bool snapshot_queued = false;
  int64_t snapshot_queued_ms = 0;
  std::string last_state;
  int64_t last_tick_ms = 0, paused_since_ms = 0;
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
  chunk += "local res = 'no:busy'\n";
  chunk += "if hench and hench:IsAlive() then res = CoopKit.Apply(hench, '" + std::string(OpName(c.op)) + "', '" +
           c.rec + "') end\n";
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
constexpr int64_t kStalledAfterMs = 1500;
inline bool lua_stalled() {
  return fable2::f5lua::queued_count() > 0 &&
         fable2::f5lua::steady_ms() - fable2::f5lua::g_last_run_ms > kStalledAfterMs;
}

inline void host_tick(const lobby::LobbyStatus& s, int64_t now) {
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
  const auto cmds = lobby::GetPeerData("kit_cmd");
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
  h.applier.Done(h.running->seq, res == "no:pending" ? "no:busy" : res);
  h.running.reset();
  h.next_snapshot_ms = 0;  // a fresh kit_state with the result
}

// Host: the snapshot line. Publishes kit_state and the ack that goes with it.
inline void OnStateLine(std::string_view line) {
  std::lock_guard<std::mutex> l(mtx());
  auto& h = host();
  h.snapshot_queued = false;
  const auto k = ParseKitState(line);
  if (!k || k->rev == 0) return;  // rev 0 = a console `luafile coop_kit`, not the channel's
  const std::string value(line);
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
}

inline void joiner_tick(int64_t /*now*/) {
  auto& j = joiner();
  const std::string pub = j.queue.Published();
  if (pub != j.published) {
    j.published = pub;
    lobby::PublishData("kit_cmd", pub);
  }
  for (const auto& d : lobby::GetPeerData("kit_ack"))
    if (auto a = ParseAck(d.value))
      for (const auto& r : j.queue.OnAck(*a)) REXSYS_INFO("[coopkit] command {}: {} ({})", r.seq, r.res, ReasonText(r.res));
  for (const auto& d : lobby::GetPeerData("kit_state")) {
    auto k = ParseKitState(d.value);
    if (!k || k->rev == 0 || (j.state && k->rev <= j.state->rev)) continue;
    j.state = *k;
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
  REXSYS_INFO("[coopkit] partner: command {} {} {}", seq, OpName(op), IdToHex(id));
}

inline int64_t g_next_tick_ms = 0;
inline void (*g_prev_frame_hook)() = nullptr;
inline void OnFrame() {
  if (g_prev_frame_hook) g_prev_frame_hook();
  if (!REXCVAR_GET(coop_kit)) return;
  const int64_t now = now_ms();
  if (now < g_next_tick_ms) return;
  g_next_tick_ms = now + kTickMs;
  const lobby::LobbyStatus s = lobby::GetLobbyStatus();
  if (!s.networking_enabled) return;
  std::lock_guard<std::mutex> l(mtx());
  if (host_active(s)) host_tick(s, now);
  else if (joiner_active()) joiner_tick(now);
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
// The leaf call on the joiner: send instead of acting. True = skipped. With
// the action's switch off the stock local change runs (launch 1 behaviour).
inline bool screen_action(PPCContext& ctx, const char* action, bool enabled) {
  if (!t_screen_item || !enabled) return false;
  const uint32_t id = t_screen_item;
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
extern "C" void __imp__sub_825A6020(PPCContext& ctx, uint8_t* base);
extern "C" void sub_825A6020(PPCContext& ctx, uint8_t* base) {
  if (fable2::coopkit::screen_action(ctx, "equip", REXCVAR_GET(coop_kit_send_equip))) return;
  __imp__sub_825A6020(ctx, base);
}
extern "C" void __imp__sub_825A60F8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_825A60F8(PPCContext& ctx, uint8_t* base) {
  if (fable2::coopkit::screen_action(ctx, "use", REXCVAR_GET(coop_kit_send_use))) return;
  __imp__sub_825A60F8(ctx, base);
}
extern "C" void __imp__sub_825A71A0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_825A71A0(PPCContext& ctx, uint8_t* base) {
  if (fable2::coopkit::screen_action(ctx, "wear", REXCVAR_GET(coop_kit_send_wear))) return;
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
  __imp__sub_82336D70(ctx, base);
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
  if (type != ck::kPageChosen || !str) {
    REXSYS_DEBUG("[coopkit] pause page event type {} name '{}'", type, name);
    return __imp__sub_8240EAA0(ctx, base);
  }
  const ck::PageRoute route = ck::RoutePage(
      name, {REXCVAR_GET(coop_kit_redirect_save), REXCVAR_GET(coop_kit_redirect_abilities),
             REXCVAR_GET(coop_kit_redirect_kick)});
  static const char* kRouteNames[] = {"as is", "henchman abilities", "henchman quit", "ignored"};
  REXSYS_INFO("[coopkit] pause page '{}' chosen: {}", name, kRouteNames[static_cast<int>(route)]);
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
extern "C" void __imp__sub_8240F040(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8240F040(PPCContext& ctx, uint8_t* base) {
  if ((ctx.r4.u32 & 0xFF) == 0 && fable2::coopkit::joiner_active()) {
    if (fable2::coopkit::g_wheel_logged.fetch_add(1) < 20)
      REXSYS_INFO("[coopkit] pause menu: full wheel for player 2 (builder)");
    ctx.r4.u64 = 1;
  }
  __imp__sub_8240F040(ctx, base);
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
