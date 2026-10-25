// Unit test for src/core/fable2_coop_lobby_proto.h (no game, no SDK).
//   clang++ -std=c++20 -I src/core tools/coop_lobby_test.cpp -o out/coop_lobby_test.exe
//   out\coop_lobby_test.exe        (prints each check; exit 1 on any failure)
#include <cstdio>
#include "fable2_coop_lobby_proto.h"

using namespace fable2::cooplobby;

static int g_fail = 0;
#define CHECK(cond)                                              \
  do {                                                           \
    const bool ok_ = (cond); /* once: some checks change state */ \
    std::printf("%s  %s\n", ok_ ? "ok  " : "FAIL", #cond);       \
    if (!ok_) ++g_fail;                                          \
  } while (0)

int main() {
  // Records round-trip and reject other tags/versions.
  LobbyRecord l;
  l.host = "Josh";
  l.gender2 = Gender::kFemale;
  CHECK(Format(l) == "COOPLOBBY:v1|kind=new|host=Josh|partner=|gender1=M|gender2=F|childhood=skip|hready=0|state=Hosting");
  l.gender1 = Gender::kFemale;
  CHECK(ParseLobby(Format(l))->gender1 == Gender::kFemale);
  CHECK(ParseLobby(Format(l))->gender2 == Gender::kFemale);
  CHECK(!ParseLobby("COOPLOBBY:v2|kind=new|host=Josh"));
  CHECK(!ParseLobby("COOPLOBBYX:v1|host=Josh"));
  CHECK(!ParseSeat("COOPSEAT:v1|cid=new"));  // no host named
  SeatRecord s{"Jo|sh", "new", Gender::kFemale, true};
  CHECK(Format(s) == "COOPSEAT:v1|host=Josh|cid=new|gender=F|ready=1");  // '|' cleaned
  CHECK(ParseGo(Format(GoRecord{"abc", 7}))->seq == 7);

  // Host (new lobby): first seat that names us is taken; gender follows the seat.
  HostLobby host;
  host.rec.host = "Josh";
  host.OnSeats({{2, "Calvin", "COOPSEAT:v1|host=Josh|cid=new|gender=F|ready=0", 1},
                {3, "Mallory", "COOPSEAT:v1|host=Josh|cid=new|gender=M|ready=1", 1}});
  CHECK(host.partner_ip == 2 && host.rec.partner == "Calvin");
  CHECK(host.rec.gender2 == Gender::kFemale);
  CHECK(host.state == HostState::kPartnerJoined);
  CHECK(!host.Start());  // nobody ready
  host.rec.hready = true;
  host.OnSeats({{3, "Mallory", "COOPSEAT:v1|host=Josh|cid=new|gender=M|ready=1", 1},
                {2, "Calvin", "COOPSEAT:v1|host=Josh|cid=new|gender=F|ready=1", 1}});
  CHECK(host.partner_ip == 2);  // the seated partner keeps the seat
  CHECK(host.state == HostState::kBothReady);
  CHECK(!host.Start());  // partner's proto not confirmed
  host.partner_proto_ok = true;
  CHECK(host.Start() && host.rec.state == "Starting");
  GoRecord g1 = host.Go("cid-1");
  CHECK(g1.seq == 1 && host.state == HostState::kInWorld);

  // Host: a stale seat frees the lobby again.
  HostLobby h2;
  h2.rec.host = "Josh";
  h2.OnSeats({{2, "Calvin", "COOPSEAT:v1|host=Josh|cid=new|gender=F|ready=1", 1}});
  h2.OnSeats({{2, "Calvin", "COOPSEAT:v1|host=Josh|cid=new|gender=F|ready=1", 11}});
  CHECK(h2.partner_ip == 0 && h2.rec.partner.empty() && h2.state == HostState::kHosting);

  // Host (continue lobby): only the campaign's partner with the right cid.
  HostLobby h3;
  h3.rec.host = "Josh";
  h3.rec.kind = Kind::kContinue;
  h3.rec.cid = "cid-9";
  h3.rec.partner = "Calvin";
  h3.OnSeats({{3, "Mallory", "COOPSEAT:v1|host=Josh|cid=cid-9|gender=M|ready=1", 1}});
  CHECK(h3.partner_ip == 0 && h3.rec.partner == "Calvin");  // stranger refused, partner name kept
  h3.OnSeats({{2, "Calvin", "COOPSEAT:v1|host=Josh|cid=other|gender=M|ready=1", 1}});
  CHECK(h3.partner_ip == 0);  // wrong campaign
  h3.OnSeats({{2, "Calvin", "COOPSEAT:v1|host=Josh|cid=cid-9|gender=M|ready=1", 1}});
  CHECK(h3.partner_ip == 2);

  // Partner: listing shows open lobbies and campaigns for me; strangers' campaigns are not joinable.
  PartnerLobby p;
  p.my_name = "Calvin";
  auto list = p.List({{1, "Josh", "COOPLOBBY:v1|kind=new|host=Josh|partner=|gender2=M|childhood=skip|hready=0|state=Hosting", 1},
                      {4, "Eve", "COOPLOBBY:v1|kind=continue|cid=x|host=Eve|partner=Bob|gender2=M|childhood=skip|hready=0|state=Hosting", 1},
                      {5, "Old", "COOPLOBBY:v1|kind=new|host=Old|partner=|gender2=M|childhood=skip|hready=0|state=Hosting", 30}});
  CHECK(list.size() == 2 && list[0].joinable && !list[1].joinable);
  p.Join(list[0]);
  p.seat.gender = Gender::kFemale;
  p.SetReady(true);
  CHECK(p.state == PartnerState::kReady);
  const std::string seated = "COOPLOBBY:v1|kind=new|host=Josh|partner=Calvin|gender2=F|childhood=skip|hready=1|state=Starting";
  CHECK(!p.OnHost(Heard{1, "Josh", seated, 1}, std::nullopt));
  CHECK(p.state == PartnerState::kWaitingForHostWorld);
  CHECK(p.OnHost(Heard{1, "Josh", seated, 1}, Heard{1, "Josh", "COOPGO:v1|cid=cid-1|seq=1", 1}));
  CHECK(p.state == PartnerState::kJoining);
  CHECK(!p.OnHost(Heard{1, "Josh", seated, 1}, Heard{1, "Josh", "COOPGO:v1|cid=cid-1|seq=1", 1}));  // same seq ignored

  // Partner: host goes quiet -> back to browsing.
  PartnerLobby p2;
  p2.my_name = "Calvin";
  p2.Join(list[0]);
  p2.OnHost(Heard{1, "Josh", seated, 12}, std::nullopt);
  CHECK(p2.state == PartnerState::kBrowsing);

  // Host view: other open lobbies to join, never its own, never full or foreign campaigns.
  PartnerLobby me;
  me.my_name = "Josh";
  auto others = me.JoinableOthers(
      {{1, "Josh", "COOPLOBBY:v1|kind=new|host=Josh|partner=|gender2=M|childhood=skip|hready=0|state=Hosting", 1},
       {2, "Calvin", "COOPLOBBY:v1|kind=new|host=Calvin|partner=|gender2=M|childhood=skip|hready=0|state=Hosting", 1},
       {3, "Eve", "COOPLOBBY:v1|kind=new|host=Eve|partner=Bob|gender2=M|childhood=skip|hready=0|state=PartnerJoined", 1},
       {4, "Dan", "COOPLOBBY:v1|kind=continue|cid=c|host=Dan|partner=Josh|gender2=M|childhood=skip|hready=0|state=Hosting", 1}});
  CHECK(others.size() == 2 && others[0].rec.host == "Calvin" && others[1].rec.host == "Dan");

  // 2026-10-24, first two-PC session: Josh (host) and Calvin (blank gamertag,
  // known on the network as "User"), both on the home LAN and ZeroTier, so every
  // record arrives twice (from .1.x and .192.x). Matched by XUID now.
  {
    const uint64_t kJosh = 0x0009AAAA00000001ull, kCalvin = 0x0009BBBB00000002ull;
    HostLobby h;
    h.rec.host = "Wizit";
    h.rec.hxuid = XuidStr(kJosh);
    PartnerLobby c;
    c.my_name = "CALVIN-PC";  // MyName(): the PC name when the gamertag is blank
    c.my_xuid = kCalvin;
    const std::string lob = Format(h.rec);
    auto games = c.List({{0xC0A80102, "Wizit", lob, 1, kJosh}, {0xC0A8C088, "Wizit", lob, 0, kJosh}});
    CHECK(games.size() == 1 && games[0].joinable && games[0].ip == 0xC0A8C088);  // one row, the fresher copy
    c.Join(games[0]);
    CHECK(c.seat.hxuid == XuidStr(kJosh) && c.seat.name == "CALVIN-PC");
    c.SetReady(true);
    const std::string seat = Format(c.seat);
    h.OnSeats({{0xC0A80159, "User", seat, 1, kCalvin}, {0xC0A8C085, "User", seat, 2, kCalvin}});
    CHECK(h.partner_xuid == kCalvin && h.rec.partner == "CALVIN-PC" && h.rec.pxuid == XuidStr(kCalvin));
    h.OnSeats({{0xC0A80159, "User", seat, 3, kCalvin}, {0xC0A8C085, "User", seat, 0, kCalvin}});
    CHECK(h.partner_xuid == kCalvin && h.partner_ip == 0xC0A8C085);  // same partner on the other network
    h.rec.hready = true;
    h.OnSeats({{0xC0A8C085, "User", seat, 0, kCalvin}});
    h.partner_proto_ok = true;
    CHECK(h.Start());
    const std::string started = Format(h.rec);
    CHECK(!c.OnHost(Heard{0xC0A80102, "Wizit", started, 1, kJosh}, std::nullopt));
    CHECK(c.state == PartnerState::kWaitingForHostWorld);  // the partner sees itself seated
    const GoRecord go = h.Go("cid-7");
    CHECK(c.OnHost(Heard{0xC0A80102, "Wizit", Format(h.rec), 1, kJosh},
                   Heard{0xC0A8C088, "Wizit", Format(go), 0, kJosh}));
    CHECK(c.state == PartnerState::kJoining && c.last_go_seq == 1);
    CHECK(ParseAck(Format(AckRecord{XuidStr(kJosh), 1}))->seq == 1);
    // Proof run, 2026-10-24: the LAN lobby gave XUID 0 for one of the two
    // addresses. The sender's XUID is read from the record itself.
    CHECK(SenderXuid(lob) == kJosh && SenderXuid(seat) == kCalvin);
    CHECK(SenderXuid(Format(go)) == kJosh);
    CHECK(SenderXuid(Format(AckRecord{XuidStr(kJosh), 1, XuidStr(kCalvin)})) == kCalvin);
    std::vector<Heard> two = {{0x7F000001, "Wizit", lob, 1, 0}, {0x7F000004, "Wizit", lob, 0, kJosh}};
    for (auto& h : two)
      if (const uint64_t own = SenderXuid(h.value)) h.xuid = own;  // as heard() does
    CHECK(OnePerPlayer(two).size() == 1);
    // The host's own lobby is never offered to itself, even under another address.
    PartnerLobby self;
    self.my_name = "Wizit";
    self.my_xuid = kJosh;
    CHECK(self.JoinableOthers({{0x7F000001, "Wizit", lob, 0, kJosh}}).empty());
    // A partner that leaves frees the seat and says who left.
    HostLobby h4;
    h4.rec.host = "Wizit";
    h4.rec.hxuid = XuidStr(kJosh);
    h4.OnSeats({{0xC0A8C085, "User", seat, 1, kCalvin}});
    h4.OnSeats({});
    CHECK(h4.partner_ip == 0 && h4.left == "CALVIN-PC" && h4.state == HostState::kHosting);
  }

  // Shared purse (Josh's playtest 2026-10-25): both had 3; a chest of 150 gave
  // the game's own share +75/+75 -> 153 on both; spending 100 anywhere -> 53.
  {
    int64_t last = 3;
    int64_t t = PurseTotal(last, 78, 78);
    CHECK(t == 153);
    last = t;
    CHECK(PurseTotal(last, 53, 153) == 53);   // the host spends 100
    CHECK(PurseTotal(last, 153, 53) == 53);   // player 2 spends 100 (host's henchman copy)
    CHECK(PurseTotal(last, 163, 140) == 150); // both in one tick: host +10, player 2 -13
    CHECK(PurseTotal(last, 153, 153) == 153); // nothing happened
    PurseRecord r{XuidStr(0x0009AAAA00000001ull), 153, 7};
    auto p = ParsePurse(Format(r));
    CHECK(p && p->total == 153 && p->seq == 7 && p->hxuid == XuidStr(0x0009AAAA00000001ull));
    CHECK(SenderXuid(Format(r)) == 0);  // only lobby/go/seat/ack carry a sender field it reads
    CHECK(!ParsePurse("COOPPURSE:v1|hxuid=1|seq=2"));  // no total
    CHECK(ParsePurse(Format(PurseRecord{"", -5, 1}))->total == -5);
  }

  std::printf("%s (%d failed)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
  return g_fail ? 1 : 0;
}
