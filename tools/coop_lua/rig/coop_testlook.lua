-- Test harness only (joiner, in its own world): make this hero recognisable
-- before joining - mascot hat and maximum fat.
local h = GetPlayerHero()
pcall(AppearanceModifierManager.AddAndReplaceItemOfType, h, "ObjectClothingHatMascotM")
pcall(function() Stats.ModifyFat(h, 1.0 - Stats.GetFat(h)) end)
print('[coop] testlook applied: fat ' .. tostring(Stats.GetFat(h)))
