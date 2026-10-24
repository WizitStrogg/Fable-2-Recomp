-- Feature 001 rig (T021a): the potion's way (Player.ChangePlayerEntityType).
print("[coop] gender try player: " .. tostring(pcall(Player.ChangePlayerEntityType, GetPlayerHero(), "CreatureHeroFemale")))
