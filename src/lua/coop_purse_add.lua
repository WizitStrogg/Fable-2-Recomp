-- Feature 001 rig (T034): gold through player 2 (the game splits it) and a spend
-- by the host. The shared purse (fable2_coop_frontend.h) must end at +70.
--   luafile coop_purse_add
local hero, hench = GetPlayerHero(), GetPlayerHenchman()
print(string.format("[coop] purse test before: host %d, player 2 %d", Money.Get(hero), Money.Get(hench)))
Money.Modify(hench, 100)
Money.Modify(hero, -30)
print(string.format("[coop] purse test after add: host %d, player 2 %d", Money.Get(hero), Money.Get(hench)))
