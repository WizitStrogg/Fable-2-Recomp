-- Feature 001 rig (T034): a real 100-gold bag in the world, at player 2's feet, for
-- the partner to pick up (the shared purse must count it once, in full).
--   luafile coop_gold_p2
local h = GetPlayerHenchman()
if not h or not h:IsAlive() then print("[coop] gold drop: no player 2") return end
local p = h:GetPosition()
local ok, bag = pcall(Debug.CreateEntityAt, "ObjectInventoryGoldBag_100", "coop_gold_p2",
  CVector3(p:GetX(), p:GetY(), p:GetZ()))
print("[coop] gold drop at player 2: " .. tostring(ok and bag ~= nil))
