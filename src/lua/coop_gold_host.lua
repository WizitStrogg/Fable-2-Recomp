-- Feature 001 rig (T034): a real 100-gold bag in the world, at the host's feet.
--   luafile coop_gold_host
local h = GetPlayerHero()
local p = h:GetPosition()
local ok, bag = pcall(Debug.CreateEntityAt, "ObjectInventoryGoldBag_100", "coop_gold_host",
  CVector3(p:GetX(), p:GetY(), p:GetZ()))
print("[coop] gold drop at the host: " .. tostring(ok and bag ~= nil))
