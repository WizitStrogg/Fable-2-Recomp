// fable_2 - 001eq trace (rig only, cvar coop_trace_xp): what the Abilities page reads
// and calls. Logs each distinct (function, caller, r3, r4) once, at most kMaxLines:
//   sub_8251C068  Experience.Get(component, type): the float at *(component + 16) + type * 4
//   sub_8251C0A8  the Experience.Modify core
//   sub_824AA728  Stats binding (SetHeroAbilityLevel by the binding table)
//   sub_824AA140  Stats binding (GetHeroAbilityLevel by the binding table)
// On the joiner it shows which object the partner's page reads for its XP figures;
// on the host, a purchase on the host's own page shows the learn path's calls.

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
#include <tuple>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "fable2_coop_kit.h"

REXCVAR_DEFINE_BOOL(coop_trace_xp, false, "Network",
                    "Testing: log the experience / ability calls (001eq trace), each distinct call once");

namespace fable2::eqtrace {
constexpr int kMaxLines = 120;
inline std::mutex g_mu;
inline std::set<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>> g_seen;
inline void log(const char* fn, uint32_t id, PPCContext& ctx, uint32_t r3, uint32_t r4, uint32_t r5, float f1,
                uint64_t ret) {
  {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_seen.size() >= kMaxLines || !g_seen.insert({id, static_cast<uint32_t>(ctx.lr), r3, r4}).second) return;
  }
  REXSYS_INFO("[eqtrace] {} r3=0x{:08X} r4={} r5={} f1={:.2f} lr=0x{:08X} -> 0x{:X}", fn, r3, int32_t(r4),
              int32_t(r5), f1, static_cast<uint32_t>(ctx.lr), ret);
}
}  // namespace fable2::eqtrace

#define FABLE2_EQTRACE_HOOK(addr, label, id)                                                         \
  extern "C" void __imp__sub_##addr(PPCContext& ctx, uint8_t* base);                                 \
  extern "C" void sub_##addr(PPCContext& ctx, uint8_t* base) {                                       \
    if (!REXCVAR_GET(coop_trace_xp)) {                                                               \
      __imp__sub_##addr(ctx, base);                                                                  \
      return;                                                                                        \
    }                                                                                                \
    const uint32_t r3 = ctx.r3.u32, r4 = ctx.r4.u32, r5 = ctx.r5.u32;                                \
    const float f1 = static_cast<float>(ctx.f1.f64);                                                 \
    const uint32_t lr = static_cast<uint32_t>(ctx.lr);                                               \
    __imp__sub_##addr(ctx, base);                                                                    \
    const uint64_t saved_lr = ctx.lr;                                                                \
    ctx.lr = lr;                                                                                     \
    fable2::eqtrace::log(label, id, ctx, r3, r4, r5, f1, ctx.r3.u64);                                \
    ctx.lr = saved_lr;                                                                               \
  }

FABLE2_EQTRACE_HOOK(8251C068, "Experience.Get", 1)
FABLE2_EQTRACE_HOOK(8251C0A8, "Experience.Modify core", 2)
FABLE2_EQTRACE_HOOK(824AA728, "Stats 0x824AA728 (SetHeroAbilityLevel?)", 3)
FABLE2_EQTRACE_HOOK(824AA140, "Stats 0x824AA140 (GetHeroAbilityLevel?)", 4)

// 001up: the Experience binding thunk (sub_8251ED18: r3 = the binding, its function
// at +8; r4 = the entity; r5 = the type) looks the entity's Experience component up
// and calls the function (Get sub_8251C068, ...); without one it returns 0. The
// partner's copy of player 2 has none, so its Abilities page showed 0 XP. On the
// partner, a Get on player 2's copy is answered from the host's kit_state instead,
// with no engine call on the copy. coop_trace_xp logs every distinct
// (entity, function) the thunk sees.
extern "C" void __imp__sub_8251ED18(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8251ED18(PPCContext& ctx, uint8_t* base) {
  namespace ck = fable2::coopkit;
  namespace ta = fable2::textappend;
  const uint32_t bind = ctx.r3.u32, entity = ctx.r4.u32, type = ctx.r5.u32;
  const uint32_t fn = ta::rdable2(base, bind + 8, 4) ? ta::load_be(base, bind + 8) : 0;
  const uint32_t p2 = fable2::coopcam::g_p2_entity.load();
  if (REXCVAR_GET(coop_trace_xp)) {
    static std::mutex mu;
    static std::set<std::pair<uint32_t, uint32_t>> seen;
    std::lock_guard<std::mutex> l(mu);
    if (seen.size() < 64 && seen.insert({entity, fn}).second)
      REXSYS_INFO("[eqtrace] Experience thunk: entity 0x{:08X}{} fn 0x{:08X} type {} lr 0x{:08X}", entity,
                  entity == p2 ? " (player 2's copy)" : "", fn, int32_t(type), static_cast<uint32_t>(ctx.lr));
  }
  if (entity && entity == p2 && ck::g_p2_xp_valid.load() && ck::joiner_active() && type < 4 && fn == 0x8251C068) {
    ctx.r3.u64 = static_cast<uint32_t>(ck::g_p2_xp[type].load());
    return;
  }
  __imp__sub_8251ED18(ctx, base);
}
