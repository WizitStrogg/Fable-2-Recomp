-- Co-op campaign start (feature 001, T021): skip the childhood.
-- Run on the host as soon as a new game's scripts are up, before its gameflow
-- leaves GAMEFLOW_START. A fresh game moves itself to the childhood with
-- SetGameflowPosition(QC010) (log: "Skipping gameflow to 1"); this wrapper turns
-- that one call into SetGameflowPosition(QC060), so the gameflow runs its own
-- "Skipping QC010" branch (dog, child weapons, Rose's diary layer) and then
-- starts QC060_NewBeginning normally, which loads Bower Lake.
-- Skipping from inside the running childhood instead leaves QC060 waiting on
-- a black screen for a Bower Lake load that never comes (rig launches 5-7).
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
print("[coop] skip: wrapper installed (base " .. tostring(wrapped_base) .. ", own " ..
  tostring(wrapped_own) .. ") at gameflow position " .. tostring(where))
