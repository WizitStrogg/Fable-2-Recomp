// fable_2 - "bring your own hero" scaffolding for System Link co-op.
//
// The co-op partner appears in the host's world as a henchman. This module
// turns that henchman into the partner's own hero by running CoopHero.Apply
// (data/scripts/recomp/coop_lib.lua) on it with the partner's profile line:
//   COOPHERO:v1|g=..|mor=..|...|worn=<record ids>|weap=<record ids>
//
// Pieces:
//   * Own profile: `luafile coop_capture` prints this PC's COOPHERO line; the
//     line handler keeps the latest one (OwnProfile()) for the LAN data
//     channel to publish.
//   * Partner profile: cvar coop_partner_profile (set by the LAN data channel
//     once it exists, or by hand from the console for testing).
//   * Henchman spawn: an override of the spawn function sub_8231FA68 records
//     when a henchman appears; with coop_auto_apply on, the partner profile is
//     applied coop_apply_delay_ms later (the entity needs a moment to settle).
//   * Console: `coop_apply` applies the partner profile now, `coop_apply own`
//     applies this PC's own profile (local-view fallback / self test).

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "fable2_f5_lua.h"
#include "fable2_lua_console.h"

REXCVAR_DEFINE_STRING(coop_partner_profile, "", "Network",
                      "COOPHERO profile line of the co-op partner, applied to the henchman "
                      "(filled in by the LAN data channel; can be set by hand for testing)");
REXCVAR_DEFINE_BOOL(coop_auto_apply, false, "Network",
                    "Turn the henchman into the partner's own hero automatically when it "
                    "spawns (experimental)");
REXCVAR_DEFINE_INT32(coop_apply_delay_ms, 1500, "Network",
                     "Delay between the henchman spawning and applying the partner's hero");

namespace fable2::coophero {

inline std::mutex& mtx() {
  static std::mutex m;
  return m;
}
inline std::string& own_profile_storage() {
  static std::string s;
  return s;
}

// Latest COOPHERO line printed on this PC (by coop_capture), or empty.
inline std::string OwnProfile() {
  std::lock_guard<std::mutex> l(mtx());
  return own_profile_storage();
}

using Clock = std::chrono::steady_clock;
inline std::atomic<int64_t> g_spawned_at_ms{0};  // 0 = nothing pending

inline int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch())
      .count();
}

inline std::string read_lib() {
  const auto file = fable2::f5lua::g_data_root / "data" / "scripts" / "recomp" / "coop_lib.lua";
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    REXSYS_ERROR("[coop] {} not found", file.string());
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Queues CoopHero.Apply(<henchman>, <profile>). Returns false if nothing was
// queued (no profile, no library).
inline bool ApplyToHenchman(const std::string& profile, const char* why) {
  if (profile.rfind("COOPHERO:v1|", 0) != 0) {
    REXSYS_WARN("[coop] no usable hero profile to apply ({})", why);
    return false;
  }
  const std::string lib = read_lib();
  if (lib.empty()) return false;
  const std::string eq = fable2::luaconsole::long_bracket_level(profile);
  std::string chunk = lib;
  chunk += "\nlocal hench = GetPlayerHenchman()\n";
  chunk += "if not hench or not hench:IsAlive() then print(\"[coop] apply: no live henchman\") return end\n";
  chunk += "local profile = CoopHero.Parse([" + eq + "[" + profile + "]" + eq + "])\n";
  chunk += "if not profile then print(\"[coop] apply: bad profile\") return end\n";
  chunk += "CoopHero.Apply(hench, profile)\n";
  chunk += "print(\"[coop] applied hero profile to the henchman (" + std::string(why) + ")\")\n";
  REXSYS_INFO("[coop] applying hero profile to the henchman ({})", why);
  return fable2::luaconsole::RunChunk(chunk);
}

// Per-frame (guest main thread): auto-apply after a henchman spawn.
inline void OnFrame() {
  const int64_t spawned = g_spawned_at_ms.load();
  if (spawned == 0) return;
  if (now_ms() - spawned < REXCVAR_GET(coop_apply_delay_ms)) return;
  g_spawned_at_ms.store(0);
  if (!REXCVAR_GET(coop_auto_apply)) return;
  ApplyToHenchman(REXCVAR_GET(coop_partner_profile), "henchman spawned");
}

inline void CmdCoopApply(std::string_view args) {
  const std::string arg = fable2::luaconsole::trim(args);
  if (arg == "own") {
    ApplyToHenchman(OwnProfile(), "own profile");
  } else if (arg.empty() || arg == "partner") {
    ApplyToHenchman(REXCVAR_GET(coop_partner_profile), "partner profile");
  } else {
    ApplyToHenchman(arg, "profile from the console");
  }
}

// Installs the line handler and the per-frame hook. Call once at startup.
inline void Install() {
  fable2::luaconsole::AddLineHandler("COOPHERO:", [](std::string_view line) {
    std::lock_guard<std::mutex> l(mtx());
    own_profile_storage().assign(line);
  });
  fable2::f5lua::g_frame_hook = &OnFrame;
}

// Self-install at static init (header is included once, from main.cpp).
inline const bool g_installed = (Install(), true);

}  // namespace fable2::coophero

REXCVAR_DEFINE_COMMAND_ARGS(coop_apply, fable2::coophero::CmdCoopApply, "Network",
                            "Apply a hero profile to the co-op henchman: no argument = the "
                            "partner's (coop_partner_profile), 'own' = this PC's last "
                            "coop_capture, or a COOPHERO line");

// Henchman spawn (see research/02-hench-lua-rewards.md): note the time, then
// let the original run.
extern "C" void __imp__sub_8231FA68(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8231FA68(PPCContext& ctx, uint8_t* base) {
  __imp__sub_8231FA68(ctx, base);
  REXSYS_INFO("[coop] henchman spawned");
  fable2::coophero::g_spawned_at_ms.store(fable2::coophero::now_ms());
}
