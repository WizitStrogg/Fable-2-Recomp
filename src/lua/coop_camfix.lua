-- coop_camfix.lua - JOINER PC, in the host's world: make the camera follow
-- your own character. Run from the backtick console: luafile coop_camfix
--
-- CameraManager:FrameUpdate activates camera modes through the native
-- ActivateCamera, which calls <Mode>:Activate(player_entity) with the world's
-- hero - on a joiner that is the host's hero. This swaps the entity in each
-- mode's Activate for your own character (GetPlayerHenchman on the joiner) and
-- forces one re-activation. Run it again after a level load if needed.
-- To undo: luafile coop_camfix_off

local function mine()
  local h = GetPlayerHenchman()
  if h and h:IsAlive() then return h end
end

local modes = {SACCamera, MagicSACCamera, RangedCombatCamera, FirstPersonCamera,
               CombatCannedActionCamera, CannedActionCamera, SimpleCamera}
local n = 0
for _, mode in ipairs(modes) do
  if type(mode) == 'table' and type(mode.Activate) == 'function' then
    mode.__CoopOrigActivate = mode.__CoopOrigActivate or mode.Activate
    local orig_activate = mode.__CoopOrigActivate
    mode.Activate = function(self, player_entity, ...)
      return orig_activate(self, mine() or player_entity, ...)
    end
    n = n + 1
  end
end
CameraManager.IgnoreMultiplayerCameras = true
CameraManager.CurrentSet = nil
CameraManager.CurrentCamera = nil
CameraManager.CurrentSetPriority = nil
print('[coop] camfix: wrapped ' .. n .. ' camera modes, own character found=' .. tostring(mine() ~= nil))
