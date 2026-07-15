local sessions = {}
local nextSessionId = 0

local RECORDING_ID = 207
local RECORDING_START = {2337.86, -1467.21, 22.82, 180}
local RECORDING_END = {2381.0720, -1528.4404, 23.6556}
local MINIMUM_ELAPSED = 6500
local MAXIMUM_ELAPSED = 12000

local function snapshotPlayer(player)
    local x, y, z = getElementPosition(player)
    local _, _, rotation = getElementRotation(player)
    return {
        x = x,
        y = y,
        z = z,
        rotation = rotation,
        interior = getElementInterior(player),
        dimension = getElementDimension(player),
    }
end

local function restorePlayer(player, snapshot)
    if not isElement(player) or not snapshot then
        return
    end
    removePedFromVehicle(player)
    setElementInterior(player, snapshot.interior)
    setElementDimension(player, snapshot.dimension)
    setElementPosition(player, snapshot.x, snapshot.y, snapshot.z)
    setElementRotation(player, 0, 0, snapshot.rotation)
end

local function destroySession(player, restore)
    local session = sessions[player]
    if not session then
        return
    end
    if isTimer(session.startTimer) then
        killTimer(session.startTimer)
    end
    if isTimer(session.cleanupTimer) then
        killTimer(session.cleanupTimer)
    end
    if isElement(session.ped) then
        destroyElement(session.ped)
    end
    if isElement(session.vehicle) then
        destroyElement(session.vehicle)
    end
    if restore then
        restorePlayer(player, session.snapshot)
    end
    sessions[player] = nil
end

local function requestCleanup(player, restore)
    local session = sessions[player]
    if not session then
        return false
    end
    triggerClientEvent(player, "nativeVehicleRecording:cancel", resourceRoot, session.id, session.vehicle)
    session.cleanupTimer = setTimer(function(target, expectedId)
        local active = sessions[target]
        if active and active.id == expectedId then
            destroySession(target, restore)
        end
    end, 150, 1, player, session.id)
    return true
end

addCommandHandler("nativecarrec", function(player)
    if sessions[player] then
        requestCleanup(player, false)
        outputChatBox("[native carrec] Nettoyage en cours; relancez la commande dans une seconde.", player, 255, 180, 80)
        return
    end

    local snapshot = snapshotPlayer(player)
    removePedFromVehicle(player)
    setElementInterior(player, 0)
    setElementDimension(player, 0)
    setElementPosition(player, 2347.0, -1479.0, 24.0)
    setElementRotation(player, 0, 0, 180)

    local vehicle = createVehicle(492, RECORDING_START[1], RECORDING_START[2], RECORDING_START[3], 0, 0, RECORDING_START[4])
    local ped = vehicle and createPed(270, RECORDING_START[1], RECORDING_START[2], RECORDING_START[3] + 1, RECORDING_START[4]) or nil
    if not isElement(vehicle) or not isElement(ped) then
        if isElement(vehicle) then destroyElement(vehicle) end
        if isElement(ped) then destroyElement(ped) end
        restorePlayer(player, snapshot)
        outputChatBox("[native carrec] Impossible de creer Sweet ou la Greenwood.", player, 255, 80, 80)
        return
    end

    warpPedIntoVehicle(ped, vehicle, 1)
    setElementSyncer(ped, player, true, true)
    setElementSyncer(vehicle, player, true, true)

    nextSessionId = nextSessionId + 1
    local session = {
        id = nextSessionId,
        player = player,
        ped = ped,
        vehicle = vehicle,
        snapshot = snapshot,
        requestedAt = getTickCount(),
    }
    sessions[player] = session

    outputChatBox("[native carrec] Recording 207: Sweet passager, conducteur vide, client double-syncer.", player, 100, 220, 130)
    outputChatBox("La trajectoire dure environ 8 s. /nativecarreccleanup pour arreter.", player, 210, 210, 210)
    session.startTimer = setTimer(function(target, expectedId)
        local active = sessions[target]
        if active and active.id == expectedId then
            triggerClientEvent(target, "nativeVehicleRecording:start", resourceRoot, active.id, active.ped, active.vehicle, RECORDING_ID)
        end
    end, 1000, 1, player, session.id)
end)

addEvent("nativeVehicleRecording:result", true)
addEventHandler("nativeVehicleRecording:result", resourceRoot, function(sessionId, vehicle, result, details, elapsed)
    local player = client
    local session = sessions[player]
    if source ~= resourceRoot or not session or session.id ~= tonumber(sessionId) or session.vehicle ~= vehicle then
        outputDebugString("[native carrec] Rejected stale or unauthorized result", 2)
        return
    end

    local good = result == "guards" or result == "requested" or result == "loaded" or result == "started" or result == "completed" or result == "cancelled"
    outputDebugString(("[native carrec] client=%s elapsed=%s: %s"):format(tostring(result), tostring(elapsed or "-"), tostring(details or "")),
                      good and 3 or 2)
    outputChatBox(("[native carrec] %s: %s"):format(tostring(result), tostring(details or "")), player, good and 100 or 255, good and 220 or 80,
                  good and 130 or 80)

    if result == "completed" then
        local x, y, z = getElementPosition(vehicle)
        local endDistance = getDistanceBetweenPoints3D(x, y, z, RECORDING_END[1], RECORDING_END[2], RECORDING_END[3])
        local elapsedMs = tonumber(elapsed) or 0
        local passed = elapsedMs >= MINIMUM_ELAPSED and elapsedMs <= MAXIMUM_ELAPSED and endDistance <= 6 and getElementSyncer(vehicle) == player and
                           getPedOccupiedVehicle(session.ped) == vehicle and getPedOccupiedVehicleSeat(session.ped) == 1
        outputChatBox(("[native carrec] %s: fin a %.2f m, %d ms, Sweet passager."):format(passed and "PASS" or "FAIL", endDistance, elapsedMs),
                      player, passed and 100 or 255, passed and 230 or 80, passed and 130 or 80)
    end
end)

addCommandHandler("nativecarreccleanup", function(player)
    if not requestCleanup(player, true) then
        outputChatBox("[native carrec] Aucun test actif.", player, 255, 170, 80)
    end
end)

addEventHandler("onPlayerQuit", root, function()
    destroySession(source, false)
end)

addEventHandler("onResourceStop", resourceRoot, function()
    for player in pairs(sessions) do
        destroySession(player, true)
    end
end)
