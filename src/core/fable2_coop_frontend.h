// fable_2 - co-op campaign front end (specs/001-coop-campaign, T018).
//
// With coop_campaign on, the main menu has a native "Co-op" entry below
// Continue (fable2_coop_mainmenu.h, Josh 2026-10-24); New Game and Continue are
// the stock game's. Co-op opens the hub (open_coop_hub): the lobbies heard on
// the network to join, "Host: continue a co-op save", "Host: new co-op
// campaign", Back. The host's Start loads the campaign save through the menu's
// own "Load Game"; the partner joins once the host's world is up.
// With coop_campaign off nothing is intercepted.
//
// Hook point for the panel's pad ownership and the host's load request:
// FrontMenu_PressBitDispatch (0x82188F20), the main menu's raw press dispatch
// (proof P1). r5 is the pressed mask (Up 1, Down 2, Back 32, A 4096), and each
// physical press arrives about twice within 400 ms.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "fable2_coop_campaign.h"
#include "fable2_coop_lobby.h"
#include "fable2_coop_backup.h"
#include "fable2_coop_p2file.h"
#include "fable2_coop_template.h"
#include "fable2_coop_test_input.h"
#include "fable2_front_hooks.h"
#include "../diagnostics/fable2_state_probe.h"

// rexglue-sdk: narrow the game's save list to one slot ("" = all saves).
extern "C" void rex_content_set_save_filter(const char* file_name);
extern "C" void rex_ui_set_lobby_view(const char* view);
extern "C" void rex_ui_set_lobby_font(const char* bft_path);
extern "C" void rex_ui_set_lobby_cover(const char* text);

REXCVAR_DEFINE_BOOL(coop_join_screen_skip, false, "Network",
                    "Co-op campaign: answer the game's own join screens through their action "
                    "readers (fable2_coop_joinscreen.h) instead of pad presses");
REXCVAR_DEFINE_BOOL(coop_join_screen_nolayout, false, "Network",
                    "Experiment: with coop_join_screen_skip, do not build the join screens' "
                    "layout at all (fable2_coop_joinscreen.h)");
REXCVAR_DEFINE_BOOL(coop_join_screens_auto, true, "Network",
                    "Co-op campaign: answer the game's own join screens automatically "
                    "(off only for rig traces that step through them)");
REXCVAR_DEFINE_BOOL(coop_menu_probe, false, "Network",
                    "Testing: log the main menu dispatch's element memory on each press (T018a)");
REXCVAR_DEFINE_STRING(coop_rig_save_filter, "", "Network",
                      "Testing: list only this save slot (HeroNNN), so Continue loads it");
REXCVAR_DEFINE_INT32(coop_partner_join_delay_ms, 0, "Network",
                     "Testing: the partner waits this long after coop_go before its first join "
                     "attempt (rig T028: lets a third player try the open slot first)");

REXCVAR_DEFINE_STRING(coop_campaign_status, "", "Network",
                      "Read only: what the co-op campaign is doing (shown in F6)");

namespace fable2::coopfrontend {

constexpr uint32_t kUp = 1, kDown = 2, kBack = 32, kA = 0x1000, kB = 0x2000;
constexpr int kNewGameRow = 0, kLastRow = 4;
constexpr int64_t kDedupMs = 400;

inline int g_row = 0;               // mirrored main-menu cursor
// Host Start -> load the campaign save through the menu's own handler
// (fable2::coopmainmenu::RequestLoadGame, set by fable2_coop_mainmenu.h).
inline std::atomic<bool> g_load_pending{false};
inline bool (*g_request_load)(PPCContext&, uint8_t*) = nullptr;
inline bool g_armed = true;         // false once the stock New Game is running
inline int g_pass_a = 0;            // injected A presses to let through
inline int64_t g_last_press_ms = 0;
inline bool g_past_title = false;   // the title's "Press A" was passed this visit
inline bool g_continue_swallowed = false;  // the last A on Continue opened a campaign lobby
// Host: the campaign partner's online XUID; joins from anyone else are refused
// while a campaign is hosted (T028, fable2_coop_joingate.h). 0 = not known.
inline std::atomic<uint64_t> g_partner_xuid{0};
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

inline std::optional<std::string> restore_player2_chunk(const std::filesystem::path& slot);
inline void p2_watch_start(const std::filesystem::path& slot, const std::string& cid, uint32_t partner_ip);
inline void p2_watch_stop();


// Lets the next A press (normally the one that closed the panel) reach the
// stock New Game.
inline void pass_next_a_to_new_game() {
  g_pass_a = 1;
  g_pass_deadline = fable2::f5lua::steady_ms() + kLeakWindowMs;
}

inline void swallow_closing_a() { g_swallow_a_until = fable2::f5lua::steady_ms() + kLeakWindowMs; }

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
  uint64_t host_xuid = 0;  // the host is heard once per shared network: match by XUID
  std::string host_name;
  // Host: the partner's XUID, and the last coop_go the partner acknowledged.
  uint64_t partner_xuid = 0;
  uint32_t acked_seq = 0;
  int64_t go_ms = 0;
  int64_t last_try_ms = 0, joined_since_ms = 0;
  int tries = 0;
  // Join screens: A presses made, and when the last one was.
  int accept_presses = 0;
  int64_t accept_ms = 0;
  // Start -> stock New Game -> hero card -> childhood -> skip to QC060 (T021)
  int64_t newgame_ms = 0, card_ms = 0, skip_ms = 0;
  uint32_t load_epoch = 0;  // first game script load after the card pick
  // Starter-save start (default): the slot made from the template, its gender,
  // and when the loaded world was first seen settled.
  bool from_template = false, template_female = false, gender_sent = false;
  int64_t gender_try_ms = 0;  // last gender switch attempt (T021a)
  std::string slot_name;
  std::filesystem::path slot_path;
};
inline std::atomic<int64_t> g_qc060_started_ms{0};  // "QC060_NewBeginning Starting" printed
inline std::atomic<bool> g_skip_installed{false};      // coop_skip_childhood.lua took
inline std::atomic<bool> g_childhood_started{false};   // the game moved to QC010 itself
inline std::atomic<int64_t> g_coop_screen_ms{0};       // QC060's co-op introduction finished
inline std::atomic<bool> g_coop_screen_hook_failed{false};
// Host: when ProcessJoinSession last approved a join (fable2_coop_joingate.h).
inline std::atomic<int64_t> g_join_approved_ms{0};
// Partner: the campaign host whose purse total this PC mirrors (partner_purse_tick).
inline std::atomic<uint64_t> g_purse_host_xuid{0};  // set at the go, cleared on leaving
inline uint32_t g_purse_host_ip = 0;
inline uint32_t g_purse_applied_seq = 0;
// The host hero reads the lobby's gender ("[coop] hero gender ok", T021a).
inline std::atomic<bool> g_gender_ok{false};
// The game's own join screens (gold/XP split on the host; look, then "continue
// without choosing a save" on the partner) all accept with A (launch 13). During a
// campaign A is pressed for both, every kAcceptEveryMs, at most kAcceptPresses times.
constexpr int64_t kAcceptFirstMs = 2000, kAcceptEveryMs = 2000;
constexpr int kAcceptPresses = 7;
// The go handshake: the host publishes coop_go again until the partner
// acknowledges it; either side gives up (with a message) after kPartnerJoinMs.
constexpr int64_t kGoRetryMs = 15000, kPartnerJoinMs = 120000;

// The join cover (Josh, 2026-10-24: neither player may see the game's own co-op
// join screens, not for one frame). A full-screen card in the menu style (lobby
// panel 'C' view) goes up before those screens can exist: on the host at the
// join approval (inside ProcessJoinSession, fable2_coop_joingate.h), on the
// partner at the host's go. It comes down kCoverAfterSpawnMs after player 2
// spawns (the screens close at the spawn; both PCs see the spawn), or after
// kCoverMaxMs. While it is up the stock screens are answered underneath.
// kCoverMaxMs: from the host's Start (both cards go up then) to player 2 in the world.
constexpr int64_t kCoverAfterSpawnMs = 1500, kCoverMaxMs = 300000;
inline std::atomic<int64_t> g_cover_since_ms{0};
inline std::atomic<int64_t> g_cover_hide_at_ms{0};
inline std::atomic<uint32_t> g_cover_epoch{0};
inline void ShowCover2(const std::string& title, const std::string& line) {
  g_cover_epoch.store(fable2::coophero::g_spawn_epoch.load());
  g_cover_hide_at_ms.store(0);
  g_cover_since_ms.store(fable2::f5lua::steady_ms());
  // Its own channel: the lobby closing underneath (the partner's go) must not
  // clear it (launch 26: the partner's cover was wiped by the lobby's "").
  rex_ui_set_lobby_cover((title + "\n" + line).c_str());
  REXSYS_INFO("[coop] join cover up: {} / {}", title, line);
}
inline void ShowCover(const std::string& text) { ShowCover2(text, "The co-op campaign is starting"); }
// A message on the same card (title, then a second line), taken down after for_ms.
inline void ShowCoverMessage(const std::string& title, const std::string& line, int64_t for_ms) {
  const int64_t now = fable2::f5lua::steady_ms();
  g_cover_epoch.store(fable2::coophero::g_spawn_epoch.load());
  g_cover_since_ms.store(now);
  g_cover_hide_at_ms.store(now + for_ms);
  rex_ui_set_lobby_cover((title + "\n" + line).c_str());
  REXSYS_INFO("[coop] message: {} / {}", title, line);
}
inline void HideCover(const char* why) {
  if (!g_cover_since_ms.exchange(0)) return;
  g_cover_hide_at_ms.store(0);
  rex_ui_set_lobby_cover("");
  REXSYS_INFO("[coop] join cover down ({})", why);
}
inline void cover_tick() {
  const int64_t since = g_cover_since_ms.load();
  if (!since) return;
  const int64_t now = fable2::f5lua::steady_ms();
  if (!g_cover_hide_at_ms.load() && fable2::coophero::g_spawn_epoch.load() != g_cover_epoch.load())
    g_cover_hide_at_ms.store(now + kCoverAfterSpawnMs);
  const int64_t hide_at = g_cover_hide_at_ms.load();
  if (hide_at && now >= hide_at) HideCover("player 2 is in the world");
  else if (now - since > kCoverMaxMs) HideCover("timed out");
}
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

// A campaign slot is a save slot with a coop_campaign.toml marker (data-model.md).
inline std::optional<fable2::cooptemplate::NewSlot> campaign_slot(const std::string& cid) {
  for (auto& s : fable2::cooptemplate::Slots()) {
    const auto m = fable2::coopcampaign::ReadMarker(s.path);
    if (m && m->campaign_id == cid) return s;
  }
  return std::nullopt;
}

inline std::string utc_now() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

// The main menu's rows with saves present: New Game, Continue, ...
constexpr int kContinueRow = 1;

// Moves the mirrored cursor to Continue and presses A there, after `delay` ms.
inline void inject_continue(int64_t delay) {
  int row = g_row;
  while (row != kContinueRow) {
    const bool down = row < kContinueRow;
    fable2::cooplobby::InjectPress(static_cast<uint16_t>(down ? fable2::cooplobby::kDown : fable2::cooplobby::kUp),
                                   delay, 200);
    row += down ? 1 : -1;
    delay += 700;
  }
  fable2::cooplobby::InjectPress(static_cast<uint16_t>(fable2::cooplobby::kA), delay, 200);
}

// T031a: player 2 in a new campaign is a fresh adult hero with the lobby's
// gender and the host's starter kit (clothes, starter weapons, abilities, age,
// height), without the host's own state (morality, purity, renown, title,
// items). Taken from the host's hero when player 2 spawns. Until T031 reads
// coop_player2.txt, a continued campaign seeds the same way.
inline std::string seed_player2_chunk(bool male) {
  std::string c;
  c += "local hero, hench = GetPlayerHero(), GetPlayerHenchman()\n";
  c += "if not hench or not hench:IsAlive() then print('[coop] seed: no live player 2') return end\n";
  c += "local p = CoopHero.Capture(hero)\n";
  c += std::string("p.male = ") + (male ? "true" : "false") + "\n";
  c += "p.mor, p.pur, p.ren, p.title = 0, 0, 0, nil\n";
  c += "p.items = {}\n";
  c += "CoopHero.Apply(hench, p, {body = false})\n";
  c += "print('[coop] player 2 seeded from the host starter profile')\n";
  c += "print(CoopHero.SerializeV2(CoopHero.Capture(hench), 0))\n";
  return c;
}

// Host pressed Start (spec D4). A new campaign copies the starter save into a
// slot of its own and marks it; a continued one (Continue on a campaign slot,
// T025) reloads its slot. Either way the game's own Continue loads the slot.
inline void on_host_start() {
  std::optional<fable2::cooptemplate::NewSlot> slot;
  std::optional<fable2::cooptemplate::TemplateInfo> tmpl;
  const bool from_template = !REXCVAR_GET(coop_childhood_skip);
  fable2::cooplobby::LobbyRecord rec;
  {
    auto& lb = fable2::cooplobby::L();
    std::lock_guard<std::mutex> l(lb.m);
    rec = lb.host.rec;
  }
  const bool resume = rec.kind == fable2::cooplobby::Kind::kContinue;
  const std::string cid = resume ? rec.cid : new_campaign_id();
  if (from_template || resume) {
    // If the slot cannot be made (or found) the lobby stays open and says why.
    std::string error;
    if (resume) {
      slot = campaign_slot(cid);
      if (!slot) error = "the campaign's save was not found";
    } else {
      const bool female = rec.gender1 == fable2::cooplobby::Gender::kFemale;
      tmpl = fable2::cooptemplate::ReadTemplate(female);
      slot = fable2::cooptemplate::CreateCampaignSlot(&error, female);
      if (slot) {
        fable2::coopcampaign::Marker m;
        m.campaign_id = cid;
        m.name = rec.host + " and " + rec.partner;
        m.created = utc_now();
        m.host_name = rec.host;
        m.host_xuid = rec.hxuid;
        m.partner_name = rec.partner;
        m.partner_xuid = rec.pxuid;  // the partner is matched by this on Continue
        m.p2_gender = std::string(1, fable2::cooplobby::GenderChar(rec.gender2));
        m.mod_version = "001";
        if (!fable2::coopcampaign::WriteFileAtomic(slot->path, "coop_campaign.toml",
                                                   fable2::coopcampaign::FormatMarker(m))) {
          std::error_code ec;
          std::filesystem::remove_all(slot->path, ec);
          std::filesystem::remove(slot->path.parent_path().parent_path() / "Headers" / "00000001" /
                                      (slot->name + ".header"), ec);
          slot.reset();
          error = "could not mark the campaign save";
        }
      }
    }
    if (!slot) {
      auto& lb = fable2::cooplobby::L();
      std::lock_guard<std::mutex> l(lb.m);
      lb.host.state = fable2::cooplobby::HostState::kBothReady;
      lb.host.rec.state = fable2::cooplobby::Name(lb.host.state);
      lb.notice = "Cannot start: " + error;
      return;
    }
  }
  {
    auto& lb = fable2::cooplobby::L();
    std::lock_guard<std::mutex> l1(lb.m);
    std::lock_guard<std::mutex> l2(S().m);
    S().s = Session::kHostStarting;
    S().cid = cid;
    S().partner_name = lb.host.rec.partner;
    S().partner_ip = lb.host.partner_ip;
    uint64_t pxuid = lb.host.partner_xuid;
    for (const auto& p : lobby::GetLanPeers())
      if (!pxuid && p.ip == lb.host.partner_ip) pxuid = p.xuid;
    g_partner_xuid.store(pxuid);
    S().partner_xuid = pxuid;
    S().acked_seq = 0;
    S().go_ms = 0;
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
    S().from_template = from_template || resume;
    // A continued campaign keeps its hero as saved: no gender switch.
    S().template_female = resume ? S().host_female : tmpl && tmpl->female;
    S().gender_sent = false;
    S().gender_try_ms = 0;
    g_gender_ok.store(false);
    S().slot_name = slot ? slot->name : "";
    S().slot_path = slot ? slot->path : std::filesystem::path();
    S().load_epoch = fable2::f5lua::g_load_epoch.load() + 1;  // the world load that follows
    REXSYS_INFO("[coop] host start: campaign {} with {}, host hero {}, {}", S().cid, S().partner_name,
                S().host_female ? "female" : "male",
                resume ? "continuing " + S().slot_name
                : from_template ? "starter save " + S().slot_name : std::string("gameflow skip"));
  }
  // T031: a continued campaign brings player 2 back from its file; otherwise
  // (new campaign, or no usable file) player 2 is seeded (T031a).
  std::optional<std::string> restore = resume && slot ? restore_player2_chunk(slot->path) : std::nullopt;
  fable2::coophero::SetCampaignPlayer2(
      restore ? *restore : seed_player2_chunk(rec.gender2 == fable2::cooplobby::Gender::kMale));
  fable2::cooplobby::HidePanel();
  // Josh, 2026-10-24: both players go straight to the joining card at Start.
  ShowCover2("Starting the co-op campaign",
             "Loading the world. " + (rec.partner.empty() ? std::string("Your partner") : rec.partner) +
                 " joins as soon as it is ready");
  if (S().from_template) {
    // The game's own Continue loads the campaign slot: while the save list
    // holds only that slot there is no save picker (with several saves the
    // picker came up, launch 12) and no way to load the wrong hero. The
    // panel's closing A is swallowed, then the cursor goes to Continue and A.
    rex_content_set_save_filter(slot->name.c_str());
    swallow_closing_a();
    // Load it with the game's own Continue, on the game thread: an injected A
    // reaches the press dispatch (OnPress), which calls the menu's handler.
    g_load_pending.store(true);
    fable2::cooplobby::InjectPress(static_cast<uint16_t>(fable2::cooplobby::kA), 700, 200);
  } else {
    pass_next_a_to_new_game();
  }
}

// Partner: the host's world is up. Join it (the F9 path, proven by P3).
// Partner: the host's world is up. The join itself happens in session_tick:
// only while the host's session says it has a free slot, and again every
// 15 s if the host refuses (launch 7: the host was still in QC060's opening
// and answered the join with a refusal).
inline void on_partner_go(uint32_t ip) {
  uint64_t host_xuid = 0;
  std::string host_name;
  {
    auto& lb = fable2::cooplobby::L();
    std::lock_guard<std::mutex> l(lb.m);
    host_xuid = lb.partner.host_xuid;
    host_name = lb.partner.seat.host;
  }
  {
    std::lock_guard<std::mutex> l(S().m);
    S().s = Session::kPartnerJoining;
    S().since_ms = fable2::f5lua::steady_ms();
    S().host_ip = ip;
    S().host_xuid = host_xuid;
    S().host_name = host_name;
    g_purse_host_ip = ip;
    g_purse_applied_seq = 0;
    g_purse_host_xuid.store(host_xuid ? host_xuid : 1);
    S().last_try_ms = S().joined_since_ms = 0;
    S().tries = 0;
  }
  fable2::cooplobby::Close();
  ShowCover2("Joining " + host_name + "'s campaign", "Joining...");
  REXSYS_INFO("[coop] partner: go from host {:08X} ({}), joining when its game is joinable", ip,
              fable2::cooplobby::XuidStr(host_xuid));
}

// The host's peer entries: one per network it shares with us. Returns the
// freshest (seconds since seen; -1 if none), its address, and whether its
// game is joinable on any of them.
struct PeerView {
  int quiet = -1;
  uint32_t ip = 0;
  bool joinable = false;
};
inline PeerView peer_view(uint64_t xuid, uint32_t ip) {
  PeerView v;
  for (const auto& p : lobby::GetLanPeers()) {
    if (xuid && p.xuid ? p.xuid != xuid : p.ip != ip) continue;
    if (v.quiet < 0 || p.seconds_since_seen < v.quiet) {
      v.quiet = p.seconds_since_seen;
      v.ip = p.ip;
    }
    if (p.in_world && p.joinable) {
      v.joinable = true;
      v.ip = p.ip;
    }
  }
  return v;
}

inline void open_coop_hub();

inline void open_host_lobby(fable2::cooplobby::Kind kind, const std::string& cid,
                            const std::string& partner, const std::string& partner_xuid = "") {
  fable2::cooplobby::Callbacks cb;
  cb.host_start = on_host_start;
  cb.partner_go = on_partner_go;
  cb.back = [] {  // Back from a lobby returns to the Co-op hub
    REXSYS_INFO("[coop] lobby closed, back to the Co-op hub");
    swallow_closing_a();
    open_coop_hub();
  };
  fable2::cooplobby::OpenHost(kind, cid, partner, cb, partner_xuid);
}

// The host's campaign saves (slots with a coop_campaign.toml marker), newest first.
struct CampaignSave {
  std::string cid, partner, partner_xuid, label;
  std::filesystem::file_time_type when;
};
inline std::vector<CampaignSave> campaign_saves() {
  std::vector<CampaignSave> out;
  for (const auto& s : fable2::cooptemplate::Slots()) {
    const auto m = fable2::coopcampaign::ReadMarker(s.path);
    if (!m || m->campaign_id.empty()) continue;
    std::error_code ec;
    CampaignSave c{m->campaign_id, m->partner_name, m->partner_xuid, "", {}};
    c.when = std::filesystem::last_write_time(s.path / "mainsave.bin", ec);
    c.label = "With " + (m->partner_name.empty() ? std::string("your partner") : m->partner_name) +
              (m->created.size() >= 10 ? "  -  started " + m->created.substr(0, 10) : std::string());
    out.push_back(std::move(c));
  }
  std::sort(out.begin(), out.end(), [](const CampaignSave& x, const CampaignSave& y) { return x.when > y.when; });
  return out;
}

inline std::vector<CampaignSave> g_saves_shown;

// Hub -> "Host: continue a co-op save": pick one, then its lobby (only its
// partner can take the seat).
inline void open_save_list() {
  g_saves_shown = campaign_saves();
  std::vector<std::string> labels;
  for (const auto& c : g_saves_shown) labels.push_back(c.label);
  fable2::cooplobby::Callbacks cb;
  cb.pick_save = [](size_t i) {
    if (i >= g_saves_shown.size()) return;
    const auto c = g_saves_shown[i];
    REXSYS_INFO("[coop] hub: hosting campaign {} with {}", c.cid, c.partner);
    open_host_lobby(fable2::cooplobby::Kind::kContinue, c.cid, c.partner, c.partner_xuid);
  };
  cb.back = [] {
    swallow_closing_a();
    open_coop_hub();
  };
  fable2::cooplobby::OpenSaves(std::move(labels), cb);
}

// Main menu -> Co-op (Josh, 2026-10-24): the only way into co-op. The hub lists
// the lobbies heard on the network (join), then "Host: continue a co-op save"
// (when this PC has one) and "Host: new co-op campaign" (from the adult starter
// save: no childhood), then Back to the main menu (focus stays on Co-op).
inline void open_coop_hub() {
  fable2::cooplobby::Callbacks cb;
  cb.partner_go = on_partner_go;
  cb.host_new = [] {
    REXSYS_INFO("[coop] hub: hosting a new campaign");
    open_host_lobby(fable2::cooplobby::Kind::kNew, "", "");
  };
  cb.host_saves = [] { open_save_list(); };
  // The host pressed Start: the joining card goes up at once, the lobby stays
  // open under it (it still follows the host's lobby and go; the pad is idle).
  cb.partner_wait = [](std::string host) {
    ShowCover2(host + " started the campaign", "Waiting for " + host + "'s world to load...");
  };
  cb.partner_wait_failed = [](std::string host, std::string why) {
    ShowCoverMessage(why == "left" ? host + " left the lobby" : "No answer from " + host,
                     why == "left" ? "Back to the Co-op menu" : "Their world did not load. Back to the Co-op menu",
                     8000);
  };
  cb.back = [] {
    REXSYS_INFO("[coop] hub closed, back to the main menu");
    swallow_closing_a();
  };
  fable2::cooplobby::OpenPartner(cb, false, !campaign_saves().empty());
}

// Called by the main menu's Co-op entry (fable2_coop_mainmenu.h), on the game thread.
inline void OnCoopEntry() {
  if (fable2::cooplobby::g_open.load()) return;
  g_past_title = true;
  REXSYS_INFO("[coop] Co-op: opening the hub");
  open_coop_hub();
}

// One A for the game's join screens if one is due: the first kAcceptFirstMs after
// `since` (the join), then every kAcceptEveryMs, kAcceptPresses in all.
inline void accept_tick(SessionState& s, int64_t since, int64_t now, const char* who) {
  if (!REXCVAR_GET(coop_join_screens_auto)) return;  // rig: stepped by hand (trace run)
  if (REXCVAR_GET(coop_join_screen_skip)) return;  // answered in code instead
  // Player 2 has spawned: the screens are gone, and A in the world would act
  // (launch 26: the partner pressed 3 more times after the spawn).
  if (g_cover_hide_at_ms.load()) return;
  if (s.accept_presses >= kAcceptPresses) return;
  const int64_t due = s.accept_presses ? s.accept_ms + kAcceptEveryMs : since + kAcceptFirstMs;
  if (now < due) return;
  s.accept_ms = now;
  ++s.accept_presses;
  fable2::cooplobby::InjectPress(static_cast<uint16_t>(fable2::cooplobby::kA), 0, 200);
  REXSYS_INFO("[coop] {}: A for the join screen ({}/{})", who, s.accept_presses, kAcceptPresses);
}

// --- player 2's hero file (T030 save, T031 load; fable2_coop_p2file.h) -------
//
// Host only. Player 2 is captured every kP2CacheEveryMs while in the world; a
// game save of the campaign slot (mainsave.bin changes, then holds still for
// kP2SettleMs) writes coop_player2.txt from a fresh capture, or from the last
// one when player 2 has already left (the partner-loss autosave, P6).
constexpr int64_t kP2CacheEveryMs = 20000, kP2SettleMs = 3000;
constexpr const char* kP2File = "coop_player2.txt";

struct P2Watch {
  std::mutex m;
  std::filesystem::path slot;  // the campaign slot folder ("" = not hosting a campaign)
  std::string cid;             // the campaign, and the partner the backups go to (T039)
  uint32_t partner_ip = 0;
  std::string cache;           // last COOPHERO:v2 line of player 2
  std::filesystem::file_time_type seen{};
  int64_t changed_ms = 0, next_cache_ms = 0;
};
inline P2Watch& P2() {
  static P2Watch w;
  return w;
}

inline std::optional<std::string> read_bytes(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return std::nullopt;
  return std::string(std::istreambuf_iterator<char>(in), {});
}

inline bool write_p2_file(const std::filesystem::path& slot, const std::string& profile,
                          const char* source) {
  namespace pf = fable2::coopp2file;
  const auto uid = read_bytes(slot / "saveuid.bin");
  const auto main = read_bytes(slot / "mainsave.bin");
  if (!uid || !main || profile.rfind("COOPHERO:v2|", 0) != 0) {
    REXSYS_WARN("[coop] player 2 file not written (slot files or profile missing)");
    return false;
  }
  pf::P2File f{pf::HexBytes(*uid), pf::Hex64(pf::Fnv1a64(*main)), utc_now(), source, profile};
  const bool ok = fable2::coopcampaign::WriteFileAtomic(slot, kP2File, pf::Format(f));
  REXSYS_INFO("[coop] player 2 file {} ({}, stamp {})", ok ? "written" : "NOT written", source, f.stamp);
  return ok;
}

// must: the capture after a save (COOPP2W) is kept until a console slot is
// free; the periodic cache capture just tries again next time.
inline void request_p2_capture(const char* tag, bool must = false) {
  const std::string lib = fable2::coophero::read_lib();
  if (lib.empty()) return;
  std::string chunk = lib + "\nlocal h = GetPlayerHenchman()\n"
                            "if h and h:IsAlive() then print('" + tag +
                      "|' .. CoopHero.SerializeV2(CoopHero.Capture(h), 0))\n"
                      "else print('" + tag + "-NONE') end\n";
  if (must) {
    fable2::luaconsole::RunChunkOrKeep(std::move(chunk));
  } else {
    fable2::luaconsole::RunChunk(chunk);
  }
}

inline void p2_watch_start(const std::filesystem::path& slot, const std::string& cid,
                           uint32_t partner_ip) {
  auto& w = P2();
  std::lock_guard<std::mutex> l(w.m);
  std::error_code ec;
  w.slot = slot;
  w.cid = cid;
  w.partner_ip = partner_ip;
  w.cache.clear();
  w.seen = std::filesystem::last_write_time(slot / "mainsave.bin", ec);
  w.changed_ms = 0;
  w.next_cache_ms = 0;
}
inline void p2_watch_stop() {
  std::lock_guard<std::mutex> l(P2().m);
  P2().slot.clear();
}

// Called from session_tick while hosting a campaign world.
inline void p2_watch_tick(int64_t now, bool player2_here) {
  auto& w = P2();
  std::unique_lock<std::mutex> l(w.m);
  if (w.slot.empty()) return;
  if (player2_here && now >= w.next_cache_ms) {
    w.next_cache_ms = now + kP2CacheEveryMs;
    l.unlock();
    request_p2_capture("COOPP2");
    l.lock();
  }
  std::error_code ec;
  const auto t = std::filesystem::last_write_time(w.slot / "mainsave.bin", ec);
  if (ec) return;
  if (t != w.seen) {
    w.seen = t;
    w.changed_ms = now;  // still being written: wait for it to settle
    return;
  }
  if (!w.changed_ms || now - w.changed_ms < kP2SettleMs) return;
  w.changed_ms = 0;
  REXSYS_INFO("[coop] campaign saved: recording player 2");
  if (player2_here) {
    l.unlock();
    request_p2_capture("COOPP2W", true);  // written by the line handler
    return;
  }
  if (w.cache.empty()) {
    REXSYS_WARN("[coop] campaign saved without player 2 and no earlier capture: file not written");
    return;
  }
  write_p2_file(w.slot, w.cache, "cache");
}

inline void on_p2_line(std::string_view line, bool write) {
  const size_t bar = line.find('|');
  if (bar == std::string_view::npos) return;
  const std::string profile(line.substr(bar + 1));
  auto& w = P2();
  std::lock_guard<std::mutex> l(w.m);
  w.cache = profile;
  // A save with player 2 in the world: the partner gets a backup of the slot (T039).
  if (write && !w.slot.empty() && write_p2_file(w.slot, profile, "capture"))
    fable2::coopbackup::SendBackup(w.slot, w.cid, w.partner_ip);
}
inline void on_p2_none(bool write) {
  auto& w = P2();
  std::lock_guard<std::mutex> l(w.m);
  if (write && !w.slot.empty() && !w.cache.empty()) write_p2_file(w.slot, w.cache, "cache");
}

// T031: the Lua that brings player 2 back from the slot's file, or nothing
// (no file, or one from another slot): then the seed applies.
inline std::optional<std::string> restore_player2_chunk(const std::filesystem::path& slot) {
  namespace pf = fable2::coopp2file;
  const auto text = read_bytes(slot / kP2File);
  if (!text) {
    REXSYS_INFO("[coop] player 2: no {} in {}, seeding", kP2File, slot.filename().string());
    return std::nullopt;
  }
  const auto f = pf::Parse(*text);
  const auto uid = read_bytes(slot / "saveuid.bin");
  const auto main = read_bytes(slot / "mainsave.bin");
  if (!f || !uid || !main) {
    REXSYS_WARN("[coop] player 2: {} unreadable, seeding", kP2File);
    return std::nullopt;
  }
  const auto match = pf::Check(*f, pf::HexBytes(*uid), pf::Hex64(pf::Fnv1a64(*main)));
  if (match == pf::Match::kOtherSlot) {
    REXSYS_WARN("[coop] player 2: {} belongs to another save (saveuid {}), seeding", kP2File, f->saveuid);
    return std::nullopt;
  }
  REXSYS_INFO("[coop] player 2: restoring from {} ({}, written {}{})", kP2File, f->source, f->written,
              match == pf::Match::kOlder ? ", older than the last save" : "");
  const std::string eq = fable2::luaconsole::long_bracket_level(f->profile);
  std::string c;
  c += "local hench = GetPlayerHenchman()\n";
  c += "if not hench or not hench:IsAlive() then print('[coop] restore: no live player 2') return end\n";
  c += "local p = CoopHero.Parse([" + eq + "[" + f->profile + "]" + eq + "])\n";
  c += "if not p then print('[coop] restore: bad profile') return end\n";
  c += "CoopHero.Apply(hench, p, {body = false})\n";
  c += "print('[coop] player 2 restored from coop_player2.txt')\n";
  c += "print(CoopHero.SerializeV2(CoopHero.Capture(hench), 0))\n";
  return c;
}

// --- one shared purse (T033, spec FR-010) ---------------------------------
//
// The game gives player 2 a purse of its own and splits gold between the two
// (P5: Money.Modify(player2, 100) gave each +50). The host's purse is the
// campaign's: once a second, whatever either purse gained or spent since the
// last sync is added to one total, and both purses are set to it (Money.Set,
// no split). A split pickup (+50/+50) counts +100; a purchase by either counts
// once. After player 2 (re)spawns its purse starts at the host's total.
// The host's Money.Set on its henchman copy does not reach the partner's own
// game (Josh's playtest 2026-10-25: the host showed 153, the partner 78), so
// the host publishes the total (coop_purse, COOPPURSE line from the Lua) and
// the partner sets its own purse to it (partner_purse_tick). The partner sends
// nothing back: everything it does with gold happens in the host's world and
// is already in the host's sum. The partner joins without a save of its own
// ("continue without choosing a save"), so the mirrored total never reaches
// the partner's own hero save.
constexpr int64_t kPurseEveryMs = 1000;
inline int64_t g_next_purse_ms = 0;
inline uint32_t g_purse_epoch = ~0u;

// Host: the Lua's COOPPURSE:<total> line -> the coop_purse record.
inline uint32_t g_purse_seq = 0;
inline void on_purse_line(std::string_view line) {
  const int64_t total = std::strtoll(std::string(line.substr(10)).c_str(), nullptr, 10);
  fable2::cooplobby::PurseRecord r{fable2::cooplobby::XuidStr(fable2::cooplobby::MyXuid()), total, ++g_purse_seq};
  lobby::PublishData("coop_purse", fable2::cooplobby::Format(r));
}

// Partner, in a campaign host's world: its own purse shows the host's total.
inline int64_t g_next_mirror_ms = 0, g_mirror_again_ms = 0;
inline bool g_purse_joined = false;
inline void partner_purse_tick(int64_t now) {
  const uint64_t hx = g_purse_host_xuid.load();
  if (!hx || now < g_next_mirror_ms) return;
  g_next_mirror_ms = now + 500;
  const bool joined = lobby::GetLobbyStatus().joined_host_ip != 0;
  if (!joined) {
    if (g_purse_joined) {  // left the host's world
      g_purse_joined = false;
      g_purse_host_xuid.store(0);
      g_purse_applied_seq = 0;
    }
    return;
  }
  g_purse_joined = true;
  if (!fable2::f5lua::ready()) return;
  std::optional<fable2::cooplobby::PurseRecord> best;
  for (const auto& d : lobby::GetPeerData("coop_purse")) {
    auto r = fable2::cooplobby::ParsePurse(d.value);
    if (!r) continue;
    const uint64_t from = std::strtoull(r->hxuid.c_str(), nullptr, 16);
    if (from ? from != hx : d.ip != g_purse_host_ip) continue;
    if (!best || r->seq > best->seq) best = r;
  }
  if (!best) return;
  // A new total at once; the same one again every 3 s (the game's own share
  // messages can still move the partner's local purse).
  if (best->seq == g_purse_applied_seq && now < g_mirror_again_ms) return;
  const bool fresh = best->seq != g_purse_applied_seq;
  if (fable2::f5lua::g_queued.load() > 0) return;  // paused: queued scripts wait; try next tick
  const std::string t = std::to_string(best->total);
  if (!fable2::luaconsole::RunChunk("local me = GetPlayerHenchman()\n"
                               "if me and me:IsAlive() and Money.Get(me) ~= " + t + " then\n"
                               "  local was = Money.Get(me)\n"
                               "  Money.Set(me, " + t + ")\n"
                               "  print('[coop] purse: mirrored the host total " + t + " (was ' .. was .. ')')\n"
                               "end\n"))
    return;  // no free console slot: the next tick tries again
  g_purse_applied_seq = best->seq;
  g_mirror_again_ms = now + 3000;
  if (fresh) REXSYS_INFO("[coop] purse: host total {} (seq {})", best->total, best->seq);
}

inline void purse_tick(int64_t now) {
  if (now < g_next_purse_ms) return;
  // Paused, queued scripts wait: do not stack syncs behind each other (they
  // would take every console slot); the next tick after the pause syncs.
  if (fable2::f5lua::g_queued.load() > 0) return;
  g_next_purse_ms = now + kPurseEveryMs;
  const uint32_t epoch = fable2::coophero::g_spawn_epoch.load();
  const bool reset = epoch != g_purse_epoch;
  g_purse_epoch = epoch;
  std::string c = R"lua(
local hero, hench = GetPlayerHero(), GetPlayerHenchman()
if not hench or not hench:IsAlive() then return end
local H, h = Money.Get(hero), Money.Get(hench)
)lua";
  if (reset) c += "CoopPurseLast = nil\n";
  c += R"lua(
if CoopPurseLast == nil then
  CoopPurseLast = H
  if h ~= H then Money.Set(hench, H) end
  print('[coop] purse: shared total ' .. H)
  print('COOPPURSE:' .. H)
  return
end
if H == CoopPurseLast and h == CoopPurseLast then return end
local total = CoopPurseLast + (H - CoopPurseLast) + (h - CoopPurseLast)
Money.Set(hero, total)
Money.Set(hench, total)
print(string.format('[coop] purse: host %d, player 2 %d, was %d -> total %d (now %d / %d)',
  H, h, CoopPurseLast, total, Money.Get(hero), Money.Get(hench)))
CoopPurseLast = total
print('COOPPURSE:' .. total)
)lua";
  fable2::luaconsole::RunChunk(c);
}

// F6 status line (T035): what the campaign is doing, kept in a cvar the
// overlay shows. Called with the session lock held.
inline int64_t g_next_status_ms = 0, g_next_template_check_ms = 0;
inline bool g_has_template = false;
inline std::string g_last_status;
inline void status_tick(const SessionState& s, int64_t now) {
  if (now < g_next_status_ms) return;
  g_next_status_ms = now + 1000;
  if (now >= g_next_template_check_ms) {
    g_next_template_check_ms = now + 10000;
    g_has_template = fable2::cooptemplate::ReadTemplate().has_value();
  }
  std::string st;
  switch (s.s) {
    case Session::kNone:
      st = g_has_template ? "Ready. New Game -> Co-op to host, or join a partner's lobby."
                          : "No starter save (coop_template): you can join a partner's campaign, not host one.";
      break;
    case Session::kHostStarting:
      st = "Starting the campaign with " + s.partner_name + "...";
      break;
    case Session::kHostInWorld:
      st = s.partner_spawned ? "Playing the campaign with " + s.partner_name + " (shared purse, " +
                                   s.slot_name + ")"
                             : "Campaign world up: waiting for " + s.partner_name + " to join";
      break;
    case Session::kHostPartnerLost:
      st = s.partner_name + " left: saving, then back to the main menu";
      break;
    case Session::kPartnerJoining:
      st = "Joining the host's campaign...";
      break;
  }
  if (st == g_last_status) return;
  g_last_status = st;
  rex::cvar::SetFlagByName("coop_campaign_status", st);
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
  status_tick(s, now);
  switch (s.s) {
    case Session::kHostStarting: {
      if (s.from_template) {
        // The starter save's world loads, settles, gets the lobby's gender,
        // then the partner may join (the template is past QC060's opening).
        const bool loaded = fable2::f5lua::ready() &&
                            fable2::f5lua::g_load_epoch.load() >= s.load_epoch &&
                            lobby::GetLobbyStatus().in_world;
        const int64_t settled = now - fable2::f5lua::g_load_at_ms.load();
        // T021a: the lobby's gender, by the game's own sex-change call (the potion's).
        // It is a queued action: it takes only when the hero is free, which the
        // starter's opening moments are not (launches 19, 20: one call, still male;
        // launch 21: on a free hero it took within 12 s and survived save + reload).
        // So: every 5 s while the host is not busy, read the gender and call again
        // until it reads right; the partner's go waits for it (2 min at most).
        if (loaded && settled > 10000 && !s.gender_sent) {
          if (s.host_female == s.template_female || g_gender_ok.load()) {
            s.gender_sent = true;
            if (g_gender_ok.load()) REXSYS_INFO("[coop] host start: hero gender is {}", s.host_female ? "F" : "M");
          } else if (now - s.since_ms > 180000) {
            s.gender_sent = true;
            REXSYS_WARN("[coop] host start: hero gender did not change in time; starting as the starter's");
          } else if (now - s.gender_try_ms > 5000 && fable2::coopbusy::Busy().empty()) {
            if (!s.gender_try_ms)
              REXSYS_INFO("[coop] host start: hero gender {} -> {}", s.template_female ? "F" : "M",
                          s.host_female ? "F" : "M");
            s.gender_try_ms = now;
            if (!fable2::luaconsole::RunChunk(std::string(R"lua(
local h = GetPlayerHero()
local want = )lua") + (s.host_female ? "EGender.EG_FEMALE" : "EGender.EG_MALE") + R"lua(
if Gender.Get(h) == want then
  print('[coop] hero gender ok')
elseif not Action.IsPerformingAnyAction(h) then
  print('[coop] hero gender switching: ' .. tostring(pcall(Player.ChangePlayerEntityType, h, ')lua" +
                (s.host_female ? "CreatureHeroFemale" : "CreatureHero") + R"lua(')))
end
)lua"))
              s.gender_try_ms = now - 4500;  // no free console slot: again in 0.5 s
          }
        }
        if (!loaded && now - s.since_ms > 180000) {
          // Never leave the player's save list narrowed to one slot.
          REXSYS_INFO("[coop] host start: the campaign world did not load in 3 min, giving up");
          HideCover("the campaign world did not load");
          fable2::coophero::SetCampaignPlayer2("");
          p2_watch_stop();
          rex_content_set_save_filter("");
          s.s = Session::kNone;
          fable2::coophero::g_capture_paused.store(false);
          break;
        }
        if (loaded && settled > 20000 && s.gender_sent) {
          s.s = Session::kHostInWorld;
          s.since_ms = now;
          g_past_title = false;  // the next front-end visit starts at the title
          g_seen_spawn_epoch = fable2::coophero::g_spawn_epoch.load();
          s.accept_presses = 0;
          const std::string go = fable2::cooplobby::Format(fable2::cooplobby::GoRecord{s.cid, ++s.go_seq, fable2::cooplobby::XuidStr(fable2::cooplobby::MyXuid())});
          lobby::PublishData("coop_go", go);
          s.go_ms = now;
          REXSYS_INFO("[coop] host world up (starter save {}): published {}", s.slot_name, go);
          if (!s.slot_path.empty()) p2_watch_start(s.slot_path, s.cid, s.partner_ip);
          rex_content_set_save_filter("");
          fable2::coophero::g_capture_paused.store(false);
        }
        break;
      }
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
        g_past_title = false;
        g_seen_spawn_epoch = fable2::coophero::g_spawn_epoch.load();
        s.accept_presses = 0;
        const std::string go = fable2::cooplobby::Format(fable2::cooplobby::GoRecord{s.cid, ++s.go_seq, fable2::cooplobby::XuidStr(fable2::cooplobby::MyXuid())});
        lobby::PublishData("coop_go", go);
        s.go_ms = now;
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
      if (!s.partner_spawned) {
        const int64_t approved = g_join_approved_ms.load();
        if (approved > s.since_ms) accept_tick(s, approved, now, "host");
        // The partner acknowledges the go (coop_ack). Without that, the go is
        // published again with a new seq every kGoRetryMs; a partner that is
        // gone, or no join in kPartnerJoinMs, sends the host back to the
        // campaign's lobby to wait (the partner-lost path, which saves first).
        for (const auto& d : lobby::GetPeerData("coop_ack")) {
          const auto ack = fable2::cooplobby::ParseAck(d.value);
          const uint64_t from = ack && !ack->pxuid.empty() ? std::strtoull(ack->pxuid.c_str(), nullptr, 16) : d.xuid;
          if (s.partner_xuid ? from != s.partner_xuid : d.ip != s.partner_ip) continue;
          if (ack && ack->seq > s.acked_seq && ack->seq <= s.go_seq) {
            s.acked_seq = ack->seq;
            REXSYS_INFO("[coop] host: {} acknowledged the go (seq {})", s.partner_name, ack->seq);
          }
        }
        const PeerView pv = peer_view(s.partner_xuid, s.partner_ip);
        if (s.acked_seq < s.go_seq && s.go_ms && now - s.go_ms > kGoRetryMs && approved <= s.since_ms) {
          const std::string go = fable2::cooplobby::Format(fable2::cooplobby::GoRecord{s.cid, ++s.go_seq, fable2::cooplobby::XuidStr(fable2::cooplobby::MyXuid())});
          lobby::PublishData("coop_go", go);
          s.go_ms = now;
          REXSYS_INFO("[coop] host: no answer to the go yet, published again: {}", go);
        }
        const bool gone = pv.quiet < 0 || pv.quiet > 20;
        if ((gone || now - s.since_ms > kPartnerJoinMs) && approved <= s.since_ms) {
          REXSYS_INFO("[coop] host: {} did not join ({}), back to the campaign lobby", s.partner_name,
                      gone ? "partner gone" : "timed out");
          ShowCoverMessage(s.partner_name + " could not join",
                           "Saving, then back to the main menu (Co-op to try again)", 10000);
          s.s = Session::kHostPartnerLost;
          s.since_ms = now;
          s.quit_sent = false;
          fable2::luaconsole::RunChunkOrKeep(
              "print('[coop] AutoSaveGame: ' .. tostring(pcall(AutoSaveGame, true, true)))");
        }
        break;
      }
      p2_watch_tick(now, true);
      purse_tick(now);
      const int quiet = peer_view(s.partner_xuid, s.partner_ip).quiet;
      if (quiet < 0 || quiet > 6) {
        // Partner lost (proof P6): save with the game's own autosave call
        // (QC060 uses AutoSaveGame(true, true)), then, once it has had time
        // to finish, back to the front end to wait in the campaign's lobby.
        REXSYS_INFO("[coop] host: partner {} lost (quiet {} s), saving", s.partner_name, quiet);
        lobby::PublishData("coop_purse", "");
        s.s = Session::kHostPartnerLost;
        s.since_ms = now;
        s.quit_sent = false;
        fable2::luaconsole::RunChunkOrKeep(
            "print('[coop] AutoSaveGame: ' .. tostring(pcall(AutoSaveGame, true, true)))");
      }
      break;
    }
    case Session::kHostPartnerLost:
      if (!s.quit_sent) p2_watch_tick(now, false);  // the autosave: player 2 from the cache
      if (!s.quit_sent && now - s.since_ms > 10000) {
        s.quit_sent = true;
        REXSYS_INFO("[coop] host: returning to the front end");
        fable2::luaconsole::RunChunkOrKeep(
            "print('[coop] QuitToFrontEnd: ' .. tostring(pcall(Debug.QuitToFrontEnd)))");
      }
      // Back at the front end: the main menu, not a lobby (the proof run of
      // 2026-10-24 opened the lobby while the front end was still loading; the
      // state probe stays "MainMenu" in the world, so wait well past the quit).
      // The host continues later through Co-op -> Host: continue a co-op save.
      if (s.quit_sent && !lobby::GetLobbyStatus().in_world && now - s.since_ms > 30000) {
        REXSYS_INFO("[coop] host: back at the front end; {} can rejoin through Co-op", s.partner_name);
        fable2::coophero::SetCampaignPlayer2("");
        p2_watch_stop();
        s.s = Session::kNone;
        after = [partner = s.partner_name] {
          ShowCoverMessage(partner + " left the campaign",
                           "The game was saved. To play on: Co-op -> Host: continue a co-op save", 12000);
        };
      }
      break;
    case Session::kPartnerJoining: {
      // Joined only counts once it holds past the join screens (a refused join
      // shows the joined state for a few milliseconds).
      if (lobby::GetLobbyStatus().joined_host_ip) {
        if (!s.joined_since_ms) {
          s.joined_since_ms = now;
          s.accept_presses = 0;
        }
        accept_tick(s, s.joined_since_ms, now, "partner");
        if (now - s.joined_since_ms > kAcceptFirstMs + kAcceptPresses * kAcceptEveryMs) {
          REXSYS_INFO("[coop] partner: in the host's world (after {} attempt(s))", s.tries);
          g_past_title = false;
          s.s = Session::kNone;
          g_armed = false;  // in the host's world: pause-menu presses are not New Game
        }
        break;
      }
      s.joined_since_ms = 0;
      const PeerView hv = peer_view(s.host_xuid, s.host_ip);
      const bool host_gone = hv.quiet < 0 || hv.quiet > 20;
      if (host_gone || now - s.since_ms > kPartnerJoinMs) {
        REXSYS_INFO("[coop] partner: no join ({}, {} attempt(s)), giving up", host_gone ? "host gone" : "timed out",
                    s.tries);
        s.s = Session::kNone;
        ShowCoverMessage("Could not join " + s.host_name + "'s game",
                         host_gone ? "The host left. Back to the main menu."
                                   : "No answer from the host. Back to the main menu: try New Game -> Co-op again.",
                         10000);
        break;
      }
      if (now - s.since_ms < REXCVAR_GET(coop_partner_join_delay_ms)) break;  // rig only
      if (hv.joinable && now - s.last_try_ms > 15000) {
        s.last_try_ms = now;
        ++s.tries;
        s.host_ip = hv.ip;
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

// The press hook. Returns true to swallow the press.
// T018a discovery (rig only, log only): the dispatch's registers and the first
// 96 bytes of its r3 element, to find the menu's real focused-row field.
inline void probe_menu_press(PPCContext& ctx, uint8_t* base) {
  const uint32_t el = ctx.r3.u32;
  std::string words;
  if (el >= 0x10000 && el < 0xFFFFFF00u) {
    char w[10];
    for (uint32_t off = 0; off < 96; off += 4) {
      const uint8_t* p = base + el + off;
      std::snprintf(w, sizeof(w), "%02X%02X%02X%02X ", p[0], p[1], p[2], p[3]);
      words += w;
    }
  }
  REXSYS_INFO("[menuprobe] mask={:04X} row={} r3={:08X} r4={:08X} r6={:08X} el={}", ctx.r5.u32 & 0xFFFF,
              g_row, el, ctx.r4.u32, ctx.r6.u32, words);
}

inline bool OnPress(PPCContext& ctx, uint8_t* base) {
  if (REXCVAR_GET(coop_menu_probe) && in_main_menu()) probe_menu_press(ctx, base);
  if (!REXCVAR_GET(coop_campaign)) return false;
  // While the chooser or lobby is open the panel owns the pad; the stock
  // menu sees nothing (its state is idle, this catches stragglers).
  if (fable2::cooplobby::g_open.load()) return true;
  {
    // Partner joining the host: the game's own join prompts need the pad
    // (launch 6: an A meant for the prompt opened the chooser instead).
    std::lock_guard<std::mutex> l(S().m);
    if (S().s == Session::kPartnerJoining) return false;
    // In the world the pause menu uses this same dispatch and the state probe
    // can still say MainMenu (launch 16: a pause-menu A opened the chooser).
    if (S().s == Session::kHostInWorld || S().s == Session::kHostPartnerLost) return false;
  }
  const auto st = lobby::GetLobbyStatus();
  if (!in_main_menu() || st.in_world || st.joined_host_ip) {
    g_past_title = false;  // left the front end: the next visit starts at the title
    return false;
  }

  const uint32_t mask = ctx.r5.u32 & 0xFFFF;
  if (mask & kA) {
    // The host's Start: load the campaign save with the game's own Continue
    // ("Load Game"), here on the game thread; the press that brought us here
    // was injected for this and goes no further. Before the title logic: the
    // menu is open (final smoke test, 2026-10-24: the press was taken for the
    // title's and reopened the hub).
    if (g_load_pending.exchange(false)) {
      swallow_closing_a();
      g_past_title = true;
      if (g_request_load && g_request_load(ctx, base)) return true;
      REXSYS_WARN("[coop] host start: main menu not known, steering to Continue instead");
      inject_continue(300);
      return true;
    }
  }
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
    // New Game and Continue are the stock game's again (Josh, 2026-10-24: co-op
    // only behind the main menu's Co-op entry, fable2_coop_mainmenu.h).
    return false;
  }
  if (repeat) return false;
  // The B that closed a panel reaches the menu right after it closes (launch 23:
  // it reset the mirrored row to New Game while the menu's cursor stayed on
  // Continue, so the next A on Continue opened the chooser): swallow it.
  if ((mask & (kBack | kB)) && now < g_swallow_a_until) return true;
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
// Testing only (FABLE2_TEST_INPUT=1 in the environment, set by
// tools/coop_ship_e2e.py; never by play-coop.cmd): commands from
// <exe dir>\test_input.txt, read and emptied each frame, so proofs do not depend
// on which window is in front (2026-10-25: focus loss and key repeat at 60 fps
// broke two runs). Command language: fable2_coop_test_input.h.
inline void test_input_tick() {
  static const bool on = std::getenv("FABLE2_TEST_INPUT") != nullptr;
  if (!on) return;
  static int64_t next = 0;
  const int64_t now = fable2::f5lua::steady_ms();
  if (now < next) return;
  next = now + 50;
  const auto path = fable2::cooptemplate::exe_dir() / "test_input.txt";
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) == 0) return;
  std::string text;
  {
    std::ifstream f(path, std::ios::binary);
    text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  }
  std::ofstream(path, std::ios::binary | std::ios::trunc).close();
  namespace ti = fable2::cooptestinput;
  std::istringstream lines(text);
  std::string line;
  int64_t delay = 0;  // presses in one read run one after the other
  while (std::getline(lines, line)) {
    const ti::Command c = ti::Parse(line);
    switch (c.kind) {
      case ti::Kind::kLua:
        fable2::luaconsole::RunChunkOrKeep(c.text);
        break;
      case ti::Kind::kCvar:
        rex::cvar::SetFlagByName(c.text, c.value_text);
        break;
      case ti::Kind::kPress:
        fable2::cooplobby::InjectPress(c.mask, delay, c.ms);
        delay += c.ms + 150;
        break;
      case ti::Kind::kStick:
        fable2::cooplobby::InjectAnalog(c.side == 'L' ? 'l' : 'r', ti::StickValue(c.x), ti::StickValue(c.y), 0, 0,
                                        c.ms);
        break;
      case ti::Kind::kTrigger:
        fable2::cooplobby::InjectAnalog(c.side, 0, 0, ti::TriggerValue(c.value), 0, c.ms);
        break;
      case ti::Kind::kNone:
        continue;
    }
    REXSYS_INFO("[test-input] {}", line);
  }
}

inline void OnFrame() {
  if (g_prev_frame_hook) g_prev_frame_hook();
  // Rig only: narrow the save list to one slot so Continue loads it with no
  // picker (launch 21: test a starter slot without a campaign lobby).
  static std::string s_rig_filter;
  if (const std::string f = REXCVAR_GET(coop_rig_save_filter); f != s_rig_filter) {
    s_rig_filter = f;
    rex_content_set_save_filter(f.c_str());
  }
  if (!REXCVAR_GET(coop_campaign)) return;
  static const bool font_set = [] {
    // The menu panel draws with the game's own font, from the player's data.
    const auto bft = fable2::cooptemplate::exe_dir() / "data" / "language" / "en-uk" / "fonts" / "gui_large.bft";
    rex_ui_set_lobby_font(bft.string().c_str());
    return true;
  }();
  (void)font_set;
  cover_tick();
  static const bool backup_shown = (fable2::coopbackup::LoadLast(), true);  // F6, once
  (void)backup_shown;
  fable2::coopbackup::EnsureListener();  // the partner side of campaign backups (T039)
  // A quick tap released before the next poll: the closing A never reached
  // the dispatch, so send one.
  if (g_pass_a > 0 && g_pass_deadline && fable2::f5lua::steady_ms() > g_pass_deadline) {
    g_pass_deadline = 0;
    REXSYS_INFO("[coop] new game: closing A did not arrive, injecting one");
    fable2::cooplobby::InjectPress(static_cast<uint16_t>(fable2::cooplobby::kA), 0, 200);
  }
  session_tick();
  partner_purse_tick(fable2::f5lua::steady_ms());
  test_input_tick();
  fable2::luaconsole::RetryKept();
}

inline const bool g_installed = [] {
  fable2::fronthooks::g_press_hook = &OnPress;
  fable2::luaconsole::AddLineHandler("[coop] hero gender ok", [](std::string_view) { g_gender_ok.store(true); });
  fable2::luaconsole::AddLineHandler("COOPPURSE:", on_purse_line);
  fable2::luaconsole::AddLineHandler("COOPP2W|", [](std::string_view l) { on_p2_line(l, true); });
  fable2::luaconsole::AddLineHandler("COOPP2W-NONE", [](std::string_view) { on_p2_none(true); });
  fable2::luaconsole::AddLineHandler("COOPP2|", [](std::string_view l) { on_p2_line(l, false); });
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
