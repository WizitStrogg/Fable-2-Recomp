-- coop_f5_cleanup.lua - removes the F5 script menu from a save.
-- Run from the backtick console in your own world: luafile coop_f5_cleanup
-- then save the game.
--
-- The F5 menu (F5.lua) registers a persistent per-frame script with the
-- game's GeneralScriptManager. The manager saves every running script into
-- the save (GetSaveTable) and resumes it on load (LoadFromSave), so once F5
-- was used the menu script lives on in that save. This unlinks it from the
-- running-script list (the same way the manager drops a finished script) and
-- clears its state, so the next save no longer contains it.

local function is_f5_menu(v)
  return type(v) == "table" and v._Name == nil and rawget(v, "__index") == _G and
         rawget(v, "_G") == _G and type(rawget(v, "Update")) == "function" and
         getmetatable(v) == v
end

local removed = 0
local l = GeneralScriptManager.CurrentlyRunningScripts
while l do
  local nxt = l.next
  if is_f5_menu(l.value) then
    if l.next then l.next.prev = l.prev end
    if l.prev then l.prev.next = l.next end
    if GeneralScriptManager.CurrentlyRunningScripts == l then
      GeneralScriptManager.CurrentlyRunningScripts = l.next
    end
    if GeneralScriptManager.CurrentlyRunningScriptsEnd == l then
      GeneralScriptManager.CurrentlyRunningScriptsEnd = l.prev
    end
    removed = removed + 1
  end
  l = nxt
end
rawset(_G, "__f5_menu_state", nil)
print("[coop] f5 cleanup: removed " .. removed .. " F5 menu script(s); save the game to keep it")
