local mission = {
    running = false,
    finishing = false,
    stage = nil,
    leader = nil,
    party = {},
    snapshots = {},
    entities = {},
    tagProgress = {},
    completedTags = {},
    sprayCooldown = {},
    timers = {},
    demoLeaveSerial = 0,
    demoLeave = nil,
    demoWalkSerial = 0,
    demoWalk = nil,
    demoShootSerial = 0,
    demoShoot = nil,
    demoEnterSerial = 0,
    demoEnter = nil,
    ballasDepartureSerial = 0,
    ballasDeparture = nil,
    vehiclePlaybackSerial = 0,
    vehiclePlayback = nil,
}

-- The server owns every stage transition and spray increment so several clients can
-- cooperate without letting the fastest (or a modified) client decide mission state.

local function isMissionPlayer(player)
    if not isElement(player) then
        return false
    end
    for _, member in ipairs(mission.party) do
        if member == player then
            return true
        end
    end
    return false
end

local function rememberTimer(timer)
    table.insert(mission.timers, timer)
    return timer
end

local function clearMissionTimers()
    for _, timer in ipairs(mission.timers) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    mission.timers = {}
end

local function snapshotPlayer(player)
    local x, y, z = getElementPosition(player)
    local _, _, rotation = getElementRotation(player)
    local weapons = {}
    for slot = 0, 12 do
        local weapon = getPedWeapon(player, slot)
        local ammo = getPedTotalAmmo(player, slot)
        if weapon and weapon ~= 0 and ammo > 0 then
            table.insert(weapons, {weapon = weapon, ammo = ammo})
        end
    end
    return {
        x = x,
        y = y,
        z = z,
        rotation = rotation,
        interior = getElementInterior(player),
        dimension = getElementDimension(player),
        health = getElementHealth(player),
        armor = getPedArmor(player),
        model = getElementModel(player),
        weapons = weapons,
    }
end

local function restorePlayer(player, snapshot)
    if not isElement(player) or not snapshot then
        return
    end

    if isPedDead(player) then
        spawnPlayer(player, snapshot.x, snapshot.y, snapshot.z, snapshot.rotation, snapshot.model, snapshot.interior, snapshot.dimension)
    else
        removePedFromVehicle(player)
        setElementInterior(player, snapshot.interior)
        setElementDimension(player, snapshot.dimension)
        setElementPosition(player, snapshot.x, snapshot.y, snapshot.z)
        setElementRotation(player, 0, 0, snapshot.rotation)
        setElementModel(player, snapshot.model)
    end

    setElementFrozen(player, false)
    setElementHealth(player, math.max(1, snapshot.health))
    setPedArmor(player, snapshot.armor)
    takeAllWeapons(player)
    for _, weapon in ipairs(snapshot.weapons) do
        giveWeapon(player, weapon.weapon, weapon.ammo, false)
    end
end

local function destroyMissionEntities()
    for _, entity in pairs(mission.entities) do
        if isElement(entity) then
            destroyElement(entity)
        end
    end
    mission.entities = {}
end

local function warpSweetIntoFirstFreeSeat()
    local sweet, vehicle = mission.entities.sweet, mission.entities.vehicle
    if not isElement(sweet) or not isElement(vehicle) then
        return false
    end

    if getPedOccupiedVehicle(sweet) == vehicle then
        return true
    end

    removePedFromVehicle(sweet)
    setElementDimension(sweet, TAGUP.dimension)
    local x, y, z = getElementPosition(vehicle)
    setElementPosition(sweet, x, y, z + 1)
    if isElement(mission.leader) then
        setElementSyncer(sweet, mission.leader)
    end

    for seat = 1, getVehicleMaxPassengers(vehicle) do
        if not getVehicleOccupant(vehicle, seat) then
            warpPedIntoVehicle(sweet, vehicle, seat)
            if getPedOccupiedVehicle(sweet) == vehicle then
                outputDebugString("[tagging-up-turf] Sweet seated in passenger seat " .. seat)
                return true
            end
        end
    end
    outputDebugString("[tagging-up-turf] Unable to seat Sweet: no usable passenger seat", 1)
    return false
end

local function stagePayload(extra)
    local payload = {
        stage = mission.stage,
        vehicle = mission.entities.vehicle,
        sweet = mission.entities.sweet,
        leader = mission.leader,
        tagProgress = mission.tagProgress,
        completedTags = mission.completedTags,
    }
    if extra then
        for key, value in pairs(extra) do
            payload[key] = value
        end
    end
    return payload
end

local function broadcastState(extra)
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:state", resourceRoot, stagePayload(extra))
        end
    end
end

local function setStage(stage, extra)
    mission.stage = stage
    outputDebugString("[tagging-up-turf] Stage: " .. stage)
    broadcastState(extra)
end

local function cancelDemoLeave(reason)
    local leave = mission.demoLeave
    if not leave then
        return
    end

    mission.demoLeave = nil
    if isTimer(leave.guardTimer) then
        killTimer(leave.guardTimer)
    end
    if isElement(mission.leader) then
        triggerClientEvent(mission.leader, "tagup:sweetDemoLeaveCancel", resourceRoot, leave.id, reason or "server_cancelled")
    end
end

local function cancelDemoWalk(reason)
    local walk = mission.demoWalk
    if not walk then
        return
    end

    mission.demoWalk = nil
    if isTimer(walk.guardTimer) then
        killTimer(walk.guardTimer)
    end
    if isElement(mission.leader) then
        triggerClientEvent(mission.leader, "tagup:sweetDemoWalkCancel", resourceRoot, walk.id, reason or "server_cancelled")
    end
end

local function cancelDemoShoot(reason)
    local shoot = mission.demoShoot
    if not shoot then
        return
    end

    mission.demoShoot = nil
    if isTimer(shoot.guardTimer) then
        killTimer(shoot.guardTimer)
    end
    if isTimer(shoot.progressTimer) then
        killTimer(shoot.progressTimer)
    end
    if isTimer(shoot.completionTimer) then
        killTimer(shoot.completionTimer)
    end
    if isElement(mission.leader) then
        triggerClientEvent(mission.leader, "tagup:sweetDemoShootCancel", resourceRoot, shoot.id, reason or "server_cancelled")
    end
end

local function cancelDemoEnter(reason)
    local enter = mission.demoEnter
    if not enter then
        return
    end

    mission.demoEnter = nil
    if isTimer(enter.guardTimer) then
        killTimer(enter.guardTimer)
    end
    if isElement(mission.leader) then
        triggerClientEvent(mission.leader, "tagup:sweetReturnEnterCancel", resourceRoot, enter.id, reason or "server_cancelled")
    end
end

local function cancelBallasDeparture(reason)
    local departure = mission.ballasDeparture
    if not departure then
        return
    end

    mission.ballasDeparture = nil
    if isTimer(departure.guardTimer) then
        killTimer(departure.guardTimer)
    end
    if isTimer(departure.postStartTimer) then
        killTimer(departure.postStartTimer)
    end
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasDepartureCancel", resourceRoot, departure.id, reason or "server_cancelled")
        end
    end
end

local function cancelVehiclePlayback(reason)
    local playback = mission.vehiclePlayback
    if not playback then
        return
    end

    mission.vehiclePlayback = nil
    if isTimer(playback.guardTimer) then
        killTimer(playback.guardTimer)
    end
    if isTimer(playback.startTimer) then
        killTimer(playback.startTimer)
    end
    if isTimer(playback.completionTimer) then
        killTimer(playback.completionTimer)
    end
    if isElement(mission.leader) then
        triggerClientEvent(mission.leader, "tagup:vehiclePlaybackCancel", resourceRoot, playback.id, reason or "server_cancelled")
    end
end

local function isPartyInVehicle()
    local vehicle = mission.entities.vehicle
    if not isElement(vehicle) then
        return false
    end
    for _, player in ipairs(mission.party) do
        if isElement(player) and getPedOccupiedVehicle(player) ~= vehicle then
            return false
        end
    end
    return true
end

local function createTagObject(tag)
    local object = createObject(TAGUP.tagModel, tag.x, tag.y, tag.z, 0, 0, tag.rotation)
    if object then
        setElementDimension(object, TAGUP.dimension)
        setElementInterior(object, 0)
        setElementCollisionsEnabled(object, false)
        setElementData(object, "tagup.tagId", tag.id, false)
        -- GTA stores the rival and Grove artwork in two materials of this same
        -- model. The synchronized byte drives only the Grove material client-side.
        setElementData(object, "tagup.paintAlpha", 0, true)
        mission.entities["tag" .. tag.id] = object
    end
    return object
end

local function updateTagVisual(tagId, progress)
    local tag = mission.entities["tag" .. tagId]
    if isElement(tag) then
        setElementData(tag, "tagup.paintAlpha", math.floor(255 * progress + 0.5), true)
    end
end

local function replaceTagObject(tagId)
    local tag = mission.entities["tag" .. tagId]
    if isElement(tag) then
        setElementData(tag, "tagup.paintAlpha", 255, true)
    end
end

local function spawnBallas()
    local positions = {
        {2401.0, -1471.0, 24.2, 230, 102, 22},
        {2398.0, -1465.0, 24.2, 210, 103, 5},
    }
    local enemies = {}
    for index, data in ipairs(positions) do
        local ped = createPed(data[5], data[1], data[2], data[3], data[4])
        if ped then
            setElementDimension(ped, TAGUP.dimension)
            giveWeapon(ped, data[6], data[6] == 22 and 500 or 1, true)
            setElementData(ped, "tagup.enemy", true, true)
            setElementData(ped, "tagup.active", true, true)
            setPedStat(ped, 76, 700)
            if isElement(mission.leader) then
                setElementSyncer(ped, mission.leader)
            end
            mission.entities["enemy" .. index] = ped
            table.insert(enemies, ped)
        end
    end
    broadcastState({enemies = enemies, message = "Ballas: Get that fool!"})
end

local function activeTagIds()
    if mission.stage == "tags_idlewood" then
        return {1, 2}
    elseif mission.stage == "tags_ballas" then
        return {3, 4}
    elseif mission.stage == "rooftop" then
        return {5}
    end
    return {}
end

local function currentGroupComplete()
    for _, tagId in ipairs(activeTagIds()) do
        if not mission.completedTags[tagId] then
            return false
        end
    end
    return true
end

local failMission

local function startVehiclePlaybackReturn(extra)
    local sweet, vehicle = mission.entities.sweet, mission.entities.vehicle
    if mission.vehiclePlayback or not isElement(mission.leader) or not isElement(sweet) or not isElement(vehicle) then
        return failMission("Sweet ou la Greenwood est indisponible pour le recording 207.")
    end

    mission.vehiclePlaybackSerial = mission.vehiclePlaybackSerial + 1
    local playback = {
        id = mission.vehiclePlaybackSerial,
        ped = sweet,
        vehicle = vehicle,
        extra = extra,
        requestedAt = getTickCount(),
    }
    mission.vehiclePlayback = playback
    setElementSyncer(sweet, mission.leader, true, true)
    setElementSyncer(vehicle, mission.leader, true, true)

    playback.guardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.vehiclePlayback
        if not mission.running or mission.stage ~= "rooftop" or not active or active.id ~= expectedId then
            return
        end
        cancelVehiclePlayback("server_timeout")
        failMission("Le recording 207 a depasse son delai de garde.")
    end, TAGUP.vehicleRecording207.guardTimeout, 1, playback.id))

    outputDebugString(("[tagging-up-turf] Requesting recorded-car return #%d (recording=%d) from %s"):format(
                          playback.id, TAGUP.vehicleRecording207.id, getPlayerName(mission.leader)))
    triggerClientEvent(mission.leader, "tagup:vehiclePlaybackPrepare", resourceRoot, playback.id, sweet, vehicle, TAGUP.vehicleRecording207)
end

local function advanceAfterTags(extra)
    if mission.stage == "tags_idlewood" then
        setStage("return_car", extra)
    elseif mission.stage == "tags_ballas" then
        setStage("rooftop", extra)
    elseif mission.stage == "rooftop" then
        startVehiclePlaybackReturn(extra)
    end
end

local finishMission

failMission = function(reason)
    if not mission.running or mission.finishing or mission.stage == "failed" then
        return
    end
    mission.stage = "failed"
    broadcastState({failureReason = reason or "La mission a echoue."})
    outputDebugString("[tagging-up-turf] Failed: " .. tostring(reason))
    rememberTimer(setTimer(function()
        finishMission(false)
    end, 3500, 1))
end

addEvent("tagup:vehiclePlaybackResult", true)
addEventHandler("tagup:vehiclePlaybackResult", resourceRoot, function(playbackId, ped, vehicle, result, details, elapsed)
    local player = client
    local playback = mission.vehiclePlayback
    if source ~= resourceRoot or not mission.running or mission.stage ~= "rooftop" or player ~= mission.leader or not playback or
        playback.id ~= tonumber(playbackId) or playback.ped ~= ped or playback.vehicle ~= vehicle or ped ~= mission.entities.sweet or
        vehicle ~= mission.entities.vehicle then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized vehicle-playback result", 2)
        return
    end

    outputDebugString(("[tagging-up-turf] Recording 207 #%d result=%s elapsed=%s (%s)"):format(
                          playback.id, tostring(result), tostring(elapsed or "-"), tostring(details or ""):sub(1, 180)))

    if result == "ready" then
        if playback.ready or getElementSyncer(ped) ~= player or getElementSyncer(vehicle) ~= player then
            cancelVehiclePlayback("invalid_ready")
            return failMission("Le recording 207 n'a pas ete prepare par le double syncer attendu.")
        end

        playback.ready = true
        -- SWEET1 clears Sweet's task and warps him into the driver seat before
        -- 05EB. The vehicle itself is not placed here: recording frame zero
        -- performs the native repositioning that replaces the old Lua teleport.
        removePedFromVehicle(ped)
        warpPedIntoVehicle(ped, vehicle, 0)
        setElementSyncer(ped, player, true, true)
        setElementSyncer(vehicle, player, true, true)
        if getVehicleOccupant(vehicle, 0) ~= ped then
            cancelVehiclePlayback("driver_warp_failed")
            return failMission("Sweet n'a pas pu prendre le volant avant le recording 207.")
        end

        playback.startTimer = rememberTimer(setTimer(function(expectedId)
            local active = mission.vehiclePlayback
            if not active or active.id ~= expectedId or not isElement(mission.leader) then
                return
            end
            triggerClientEvent(mission.leader, "tagup:vehiclePlaybackStart", resourceRoot, active.id, active.ped, active.vehicle,
                               TAGUP.vehicleRecording207)
        end, 200, 1, playback.id))
        return
    end

    if result == "started" then
        if not playback.ready or playback.started then
            cancelVehiclePlayback("invalid_start")
            return failMission("Le demarrage du recording 207 est incoherent.")
        end
        playback.started = true
        playback.startedAt = getTickCount()
        return
    end

    if result == "completed" then
        if not playback.started or playback.completing then
            cancelVehiclePlayback("invalid_completion")
            return failMission("La fin du recording 207 est incoherente.")
        end
        playback.completing = true
        local elapsedMs = tonumber(elapsed) or 0
        playback.completionTimer = rememberTimer(setTimer(function(expectedId, reportedElapsed)
            local active = mission.vehiclePlayback
            if not active or active.id ~= expectedId or not isElement(active.vehicle) or not isElement(active.ped) then
                return
            end
            local x, y, z = getElementPosition(active.vehicle)
            local endpoint = TAGUP.vehicleRecording207.endPosition
            local distance = getDistanceBetweenPoints3D(x, y, z, endpoint[1], endpoint[2], endpoint[3])
            local valid = reportedElapsed >= TAGUP.vehicleRecording207.minimumElapsed and reportedElapsed <= TAGUP.vehicleRecording207.maximumElapsed and
                              distance <= TAGUP.vehicleRecording207.serverEndRadius and getElementSyncer(active.vehicle) == mission.leader
            if not valid then
                cancelVehiclePlayback("invalid_completion")
                return failMission(("Le recording 207 a fini hors profil (%.2f m, %d ms)."):format(distance, reportedElapsed))
            end

            local extra = active.extra
            removePedFromVehicle(active.ped)
            if not warpSweetIntoFirstFreeSeat() then
                cancelVehiclePlayback("passenger_warp_failed")
                return failMission("Sweet n'a pas pu reprendre sa place passager apres le recording 207.")
            end
            outputDebugString(("[tagging-up-turf] Recording 207 #%d completed at %.2f m after %d ms; Sweet restored as passenger"):format(
                                  active.id, distance, reportedElapsed))
            cancelVehiclePlayback("completed")
            setStage("return_after_roof", extra)
        end, 750, 1, playback.id, elapsedMs))
        return
    end

    cancelVehiclePlayback(tostring(result))
    failMission("Le recording 207 a echoue: " .. tostring(result) .. " (" .. tostring(details or "") .. ")")
end)

finishMission = function(passed, traceExtra)
    if not mission.running or mission.finishing then
        return
    end

    mission.finishing = true
    cancelDemoLeave("mission_finished")
    cancelDemoWalk("mission_finished")
    cancelDemoShoot("mission_finished")
    cancelDemoEnter("mission_finished")
    cancelBallasDeparture("mission_finished")
    cancelVehiclePlayback("mission_finished")
    clearMissionTimers()
    if passed then
        mission.stage = "complete"
        -- Failure paths already log their terminal state; keep successful runs
        -- equally visible so a complete manual mission can be audited afterward.
        outputDebugString(("[tagging-up-turf] Mission passed: rewarding %d participant(s)."):format(#mission.party))
        broadcastState(traceExtra)
        for _, player in ipairs(mission.party) do
            if isElement(player) then
                givePlayerMoney(player, 500)
            end
        end
    end

    local party = mission.party
    local snapshots = mission.snapshots
    destroyMissionEntities()
    local delay = passed and 6000 or 250
    setTimer(function()
        for _, player in ipairs(party) do
            if isElement(player) then
                restorePlayer(player, snapshots[player])
                triggerClientEvent(player, "tagup:stop", resourceRoot, passed)
            end
        end
        mission.running = false
        mission.finishing = false
        mission.stage = nil
        mission.leader = nil
        mission.party = {}
        mission.snapshots = {}
        mission.tagProgress = {}
        mission.completedTags = {}
        mission.sprayCooldown = {}
        mission.demoLeave = nil
        mission.demoWalk = nil
        mission.demoShoot = nil
        mission.demoEnter = nil
        mission.ballasDeparture = nil
        mission.vehiclePlayback = nil
    end, delay, 1)
end

local function setupMissionPlayers()
    local offsets = {
        {2514.0, -1666.6, 13.4, 90},
        {2514.0, -1668.0, 13.4, 90},
        {2514.0, -1669.4, 13.4, 90},
    }
    for index, player in ipairs(mission.party) do
        mission.snapshots[player] = snapshotPlayer(player)
        removePedFromVehicle(player)
        setElementInterior(player, 0)
        setElementDimension(player, TAGUP.dimension)
        setElementPosition(player, offsets[index][1], offsets[index][2], offsets[index][3])
        setElementRotation(player, 0, 0, offsets[index][4])
        setElementHealth(player, 100)
        setPedArmor(player, 0)
        takeAllWeapons(player)
        giveWeapon(player, TAGUP.sprayWeapon, 1000, true)
        setElementFrozen(player, true)
    end
end

local function startMission(requester)
    if mission.running then
        outputChatBox("Tagging Up Turf est deja en cours.", requester, 255, 190, 80)
        return
    end

    mission.running = true
    mission.leader = requester
    mission.party = {requester}
    for _, player in ipairs(getElementsByType("player")) do
        if player ~= requester and #mission.party < TAGUP.maximumPlayers then
            table.insert(mission.party, player)
        end
    end

    setupMissionPlayers()

    local vehicle = createVehicle(TAGUP.vehicleModel, TAGUP.start[1], TAGUP.start[2], TAGUP.start[3], 0, 0, TAGUP.start[4])
    setElementDimension(vehicle, TAGUP.dimension)
    setVehicleColor(vehicle, 25, 86, 39, 25, 86, 39)
    setVehicleEngineState(vehicle, true)
    mission.entities.vehicle = vehicle

    local sweet = createPed(TAGUP.sweetModel, unpack(TAGUP.sweetStart))
    setElementDimension(sweet, TAGUP.dimension)
    setElementData(sweet, "tagup.sweet", true, true)
    -- GTA's CREATE_CHAR marks story actors as PED_MISSION. Replicate the
    -- policy so every client applies it before becoming Sweet's syncer.
    setElementData(sweet, TAGUP.missionActorData, true, true)
    mission.entities.sweet = sweet
    setElementSyncer(sweet, requester)

    for _, tag in ipairs(TAGUP.tags) do
        mission.tagProgress[tag.id] = 0
        createTagObject(tag)
    end

    local demo = TAGUP.demoTag
    local demoObject = createObject(TAGUP.tagModel, demo.x, demo.y, demo.z, 0, 0, demo.rotation)
    setElementDimension(demoObject, TAGUP.dimension)
    setElementCollisionsEnabled(demoObject, false)
    setElementData(demoObject, "tagup.paintAlpha", 0, true)
    mission.entities.demoTag = demoObject

    setStage("intro")
    rememberTimer(setTimer(function()
        if not mission.running or mission.stage ~= "intro" then
            return
        end
        for _, player in ipairs(mission.party) do
            if isElement(player) then
                setElementFrozen(player, false)
            end
        end
        if warpSweetIntoFirstFreeSeat() then
            setStage("enter_car", {message = "Sweet vous attend dans la Greenwood."})
        else
            failMission("Sweet n'a pas pu monter dans la Greenwood.")
        end
    end, 7000, 1))
end

addCommandHandler("tagup", function(player)
    if not player then
        return
    end
    startMission(player)
end)

addCommandHandler("tagupabort", function(player)
    if mission.running and isMissionPlayer(player) then
        failMission("Mission abandonnee.")
    end
end)

addCommandHandler("tagupskip", function(player)
    if not mission.running or player ~= mission.leader then
        return
    end
    if mission.stage == "intro" then
        for _, member in ipairs(mission.party) do
            setElementFrozen(member, false)
        end
        warpSweetIntoFirstFreeSeat()
        setStage("enter_car", {traceSkipped = true})
    elseif mission.stage == "enter_car" then
        if warpSweetIntoFirstFreeSeat() then
            setStage("drive_idlewood", {traceSkipped = true})
        end
    elseif mission.stage == "drive_idlewood" then
        setStage("demo", {traceSkipped = true})
        triggerEvent("tagup:beginDemo", resourceRoot)
    elseif mission.stage == "demo" then
        cancelDemoLeave("stage_skipped")
        cancelDemoWalk("stage_skipped")
        cancelDemoShoot("stage_skipped")
        cancelDemoEnter("stage_skipped")
        if isElement(mission.entities.sweet) then
            removePedFromVehicle(mission.entities.sweet)
        end
        -- Debug skips do not execute the native task trace. Seat Sweet immediately
        -- so later skip stages remain usable without weakening the real path.
        warpSweetIntoFirstFreeSeat()
        setStage("tags_idlewood", {traceSkipped = true})
    elseif mission.stage == "tags_idlewood" or mission.stage == "tags_ballas" or mission.stage == "rooftop" then
        for _, id in ipairs(activeTagIds()) do
            mission.completedTags[id] = true
            mission.tagProgress[id] = 1
            replaceTagObject(id)
        end
        advanceAfterTags({traceSkipped = true})
    elseif mission.stage == "return_car" then
        setStage("drive_ballas", {traceSkipped = true})
    elseif mission.stage == "drive_ballas" then
        setStage("tags_ballas", {traceSkipped = true})
        spawnBallas()
    elseif mission.stage == "ballas_departure" then
        cancelBallasDeparture("stage_skipped")
        for _, member in ipairs(mission.party) do
            removePedFromVehicle(member)
        end
        setStage("tags_ballas", {traceSkipped = true})
        spawnBallas()
    elseif mission.stage == "return_after_roof" then
        setStage("drive_home", {traceSkipped = true})
    elseif mission.stage == "drive_home" then
        finishMission(true, {traceSkipped = true})
    end
end)

local function startDemoWalk(sweet)
    local profile = TAGUP.sweetDemoWalk
    local exitX, exitY, exitZ = getElementPosition(sweet)
    local _, _, exitHeading = getElementRotation(sweet)
    local deltaX, deltaY = profile.target.x - exitX, profile.target.y - exitY
    local distance2D = math.sqrt(deltaX * deltaX + deltaY * deltaY)
    local distance3D = tagupDistance3D(exitX, exitY, exitZ, profile.target.x, profile.target.y, profile.target.z)

    -- The native leave-car task owns Sweet's final position and heading. Keeping
    -- them avoids a visible snap and mirrors the SCM sequence, which assigns the
    -- following go-to task from the actor's natural vehicle-exit position.
    setElementSyncer(sweet, mission.leader)

    mission.demoWalkSerial = mission.demoWalkSerial + 1
    local walk = {id = mission.demoWalkSerial, ped = sweet}
    mission.demoWalk = walk
    walk.guardTimer = rememberTimer(setTimer(function(expectedId)
        if not mission.running or mission.stage ~= "demo" or not mission.demoWalk or mission.demoWalk.id ~= expectedId then
            return
        end
        outputDebugString(("[tagging-up-turf] Sweet native go-to #%d exceeded the %d ms server guard"):format(expectedId,
                                                                                                             profile.guardTimeout), 1)
        cancelDemoWalk("server_timeout")
        failMission("La marche native de Sweet a depasse le delai de garde.")
    end, profile.guardTimeout, 1, walk.id))

    local diagnostic =
        ("[tagging-up-turf] Starting Sweet native go-to #%d from natural exit=(%.2f, %.2f, %.2f, heading=%.1f) to target=(%.2f, %.2f, %.2f), distance2D=%.2f m, distance3D=%.2f m, syncer=%s")
            :format(walk.id, exitX, exitY, exitZ, exitHeading, profile.target.x, profile.target.y, profile.target.z, distance2D, distance3D,
                    getPlayerName(mission.leader))
    outputDebugString(diagnostic)
    triggerClientEvent(mission.leader, "tagup:sweetDemoWalkStart", resourceRoot, walk.id, sweet, profile)
end

local function tryCompleteDemoLeave()
    local leave = mission.demoLeave
    if not leave or not isElement(leave.ped) or not leave.clientObserved or not leave.serverExited or getPedOccupiedVehicle(leave.ped) then
        return
    end

    local ped, leaveId = leave.ped, leave.id
    outputDebugString(("[tagging-up-turf] Sweet native leave-car #%d confirmed by task observation and server vehicle state"):format(leaveId))
    cancelDemoLeave("completed")
    startDemoWalk(ped)
end

addEvent("tagup:beginDemo", false)
addEventHandler("tagup:beginDemo", resourceRoot, function()
    if not mission.running or mission.stage ~= "demo" then
        return
    end
    local sweet, vehicle = mission.entities.sweet, mission.entities.vehicle
    if not isElement(sweet) or not isElement(vehicle) or not isElement(mission.leader) then
        return failMission("Sweet, la Greenwood ou le leader n'est plus disponible pour la demonstration.")
    end
    if getPedOccupiedVehicle(sweet) ~= vehicle then
        return failMission("Sweet n'est plus dans la Greenwood avant sa sortie native.")
    end

    cancelDemoLeave("replaced")
    cancelDemoWalk("replaced")
    cancelDemoShoot("replaced")
    setElementSyncer(sweet, mission.leader)

    mission.demoLeaveSerial = mission.demoLeaveSerial + 1
    local leave = {
        id = mission.demoLeaveSerial,
        ped = sweet,
        vehicle = vehicle,
        clientObserved = false,
        serverExited = false,
    }
    mission.demoLeave = leave
    leave.guardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.demoLeave
        if not mission.running or mission.stage ~= "demo" or not active or active.id ~= expectedId then
            return
        end
        outputDebugString(("[tagging-up-turf] Sweet native leave-car #%d exceeded the %d ms server guard"):format(
                              expectedId, TAGUP.sweetDemoLeave.guardTimeout), 1)
        cancelDemoLeave("server_timeout")
        failMission("La sortie native de Sweet a depasse le delai de garde.")
    end, TAGUP.sweetDemoLeave.guardTimeout, 1, leave.id))

    outputDebugString(("[tagging-up-turf] Requesting Sweet native leave-car #%d from syncer %s"):format(leave.id, getPlayerName(mission.leader)))
    triggerClientEvent(mission.leader, "tagup:sweetDemoLeaveStart", resourceRoot, leave.id, sweet, vehicle, TAGUP.sweetDemoLeave)
end)

addEvent("tagup:sweetDemoLeaveResult", true)
addEventHandler("tagup:sweetDemoLeaveResult", resourceRoot, function(leaveId, ped, vehicle, result, details)
    local player = client
    local leave = mission.demoLeave
    if source ~= resourceRoot or not mission.running or mission.stage ~= "demo" or player ~= mission.leader or not isMissionPlayer(player) or
        not leave or leave.id ~= tonumber(leaveId) or leave.ped ~= ped or leave.vehicle ~= vehicle or ped ~= mission.entities.sweet or
        vehicle ~= mission.entities.vehicle or not isElement(ped) or not isElement(vehicle) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Sweet leave-car result", 2)
        return
    end

    details = tostring(details or "")
    outputDebugString(("[tagging-up-turf] Sweet native leave-car #%d result=%s (%s)"):format(leave.id, tostring(result), details:sub(1, 240)))
    if result ~= "exited" then
        cancelDemoLeave("client_" .. tostring(result))
        return failMission("La sortie native de Sweet a echoue: " .. tostring(result))
    end
    if getElementSyncer(ped) ~= player then
        cancelDemoLeave("invalid_syncer")
        return failMission("Le resultat de sortie de Sweet ne vient plus de son syncer.")
    end

    leave.clientObserved = true
    if getPedOccupiedVehicle(ped) ~= vehicle then
        leave.serverExited = true
    end
    tryCompleteDemoLeave()
end)

local function startDemoShoot(ped, distanceFromWalkTarget)
    local profile = TAGUP.sweetDemoShoot
    local demo = TAGUP.demoTag
    giveWeapon(ped, TAGUP.sprayWeapon, 500, true)
    local x, y = getElementPosition(ped)
    setElementRotation(ped, 0, 0, -math.deg(math.atan2(demo.x - x, demo.y - y)))
    setElementSyncer(ped, mission.leader)

    mission.demoShootSerial = mission.demoShootSerial + 1
    local shoot = {id = mission.demoShootSerial, ped = ped, requestedAt = getTickCount()}
    mission.demoShoot = shoot
    shoot.guardTimer = rememberTimer(setTimer(function(expectedId)
        if not mission.running or mission.stage ~= "demo" or not mission.demoShoot or mission.demoShoot.id ~= expectedId then
            return
        end
        outputDebugString(("[tagging-up-turf] Sweet native shoot #%d exceeded the %d ms server guard"):format(expectedId,
                                                                                                            profile.guardTimeout), 1)
        cancelDemoShoot("server_timeout")
        failMission("Le tir natif de Sweet a depasse le delai de garde.")
    end, profile.guardTimeout, 1, shoot.id))

    outputDebugString(("[tagging-up-turf] Sweet go-to accepted at %.2f m; starting native shoot #%d (duration=%d, burst=%d)"):format(
        distanceFromWalkTarget, shoot.id, profile.duration, profile.burstLength))
    triggerClientEvent(mission.leader, "tagup:sweetDemoShootStart", resourceRoot, shoot.id, ped, demo, profile)
end

local function tryCompleteSweetReturnEnter()
    local enter = mission.demoEnter
    if not enter or not enter.clientObserved or not enter.serverEntered or not isElement(enter.ped) or not isElement(enter.vehicle) or
        getPedOccupiedVehicle(enter.ped) ~= enter.vehicle or getPedOccupiedVehicleSeat(enter.ped) ~= enter.seat then
        return
    end

    outputDebugString(("[tagging-up-turf] Sweet native passenger entry #%d confirmed by task observation and server vehicle state (seat=%d)"):format(
                          enter.id, enter.seat))
    cancelDemoEnter("completed")
    broadcastState({message = "Sweet est remonte dans la Greenwood."})
end

local function startSweetReturnEnter(ped)
    local vehicle = mission.entities.vehicle
    local profile = TAGUP.sweetReturnEnter
    if not isElement(ped) or not isElement(vehicle) or not isElement(mission.leader) then
        return failMission("Sweet, la Greenwood ou le leader a disparu avant l'entree passager native.")
    end
    if getPedOccupiedVehicle(ped) then
        return failMission("Sweet occupe deja un vehicule avant l'entree passager native.")
    end
    if getVehicleOccupant(vehicle, profile.seat) then
        return failMission("Le siege passager de Sweet est deja occupe.")
    end

    cancelDemoEnter("replaced")
    setElementSyncer(ped, mission.leader)
    mission.demoEnterSerial = mission.demoEnterSerial + 1
    local enter = {
        id = mission.demoEnterSerial,
        ped = ped,
        vehicle = vehicle,
        seat = profile.seat,
        requestedAt = getTickCount(),
        clientObserved = false,
        serverEntered = false,
    }
    mission.demoEnter = enter
    enter.guardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.demoEnter
        if not mission.running or not active or active.id ~= expectedId or
            (mission.stage ~= "tags_idlewood" and mission.stage ~= "return_car") then
            return
        end
        outputDebugString(("[tagging-up-turf] Sweet native passenger entry #%d exceeded the %d ms server guard"):format(
                              expectedId, profile.guardTimeout), 1)
        cancelDemoEnter("server_timeout")
        failMission("L'entree passager native de Sweet a depasse le delai de garde.")
    end, profile.guardTimeout, 1, enter.id))

    outputDebugString(("[tagging-up-turf] Requesting Sweet native passenger entry #%d (SCM seat=0, MTA seat=%d, SCM timeout=%d ms) from syncer %s"):format(
                          enter.id, enter.seat, profile.scmTimeout, getPlayerName(mission.leader)))
    triggerClientEvent(mission.leader, "tagup:sweetReturnEnterStart", resourceRoot, enter.id, ped, vehicle, profile)
end

addEvent("tagup:sweetDemoWalkResult", true)
addEventHandler("tagup:sweetDemoWalkResult", resourceRoot, function(walkId, ped, result, details)
    local player = client
    local walk = mission.demoWalk
    if source ~= resourceRoot or not mission.running or mission.stage ~= "demo" or player ~= mission.leader or not isMissionPlayer(player) or
        not walk or walk.id ~= tonumber(walkId) or walk.ped ~= ped or ped ~= mission.entities.sweet or not isElement(ped) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Sweet go-to result", 2)
        return
    end

    details = tostring(details or "")
    outputDebugString(("[tagging-up-turf] Sweet native go-to #%d result=%s (%s)"):format(walk.id, tostring(result), details:sub(1, 240)))
    if result ~= "arrived" and result ~= "timeout_relocated" then
        cancelDemoWalk("client_" .. tostring(result))
        return failMission("La marche native de Sweet a echoue: " .. tostring(result))
    end

    local x, y = getElementPosition(ped)
    local target = TAGUP.sweetDemoWalk.target
    local distance = getDistanceBetweenPoints2D(x, y, target.x, target.y)
    if distance > TAGUP.sweetDemoWalk.serverCompletionRadius then
        cancelDemoWalk("invalid_completion_position")
        return failMission(("Sweet a termine sa marche trop loin du tag (%.2f m)."):format(distance))
    end

    cancelDemoWalk("completed")
    startDemoShoot(ped, distance)
end)

addEvent("tagup:sweetDemoShootResult", true)
addEventHandler("tagup:sweetDemoShootResult", resourceRoot, function(shootId, ped, result, details)
    local player = client
    local shoot = mission.demoShoot
    if source ~= resourceRoot or not mission.running or mission.stage ~= "demo" or player ~= mission.leader or not isMissionPlayer(player) or
        not shoot or shoot.id ~= tonumber(shootId) or shoot.ped ~= ped or ped ~= mission.entities.sweet or not isElement(ped) or isPedDead(ped) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Sweet shoot result", 2)
        return
    end

    details = tostring(details or "")
    outputDebugString(("[tagging-up-turf] Sweet native shoot #%d result=%s (%s)"):format(shoot.id, tostring(result), details:sub(1, 240)))
    if getElementSyncer(ped) ~= player then
        cancelDemoShoot("invalid_syncer")
        return failMission("Le resultat du tir de Sweet ne vient plus de son syncer.")
    end

    -- In SWEET1 the 15-second task is only a ceiling: CTagManager reaching 100%
    -- interrupts it. Any spontaneous task end before our authoritative tag reaches
    -- 100% is therefore a failure, not the success condition for this stage.
    cancelDemoShoot("client_" .. tostring(result))
    failMission("Le tir natif de Sweet s'est termine avant que le tag soit recouvert: " .. tostring(result))
end)

addEvent("tagup:sweetDemoShootObserved", true)
addEventHandler("tagup:sweetDemoShootObserved", resourceRoot, function(shootId, ped)
    local player = client
    local shoot = mission.demoShoot
    if source ~= resourceRoot or not mission.running or mission.stage ~= "demo" or player ~= mission.leader or not isMissionPlayer(player) or
        not shoot or shoot.id ~= tonumber(shootId) or shoot.ped ~= ped or ped ~= mission.entities.sweet or not isElement(ped) or isPedDead(ped) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Sweet shoot observation", 2)
        return
    end
    if shoot.observedAt then
        outputDebugString(("[tagging-up-turf] Ignoring duplicate Sweet native shoot #%d observation"):format(shoot.id), 2)
        return
    end
    if getElementSyncer(ped) ~= player then
        cancelDemoShoot("invalid_observation_syncer")
        return failMission("Le demarrage du tir de Sweet ne vient pas de son syncer.")
    end

    local profile = TAGUP.sweetDemoShoot
    local demo = TAGUP.demoTag
    local x, y, z = getElementPosition(ped)
    local distance = tagupDistance3D(x, y, z, demo.x, demo.y, demo.z)
    if distance > profile.serverMaxDistance or getPedWeapon(ped) ~= TAGUP.sprayWeapon or not isElement(mission.entities.demoTag) then
        cancelDemoShoot("invalid_observation_state")
        return failMission(("Sweet a commence le tir dans un etat invalide (distance=%.2f m, weapon=%d)."):format(distance, getPedWeapon(ped)))
    end

    shoot.observedAt = getTickCount()
    shoot.progress = 0
    outputDebugString(("[tagging-up-turf] Sweet native shoot #%d observed after %d ms; starting authoritative demo-tag progress (task ceiling=%d ms)"):format(
        shoot.id, shoot.observedAt - shoot.requestedAt, profile.duration))

    shoot.progressTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.demoShoot
        if not mission.running or mission.stage ~= "demo" or not active or active.id ~= expectedId then
            return
        end
        if not isElement(active.ped) or isPedDead(active.ped) or not isElement(mission.leader) or getElementSyncer(active.ped) ~= mission.leader then
            cancelDemoShoot("progress_ownership_lost")
            return failMission("Sweet ou son syncer a disparu pendant la progression du tag.")
        end

        local px, py, pz = getElementPosition(active.ped)
        local activeDistance = tagupDistance3D(px, py, pz, demo.x, demo.y, demo.z)
        if activeDistance > profile.serverMaxDistance or getPedWeapon(active.ped) ~= TAGUP.sprayWeapon or not isElement(mission.entities.demoTag) then
            cancelDemoShoot("invalid_progress_state")
            return failMission(("Sweet ne peut plus recouvrir le tag (distance=%.2f m, weapon=%d)."):format(activeDistance,
                                                                                                           getPedWeapon(active.ped)))
        end

        local previousProgress = active.progress
        active.progress = math.min(1, previousProgress + profile.progressPerTick)
        setElementData(mission.entities.demoTag, "tagup.paintAlpha", math.floor(255 * active.progress + 0.5), true)
        if math.floor(previousProgress * 4) ~= math.floor(active.progress * 4) then
            outputDebugString(("[tagging-up-turf] Sweet demo tag: %d%% (server-authoritative)"):format(math.floor(active.progress * 100)))
        end
        if active.progress < 1 then
            return
        end

        if isTimer(active.progressTimer) then
            killTimer(active.progressTimer)
        end
        active.progressTimer = nil
        active.nativeCancelled = true
        setElementData(mission.entities.demoTag, "tagup.paintAlpha", 255, true)
        triggerClientEvent(mission.leader, "tagup:sweetDemoShootCancel", resourceRoot, active.id, "authoritative_tag_complete")
        outputDebugString(("[tagging-up-turf] Sweet demo tag reached 100%%; interrupting native shoot and honoring SCM WAIT %d"):format(
            profile.postCompletionWait))

        active.completionTimer = rememberTimer(setTimer(function(completedId)
            local completed = mission.demoShoot
            if not mission.running or mission.stage ~= "demo" or not completed or completed.id ~= completedId then
                return
            end
            if not isElement(completed.ped) or isPedDead(completed.ped) or not isElement(mission.entities.demoTag) then
                cancelDemoShoot("invalid_post_wait_state")
                return failMission("Sweet ou le tag a disparu pendant l'attente de fin de demonstration.")
            end

            cancelDemoShoot("completed_after_scm_wait")
            outputDebugString("[tagging-up-turf] SCM WAIT 1000 complete; advancing without the not-yet-ported checkout animation, audio, or camera")
            -- SWEET1 releases the player to spray the two tags while Sweet walks
            -- back to the Greenwood. Keep both operations concurrent.
            setStage("tags_idlewood", {deferTraceStep = true})
            startSweetReturnEnter(completed.ped)
        end, profile.postCompletionWait, 1, active.id))
    end, profile.progressInterval, 0, shoot.id))
end)

local function allMissionPlayersExitedBallasVehicle(departure)
    for _, player in ipairs(mission.party) do
        if isElement(player) and not departure.exitedPlayers[player] then
            return false
        end
    end
    return true
end

local function tryStartBallasWander()
    local departure = mission.ballasDeparture
    local sweet, vehicle = mission.entities.sweet, mission.entities.vehicle
    if not departure or departure.wanderRequested or not allMissionPlayersExitedBallasVehicle(departure) then
        return
    end
    if not isElement(sweet) or not isElement(vehicle) or getVehicleController(vehicle) or getPedOccupiedVehicle(sweet) ~= vehicle then
        cancelBallasDeparture("invalid_wander_state")
        return failMission("La Greenwood n'est pas prete pour le depart natif de Sweet.")
    end

    -- The ped task drives the vehicle autopilot, so the same persistent client
    -- must own both streams for ordinary MTA synchronization to carry 05D2.
    setElementSyncer(sweet, mission.leader, true, true)
    setElementSyncer(vehicle, mission.leader, true, true)
    departure.wanderRequested = true
    departure.wanderRequestedAt = getTickCount()
    outputDebugString(("[tagging-up-turf] All players exited; requesting 05D2 DriveWander #%d (speed=%.1f, style=%s) from %s"):format(
                          departure.id, TAGUP.ballasDeparture.speed, TAGUP.ballasDeparture.drivingStyle, getPlayerName(mission.leader)))
    triggerClientEvent(mission.leader, "tagup:ballasDriveWanderStart", resourceRoot, departure.id, sweet, vehicle, TAGUP.ballasDeparture)
end

local function startBallasDeparture()
    local vehicle, sweet = mission.entities.vehicle, mission.entities.sweet
    if not isElement(vehicle) or not isElement(sweet) then
        return failMission("Sweet ou la Greenwood a disparu a l'arrivee Ballas.")
    end

    cancelBallasDeparture("replaced")
    mission.ballasDepartureSerial = mission.ballasDepartureSerial + 1
    local departure = {
        id = mission.ballasDepartureSerial,
        vehicle = vehicle,
        ped = sweet,
        exitedPlayers = {},
        clientExitReports = {},
        requestedAt = getTickCount(),
    }
    mission.ballasDeparture = departure
    setStage("ballas_departure", {deferTraceStep = true})

    departure.guardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.ballasDeparture
        if not mission.running or mission.stage ~= "ballas_departure" or not active or active.id ~= expectedId then
            return
        end
        cancelBallasDeparture("server_timeout")
        failMission("La sequence native de depart de Sweet a depasse le delai de garde.")
    end, TAGUP.ballasDeparture.guardTimeout, 1, departure.id))

    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasPlayerExitStart", resourceRoot, departure.id, vehicle, TAGUP.ballasDeparture)
        end
    end
end

addEvent("tagup:ballasPlayerExitResult", true)
addEventHandler("tagup:ballasPlayerExitResult", resourceRoot, function(departureId, vehicle, result, details)
    local player = client
    local departure = mission.ballasDeparture
    if source ~= resourceRoot or not mission.running or mission.stage ~= "ballas_departure" or not departure or
        departure.id ~= tonumber(departureId) or departure.vehicle ~= vehicle or vehicle ~= mission.entities.vehicle or not isMissionPlayer(player) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Ballas exit result", 2)
        return
    end

    outputDebugString(("[tagging-up-turf] Ballas departure #%d player=%s exit=%s (%s)"):format(
                          departure.id, getPlayerName(player), tostring(result), tostring(details or ""):sub(1, 180)))
    if result ~= "exited" and result ~= "already_out" then
        cancelBallasDeparture("client_exit_" .. tostring(result))
        return failMission("La sortie native d'un membre de l'equipe a echoue: " .. tostring(result))
    end
    departure.clientExitReports[player] = true
    if getPedOccupiedVehicle(player) ~= vehicle then
        departure.exitedPlayers[player] = true
        tryStartBallasWander()
    else
        outputDebugString(("[tagging-up-turf] Waiting for server onVehicleExit for %s"):format(getPlayerName(player)))
    end
end)

addEvent("tagup:ballasDriveWanderResult", true)
addEventHandler("tagup:ballasDriveWanderResult", resourceRoot, function(departureId, ped, vehicle, result, details)
    local player = client
    local departure = mission.ballasDeparture
    if source ~= resourceRoot or not mission.running or mission.stage ~= "ballas_departure" or player ~= mission.leader or not departure or
        departure.id ~= tonumber(departureId) or departure.ped ~= ped or departure.vehicle ~= vehicle or ped ~= mission.entities.sweet or
        vehicle ~= mission.entities.vehicle then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized DriveWander result", 2)
        return
    end

    outputDebugString(("[tagging-up-turf] Sweet 05D2 #%d result=%s (%s)"):format(
                          departure.id, tostring(result), tostring(details or ""):sub(1, 220)))
    if result ~= "observed" then
        cancelBallasDeparture("client_wander_" .. tostring(result))
        return failMission("Le depart natif de Sweet a echoue: " .. tostring(result))
    end
    if departure.wanderObserved or getElementSyncer(ped) ~= player or getElementSyncer(vehicle) ~= player or getVehicleController(vehicle) then
        cancelBallasDeparture("invalid_wander_observation")
        return failMission("L'observation 05D2 ne vient pas du double syncer attendu.")
    end

    departure.wanderObserved = true
    departure.postStartTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.ballasDeparture
        if not mission.running or mission.stage ~= "ballas_departure" or not active or active.id ~= expectedId then
            return
        end
        cancelBallasDeparture("keep_wandering")
        setStage("tags_ballas")
        spawnBallas()
    end, TAGUP.ballasDeparture.postStartWait, 1, departure.id))
end)

addEvent("tagup:vehicleReady", true)
addEventHandler("tagup:vehicleReady", resourceRoot, function(kind)
    local player = client
    if not mission.running or not isMissionPlayer(player) or player ~= mission.leader then
        return
    end
    local vehicle = mission.entities.vehicle
    if getPedOccupiedVehicle(player) ~= vehicle or getVehicleController(vehicle) ~= player then
        return
    end

    if kind == "party" and mission.stage == "enter_car" and isPartyInVehicle() then
        if warpSweetIntoFirstFreeSeat() then
            setStage("drive_idlewood", {message = "Sweet est a bord. Direction Idlewood."})
        else
            broadcastState({message = "Impossible d'installer Sweet dans la voiture."})
        end
    elseif kind == "idlewood" and mission.stage == "drive_idlewood" then
        local x, y, z = getElementPosition(vehicle)
        if tagupDistance3D(x, y, z, unpack(TAGUP.idlewoodDestination)) < 11 then
            setStage("demo")
            triggerEvent("tagup:beginDemo", resourceRoot)
        end
    elseif kind == "returned" and mission.stage == "return_car" and isPartyInVehicle() then
        if getPedOccupiedVehicle(mission.entities.sweet) == vehicle and getPedOccupiedVehicleSeat(mission.entities.sweet) == TAGUP.sweetReturnEnter.seat then
            setStage("drive_ballas")
        elseif mission.demoEnter then
            broadcastState({message = "Attendez que Sweet finisse de monter."})
        else
            failMission("Sweet n'est pas dans la Greenwood apres son entree passager native.")
        end
    elseif kind == "ballas" and mission.stage == "drive_ballas" then
        local x, y, z = getElementPosition(vehicle)
        if tagupDistance3D(x, y, z, unpack(TAGUP.ballasDestination)) < 13 then
            startBallasDeparture()
        end
    elseif kind == "roof_return" and mission.stage == "return_after_roof" and isPartyInVehicle() then
        warpSweetIntoFirstFreeSeat()
        setStage("drive_home")
    elseif kind == "home" and mission.stage == "drive_home" then
        local x, y, z = getElementPosition(vehicle)
        if tagupDistance3D(x, y, z, unpack(TAGUP.homeDestination)) < 12 then
            finishMission(true)
        end
    end
end)

addEvent("tagup:spray", true)
addEventHandler("tagup:spray", resourceRoot, function(tagId)
    local player = client
    tagId = tonumber(tagId)
    if not mission.running or not isMissionPlayer(player) or not tagId or mission.completedTags[tagId] then
        return
    end

    local active = false
    for _, id in ipairs(activeTagIds()) do
        if id == tagId then
            active = true
            break
        end
    end
    if not active or getPedWeapon(player) ~= TAGUP.sprayWeapon then
        return
    end

    -- Distance, weapon and rate checks intentionally duplicate client-side checks:
    -- client prediction keeps spraying responsive, but only this path grants progress.
    local tag = tagupGetTag(tagId)
    local x, y, z = getElementPosition(player)
    if tagupDistance3D(x, y, z, tag.x, tag.y, tag.z) > TAGUP.sprayRange then
        return
    end

    local now = getTickCount()
    if mission.sprayCooldown[player] and now - mission.sprayCooldown[player] < 100 then
        return
    end
    mission.sprayCooldown[player] = now
    local previousProgress = mission.tagProgress[tagId] or 0
    mission.tagProgress[tagId] = math.min(1, previousProgress + 0.05)
    updateTagVisual(tagId, mission.tagProgress[tagId])
    if math.floor(previousProgress * 4) ~= math.floor(mission.tagProgress[tagId] * 4) then
        outputDebugString(
            ("[tagging-up-turf] Tag %d: %d%% by %s"):format(tagId, math.floor(mission.tagProgress[tagId] * 100), getPlayerName(player))
        )
    end
    if mission.tagProgress[tagId] >= 1 then
        mission.completedTags[tagId] = true
        replaceTagObject(tagId)
        broadcastState({message = getPlayerName(player) .. " a termine un tag."})
        if currentGroupComplete() then
            rememberTimer(setTimer(advanceAfterTags, 900, 1))
        end
    else
        broadcastState()
    end
end)

addEventHandler("onVehicleExplode", root, function()
    if mission.running and source == mission.entities.vehicle then
        failMission("La Greenwood de Sweet a ete detruite.")
    end
end)

addEventHandler("onVehicleExit", root, function(ped)
    local leave = mission.demoLeave
    if mission.running and mission.stage == "demo" and leave and source == leave.vehicle and ped == leave.ped then
        leave.serverExited = true
        outputDebugString(("[tagging-up-turf] Server observed Sweet leave vehicle for native leave-car #%d"):format(leave.id))
        tryCompleteDemoLeave()
    end

    local departure = mission.ballasDeparture
    if mission.running and mission.stage == "ballas_departure" and departure and source == departure.vehicle and isMissionPlayer(ped) then
        outputDebugString(("[tagging-up-turf] Server observed Ballas departure vehicle exit for %s"):format(getPlayerName(ped)))
        if departure.clientExitReports[ped] then
            departure.exitedPlayers[ped] = true
            tryStartBallasWander()
        end
    end
end)

addEvent("tagup:sweetReturnEnterResult", true)
addEventHandler("tagup:sweetReturnEnterResult", resourceRoot, function(enterId, ped, vehicle, result, details)
    local player = client
    local enter = mission.demoEnter
    if source ~= resourceRoot or not mission.running or (mission.stage ~= "tags_idlewood" and mission.stage ~= "return_car") or
        player ~= mission.leader or not isMissionPlayer(player) or not enter or enter.id ~= tonumber(enterId) or enter.ped ~= ped or
        enter.vehicle ~= vehicle or ped ~= mission.entities.sweet or vehicle ~= mission.entities.vehicle or not isElement(ped) or
        not isElement(vehicle) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Sweet passenger-entry result", 2)
        return
    end

    details = tostring(details or "")
    outputDebugString(("[tagging-up-turf] Sweet native passenger entry #%d result=%s (%s)"):format(
                          enter.id, tostring(result), details:sub(1, 240)))
    if result ~= "entered" then
        cancelDemoEnter("client_" .. tostring(result))
        return failMission("L'entree passager native de Sweet a echoue: " .. tostring(result))
    end
    if getElementSyncer(ped) ~= player then
        cancelDemoEnter("invalid_syncer")
        return failMission("Le resultat d'entree passager de Sweet ne vient plus de son syncer.")
    end

    enter.clientObserved = true
    tryCompleteSweetReturnEnter()
end)

addEventHandler("onVehicleEnter", root, function(ped, seat)
    local enter = mission.demoEnter
    if not mission.running or not enter or source ~= enter.vehicle or ped ~= enter.ped then
        return
    end

    enter.serverEntered = tonumber(seat) == enter.seat
    outputDebugString(("[tagging-up-turf] Server observed Sweet enter passenger seat %d for native entry #%d"):format(
                          tonumber(seat) or -1, enter.id))
    if not enter.serverEntered then
        cancelDemoEnter("wrong_server_seat")
        return failMission("Sweet est monte dans le mauvais siege.")
    end
    tryCompleteSweetReturnEnter()
end)

addEventHandler("onPedWasted", root, function()
    if not mission.running then
        return
    end
    if source == mission.entities.sweet then
        failMission("Sweet est mort.")
    elseif getElementData(source, "tagup.enemy") then
        setElementData(source, "tagup.active", false, true)
    end
end)

addEventHandler("onElementDestroy", root, function()
    if mission.running and (source == mission.entities.sweet or source == mission.entities.vehicle) and
        (mission.demoLeave or mission.demoWalk or mission.demoShoot or mission.demoEnter or mission.ballasDeparture) then
        cancelDemoLeave("ped_destroyed")
        cancelDemoWalk("ped_destroyed")
        cancelDemoShoot("ped_destroyed")
        cancelDemoEnter("ped_destroyed")
        cancelBallasDeparture("ped_destroyed")
        failMission("Sweet ou la Greenwood a ete detruit pendant sa demonstration native.")
    end
end)

addEventHandler("onPlayerWasted", root, function()
    if mission.running and isMissionPlayer(source) then
        local alive = false
        for _, player in ipairs(mission.party) do
            if isElement(player) and not isPedDead(player) then
                alive = true
                break
            end
        end
        if not alive then
            failMission("Toute l'equipe est morte.")
        end
    end
end)

addEventHandler("onPlayerQuit", root, function()
    if mission.running and isMissionPlayer(source) then
        failMission("Un membre de l'equipe a quitte la mission.")
    end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    cancelDemoLeave("resource_stopped")
    cancelDemoWalk("resource_stopped")
    cancelDemoShoot("resource_stopped")
    cancelDemoEnter("resource_stopped")
    cancelBallasDeparture("resource_stopped")
    clearMissionTimers()
    for _, player in ipairs(mission.party) do
        restorePlayer(player, mission.snapshots[player])
    end
    destroyMissionEntities()
end)

addEventHandler("onResourceStart", resourceRoot, function()
    outputDebugString("[tagging-up-turf] Ready. Use /tagup (up to three connected players).")
end)
