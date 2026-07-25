-- This resource deliberately owns no Bullworth models, TXDs, COLs, or IPLs.
-- It only provides repeatable travel commands while GTA's process-global
-- native streaming registrar owns the city lifecycle.

local viceCitySpawn = {x = 7343.25, y = -8416.45, z = 35}
local libertyCitySpawn = {x = 8377, y = 7882, z = 35}
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
    outputChatBox("[Native World] Bullworth n'est pas resident dans le checkpoint LOD VC/LC.", player, 255, 180, 80)
end)

addCommandHandler("nativecc", function(player)
    outputChatBox("[Native World] Carcer City n'est pas resident dans le checkpoint LOD VC/LC.", player, 255, 180, 80)
end)

addCommandHandler("nativevc", function(player)
    teleportWithVehicle(player, viceCitySpawn, 90)
    outputChatBox("[Native World] Vice City. Teste distance LOD, collisions et /nativeback.", player, 80, 255, 160)
end)

addCommandHandler("nativelc", function(player)
    teleportWithVehicle(player, libertyCitySpawn, 90)
    outputChatBox("[Native World] Liberty City. Teste distance LOD, collisions et /nativeback.", player, 80, 255, 160)
end)

addCommandHandler("nativeback", function(player)
    teleportWithVehicle(player, sanAndreasSpawn, 0)
    outputChatBox("[Native World] Retour San Andreas. Reviens avec /nativevc ou /nativelc pour tester le reload.", player, 80, 200, 255)
end)

addEventHandler("onPlayerQuit", root, function()
    destroyTestVehicle(source)
end)

addEventHandler("onPlayerWasted", root, function()
    destroyTestVehicle(source)
end)
