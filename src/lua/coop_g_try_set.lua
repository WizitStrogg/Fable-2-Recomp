-- Feature 001 rig (T021a): set the gender value itself (Gender.Set).
print("[coop] gender try set: " .. tostring(pcall(Gender.Set, GetPlayerHero(), EGender.EG_FEMALE)))
