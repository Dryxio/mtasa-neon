local playerVehicles = {}
local testPositions = {
    [-9999] = true,
    [-9000] = true,
    [-8999] = true,
    [-4096] = true,
    [-4095] = true,
    [-3000] = true,
    [-2999] = true,
    [2999] = true,
    [3000] = true,
    [4095] = true,
    [4096] = true,
    [8999] = true,
    [9000] = true,
    [9999] = true,
}

local function removeVehicle(player)
    local vehicle = playerVehicles[player]
    if isElement(vehicle) then
        destroyElement(vehicle)
    end
    playerVehicles[player] = nil
end

addCommandHandler("radartest", function(player, _, requestedPosition)
    local position = tonumber(requestedPosition) or 9000
    if not testPositions[position] then
        outputChatBox("[Radar test] Valeurs: +/-2999, 3000, 4095, 4096, 8999, 9000, 9999.", player, 255, 180, 80)
        return
    end

    removeVehicle(player)
    local vehicle = createVehicle(411, position, 0, 25, 0, 0, 90)
    if not vehicle then
        outputChatBox("[Radar test] Echec de creation du vehicule.", player, 255, 80, 80)
        return
    end
    playerVehicles[player] = vehicle
    setElementFrozen(vehicle, true)
    warpPedIntoVehicle(player, vehicle)
    setCameraTarget(player, player)
    outputChatBox(("[Radar test] Zone x=%d. /radarstats, /radarmissing, /radarreload, /radarback."):format(position), player, 80, 255, 160)
end)

addCommandHandler("radarback", function(player)
    removeVehicle(player)
    setElementPosition(player, 1481, -1771, 19)
    setCameraTarget(player, player)
end)

addEventHandler("onPlayerQuit", root, function()
    removeVehicle(source)
end)

addEventHandler("onPlayerWasted", root, function()
    removeVehicle(source)
end)
