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
