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
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <functional>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "fable2_coop_campaign.h"
#include "fable2_coop_lobby.h"
#include "fable2_coop_backup.h"
#include "fable2_coop_p2file.h"
#include "fable2_coop_template.h"
#include "fable2_front_hooks.h"
#include "../diagnostics/fable2_state_probe.h"

// rexglue-sdk: narrow the game's save list to one slot ("" = all saves).
extern "C" void rex_content_set_save_filter(const char* file_name);

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

inline void open_chooser();
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

inline void on_solo() {
  fable2::coophero::SetCampaignPlayer2("");  // a solo game has no campaign player 2
  g_partner_xuid.store(0);
  p2_watch_stop();
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
// The host hero reads the lobby's gender ("[coop] hero gender ok", T021a).
inline std::atomic<bool> g_gender_ok{false};
// The game's own join screens (gold/XP split on the host; look, then "continue
// without choosing a save" on the partner) all accept with A (launch 13). During a
// campaign A is pressed for both, every kAcceptEveryMs, at most kAcceptPresses times.
constexpr int64_t kAcceptFirstMs = 3000, kAcceptEveryMs = 3000;
constexpr int kAcceptPresses = 5;
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
        m.partner_name = rec.partner;
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
    uint64_t pxuid = 0;
    for (const auto& p : lobby::GetLanPeers())
      if (p.ip == lb.host.partner_ip) pxuid = p.xuid;
    g_partner_xuid.store(pxuid);
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
  if (S().from_template) {
    // The game's own Continue loads the campaign slot: while the save list
    // holds only that slot there is no save picker (with several saves the
    // picker came up, launch 12) and no way to load the wrong hero. The
    // panel's closing A is swallowed, then the cursor goes to Continue and A.
    rex_content_set_save_filter(slot->name.c_str());
    swallow_closing_a();
    inject_continue(700);
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
  if (kind == fable2::cooplobby::Kind::kContinue) {
    cb.back = [] {  // T025: Back from a campaign's lobby returns to the menu
      REXSYS_INFO("[coop] campaign lobby closed, back to the menu");
      swallow_closing_a();
    };
  } else {
    cb.back = [] {
      REXSYS_INFO("[coop] lobby closed, back to the chooser");
      swallow_closing_a();
      open_chooser();
    };
  }
  fable2::cooplobby::OpenHost(kind, cid, partner, cb);
}

inline void on_coop() {
  REXSYS_INFO("[coop] new game: Co-op, opening the host lobby");
  open_host_lobby(fable2::cooplobby::Kind::kNew, "", "");
}

// One A for the game's join screens if one is due: the first kAcceptFirstMs after
// `since` (the join), then every kAcceptEveryMs, kAcceptPresses in all.
inline void accept_tick(SessionState& s, int64_t since, int64_t now, const char* who) {
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

inline void request_p2_capture(const char* tag) {
  const std::string lib = fable2::coophero::read_lib();
  if (lib.empty()) return;
  fable2::luaconsole::RunChunk(
      lib + "\nlocal h = GetPlayerHenchman()\n"
            "if h and h:IsAlive() then print('" + tag +
      "|' .. CoopHero.SerializeV2(CoopHero.Capture(h), 0))\n"
      "else print('" + tag + "-NONE') end\n");
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
    request_p2_capture("COOPP2W");  // written by the line handler
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
constexpr int64_t kPurseEveryMs = 1000;
inline int64_t g_next_purse_ms = 0;
inline uint32_t g_purse_epoch = ~0u;

inline void purse_tick(int64_t now) {
  if (now < g_next_purse_ms) return;
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
  return
end
if H == CoopPurseLast and h == CoopPurseLast then return end
local total = CoopPurseLast + (H - CoopPurseLast) + (h - CoopPurseLast)
Money.Set(hero, total)
Money.Set(hench, total)
print(string.format('[coop] purse: host %d, player 2 %d, was %d -> total %d (now %d / %d)',
  H, h, CoopPurseLast, total, Money.Get(hero), Money.Get(hench)))
CoopPurseLast = total
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
      st = s.partner_name + " left: saving, then back to the campaign lobby";
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
            fable2::luaconsole::RunChunk(std::string(R"lua(
local h = GetPlayerHero()
local want = )lua") + (s.host_female ? "EGender.EG_FEMALE" : "EGender.EG_MALE") + R"lua(
if Gender.Get(h) == want then
  print('[coop] hero gender ok')
elseif not Action.IsPerformingAnyAction(h) then
  print('[coop] hero gender switching: ' .. tostring(pcall(Player.ChangePlayerEntityType, h, ')lua" +
                (s.host_female ? "CreatureHeroFemale" : "CreatureHero") + R"lua(')))
end
)lua");
          }
        }
        if (!loaded && now - s.since_ms > 180000) {
          // Never leave the player's save list narrowed to one slot.
          REXSYS_INFO("[coop] host start: the campaign world did not load in 3 min, giving up");
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
          g_seen_spawn_epoch = fable2::coophero::g_spawn_epoch.load();
          s.accept_presses = 0;
          const std::string go = fable2::cooplobby::Format(fable2::cooplobby::GoRecord{s.cid, ++s.go_seq});
          lobby::PublishData("coop_go", go);
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
        g_seen_spawn_epoch = fable2::coophero::g_spawn_epoch.load();
        s.accept_presses = 0;
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
      if (!s.partner_spawned) {
        const int64_t approved = g_join_approved_ms.load();
        if (approved > s.since_ms) accept_tick(s, approved, now, "host");
        break;
      }
      p2_watch_tick(now, true);
      purse_tick(now);
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
      if (!s.quit_sent) p2_watch_tick(now, false);  // the autosave: player 2 from the cache
      if (!s.quit_sent && now - s.since_ms > 10000) {
        s.quit_sent = true;
        REXSYS_INFO("[coop] host: returning to the front end");
        fable2::luaconsole::RunChunk(
            "print('[coop] QuitToFrontEnd: ' .. tostring(pcall(Debug.QuitToFrontEnd)))");
      }
      if (s.quit_sent && !lobby::GetLobbyStatus().in_world && in_main_menu() && now - s.since_ms > 13000) {
        REXSYS_INFO("[coop] host: back at the front end, waiting for {} in the lobby", s.partner_name);
        fable2::coophero::SetCampaignPlayer2("");
        p2_watch_stop();
        s.s = Session::kNone;
        after = [cid = s.cid, partner = s.partner_name] {
          open_host_lobby(fable2::cooplobby::Kind::kContinue, cid, partner);
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
      if (now - s.since_ms < REXCVAR_GET(coop_partner_join_delay_ms)) break;  // rig only
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
    if (repeat)  // second call of a swallowed press
      return g_armed && (g_row == kNewGameRow || (g_row == kContinueRow && g_continue_swallowed));
    if (g_row == kNewGameRow && g_armed) {
      REXSYS_INFO("[coop] new game: A on New Game, showing Solo / Co-op");
      open_chooser();
      return true;
    }
    if (g_row == kContinueRow && g_armed) {
      // T025: Continue on a campaign save opens that campaign's lobby instead of
      // loading it alone; any other save loads as usual. Start's own A on
      // Continue (kHostStarting) passes.
      {
        std::lock_guard<std::mutex> l(S().m);
        if (S().s == Session::kHostStarting) return false;
      }
      const auto newest = fable2::cooptemplate::NewestSlot();
      const auto m = newest ? fable2::coopcampaign::ReadMarker(newest->path) : std::nullopt;
      g_continue_swallowed = m.has_value();
      if (m) {
        REXSYS_INFO("[coop] continue: {} is campaign {} with {}, opening its lobby", newest->name,
                    m->campaign_id, m->partner_name);
        open_host_lobby(fable2::cooplobby::Kind::kContinue, m->campaign_id, m->partner_name);
        return true;
      }
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
  // Rig only: narrow the save list to one slot so Continue loads it with no
  // picker (launch 21: test a starter slot without a campaign lobby).
  static std::string s_rig_filter;
  if (const std::string f = REXCVAR_GET(coop_rig_save_filter); f != s_rig_filter) {
    s_rig_filter = f;
    rex_content_set_save_filter(f.c_str());
  }
  if (!REXCVAR_GET(coop_campaign)) return;
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
}

inline const bool g_installed = [] {
  fable2::fronthooks::g_press_hook = &OnPress;
  fable2::luaconsole::AddLineHandler("[coop] hero gender ok", [](std::string_view) { g_gender_ok.store(true); });
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
