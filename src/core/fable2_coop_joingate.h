// fable_2 - co-op join gate (feature 001, starter save inside QC060).
//
// The host's network message handler sub_8230DFF0 (r3 = the multiplayer
// manager, r5 = message, u16 type at +0, cases 275..289) turns a joining
// partner away unless several byte flags are set (static map, research.md):
//   global 0x834968C5, manager +52 / +53, +54, +226 (written by the Lua call
//   AllowHenchman, which the gameflow only makes at QC070).
// In QC060 "A New Beginning" every join was refused (launches 7, 12) even after
// AllowHenchman(true), so another gate is closed there as well.
//
// This module logs those flags per message (coop_join_gate_log) and, for the
// rig, can force chosen ones open while a co-op campaign is on
// (coop_join_gate_force = "226,52,53,54,g"). With coop_campaign off nothing is
// touched; with both cvars at their defaults it only forwards the call.

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_BOOL(coop_join_gate_log, false, "Network",
                    "Testing: log the co-op join gate flags on each host network message");
REXCVAR_DEFINE_STRING(coop_join_gate_force, "", "Network",
                      "Testing: during a co-op campaign, force these join gates open: "
                      "comma list of 226, 52, 53, 54 (manager bytes) and g (global)");

namespace fable2::coopjoingate {

constexpr uint32_t kGlobalGate = 0x834968C5;

inline uint8_t rd8(uint8_t* base, uint32_t a) { return base[a]; }
inline void wr8(uint8_t* base, uint32_t a, uint8_t v) { base[a] = v; }

inline bool listed(const std::string& list, const char* item) {
  const std::string want = std::string(",") + item + ",";
  return ("," + list + ",").find(want) != std::string::npos;
}

inline std::atomic<int> g_logged{0};

inline void before(PPCContext& ctx, uint8_t* base) {
  const uint32_t mgr = ctx.r3.u32, msg = ctx.r5.u32;
  if (!mgr || !msg) return;
  const uint16_t type = static_cast<uint16_t>((base[msg] << 8) | base[msg + 1]);
  if (REXCVAR_GET(coop_campaign)) {
    const std::string force = REXCVAR_GET(coop_join_gate_force);
    if (!force.empty()) {
      if (listed(force, "226")) wr8(base, mgr + 226, 1);
      if (listed(force, "52")) wr8(base, mgr + 52, 1);
      if (listed(force, "53")) wr8(base, mgr + 53, 1);
      if (listed(force, "54")) wr8(base, mgr + 54, 0);  // +54 set = refuse in case 1
      if (listed(force, "g")) wr8(base, kGlobalGate, 1);
    }
  }
  if (REXCVAR_GET(coop_join_gate_log) && type >= 275 && type <= 289 && g_logged.fetch_add(1) < 40) {
    REXSYS_INFO("[joingate] msg {} (case {}): g={} +52={} +53={} +54={} +226={} mgr={:08X}", type,
                type - 275, rd8(base, kGlobalGate), rd8(base, mgr + 52), rd8(base, mgr + 53),
                rd8(base, mgr + 54), rd8(base, mgr + 226), mgr);
  }
}

}  // namespace fable2::coopjoingate

extern "C" void __imp__sub_8230DFF0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8230DFF0(PPCContext& ctx, uint8_t* base) {
  fable2::coopjoingate::before(ctx, base);
  __imp__sub_8230DFF0(ctx, base);
}
