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
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>

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
// The A that closes a panel is still held on the next poll, so the game's own
// dispatch receives it right after the panel closes (launch 3: it reopened the
// chooser). Solo / Start let that press through to the stock New Game; Back
// swallows it. A tap released before the next poll never arrives: then an A
// is injected once the deadline passes.
inline int64_t g_swallow_a_until = 0;
inline int64_t g_pass_deadline = 0;
constexpr int64_t kLeakWindowMs = 600;

// The front end: title prompt, menu movie or main menu. The state probe
// reports the title as PressAScreen or already as MainMenu, so both count.
inline bool in_main_menu() {
  const int s = fable2::stateprobe::CurrentState();
  return s == fable2::stateprobe::kMainMenu || s == fable2::stateprobe::kMainMenuMovie ||
         s == fable2::stateprobe::kPressAScreen;
}

inline void open_chooser();

// Lets the next A press (normally the one that closed the panel) reach the
// stock New Game.
inline void pass_next_a_to_new_game() {
  g_pass_a = 1;
  g_pass_deadline = fable2::f5lua::steady_ms() + kLeakWindowMs;
}

inline void swallow_closing_a() { g_swallow_a_until = fable2::f5lua::steady_ms() + kLeakWindowMs; }

inline void on_solo() {
  REXSYS_INFO("[coop] new game: Solo");
  pass_next_a_to_new_game();
}

// --- the co-op session after Start (host) or go (partner) ---------------------------

namespace lobby = rex::kernel::xam::lobby;

enum class Session { kNone, kHostStarting, kHostInWorld, kHostPartnerLost, kPartnerJoining };

struct SessionState {
  std::mutex m;
  Session s = Session::kNone;
  std::string cid, partner_name;
  uint32_t partner_ip = 0;
  uint32_t go_seq = 0;
  int64_t since_ms = 0;
  bool partner_spawned = false;
  bool host_female = false;
  bool quit_sent = false;  // host after a partner loss: QuitToFrontEnd requested
  // Partner: the host to join, the last join attempt, and since when joined.
  uint32_t host_ip = 0;
  int64_t last_try_ms = 0, joined_since_ms = 0;
  int tries = 0;
  // Start -> stock New Game -> hero card -> childhood -> skip to QC060 (T021)
  int64_t newgame_ms = 0, card_ms = 0, skip_ms = 0;
  uint32_t load_epoch = 0;  // first game script load after the card pick
};
inline std::atomic<int64_t> g_qc060_started_ms{0};  // "QC060_NewBeginning Starting" printed
inline std::atomic<bool> g_skip_installed{false};      // coop_skip_childhood.lua took
inline std::atomic<bool> g_childhood_started{false};   // the game moved to QC010 itself
inline std::atomic<int64_t> g_coop_screen_ms{0};       // QC060's co-op introduction finished
inline std::atomic<bool> g_coop_screen_hook_failed{false};
inline SessionState& S() {
  static SessionState s;
  return s;
}

inline std::string new_campaign_id() {
  static uint64_t salt = 0x9E3779B97F4A7C15ull;
  salt ^= static_cast<uint64_t>(fable2::f5lua::steady_ms()) * 0x100000001B3ull;
  salt ^= reinterpret_cast<uintptr_t>(&salt);
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(salt));
  return buf;
}

// Host pressed Start (spec D4): the stock New Game, the hero card from the
// lobby's gender, the childhood world, then the game's own gameflow skip to
// QC060 (session_tick); the partner is sent coop_go once adulthood starts.
inline void on_host_start() {
  {
    auto& lb = fable2::cooplobby::L();
    std::lock_guard<std::mutex> l1(lb.m);
    std::lock_guard<std::mutex> l2(S().m);
    S().s = Session::kHostStarting;
    S().cid = new_campaign_id();
    S().partner_name = lb.host.rec.partner;
    S().partner_ip = lb.host.partner_ip;
    S().since_ms = fable2::f5lua::steady_ms();
    S().partner_spawned = false;
    S().host_female = lb.host.rec.gender1 == fable2::cooplobby::Gender::kFemale;
    S().newgame_ms = S().card_ms = S().skip_ms = 0;
    S().load_epoch = 0;
    g_qc060_started_ms.store(0);
    g_skip_installed.store(false);
    g_childhood_started.store(false);
    g_coop_screen_ms.store(0);
    g_coop_screen_hook_failed.store(false);
    fable2::coophero::g_capture_paused.store(true);  // until the adult world is ready
    REXSYS_INFO("[coop] host start: campaign {} with {}, host hero {}", S().cid, S().partner_name,
                S().host_female ? "female" : "male");
  }
  fable2::cooplobby::HidePanel();
  pass_next_a_to_new_game();
}

// Partner: the host's world is up. Join it (the F9 path, proven by P3).
// Partner: the host's world is up. The join itself happens in session_tick:
// only while the host's session says it has a free slot, and again every
// 15 s if the host refuses (launch 7: the host was still in QC060's opening
// and answered the join with a refusal).
inline void on_partner_go(uint32_t ip) {
  {
    std::lock_guard<std::mutex> l(S().m);
    S().s = Session::kPartnerJoining;
    S().since_ms = fable2::f5lua::steady_ms();
    S().host_ip = ip;
    S().last_try_ms = S().joined_since_ms = 0;
    S().tries = 0;
  }
  fable2::cooplobby::Close();
  REXSYS_INFO("[coop] partner: go from host {:08X}, joining when its game is joinable", ip);
}

inline void open_host_lobby(fable2::cooplobby::Kind kind, const std::string& cid,
                            const std::string& partner) {
  fable2::cooplobby::Callbacks cb;
  cb.host_start = on_host_start;
  cb.partner_go = on_partner_go;
  cb.back = [] {
    REXSYS_INFO("[coop] lobby closed, back to the chooser");
    swallow_closing_a();
    open_chooser();
  };
  fable2::cooplobby::OpenHost(kind, cid, partner, cb);
}

inline void on_coop() {
  REXSYS_INFO("[coop] new game: Co-op, opening the host lobby");
  open_host_lobby(fable2::cooplobby::Kind::kNew, "", "");
}

// Session watcher (render thread, ~2 Hz).
inline int64_t g_next_session_ms = 0;
inline uint32_t g_seen_spawn_epoch = 0;
inline void session_tick() {
  const int64_t now = fable2::f5lua::steady_ms();
  if (now < g_next_session_ms) return;
  g_next_session_ms = now + 500;
  std::function<void()> after;  // runs once the session lock is released
  {
  std::lock_guard<std::mutex> l(S().m);
  auto& s = S();
  switch (s.s) {
    case Session::kHostStarting: {
      // 1. The stock hero cards come up ~20 s after New Game: pick the card
      //    the host chose in the lobby (boy is selected first; Right = girl).
      if (s.newgame_ms && !s.card_ms && now - s.newgame_ms > 30000) {
        s.card_ms = now;
        if (s.host_female) {
          fable2::cooplobby::InjectPress(static_cast<uint16_t>(fable2::cooplobby::kRight), 0, 200);
          fable2::cooplobby::InjectPress(static_cast<uint16_t>(fable2::cooplobby::kA), 700, 200);
        } else {
          fable2::cooplobby::InjectPress(static_cast<uint16_t>(fable2::cooplobby::kA), 0, 200);
        }
        REXSYS_INFO("[coop] host start: picking the {} hero card", s.host_female ? "girl" : "boy");
      }
      // 2. Install the skip before the gameflow leaves GAMEFLOW_START: from the
      //    first game script load after the card, run coop_skip_childhood.lua
      //    every 500 ms until it reports "installed" (the first run can land
      //    before the world's Lua state exists, launch 8). The world's scripts
      //    come up ~4 s before the game starts the childhood. See the Lua file
      //    for why it must not be skipped from inside the running childhood.
      if (s.card_ms && !s.load_epoch) s.load_epoch = fable2::f5lua::g_load_epoch.load() + 1;
      if (s.card_ms && !g_skip_installed.load() && fable2::f5lua::ready() &&
          fable2::f5lua::g_load_epoch.load() >= s.load_epoch) {
        if (g_childhood_started.load()) {
          if (s.skip_ms >= 0) {
            REXSYS_INFO("[coop] host start: FAIL - the childhood started before the skip was in place");
            s.skip_ms = -1;
          }
        } else if (now - s.skip_ms >= 500) {
          if (!s.skip_ms) REXSYS_INFO("[coop] host start: new game scripts up, installing the skip to adulthood (QC060)");
          s.skip_ms = now;
          fable2::luaconsole::RunFile("coop_skip_childhood.lua");
        }
      }
      // 3. Adulthood started and settled: the partner may join.
      // Only after QC060's opening (autosave, movie, the game's own co-op
      // screen) is the world ready for a partner: a join during the opening
      // crashed the host (launch 9). Fallback if the screen hook failed.
      const int64_t ready_ms =
          g_coop_screen_ms.load() ? g_coop_screen_ms.load() + 10000
          : (g_coop_screen_hook_failed.load() && g_qc060_started_ms.load())
              ? g_qc060_started_ms.load() + 150000
              : 0;
      if (g_skip_installed.load() && ready_ms && now > ready_ms &&
          lobby::GetLobbyStatus().in_world) {
        s.s = Session::kHostInWorld;
        s.since_ms = now;
        g_seen_spawn_epoch = fable2::coophero::g_spawn_epoch.load();
        const std::string go = fable2::cooplobby::Format(fable2::cooplobby::GoRecord{s.cid, ++s.go_seq});
        lobby::PublishData("coop_go", go);
        REXSYS_INFO("[coop] host world up: published {}", go);
        fable2::coophero::g_capture_paused.store(false);
      }
      break;
    }
    case Session::kHostInWorld: {
      if (!s.partner_spawned && fable2::coophero::g_spawn_epoch.load() != g_seen_spawn_epoch) {
        s.partner_spawned = true;
        lobby::PublishData("coop_go", "");  // joined: a stale go must not pull a rejoin
        lobby::PublishData("coop_lobby", "");
        REXSYS_INFO("[coop] host: player 2 is in the world");
      }
      if (!s.partner_spawned) break;
      int quiet = -1;
      for (const auto& p : lobby::GetLanPeers())
        if (p.ip == s.partner_ip) quiet = p.seconds_since_seen;
      if (quiet < 0 || quiet > 6) {
        // Partner lost (proof P6): save with the game's own autosave call
        // (QC060 uses AutoSaveGame(true, true)), then, once it has had time
        // to finish, back to the front end to wait in the campaign's lobby.
        REXSYS_INFO("[coop] host: partner {} lost (quiet {} s), saving", s.partner_name, quiet);
        s.s = Session::kHostPartnerLost;
        s.since_ms = now;
        s.quit_sent = false;
        fable2::luaconsole::RunChunk(
            "print('[coop] AutoSaveGame: ' .. tostring(pcall(AutoSaveGame, true, true)))");
      }
      break;
    }
    case Session::kHostPartnerLost:
      if (!s.quit_sent && now - s.since_ms > 10000) {
        s.quit_sent = true;
        REXSYS_INFO("[coop] host: returning to the front end");
        fable2::luaconsole::RunChunk(
            "print('[coop] QuitToFrontEnd: ' .. tostring(pcall(Debug.QuitToFrontEnd)))");
      }
      if (s.quit_sent && !lobby::GetLobbyStatus().in_world && in_main_menu() && now - s.since_ms > 13000) {
        REXSYS_INFO("[coop] host: back at the front end, waiting for {} in the lobby", s.partner_name);
        s.s = Session::kNone;
        after = [cid = s.cid, partner = s.partner_name] {
          open_host_lobby(fable2::cooplobby::Kind::kContinue, cid, partner);
        };
      }
      break;
    case Session::kPartnerJoining: {
      // Joined only counts once it holds for 10 s (a refused join shows the
      // joined state for a few milliseconds).
      if (lobby::GetLobbyStatus().joined_host_ip) {
        if (!s.joined_since_ms) s.joined_since_ms = now;
        if (now - s.joined_since_ms > 10000) {
          REXSYS_INFO("[coop] partner: in the host's world (after {} attempt(s))", s.tries);
          s.s = Session::kNone;
          g_armed = false;  // in the host's world: pause-menu presses are not New Game
        }
        break;
      }
      s.joined_since_ms = 0;
      if (now - s.since_ms > 300000) {
        REXSYS_INFO("[coop] partner: no join in 5 min ({} attempt(s)), giving up", s.tries);
        s.s = Session::kNone;
        break;
      }
      bool joinable = false;
      for (const auto& p : lobby::GetLanPeers())
        if (p.ip == s.host_ip) joinable = p.in_world && p.joinable;
      if (joinable && now - s.last_try_ms > 15000) {
        s.last_try_ms = now;
        ++s.tries;
        const bool ok = lobby::JoinLanPeer(s.host_ip);
        REXSYS_INFO("[coop] partner: join attempt {} to {:08X}: {}", s.tries, s.host_ip,
                    ok ? "started" : "no joinable game");
      }
      break;
    }
    case Session::kNone:
      break;
  }
  }
  if (after) after();
}

inline void open_chooser() {
  fable2::cooplobby::Callbacks cb;
  cb.solo = on_solo;
  cb.coop = on_coop;
  cb.back = [] {
    REXSYS_INFO("[coop] new game chooser: Back");
    swallow_closing_a();
  };
  fable2::cooplobby::OpenChooser(cb);
}

// The press hook. Returns true to swallow the press.
inline bool OnPress(PPCContext& ctx, uint8_t* /*base*/) {
  if (!REXCVAR_GET(coop_campaign)) return false;
  // While the chooser or lobby is open the panel owns the pad; the stock
  // menu sees nothing (its state is idle, this catches stragglers).
  if (fable2::cooplobby::g_open.load()) return true;
  {
    // Partner joining the host: the game's own join prompts need the pad
    // (launch 6: an A meant for the prompt opened the chooser instead).
    std::lock_guard<std::mutex> l(S().m);
    if (S().s == Session::kPartnerJoining) return false;
  }
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
      // The closing (or injected) A, both of its dispatch calls, goes to the
      // stock menu: New Game starts.
      if (!repeat) {
        --g_pass_a;
        g_armed = false;
        REXSYS_INFO("[coop] new game: A passed to the stock New Game");
        std::lock_guard<std::mutex> l(S().m);
        if (S().s == Session::kHostStarting && !S().newgame_ms) S().newgame_ms = now;
      }
      return false;
    }
    if (now < g_swallow_a_until) return true;  // the A that closed a panel with Back
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

inline void (*g_prev_frame_hook)() = nullptr;
inline void OnFrame() {
  if (g_prev_frame_hook) g_prev_frame_hook();
  if (!REXCVAR_GET(coop_campaign)) return;
  // A quick tap released before the next poll: the closing A never reached
  // the dispatch, so send one.
  if (g_pass_a > 0 && g_pass_deadline && fable2::f5lua::steady_ms() > g_pass_deadline) {
    g_pass_deadline = 0;
    REXSYS_INFO("[coop] new game: closing A did not arrive, injecting one");
    fable2::cooplobby::InjectPress(static_cast<uint16_t>(fable2::cooplobby::kA), 0, 200);
  }
  session_tick();
}

inline const bool g_installed = [] {
  fable2::fronthooks::g_press_hook = &OnPress;
  fable2::luaconsole::AddLineHandler("QC060_NewBeginning Starting", [](std::string_view) {
    g_qc060_started_ms.store(fable2::f5lua::steady_ms());
  });
  fable2::luaconsole::AddLineHandler("[coop] skip: co-op screen done", [](std::string_view) {
    g_coop_screen_ms.store(fable2::f5lua::steady_ms());
  });
  fable2::luaconsole::AddLineHandler("[coop] skip: co-op screen hook failed",
                                     [](std::string_view) { g_coop_screen_hook_failed.store(true); });
  fable2::luaconsole::AddLineHandler("[coop] skip: wrapper installed",
                                     [](std::string_view) { g_skip_installed.store(true); });
  fable2::luaconsole::AddLineHandler("Skipping gameflow to 1", [](std::string_view) {
    if (!g_skip_installed.load()) g_childhood_started.store(true);
  });
  g_prev_frame_hook = fable2::f5lua::g_frame_hook;  // the lobby's (fable2_coop_lobby.h)
  fable2::f5lua::g_frame_hook = &OnFrame;
  return true;
}();

}  // namespace fable2::coopfrontend
