-- CoopAbilities (001up, partner): the Abilities page's hero record. The recomp's
-- kit channel (fable2_coop_kit.h abilities_tick) runs this body with four locals
-- put in front of it: XP = player 2's {general, strength, skill, will} and LVL =
-- player 2's ability levels (index -> level, 1 and up) from the host's kit_state,
-- BASE = this record's levels when the check was armed, ARMED = true once a pass
-- found nothing to change.
--
-- The page reads the joiner's own hero record (GetPlayerHero(), as its purse
-- does: rig 2026-10-28, the page showed the mirrored purse and 0 XP while the
-- host's player 2 had 1500/5000). Player 2's experience and levels are put on that
-- record (on a rejoin the record is the host save's hero again). A level is only
-- set to 1 and up: a pass that set seven abilities to 0 crashed the partner (proof
-- pair 8), while raises and a 2 -> 1 on the same record were safe (probe pair 14).
-- The page's own Learn raises the record's level, and a level above BASE is
-- reported (COOPLEARN, the rise and the experience left) for the host to apply.
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
  local cur, cur_xp, rose, BASE_CUR = {}, {}, {}, {}
  for i, k in ipairs(names) do
    local o, v = pcall(Stats.GetHeroAbilityLevel, hero, EHeroAbilityType["HERO_ABILITY_" .. k])
    if o and v then
      local l = math.floor(v)
      cur[#cur + 1] = (i - 1) .. ":" .. l
      BASE_CUR[i - 1] = l
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
    for i, k in ipairs(names) do
      local want, have = LVL[i - 1], BASE_CUR[i - 1]
      if want and want >= 1 and have and have ~= want then
        pcall(Stats.SetHeroAbilityLevel, hero, EHeroAbilityType["HERO_ABILITY_" .. k], want)
        n = n + 1
      end
    end
    for i = 1, 4 do
      if cur_xp[i] ~= XP[i] then n = n + 1 end
    end
    if n > 0 then pcall(Experience.Get, hero, 99) end
    print("COOPMIRROR|n=" .. n .. "|xp=" .. table.concat(XP, ",") .. "|cur=" .. table.concat(cur, ","))
  end
end
