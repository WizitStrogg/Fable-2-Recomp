-- Test harness only: what do worn items / their records look like?
local h = GetPlayerHero()
local items = AppearanceModifierManager.GetAllWornItems(h) or {}
print('[coop] itemprobe worn count=' .. #items)
for i, item in ipairs(items) do
  if i > 4 then break end
  local rec_ok, rec = pcall(function() return item:GetRecord() end)
  local id_ok, id = pcall(function() return rec:GetID() end)
  local nm_ok, nm = pcall(function() return rec:GetName() end)
  local inm_ok, inm = pcall(function() return item:GetName() end)
  local mt = getmetatable(id)
  local keys = {}
  if type(mt) == 'table' then for k in pairs(mt) do keys[#keys + 1] = tostring(k) end end
  local idx = type(mt) == 'table' and mt.__index
  if type(idx) == 'table' then for k in pairs(idx) do keys[#keys + 1] = 'i.' .. tostring(k) end end
  print('[coop] itemprobe ' .. i .. ' rec=' .. tostring(rec) .. ' id=' .. tostring(id) .. ' (' .. type(id) ..
        ') name=' .. tostring(nm_ok and nm) .. ' itemname=' .. tostring(inm_ok and inm) ..
        ' idmeta=' .. table.concat(keys, ','))
end
