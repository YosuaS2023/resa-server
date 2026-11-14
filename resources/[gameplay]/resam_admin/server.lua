-- resam_admin: Administrative commands & tools
print("[resam_admin] Loading Admin Suite...")

RegisterCommand("veh", function(source, args)
    local model = tonumber(args[1]) or 411 -- Default to Infernus (411)
    local col1 = tonumber(args[2]) or -1
    local col2 = tonumber(args[3]) or -1

    local x, y, z = GetPlayerPos(source)
    local angle = GetPlayerFacingAngle(source) or 0.0

    local veh = CreateVehicle(model, x + 2.0, y, z, angle, col1, col2)
    if veh then
        PutPlayerInVehicle(source, veh, 0)
        SendClientMessage(source, 0x00FF00FF, string.format("Kendaraan model %d berhasil di-spawn (ID: %d)!", model, veh))
    else
        SendClientMessage(source, 0xFF0000FF, "Gagal membuat kendaraan.")
    end
end)

RegisterCommand("heal", function(source, args)
    local target = tonumber(args[1]) or source
    SetPlayerHealth(target, 100.0)
    SetPlayerArmour(target, 100.0)
    SendClientMessage(source, 0x00FF00FF, string.format("Health dan Armour Player ID %d telah dipulihkan!", target))
end)

RegisterCommand("setskin", function(source, args)
    local target = tonumber(args[1])
    local skin = tonumber(args[2])
    if not target or not skin then
        SendClientMessage(source, 0xFFFF00FF, "Gunakan: /setskin <playerId> <skinId>")
        return
    end
    SetPlayerSkin(target, skin)
    SendClientMessage(source, 0x00FF00FF, string.format("Skin Player %d diubah ke %d.", target, skin))
end)

RegisterCommand("goto", function(source, args)
    local target = tonumber(args[1])
    if not target then
        SendClientMessage(source, 0xFFFF00FF, "Gunakan: /goto <playerId>")
        return
    end
    local x, y, z = GetPlayerPos(target)
    if x then
        SetPlayerPos(source, x + 1.0, y + 1.0, z)
        SendClientMessage(source, 0x00FF00FF, string.format("Teleport ke player ID %d.", target))
    else
        SendClientMessage(source, 0xFF0000FF, "Player tidak ditemukan.")
    end
end)

RegisterCommand("gethere", function(source, args)
    local target = tonumber(args[1])
    if not target then
        SendClientMessage(source, 0xFFFF00FF, "Gunakan: /gethere <playerId>")
        return
    end
    local x, y, z = GetPlayerPos(source)
    if x then
        SetPlayerPos(target, x + 1.0, y, z)
        SendClientMessage(source, 0x00FF00FF, string.format("Player %d diteleport ke lokasimu.", target))
    end
end)

RegisterCommand("givegun", function(source, args)
    local target = tonumber(args[1])
    local weapon = tonumber(args[2])
    local ammo = tonumber(args[3]) or 100
    if not target or not weapon then
        SendClientMessage(source, 0xFFFF00FF, "Gunakan: /givegun <playerId> <weaponId> [ammo]")
        return
    end
    GivePlayerWeapon(target, weapon, ammo)
    SendClientMessage(source, 0x00FF00FF, string.format("Memberikan senjata %d (%d peluru) ke Player %d.", weapon, ammo, target))
end)

RegisterCommand("givemoney", function(source, args)
    local target = tonumber(args[1])
    local amount = tonumber(args[2])
    if not target or not amount then
        SendClientMessage(source, 0xFFFF00FF, "Gunakan: /givemoney <playerId> <amount>")
        return
    end
    GivePlayerMoney(target, amount)
    SendClientMessage(source, 0x00FF00FF, string.format("Memberikan $%d ke Player %d.", amount, target))
end)

RegisterCommand("kick", function(source, args)
    local target = tonumber(args[1])
    if not target then
        SendClientMessage(source, 0xFFFF00FF, "Gunakan: /kick <playerId>")
        return
    end
    local name = GetPlayerName(target) or "Player"
    SendClientMessageToAll(0xFF0000FF, string.format("[Admin] %s (ID %d) telah di-kick oleh Admin.", name, target))
    KickPlayer(target)
end)

print("[resam_admin] Admin Suite loaded successfully!")
