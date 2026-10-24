-- Feature 001 rig (T034): is the last dropped gold bag still lying there?
print("[coop] gold bag alive: " .. tostring(CoopGoldBag ~= nil and CoopGoldBag:IsAlive()))
