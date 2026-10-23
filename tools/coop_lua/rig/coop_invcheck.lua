-- Test harness: inventory counts of the hero (and player 2) for the co-op
-- items, to prove the own-hero apply leaves the host hero's inventory alone.
local ids = {"BD511D47","331B2070","18903B97","01C2BCAA","EB72D99D","CBFF7957","C0F0F31B"}
local function counts(e)
  if not e then return "none" end
  local total = 0
  pcall(function() total = Inventory.GetNumberOfItems(e) end)
  local parts = {}
  for _, h in ipairs(ids) do
    local ok, n = pcall(function() return Inventory.GetNumberOfItemsOfType(e, GDB.GetRecord("#" .. h):GetID()) end)
    parts[#parts + 1] = h .. "=" .. tostring(ok and n or "?")
  end
  return "total=" .. tostring(total) .. " " .. table.concat(parts, " ")
end
print("[coop] invcheck hero " .. counts(GetPlayerHero()))
print("[coop] invcheck player2 " .. counts(GetPlayerHenchman()))
