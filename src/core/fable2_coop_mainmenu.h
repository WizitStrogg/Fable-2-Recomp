// fable_2 - a native "Co-op" entry on the main menu (Josh, 2026-10-24: "Below
// the Continue bar, please add another option labeled Co-op ... the same
// template as the other options ... the animations and everything should also
// match"; co-op lives only behind this entry).
//
// The front end builds the main menu as a tree of entries (sub_826CA6E0), one
// AddEntry call each (sub_826CB808):
//   AddEntry(menu, &name, &label, &icon, &parent, 0, 2, *(menu+20)), stack arg 0
// in this order: "New Game", "Load Game" (= Continue, only with saves),
// "Downloadable Content", "Language", "Subtitles" (+ its On/Off children). The
// label is the text system's lookup of an id (sub_82301D40); an id the text bank
// does not have is shown as itself, so the id "Co-op" reads "Co-op".
//
// With coop_campaign on, one more AddEntry call adds "Co-op" (parent "Root")
// right after "Load Game" (after "New Game" when there are no saves), so the
// game lays it out, highlights and animates it like the others and the rows
// below move down. Choosing an entry calls the menu's handler (sub_826C81A0)
// with an event {+0 name, +4 type}; it compares the name with the stock entries.
// For "Co-op" the handler is not run: the co-op hub opens instead
// (fable2::coopfrontend::OnCoopEntry). The host's Start later loads the
// campaign save through the same handler with "Load Game" (RequestLoadGame),
// the game's own Continue, on the game thread.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "fable2_coop_frontend.h"  // also declares fable2::textappend::guest_alloc (fable2_f5_lua.h)

extern "C" void __imp__ProcessAndProcessAndProcess2440_826CB808(PPCContext& ctx, uint8_t* base);  // AddEntry
extern "C" void __imp__ProcessAndProcessAndProcess2446_826C81A0(PPCContext& ctx, uint8_t* base);  // OnSelect
extern "C" void ProcessAndProcessAndProcess1726_82301D40(PPCContext& ctx, uint8_t* base);  // text lookup
extern "C" void ConstructRefCounted_8222CF18(PPCContext& ctx, uint8_t* base);  // name string from char*
extern "C" void DestructRefCounted_82214F08(PPCContext& ctx, uint8_t* base);
extern "C" void ReleaseGlobalRef_821C6868(PPCContext& ctx, uint8_t* base);  // text release

namespace fable2::coopmainmenu {

constexpr uint32_t kRoot = 0x820A5998;      // "Root"
constexpr uint32_t kLoadGame = 0x820CD384;  // "Load Game"
constexpr uint32_t kNewGameIcon = 0x820CD3E8;
constexpr const char* kCoop = "Co-op";

inline uint32_t rd32(uint8_t* base, uint32_t a) {
  return (uint32_t(base[a]) << 24) | (uint32_t(base[a + 1]) << 16) | (uint32_t(base[a + 2]) << 8) | base[a + 3];
}
inline void wr32(uint8_t* base, uint32_t a, uint32_t v) {
  base[a] = uint8_t(v >> 24);
  base[a + 1] = uint8_t(v >> 16);
  base[a + 2] = uint8_t(v >> 8);
  base[a + 3] = uint8_t(v);
}
inline std::string cstr(uint8_t* base, uint32_t a) {
  std::string s;
  if (a < 0x10000) return s;
  for (int i = 0; i < 64 && base[a + i]; ++i) s.push_back(char(base[a + i]));
  return s;
}
// A name slot holds a pointer to an interned string whose +0 is its char*.
inline std::string slot_str(uint8_t* base, uint32_t slot) {
  const uint32_t buf = rd32(base, slot);
  return buf ? cstr(base, rd32(base, buf)) : std::string();
}

inline uint32_t g_coop_cstr = 0;           // "Co-op" in guest memory (the game's allocator)
inline std::atomic<uint32_t> g_menu{0};    // the main menu (from its last build / selection)
inline uint32_t g_pending_menu = 0;        // "New Game" added, "Co-op" not yet
inline std::atomic<uint32_t> g_select_type{0};  // the event type of a selection

inline bool enabled() { return REXCVAR_GET(coop_campaign); }

// One more AddEntry with the same constant arguments the game passes, on a
// scratch frame below the caller's stack. `in` = the registers of the game's
// own AddEntry call.
inline void add_coop_entry(PPCContext& ctx, uint8_t* base, const PPCContext& in) {
  if (!g_coop_cstr) {
    g_coop_cstr = fable2::textappend::guest_alloc(ctx, base, 8);
    std::memcpy(base + g_coop_cstr, kCoop, std::strlen(kCoop) + 1);
  }
  const PPCContext saved = ctx;
  const uint32_t sp = (in.r1.u32 - 0x200) & ~0xFu;
  for (uint32_t i = 0; i < 0x100; i += 4) wr32(base, sp + i, 0);
  wr32(base, sp, in.r1.u32);  // back chain
  const uint32_t name = sp + 0xA0, label = sp + 0xA4, icon = sp + 0xA8, parent = sp + 0xAC, id = sp + 0xB0;
  ctx.r1.u64 = sp;
  auto make = [&](uint32_t slot, uint32_t text) {
    ctx.r3.u64 = slot;
    ctx.r4.u64 = text;
    ctx.r5.s64 = -1;
    ConstructRefCounted_8222CF18(ctx, base);
  };
  make(name, g_coop_cstr);
  make(icon, kNewGameIcon);
  make(parent, kRoot);
  make(id, g_coop_cstr);
  ctx.r3.u64 = label;
  ctx.r4.u64 = id;  // consumed by the lookup
  ProcessAndProcessAndProcess1726_82301D40(ctx, base);
  wr32(base, sp + 84, 0);
  ctx.r3.u64 = in.r3.u64;
  ctx.r4.u64 = name;
  ctx.r5.u64 = label;
  ctx.r6.u64 = icon;
  ctx.r7.u64 = parent;
  ctx.r8.u64 = 0;
  ctx.r9.u64 = 2;
  ctx.r10.u64 = in.r10.u64;
  __imp__ProcessAndProcessAndProcess2440_826CB808(ctx, base);
  ctx.r3.u64 = label;
  ReleaseGlobalRef_821C6868(ctx, base);
  for (uint32_t s : {name, icon, parent}) {
    ctx.r3.u64 = s;
    DestructRefCounted_82214F08(ctx, base);
  }
  ctx = saved;
  REXSYS_INFO("[coop] main menu: Co-op entry added");
}

// Calls the menu's own handler as if "Load Game" (Continue) was chosen. Game
// thread only (from the press dispatch). Returns false if the menu is not known.
inline bool RequestLoadGame(PPCContext& ctx, uint8_t* base) {
  const uint32_t menu = g_menu.load();
  if (!menu) return false;
  const PPCContext saved = ctx;
  const uint32_t sp = (ctx.r1.u32 - 0x200) & ~0xFu;
  for (uint32_t i = 0; i < 0x100; i += 4) wr32(base, sp + i, 0);
  wr32(base, sp, ctx.r1.u32);
  const uint32_t slot = sp + 0xA0, event = sp + 0xC0;
  ctx.r1.u64 = sp;
  ctx.r3.u64 = slot;
  ctx.r4.u64 = kLoadGame;
  ctx.r5.s64 = -1;
  ConstructRefCounted_8222CF18(ctx, base);
  wr32(base, event, rd32(base, slot));
  wr32(base, event + 4, g_select_type.load());
  ctx.r3.u64 = menu;
  ctx.r4.u64 = event;
  __imp__ProcessAndProcessAndProcess2446_826C81A0(ctx, base);
  ctx.r3.u64 = slot;
  DestructRefCounted_82214F08(ctx, base);
  ctx = saved;
  REXSYS_INFO("[coop] main menu: Load Game (the game's Continue) for the campaign save");
  return true;
}

inline const bool g_installed = [] {
  fable2::coopfrontend::g_request_load = &RequestLoadGame;
  return true;
}();

}  // namespace fable2::coopmainmenu

extern "C" void ProcessAndProcessAndProcess2440_826CB808(PPCContext& ctx, uint8_t* base) {
  namespace mm = fable2::coopmainmenu;
  if (!mm::enabled()) {
    __imp__ProcessAndProcessAndProcess2440_826CB808(ctx, base);
    return;
  }
  const PPCContext in = ctx;
  const std::string name = mm::slot_str(base, in.r4.u32);
  const bool root = mm::slot_str(base, in.r7.u32) == "Root";
  // No saves (no "Load Game"): Co-op goes in before the next root entry.
  if (root && mm::g_pending_menu == in.r3.u32 && name != "Load Game" && name != mm::kCoop) {
    mm::g_pending_menu = 0;
    mm::add_coop_entry(ctx, base, in);
  }
  __imp__ProcessAndProcessAndProcess2440_826CB808(ctx, base);
  if (!root) return;
  if (name == "New Game") {
    mm::g_pending_menu = in.r3.u32;  // a (re)build of the menu starts
    mm::g_menu.store(in.r3.u32);
  } else if (name == "Load Game" && mm::g_pending_menu == in.r3.u32) {
    mm::g_pending_menu = 0;
    mm::add_coop_entry(ctx, base, in);
  }
}

extern "C" void ProcessAndProcessAndProcess2446_826C81A0(PPCContext& ctx, uint8_t* base) {
  namespace mm = fable2::coopmainmenu;
  const uint32_t ev = ctx.r4.u32;
  const uint32_t type = ev ? mm::rd32(base, ev + 4) : 0;
  if (mm::enabled() && ev && type != 302 && type != 141 && type != 140) {
    const uint32_t buf = mm::rd32(base, ev);
    if (buf && mm::cstr(base, mm::rd32(base, buf)) == mm::kCoop) {
      mm::g_menu.store(ctx.r3.u32);
      mm::g_select_type.store(type);
      REXSYS_INFO("[coop] main menu: Co-op chosen");
      fable2::coopfrontend::OnCoopEntry();
      return;
    }
  }
  if (ev && type != 302 && type != 141 && type != 140) mm::g_select_type.store(type);
  __imp__ProcessAndProcessAndProcess2446_826C81A0(ctx, base);
}
