-- Feature 001 rig (T034): both purses.
--   luafile coop_purse_read
local hero, hench = GetPlayerHero(), GetPlayerHenchman()
print(string.format("[coop] purse read: host %d, player 2 %d", Money.Get(hero),
  hench and hench:IsAlive() and Money.Get(hench) or -1))
