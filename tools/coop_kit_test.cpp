// Unit test for src/core/fable2_coop_kit_proto.h (no game, no SDK).
//   clang++ -std=c++20 -I src/core tools/coop_kit_test.cpp -o out/coop_kit_test.exe
//   out\coop_kit_test.exe        (prints each check; exit 1 on any failure)
#include <cstdio>
#include "fable2_coop_kit_proto.h"

using namespace fable2::coopkit;

static int g_fail = 0;
#define CHECK(cond)                                              \
  do {                                                           \
    const bool ok_ = (cond); /* once: some checks change state */ \
    std::printf("%s  %s\n", ok_ ? "ok  " : "FAIL", #cond);       \
    if (!ok_) ++g_fail;                                          \
  } while (0)

int main() {
  // Commands round-trip; bad ones are rejected.
  const Command c{"s1", 3, Op::kWear, "BD511D47"};
  CHECK(Format(c) == "COOPCMD:v1|sid=s1|seq=3|op=wear|rec=BD511D47");
  CHECK(ParseCommand(Format(c))->op == Op::kWear);
  CHECK(ParseCommand(Format(c))->seq == 3);
  CHECK(!ParseCommand("COOPCMD:v1|sid=s1|seq=3|op=fly|rec=BD511D47"));
  CHECK(!ParseCommand("COOPCMD:v1|sid=s1|seq=0|op=use|rec=BD511D47"));
  CHECK(!ParseCommand("COOPCMD:v1|sid=s1|seq=1|op=use|rec=bd511d47"));  // ids are upper-case hex
  CHECK(!ParseCommand("COOPCMD:v2|sid=s1|seq=1|op=use|rec=BD511D47"));
  CHECK(ParseCommandList("COOPCMD:v1|sid=a|seq=1|op=use|rec=00000001;junk;"
                         "COOPCMD:v1|sid=a|seq=2|op=equip|rec=00000002").size() == 2);

  // Acks round-trip.
  Ack a{"s1", 4, 9, {{3, "ok"}, {4, "no:questitem"}}};
  CHECK(Format(a) == "COOPKITACK:v1|sid=s1|upto=4|rev=9|res=3:ok,4:no:questitem");
  CHECK(ParseAck(Format(a))->res.size() == 2);
  CHECK(ParseAck(Format(a))->res[1].res == "no:questitem");
  CHECK(ReasonText("no:questitem") == "Quest items cannot be used here");

  // Joiner queue: publishes until acked, results reported once.
  JoinerQueue q("s1");
  CHECK(q.Add(Op::kEquip, "00000001") == 1);
  CHECK(q.Add(Op::kUse, "00000002") == 2);
  CHECK(ParseCommandList(q.Published()).size() == 2);
  auto fresh = q.OnAck({"s1", 1, 1, {{1, "ok"}}});
  CHECK(fresh.size() == 1 && q.pending() == 1);
  CHECK(q.OnAck({"s1", 1, 1, {{1, "ok"}}}).empty());  // same ack again: nothing new
  CHECK(q.OnAck({"other", 2, 2, {{2, "ok"}}}).empty() && q.pending() == 1);  // other session
  q.OnAck({"s1", 2, 2, {{1, "ok"}, {2, "no:busy"}}});
  CHECK(q.pending() == 0 && q.Published().empty());

  // Host: in order, once, gap stops, new session restarts.
  HostApplier h;
  std::vector<Command> list = {{"s1", 2, Op::kUse, "00000002"}};
  CHECK(!h.Next(list));  // gap: seq 1 missing
  list.insert(list.begin(), {"s1", 1, Op::kEquip, "00000001"});
  auto n = h.Next(list);
  CHECK(n && n->seq == 1);
  CHECK(!h.Next(list));  // one in flight at a time
  h.Done(1, "ok");
  n = h.Next(list);
  CHECK(n && n->seq == 2);
  h.Done(2, "no:noitem");
  CHECK(!h.Next(list));  // both applied: nothing re-runs
  CHECK(h.MakeAck(5).upto == 2 && h.MakeAck(5).res.size() == 2);
  h.Done(2, "ok");  // stale Done is ignored
  CHECK(h.MakeAck(5).res.size() == 2);
  n = h.Next({{"s2", 1, Op::kWear, "00000003"}});
  CHECK(n && n->sid == "s2" && n->seq == 1);
  h.Done(1, "ok");
  CHECK(h.MakeAck(6).sid == "s2" && h.MakeAck(6).upto == 1 && h.MakeAck(6).res.size() == 1);

  // Kit state round-trips; bad item entries are dropped.
  KitState k;
  k.rev = 7;
  k.hp = 80;
  k.hpmax = 120;
  k.melee = "00000010";
  k.worn = {"00000020", "00000021"};
  k.items = {{"00000010", 1, "WEAPONS", "INV_ITEM_SWORD"}, {"00000030", 3, "ITEMS_POTIONS", "Health:Potion"}};
  const std::string line = Format(k);
  CHECK(line.find("00000030:3:ITEMS_POTIONS:HealthPotion") != std::string::npos);  // ':' cleaned from the tag
  auto k2 = ParseKitState(line);
  CHECK(k2 && k2->rev == 7 && k2->hp == 80 && k2->hpmax == 120 && k2->melee == "00000010");
  CHECK(k2->worn.size() == 2 && k2->items.size() == 2 && k2->items[1].count == 3);
  CHECK(ParseKitState("COOPKIT:v1|rev=1|items=zz:1:A:B,00000001:2:ITEMS_FOOD_AND_DRINK:X")->items.size() == 1);

  // Mirror diff.
  auto d = MirrorDiff(k, {{"00000010", 1}, {"00000030", 1}, {"00000099", 2}});
  CHECK(d.size() == 2);
  CHECK(d[0].rec == "00000030" && d[0].delta == 2);
  CHECK(d[1].rec == "00000099" && d[1].delta == -2);
  CHECK(MirrorDiff(k, {{"00000010", 1}, {"00000030", 3}}).empty());
  auto w = DiffWorn({"A0000000", "B0000000"}, {"B0000000", "C0000000"});
  CHECK(w.put_on.size() == 1 && w.put_on[0] == "A0000000");
  CHECK(w.take_off.size() == 1 && w.take_off[0] == "C0000000");
  CHECK(*OpForScreenAction("wear", true) == Op::kUnwear);
  CHECK(*OpForScreenAction("wear", false) == Op::kWear);
  CHECK(!OpForScreenAction("drop", false));
  CHECK(IdToHex(0xBD511D47) == "BD511D47" && HexToId("BD511D47") == 0xBD511D47u);

  // The copy's wanted inventory: no clothing; equipped weapons added once.
  KitState eq;
  eq.melee = "98F97077";
  eq.ranged = "00000050";
  eq.items = {{"00000050", 2, "WEAPONS_RANGED", ""}, {"C0F0F31B", 1, "CLOTHING_HATS", ""},
              {"11111111", 1, "WEAPONS_MELEE", ""}};
  const KitState wi = WantedInventory(eq, true);
  CHECK(wi.items.size() == 3);
  CHECK(std::count_if(wi.items.begin(), wi.items.end(), [](const Item& i) { return i.rec == "98F97077"; }) == 1);
  CHECK(std::find_if(wi.items.begin(), wi.items.end(), [](const Item& i) { return i.rec == "00000050"; })->count == 2);
  CHECK(WantedInventory(eq, false).items.size() == 2);
  CHECK(MirrorDiff(wi, {{"00000050", 2}, {"11111111", 1}, {"98F97077", 1}}).empty());

  // The partner's wheel: stock pages pass, unsafe ones are rerouted, each switch on its own.
  const PageSwitches all{};
  CHECK(RoutePage("Items", all) == PageRoute::kPass);
  CHECK(RoutePage("Weapons", all) == PageRoute::kPass);
  CHECK(RoutePage("Clothing", all) == PageRoute::kPass);
  CHECK(RoutePage("Map", all) == PageRoute::kPass);
  CHECK(RoutePage("HenchmanQuit", all) == PageRoute::kPass);
  CHECK(RoutePage("Abilities", all) == PageRoute::kHenchmanAbilities);
  CHECK(RoutePage("Kick", all) == PageRoute::kSwallow);
  CHECK(RoutePage("Save", all) == PageRoute::kHenchmanQuit);
  CHECK(RoutePage("SaveAndQuit", all) == PageRoute::kHenchmanQuit);
  CHECK(RoutePage("items", all) == PageRoute::kHenchmanQuit);  // names are exact
  CHECK(RoutePage("Abilities", {true, false, true}) == PageRoute::kPass);
  CHECK(RoutePage("Kick", {true, true, false}) == PageRoute::kPass);
  CHECK(RoutePage("Save", {false, true, true}) == PageRoute::kPass);

  std::printf("%s\n", g_fail ? "SOME CHECKS FAILED" : "all checks passed");
  return g_fail ? 1 : 0;
}
