-- CoopAbilities (001up, partner): the Abilities page's hero record. The recomp's
-- kit channel (fable2_coop_kit.h abilities_tick) runs this body with three locals
-- put in front of it: XP = player 2's {general, strength, skill, will} from the
-- host's kit_state, BASE = this record's ability levels when the check was armed
-- (index -> level), ARMED = true once a pass found nothing to change.
--
-- The page reads the joiner's own hero record (GetPlayerHero(), as its purse
-- does: rig 2026-10-28, the page showed the mirrored purse and 0 XP while the
-- host's player 2 had 1500/5000). Player 2's experience is put on that record.
-- Levels are never written here (Stats.SetHeroAbilityLevel on this record crashed
-- the partner, proof pair 8): the page's own Learn raises the record's level, and a
-- level above BASE is reported (COOPLEARN, the rise and the experience left) for the
-- host to apply to player 2.
--
-- Written flat, in the form of the rig probes that were safe on the partner (one
-- block of locals, direct pcalls): the same calls through coop_kit.lua's vararg
-- try() and a global CoopKit table crashed the partner a few ms after the chunk
-- (proof pairs 1 and 4, probe pairs 3, 5, 6; 2026-10-28). The ability order is
-- coop_kit.lua's ABILITIES: the index travels.
local hero = GetPlayerHero()
if hero then
  local names = {"STRENGTH_MELEE_WEAPONS", "STRENGTH_PHYSIQUE", "STRENGTH_TOUGHNESS", "SKILL_RANGED_WEAPONS",
                 "SKILL_ACCURACY", "SKILL_SPEED", "WILL_LIGHTNING", "WILL_FIREBALL", "WILL_SLOW_TIME", "WILL_SWORDS",
                 "WILL_VORTEX", "WILL_CHAOS", "WILL_FORCE_PUSH", "WILL_DEAD_RISING"}
  local types = {"GENERAL", "STRENGTH", "SKILL", "WILL"}
  local cur, cur_xp, rose = {}, {}, {}
  for i, k in ipairs(names) do
    local o, v = pcall(Stats.GetHeroAbilityLevel, hero, EHeroAbilityType["HERO_ABILITY_" .. k])
    if o and v then
      local l = math.floor(v)
      cur[#cur + 1] = (i - 1) .. ":" .. l
      if ARMED and l > (BASE[i - 1] or 0) then rose[#rose + 1] = (i - 1) .. ":" .. (l - (BASE[i - 1] or 0)) end
    end
  end
  for i, t in ipairs(types) do
    local o, v = pcall(Experience.Get, hero, EExperienceType["EXPERIENCE_" .. t])
    cur_xp[i] = o and math.floor(v or 0) or 0
  end
  if #rose > 0 then
    print("COOPLEARN|rise=" .. table.concat(rose, ",") .. "|xp=" .. table.concat(cur_xp, ",") .. "|cur=" .. table.concat(cur, ","))
  else
    -- Not Experience.Modify: +1500/+5000 crashed the partner (the notches notify its
    -- creature). Type 99 asks the recomp's Experience thunk to write XP natively.
    local n = 0
    for i = 1, 4 do
      if cur_xp[i] ~= XP[i] then n = n + 1 end
    end
    if n > 0 then pcall(Experience.Get, hero, 99) end
    print("COOPMIRROR|n=" .. n .. "|xp=" .. table.concat(XP, ",") .. "|cur=" .. table.concat(cur, ","))
  end
end
