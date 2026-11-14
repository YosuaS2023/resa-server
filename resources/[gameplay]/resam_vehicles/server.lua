-- resam_vehicles: Vehicle control and mechanics
print("[resam_vehicles] Initializing Vehicle Control Module...")

AddEventHandler('playerEnterVehicle', function(playerId, vehicleId, isPassenger)
    local seatName = isPassenger and "penumpang" or "pengemudi"
    SendClientMessage(playerId, 0x3498DBFF, string.format("Kamu masuk ke kendaraan ID %d sebagai %s. Ketik /engine untuk menyalakan mesin.", vehicleId, seatName))
end)

AddEventHandler('playerExitVehicle', function(playerId, vehicleId)
    SendClientMessage(playerId, 0x95A5A6FF, string.format("Kamu keluar dari kendaraan ID %d.", vehicleId))
end)

RegisterCommand("engine", function(source, args)
    local vehId = GetPlayerVehicleID(source)
    if not vehId then
        SendClientMessage(source, 0xFF0000FF, "Kamu harus berada di dalam kendaraan!")
        return
    end

    local params = GetVehicleParamsEx(vehId)
    if params then
        local newEngine = (params.engine == 1) and 0 or 1
        SetVehicleParamsEx(vehId, newEngine, -1, -1, -1, -1, -1, -1)
        if newEngine == 1 then
            SendClientMessage(source, 0x2ECC71FF, "[Kendaraan] Mesin dinyalakan.")
        else
            SendClientMessage(source, 0xE74C3CFF, "[Kendaraan] Mesin dimatikan.")
        end
    end
end)

RegisterCommand("lights", function(source, args)
    local vehId = GetPlayerVehicleID(source)
    if not vehId then
        SendClientMessage(source, 0xFF0000FF, "Kamu harus berada di dalam kendaraan!")
        return
    end

    local params = GetVehicleParamsEx(vehId)
    if params then
        local newLights = (params.lights == 1) and 0 or 1
        SetVehicleParamsEx(vehId, -1, newLights, -1, -1, -1, -1, -1)
        if newLights == 1 then
            SendClientMessage(source, 0xF1C40FFF, "[Kendaraan] Lampu dinyalakan.")
        else
            SendClientMessage(source, 0x95A5A6FF, "[Kendaraan] Lampu dimatikan.")
        end
    end
end)

RegisterCommand("lock", function(source, args)
    local vehId = GetPlayerVehicleID(source)
    if not vehId then
        SendClientMessage(source, 0xFF0000FF, "Kamu harus berada di dalam kendaraan!")
        return
    end

    local params = GetVehicleParamsEx(vehId)
    if params then
        local newDoors = (params.doors == 1) and 0 or 1
        SetVehicleParamsEx(vehId, -1, -1, -1, newDoors, -1, -1, -1)
        if newDoors == 1 then
            SendClientMessage(source, 0xE74C3CFF, "[Kendaraan] Pintu dikunci.")
        else
            SendClientMessage(source, 0x2ECC71FF, "[Kendaraan] Pintu dibuka kuncinya.")
        end
    end
end)

RegisterCommand("repair", function(source, args)
    local vehId = GetPlayerVehicleID(source)
    if not vehId then
        SendClientMessage(source, 0xFF0000FF, "Kamu harus berada di dalam kendaraan!")
        return
    end

    RepairVehicle(vehId)
    SetVehicleHealth(vehId, 1000.0)
    SendClientMessage(source, 0x2ECC71FF, "[Kendaraan] Kendaraan telah diperbaiki sepenuhnya!")
end)

RegisterCommand("color", function(source, args)
    local vehId = GetPlayerVehicleID(source)
    if not vehId then
        SendClientMessage(source, 0xFF0000FF, "Kamu harus berada di dalam kendaraan!")
        return
    end

    local col1 = tonumber(args[1]) or 0
    local col2 = tonumber(args[2]) or col1
    ChangeVehicleColor(vehId, col1, col2)
    SendClientMessage(source, 0x3498DBFF, string.format("[Kendaraan] Warna diubah menjadi (%d, %d).", col1, col2))
end)

print("[resam_vehicles] Vehicle Control Module loaded successfully!")
