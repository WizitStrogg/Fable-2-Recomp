-- Research (v13): put the mascot hat on the hero and on player 2.
for _, who in ipairs({"hero", "henchman"}) do
  local e = who == "hero" and GetPlayerHero() or GetPlayerHenchman()
  local ok, err = pcall(AppearanceModifierManager.AddAndReplaceItemOfType, e, "ObjectClothingHatMascotM")
  local n = #(AppearanceModifierManager.GetAllWornItems(e) or {})
  print('[coop] hattest ' .. who .. ' ok=' .. tostring(ok) .. ' worn=' .. n)
end
