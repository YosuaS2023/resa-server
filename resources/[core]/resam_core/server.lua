-- resam_core: Main server logic
print("[resam_core] Initializing Core Resource...")

AddEventHandler('playerConnect', function(playerId)
    local name = GetPlayerName(playerId)
    print(string.format("[resam_core] Player connected: %s (ID: %d)", name or "Unknown", playerId))
    SendClientMessageToAll(0x00FF00FF, string.format("%s joined the server.", name or "A player"))
end)

AddEventHandler('playerDisconnect', function(playerId, reason)
    local name = GetPlayerName(playerId)
    print(string.format("[resam_core] Player disconnected: %s (ID: %d, Reason: %d)", name or "Unknown", playerId, reason))
    SendClientMessageToAll(0xFF0000FF, string.format("%s left the server.", name or "A player"))
end)

AddEventHandler('playerCommand', function(playerId, cmd)
    print(string.format("[resam_core] Player %d executed command: %s", playerId, cmd))
    
    if cmd == "/pos" then
        local x, y, z = GetPlayerPos(playerId)
        SendClientMessage(playerId, 0xFFFF00FF, string.format("Your Position: X: %.2f, Y: %.2f, Z: %.2f", x, y, z))
    elseif cmd == "/help" then
        SendClientMessage(playerId, 0x00FFFFFF, "Welcome to resa-multiplayer! Powered by Lua & FiveM-style Resources.")
    end
end)

print("[resam_core] Core Resource successfully loaded!")
