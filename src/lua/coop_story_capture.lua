-- coop_story_capture.lua: print this save's story state as COOPSTORY lines.
-- Read-only (changes nothing in the game or the save).
-- Run from the backtick console: luafile coop_story_capture
--
-- Output, split into parts of at most ~1500 characters:
--   COOPSTORY:v1|part=<i>/<n>|<field>=<value>|<field>=<value>...
-- Fields:
--   pos                 Gameflow.PositionInGameflow (ScriptEnum.DebugQC010 = 1 ... GAMEFLOW_END = 32)
--   gf.<path>           every saved plain value (boolean/number/string) in the Gameflow table,
--                       nested tables flattened with dots, entities and functions skipped
--   sd.<path>           the same for ScriptData (side-quest counters etc.)
--   q.<QuestName>       QuestTracker flags for that quest on the hero, as letters:
--                       R registered, U unlocked, A activated, C completed, P primary
--                       (quests the hero has never registered are left out)

local MAX_DEPTH = 4
local PART_LEN = 1500

local QUESTS = {
  "QC010_Childhood", "QC020_PoseForPicture", "QC030_BeetleHunter", "QC040_BulliesAndDog",
  "QC045_LoveLetter", "QC050_Wanted", "QC055_Wino", "QC060_NewBeginning", "QC065_Roadblock",
  "QC066_Gypsies", "QC070_Thag", "QC075_JourneyToRavenscar", "QC080_MonksQuest",
  "QC085_MonksQuest2", "QC090_Garth", "QC100_Fairfax", "QC110_RoadToWestcliff", "QC120_Arena",
  "QC130_TheCampInitiation", "QC140_Breakout", "QC150_BloodstoneOrBust", "QC160_CullisGate",
  "QC170_Stranded", "QC180_ThePirateKing", "QC200_BloodstoneAssault", "QC220_OpeningThePortal",
  "QC230_TheMusicBox", "QC235_MusicBoxAux", "QC240_Endgame", "QD010_Free",
  "QD010_KnotholeIsland", "QD010_KnotholeIslandPart2", "QD010_KnotholeIslandPart3",
  "QD020_LuckyDipShop", "QD030_DogShrine", "QD040_PhotoMode", "QD050_DLC2Free",
  "QD050_MurgoHolding", "QJ001_JobWoodsman", "QJ001_JobWoodsman_Active_Brightwood1",
  "QJ001_JobWoodsman_Active_Ravenscar1", "QJ001_JobWoodsman_Active_Ravenscar2",
  "QJ002_JobBlacksmith", "QJ002_JobBlacksmith_Active_BWSMarket1", "QJ003_JobBartender",
  "QJ003_JobBartender_Active_BWSMarket1", "QJ003_JobBartender_Active_Bloodstone1",
  "QJ003_JobBartender_Active_Dunecrest1", "QJ003_JobBartender_Active_Ravenscar1",
  "QJ003_JobBartender_Active_Westcliff1", "QJ004_JobBard", "QO010_ABridgeTooFar",
  "QO015_IntroToTempleOfEvil", "QO020_RescueMyBaby", "QO030_BarnumsDelivery",
  "QO040_ShipAttack", "QO050_HobbesOnTheFarm", "QO060_Gargoyles", "QO100_BrightwoodFarmer",
  "QO110_BrightwoodBandits", "QO130_IntroToBard", "QO135_Bard", "QO150_BigamyBlackmail",
  "QO160_CemeterySummoning", "QO170_TreasureIsland", "QO180_CrucibleRevisited",
  "QO190_HobbeSquatters", "QO200_DunecrestInn", "QO220_TOLRecruits", "QO225_TOLDonations",
  "QO230_TOESacrificeIntro", "QO235_TOESacrifices", "QO240_HauntedShore", "QO250_Hit",
  "QO270_Kidnap", "QO320_PunchClub1", "QO330_PunchClub2", "QO340_PunchClub3",
  "QO350_Sculptors", "QO390_SingingSword", "QO400_SlumsRevisited", "QO460_TOGFavour",
  "QO470_TOEFavour", "QO540_WestcliffShootingRange", "QO570_FrankenBride",
  "QO571_FrankenBrideExperiment", "QO600_BrightwoodFarmerRevisited",
  "QO620_WraithmarshSummoning", "QO630_TroubleInBloodstone", "QO650_WestcliffParty",
  "QO660_WestcliffDevelopment", "QO666_TestScript", "QO700_WeylandsDiary",
  "QO800_OwnershipFairfaxCastle", "QO805_OwnershipBookstore",
  "QO815_OwnershipGravekeepersMansion", "QO820_OwnershipBrightwoodTower",
  "QO850_OwnershipBowerMarket", "QO860_WraithmarshRevisited", "QO870_CostumeQuests",
  "QO880_Vision", "QO890_Colosseum", "QO900_DemonDoorDunecrest", "QO910_DemonDoorRavenscar",
  "QO920_DemonDoorWestcliffPreCave", "QO925_DemonDoorBrightwood_2", "QO930_DemonDoorBowerLake",
  "QO940_DemonDoorBrightwood", "QO950_DemonDoorHobbesCave", "QO960_DemonDoorFairfax",
  "QO970_DemonDoorWraithmarsh", "QO980_DemonDoorBWSCemetery", "QO990_DemonDoorBloodstone",
  "QRA001_Assassination"
}

local fields = {}

local function escape(s)
  return (tostring(s):gsub("[|\r\n]", function(c) return c == "|" and "/" or " " end))
end

local function add(key, value)
  fields[#fields + 1] = key .. "=" .. escape(value)
end

-- Flattens plain values of a table; skips functions, entities (userdata),
-- coroutines and anything already visited.
local function flatten(prefix, t, depth, seen)
  if depth > MAX_DEPTH or seen[t] then return end
  seen[t] = true
  local keys = {}
  for k in pairs(t) do
    if type(k) == "string" or type(k) == "number" then keys[#keys + 1] = k end
  end
  table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
  for _, k in ipairs(keys) do
    local v = rawget(t, k)
    local tv = type(v)
    local key = prefix .. "." .. tostring(k)
    if tv == "boolean" or tv == "number" or tv == "string" then
      add(key, tv == "boolean" and (v and "T" or "F") or v)
    elseif tv == "table" then
      flatten(key, v, depth + 1, seen)
    end
  end
end

local ok, err = pcall(function()
  add("pos", Gameflow and Gameflow.PositionInGameflow)
  if type(Gameflow) == "table" then flatten("gf", Gameflow, 1, {}) end
  if type(ScriptData) == "table" then flatten("sd", ScriptData, 1, {}) end

  local hero = QuestManager.HeroEntity
  for _, name in ipairs(QUESTS) do
    local function flag(fn, letter)
      local ok2, res = pcall(fn, hero, name)
      return (ok2 and res) and letter or ""
    end
    local registered = flag(QuestTracker.IsRegistered, "R")
    if registered ~= "" then
      add("q." .. name, registered .. flag(QuestTracker.IsUnlocked, "U") ..
        flag(QuestTracker.HasBeenActivated, "A") .. flag(QuestTracker.IsCompleted, "C") ..
        flag(QuestTracker.IsPrimary, "P"))
    end
  end
end)
if not ok then print("[coop] story capture error: " .. tostring(err)) end

-- Split into parts of at most PART_LEN characters.
local parts, cur = {}, ""
for _, f in ipairs(fields) do
  if #cur > 0 and #cur + #f + 1 > PART_LEN then
    parts[#parts + 1] = cur
    cur = ""
  end
  cur = (#cur > 0) and (cur .. "|" .. f) or f
end
if #cur > 0 then parts[#parts + 1] = cur end
for i, p in ipairs(parts) do
  print("COOPSTORY:v1|part=" .. i .. "/" .. #parts .. "|" .. p)
end
print("[coop] story capture: " .. #fields .. " fields in " .. #parts .. " part(s)")
