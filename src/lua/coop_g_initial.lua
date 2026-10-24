-- Feature 001 rig (T021a): the record a new hero is created from, before a load.
print("[coop] gender initial set: " .. tostring(pcall(SetInitialHeroModelName, "CreatureHeroFemale")) ..
  " now " .. tostring(pcall(GetInitialHeroModelName) and GetInitialHeroModelName()))
