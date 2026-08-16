local activeSession
local nextSessionId = 0

local function copyPlayerState(player)
    local x, y, z = getElementPosition(player)
    local rx, ry, rz = getElementRotation(player)
    return {
        x = x, y = y, z = z, rx = rx, ry = ry, rz = rz,
        interior = getElementInterior(player),
        dimension = getElementDimension(player),
        alpha = getElementAlpha(player),
        frozen = isElementFrozen(player),
        collisions = getElementCollisionsEnabled(player),
    }
end

local function restorePlayer(player, snapshot)
    if not isElement(player) or not snapshot then
        return
    end
    setElementInterior(player, snapshot.interior)
    setElementDimension(player, snapshot.dimension)
    setElementPosition(player, snapshot.x, snapshot.y, snapshot.z)
    setElementRotation(player, snapshot.rx, snapshot.ry, snapshot.rz)
    setElementAlpha(player, snapshot.alpha)
    setElementFrozen(player, snapshot.frozen)
    setElementCollisionsEnabled(player, snapshot.collisions)
    triggerClientEvent(player, "nativeBikeRoute:stop", resourceRoot, activeSession and activeSession.id or -1)
end

local function destroySession(restore)
    local session = activeSession
    if not session then
        return
    end

    if isTimer(session.monitorTimer) then killTimer(session.monitorTimer) end
    if isTimer(session.handoffTimer) then killTimer(session.handoffTimer) end

    for player, snapshot in pairs(session.snapshots) do
        if isElement(player) then
            triggerClientEvent(player, "nativeBikeRoute:stop", resourceRoot, session.id)
            if restore then restorePlayer(player, snapshot) end
        end
    end
    if isElement(session.ped) then destroyElement(session.ped) end
    if isElement(session.vehicle) then destroyElement(session.vehicle) end
    activeSession = nil
end

local function isParticipant(session, player)
    return session and isElement(player) and session.snapshots[player] ~= nil
end

local function sendEpoch(session)
    for player in pairs(session.snapshots) do
        if isElement(player) then
            if player == session.owner then
                triggerClientEvent(player, "nativeBikeRoute:assign", resourceRoot, session.id, session.epoch, session.ped, session.vehicle, true,
                                   math.max(0, session.resumeIndex))
            else
                triggerClientEvent(player, "nativeBikeRoute:observe", resourceRoot, session.id, session.epoch, session.ped, session.vehicle)
            end
        end
    end
end

local function beginHandoff(session, newOwner)
    if activeSession ~= session or not isParticipant(session, newOwner) or newOwner == session.owner then
        return false
    end
    if session.pendingHandoff then
        return false
    end

    session.pendingHandoff = {
        from = session.owner,
        to = newOwner,
        epoch = session.epoch,
        requestedAt = getTickCount(),
    }
    triggerClientEvent(session.owner, "nativeBikeRoute:revoke", resourceRoot, session.id, session.epoch)
    session.handoffTimer = setTimer(function()
        if activeSession == session and session.pendingHandoff then
            outputDebugString("[native bike-route] handoff revoke ACK timeout", 2)
            session.pendingHandoff = nil
        end
    end, 5000, 1)
    return true
end

local function finishHandoff(session)
    local pending = session.pendingHandoff
    if not pending then return end
    if isTimer(session.handoffTimer) then killTimer(session.handoffTimer) end

    session.resumeIndex = math.max(0, session.highestIndex)
    session.epoch = session.epoch + 1
    session.owner = pending.to
    session.pendingHandoff = nil
    session.accepted = false

    setElementSyncer(session.ped, session.owner, true, true)
    setElementSyncer(session.vehicle, session.owner, true, true)
    sendEpoch(session)

    outputChatBox(("[native bike-route] Handoff epoch %d vers %s, reprise index %d."):format(session.epoch, getPlayerName(session.owner), session.resumeIndex),
                  root, 120, 200, 255)
end

addCommandHandler("nativebikeroute", function(player)
    destroySession(true)
    if isPedInVehicle(player) then
        return outputChatBox("[native bike-route] Sors de ton vehicule avant le test.", player, 255, 160, 80)
    end

    nextSessionId = nextSessionId + 1
    local dimension = 65000 + (nextSessionId % 400)
    local start = NATIVE_BIKE_ROUTE_START
    local vehicle = createVehicle(NATIVE_BIKE_ROUTE_MODEL, start[1], start[2], start[3], 0, 0, start[4])
    local ped = vehicle and createPed(NATIVE_BIKE_ROUTE_PED_MODEL, start[1], start[2], start[3] + 0.5, start[4]) or nil
    if not isElement(vehicle) or not isElement(ped) then
        if isElement(vehicle) then destroyElement(vehicle) end
        if isElement(ped) then destroyElement(ped) end
        return outputChatBox("[native bike-route] Creation BMX/rider impossible.", player, 255, 80, 80)
    end

    local session = {
        id = nextSessionId,
        epoch = 1,
        owner = player,
        ped = ped,
        vehicle = vehicle,
        dimension = dimension,
        snapshots = {},
        accepted = false,
        highestIndex = 0,
        resumeIndex = 0,
        observerSeen = {},
    }
    activeSession = session

    setElementDimension(vehicle, dimension)
    setElementDimension(ped, dimension)
    warpPedIntoVehicle(ped, vehicle, 0)
    setElementHealth(vehicle, 2000)
    setVehicleDamageProof(vehicle, true)

    local offset = 0
    for _, participant in ipairs(getElementsByType("player")) do
        session.snapshots[participant] = copyPlayerState(participant)
        setElementInterior(participant, 0)
        setElementDimension(participant, dimension)
        setElementPosition(participant, start[1] + 3.0 + offset, start[2] + 3.0, start[3] + 1.0)
        setElementFrozen(participant, true)
        setElementCollisionsEnabled(participant, false)
        setElementAlpha(participant, 0)
        offset = offset + 0.75
    end

    setElementSyncer(ped, player, true, true)
    setElementSyncer(vehicle, player, true, true)
    sendEpoch(session)

    outputChatBox("[native bike-route] INTRO1 drive_to_hub1 lance. Les clients suivent le rider en camera.", root, 100, 230, 140)
    outputChatBox("Utilise /nativebikeroutehandoff depuis l'autre client pour forcer le transfert. /nativebikeroutecleanup pour quitter.", root, 210, 210, 210)

    session.monitorTimer = setTimer(function()
        if activeSession ~= session or not isElement(session.vehicle) or not isElement(session.ped) then return end
        local x, y, z = getElementPosition(session.vehicle)
        local final = NATIVE_BIKE_ROUTE[#NATIVE_BIKE_ROUTE]
        local distance = getDistanceBetweenPoints3D(x, y, z, final[1], final[2], final[3])
        local seated = getPedOccupiedVehicle(session.ped) == session.vehicle and getPedOccupiedVehicleSeat(session.ped) == 0
        local ownerStable = getElementSyncer(session.ped) == session.owner and getElementSyncer(session.vehicle) == session.owner
        local observerRequired = 0
        local observerAccepted = 0
        for participant in pairs(session.snapshots) do
            if participant ~= session.owner and isElement(participant) then
                observerRequired = observerRequired + 1
                if session.observerSeen[participant] == session.epoch then observerAccepted = observerAccepted + 1 end
            end
        end
        if session.accepted and session.highestIndex >= #NATIVE_BIKE_ROUTE - 1 and distance <= 12.0 and seated and ownerStable and
           (observerRequired == 0 or observerAccepted > 0) and not session.passed then
            session.passed = true
            outputChatBox(("[native bike-route] PASS epoch=%d final=%.2fm observer=%d/%d"):format(session.epoch, distance, observerAccepted, observerRequired),
                          root, 80, 240, 120)
        end
    end, 500, 0)
end)

addCommandHandler("nativebikeroutehandoff", function(player)
    local session = activeSession
    if not session or not isParticipant(session, player) then
        return outputChatBox("[native bike-route] Aucun harness actif pour toi.", player, 255, 160, 80)
    end
    if player == session.owner then
        return outputChatBox("[native bike-route] Tu es deja owner. Lance cette commande sur l'autre client.", player, 255, 210, 100)
    end
    if not beginHandoff(session, player) then
        return outputChatBox("[native bike-route] Handoff deja en cours ou invalide.", player, 255, 160, 80)
    end
    outputChatBox(("[native bike-route] Revocation epoch %d demandee."):format(session.epoch), root, 120, 190, 255)
end)

addEvent("nativeBikeRoute:revoked", true)
addEventHandler("nativeBikeRoute:revoked", resourceRoot, function(sessionId, epoch)
    local session = activeSession
    if source ~= resourceRoot or not session or not session.pendingHandoff or client ~= session.pendingHandoff.from or
       session.id ~= tonumber(sessionId) or session.epoch ~= tonumber(epoch) then
        return
    end
    finishHandoff(session)
end)

addEvent("nativeBikeRoute:evidence", true)
addEventHandler("nativeBikeRoute:evidence", resourceRoot, function(sessionId, epoch, kind, ...)
    local session = activeSession
    if source ~= resourceRoot or not session or session.id ~= tonumber(sessionId) or session.epoch ~= tonumber(epoch) or not isParticipant(session, client) then
        return outputDebugString("[native bike-route] rejected stale/unauthorized evidence", 2)
    end

    local values = {...}
    if kind == "observer_sample" then
        if client ~= session.owner then
            session.observerSeen[client] = session.epoch
        end
        return
    end

    if client ~= session.owner then
        return outputDebugString("[native bike-route] rejected non-owner authoritative evidence", 2)
    end

    if kind == "acceptance" then
        session.accepted = values[1] == true
        outputDebugString(("[native bike-route] ACCEPT id=%d epoch=%d value=%s resume=%s"):format(session.id, session.epoch, tostring(values[1]), tostring(values[2])),
                          session.accepted and 3 or 2)
    elseif kind == "index" then
        local index = tonumber(values[1]) or -1
        if index >= session.highestIndex and index < #NATIVE_BIKE_ROUTE then
            session.highestIndex = index
        end
        outputDebugString(("[native bike-route] INDEX id=%d epoch=%d index=%d elapsed=%d"):format(session.id, session.epoch, index, tonumber(values[2]) or -1), 3)
    elseif kind == "sample" then
        outputDebugString(("[native bike-route] SAMPLE id=%d epoch=%d xyz=%.2f,%.2f,%.2f nearest=%s dist=%.2f move=%s anim=%s/%s pedOwner=%s bikeOwner=%s"):format(
            session.id, session.epoch, tonumber(values[1]) or 0, tonumber(values[2]) or 0, tonumber(values[3]) or 0, tostring(values[4]),
            tonumber(values[5]) or -1, tostring(values[6]), tostring(values[7]), tostring(values[8]), tostring(values[9]), tostring(values[10])), 3)
    elseif kind == "failure" then
        outputChatBox(("[native bike-route] ECHEC owner: %s"):format(tostring(values[1])), root, 255, 80, 80)
        outputDebugString(("[native bike-route] FAILURE id=%d epoch=%d %s"):format(session.id, session.epoch, tostring(values[1])), 2)
    end
end)

addCommandHandler("nativebikeroutecleanup", function()
    destroySession(true)
    outputChatBox("[native bike-route] Nettoye, snapshots joueurs restaures.", root, 180, 220, 255)
end)

addEventHandler("onPlayerQuit", root, function()
    local session = activeSession
    if not session or not session.snapshots[source] then return end
    session.snapshots[source] = nil
    session.observerSeen[source] = nil
    if session.owner == source then
        local replacement
        for participant in pairs(session.snapshots) do
            if isElement(participant) then replacement = participant break end
        end
        if replacement then
            session.owner = replacement
            session.epoch = session.epoch + 1
            session.resumeIndex = math.max(0, session.highestIndex)
            session.accepted = false
            setElementSyncer(session.ped, replacement, true, true)
            setElementSyncer(session.vehicle, replacement, true, true)
            sendEpoch(session)
        else
            destroySession(false)
        end
    end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    destroySession(true)
end)

outputDebugString("[native bike-route] Ready. /nativebikeroute", 3)
