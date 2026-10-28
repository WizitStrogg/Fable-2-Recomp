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

#include <algorithm>
#include <atomic>
#include <mutex>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/kernel/xam/lan_lobby.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "fable2_coop_frontend.h"
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
// sub_8247AAB0 = accept the split; partner look screen: 0x8247A118, which queues the
// message boxes, see sub_8247A6A8 below), 37 = back / No (JOIN-SCREEN-SEQUENCE.txt). With
// coop_join_screen_skip on, during a campaign, while the controller is open, a reader
// that returns no action returns 36 instead (at most kMaxPerReader times). No pad
// press is involved, so nothing reaches the world after the screens close.
inline std::atomic<uint32_t> g_controller{0};  // set at open, cleared at close
inline std::atomic<int64_t> g_open_ms{0};
inline std::atomic<int> g_skips{0};

// Fallback (coordinator, launch 28 hung): with the skip on, if the screens get no
// in-code accept within kNoAcceptMs, or are still open after kStuckMs, log the
// reader's raw samples, turn the skip off at runtime, and let the pad answers
// (fable2_coop_frontend.h accept_tick) finish the join.
constexpr int64_t kNoAcceptMs = 15000, kStuckMs = 25000;
// Every reader call while the controller is open, any address (launch 28 rerun: the
// partner's later screen did not use ctrl+348), kept as distinct (reader, caller,
// input, returned) combinations with a count.
struct Sample {
  uint32_t reader, lr;
  int32_t input, got;
  int count;
};
inline std::mutex g_samples_mu;
inline Sample g_samples[32];
inline int g_sample_n = 0;
inline void record(uint32_t reader, uint32_t lr, int32_t input, int32_t got) {
  std::lock_guard<std::mutex> l(g_samples_mu);
  for (int i = 0; i < g_sample_n; ++i) {
    Sample& s = g_samples[i];
    if (s.reader == reader && s.lr == lr && s.input == input && s.got == got) {
      ++s.count;
      return;
    }
  }
  if (g_sample_n < 32) g_samples[g_sample_n++] = {reader, lr, input, got, 1};
}
// At most kMaxPerReader accepts per reader, kSkipGapMs apart (launch 28 rerun: 62
// accepts on the host reader may themselves have upset the screens).
constexpr int kMaxPerReader = 2;
constexpr int64_t kSkipGapMs = 1500;
struct Injected {
  uint32_t reader;
  int count;
  int64_t last_ms;
};
inline Injected g_injected[8];
inline int g_injected_n = 0;
inline void check_fallback() {
  const uint32_t ctrl = g_controller.load();
  if (!ctrl || !REXCVAR_GET(coop_join_screen_skip) || !REXCVAR_GET(coop_campaign)) return;
  const int64_t open = g_open_ms.load();
  const int64_t now = fable2::f5lua::steady_ms();
  const int skips = g_skips.load();
  if (!open || !((skips == 0 && now - open > kNoAcceptMs) || now - open > kStuckMs)) return;
  REXSYS_WARN("[joinscreen] FALLBACK after {} ms with {} in-code accept(s): skip off, pad answers take over",
              now - open, skips);
  {
    std::lock_guard<std::mutex> l(g_samples_mu);
    for (int i = 0; i < g_sample_n; ++i) {
      const Sample& s = g_samples[i];
      REXSYS_INFO("[joinscreen] sample reader={:08X} (ctrl{:+}) caller={:08X} input={} returned={} x{}", s.reader,
                  int32_t(s.reader - ctrl), s.lr, s.input, s.got, s.count);
    }
  }
  rex::cvar::SetFlagByName("coop_join_screen_skip", "false");
}

// --- the partner's answer while the screens are open (001jn) --------------------------
//
// Round two (2026-10-28, a later-story Oakfield save): the partner's timed A presses
// (frontend accept_tick, from the join on) were all spent before its game had loaded
// the host's level and opened these screens, so the screens sat under the partner's
// "Joining..." cover until Calvin pressed A by chance (the host waited 347 s). Now the
// partner answers the screens while they are actually open: a real pad A (the same path
// as a press) every kScreenAEveryMs from kScreenFirstAMs after the open, at most
// kScreenMaxA times; the partner's look screen and its "won't be saved" box both take
// A. If they are still open kScreenCoverMs after the open, the cover comes down so the
// player can see and answer them. Off with coop_join_screens_auto (rig stepping) or
// when coop_join_screen_skip answers them in code.
constexpr int64_t kScreenFirstAMs = 1500, kScreenAEveryMs = 2000, kScreenCoverMs = 10000;
constexpr int kScreenMaxA = 10;
inline int64_t g_screen_seen_open = 0, g_screen_last_a_ms = 0;
inline int g_screen_a = 0;
inline bool g_screen_cover_dropped = false;
inline void partner_screen_tick() {
  const int64_t open = g_open_ms.load(std::memory_order_relaxed);  // solo: 0, nothing else runs
  if (!open || !g_controller.load()) return;
  if (!REXCVAR_GET(coop_campaign) || !REXCVAR_GET(coop_join_screens_auto) || REXCVAR_GET(coop_join_screen_skip))
    return;
  if (rex::kernel::xam::lobby::GetLobbyStatus().joined_host_ip == 0) return;  // the host keeps its timed answers
  if (open != g_screen_seen_open) {
    g_screen_seen_open = open;
    g_screen_a = 0;
    g_screen_last_a_ms = 0;
    g_screen_cover_dropped = false;
    REXSYS_INFO("[coop] partner: the game's join screen is open");
  }
  if (fable2::coopfrontend::g_cover_hide_at_ms.load()) return;  // player 2 spawned: A would act in the world
  const int64_t now = fable2::f5lua::steady_ms();
  if (g_screen_a < kScreenMaxA && now - open >= kScreenFirstAMs && now - g_screen_last_a_ms >= kScreenAEveryMs) {
    g_screen_last_a_ms = now;
    ++g_screen_a;
    fable2::cooplobby::InjectPress(static_cast<uint16_t>(fable2::cooplobby::kA), 0, 200);
    REXSYS_INFO("[coop] partner: auto-accepted stock join screen (A {} of {}, open {} ms)", g_screen_a, kScreenMaxA,
                now - open);
  }
  if (!g_screen_cover_dropped && now - open > kScreenCoverMs) {
    g_screen_cover_dropped = true;
    fable2::coopfrontend::HideCover("the game's join screen is still open after 10 s: shown so it can be answered");
  }
}

// The fallback also runs from its own frame hook: launch 28 rerun showed the
// partner's screens can leave the per-frame update the check used to hang off.
inline void (*g_prev_frame_hook)() = nullptr;
inline void OnFrame() {
  if (g_prev_frame_hook) g_prev_frame_hook();
  check_fallback();
  partner_screen_tick();
}
inline const bool g_installed = [] {
  g_prev_frame_hook = fable2::f5lua::g_frame_hook;
  fable2::f5lua::g_frame_hook = &OnFrame;
  return true;
}();

}  // namespace fable2::coopjoinscreen

// coop_join_screen_skip is defined in fable2_coop_frontend.h (its pad answers stand down).

extern "C" void __imp__sub_82841730(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82841730(PPCContext& ctx, uint8_t* base) {
  using namespace fable2::coopjoinscreen;
  const uint32_t reader = ctx.r3.u32;
  const int32_t input = int32_t(ctx.r4.u32);  // the screen's input code this frame
  __imp__sub_82841730(ctx, base);
  const uint32_t ctrl = g_controller.load();
  if (!ctrl || !REXCVAR_GET(coop_join_screen_skip) || !REXCVAR_GET(coop_campaign)) return;
  const int32_t got = int32_t(ctx.r3.u32);
  record(reader, uint32_t(ctx.lr), input, got);
  if (reader != ctrl + 308 && reader != ctrl + 348) return;
  // No action this frame: the reader hands its input code straight back (24 every
  // idle frame, launch 28 rerun; 0 in some branches). Real actions are 28..38.
  if ((got != 0 && got != input) || (input >= 28 && input <= 38)) return;
  const int64_t now = fable2::f5lua::steady_ms();
  Injected* inj = nullptr;
  {
    std::lock_guard<std::mutex> l(g_samples_mu);
    for (int i = 0; i < g_injected_n; ++i)
      if (g_injected[i].reader == reader) inj = &g_injected[i];
    if (!inj && g_injected_n < 8) inj = &(g_injected[g_injected_n++] = {reader, 0, 0});
    if (!inj || inj->count >= kMaxPerReader || now - inj->last_ms < kSkipGapMs) return;
    ++inj->count;
    inj->last_ms = now;
  }
  ctx.r3.u64 = 36;
  REXSYS_INFO("[joinscreen] skip: accept (36) to the {} reader ({} of {} there)",
              reader == ctrl + 308 ? "host" : "partner", inj->count, kMaxPerReader);
  g_skips.fetch_add(1);
}

extern "C" void __imp__sub_82476600(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82476600(PPCContext& ctx, uint8_t* base) {
  if (fable2::coopjoinscreen::on()) fable2::coopjoinscreen::log_state("open", ctx, base);
  fable2::coopjoinscreen::g_controller.store(ctx.r3.u32);
  fable2::coopjoinscreen::g_skips.store(0);
  fable2::coopjoinscreen::g_open_ms.store(fable2::f5lua::steady_ms());
  {
    std::lock_guard<std::mutex> l(fable2::coopjoinscreen::g_samples_mu);
    fable2::coopjoinscreen::g_sample_n = 0;
    fable2::coopjoinscreen::g_injected_n = 0;
  }
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
  check_fallback();  // runs every frame the screens are open, even if no reader is
  __imp__sub_82479BF0(ctx, base);
}

// The partner's look-screen accept does not finish the join: it queues message boxes
// at ctrl+480 (39 GUI_COOP_PROGRESS_WONT_BE_SAVED; 41 achievements, 43 profile space
// when they apply) and runs this queue, which opens them one by one in the front end's
// generic message box. The box reads its own input, not ctrl+348, which is why launch 28
// stuck there (JOIN-SCREEN-SEQUENCE.txt P2). With the skip on, no box is opened: each
// queued box gets what the game's answer handler sub_8247BA20 does for "yes" (id 39:
// clear the stored hero save = continue without a save), then the queue is cleared and
// the empty-queue step sub_8247B270 (the final partner accept) runs as the game's own.
extern "C" void CopyAssign_RefCounted2_82265160(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8247D3B8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8247B270(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_8247A6A8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8247A6A8(PPCContext& ctx, uint8_t* base) {
  using namespace fable2::coopjoinscreen;
  const uint32_t ctrl = ctx.r3.u32;
  if (ctrl != g_controller.load() || !REXCVAR_GET(coop_join_screen_skip) || !REXCVAR_GET(coop_campaign) ||
      rd32(base, ctrl + 488) == 0) {
    __imp__sub_8247A6A8(ctx, base);
    return;
  }
  const PPCContext saved = ctx;
  const uint32_t sentinel = rd32(base, ctrl + 484);
  int n = 0;
  for (uint32_t node = rd32(base, sentinel); node != sentinel && n < 8; node = rd32(base, node), ++n) {
    const int32_t id = int32_t(rd32(base, node + 8));
    REXSYS_INFO("[joinscreen] skip: message box {} answered yes in code", id);
    if (id != 39) continue;
    // sub_8247BA20, box 39: assign "" to the hero-save string at net+4 (sub_82265160) and net+2024 = -1.
    const uint32_t net = rd32(base, rd32(base, rd32(base, 0x83496920) + 12) + 148);
    ctx.r3.u64 = net + 4;
    ctx.r4.u64 = 0x834A5B6C;
    CopyAssign_RefCounted2_82265160(ctx, base);
    for (int i = 0; i < 4; ++i) base[net + 2024 + i] = 0xFF;
  }
  ctx.r3.u64 = ctrl + 480;
  sub_8247D3B8(ctx, base);
  ctx.r3.u64 = ctrl;
  sub_8247B270(ctx, base);
  ctx = saved;
  g_skips.fetch_add(1);
}

extern "C" void __imp__sub_82476FC8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82476FC8(PPCContext& ctx, uint8_t* base) {
  if (fable2::coopjoinscreen::on()) fable2::coopjoinscreen::log_state("close", ctx, base);
  fable2::coopjoinscreen::g_controller.store(0);
  fable2::coopjoinscreen::g_open_ms.store(0);
  __imp__sub_82476FC8(ctx, base);
}
