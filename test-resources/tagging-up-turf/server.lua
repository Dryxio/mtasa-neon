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
    demoWalkSerial = 0,
    demoWalk = nil,
    demoShootSerial = 0,
    demoShoot = nil,
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

local function advanceAfterTags()
    if mission.stage == "tags_idlewood" then
        setStage("return_car")
    elseif mission.stage == "tags_ballas" then
        setStage("rooftop")
    elseif mission.stage == "rooftop" then
        setStage("return_after_roof")
    end
end

local finishMission

local function failMission(reason)
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

finishMission = function(passed)
    if not mission.running or mission.finishing then
        return
    end

    mission.finishing = true
    cancelDemoWalk("mission_finished")
    cancelDemoShoot("mission_finished")
    clearMissionTimers()
    if passed then
        mission.stage = "complete"
        -- Failure paths already log their terminal state; keep successful runs
        -- equally visible so a complete manual mission can be audited afterward.
        outputDebugString(("[tagging-up-turf] Mission passed: rewarding %d participant(s)."):format(#mission.party))
        broadcastState()
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
        mission.demoWalk = nil
        mission.demoShoot = nil
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
        setStage("enter_car")
    elseif mission.stage == "enter_car" then
        if warpSweetIntoFirstFreeSeat() then
            setStage("drive_idlewood")
        end
    elseif mission.stage == "drive_idlewood" then
        setStage("demo")
        triggerEvent("tagup:beginDemo", resourceRoot)
    elseif mission.stage == "demo" then
        cancelDemoWalk("stage_skipped")
        cancelDemoShoot("stage_skipped")
        setStage("tags_idlewood")
    elseif mission.stage == "tags_idlewood" or mission.stage == "tags_ballas" or mission.stage == "rooftop" then
        for _, id in ipairs(activeTagIds()) do
            mission.completedTags[id] = true
            mission.tagProgress[id] = 1
            replaceTagObject(id)
        end
        advanceAfterTags()
    elseif mission.stage == "return_car" then
        setStage("drive_ballas")
    elseif mission.stage == "drive_ballas" then
        setStage("tags_ballas")
        spawnBallas()
    elseif mission.stage == "return_after_roof" then
        setStage("drive_home")
    elseif mission.stage == "drive_home" then
        finishMission(true)
    end
end)

addEvent("tagup:beginDemo", false)
addEventHandler("tagup:beginDemo", resourceRoot, function()
    if not mission.running or mission.stage ~= "demo" then
        return
    end
    local sweet = mission.entities.sweet
    if not isElement(sweet) or not isElement(mission.leader) then
        return failMission("Sweet ou le leader n'est plus disponible pour la demonstration.")
    end

    cancelDemoWalk("replaced")
    cancelDemoShoot("replaced")
    removePedFromVehicle(sweet)
    local profile = TAGUP.sweetDemoWalk
    setElementInterior(sweet, 0)
    setElementDimension(sweet, TAGUP.dimension)
    setElementPosition(sweet, profile.start.x, profile.start.y, profile.start.z)
    setElementRotation(sweet, 0, 0, profile.start.heading)
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

    outputDebugString(("[tagging-up-turf] Starting Sweet native go-to #%d for syncer %s"):format(walk.id, getPlayerName(mission.leader)))
    triggerClientEvent(mission.leader, "tagup:sweetDemoWalkStart", resourceRoot, walk.id, sweet, profile)
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
            setStage("tags_idlewood")
        end, profile.postCompletionWait, 1, active.id))
    end, profile.progressInterval, 0, shoot.id))
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
        warpSweetIntoFirstFreeSeat()
        setStage("drive_ballas")
    elseif kind == "ballas" and mission.stage == "drive_ballas" then
        local x, y, z = getElementPosition(vehicle)
        if tagupDistance3D(x, y, z, unpack(TAGUP.ballasDestination)) < 13 then
            removePedFromVehicle(mission.entities.sweet)
            setElementPosition(mission.entities.sweet, 2341.0, -1498.5, 23.0)
            setStage("tags_ballas")
            spawnBallas()
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
    if mission.running and source == mission.entities.sweet and (mission.demoWalk or mission.demoShoot) then
        cancelDemoWalk("ped_destroyed")
        cancelDemoShoot("ped_destroyed")
        failMission("Sweet a ete detruit pendant sa demonstration native.")
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
    cancelDemoWalk("resource_stopped")
    cancelDemoShoot("resource_stopped")
    clearMissionTimers()
    for _, player in ipairs(mission.party) do
        restorePlayer(player, mission.snapshots[player])
    end
    destroyMissionEntities()
end)

addEventHandler("onResourceStart", resourceRoot, function()
    outputDebugString("[tagging-up-turf] Ready. Use /tagup (up to three connected players).")
end)
