local spawnedVehicles = {}

local function destroyPreviousVehicle(player)
    local vehicle = spawnedVehicles[player]
    if isElement(vehicle) then
        destroyElement(vehicle)
    end
    spawnedVehicles[player] = nil
end

local function spawnTestVehicle(player, model)
    if not isElement(player) then
        return
    end

    local modelName = getVehicleNameFromModel(model)
    if not modelName then
        outputChatBox("[radio-selector] Invalid vehicle model.", player, 255, 90, 90)
        return
    end

    destroyPreviousVehicle(player)
    local x, y, z = getElementPosition(player)
    local _, _, rotation = getElementRotation(player)
    local vehicle = createVehicle(model, x + 2, y, z + 1.2, 0, 0, rotation)
    if not vehicle then
        outputChatBox("[radio-selector] Failed to create the test vehicle.", player, 255, 90, 90)
        return
    end

    setElementInterior(vehicle, getElementInterior(player))
    setElementDimension(vehicle, getElementDimension(player))
    spawnedVehicles[player] = vehicle
    warpPedIntoVehicle(player, vehicle)
end

addCommandHandler("radiocar", function(player, _, requestedModel)
    spawnTestVehicle(player, tonumber(requestedModel) or 411)
end)

addCommandHandler("radioplane", function(player)
    spawnTestVehicle(player, 513)
end)

addEventHandler("onPlayerQuit", root, function()
    destroyPreviousVehicle(source)
end)
