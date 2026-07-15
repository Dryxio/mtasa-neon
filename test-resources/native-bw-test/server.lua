-- This resource deliberately owns no Bullworth models, TXDs, COLs, or IPLs.
-- It only provides repeatable travel commands while GTA's process-global
-- native streaming registrar owns the city lifecycle.

local bullworthSpawn = {x = -8150, y = 7650, z = 25}
local sanAndreasSpawn = {x = 1481, y = -1771, z = 19}
local testVehicles = {}

local function destroyTestVehicle(player)
    local vehicle = testVehicles[player]
    if isElement(vehicle) then
        destroyElement(vehicle)
    end
    testVehicles[player] = nil
end

local function leaveCurrentVehicle(player)
    local vehicle = getPedOccupiedVehicle(player)
    if isElement(vehicle) then
        removePedFromVehicle(player)
    end
end

local function teleportWithVehicle(player, position, rotation)
    destroyTestVehicle(player)
    leaveCurrentVehicle(player)

    setElementInterior(player, 0)
    setElementDimension(player, 0)

    local vehicle = createVehicle(411, position.x, position.y, position.z, 0, 0, rotation)
    if not vehicle then
        setElementPosition(player, position.x, position.y, position.z)
        outputChatBox("[Native BW] Vehicule indisponible; teleportation a pied.", player, 255, 180, 80)
        return
    end

    testVehicles[player] = vehicle
    warpPedIntoVehicle(player, vehicle)
end

addCommandHandler("nativebw", function(player)
    teleportWithVehicle(player, bullworthSpawn, 90)
    outputChatBox("[Native BW] Bullworth. Explore les 7 quartiers puis /nativeback.", player, 80, 255, 160)
end)

addCommandHandler("nativeback", function(player)
    teleportWithVehicle(player, sanAndreasSpawn, 0)
    outputChatBox("[Native BW] Retour San Andreas. Reviens avec /nativebw pour tester le reload.", player, 80, 200, 255)
end)

addEventHandler("onPlayerQuit", root, function()
    destroyTestVehicle(source)
end)

addEventHandler("onPlayerWasted", root, function()
    destroyTestVehicle(source)
end)
