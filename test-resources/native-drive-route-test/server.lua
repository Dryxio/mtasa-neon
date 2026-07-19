local sessions = {}
local nextSessionId = 0

local function nearestRoutePoint(x, y, z)
    local nearestIndex, nearestDistance
    for index, point in ipairs(NATIVE_DRIVE_ROUTE) do
        local distance = getDistanceBetweenPoints3D(x, y, z, point[1], point[2], point[3])
        if not nearestDistance or distance < nearestDistance then
            nearestIndex, nearestDistance = index - 1, distance
        end
    end
    return nearestIndex, nearestDistance
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
    if isElement(player) and isElement(session.ped) and isElement(session.vehicle) then
        triggerClientEvent(player, "nativeDriveRoute:stop", resourceRoot, session.id, session.ped, session.vehicle)
    end
    if isTimer(session.monitorTimer) then
        killTimer(session.monitorTimer)
    end
    if restore then
        restorePlayer(player, session.snapshot)
    end
    if isElement(session.ped) then
        destroyElement(session.ped)
    end
    if isElement(session.vehicle) then
        destroyElement(session.vehicle)
    end
    sessions[player] = nil
end

addCommandHandler("nativedriveroute", function(player)
    destroySession(player, true)
    if isPedInVehicle(player) then
        return outputChatBox("[native drive-route] Sors de ton vehicule avant de lancer le harness.", player, 255, 170, 80)
    end

    local x, y, z = getElementPosition(player)
    local _, _, rotation = getElementRotation(player)
    local snapshot = {
        x = x,
        y = y,
        z = z,
        rotation = rotation,
        interior = getElementInterior(player),
        dimension = getElementDimension(player),
    }

    local start = NATIVE_DRIVE_ROUTE_START
    local vehicle = createVehicle(412, start[1], start[2], start[3], 0, 0, start[4])
    local ped = vehicle and createPed(103, start[1], start[2], start[3] + 0.5, start[4]) or nil
    if not isElement(vehicle) or not isElement(ped) then
        if isElement(vehicle) then destroyElement(vehicle) end
        if isElement(ped) then destroyElement(ped) end
        return outputChatBox("[native drive-route] Creation Voodoo/Ballas impossible.", player, 255, 80, 80)
    end

    setElementInterior(vehicle, snapshot.interior)
    setElementInterior(ped, snapshot.interior)
    setElementDimension(vehicle, snapshot.dimension)
    setElementDimension(ped, snapshot.dimension)
    warpPedIntoVehicle(ped, vehicle, 0)
    warpPedIntoVehicle(player, vehicle, 1)
    setElementSyncer(ped, player, true, true)
    setElementSyncer(vehicle, player, true, true)

    nextSessionId = nextSessionId + 1
    local session = {
        id = nextSessionId,
        ped = ped,
        vehicle = vehicle,
        snapshot = snapshot,
        accepted = false,
        highestIndex = -1,
        serverSamples = 0,
    }
    sessions[player] = session

    outputChatBox("[native drive-route] Harness SWEET3 pret. Tu es passager du Voodoo Ballas.", player, 100, 220, 130)
    outputChatBox("Preuves separees: ACCEPT, INDEX natif, POS client et POS serveur. /nativedriveroutecleanup pour quitter.", player, 210, 210, 210)

    setTimer(function(targetPlayer, expectedId)
        local active = sessions[targetPlayer]
        if not active or active.id ~= expectedId then
            return
        end
        triggerClientEvent(targetPlayer, "nativeDriveRoute:start", resourceRoot, active.id, active.ped, active.vehicle)
        active.monitorTimer = setTimer(function()
            if not isElement(active.vehicle) then
                return
            end
            active.serverSamples = active.serverSamples + 1
            local currentX, currentY, currentZ = getElementPosition(active.vehicle)
            local nearestIndex, nearestDistance = nearestRoutePoint(currentX, currentY, currentZ)
            if active.serverSamples % 5 == 0 then
                outputDebugString(("[native drive-route] POS server id=%d xyz=%.3f,%.3f,%.3f nearest=%d distance=%.2f"):format(
                    active.id, currentX, currentY, currentZ, nearestIndex, nearestDistance), 3)
            end

            local final = NATIVE_DRIVE_ROUTE[#NATIVE_DRIVE_ROUTE]
            local finalDistance = getDistanceBetweenPoints3D(currentX, currentY, currentZ, final[1], final[2], final[3])
            if active.accepted and active.highestIndex == #NATIVE_DRIVE_ROUTE - 1 and not active.arrived and finalDistance <= 15.0 then
                active.arrived = true
                outputChatBox(("[native drive-route] POS serveur finale observee a %.2f m. Indice et position restent deux preuves distinctes."):format(finalDistance),
                              targetPlayer, 100, 230, 130)
            end
        end, 1000, 0)
    end, 1000, 1, player, session.id)
end)

addEvent("nativeDriveRoute:evidence", true)
addEventHandler("nativeDriveRoute:evidence", resourceRoot, function(sessionId, ped, vehicle, evidence, a, b, c, d, e)
    local player = client
    local session = sessions[player]
    if source ~= resourceRoot or not session or session.id ~= tonumber(sessionId) or session.ped ~= ped or session.vehicle ~= vehicle then
        return outputDebugString("[native drive-route] Rejected stale or unauthorized evidence", 2)
    end

    if evidence == "acceptance" then
        session.accepted = a == true
        outputChatBox(("[native drive-route] ACCEPT native=%s"):format(tostring(a)), player, a and 100 or 255, a and 230 or 80, a and 130 or 80)
        outputDebugString(("[native drive-route] ACCEPT id=%d native=%s"):format(session.id, tostring(a)), a and 3 or 2)
    elseif evidence == "index" then
        local index = tonumber(a) or -1
        session.highestIndex = math.max(session.highestIndex, index)
        outputChatBox(("[native drive-route] INDEX natif=%d elapsed=%dms"):format(index, tonumber(b) or -1), player, 120, 190, 255)
        outputDebugString(("[native drive-route] INDEX id=%d value=%d elapsed=%dms"):format(session.id, index, tonumber(b) or -1), 3)
    elseif evidence == "position" then
        outputDebugString(("[native drive-route] POS client id=%d xyz=%.3f,%.3f,%.3f nearest=%d distance=%.2f"):format(
            session.id, tonumber(a) or 0, tonumber(b) or 0, tonumber(c) or 0, tonumber(d) or -1, tonumber(e) or -1), 3)
    elseif evidence == "failure" then
        outputChatBox(("[native drive-route] ECHEC client: %s"):format(tostring(a)), player, 255, 80, 80)
        outputDebugString(("[native drive-route] FAILURE id=%d %s"):format(session.id, tostring(a)), 2)
    end
end)

addCommandHandler("nativedriveroutecleanup", function(player)
    destroySession(player, true)
    outputChatBox("[native drive-route] Harness nettoye et position restauree.", player, 180, 220, 255)
end)

addEventHandler("onPlayerQuit", root, function()
    destroySession(source, false)
end)

addEventHandler("onResourceStop", resourceRoot, function()
    for player in pairs(sessions) do
        destroySession(player, true)
    end
end)

outputDebugString("[native drive-route] Ready. Use /nativedriveroute while on foot.", 3)
