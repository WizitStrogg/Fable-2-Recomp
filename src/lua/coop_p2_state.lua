-- Proof P2 (feature 001): print the hero's story state as one P2STATE line,
-- to compare a skip-to-adulthood start with a normal QC060 start.
--   luafile coop_p2_state.lua
-- Every getter is wrapped: a missing API prints "err" instead of stopping.
local function try(fn, ...)
  local ok, v = pcall(fn, ...)
  if ok then return v end
  return "err"
end

local hero = GetPlayerHero()
local f = {}
local function add(k, v) f[#f + 1] = k .. "=" .. tostring(v) end

add("gfpos", try(function() return Gameflow.PositionInGameflow end))
add("skipping", try(function() return Gameflow.SkippingThroughGameflowFlag end))
add("chapter", try(Stats.GetChapterProgress, hero))
add("age", try(Age.GetAge, hero))
add("agegroup", try(Age.GetAgeGroup, hero))
add("gold", try(Money.Get, hero))
add("title", try(Stats.GetTitle, hero))
local pos = try(function() return hero:GetPosition() end)
if type(pos) ~= "string" and pos then
  add("pos", string.format("%.1f,%.1f,%.1f", pos.x or 0, pos.y or 0, pos.z or 0))
end
add("items", try(Inventory.GetNumberOfItems, hero))
for _, w in ipairs({"AdultStarterMelee", "AdultStarterRanged", "ChildMelee", "ChildRanged"}) do
  local name = try(function() return Gameflow.WeaponNames[w] end)
  add(w, name ~= "err" and name and (name .. ":" .. tostring(try(Inventory.GetNumberOfItemsOfType, hero, name))) or "?")
end
for _, l in ipairs({"Layer_BrokenBridge", "Layer_TavernBandit", "QC010_RoseDiaryLayer_Slums",
                    "QC010_RoseDiaryLayer_Posh"}) do
  add(l, try(Layers.IsLayerActive, l))
end
add("dog", try(function() return GetDog() ~= nil end))
print("P2STATE|" .. table.concat(f, "|"))
