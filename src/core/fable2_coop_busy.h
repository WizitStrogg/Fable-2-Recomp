// fable_2 - "the host is busy" for co-op joins and hero captures (T028a, T028b).
//
// Rig launches 9 and 10 crashed the host when a join or the own-hero capture
// landed in QC060's opening (autosave + movie + co-op screen). Busy means any
// of:
//   - a save is being loaded or written (the SDK has the "Save" root open,
//     rex_content_save_busy);
//   - a level loaded less than kAfterLoadMs ago;
//   - a movie or full cutscene plays, or the screen is not fully faded in
//     (Lua probe below, about once a second while in this PC's own world);
//   - the probe has not been heard for kStaleMs (the game is paused or stuck):
//     not known to be safe, so busy.
// Joins are held off while busy (fable2_coop_joingate.h: the host answers
// "session full" and the partner retries) and captures wait (fable2_coop_hero.h).

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include <rex/kernel/xam/lan_lobby.h>
#include <rex/logging.h>

#include "fable2_f5_lua.h"
#include "fable2_lua_console.h"

extern "C" int rex_content_save_busy();

namespace fable2::coopbusy {

constexpr int64_t kProbeEveryMs = 1000, kStaleMs = 5000, kAfterLoadMs = 10000;
// A probe that says busy for longer than this is not trusted (a query that
// means something else must not block co-op for good); logged once.
constexpr int64_t kMaxLuaBusyMs = 120000;

inline std::atomic<int> g_lua_busy{0};
inline std::atomic<int64_t> g_heard_ms{0};
inline std::atomic<int64_t> g_busy_since_ms{0};
inline std::atomic<bool> g_distrust_logged{false};
inline std::mutex g_why_mu;
inline std::string g_why;
inline int64_t g_next_probe_ms = 0;

// The probe looks the movie and cutscene queries up once (they are bound but
// no game script calls them, so their table is found at run time), prints its
// state when it changes and every 5th run so C++ knows it is alive.
constexpr const char* kProbe = R"lua(
if not CoopBusyFns then
  CoopBusyFns = {}
  for name, t in pairs(_G) do
    if type(t) == 'table' then
      for _, f in ipairs({'IsMoviePlaying', 'IsFullCutscenePlaying'}) do
        local fn = rawget(t, f)
        if type(fn) == 'function' and not CoopBusyFns[f] then
          CoopBusyFns[f] = fn
          print('[coop] busy probe uses ' .. name .. '.' .. f)
        end
      end
    end
  end
  CoopBusyRuns = 0
end
local why = ''
for f, fn in pairs(CoopBusyFns) do
  local ok, r = pcall(fn)
  if ok and r then why = why .. f .. ' ' end
end
local ok, fadedin = pcall(GUI.IsScreenFullyFadedIn)
if ok and fadedin == false then why = why .. 'faded ' end
CoopBusyRuns = CoopBusyRuns + 1
local line = '[coop] busy=' .. (why == '' and '0' or ('1 ' .. why))
if line ~= CoopBusyLast or CoopBusyRuns % 5 == 0 then
  CoopBusyLast = line
  print(line)
end
)lua";

inline void OnBusyLine(std::string_view line) {
  // "[coop] busy=0" or "[coop] busy=1 <why>"
  const auto eq = line.find("busy=");
  if (eq == std::string_view::npos || eq + 5 >= line.size()) return;
  const bool busy = line[eq + 5] == '1';
  const int was = g_lua_busy.exchange(busy ? 1 : 0);
  const int64_t now = fable2::f5lua::steady_ms();
  g_heard_ms.store(now);
  if (busy && !was) g_busy_since_ms.store(now);
  std::lock_guard<std::mutex> l(g_why_mu);
  g_why = busy && eq + 7 < line.size() ? std::string(line.substr(eq + 7)) : "";
  if (was != (busy ? 1 : 0)) REXSYS_INFO("[coop] host {}{}", busy ? "busy: " : "not busy", g_why);
}

// Why this PC should not take a join or run a capture now; "" = free.
inline std::string Busy() {
  if (rex_content_save_busy()) return "save";
  const int64_t now = fable2::f5lua::steady_ms();
  const int64_t loaded = fable2::f5lua::g_load_at_ms.load();
  if (loaded && now - loaded < kAfterLoadMs) return "level load";
  const int64_t heard = g_heard_ms.load();
  if (!heard || now - heard > kStaleMs) return "no recent probe (paused?)";
  if (g_lua_busy.load()) {
    if (now - g_busy_since_ms.load() > kMaxLuaBusyMs) {
      if (!g_distrust_logged.exchange(true))
        REXSYS_WARN("[coop] busy probe has said busy for over {} s: ignoring it", kMaxLuaBusyMs / 1000);
      return "";
    }
    std::lock_guard<std::mutex> l(g_why_mu);
    return g_why.empty() ? std::string("busy") : g_why;
  }
  return "";
}

// Game thread, from the co-op frame hook: probe while in this PC's own world
// (not while visiting someone else's: their host decides).
inline void tick(const rex::kernel::xam::lobby::LobbyStatus& status, int64_t now) {
  if (status.joined_host_ip || !fable2::f5lua::ready()) return;
  if (now < g_next_probe_ms) return;
  g_next_probe_ms = now + kProbeEveryMs;
  fable2::luaconsole::RunChunk(kProbe);
}

inline const bool g_installed = [] {
  fable2::luaconsole::AddLineHandler("[coop] busy=", OnBusyLine);
  return true;
}();

}  // namespace fable2::coopbusy
