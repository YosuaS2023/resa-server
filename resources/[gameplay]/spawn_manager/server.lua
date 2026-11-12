-- spawn_manager: Handles player spawn location and skins
print("[spawn_manager] Initializing Spawn Manager...")

-- Default Spawn coordinates (e.g. Pershing Square, Los Santos)
local DEFAULT_SPAWN = {
    x = 1481.0,
    y = -1750.0,
    z = 15.5,
    skin = 299 -- Claude skin
}

AddEventHandler('playerSpawn', function(playerId)
    print(string.format("[spawn_manager] Spawning player ID: %d", playerId))
    
    SetPlayerPos(playerId, DEFAULT_SPAWN.x, DEFAULT_SPAWN.y, DEFAULT_SPAWN.z)
    SetPlayerSkin(playerId, DEFAULT_SPAWN.skin)
    SetPlayerHealth(playerId, 100.0)
    SetPlayerArmour(playerId, 50.0)

    SendClientMessage(playerId, 0x00FF00FF, "You have been spawned at Pershing Square.")
end)

print("[spawn_manager] Spawn Manager loaded successfully!")
