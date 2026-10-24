-- Feature 001 rig: leave the world for the front end without saving
-- (Debug.QuitToFrontEnd; the E3 demo script uses it). Prints whether it ran.
--   luafile coop_quit
print("[coop] QuitToFrontEnd: " .. tostring(pcall(Debug.QuitToFrontEnd)))
