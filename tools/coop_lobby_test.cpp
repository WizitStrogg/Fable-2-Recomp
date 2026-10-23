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
  CHECK(Format(l) == "COOPLOBBY:v1|kind=new|host=Josh|partner=|gender2=F|childhood=skip|hready=0|state=Hosting");
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

  std::printf("%s (%d failed)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
  return g_fail ? 1 : 0;
}
