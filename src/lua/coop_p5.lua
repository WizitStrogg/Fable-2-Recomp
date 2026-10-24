-- Proof P5 (feature 001), HOST: where player 2's gold goes.
--   luafile coop_p5.lua      prints both purses and gives player 2 100 gold
--   luafile coop_p5.lua again later to see where the 100 went on both PCs
local function try(fn, ...)
  local ok, v = pcall(fn, ...)
  return ok and v or ("err:" .. tostring(v))
end
local hero, p2 = GetPlayerHero(), GetPlayerHenchman()
P5Runs = (P5Runs or 0) + 1
print("P5|run=" .. P5Runs .. "|host=" .. tostring(try(Money.Get, hero)) ..
  "|p2=" .. tostring(p2 and try(Money.Get, p2) or "none"))
if p2 and P5Runs == 1 then
  print("P5|modify p2 +100: " .. tostring(try(Money.Modify, p2, 100)))
  print("P5|after|host=" .. tostring(try(Money.Get, hero)) .. "|p2=" .. tostring(try(Money.Get, p2)))
end
