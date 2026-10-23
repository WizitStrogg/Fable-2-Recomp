-- Co-op campaign start (feature 001, T021): skip the childhood.
-- Run on the host as soon as a new game's scripts are up, before its gameflow
-- leaves GAMEFLOW_START. A fresh game moves itself to the childhood with
-- SetGameflowPosition(QC010) (log: "Skipping gameflow to 1"); this wrapper turns
-- that one call into SetGameflowPosition(QC060), so the gameflow runs its own
-- "Skipping QC010" branch (dog, child weapons, Rose's diary layer) and then
-- starts QC060_NewBeginning normally, which loads Bower Lake.
-- Skipping from inside the running childhood instead leaves QC060 waiting on
-- a black screen for a Bower Lake load that never comes (rig launches 5-7).
if not (GameflowBase and Gameflow and ScriptEnum and ScriptEnum.DebugQC060) then
  print("[coop] skip: not yet (no gameflow in this Lua state)")
  return
end
if CoopSkipInstalled then return end
local target = ScriptEnum.DebugQC060
local function wrap(owner)
  local orig = rawget(owner, "SetGameflowPosition")
  if not orig or rawget(owner, "CoopSkipOrig") then return false end
  owner.CoopSkipOrig = orig
  owner.SetGameflowPosition = function(self, pos, no_question)
    if not CoopSkipDone and pos and pos < target then
      CoopSkipDone = true
      print("[coop] skip: new game start " .. tostring(pos) .. " redirected to QC060 (" .. tostring(target) .. ")")
      return orig(self, target, true)
    end
    return orig(self, pos, no_question)
  end
  return true
end
local where = Gameflow and Gameflow.PositionInGameflow
local wrapped_base = wrap(GameflowBase)
local wrapped_own = Gameflow and wrap(Gameflow)
CoopSkipInstalled = true
print("[coop] skip: wrapper installed (base " .. tostring(wrapped_base) .. ", own " ..
  tostring(wrapped_own) .. ") at gameflow position " .. tostring(where))

-- QC060's opening ends with Multiplayer.DisplayCoopScreen(), the game's own
-- co-op introduction; only after it is the adult world ready for a partner
-- (rig launch 9: a join during the opening crashed the host). Report it.
if not CoopScreenWrapped and Multiplayer and Multiplayer.DisplayCoopScreen then
  local ok, err = pcall(function()
    local orig = Multiplayer.DisplayCoopScreen
    Multiplayer.DisplayCoopScreen = function(...)
      local r = { orig(...) }
      print("[coop] skip: co-op screen done, adulthood ready for the partner")
      return unpack(r)
    end
  end)
  CoopScreenWrapped = ok
  print("[coop] skip: co-op screen hook " .. (ok and "installed" or ("failed: " .. tostring(err))))
end
