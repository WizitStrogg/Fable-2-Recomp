// fable_2 - co-op campaign front end (specs/001-coop-campaign, T018).
//
// With coop_campaign on, A on the main menu's New Game opens a small
// "Solo / Co-op" chooser (the lobby panel) instead of starting the game:
//   Solo  -> the chooser closes and a fresh A press is injected; that press
//            passes through to the stock New Game (the boy/girl hero cards).
//   Co-op -> the host lobby opens (fable2_coop_lobby.h).
//   Back  -> the main menu, unchanged.
// With coop_campaign off nothing is intercepted.
//
// Hook point: FrontMenu_PressBitDispatch (0x82188F20), the main menu's raw
// press dispatch (proof P1; plans/main-menu-mod-item.md). r5 is the pressed
// mask (Up 1, Down 2, Back 32, A 4096), and each physical press arrives about
// twice within 400 ms. The stock menu opens with New Game selected and clamps
// the cursor to rows 0..4, so a mirror of Up/Down presses gives the row.

#pragma once

#include <atomic>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "fable2_coop_campaign.h"
#include "fable2_coop_lobby.h"
#include "fable2_front_hooks.h"
#include "../diagnostics/fable2_state_probe.h"

namespace fable2::coopfrontend {

constexpr uint32_t kUp = 1, kDown = 2, kBack = 32, kA = 0x1000, kB = 0x2000;
constexpr int kNewGameRow = 0, kLastRow = 4;
constexpr int64_t kDedupMs = 400;

inline int g_row = 0;               // mirrored main-menu cursor
inline bool g_armed = true;         // false once the stock New Game is running
inline int g_pass_a = 0;            // injected A presses to let through
inline int64_t g_last_press_ms = 0;
inline bool g_past_title = false;   // the title's "Press A" was passed this visit
inline uint32_t g_last_mask = 0;

// The front end: title prompt, menu movie or main menu. The state probe
// reports the title as PressAScreen or already as MainMenu, so both count.
inline bool in_main_menu() {
  const int s = fable2::stateprobe::CurrentState();
  return s == fable2::stateprobe::kMainMenu || s == fable2::stateprobe::kMainMenuMovie ||
         s == fable2::stateprobe::kPressAScreen;
}

inline void open_chooser();

// Solo: let the next A reach the stock New Game. The A that picked Solo was
// eaten by the chooser, so inject a fresh one after the release.
inline void on_solo() {
  REXSYS_INFO("[coop] new game: Solo");
  g_pass_a = 1;
  fable2::cooplobby::InjectPress(static_cast<uint16_t>(fable2::cooplobby::kA), 250, 200);
}

inline void on_coop() {
  REXSYS_INFO("[coop] new game: Co-op, opening the host lobby");
  fable2::cooplobby::Callbacks cb;
  cb.host_start = [] { REXSYS_INFO("[coop] host start (T021: run New Game + skip to adulthood)"); };
  cb.back = [] {
    REXSYS_INFO("[coop] lobby closed, back to the chooser");
    open_chooser();
  };
  fable2::cooplobby::OpenHost(fable2::cooplobby::Kind::kNew, "", "", cb);
}

inline void open_chooser() {
  fable2::cooplobby::Callbacks cb;
  cb.solo = on_solo;
  cb.coop = on_coop;
  cb.back = [] { REXSYS_INFO("[coop] new game chooser: Back"); };
  fable2::cooplobby::OpenChooser(cb);
}

// The press hook. Returns true to swallow the press.
inline bool OnPress(PPCContext& ctx, uint8_t* /*base*/) {
  if (!REXCVAR_GET(coop_campaign)) return false;
  // While the chooser or lobby is open the panel owns the pad; the stock
  // menu sees nothing (its state is idle, this catches stragglers).
  if (fable2::cooplobby::g_open.load()) return true;
  if (!in_main_menu()) {
    g_past_title = false;  // left the front end: the next visit starts at the title
    return false;
  }

  const uint32_t mask = ctx.r5.u32 & 0xFFFF;
  // The title prompt ("Press A to start") goes through the same dispatch with
  // the same registers while the state probe already says MainMenu, so the
  // first A of a front-end visit is the title's and always passes.
  if (!g_past_title) {
    if ((mask & kA) && !(mask == g_last_mask && fable2::f5lua::steady_ms() - g_last_press_ms < kDedupMs)) {
      g_past_title = true;
      g_row = kNewGameRow;
      g_armed = true;
      REXSYS_INFO("[coop] front end: title passed, main menu has the input");
    }
    g_last_mask = mask;
    g_last_press_ms = fable2::f5lua::steady_ms();
    return false;
  }
  const int64_t now = fable2::f5lua::steady_ms();
  const bool repeat = mask == g_last_mask && now - g_last_press_ms < kDedupMs;
  g_last_mask = mask;
  g_last_press_ms = now;

  if (mask & kA) {
    if (g_pass_a > 0) {
      // The injected A (both of its dispatch calls) goes to the stock menu.
      if (!repeat) {
        --g_pass_a;
        g_armed = g_row != kNewGameRow;  // the stock New Game is starting
        REXSYS_INFO("[coop] new game: injected A passed to the stock menu");
      }
      return false;
    }
    if (repeat) return g_row == kNewGameRow && g_armed;  // second call of a swallowed press
    if (g_row == kNewGameRow && g_armed) {
      REXSYS_INFO("[coop] new game: A on New Game, showing Solo / Co-op");
      open_chooser();
      return true;
    }
    return false;
  }
  if (repeat) return false;
  if (mask & kDown) g_row = g_row < kLastRow ? g_row + 1 : kLastRow;
  if (mask & kUp) g_row = g_row > 0 ? g_row - 1 : 0;
  if (mask & (kBack | kB)) {
    // B (or Back) from the hero cards or any sub-screen lands on the main
    // menu with New Game selected.
    g_armed = true;
    g_row = kNewGameRow;
  }
  return false;
}

inline const bool g_installed = [] {
  fable2::fronthooks::g_press_hook = &OnPress;
  return true;
}();

}  // namespace fable2::coopfrontend
