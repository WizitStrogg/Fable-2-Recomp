-- Feature 001 rig (T034): a collectible gold bag the way the game itself makes one
-- (the Crucible's reward thread, qz_cruciblecommon.lua: Debug.CreateEntityAtEntitysPosition
-- with ObjectInventoryGoldBag_100, collected when the bag stops being alive).
--   luafile coop_gold_drop   (CoopGoldFor = "p2" or "host" set first by coop_gold_for_*)
local who = CoopGoldFor or "p2"
local at = who == "host" and GetPlayerHero() or GetPlayerHenchman()
if not at or not at:IsAlive() then print("[coop] gold bag: no " .. who) return end
local ok, bag = pcall(Debug.CreateEntityAtEntitysPosition, "ObjectInventoryGoldBag_100", "coop_gold_" .. who, at)
CoopGoldBag = ok and bag or nil
print("[coop] gold bag for " .. who .. ": " .. tostring(ok and bag ~= nil and bag:IsAlive()))
