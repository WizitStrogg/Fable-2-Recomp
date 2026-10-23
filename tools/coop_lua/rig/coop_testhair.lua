-- Test harness only (joiner, own world): take the hat off so the hair shows.
local h = GetPlayerHero()
local ok, err = pcall(AppearanceModifierManager.RemoveItemOfType, h, "ObjectClothingHatMascotM")
print('[coop] testhair hat removed ' .. tostring(ok) .. ' ' .. tostring(err))
