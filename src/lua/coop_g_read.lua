-- Feature 001 rig (T021a): the host hero's gender state.
--   luafile coop_g_read
local h = GetPlayerHero()
local function q(f, ...) local ok, r = pcall(f, ...) return ok and tostring(r) or ("err " .. tostring(r)) end
print("[coop] gender read: Gender.Get=" .. q(function() return Gender.Get(h) == EGender.EG_FEMALE and "F" or "M" end) ..
  " HasHadSexChange=" .. q(Gender.HasHadSexChange, h) ..
  " busy=" .. q(Action.IsPerformingAnyAction, h) ..
  " initial=" .. q(GetInitialHeroModelName))
