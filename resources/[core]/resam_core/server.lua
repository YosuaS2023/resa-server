-- resam_core: Main server logic
print("[resam_core] Initializing Core Resource...")

-- Exported function example for other resources
exports("getOnlineCount", function()
    -- Sample exported function callable by other resources via exports['resam_core']:getOnlineCount()
    return 1
end)

AddEventHandler('playerConnect', function(playerId)
    local name = GetPlayerName(playerId)
    print(string.format("[resam_core] Player connected: %s (ID: %d)", name or "Unknown", playerId))
    SendClientMessageToAll(0x00FF00FF, string.format("%s joined the server.", name or "A player"))
    
    -- Welcome message with timer delay
    SetTimeout(function()
        SendClientMessage(playerId, 0x00FFFFFF, "Welcome to resa-multiplayer! Type /help or /menu.")
    end, 1500)
end)

AddEventHandler('playerDisconnect', function(playerId, reason)
    local name = GetPlayerName(playerId)
    print(string.format("[resam_core] Player disconnected: %s (ID: %d, Reason: %d)", name or "Unknown", playerId, reason))
    SendClientMessageToAll(0xFF0000FF, string.format("%s left the server.", name or "A player"))
end)

AddEventHandler('playerDeath', function(playerId, killerId, reason)
    local name = GetPlayerName(playerId) or "Unknown"
    if killerId and killerId ~= -1 then
        local killerName = GetPlayerName(killerId) or "Someone"
        SendClientMessageToAll(0xFF6347FF, string.format("%s was killed by %s (Reason: %d)", name, killerName, reason))
    else
        SendClientMessageToAll(0xFF6347FF, string.format("%s died. (Reason: %d)", name, reason))
    end
end)

AddEventHandler('playerClickMap', function(playerId, x, y, z)
    SendClientMessage(playerId, 0xFFFF00FF, string.format("[Map] Waypoint clicked at: X: %.1f, Y: %.1f, Z: %.1f", x, y, z))
end)

-- FiveM-Style RegisterCommand
RegisterCommand("pos", function(source, args)
    local x, y, z = GetPlayerPos(source)
    local angle = GetPlayerFacingAngle(source) or 0.0
    SendClientMessage(source, 0xFFFF00FF, string.format("Your Position: X: %.2f, Y: %.2f, Z: %.2f (Angle: %.1f)", x, y, z, angle))
end)

RegisterCommand("help", function(source, args)
    ShowPlayerDialog(
        source,
        1,
        DIALOG_STYLE_MSGBOX,
        "Bantuan Server ReSA",
        "Selamat datang di resa-multiplayer!\n\nPerintah Tersedia:\n/pos - Cek posisi koordinat\n/help - Dialog bantuan ini\n/menu - Menu interaktif\n/veh <model> - Spawn kendaraan\n/goto & /gethere - Teleport admin",
        "OK",
        "Tutup"
    )
end)

RegisterCommand("menu", function(source, args)
    ShowPlayerDialog(
        source,
        2,
        DIALOG_STYLE_LIST,
        "Menu Pilihan",
        "1. Dapatkan Senjata Deagle & M4\n2. Respawn Karakter\n3. Cek Health & Armour\n4. Dapatkan $10,000 Uang",
        "Pilih",
        "Batal"
    )
end)

AddEventHandler('playerDialogResponse', function(playerId, dialogId, response, listItem, inputText)
    if dialogId == 2 and response == 1 then
        if listItem == 0 then
            GivePlayerWeapon(playerId, WEAPON_DEAGLE or 24, 100)
            GivePlayerWeapon(playerId, WEAPON_M4 or 31, 300)
            SendClientMessage(playerId, 0x00FF00FF, "Kamu menerima Desert Eagle & M4 Carbine!")
        elseif listItem == 1 then
            SpawnPlayer(playerId)
            SendClientMessage(playerId, 0x00FF00FF, "Karakter kamu di-respawn!")
        elseif listItem == 2 then
            local hp = GetPlayerHealth(playerId) or 0
            local arm = GetPlayerArmour(playerId) or 0
            SendClientMessage(playerId, 0x00FFFFFF, string.format("Health: %.1f | Armour: %.1f", hp, arm))
        elseif listItem == 3 then
            GivePlayerMoney(playerId, 10000)
            SendClientMessage(playerId, 0x2ECC71FF, "Kamu menerima $10,000 tunai!")
        end
    end
end)

print("[resam_core] Core Resource successfully loaded!")
