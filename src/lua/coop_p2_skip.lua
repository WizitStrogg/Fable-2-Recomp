-- Proof P2 (feature 001): skip a fresh New Game to the start of adulthood with
-- the game's own gameflow skip (research/03-story-sync.md section 4).
--   luafile coop_p2_skip.lua     then, once the skip settles: luafile coop_p2_state.lua
local ok, err = pcall(function()
  print("P2SKIP|before|gfpos=" .. tostring(Gameflow.PositionInGameflow) ..
    "|target=" .. tostring(ScriptEnum.DebugQC060))
  Gameflow:SetGameflowPosition(ScriptEnum.DebugQC060, true)
  print("P2SKIP|requested|skipto=" .. tostring(Gameflow.SkipToPositionInGameflow) ..
    "|skipping=" .. tostring(Gameflow.SkippingThroughGameflowFlag))
end)
if not ok then print("P2SKIP|error|" .. tostring(err)) end
