// fable_2 - co-op campaign lobby: protocol records and state machine
// (specs/001-coop-campaign/contracts/lobby-protocol.md, data-model.md "Lobby").
//
// No SDK or game dependencies, so it can be unit-tested on its own
// (tools/coop_lobby_test.cpp). fable2_coop_lobby.h feeds it peer records from
// the LAN lobby's keyed data and publishes what it returns.
//
// Records (pipe-separated key=value, like COOPHERO):
//   coop_lobby (host)    COOPLOBBY:v1|kind=new|host=Josh|partner=|gender2=F|childhood=skip|hready=0|state=Hosting
//                        kind=continue adds |cid=<guid>|partner=Calvin (the only seat it accepts)
//   coop_seat (partner)  COOPSEAT:v1|host=Josh|cid=<guid or new>|gender=F|ready=1
//   coop_go (host)       COOPGO:v1|cid=<guid>|seq=<n>
//   coop_ack (partner)   COOPACK:v1|hxuid=<host xuid>|seq=<n>   (the go it is following)
//
// Players are told apart by their online XUID, never by name (first two-PC
// session, 2026-10-24: the host knew the partner as "User", the LAN lobby's
// name for a blank gamertag, while the partner looked for "" and never saw
// itself seated). Records add hxuid / pxuid (lobby) and hxuid / name (seat);
// names are for display only. A PC on two shared networks (home LAN + ZeroTier)
// is heard once per network: OnePerPlayer keeps one record per XUID.

#pragma once

#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fable2::cooplobby {

// --- records -----------------------------------------------------------------

using Fields = std::map<std::string, std::string>;

// Parses "<TAG>:v1|k=v|k=v". Returns nothing if the tag or version differs.
inline std::optional<Fields> ParseRecord(std::string_view line, std::string_view tag) {
  const std::string head = std::string(tag) + ":v1";
  if (line.substr(0, head.size()) != head) return std::nullopt;
  line.remove_prefix(head.size());
  if (!line.empty() && line.front() != '|') return std::nullopt;
  Fields f;
  while (!line.empty()) {
    line.remove_prefix(1);  // '|'
    const size_t bar = line.find('|');
    const std::string_view item = line.substr(0, bar);
    const size_t eq = item.find('=');
    if (eq != std::string_view::npos)
      f[std::string(item.substr(0, eq))] = std::string(item.substr(eq + 1));
    if (bar == std::string_view::npos) break;
    line.remove_prefix(bar);
  }
  return f;
}

// '|' and '=' cannot appear in values; names are cleaned before use.
inline std::string Clean(std::string_view s) {
  std::string out;
  for (char c : s)
    if (c != '|' && c != '=' && c != '\n' && c != '\r') out.push_back(c);
  return out;
}

enum class Kind { kNew, kContinue };
enum class Gender { kMale, kFemale };

struct LobbyRecord {  // published by the host
  Kind kind = Kind::kNew;
  std::string cid;      // campaign id (continue only)
  std::string host;     // host's player name
  std::string partner;  // seated partner (new) or the campaign's partner (continue)
  Gender gender1 = Gender::kMale;  // host's hero (picks the hero card at Start)
  Gender gender2 = Gender::kMale;
  std::string childhood = "skip";
  bool hready = false;
  std::string state = "Hosting";
  std::string hxuid;  // host's online XUID (XuidStr)
  std::string pxuid;  // seated partner's (new) or the campaign partner's (continue), if known
};

struct SeatRecord {  // published by the partner
  std::string host;
  std::string cid = "new";
  Gender gender = Gender::kMale;
  bool ready = false;
  std::string hxuid;  // the host this seat is for
  std::string name;   // the partner's display name
};

struct AckRecord {  // published by the partner when a coop_go arrives
  std::string hxuid;
  uint32_t seq = 0;
};

inline std::string XuidStr(uint64_t x) {
  if (!x) return "";
  static const char* d = "0123456789ABCDEF";
  std::string s(16, '0');
  for (int i = 15; i >= 0; --i, x >>= 4) s[i] = d[x & 0xF];
  return s;
}

struct GoRecord {  // published by the host when its world is ready
  std::string cid;
  uint32_t seq = 0;
};

inline char GenderChar(Gender g) { return g == Gender::kFemale ? 'F' : 'M'; }
inline Gender GenderFrom(std::string_view s) { return s == "F" ? Gender::kFemale : Gender::kMale; }

inline std::string Format(const LobbyRecord& r) {
  std::string s = "COOPLOBBY:v1|kind=";
  s += r.kind == Kind::kNew ? "new" : "continue";
  if (r.kind == Kind::kContinue) s += "|cid=" + Clean(r.cid);
  s += "|host=" + Clean(r.host) + "|partner=" + Clean(r.partner);
  s += std::string("|gender1=") + GenderChar(r.gender1) + "|gender2=" + GenderChar(r.gender2) + "|childhood=" + Clean(r.childhood);
  s += std::string("|hready=") + (r.hready ? "1" : "0") + "|state=" + Clean(r.state);
  if (!r.hxuid.empty()) s += "|hxuid=" + Clean(r.hxuid);
  if (!r.pxuid.empty()) s += "|pxuid=" + Clean(r.pxuid);
  return s;
}
inline std::optional<LobbyRecord> ParseLobby(std::string_view line) {
  auto f = ParseRecord(line, "COOPLOBBY");
  if (!f) return std::nullopt;
  LobbyRecord r;
  r.kind = (*f)["kind"] == "continue" ? Kind::kContinue : Kind::kNew;
  r.cid = (*f)["cid"];
  r.host = (*f)["host"];
  r.partner = (*f)["partner"];
  r.gender1 = GenderFrom((*f)["gender1"]);
  r.gender2 = GenderFrom((*f)["gender2"]);
  r.childhood = (*f)["childhood"];
  r.hready = (*f)["hready"] == "1";
  r.state = (*f)["state"];
  r.hxuid = (*f)["hxuid"];
  r.pxuid = (*f)["pxuid"];
  if (r.host.empty() && r.hxuid.empty()) return std::nullopt;
  return r;
}

inline std::string Format(const SeatRecord& r) {
  std::string s = "COOPSEAT:v1|host=" + Clean(r.host) + "|cid=" + Clean(r.cid) + "|gender=" +
                  GenderChar(r.gender) + "|ready=" + (r.ready ? "1" : "0");
  if (!r.hxuid.empty()) s += "|hxuid=" + Clean(r.hxuid);
  if (!r.name.empty()) s += "|name=" + Clean(r.name);
  return s;
}
inline std::optional<SeatRecord> ParseSeat(std::string_view line) {
  auto f = ParseRecord(line, "COOPSEAT");
  if (!f) return std::nullopt;
  SeatRecord r;
  r.host = (*f)["host"];
  r.cid = (*f)["cid"].empty() ? "new" : (*f)["cid"];
  r.gender = GenderFrom((*f)["gender"]);
  r.ready = (*f)["ready"] == "1";
  r.hxuid = (*f)["hxuid"];
  r.name = (*f)["name"];
  if (r.host.empty() && r.hxuid.empty()) return std::nullopt;
  return r;
}

inline std::string Format(const AckRecord& r) {
  return "COOPACK:v1|hxuid=" + Clean(r.hxuid) + "|seq=" + std::to_string(r.seq);
}
inline std::optional<AckRecord> ParseAck(std::string_view line) {
  auto f = ParseRecord(line, "COOPACK");
  if (!f) return std::nullopt;
  AckRecord r;
  r.hxuid = (*f)["hxuid"];
  r.seq = static_cast<uint32_t>(std::strtoul((*f)["seq"].c_str(), nullptr, 10));
  return r;
}

inline std::string Format(const GoRecord& r) {
  return "COOPGO:v1|cid=" + Clean(r.cid) + "|seq=" + std::to_string(r.seq);
}
inline std::optional<GoRecord> ParseGo(std::string_view line) {
  auto f = ParseRecord(line, "COOPGO");
  if (!f) return std::nullopt;
  GoRecord r;
  r.cid = (*f)["cid"];
  r.seq = static_cast<uint32_t>(std::strtoul((*f)["seq"].c_str(), nullptr, 10));
  return r;
}

// --- state machine -------------------------------------------------------------

// A peer's latest record as seen through the LAN lobby keyed data.
struct Heard {
  uint32_t ip = 0;
  std::string name;
  std::string value;
  int age_s = 0;  // seconds since it was received
  uint64_t xuid = 0;  // the sender's online XUID (0: not known yet)
};

constexpr int kStaleSeconds = 10;  // contract rule 5

// One record per player (by XUID; by address when the XUID is not known yet):
// the freshest of the copies heard over each shared network.
inline std::vector<Heard> OnePerPlayer(const std::vector<Heard>& in) {
  std::vector<Heard> out;
  for (const auto& h : in) {
    auto it = out.begin();
    for (; it != out.end(); ++it)
      if (h.xuid ? it->xuid == h.xuid : (!it->xuid && it->ip == h.ip)) break;
    if (it == out.end()) out.push_back(h);
    else if (h.age_s < it->age_s) *it = h;
  }
  return out;
}

enum class HostState { kHosting, kPartnerJoined, kBothReady, kStarting, kInWorld };
enum class PartnerState { kBrowsing, kJoined, kReady, kWaitingForHostWorld, kJoining, kInWorld };

inline const char* Name(HostState s) {
  switch (s) {
    case HostState::kHosting: return "Hosting";
    case HostState::kPartnerJoined: return "PartnerJoined";
    case HostState::kBothReady: return "BothReady";
    case HostState::kStarting: return "Starting";
    case HostState::kInWorld: return "InWorld";
  }
  return "?";
}

// Host side. Owns the lobby record; reads seats.
struct HostLobby {
  LobbyRecord rec;
  uint32_t partner_ip = 0;    // the freshest address the partner was heard on
  uint64_t partner_xuid = 0;  // who the partner is
  bool partner_ready = false;
  bool partner_proto_ok = false;  // partner announces the campaign feature
  HostState state = HostState::kHosting;
  uint32_t go_seq = 0;
  std::string left;  // set when a seated partner leaves (name, for the notice)

  bool names_me(const SeatRecord& s) const {
    return !s.hxuid.empty() && !rec.hxuid.empty() ? s.hxuid == rec.hxuid : s.host == rec.host;
  }
  bool is_partner(const Heard& h) const {
    return partner_xuid && h.xuid ? h.xuid == partner_xuid : h.ip == partner_ip;
  }

  // Feeds every peer's coop_seat. A new lobby takes the first fresh seat that
  // names this host; a continue lobby only the campaign's partner (rule 1).
  // A seated partner whose seat went stale or stopped naming us is dropped.
  void OnSeats(const std::vector<Heard>& all) {
    if (state == HostState::kStarting || state == HostState::kInWorld) return;
    const auto seats = OnePerPlayer(all);
    const Heard* mine = nullptr;
    for (const auto& h : seats) {
      if (h.age_s > kStaleSeconds) continue;
      auto seat = ParseSeat(h.value);
      if (!seat || !names_me(*seat)) continue;
      bool wanted = partner_ip == 0 || is_partner(h);
      if (rec.kind == Kind::kContinue) {
        const std::string who = seat->name.empty() ? h.name : seat->name;
        wanted = seat->cid == rec.cid &&
                 (!rec.pxuid.empty() && h.xuid ? XuidStr(h.xuid) == rec.pxuid : who == rec.partner);
      }
      if (wanted) {
        mine = &h;
        break;
      }
    }
    if (!mine) {
      if (partner_ip) left = rec.partner.empty() ? "Your partner" : rec.partner;
      partner_ip = 0;
      partner_xuid = 0;
      partner_ready = false;
      if (rec.kind == Kind::kNew) {
        rec.partner.clear();
        rec.pxuid.clear();
      }
      state = HostState::kHosting;
      rec.state = Name(state);
      return;
    }
    auto seat = *ParseSeat(mine->value);
    partner_ip = mine->ip;
    partner_xuid = mine->xuid;
    if (mine->xuid) rec.pxuid = XuidStr(mine->xuid);
    rec.partner = seat.name.empty() ? mine->name : seat.name;
    rec.gender2 = seat.gender;  // the partner owns gender (rule 2)
    partner_ready = seat.ready;
    state = partner_ready && rec.hready ? HostState::kBothReady : HostState::kPartnerJoined;
    rec.state = Name(state);
  }

  // Start is allowed only with both ready and both speaking the campaign
  // protocol (rule 3). Returns false if Start was refused.
  bool Start() {
    if (state != HostState::kBothReady || !partner_proto_ok) return false;
    state = HostState::kStarting;
    rec.state = Name(state);
    return true;
  }

  // The host's world is up: tell the partner to join. A new seq per attempt
  // so a stale coop_go is ignored (rule 4).
  GoRecord Go(const std::string& cid) {
    state = HostState::kInWorld;
    rec.state = Name(state);
    return GoRecord{cid, ++go_seq};
  }
};

// Partner side. Lists lobbies, owns its seat, follows coop_go.
struct PartnerLobby {
  std::string my_name;
  uint64_t my_xuid = 0;
  SeatRecord seat;
  uint32_t host_ip = 0;    // the freshest address the host was heard on
  uint64_t host_xuid = 0;  // who the host is
  PartnerState state = PartnerState::kBrowsing;
  uint32_t last_go_seq = 0;
  std::optional<LobbyRecord> host_rec;

  struct Listing {
    uint32_t ip;
    LobbyRecord rec;
    bool joinable;  // open new lobby, or a campaign waiting for me
    uint64_t xuid = 0;
  };

  // The lobby record names this player as its partner.
  bool names_me(const LobbyRecord& r) const {
    return !r.pxuid.empty() && my_xuid ? r.pxuid == XuidStr(my_xuid) : r.partner == my_name;
  }
  bool is_host(const Heard& h) const { return host_xuid && h.xuid ? h.xuid == host_xuid : h.ip == host_ip; }
  // The joined host's freshest record among the copies heard (one per network).
  std::optional<Heard> FromHost(const std::vector<Heard>& v) const {
    std::optional<Heard> best;
    for (const auto& h : v)
      if (is_host(h) && (!best || h.age_s < best->age_s)) best = h;
    return best;
  }

  // Lobbies a partner may see, one per host: fresh, and either open (new, no
  // partner yet) or a campaign that names this player.
  std::vector<Listing> List(const std::vector<Heard>& lobbies) const {
    std::vector<Listing> out;
    for (const auto& h : OnePerPlayer(lobbies)) {
      if (h.age_s > kStaleSeconds) continue;
      auto rec = ParseLobby(h.value);
      if (!rec) continue;
      if (rec->host.empty()) rec->host = h.name;
      const bool open = rec->partner.empty() && rec->pxuid.empty();
      const bool joinable = rec->kind == Kind::kNew ? (open || names_me(*rec)) : names_me(*rec);
      out.push_back({h.ip, *rec, joinable, h.xuid});
    }
    return out;
  }

  // For a host whose lobby has no partner yet: other players' open new
  // lobbies and campaigns waiting for this player (never this player's own).
  std::vector<Listing> JoinableOthers(const std::vector<Heard>& lobbies) const {
    std::vector<Listing> out;
    for (auto& l : List(lobbies)) {
      const bool mine = my_xuid && l.xuid ? l.xuid == my_xuid : l.rec.host == my_name;
      if (l.joinable && !mine) out.push_back(l);
    }
    return out;
  }

  void Join(const Listing& l) {
    host_ip = l.ip;
    host_xuid = l.xuid;
    seat.host = l.rec.host;
    seat.hxuid = l.rec.hxuid;
    seat.name = my_name;
    seat.cid = l.rec.kind == Kind::kContinue ? l.rec.cid : "new";
    seat.ready = false;
    state = PartnerState::kJoined;
  }

  void Leave() {
    host_ip = 0;
    host_xuid = 0;
    host_rec.reset();
    seat = SeatRecord{};
    state = PartnerState::kBrowsing;
  }

  void SetReady(bool r) {
    seat.ready = r;
    if (state == PartnerState::kJoined || state == PartnerState::kReady)
      state = r ? PartnerState::kReady : PartnerState::kJoined;
  }

  // Feeds the joined host's lobby record and coop_go. Returns true once when
  // a new coop_go arrives: the caller starts the join.
  bool OnHost(const std::optional<Heard>& lobby, const std::optional<Heard>& go) {
    if (state == PartnerState::kBrowsing || state == PartnerState::kInWorld) return false;
    if (!lobby || lobby->age_s > kStaleSeconds) {
      Leave();  // host gone (HostLost -> Browsing)
      return false;
    }
    host_ip = lobby->ip;
    host_rec = ParseLobby(lobby->value);
    if (!host_rec || !names_me(*host_rec)) {
      // Not (or no longer) seated: back to joined-but-unconfirmed.
      if (state == PartnerState::kWaitingForHostWorld) state = PartnerState::kReady;
      return false;
    }
    if (host_rec->state == "Starting" && state == PartnerState::kReady)
      state = PartnerState::kWaitingForHostWorld;
    if (go) {
      auto g = ParseGo(go->value);
      if (g && g->seq > last_go_seq && (seat.cid == "new" || g->cid == seat.cid)) {
        last_go_seq = g->seq;
        state = PartnerState::kJoining;
        return true;
      }
    }
    return false;
  }
};

}  // namespace fable2::cooplobby
