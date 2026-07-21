local sessions = {}
local nextSessionId = 0

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

local function destroyTarget(session)
    if isElement(session.target) then
        destroyElement(session.target)
    end
    session.target = nil
end

local function destroySession(player, restore)
    local session = sessions[player]
    if not session then
        return
    end
    if isElement(player) then
        triggerClientEvent(player, "nativeDriveBy:stop", resourceRoot, session.id)
    end
    if isTimer(session.monitorTimer) then
        killTimer(session.monitorTimer)
    end
    destroyTarget(session)
    if restore then
        restorePlayer(player, session.snapshot)
    end
    if isElement(session.shooter) then
        destroyElement(session.shooter)
    end
    if isElement(session.vehicle) then
        destroyElement(session.vehicle)
    end
    sessions[player] = nil
end

local function targetPosition(phase)
    local origin = NATIVE_DRIVE_BY.origin
    local offset = NATIVE_DRIVE_BY.targetOffsets[phase]
    return origin[1] + offset[1], origin[2] + offset[2], origin[3] + offset[3]
end

local function createPhaseTarget(session, phase)
    destroyTarget(session)
    local x, y, z = targetPosition(phase)
    local target
    if phase == "vehicle" or phase == "coordinate" then
        target = createVehicle(536, x, y, z, 0, 0, 0)
        if isElement(target) then
            setElementHealth(target, 1500)
            setVehicleEngineState(target, false)
        end
    else
        target = createPed(102, x, y, z, 270)
        if isElement(target) then
            setElementHealth(target, 1000)
        end
    end
    if not isElement(target) then
        return false
    end

    setElementInterior(target, 0)
    setElementDimension(target, session.dimension)
    setElementFrozen(target, true)
    setElementSyncer(target, session.player, true, true)
    session.target = target
    session.phase = phase
    session.initialTargetHealth = getElementHealth(target)
    session.initialShooterVehicleHealth = getElementHealth(session.vehicle)
    session.serverDamageObserved = false
    session.serverShooterVehicleDamageObserved = false

    local coordinate = phase == "coordinate" and {x, y, z} or false
    triggerClientEvent(session.player, "nativeDriveBy:phase", resourceRoot, session.id, phase, session.shooter, session.vehicle, target, coordinate)
    outputDebugString(("[native drive-by] PHASE id=%d type=%s targetHealth=%.1f"):format(session.id, phase, session.initialTargetHealth), 3)
    return true
end

local function startMonitor(session)
    session.monitorTimer = setTimer(function()
        if not sessions[session.player] or sessions[session.player].id ~= session.id then
            return
        end
        if isElement(session.target) then
            local health = getElementHealth(session.target)
            if health < session.initialTargetHealth and not session.serverDamageObserved then
                session.serverDamageObserved = true
                outputChatBox(("[native drive-by] DAMAGE serveur %s: %.1f -> %.1f"):format(session.phase, session.initialTargetHealth, health), session.player,
                              100, 230, 130)
                outputDebugString(("[native drive-by] DAMAGE server id=%d phase=%s initial=%.1f current=%.1f"):format(
                    session.id, session.phase, session.initialTargetHealth, health), 3)
            end
        end
        if isElement(session.vehicle) and type(session.initialShooterVehicleHealth) == "number" then
            local health = getElementHealth(session.vehicle)
            if health < session.initialShooterVehicleHealth and not session.serverShooterVehicleDamageObserved then
                session.serverShooterVehicleDamageObserved = true
                outputChatBox(("[native drive-by] ECHEC vehicule tireur endommage: %.1f -> %.1f"):format(
                                  session.initialShooterVehicleHealth, health), session.player, 255, 80, 80)
                outputDebugString(("[native drive-by] SHOOTER VEHICLE DAMAGE server id=%d phase=%s initial=%.1f current=%.1f"):format(
                    session.id, session.phase, session.initialShooterVehicleHealth, health), 2)
            end
        end
    end, 100, 0)
end

addCommandHandler("nativedriveby", function(player)
    destroySession(player, true)
    if isPedInVehicle(player) then
        return outputChatBox("[native drive-by] Sors de ton vehicule avant de lancer le harness.", player, 255, 170, 80)
    end

    local snapshot = snapshotPlayer(player)
    local origin = NATIVE_DRIVE_BY.origin
    local dimension = 32000 + (nextSessionId % 1000)
    local vehicle = createVehicle(NATIVE_DRIVE_BY.vehicleModel, origin[1], origin[2], origin[3], 0, 0, origin[4])
    local shooter = vehicle and createPed(NATIVE_DRIVE_BY.shooterModel, origin[1], origin[2], origin[3] + 0.5, origin[4]) or nil
    if not isElement(vehicle) or not isElement(shooter) then
        if isElement(vehicle) then destroyElement(vehicle) end
        if isElement(shooter) then destroyElement(shooter) end
        return outputChatBox("[native drive-by] Creation Voodoo/Ballas impossible.", player, 255, 80, 80)
    end

    nextSessionId = nextSessionId + 1
    setElementInterior(player, 0)
    setElementInterior(vehicle, 0)
    setElementInterior(shooter, 0)
    setElementDimension(player, dimension)
    setElementDimension(vehicle, dimension)
    setElementDimension(shooter, dimension)
    warpPedIntoVehicle(player, vehicle, 0)
    warpPedIntoVehicle(shooter, vehicle, 1)
    setElementFrozen(vehicle, true)
    setElementSyncer(vehicle, player, true, true)
    setElementSyncer(shooter, player, true, true)
    giveWeapon(shooter, NATIVE_DRIVE_BY.weapon, NATIVE_DRIVE_BY.ammo, true)

    local session = {
        id = nextSessionId,
        player = player,
        shooter = shooter,
        vehicle = vehicle,
        snapshot = snapshot,
        dimension = dimension,
    }
    sessions[player] = session
    startMonitor(session)

    outputChatBox("[native drive-by] Harness pret. Tu conduis la Voodoo; Ballas2 tire depuis le siege passager.", player, 100, 220, 130)
    outputChatBox("Phases auto: cible vehicule, cible ped detruite en cours de task, puis coordonnee. /nativedrivebycleanup pour quitter.", player, 210, 210, 210)
    setTimer(function()
        if sessions[player] == session then
            createPhaseTarget(session, "vehicle")
        end
    end, 1000, 1)
end)

addEvent("nativeDriveBy:evidence", true)
addEventHandler("nativeDriveBy:evidence", resourceRoot, function(sessionId, phase, evidence, a, b, c)
    local player = client
    local session = sessions[player]
    if source ~= resourceRoot or not session or session.id ~= tonumber(sessionId) or session.phase ~= phase then
        return outputDebugString("[native drive-by] Rejected stale or unauthorized evidence", 2)
    end

    local colour = evidence == "failure" and {255, 80, 80} or {120, 200, 255}
    outputDebugString(("[native drive-by] EVIDENCE id=%d phase=%s type=%s a=%s b=%s c=%s"):format(
        session.id, phase, tostring(evidence), tostring(a), tostring(b), tostring(c)), evidence == "failure" and 2 or 3)
    if evidence == "acceptance" then
        outputChatBox(("[native drive-by] ACCEPT %s=%s"):format(phase, tostring(a)), player, a and 100 or 255, a and 230 or 80, a and 130 or 80)
    elseif evidence == "task" then
        outputChatBox(("[native drive-by] TASK active %s apres %d ms"):format(phase, tonumber(a) or -1), player, unpack(colour))
    elseif evidence == "fire" then
        outputChatBox(("[native drive-by] FIRE %s ammo %d -> %d"):format(phase, tonumber(a) or -1, tonumber(b) or -1), player, unpack(colour))
    elseif evidence == "sustained_fire" then
        outputChatBox(("[native drive-by] FIRE soutenu %s: %d tirs en %d ms"):format(
                          phase, tonumber(a) or -1, tonumber(b) or -1), player, unpack(colour))
    elseif evidence == "damage" then
        outputChatBox(("[native drive-by] DAMAGE client %s %.1f -> %.1f"):format(phase, tonumber(a) or -1, tonumber(b) or -1), player, unpack(colour))
    elseif evidence == "source_vehicle_intact" then
        outputChatBox(("[native drive-by] VEHICULE TIREUR intact %s %.1f -> %.1f"):format(
                          phase, tonumber(a) or -1, tonumber(b) or -1), player, unpack(colour))
    elseif evidence == "source_vehicle_damage" then
        outputChatBox(("[native drive-by] ECHEC vehicule tireur %s %.1f -> %.1f"):format(
                          phase, tonumber(a) or -1, tonumber(b) or -1), player, 255, 80, 80)
    elseif evidence == "cancel_queued" then
        outputChatBox(("[native drive-by] CANCEL queued %s"):format(phase), player, unpack(colour))
    elseif evidence == "cancel_call" then
        outputChatBox(("[native drive-by] CANCEL call %s taskActive=%s"):format(phase, tostring(a)), player, unpack(colour))
    elseif evidence == "cancel_return" then
        outputChatBox(("[native drive-by] CANCEL return %s killed=%s"):format(phase, tostring(a)), player, unpack(colour))
    elseif evidence == "cancelled" then
        outputChatBox(("[native drive-by] CANCEL %s taskActive=%s"):format(phase, tostring(a)), player, unpack(colour))
    elseif evidence == "target_destroyed" then
        outputChatBox(("[native drive-by] DESTROY ped safe taskActive=%s"):format(tostring(a)), player, unpack(colour))
    elseif evidence == "failure" then
        outputChatBox(("[native drive-by] ECHEC %s: %s"):format(phase, tostring(a)), player, unpack(colour))
    end
end)

addEvent("nativeDriveBy:advance", true)
addEventHandler("nativeDriveBy:advance", resourceRoot, function(sessionId, phase)
    local player = client
    local session = sessions[player]
    if source ~= resourceRoot or not session or session.id ~= tonumber(sessionId) or session.phase ~= phase then
        return
    end

    if phase == "vehicle" then
        createPhaseTarget(session, "ped")
    elseif phase == "ped" then
        createPhaseTarget(session, "coordinate")
    elseif phase == "coordinate" then
        outputChatBox("[native drive-by] Trois formes terminees. Verifie ACCEPT/TASK/FIRE/DAMAGE/CANCEL/DESTROY dans les logs.", player, 100, 230, 130)
        outputDebugString(("[native drive-by] COMPLETE id=%d all target forms exercised"):format(session.id), 3)
    end
end)

addEvent("nativeDriveBy:destroyTarget", true)
addEventHandler("nativeDriveBy:destroyTarget", resourceRoot, function(sessionId, phase)
    local player = client
    local session = sessions[player]
    if source ~= resourceRoot or not session or session.id ~= tonumber(sessionId) or session.phase ~= phase or phase ~= "ped" or not isElement(session.target) then
        return
    end
    destroyTarget(session)
    outputDebugString(("[native drive-by] TARGET destroyed server-side id=%d phase=ped while native task was active"):format(session.id), 3)
    triggerClientEvent(player, "nativeDriveBy:targetDestroyed", resourceRoot, session.id, phase)
end)

addCommandHandler("nativedrivebycleanup", function(player)
    destroySession(player, true)
    outputChatBox("[native drive-by] Harness nettoye et position restauree.", player, 180, 220, 255)
end)

addEventHandler("onPlayerQuit", root, function()
    destroySession(source, false)
end)

addEventHandler("onResourceStop", resourceRoot, function()
    for player in pairs(sessions) do
        destroySession(player, true)
    end
end)

outputDebugString("[native drive-by] Ready. Use /nativedriveby while on foot.", 3)
