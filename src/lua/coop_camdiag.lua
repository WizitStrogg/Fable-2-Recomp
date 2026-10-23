-- coop_camdiag.lua - prints who the camera follows on this PC (both PCs).
-- Run from the backtick console: luafile coop_camdiag

local function nm(e)
  if e == nil then return 'nil' end
  local ok, n = pcall(function() return e:GetName() end)
  return tostring(ok and n or e)
end
local function alive(e)
  local ok, a = pcall(function() return e and e:IsAlive() end)
  return tostring(ok and a)
end
local function same(a, b)
  local ok, r = pcall(function() return a ~= nil and b ~= nil and a == b end)
  return tostring(ok and r)
end

local hero, hench = GetPlayerHero(), GetPlayerHenchman()
print('[coop] camdiag hero=' .. nm(hero) .. ' alive=' .. alive(hero) ..
      ' henchman=' .. nm(hench) .. ' alive=' .. alive(hench))
local ok_mp, mp = pcall(function() return CameraFunctions:IsMultiplayer(hero) end)
print('[coop] camdiag IsMultiplayer=' .. tostring(ok_mp and mp) ..
      ' IgnoreMultiplayerCameras=' .. tostring(CameraManager.IgnoreMultiplayerCameras) ..
      ' FrameUpdate wrapped=' .. tostring(CameraManager.__CoopOrigFrameUpdate ~= nil and
                                           CameraManager.FrameUpdate ~= CameraManager.__CoopOrigFrameUpdate) ..
      ' Activate wrapped=' .. tostring(SACCamera.__CoopOrigActivate ~= nil and
                                         SACCamera.Activate ~= SACCamera.__CoopOrigActivate))
local cam = CameraManager.CurrentCamera
print('[coop] camdiag camera=' .. tostring(cam and cam.DebugName) ..
      ' player=' .. nm(cam and cam.Player) .. ' (hero? ' .. same(cam and cam.Player, hero) ..
      ', henchman? ' .. same(cam and cam.Player, hench) .. ')' ..
      ' secondary=' .. nm(cam and cam.Secondary))
for _, name in ipairs({'IsMultiplayerGame', 'IsLocalCoopGame'}) do
  local f = rawget(_G, name)
  if type(f) == 'function' then
    local ok, v = pcall(f)
    print('[coop] camdiag ' .. name .. '=' .. tostring(ok and v))
  end
end
