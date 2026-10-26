-- CoopHero: capture a hero's look and stats into a table / one text line, and
-- apply such a profile to another entity (the co-op partner's henchman).
--
-- Profile line format (printed by the capture step, parsed by the apply step):
--   COOPHERO:v1|g=M|mor=..|pur=..|fat=..|str=..|skl=..|wil=..|ren=..
--           |ab=<abilityvalue>:<level>,...|xp=<gen>,<str>,<skl>,<wil>
--           |worn=<recordid>,...|weap=<recordid>,...
-- Profile v2 (co-op campaign file coop_player2.txt, specs/002-player2-kit/data-model.md)
-- adds |hgt=..|age=..|xpt=<4 totals>|items=<recordid>:<count>,...|title=<id>|rev=<n>.
-- Serialize keeps writing v1 (what v13 partners accept); SerializeV2 writes v2.
-- Clothing dye and the unlocked-title list have no Lua getter; hair and tattoos
-- are worn items, so they travel in worn.
-- Every engine call is wrapped in pcall: a missing binding or a dead entity
-- prints a note instead of stopping the script.

CoopHero = CoopHero or {}

local function try(label, fn, ...)
  local ok, res = pcall(fn, ...)
  if not ok then
    print("[coop] " .. label .. " failed: " .. tostring(res))
    return nil
  end
  return res
end
CoopHero.Try = try

local WEAPON_SLOTS = {
  DummyObjects.HAND_RIGHT,
  DummyObjects.HAND_LEFT,
  DummyObjects.SHEATHE_BACK,
  DummyObjects.SHEATHE_RANGED_BACK,
  DummyObjects.SHEATHE_FRONT,
  DummyObjects.SHEATHE_HIP
}

local XP_TYPES = {
  EExperienceType.EXPERIENCE_GENERAL,
  EExperienceType.EXPERIENCE_STRENGTH,
  EExperienceType.EXPERIENCE_SKILL,
  EExperienceType.EXPERIENCE_WILL
}

-- Item record ids are light userdata; tostring() shows the 32-bit id
-- ("userdata: BD511D47"). The profile carries those 8 hex digits, and the apply
-- turns them back into a record with GDB.GetRecord("#BD511D47") (the recomp
-- resolves "#" names to the id itself, see fable2_coop_hero.h).
local function record_id(item)
  local id = try("GetRecord", function() return item:GetRecord():GetID() end)
  if id == nil then return nil end
  local hex = tostring(id):match("(%x%x%x%x%x%x%x%x)%s*$")
  return hex and hex:upper() or nil
end

local function id_from_hex(hex)
  return try("GDB.GetRecord #" .. hex, function() return GDB.GetRecord("#" .. hex):GetID() end)
end

function CoopHero.Capture(entity)
  local p = {}
  p.male = try("Gender.Get", function() return Gender.Get(entity) == EGender.EG_MALE end)
  p.mor = try("GetMorality", Stats.GetMorality, entity)
  p.pur = try("GetPurity", Stats.GetPurity, entity)
  p.fat = try("GetFat", Stats.GetFat, entity)
  p.str = try("GetStrength", Stats.GetStrength, entity)
  p.skl = try("GetSkill", Stats.GetSkill, entity)
  p.wil = try("GetWill", Stats.GetWill, entity)
  p.ren = try("GetRenown", Stats.GetRenown, entity)
  p.ab = {}
  for name, value in pairs(EHeroAbilityType) do
    if type(value) == "number" then
      local level = try("GetHeroAbilityLevel " .. name, Stats.GetHeroAbilityLevel, entity, value)
      -- One enum entry is not a real ability and reads back garbage.
      if level and level > 0 and level <= 10 then
        p.ab[#p.ab + 1] = {value, level}
      end
    end
  end
  p.xp = {}
  for i, t in ipairs(XP_TYPES) do
    p.xp[i] = try("Experience.Get", Experience.Get, entity, t) or 0
  end
  p.worn = {}
  for _, item in ipairs(try("GetAllWornItems", AppearanceModifierManager.GetAllWornItems, entity) or {}) do
    local id = record_id(item)
    if id then p.worn[#p.worn + 1] = id end
  end
  p.weap = {}
  for _, slot in ipairs(WEAPON_SLOTS) do
    local w = try("GetEntityInSlot", Carrying.GetEntityInSlot, entity, slot)
    if w and w:IsAlive() then
      local id = record_id(w)
      if id then p.weap[#p.weap + 1] = id end
    end
  end
  -- v2 fields
  p.hgt = try("GetHeight", Stats.GetHeight, entity)
  p.age = try("GetAge", Age.GetAge, entity)
  p.xpt = {}
  for i, t in ipairs(XP_TYPES) do
    p.xpt[i] = try("Experience.GetTotalEarned", Experience.GetTotalEarned, entity, t) or 0
  end
  p.title = try("GetTitle", Stats.GetTitle, entity)
  -- Items: record id -> count, read in one pass; itemtotal lets the reader
  -- check nothing was skipped (an item whose record could not be read).
  p.items = {}
  p.itemtotal = try("GetNumberOfItems", Inventory.GetNumberOfItems, entity) or 0
  for i = 0, p.itemtotal - 1 do
    local item = try("GetItemByIndex", Inventory.GetItemByIndex, entity, i)
    local id = item and record_id(item)
    if id then p.items[id] = (p.items[id] or 0) + 1 end
  end
  return p
end

local function num(v)
  return v and string.format("%.4f", v) or ""
end

function CoopHero.Serialize(p)
  local ab = {}
  for _, a in ipairs(p.ab) do ab[#ab + 1] = a[1] .. ":" .. a[2] end
  local xp = {}
  for i, v in ipairs(p.xp) do xp[i] = num(v) end
  return "COOPHERO:v1|g=" .. (p.male and "M" or "F") .. "|mor=" .. num(p.mor) ..
    "|pur=" .. num(p.pur) .. "|fat=" .. num(p.fat) .. "|str=" .. num(p.str) ..
    "|skl=" .. num(p.skl) .. "|wil=" .. num(p.wil) .. "|ren=" .. tostring(p.ren or "") ..
    "|ab=" .. table.concat(ab, ",") .. "|xp=" .. table.concat(xp, ",") ..
    "|worn=" .. table.concat(p.worn, ",") .. "|weap=" .. table.concat(p.weap, ",")
end

-- v2 line: the v1 fields plus the v2 ones. Items are sorted by record id so
-- two captures of the same hero give the same line (coop_diff.py).
function CoopHero.SerializeV2(p, rev)
  local xpt = {}
  for i, v in ipairs(p.xpt or {}) do xpt[i] = num(v) end
  local ids = {}
  for id in pairs(p.items or {}) do ids[#ids + 1] = id end
  table.sort(ids)
  local items = {}
  for _, id in ipairs(ids) do items[#items + 1] = id .. ":" .. p.items[id] end
  local v1 = CoopHero.Serialize(p):gsub("^COOPHERO:v1|", "")
  return "COOPHERO:v2|" .. v1 .. "|hgt=" .. num(p.hgt) .. "|age=" .. tostring(p.age or "") ..
    "|xpt=" .. table.concat(xpt, ",") .. "|items=" .. table.concat(items, ",") ..
    "|itemtotal=" .. tostring(p.itemtotal or "") .. "|title=" .. tostring(p.title or "") ..
    "|rev=" .. tostring(rev or 0)
end

function CoopHero.Parse(line)
  local version, body = line:match("^COOPHERO:(v[12])|(.*)$")
  if not body then return nil end
  local f = {}
  for k, v in body:gmatch("(%w+)=([^|]*)") do f[k] = v end
  local p = {male = f.g ~= "F", ab = {}, xp = {}, worn = {}, weap = {}, version = version}
  for _, k in ipairs({"mor", "pur", "fat", "str", "skl", "wil", "ren"}) do p[k] = tonumber(f[k]) end
  for a, l in (f.ab or ""):gmatch("(%-?%d+):(%-?%d+)") do p.ab[#p.ab + 1] = {tonumber(a), tonumber(l)} end
  for v in (f.xp or ""):gmatch("[^,]+") do p.xp[#p.xp + 1] = tonumber(v) end
  for v in (f.worn or ""):gmatch("%x+") do p.worn[#p.worn + 1] = v end
  for v in (f.weap or ""):gmatch("%x+") do p.weap[#p.weap + 1] = v end
  if version == "v2" then
    p.hgt, p.age, p.title = tonumber(f.hgt), tonumber(f.age), f.title
    p.itemtotal = tonumber(f.itemtotal)
    p.xpt, p.items = {}, {}
    for v in (f.xpt or ""):gmatch("[^,]+") do p.xpt[#p.xpt + 1] = tonumber(v) end
    for id, n in (f.items or ""):gmatch("(%x+):(%d+)") do p.items[id] = tonumber(n) end
  end
  return p
end

-- Stats are applied as deltas (the engine exposes Modify*, not Set*).
local function move_to(label, getter, modifier, entity, target)
  if not target or not modifier then return end
  local current = try(label .. " get", getter, entity)
  if current then try(label .. " modify", modifier, entity, target - current) end
end

-- True when the creature already has the item: in its inventory, or (weapons)
-- in a hand or sheath slot, where an equipped weapon is not counted as an
-- inventory item. Continue restores onto a player 2 that may already carry the
-- saved kit (feature 002 L4, 2026-10-25: every worn item, the sword and the
-- pistol came back twice, 12 -> 20 items).
--
-- Counted by walking the inventory the way Capture does: GetNumberOfItemsOfType
-- read 0 for worn clothes player 2 held (continue2, 2026-10-25: each continue
-- gave every worn item once more, 5 -> 10 -> 15).
local function count_of(entity, hex)
  local n = 0
  for i = 0, (try("GetNumberOfItems", Inventory.GetNumberOfItems, entity) or 0) - 1 do
    local item = try("GetItemByIndex", Inventory.GetItemByIndex, entity, i)
    if item and record_id(item) == hex then n = n + 1 end
  end
  return n
end

local function owns(entity, hex, id)
  if count_of(entity, hex) > 0 then return true end
  for _, slot in ipairs(WEAPON_SLOTS) do
    local w = try("GetEntityInSlot", Carrying.GetEntityInSlot, entity, slot)
    if w and w:IsAlive() and record_id(w) == hex then return true end
  end
  return false
end

-- The saved weapons, added only when still missing from the inventory and
-- the hand/sheath slots once player 2 is fully up (batch1, 2026-10-26: a
-- restore right after the spawn found the slots still empty and added the
-- sword and the pistol again as inventory items, 5 -> 7). Returns true when
-- done; false while every slot is still empty (the caller tries again), unless
-- final.
function CoopHero.ApplyWeapons(entity, p, final)
  local any = false
  for _, slot in ipairs(WEAPON_SLOTS) do
    local w = try("GetEntityInSlot", Carrying.GetEntityInSlot, entity, slot)
    if w and w:IsAlive() then any = true end
  end
  if not any and not final and #p.weap > 0 then
    print("[coop] weapons: player 2's weapon slots still empty, waiting")
    return false
  end
  local added, had = 0, 0
  for _, hex in ipairs(p.weap) do
    local id = id_from_hex(hex)
    if id and not owns(entity, hex, id) then
      try("AddItemOfType " .. hex, Inventory.AddItemOfType, entity, id)
      added = added + 1
    elseif id then
      had = had + 1
    end
  end
  print("[coop] weapons: added " .. added .. ", already had " .. had)
  return true
end

function CoopHero.Apply(entity, p, opts)
  opts = opts or {}
  if opts.body ~= false then
    try("ChangePlayerEntityType", Player.ChangePlayerEntityType, entity,
      p.male and "CreatureHero" or "CreatureHeroFemale")
  end
  move_to("Morality", Stats.GetMorality, Stats.ModifyMorality, entity, p.mor)
  move_to("Purity", Stats.GetPurity, Stats.ModifyPurity, entity, p.pur)
  move_to("Fat", Stats.GetFat, Stats.ModifyFat, entity, p.fat)
  move_to("Strength", Stats.GetStrength, Stats.ModifyStrength, entity, p.str)
  for _, a in ipairs(p.ab) do
    try("SetHeroAbilityLevel", Stats.SetHeroAbilityLevel, entity, a[1], a[2])
  end
  if opts.clothes ~= false then
    for _, hex in ipairs(p.worn) do
      local id = id_from_hex(hex)
      if id then
        -- Wearing takes an item the creature owns: give it first, unless it has one.
        if not owns(entity, hex, id) then try("AddItemOfType " .. hex, Inventory.AddItemOfType, entity, id) end
        try("AddAndReplaceItemOfRecordID " .. hex,
          AppearanceModifierManager.AddAndReplaceItemOfRecordID, entity, id)
      end
    end
  end
  if opts.weapons ~= false then
    for _, hex in ipairs(p.weap) do
      local id = id_from_hex(hex)
      if id and not owns(entity, hex, id) then try("AddItemOfType " .. hex, Inventory.AddItemOfType, entity, id) end
    end
  end
  if p.version == "v2" and opts.v2 ~= false then
    move_to("Height", Stats.GetHeight, Stats.ModifyHeight, entity, p.hgt)
    if p.age then try("SetAge", Age.SetAge, entity, p.age) end
    for i, t in ipairs(XP_TYPES) do
      local want = p.xp[i]
      local have = want and try("Experience.Get", Experience.Get, entity, t)
      if have and want ~= have then
        try("Experience.Modify", Experience.Modify, entity, t, want - have, false)
      end
    end
    -- Items are only ever added up to the saved count, never removed: a hero
    -- holding more than the file says keeps it (reported by coop_diff.py).
    for hex, want in pairs(p.items or {}) do
      local id = id_from_hex(hex)
      if id then
        local have = count_of(entity, hex)
        for _ = have + 1, want do try("AddItemOfType " .. hex, Inventory.AddItemOfType, entity, id) end
      else
        print("[coop] item " .. hex .. " unknown in this game; skipped")
      end
    end
  end
end

function CoopHero.Describe(label, entity)
  if not entity then
    print("[coop] " .. label .. ": none")
    return
  end
  local alive = try("IsAlive", function() return entity:IsAlive() end)
  local name = try("GetName", function() return entity:GetName() end)
  print("[coop] " .. label .. ": name=" .. tostring(name) .. " alive=" .. tostring(alive))
  if alive then
    print("[coop]   " .. CoopHero.Serialize(CoopHero.Capture(entity)))
  end
end
