// fable_2 - the game's own co-op join screens (feature 001, item (b), Josh 2026-10-24:
// neither player may see them; skip them in code instead of covering them).
//
// One persistent controller runs them (built once by sub_82335218 -> sub_82475FF8,
// vtable 0x820B4DC0; launch 25 trace):
//   slot 2  sub_82476600  open: builds the layout (GUI_SCREEN_COOP_GOLD / EXPERIENCE /
//                         REJECT, the look screen sub_824771E0 -> sub_82478F48)
//   slot 4  sub_82477768  per-frame update (reaches the spawn: sub_82478388 -> sub_8231FA68)
//   slot 6  sub_82479BF0  per-frame update by phase (r5 = 1: sub_82479C28, r5 = 2:
//                         sub_82479F18, the "won't be saved" prompt), gated by +268 / +304
//   slot 7  sub_8247BA20  GUI event handler: r4 = event id (36, 37), r5 = phase
//   slot 9  sub_82476FC8  close
//
// Step 1 (this file now): a log-only probe (rig cvar coop_join_screen_probe) of the
// events, phases and state flags each real press produces on each PC, so the skip
// can post exactly those. Step 2: a campaign-only override that skips the layout and
// posts the events.

#pragma once

#include <atomic>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "fable2_f5_lua.h"

REXCVAR_DEFINE_BOOL(coop_join_screen_probe, false, "Network",
                    "Testing: log the game's co-op join screen controller (events, phases, flags)");

namespace fable2::coopjoinscreen {

inline uint8_t rd8(uint8_t* base, uint32_t a) { return base[a]; }
inline uint32_t rd32(uint8_t* base, uint32_t a) {
  return (uint32_t(base[a]) << 24) | (uint32_t(base[a + 1]) << 16) | (uint32_t(base[a + 2]) << 8) |
         base[a + 3];
}
inline bool on() { return REXCVAR_GET(coop_join_screen_probe); }

// The controller's flags the per-frame updates test, plus the state words around them.
inline void log_state(const char* where, PPCContext& ctx, uint8_t* base) {
  const uint32_t t = ctx.r3.u32;
  if (t < 0x10000) return;
  REXSYS_INFO("[joinscreen] {} this={:08X} r4={} r5={} +268={} +269={} +270={} +271={} +272={} "
              "+276={} +280={} +296={} +300={} +304={} +308={} +344={}",
              where, t, int32_t(ctx.r4.u32), int32_t(ctx.r5.u32), rd8(base, t + 268), rd8(base, t + 269),
              rd8(base, t + 270), rd8(base, t + 271), rd8(base, t + 272), rd32(base, t + 276),
              int32_t(rd32(base, t + 280)), rd8(base, t + 296), int32_t(rd32(base, t + 300)),
              rd8(base, t + 304), rd32(base, t + 308), rd32(base, t + 344));
}

// Per-frame slots log only when their phase argument changes.
inline std::atomic<int32_t> g_last_slot4_r4{-999}, g_last_slot4_r5{-999}, g_last_slot6_r5{-999};

// --- the skip: answer the screens through their own action readers ------------------
//
// The screens' per-frame updates read an action code from sub_82841730 (host phase:
// reader at controller+308, sub_82479C28; partner phase: reader at +348,
// sub_82479F18) and switch on it: 36 = accept / Yes (host: sub_8247BF00 +
// sub_8247AAB0 = accept the split; partner prompt: sub_8247C1B0 = continue without a
// save), 37 = back / No (static read of both switches, launch 27 map). With
// coop_join_screen_skip on, during a campaign, while the controller is open, a reader
// that returns no action returns 36 instead (at most every kSkipEveryMs). No pad
// press is involved, so nothing reaches the world after the screens close.
constexpr int64_t kSkipEveryMs = 400;
inline std::atomic<uint32_t> g_controller{0};  // set at open, cleared at close
inline std::atomic<int64_t> g_last_skip_ms{0};
inline std::atomic<int> g_skips{0};

}  // namespace fable2::coopjoinscreen

// coop_join_screen_skip is defined in fable2_coop_frontend.h (its pad answers stand down).

extern "C" void __imp__sub_82841730(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82841730(PPCContext& ctx, uint8_t* base) {
  using namespace fable2::coopjoinscreen;
  const uint32_t reader = ctx.r3.u32;
  __imp__sub_82841730(ctx, base);
  const uint32_t ctrl = g_controller.load();
  if (!ctrl || ctx.r3.u32 != 0 || !REXCVAR_GET(coop_join_screen_skip) || !REXCVAR_GET(coop_campaign))
    return;
  if (reader != ctrl + 308 && reader != ctrl + 348) return;
  const int64_t now = fable2::f5lua::steady_ms();
  if (now - g_last_skip_ms.load() < kSkipEveryMs) return;
  g_last_skip_ms.store(now);
  ctx.r3.u64 = 36;
  REXSYS_INFO("[joinscreen] skip: accept (36) to the {} reader ({})",
              reader == ctrl + 308 ? "host" : "partner", g_skips.fetch_add(1) + 1);
}

extern "C" void __imp__sub_82476600(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82476600(PPCContext& ctx, uint8_t* base) {
  if (fable2::coopjoinscreen::on()) fable2::coopjoinscreen::log_state("open", ctx, base);
  fable2::coopjoinscreen::g_controller.store(ctx.r3.u32);
  fable2::coopjoinscreen::g_skips.store(0);
  // Experiment (rig, off by default): with the in-code answers on, do not build the
  // screens' layout at all. Whether the state machine still reaches the spawn without
  // it is what launch 28's third join tests.
  if (REXCVAR_GET(coop_join_screen_nolayout) && REXCVAR_GET(coop_join_screen_skip) &&
      REXCVAR_GET(coop_campaign)) {
    REXSYS_INFO("[joinscreen] layout build skipped (experiment)");
    return;
  }
  __imp__sub_82476600(ctx, base);
}

extern "C" void __imp__sub_8247BA20(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8247BA20(PPCContext& ctx, uint8_t* base) {
  if (fable2::coopjoinscreen::on()) fable2::coopjoinscreen::log_state("event", ctx, base);
  __imp__sub_8247BA20(ctx, base);
}

extern "C" void __imp__sub_82477768(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82477768(PPCContext& ctx, uint8_t* base) {
  using namespace fable2::coopjoinscreen;
  if (on() && (g_last_slot4_r4.exchange(int32_t(ctx.r4.u32)) != int32_t(ctx.r4.u32) |
               g_last_slot4_r5.exchange(int32_t(ctx.r5.u32)) != int32_t(ctx.r5.u32)))
    log_state("update4", ctx, base);
  __imp__sub_82477768(ctx, base);
}

extern "C" void __imp__sub_82479BF0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82479BF0(PPCContext& ctx, uint8_t* base) {
  using namespace fable2::coopjoinscreen;
  if (on() && g_last_slot6_r5.exchange(int32_t(ctx.r5.u32)) != int32_t(ctx.r5.u32))
    log_state("update6", ctx, base);
  __imp__sub_82479BF0(ctx, base);
}

extern "C" void __imp__sub_82476FC8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82476FC8(PPCContext& ctx, uint8_t* base) {
  if (fable2::coopjoinscreen::on()) fable2::coopjoinscreen::log_state("close", ctx, base);
  fable2::coopjoinscreen::g_controller.store(0);
  __imp__sub_82476FC8(ctx, base);
}
