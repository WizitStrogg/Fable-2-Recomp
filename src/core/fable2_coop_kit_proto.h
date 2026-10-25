// fable_2 - co-op kit (feature 002, slice 1): player 2's own inventory from the
// partner's PC, through the game's own pause menu. Protocol records, the
// joiner's command queue, the host's apply-once bookkeeping and the mirror
// diff for the joiner's local player-2 copy
// (specs/002-player2-kit/contracts/coop-channel.md, data-model.md).
//
// No SDK or game dependencies, so it can be unit-tested on its own
// (tools/coop_kit_test.cpp). fable2_coop_kit.h feeds it the LAN lobby's keyed
// data and the host's Lua output, and publishes what it returns.
//
// Keys and records (pipe-separated key=value, like COOPHERO; 001 already uses
// "coop_ack"/COOPACK, so the kit's keys and tags are all prefixed):
//   kit_cmd   (joiner) every unacknowledged command, joined by ';'
//             COOPCMD:v1|sid=<session>|seq=<n>|op=equip|rec=<record hex>
//   kit_ack   (host)   COOPKITACK:v1|sid=<session>|upto=<n>|rev=<state rev>|res=<seq>:ok,<seq>:no:<reason>
//   kit_state (host)   COOPKIT:v1|rev=<n>|hp=..|hpm=..|melee=<rec>|ranged=<rec>|worn=<rec>,..
//                      |items=<rec>:<count>:<category>:<name tag>,..
//
// ops: equip (weapon), wear / unwear (clothing), use (potion, food, drink).
// Rules (contract): the host applies each seq of a session once, in order; a
// gap stops it. The joiner keeps a command until upto >= seq. kit_state.rev
// only increases; a lower or equal rev is ignored.

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fable2::coopkit {

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

inline std::vector<std::string_view> Split(std::string_view s, char sep) {
  std::vector<std::string_view> out;
  while (!s.empty()) {
    const size_t p = s.find(sep);
    out.push_back(s.substr(0, p));
    if (p == std::string_view::npos) break;
    s.remove_prefix(p + 1);
  }
  return out;
}

inline uint32_t ToU32(const std::string& s) { return static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 10)); }

// A record id is exactly 8 hex digits, upper case (CoopHero's record_id()).
inline bool IsRecordHex(std::string_view s) {
  if (s.size() != 8) return false;
  for (char c : s)
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
  return true;
}

// Separators of the records above cannot appear in values.
inline std::string Clean(std::string_view s) {
  std::string out;
  for (char c : s)
    if (c != '|' && c != '=' && c != ';' && c != ',' && c != ':' && c != '\n' && c != '\r') out.push_back(c);
  return out;
}

// --- commands ------------------------------------------------------------------

enum class Op { kEquip, kWear, kUnwear, kUse };

inline const char* OpName(Op op) {
  switch (op) {
    case Op::kEquip: return "equip";
    case Op::kWear: return "wear";
    case Op::kUnwear: return "unwear";
    case Op::kUse: return "use";
  }
  return "?";
}
inline std::optional<Op> ParseOp(std::string_view s) {
  if (s == "equip") return Op::kEquip;
  if (s == "wear") return Op::kWear;
  if (s == "unwear") return Op::kUnwear;
  if (s == "use") return Op::kUse;
  return std::nullopt;
}

struct Command {
  std::string sid;  // joiner session: seq restarts at 1 with every new sid
  uint32_t seq = 0;
  Op op = Op::kEquip;
  std::string rec;  // record hex
};

inline std::string Format(const Command& c) {
  return "COOPCMD:v1|sid=" + Clean(c.sid) + "|seq=" + std::to_string(c.seq) + "|op=" + OpName(c.op) +
         "|rec=" + Clean(c.rec);
}
inline std::optional<Command> ParseCommand(std::string_view line) {
  auto f = ParseRecord(line, "COOPCMD");
  if (!f) return std::nullopt;
  auto op = ParseOp((*f)["op"]);
  Command c{(*f)["sid"], ToU32((*f)["seq"]), op.value_or(Op::kEquip), (*f)["rec"]};
  if (!op || c.sid.empty() || c.seq == 0 || !IsRecordHex(c.rec)) return std::nullopt;
  return c;
}
// kit_cmd value: commands joined by ';'. Malformed entries are skipped.
inline std::vector<Command> ParseCommandList(std::string_view value) {
  std::vector<Command> out;
  for (auto part : Split(value, ';'))
    if (auto c = ParseCommand(part)) out.push_back(*c);
  return out;
}

// --- acknowledgements ----------------------------------------------------------

struct Result {
  uint32_t seq = 0;
  std::string res;  // "ok" or "no:<reason>"
};

struct Ack {
  std::string sid;
  uint32_t upto = 0;  // highest seq applied (or refused) in this session
  uint32_t rev = 0;   // kit_state revision that includes the results
  std::vector<Result> res;  // the last kKeepResults results
};
constexpr size_t kKeepResults = 16;

inline std::string Format(const Ack& a) {
  std::string res;
  for (const auto& r : a.res) {
    if (!res.empty()) res += ",";
    res += std::to_string(r.seq) + ":" + r.res;
  }
  return "COOPKITACK:v1|sid=" + Clean(a.sid) + "|upto=" + std::to_string(a.upto) + "|rev=" + std::to_string(a.rev) +
         "|res=" + res;
}
inline std::optional<Ack> ParseAck(std::string_view line) {
  auto f = ParseRecord(line, "COOPKITACK");
  if (!f) return std::nullopt;
  Ack a{(*f)["sid"], ToU32((*f)["upto"]), ToU32((*f)["rev"]), {}};
  for (auto part : Split((*f)["res"], ',')) {
    const size_t colon = part.find(':');
    if (colon == std::string_view::npos) continue;
    a.res.push_back({ToU32(std::string(part.substr(0, colon))), std::string(part.substr(colon + 1))});
  }
  return a;
}

// Results the menu shows to the player.
inline std::string ReasonText(std::string_view res) {
  if (res == "ok") return "Done";
  if (res == "no:noitem") return "You no longer have that item";
  if (res == "no:questitem") return "Quest items cannot be used here";
  if (res == "no:cannotuse") return "That item cannot be used";
  if (res == "no:notyet") return "That item is not supported yet";
  if (res == "no:busy") return "Your hero is busy; try again";
  if (res == "no:fail") return "It did not work";
  return "Not done (" + std::string(res) + ")";
}

// --- the joiner's queue ----------------------------------------------------------

class JoinerQueue {
 public:
  explicit JoinerQueue(std::string sid = "1") : sid_(std::move(sid)) {}

  const std::string& sid() const { return sid_; }

  // Adds a command; returns its seq.
  uint32_t Add(Op op, std::string rec) {
    pending_.push_back({sid_, next_seq_, op, std::move(rec)});
    return next_seq_++;
  }

  // The kit_cmd value: every command not yet acknowledged ("" if none).
  std::string Published() const {
    std::string v;
    for (const auto& c : pending_) {
      if (!v.empty()) v += ";";
      v += Format(c);
    }
    return v;
  }

  // Drops acknowledged commands. Returns the results of commands that were
  // still pending (each reported once).
  std::vector<Result> OnAck(const Ack& a) {
    std::vector<Result> fresh;
    if (a.sid != sid_) return fresh;
    for (const auto& r : a.res) {
      const bool was_pending = std::any_of(pending_.begin(), pending_.end(),
                                           [&](const Command& c) { return c.seq == r.seq; });
      if (was_pending && r.seq <= a.upto) fresh.push_back(r);
    }
    while (!pending_.empty() && pending_.front().seq <= a.upto) pending_.pop_front();
    return fresh;
  }

  size_t pending() const { return pending_.size(); }

 private:
  std::string sid_;
  uint32_t next_seq_ = 1;
  std::deque<Command> pending_;
};

// --- the host's apply-once bookkeeping --------------------------------------------

class HostApplier {
 public:
  // The next command to run from the joiner's published list, if any: seq
  // upto+1 of the current session. A new sid starts a new session.
  std::optional<Command> Next(const std::vector<Command>& cmds) {
    for (const auto& c : cmds) {
      if (c.sid != sid_) {
        if (in_flight_) continue;  // finish the old session's command first
        sid_ = c.sid;
        upto_ = 0;
        results_.clear();
      }
    }
    if (in_flight_) return std::nullopt;
    for (const auto& c : cmds)
      if (c.sid == sid_ && c.seq == upto_ + 1) {
        in_flight_ = true;
        return c;
      }
    return std::nullopt;  // nothing new, or a gap (rule 1: wait for the missing seq)
  }

  // The command returned by Next() finished with `res` ("ok" / "no:<reason>").
  void Done(uint32_t seq, std::string res) {
    if (!in_flight_ || seq != upto_ + 1) return;
    in_flight_ = false;
    upto_ = seq;
    results_.push_back({seq, std::move(res)});
    if (results_.size() > kKeepResults) results_.erase(results_.begin());
  }

  Ack MakeAck(uint32_t rev) const { return {sid_, upto_, rev, results_}; }
  bool in_flight() const { return in_flight_; }
  uint32_t upto() const { return upto_; }

 private:
  std::string sid_;
  uint32_t upto_ = 0;
  bool in_flight_ = false;
  std::vector<Result> results_;
};

// --- player 2's kit as the host sees it ----------------------------------------------

struct Item {
  std::string rec;       // record hex
  uint32_t count = 0;
  std::string category;  // EInventoryCategory name without "EIC_", e.g. CLOTHING_HATS
  std::string tag;       // the item's name text tag (or its record name)
};

struct KitState {
  uint32_t rev = 0;
  int hp = 0, hpmax = 0;
  std::string melee, ranged;      // record hex of the equipped weapons ("" = none)
  std::vector<std::string> worn;  // record hex
  std::vector<Item> items;        // sorted by record hex
};

inline std::string Format(const KitState& k) {
  std::string worn, items;
  for (const auto& w : k.worn) worn += (worn.empty() ? "" : ",") + Clean(w);
  for (const auto& i : k.items)
    items += (items.empty() ? "" : ",") + Clean(i.rec) + ":" + std::to_string(i.count) + ":" + Clean(i.category) +
             ":" + Clean(i.tag);
  return "COOPKIT:v1|rev=" + std::to_string(k.rev) + "|hp=" + std::to_string(k.hp) + "|hpm=" +
         std::to_string(k.hpmax) + "|melee=" + Clean(k.melee) + "|ranged=" + Clean(k.ranged) + "|worn=" + worn +
         "|items=" + items;
}
inline std::optional<KitState> ParseKitState(std::string_view line) {
  auto f = ParseRecord(line, "COOPKIT");
  if (!f) return std::nullopt;
  KitState k;
  k.rev = ToU32((*f)["rev"]);
  k.hp = std::atoi((*f)["hp"].c_str());
  k.hpmax = std::atoi((*f)["hpm"].c_str());
  k.melee = (*f)["melee"];
  k.ranged = (*f)["ranged"];
  for (auto w : Split((*f)["worn"], ','))
    if (IsRecordHex(w)) k.worn.emplace_back(w);
  for (auto part : Split((*f)["items"], ',')) {
    const auto p = Split(part, ':');
    if (p.size() < 4 || !IsRecordHex(p[0])) continue;
    k.items.push_back({std::string(p[0]), ToU32(std::string(p[1])), std::string(p[2]), std::string(p[3])});
  }
  return k;
}

// --- the joiner's mirror ---------------------------------------------------------
// The joiner's local copy of player 2 is corrected to the host's kit_state:
// these are the changes to make, from the copy's current counts.

struct MirrorChange {
  std::string rec;
  int delta = 0;  // > 0 add that many, < 0 remove that many
};

// `have` = record hex -> count on the copy (the inventory, component 13).
// Records the host does not list are removed; only records the host lists
// are ever added.
inline std::vector<MirrorChange> MirrorDiff(const KitState& want, const std::map<std::string, uint32_t>& have) {
  std::map<std::string, int> d;
  for (const auto& i : want.items) d[i.rec] += static_cast<int>(i.count);
  for (const auto& [rec, n] : have) d[rec] -= static_cast<int>(n);
  std::vector<MirrorChange> out;
  for (const auto& [rec, n] : d)
    if (n != 0) out.push_back({rec, n});
  return out;
}

// Worn-set changes for the appearance manager (component 15): put on what the
// host wears and the copy does not, take off what the copy wears and the host
// does not.
struct WornDiff {
  std::vector<std::string> put_on, take_off;
};
inline WornDiff DiffWorn(const std::vector<std::string>& want, const std::vector<std::string>& have) {
  WornDiff w;
  for (const auto& r : want)
    if (std::find(have.begin(), have.end(), r) == have.end()) w.put_on.push_back(r);
  for (const auto& r : have)
    if (std::find(want.begin(), want.end(), r) == want.end()) w.take_off.push_back(r);
  return w;
}

// Which op a stock-screen action on an item turns into (the joiner's hooks).
inline std::optional<Op> OpForScreenAction(std::string_view action, bool currently_worn) {
  if (action == "wear") return currently_worn ? Op::kUnwear : Op::kWear;
  if (action == "equip") return Op::kEquip;
  if (action == "use") return Op::kUse;
  return std::nullopt;
}

inline uint32_t HexToId(std::string_view hex) { return static_cast<uint32_t>(std::strtoul(std::string(hex).c_str(), nullptr, 16)); }
inline std::string IdToHex(uint32_t id) {
  static const char* d = "0123456789ABCDEF";
  std::string s(8, '0');
  for (int i = 7; i >= 0; --i, id >>= 4) s[i] = d[id & 0xF];
  return s;
}

}  // namespace fable2::coopkit
