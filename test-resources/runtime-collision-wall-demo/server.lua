local spawnedVehicles = {}

local function destroyDemoVehicle(player)
    local vehicle = spawnedVehicles[player]
    if isElement(vehicle) then
        destroyElement(vehicle)
    end
    spawnedVehicles[player] = nil
end

addCommandHandler("wallcar", function(player)
    destroyDemoVehicle(player)

    local x, y, z = getElementPosition(player)
    local _, _, rz = getElementRotation(player)
    local radians = math.rad(rz)
    local spawnX = x - math.sin(radians) * 3.5
    local spawnY = y + math.cos(radians) * 3.5

    local vehicle = createVehicle(411, spawnX, spawnY, z + 0.8, 0, 0, rz)
    if not vehicle then
        outputChatBox("[runtime-wall] Could not create the test vehicle.", player, 255, 80, 80)
        return
    end

    spawnedVehicles[player] = vehicle
    warpPedIntoVehicle(player, vehicle)
    outputChatBox("[runtime-wall] Test car spawned. Drive straight into the wall.", player, 100, 255, 120)
end)

addCommandHandler("wallcarclear", function(player)
    destroyDemoVehicle(player)
end)

addEventHandler("onPlayerQuit", root, function()
    destroyDemoVehicle(source)
    spawnedVehicles[source] = nil
end)

addEventHandler("onResourceStart", resourceRoot, function()
    outputServerLog("[runtime-collision-wall-demo] Ready. Client: /wall. Vehicle: /wallcar")
end)
