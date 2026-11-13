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
        ShowPlayerDialog(
            playerId,
            1,
            DIALOG_STYLE_MSGBOX,
            "Bantuan Server",
            "Selamat datang di resa-multiplayer!\n\nPerintah yang tersedia:\n/pos - Cek koordinat posisi\n/help - Buka dialog bantuan ini\n/menu - Contoh dialog list",
            "OK",
            "Tutup"
        )
    elseif cmd == "/menu" then
        ShowPlayerDialog(
            playerId,
            2,
            DIALOG_STYLE_LIST,
            "Menu Pilihan",
            "1. Dapatkan Senjata\n2. Respawn Karakter\n3. Cek Health & Armour",
            "Pilih",
            "Batal"
        )
    end
end)

AddEventHandler('playerDialogResponse', function(playerId, dialogId, response, listItem, inputText)
    print(string.format("[resam_core] Dialog Response - Player: %d, Dialog: %d, Response: %d, Item: %d, Text: '%s'",
        playerId, dialogId, response, listItem, inputText or ""))

    if dialogId == 2 and response == 1 then
        if listItem == 0 then
            SendClientMessage(playerId, 0x00FF00FF, "Kamu memilih opsi 1 (Senjata)!")
        elseif listItem == 1 then
            SpawnPlayer(playerId)
            SendClientMessage(playerId, 0x00FF00FF, "Karakter kamu di-respawn!")
        elseif listItem == 2 then
            local hp = GetPlayerHealth(playerId) or 0
            local arm = GetPlayerArmour(playerId) or 0
            SendClientMessage(playerId, 0x00FFFFFF, string.format("Health: %.1f | Armour: %.1f", hp, arm))
        end
    end
end)

print("[resam_core] Core Resource successfully loaded!")
