// fable_2 - co-op campaign lobby overlay (specs/001-coop-campaign, T019).
//
// A small panel over the front end (drawn by the SDK, rex/ui/overlay/lobby_panel.h), driven by the pad (or the keyboard
// pad map): Up/Down move, A selects, Left/Right change an option, X toggles
// Ready, B goes back. While it is open the game sees no pad input, so the main
// menu underneath stays where it was.
//
// The protocol and state machine are in fable2_coop_lobby_proto.h; this file
// moves records between them and the LAN lobby's keyed data
// (coop_lobby / coop_seat / coop_go, contracts/lobby-protocol.md) and draws.
//
// The front end (fable2_coop_frontend.h) opens it:
//   OpenHost(kind, cid, partner)  New Game -> Co-op, or Continue on a campaign
//   OpenPartner()                 the partner's New Game/Continue -> Co-op
// and is told what happened through the callbacks below.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/kernel/xam/lan_lobby.h>
#include <rex/logging.h>
#include <rex/ui/overlay/lobby_panel.h>  // rex_ui_set_lobby_view

#include "fable2_coop_camera.h"  // fable2::coopcam::input_filter (chained)
#include "fable2_coop_campaign.h"
#include "fable2_coop_lobby_proto.h"

REXCVAR_DEFINE_STRING(coop_lobby_open, "", "Network",
                      "Testing: set to 'host' or 'partner' to open the co-op campaign lobby "
                      "without the main menu (needs coop_campaign); cleared when it opens");

namespace fable2::cooplobby {

namespace lobby = rex::kernel::xam::lobby;

// kChoose: (old) New Game -> Solo / Co-op; kPartner browsing = the Co-op hub;
// kSaves: the hub's "Host: continue a co-op save" list.
enum class Role { kClosed, kChoose, kHost, kPartner, kSaves };

// Called on the render thread; must not call guest code directly (queue Lua
// or set flags instead).
struct Callbacks {
  std::function<void()> solo;                // chooser: Solo picked
  std::function<void()> coop;                // chooser: Co-op picked
  std::function<void()> host_start;          // host pressed Start: run the original New Game / load
  std::function<void(uint32_t)> partner_go;  // coop_go arrived: join the host at this ip
  std::function<void()> back;                // lobby closed with Back / Leave
  std::function<void()> host_new;            // hub: "Host: new co-op campaign"
  std::function<void()> host_saves;          // hub: "Host: continue a co-op save"
  std::function<void(size_t)> pick_save;     // save list: the chosen campaign save
  // Partner: the host pressed Start (its lobby says Starting): the joining card
  // goes up at once (Josh, 2026-10-24). Failed: the host left or never got its
  // world up (host name, "left" / "timeout").
  std::function<void(std::string)> partner_wait;
  std::function<void(std::string, std::string)> partner_wait_failed;
};

struct Lobby {
  std::vector<std::string> saves;  // kSaves: one label per campaign save
  std::mutex m;
  Role role = Role::kClosed;
  HostLobby host;
  PartnerLobby partner;
  std::vector<PartnerLobby::Listing> listing;  // partner, browsing
  std::vector<PartnerLobby::Listing> others;   // host without a partner: lobbies to join instead
  int cursor = 0;
  std::string notice;  // one line shown under the title
  Callbacks cb;
  std::optional<GoRecord> go;  // host: published coop_go
};

inline Lobby& L() {
  static Lobby l;
  return l;
}
inline std::atomic<bool> g_open{false};

// Display only (players are matched by XUID): the gamertag, or the PC's name
// when it is blank (Calvin's was, 2026-10-24).
inline std::string MyName() {
  std::string n = rex::cvar::GetFlagByName("profile_gamertag");
  if (n.empty())
    if (const char* pc = std::getenv("COMPUTERNAME")) n = pc;
  return n.empty() ? "Player" : n;
}
inline uint64_t MyXuid() { return lobby::GetLobbyStatus().online_xuid; }

// --- open / close ------------------------------------------------------------

inline void OpenHost(Kind kind, const std::string& cid, const std::string& partner, Callbacks cb,
                     const std::string& partner_xuid = "") {
  std::lock_guard<std::mutex> l(L().m);
  auto& lb = L();
  lobby::PublishData("coop_ack", "");
  lb.role = Role::kHost;
  lb.host = HostLobby{};
  lb.host.rec.kind = kind;
  lb.host.rec.cid = cid;
  lb.host.rec.partner = partner;
  lb.host.rec.pxuid = kind == Kind::kContinue ? partner_xuid : "";
  lb.host.rec.host = MyName();
  lb.host.rec.hxuid = XuidStr(MyXuid());
  lb.cursor = 0;
  lb.go.reset();
  lb.notice = kind == Kind::kNew ? "Waiting for a partner" : "Waiting for " + partner;
  lb.cb = std::move(cb);
  g_open.store(true);
  REXSYS_INFO("[lobby] open host kind={} cid={} partner={}", kind == Kind::kNew ? "new" : "continue",
              cid, partner);
}

// The partner's list of lobbies. auto_join: join a campaign that is waiting
// for this player as soon as it is heard (the partner's Continue).
inline bool g_auto_join = false;
inline bool g_hub_has_saves = false;  // the hub offers "Host: continue a co-op save"
inline void OpenPartner(Callbacks cb, bool auto_join = false, bool has_saves = false) {
  std::lock_guard<std::mutex> l(L().m);
  auto& lb = L();
  lobby::PublishData("coop_lobby", "");
  lobby::PublishData("coop_ack", "");
  lb.role = Role::kPartner;
  lb.partner = PartnerLobby{};
  lb.partner.my_name = MyName();
  lb.partner.my_xuid = MyXuid();
  g_auto_join = auto_join;
  g_hub_has_saves = has_saves;
  lb.cursor = 0;
  lb.notice = auto_join ? "Looking for your co-op campaign" : "Join a lobby on your network, or host one";
  lb.cb = std::move(cb);
  g_open.store(true);
  REXSYS_INFO("[lobby] open partner as {}", lb.partner.my_name);
}

// New Game -> "Solo or Co-op?" (T018). Publishes nothing.
inline void OpenChooser(Callbacks cb) {
  std::lock_guard<std::mutex> l(L().m);
  auto& lb = L();
  lb.role = Role::kChoose;
  lb.cursor = 0;
  lb.notice = "Play this new game alone, or start a co-op campaign with a partner.";
  lb.cb = std::move(cb);
  g_open.store(true);
  REXSYS_INFO("[lobby] open chooser");
}

// The hub's "Host: continue a co-op save": one row per campaign save (labels
// from the caller, newest first). Publishes nothing.
inline void OpenSaves(std::vector<std::string> labels, Callbacks cb) {
  std::lock_guard<std::mutex> l(L().m);
  auto& lb = L();
  lb.role = Role::kSaves;
  lb.saves = std::move(labels);
  lb.cursor = 0;
  lb.notice = "Pick the campaign to host";
  lb.cb = std::move(cb);
  g_open.store(true);
  REXSYS_INFO("[lobby] open saves ({})", lb.saves.size());
}

// Clears this PC's published keys (contract rule 5) and hides the window.
// Caller holds L().m.
inline void close_locked(bool keep_go) {
  lobby::PublishData("coop_lobby", "");
  lobby::PublishData("coop_seat", "");
  if (!keep_go) lobby::PublishData("coop_go", "");
  L().role = Role::kClosed;
  g_open.store(false);
}

inline void Close() {
  std::lock_guard<std::mutex> l(L().m);
  close_locked(false);
}

// Host after Start: hide the panel but keep publishing the lobby record
// (state Starting), so the seated partner keeps waiting for coop_go.
inline void HidePanel() {
  std::lock_guard<std::mutex> l(L().m);
  L().role = Role::kClosed;
  g_open.store(false);
}

// Host, once its world is loaded and the session is up: publish coop_go.
inline void HostWorldReady(const std::string& cid) {
  std::lock_guard<std::mutex> l(L().m);
  if (L().role != Role::kHost) return;
  L().go = L().host.Go(cid);
  lobby::PublishData("coop_go", Format(*L().go));
  lobby::PublishData("coop_lobby", Format(L().host.rec));
  REXSYS_INFO("[lobby] coop_go {}", Format(*L().go));
}

// --- per-frame tick (render thread) ---------------------------------------------

inline std::vector<Heard> heard(const char* key) {
  std::vector<Heard> out;
  for (const auto& d : lobby::GetPeerData(key))
    if (!d.value.empty()) {
      const uint64_t own = SenderXuid(d.value);  // the record says who sent it
      out.push_back({d.ip, d.gamertag, d.value, d.seconds_since_received, own ? own : d.xuid});
    }
  return out;
}

// Lobbies this player could join right now (New Game -> Co-op shows them first).
inline std::vector<PartnerLobby::Listing> JoinableNow() {
  PartnerLobby me;
  me.my_name = MyName();
  me.my_xuid = MyXuid();
  return me.JoinableOthers(heard("coop_lobby"));
}

inline int64_t g_next_tick_ms = 0;
inline int64_t g_wait_since_ms = 0;  // partner: when the host's Start was seen
constexpr int64_t kHostWorldWaitMs = 180000;

// One lobby step under the lock. Returns a callback to run after unlocking:
// callbacks may open or close the lobby, which takes the same lock (launch 5:
// partner_go called Close() while Tick held the lock and the game died).
inline std::function<void()> tick_locked() {
  auto& lb = L();
  if (lb.role == Role::kHost) {
    if (lb.host.rec.hxuid.empty()) lb.host.rec.hxuid = XuidStr(MyXuid());
    lb.host.OnSeats(heard("coop_seat"));
    if (!lb.host.partner_ip) {
      PartnerLobby me;
      me.my_name = lb.host.rec.host;
      me.my_xuid = MyXuid();
      lb.others = me.JoinableOthers(heard("coop_lobby"));
    } else {
      lb.others.clear();
    }
    lb.host.partner_proto_ok =
        lb.host.partner_ip && fable2::coopcampaign::PeerSupportsCampaign(lb.host.partner_ip);
    if (lb.host.partner_ip && !lb.host.partner_proto_ok)
      lb.notice = lb.host.rec.partner + " needs the co-op campaign update (v14)";
    else if (lb.host.partner_ip)
      lb.notice = lb.host.rec.partner + (lb.host.partner_ready ? " is ready" : " joined");
    else if (!lb.host.left.empty()) {
      lb.notice = lb.host.left + " left the lobby. Waiting for a partner";
      REXSYS_INFO("[lobby] {} left the lobby", lb.host.left);
      lb.host.left.clear();
    }
    lobby::PublishData("coop_lobby", Format(lb.host.rec));
  } else if (lb.role == Role::kPartner) {
    if (lb.partner.state == PartnerState::kBrowsing) {
      lb.listing = lb.partner.List(heard("coop_lobby"));
      lobby::PublishData("coop_seat", "");
      if (g_auto_join) {
        for (const auto& g : lb.listing)
          if (g.joinable && g.rec.kind == Kind::kContinue) {
            lb.partner.Join(g);
            lb.cursor = 0;
            lb.notice = "Joined " + g.rec.host + "'s campaign";
            REXSYS_INFO("[lobby] continue: joining {}'s campaign {}", g.rec.host, g.rec.cid);
            break;
          }
      }
      return {};
    }
    const auto rec = lb.partner.FromHost(heard("coop_lobby"));
    const auto go = lb.partner.FromHost(heard("coop_go"));
    const PartnerState was = lb.partner.state;
    const std::string host_name = lb.partner.seat.host;
    const bool start_join = lb.partner.OnHost(rec, go);
    const int64_t now = fable2::f5lua::steady_ms();
    if (was == PartnerState::kWaitingForHostWorld && lb.partner.state == PartnerState::kWaitingForHostWorld &&
        now - g_wait_since_ms > kHostWorldWaitMs) {
      REXSYS_INFO("[lobby] partner: {}'s world did not come up in {} s", host_name, kHostWorldWaitMs / 1000);
      lb.partner.Leave();
      lb.notice = "No answer from " + host_name;
      lb.cursor = 0;
      if (lb.cb.partner_wait_failed) return [cb = lb.cb.partner_wait_failed, host_name] { cb(host_name, "timeout"); };
      return {};
    }
    if (lb.partner.state == PartnerState::kBrowsing) {
      lb.notice = "The host left the lobby";
      REXSYS_INFO("[lobby] partner: the host left the lobby");
      lb.cursor = 0;
      if (was == PartnerState::kWaitingForHostWorld && lb.cb.partner_wait_failed)
        return [cb = lb.cb.partner_wait_failed, host_name] { cb(host_name, "left"); };
      return {};
    }
    lobby::PublishData("coop_seat", Format(lb.partner.seat));
    if (lb.partner.state == PartnerState::kWaitingForHostWorld && was != PartnerState::kWaitingForHostWorld) {
      g_wait_since_ms = now;
      REXSYS_INFO("[lobby] partner: {} started the campaign, waiting for the host's world", host_name);
      if (lb.cb.partner_wait) return [cb = lb.cb.partner_wait, host_name] { cb(host_name); };
    }
    if (start_join) {
      const uint32_t ip = lb.partner.host_ip;
      lobby::PublishData("coop_ack", Format(AckRecord{lb.partner.seat.hxuid, lb.partner.last_go_seq,
                                                      XuidStr(lb.partner.my_xuid)}));
      REXSYS_INFO("[lobby] coop_go received (seq {}), joining {:08X}", lb.partner.last_go_seq, ip);
      lb.notice = "Joining " + lb.partner.seat.host;
      if (lb.cb.partner_go) return [cb = lb.cb.partner_go, ip] { cb(ip); };
    }
  }
  return {};
}

inline void Tick() {
  if (!g_open.load()) return;
  const int64_t now = fable2::f5lua::steady_ms();
  if (now < g_next_tick_ms) return;
  g_next_tick_ms = now + 250;
  std::function<void()> after;
  {
    std::lock_guard<std::mutex> l(L().m);
    after = tick_locked();
  }
  if (after) after();
}

// --- menu rows -------------------------------------------------------------------

enum class Row { kChildhood, kReady, kStart, kBack, kGame, kGender, kLeave, kSolo, kCoop, kJoinOther, kHostGender, kHostNew,
                 kHostSaves, kSave };

struct RowItem {
  Row row;
  std::string label;
  bool enabled = true;
  size_t index = 0;  // kGame: listing index
};

// Caller holds L().m.
inline std::vector<RowItem> rows_locked() {
  auto& lb = L();
  std::vector<RowItem> r;
  if (lb.role == Role::kSaves) {
    for (size_t i = 0; i < lb.saves.size(); ++i) r.push_back({Row::kSave, lb.saves[i], true, i});
    r.push_back({Row::kBack, "Back"});
  } else if (lb.role == Role::kChoose) {
    r.push_back({Row::kSolo, "Solo"});
    r.push_back({Row::kCoop, "Co-op"});
    r.push_back({Row::kBack, "Back"});
  } else if (lb.role == Role::kHost) {
    const auto& rec = lb.host.rec;
    for (size_t i = 0; i < lb.others.size(); ++i)
      r.push_back({Row::kJoinOther, "Join " + lb.others[i].rec.host + "'s co-op campaign", true, i});
    if (rec.kind == Kind::kNew) {
      r.push_back({Row::kHostGender, std::string("Your hero: ") +
                                         (rec.gender1 == Gender::kFemale ? "Female" : "Male")});
      r.push_back({Row::kChildhood, "Childhood: Skip to adulthood"});
    }
    r.push_back({Row::kReady, std::string("Ready: ") + (rec.hready ? "Yes" : "No")});
    r.push_back({Row::kStart, "Start", lb.host.state == HostState::kBothReady && lb.host.partner_proto_ok});
    r.push_back({Row::kBack, "Back"});
  } else if (lb.role == Role::kPartner) {
    if (lb.partner.state == PartnerState::kBrowsing) {
      for (size_t i = 0; i < lb.listing.size(); ++i) {
        const auto& g = lb.listing[i];
        std::string label = g.rec.kind == Kind::kNew ? "Join " + g.rec.host + "'s co-op campaign"
                                                     : "Continue " + g.rec.host + "'s campaign with you";
        if (!g.joinable) label += " (not for you)";
        r.push_back({Row::kGame, label, g.joinable, i});
      }
      if (!g_auto_join) {
        if (g_hub_has_saves) r.push_back({Row::kHostSaves, "Host: continue a co-op save"});
        r.push_back({Row::kHostNew, "Host: new co-op campaign"});
      }
      r.push_back({Row::kBack, "Back"});
    } else {
      const auto& s = lb.partner.seat;
      const bool locked = lb.partner.state != PartnerState::kJoined;
      r.push_back({Row::kGender, std::string("Your hero: ") +
                                     (s.gender == Gender::kFemale ? "Female" : "Male"),
                   !locked && s.cid == "new"});
      r.push_back({Row::kReady, std::string("Ready: ") + (s.ready ? "Yes" : "No")});
      r.push_back({Row::kLeave, "Leave"});
    }
  }
  return r;
}

// One pad action on the lobby. Caller holds L().m. Returns the callback to
// run after unlocking (Back / Start), if any.
inline std::function<void()> act_locked(int dy, int dx, bool a, bool b, bool x) {
  auto& lb = L();
  auto r = rows_locked();
  if (r.empty()) return {};
  lb.cursor = std::clamp(lb.cursor + dy, 0, static_cast<int>(r.size()) - 1);
  const RowItem& it = r[lb.cursor];
  if (b) {
    if (lb.role == Role::kPartner && lb.partner.state != PartnerState::kBrowsing) {
      lb.partner.Leave();
      lb.cursor = 0;
      return {};
    }
    close_locked(false);
    return lb.cb.back;
  }
  if (x && lb.role == Role::kChoose) return {};
  if (x) {
    if (lb.role == Role::kHost) lb.host.rec.hready = !lb.host.rec.hready;
    else if (lb.partner.state != PartnerState::kBrowsing) lb.partner.SetReady(!lb.partner.seat.ready);
    return {};
  }
  if (!a && !dx) return {};
  if (!it.enabled) return {};
  switch (it.row) {
    case Row::kSolo:
      if (a) {
        close_locked(false);
        return lb.cb.solo;
      }
      break;
    case Row::kCoop:
      if (a) {
        lb.role = Role::kClosed;  // the coop callback opens the host lobby
        g_open.store(false);
        return lb.cb.coop;
      }
      break;
    case Row::kJoinOther:
      if (a && it.index < lb.others.size()) {
        // Become the partner in that lobby: drop our own lobby, take a seat.
        const auto target = lb.others[it.index];
        lobby::PublishData("coop_lobby", "");
        lb.role = Role::kPartner;
        lb.partner = PartnerLobby{};
        lb.partner.my_name = lb.host.rec.host;
        lb.partner.my_xuid = MyXuid();
        lb.partner.Join(target);
        lb.cursor = 0;
        lb.notice = "Joined " + target.rec.host + "'s lobby";
        REXSYS_INFO("[lobby] joining {}'s lobby as partner", target.rec.host);
      }
      break;
    case Row::kHostGender:
      lb.host.rec.gender1 =
          lb.host.rec.gender1 == Gender::kFemale ? Gender::kMale : Gender::kFemale;
      break;
    case Row::kChildhood:
      break;  // one option for now (Sparrow and Rose is feature 005)
    case Row::kReady:
      if (lb.role == Role::kHost) lb.host.rec.hready = !lb.host.rec.hready;
      else lb.partner.SetReady(!lb.partner.seat.ready);
      break;
    case Row::kGender:
      lb.partner.seat.gender =
          lb.partner.seat.gender == Gender::kFemale ? Gender::kMale : Gender::kFemale;
      break;
    case Row::kStart:
      if (a && lb.host.Start()) {
        lobby::PublishData("coop_lobby", Format(lb.host.rec));
        lb.notice = "Starting";
        REXSYS_INFO("[lobby] host start: {}", Format(lb.host.rec));
        return lb.cb.host_start;
      }
      break;
    case Row::kGame:
      if (a) {
        lb.partner.Join(lb.listing[it.index]);
        lb.cursor = 0;
        lb.notice = "Joined " + lb.partner.seat.host + "'s lobby";
        REXSYS_INFO("[lobby] joining {}'s lobby as partner", lb.partner.seat.host);
      }
      break;
    case Row::kHostSaves:
      if (a) {
        close_locked(false);
        return lb.cb.host_saves;
      }
      break;
    case Row::kSave:
      if (a) {
        close_locked(false);
        if (lb.cb.pick_save) return [cb = lb.cb.pick_save, i = it.index] { cb(i); };
      }
      break;
    case Row::kHostNew:
      if (a) {
        close_locked(false);
        return lb.cb.host_new;
      }
      break;
    case Row::kLeave:
      if (a) {
        lb.partner.Leave();
        lb.cursor = 0;
      }
      break;
    case Row::kBack:
      if (a) {
        close_locked(false);
        return lb.cb.back;
      }
      break;
  }
  return {};
}

// --- input -------------------------------------------------------------------------

constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008,
                   kA = 0x1000, kB = 0x2000, kX = 0x4000;

inline uint16_t g_prev_buttons = 0;

// Button injection for the stock menu (Solo resumes the original New Game
// with a fresh A press). Window in steady-clock ms; 0 = none.
struct Injected {
  uint16_t buttons;
  int64_t from_ms, until_ms;
};
inline std::mutex g_inject_mu;
inline std::vector<Injected> g_injected;
inline void InjectPress(uint16_t buttons, int64_t delay_ms, int64_t hold_ms) {
  const int64_t now = fable2::f5lua::steady_ms();
  std::lock_guard<std::mutex> l(g_inject_mu);
  g_injected.push_back({buttons, now + delay_ms, now + delay_ms + hold_ms});
}
// Buttons injected right now; drops finished presses.
inline uint16_t injected_now() {
  const int64_t t = fable2::f5lua::steady_ms();
  std::lock_guard<std::mutex> l(g_inject_mu);
  uint16_t b = 0;
  for (const auto& i : g_injected)
    if (t >= i.from_ms && t < i.until_ms) b |= i.buttons;
  std::erase_if(g_injected, [t](const Injected& i) { return t >= i.until_ms; });
  return b;
}

// Another module may rewrite the game's pad-0 buttons (lobby panel closed):
// fable2_coop_kit.h turns the partner's Start into B while its full pause wheel
// is up. nullptr = the buttons go through unchanged.
inline uint16_t (*g_game_pad_hook)(uint16_t buttons) = nullptr;

// Stick and trigger injection (the test input source only): held values for
// a time window, applied before everything else reads the pad.
struct InjectedAnalog {
  char what;  // 'l' left stick, 'r' right stick, 'L' left trigger, 'R' right trigger
  int16_t x, y;
  uint8_t value;
  int64_t from_ms, until_ms;
};
inline std::vector<InjectedAnalog> g_injected_analog;
inline void InjectAnalog(char what, int16_t x, int16_t y, uint8_t value, int64_t delay_ms, int64_t hold_ms) {
  const int64_t now = fable2::f5lua::steady_ms();
  std::lock_guard<std::mutex> l(g_inject_mu);
  g_injected_analog.push_back({what, x, y, value, now + delay_ms, now + delay_ms + hold_ms});
}
inline void apply_analog(rex::input::X_INPUT_STATE* state) {
  const int64_t t = fable2::f5lua::steady_ms();
  std::lock_guard<std::mutex> l(g_inject_mu);
  if (g_injected_analog.empty()) return;
  for (const auto& i : g_injected_analog) {
    if (t < i.from_ms || t >= i.until_ms) continue;
    switch (i.what) {
      case 'l': state->gamepad.thumb_lx.set(i.x); state->gamepad.thumb_ly.set(i.y); break;
      case 'r': state->gamepad.thumb_rx.set(i.x); state->gamepad.thumb_ry.set(i.y); break;
      case 'L': state->gamepad.left_trigger = i.value; break;
      case 'R': state->gamepad.right_trigger = i.value; break;
    }
  }
  std::erase_if(g_injected_analog, [t](const InjectedAnalog& i) { return t >= i.until_ms; });
}

// Registered with the SDK input filter in place of the camera's, which it
// calls first. While the lobby is open, presses drive the lobby and the game
// sees an idle pad.
inline void input_filter(uint32_t user_index, rex::input::X_INPUT_STATE* state) {
  if (state && user_index == 0) apply_analog(state);  // test input: before the camera reads the look
  fable2::coopcam::input_filter(user_index, state);
  if (!state || user_index != 0) return;
  // Injected presses (our own, and the test input source) count like the pad's,
  // also for the lobby panel.
  const uint16_t inj = injected_now();
  const uint16_t now = static_cast<uint16_t>(state->gamepad.buttons | inj);
  if (!g_open.load()) {
    g_prev_buttons = now;
    const uint16_t game = g_game_pad_hook ? g_game_pad_hook(now) : now;
    if (inj || game != now) state->gamepad.buttons.set(game);
    return;
  }
  const uint16_t pressed = now & ~g_prev_buttons;
  g_prev_buttons = now;
  // Under the joining card (the host has started): the pad does nothing.
  bool covered = false;
  {
    std::lock_guard<std::mutex> l(L().m);
    covered = L().role == Role::kPartner && (L().partner.state == PartnerState::kWaitingForHostWorld ||
                                             L().partner.state == PartnerState::kJoining);
  }
  if (pressed && !covered) {
    std::function<void()> after;
    {
      std::lock_guard<std::mutex> l(L().m);
      after = act_locked((pressed & kDown ? 1 : 0) - (pressed & kUp ? 1 : 0),
                         (pressed & kRight ? 1 : 0) - (pressed & kLeft ? 1 : 0), pressed & kA,
                         pressed & kB, pressed & kX);
    }
    if (after) after();
  }
  state->gamepad = rex::input::X_INPUT_GAMEPAD{};
}

// --- drawing -------------------------------------------------------------------------
// The SDK draws the panel (rex/ui/overlay/lobby_panel.h); the game hands it a
// text view whenever something changed.

// Caller holds L().m.
inline std::string view_locked() {
  auto& lb = L();
  if (lb.role == Role::kClosed) return {};
  const bool hub = lb.role == Role::kPartner && lb.partner.state == PartnerState::kBrowsing;
  std::string v = lb.role == Role::kChoose  ? "TNew Game\n"
                  : lb.role == Role::kHost  ? "TCo-op campaign - your lobby\n"
                  : lb.role == Role::kSaves ? "TContinue a co-op campaign\n"
                  : hub                     ? "TCo-op\n"
                                            : "TCo-op campaign - " + lb.partner.seat.host + "'s lobby\n";
  v += "N" + lb.notice + "\n";
  auto ready = [](bool r) { return r ? "Ready" : "Not ready"; };
  if (lb.role == Role::kHost) {
    const auto& rec = lb.host.rec;
    v += "SPlayers\n";
    v += "L" + rec.host + " (host)  " + (rec.gender1 == Gender::kFemale ? "female" : "male") +
         " hero  " + ready(rec.hready) + "\n";
    if (lb.host.partner_ip)
      v += "L" + rec.partner + "  " + (rec.gender2 == Gender::kFemale ? "female" : "male") +
           " hero  " + ready(lb.host.partner_ready) + "\n";
    else
      v += rec.kind == Kind::kNew ? "D(waiting for a partner)\n" : "D(waiting for " + rec.partner + ")\n";
  } else if (lb.partner.state != PartnerState::kBrowsing && lb.partner.host_rec) {
    const auto& rec = *lb.partner.host_rec;
    v += "SPlayers\n";
    v += "L" + rec.host + " (host)  " + ready(rec.hready) + "\n";
    v += "L" + lb.partner.my_name + " (you)  " + ready(lb.partner.seat.ready) + "\n";
  } else if (lb.role == Role::kPartner && lb.listing.empty()) {
    v += "D(no co-op games found yet: the host picks New Game -> Co-op first)\n";
  }
  const bool browsing = lb.role == Role::kPartner && lb.partner.state == PartnerState::kBrowsing;
  if (lb.role != Role::kChoose && lb.role != Role::kSaves) v += browsing ? "SLobbies\n" : "SOptions\n";
  const auto r = rows_locked();
  lb.cursor = std::clamp(lb.cursor, 0, std::max(0, static_cast<int>(r.size()) - 1));
  for (int i = 0; i < static_cast<int>(r.size()); ++i)
    v += std::string("R") + (r[i].enabled ? "1" : "0") + (i == lb.cursor ? "1" : "0") + r[i].label + "\n";
  v += lb.role == Role::kChoose || lb.role == Role::kSaves || hub ? "FUp/Down move   A select   B back\n"
                                 : "FUp/Down move   A select   Left/Right change   X ready   B back\n";
  return v;
}

inline std::string g_shown_view;
inline void show_view() {
  std::string v;
  {
    std::lock_guard<std::mutex> l(L().m);
    v = view_locked();
  }
  if (v == g_shown_view) return;
  g_shown_view = v;
  rex_ui_set_lobby_view(v.c_str());
  // One log line per change (rig evidence of what the panel shows).
  std::string flat = v;
  for (char& c : flat)
    if (c == '\n') c = '|';
  REXSYS_INFO("[lobby] view: {}", flat.empty() ? std::string("(hidden)") : flat);
}

}  // namespace fable2::cooplobby

// The SDK keeps one input filter and the game one frame hook; the lobby takes
// both and calls the previous owners first (fable2_coop_camera.h,
// fable2_coop_campaign.h, both included earlier in main.cpp).
namespace fable2::cooplobby {
inline void (*g_prev_frame_hook)() = nullptr;
// Test hook: coop_lobby_open = host | partner opens the lobby with logging
// callbacks only (the front end wires the real ones).
inline void open_from_cvar() {
  const std::string want = REXCVAR_GET(coop_lobby_open);
  if (want.empty()) return;
  rex::cvar::SetFlagByName("coop_lobby_open", "");
  if (!REXCVAR_GET(coop_campaign) || g_open.load()) return;
  Callbacks cb;
  cb.host_start = [] { REXSYS_INFO("[lobby] test: host_start"); };
  cb.partner_go = [](uint32_t ip) { REXSYS_INFO("[lobby] test: partner_go {:08X}", ip); };
  cb.back = [] { REXSYS_INFO("[lobby] test: back"); };
  if (want == "host") OpenHost(Kind::kNew, "", "", cb);
  else if (want == "partner") OpenPartner(cb);
}
inline void OnFrame() {
  if (g_prev_frame_hook) g_prev_frame_hook();
  open_from_cvar();
  Tick();
  show_view();
}
inline const bool g_installed = [] {
  rex_input_set_state_filter(&input_filter);
  g_prev_frame_hook = fable2::f5lua::g_frame_hook;
  fable2::f5lua::g_frame_hook = &OnFrame;
  return true;
}();
}  // namespace fable2::cooplobby
