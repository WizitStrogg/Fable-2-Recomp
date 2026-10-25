-- CoopKit (feature 002, slice 1): player 2's kit on the host.
--   CoopKit.Snapshot(entity, rev) -> one COOPKIT:v1 line (src/core/fable2_coop_kit_proto.h)
--   CoopKit.Apply(entity, op, hex) -> "ok" or "no:<reason>"; op = equip, wear, unwear, use
-- Run as a file (luafile coop_kit) it defines CoopKit and prints the
-- henchman's snapshot. Every engine call is wrapped in pcall.
-- Record ids travel as 8 hex digits; "#XXXXXXXX" names resolve to that id
-- (recomp hook on the name->id function, see fable2_coop_hero.h).

CoopKit = CoopKit or {}

local function try(label, fn, ...)
  local ok, res = pcall(fn, ...)
  if not ok then
    print("[coopkit] " .. label .. " failed: " .. tostring(res))
    return nil
  end
  return res
end

local function hex_of(id)
  local hex = tostring(id):match("(%x%x%x%x%x%x%x%x)%s*$")
  return hex and hex:upper() or nil
end

local function record_hex(entity)
  local id = try("GetID", function() return entity:GetRecord():GetID() end)
  return id and hex_of(id)
end

local CATEGORY_NAMES = {}
for name, value in pairs(EInventoryCategory or {}) do
  if type(value) == "number" then CATEGORY_NAMES[value] = name:gsub("^EIC_", "") end
end

local function item_info(entity)
  local rec = try("GetRecord", function() return entity:GetRecord() end)
  local comp = rec and try("InventoryItemComponent", function() return rec:GetRecord("InventoryItemComponent") end)
  if not comp or comp:IsNull() then return "OTHER", "" end
  local cat = try("Category", function() return comp:GetEnum("Category") end)
  local tag = try("NameTag", function() return comp:GetString("NameTag") end) or ""
  return CATEGORY_NAMES[cat] or tostring(cat or "OTHER"), tag
end

local RANGED_SLOTS = {[DummyObjects.SHEATHE_RANGED_BACK] = true}
local WEAPON_SLOTS = {
  DummyObjects.HAND_RIGHT, DummyObjects.HAND_LEFT, DummyObjects.SHEATHE_BACK,
  DummyObjects.SHEATHE_RANGED_BACK, DummyObjects.SHEATHE_FRONT, DummyObjects.SHEATHE_HIP
}

local function clean(s)
  return (tostring(s or ""):gsub("[|=;,:\r\n]", ""))
end

-- Inventory items, plus owned clothing (held by the appearance manager).
local function collect(entity)
  local items, order = {}, {}
  local function add(e, n)
    local hex = record_hex(e)
    if not hex then return end
    if not items[hex] then
      local cat, tag = item_info(e)
      items[hex] = {count = 0, cat = cat, tag = tag, entity = e}
      order[#order + 1] = hex
    end
    items[hex].count = items[hex].count + n
  end
  local total = try("GetNumberOfItems", Inventory.GetNumberOfItems, entity) or 0
  for i = 0, total - 1 do
    local e = try("GetItemByIndex", Inventory.GetItemByIndex, entity, i)
    if e then add(e, 1) end
  end
  for _, e in ipairs(try("GetAllClothingItems", AppearanceModifierManager.GetAllClothingItems, entity) or {}) do
    local hex = record_hex(e)
    if hex and not items[hex] then add(e, 1) end
  end
  table.sort(order)
  return items, order
end

local function worn_list(entity)
  local out = {}
  for _, e in ipairs(try("GetAllWornItems", AppearanceModifierManager.GetAllWornItems, entity) or {}) do
    local hex = record_hex(e)
    if hex then out[#out + 1] = hex end
  end
  return out
end

local function weapons(entity)
  local melee, ranged = "", ""
  for _, slot in ipairs(WEAPON_SLOTS) do
    local w = try("GetEntityInSlot", Carrying.GetEntityInSlot, entity, slot)
    if w and w:IsAlive() then
      local hex = record_hex(w)
      if hex then
        if RANGED_SLOTS[slot] then
          if ranged == "" then ranged = hex end
        elseif melee == "" then
          melee = hex
        end
      end
    end
  end
  return melee, ranged
end

function CoopKit.Snapshot(entity, rev)
  local items, order = collect(entity)
  local list = {}
  for _, hex in ipairs(order) do
    local i = items[hex]
    list[#list + 1] = hex .. ":" .. i.count .. ":" .. clean(i.cat) .. ":" .. clean(i.tag)
  end
  local melee, ranged = weapons(entity)
  local hp = try("Health.Get", Health.Get, entity) or 0
  local hpm = try("Health.GetMax", Health.GetMax, entity) or 0
  return "COOPKIT:v1|rev=" .. tostring(rev or 0) .. "|hp=" .. math.floor(hp) .. "|hpm=" .. math.floor(hpm) ..
    "|melee=" .. melee .. "|ranged=" .. ranged .. "|worn=" .. table.concat(worn_list(entity), ",") ..
    "|items=" .. table.concat(list, ",")
end

local function has(list, x)
  for _, v in ipairs(list) do if v == x then return true end end
  return false
end

-- Applies one command to the entity and reads it back. Queued game actions may
-- take a moment (001 T021a): "no:pending" asks the caller to read again later.
function CoopKit.Apply(entity, op, hex)
  if not entity or not entity:IsAlive() then return "no:busy" end
  local items = collect(entity)
  local item = items[hex]
  if not item then return "no:noitem" end
  local name = "#" .. hex
  if op == "equip" then
    if item.cat ~= "WEAPONS" and not item.cat:match("^WEAPONS") then return "no:cannotuse" end
    -- L2 15:31Z: EquipWeaponOfType(entity, "#<id>") never took (the DLC sword
    -- stayed in the bag for 30 s); the game's own scripts equip an inventory
    -- weapon by its entity (WeaponSetManager.EquipWeapon(hero, weapon_entity)).
    local melee0, ranged0 = weapons(entity)
    if melee0 ~= hex and ranged0 ~= hex then
      if WeaponSetManager.EquipWeapon then
        try("EquipWeapon", WeaponSetManager.EquipWeapon, entity, item.entity)
      else
        try("EquipWeaponOfType", WeaponSetManager.EquipWeaponOfType, entity, name)
      end
    end
    local melee, ranged = weapons(entity)
    return (melee == hex or ranged == hex) and "ok" or "no:pending"
  elseif op == "wear" or op == "unwear" then
    if not item.cat:match("^CLOTHING") then return "no:cannotuse" end
    if op == "wear" then
      try("AddAndReplaceItemOfType", AppearanceModifierManager.AddAndReplaceItemOfType, entity, name)
    else
      try("RemoveItemOfType", AppearanceModifierManager.RemoveItemOfType, entity, name)
    end
    return (has(worn_list(entity), hex) == (op == "wear")) and "ok" or "no:pending"
  elseif op == "use" then
    -- Launch 1: the silver key's category reads back as QUEST_ITEM.
    if item.cat == "ITEMS_KEY_ITEMS" or item.cat:match("^QUEST") then return "no:questitem" end
    if item.cat ~= "ITEMS_POTIONS" and item.cat ~= "ITEMS_FOOD_AND_DRINK" then return "no:notyet" end
    if not OnCarriedActionUseBonusEffects then return "no:fail" end
    local before = item.count
    local user = try("Create", function() return OnCarriedActionUseBonusEffects:Create(item.entity, entity) end)
    if not user then return "no:fail" end
    try("ApplyBonusEffects", function() user:ApplyBonusEffects() end)
    -- L2 15:31Z: one use took two potions (2 -> 0): the effects already
    -- consume the item, so it is removed here only if it is still there.
    local after = (collect(entity)[hex] or {count = 0}).count
    if after >= before then
      try("RemoveItem", Inventory.RemoveItem, entity, item.entity)
      after = (collect(entity)[hex] or {count = 0}).count
    end
    return after < before and "ok" or "no:fail"
  end
  return "no:fail"
end

-- The recomp's channel runs this file as a library (CoopKitQuiet set) and adds
-- its own calls; `luafile coop_kit` prints the henchman's snapshot.
if not CoopKitQuiet then
  local hench = GetPlayerHenchman()
  if hench and hench:IsAlive() then
    print(CoopKit.Snapshot(hench, 0))
  else
    print("[coopkit] no live henchman")
  end
end
