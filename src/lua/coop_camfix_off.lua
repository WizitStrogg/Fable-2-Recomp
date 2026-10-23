-- coop_camfix_off.lua - undo coop_camfix (camera modes get the game's player again).
-- Run from the backtick console: luafile coop_camfix_off

local modes = {SACCamera, MagicSACCamera, RangedCombatCamera, FirstPersonCamera,
               CombatCannedActionCamera, CannedActionCamera, SimpleCamera}
for _, mode in ipairs(modes) do
  if type(mode) == 'table' and mode.__CoopOrigActivate then
    mode.Activate = mode.__CoopOrigActivate
  end
end
CameraManager.CurrentSet = nil
CameraManager.CurrentCamera = nil
CameraManager.CurrentSetPriority = nil
print('[coop] camfix off')
