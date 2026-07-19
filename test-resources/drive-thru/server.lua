local mission = {
    running = false,
    finishing = false,
    stage = nil,
    leader = nil,
    snapshot = nil,
    entities = {},
    timers = {},
    cutsceneSerial = 0,
    cutscene = nil,
    audioSerial = 0,
    audio = nil,
    driveLineIndex = 0,
    leaderInVehicle = false,
    actorTasksAccepted = false,
    actorsSeated = false,
    introFinished = false,
}

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

local function snapshotPedClothes(ped)
    local clothes = {}
    for clothingType = 0, DRIVETHRU.cj.clothingSlots - 1 do
        local texture, model = getPedClothes(ped, clothingType)
        if type(texture) == "string" and type(model) == "string" then
            clothes[clothingType] = {texture = texture, model = model}
        end
    end
    return clothes
end

local function clearPedClothes(ped)
    for clothingType = 0, DRIVETHRU.cj.clothingSlots - 1 do
        local texture = getPedClothes(ped, clothingType)
        if type(texture) == "string" and not removePedClothes(ped, clothingType) then
            return false, clothingType
        end
    end
    return true
end

local function applyPedClothes(ped, clothes)
    local cleared, failedType = clearPedClothes(ped)
    if not cleared then
        return false, ("remove slot %d refused"):format(failedType)
    end
    for clothingType, clothing in pairs(clothes or {}) do
        if not addPedClothes(ped, clothing.texture, clothing.model, clothingType) then
            return false, ("add slot %d %s/%s refused"):format(clothingType, clothing.texture, clothing.model)
        end
    end
    return true
end

local function applyMissionCJ(player)
    if getElementModel(player) ~= DRIVETHRU.cj.model and not setElementModel(player, DRIVETHRU.cj.model) then
        return false, "CJ model 0 refused"
    end
    local clothes = {}
    for _, clothing in ipairs(DRIVETHRU.cj.clothes) do
        clothes[clothing.type] = {texture = clothing.texture, model = clothing.model}
    end
    local applied, details = applyPedClothes(player, clothes)
    if not applied then
        return false, details
    end
    for _, expected in ipairs(DRIVETHRU.cj.clothes) do
        local texture, model = getPedClothes(player, expected.type)
        if type(texture) ~= "string" or type(model) ~= "string" or texture:lower() ~= expected.texture or
            model:lower() ~= expected.model then
            return false, ("slot %d readback mismatch: %s/%s"):format(expected.type, tostring(texture), tostring(model))
        end
    end
    return true
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
        clothes = snapshotPedClothes(player),
        weapons = weapons,
    }
end

local function restorePlayer(player, snapshot)
    if not isElement(player) or not snapshot then
        return
    end
    removePedFromVehicle(player)
    if getElementModel(player) ~= DRIVETHRU.cj.model then
        setElementModel(player, DRIVETHRU.cj.model)
    end
    applyPedClothes(player, snapshot.clothes)
    setElementModel(player, snapshot.model)
    setElementInterior(player, snapshot.interior)
    setElementDimension(player, snapshot.dimension)
    setElementPosition(player, snapshot.x, snapshot.y, snapshot.z)
    setElementRotation(player, 0, 0, snapshot.rotation)
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

local function resetMissionState()
    mission.running = false
    mission.finishing = false
    mission.stage = nil
    mission.leader = nil
    mission.snapshot = nil
    mission.cutscene = nil
    mission.audio = nil
    mission.driveLineIndex = 0
    mission.leaderInVehicle = false
    mission.actorTasksAccepted = false
    mission.actorsSeated = false
    mission.introFinished = false
end

local function cleanupMission(reason, restore)
    if not mission.running then
        return
    end
    local leader, snapshot = mission.leader, mission.snapshot
    clearMissionTimers()
    if isElement(leader) then
        triggerClientEvent(leader, "drivethru:stop", resourceRoot, reason or "cleanup")
    end
    destroyMissionEntities()
    if restore and isElement(leader) then
        restorePlayer(leader, snapshot)
    end
    outputDebugString(("[drive-thru] Cleanup complete: %s"):format(tostring(reason or "cleanup")))
    resetMissionState()
end

local function failMission(reason, textKey)
    if not mission.running or mission.finishing then
        return
    end
    mission.finishing = true
    mission.stage = "failed"
    outputDebugString("[drive-thru] Mission failed: " .. tostring(reason), 1)
    if isElement(mission.leader) then
        triggerClientEvent(mission.leader, "drivethru:failed", resourceRoot, textKey, reason)
    end
    rememberTimer(setTimer(function()
        cleanupMission("failed", true)
    end, 5000, 1))
end

local function startMissionWatchdog()
    rememberTimer(setTimer(function()
        if not mission.running or mission.finishing then
            return
        end
        local vehicle = mission.entities.vehicle
        if mission.stage ~= "cutscene" and not isElement(vehicle) then
            return failMission("The Greenwood element disappeared", "SWE3_D")
        end
        if isElement(vehicle) and mission.stage ~= "cutscene" and getElementHealth(vehicle) <= 250 then
            return failMission("The Greenwood health reached the vanilla 250 threshold", "SWE2_KC")
        end
        for _, actor in ipairs({"sweet", "ryder", "smoke"}) do
            if mission.entities[actor] and not isElement(mission.entities[actor]) then
                return failMission(actor .. " element disappeared")
            end
        end
    end, 250, 0))
end

local function createGreenwood()
    local profile = DRIVETHRU.vehicle
    local vehicle = createVehicle(profile.model, profile.position.x, profile.position.y, profile.position.z, 0, 0, profile.position.heading)
    if not vehicle then
        return false
    end
    setElementDimension(vehicle, DRIVETHRU.dimension)
    setVehicleColor(vehicle, profile.primaryColor[1], profile.primaryColor[2], profile.primaryColor[3], profile.secondaryColor[1],
                    profile.secondaryColor[2], profile.secondaryColor[3])
    setVehiclePlateText(vehicle, profile.plate)
    setVehicleDamageProof(vehicle, false)
    setVehicleLocked(vehicle, false)
    setVehicleEngineState(vehicle, false)
    setElementData(vehicle, DRIVETHRU.vehicleData, true, true)
    setElementSyncer(vehicle, mission.leader, true, true)
    mission.entities.vehicle = vehicle
    local x, y, z = getElementPosition(vehicle)
    local _, _, heading = getElementRotation(vehicle)
    local primaryR, primaryG, primaryB, secondaryR, secondaryG, secondaryB = getVehicleColor(vehicle, true)
    outputDebugString(("[drive-thru] Greenwood created position=(%.3f, %.3f, %.3f) heading=%.1f colours=(%d,%d,%d)/(%d,%d,%d) plate=%s"):format(
                          x, y, z, heading, primaryR, primaryG, primaryB, secondaryR, secondaryG, secondaryB,
                          tostring(getVehiclePlateText(vehicle))))
    return true
end

local function createMissionActor(name, profile)
    -- GTA's CREATE_CHAR converts script ground Z to the native ped centre by
    -- adding one metre. MTA createPed consumes the centre directly.
    local ped = createPed(profile.model, profile.position.x, profile.position.y, profile.position.scriptZ + 1.0, profile.position.heading)
    if not ped then
        return false
    end
    setElementDimension(ped, DRIVETHRU.dimension)
    setPedWalkingStyle(ped, profile.walkingStyle)
    setElementHealth(ped, 500)
    giveWeapon(ped, DRIVETHRU.weapon.id, DRIVETHRU.weapon.ammo, true)
    setElementData(ped, DRIVETHRU.missionActorData, true, true)
    setElementData(ped, "drivethru.actor", name, true)
    setElementSyncer(ped, mission.leader, true, true)
    mission.entities[name] = ped
    return true
end

local function createWorldActors()
    for _, name in ipairs({"smoke", "sweet", "ryder"}) do
        if not createMissionActor(name, DRIVETHRU.actors[name]) then
            return false
        end
    end
    return true
end

local function allActorsSeated()
    local vehicle = mission.entities.vehicle
    if not isElement(vehicle) then
        return false
    end
    for _, name in ipairs({"smoke", "sweet", "ryder"}) do
        local ped = mission.entities[name]
        local profile = DRIVETHRU.actors[name]
        if not isElement(ped) or getPedOccupiedVehicle(ped) ~= vehicle or getPedOccupiedVehicleSeat(ped) ~= profile.seat then
            return false
        end
    end
    return true
end

local function queueAudio(profile, purpose, index)
    if mission.audio or not mission.running or not isElement(mission.leader) then
        return false
    end
    mission.audioSerial = mission.audioSerial + 1
    mission.audio = {
        id = mission.audioSerial,
        profile = profile,
        purpose = purpose,
        index = index,
    }
    triggerClientEvent(mission.leader, "drivethru:audioPrepare", resourceRoot, mission.audio.id, profile)
    return true
end

local function queueNextDriveLine()
    if not mission.running or mission.stage ~= "drive" or mission.audio or not mission.leaderInVehicle then
        return
    end
    local nextIndex = mission.driveLineIndex + 1
    local profile = DRIVETHRU.audio.drive[nextIndex]
    if profile then
        queueAudio(profile, "drive", nextIndex)
    end
end

local function beginDrive()
    if mission.stage == "drive" or mission.stage == "checkpoint" then
        return
    end
    if not mission.actorsSeated or not allActorsSeated() then
        return failMission("Drive stage requested before the three passengers were seated")
    end
    mission.stage = "drive"
    triggerClientEvent(mission.leader, "drivethru:stage", resourceRoot, "drive", mission.entities)
    outputDebugString("[drive-thru] Leader entered the Greenwood; destination gate active")
    rememberTimer(setTimer(queueNextDriveLine, 4000, 1))
end

local function advanceAfterIntroAndEntry()
    if mission.stage ~= "actor_entry" or not mission.introFinished or not mission.actorsSeated then
        return
    end
    mission.stage = "enter_car"
    triggerClientEvent(mission.leader, "drivethru:stage", resourceRoot, "enter_car", mission.entities)
    if mission.leaderInVehicle then
        beginDrive()
    end
end

local function observeActorSeats()
    if mission.stage ~= "actor_entry" or mission.actorsSeated or not allActorsSeated() then
        return
    end
    mission.actorsSeated = true
    outputDebugString("[drive-thru] All three passengers reached their authoritative Greenwood seats")
    advanceAfterIntroAndEntry()
end

local function beginWorldAfterCutscene()
    local leader = mission.leader
    if not isElement(leader) then
        return failMission("Leader unavailable after SWEET2A", "SWE3_D")
    end
    -- The file cutscene owns and tears down GTA model instances globally.
    -- Create synchronized world entities only once that native teardown has
    -- completed, while the screen is still black, so MTA never has to recover
    -- a Greenwood instance invalidated underneath it.
    if not createGreenwood() then
        return failMission("Greenwood could not be created after SWEET2A", "SWE3_D")
    end
    if not createWorldActors() then
        return failMission("Mission actors could not be created after SWEET2A")
    end
    local world = DRIVETHRU.cj.world
    removePedFromVehicle(leader)
    setElementInterior(leader, 0)
    setElementDimension(leader, DRIVETHRU.dimension)
    setElementPosition(leader, world.x, world.y, world.scriptZ + 1.0)
    setElementRotation(leader, 0, 0, world.heading)
    setElementFrozen(leader, false)
    mission.stage = "actor_entry"
    triggerClientEvent(leader, "drivethru:stage", resourceRoot, "actor_entry", mission.entities)
    rememberTimer(setTimer(function()
        if mission.running and not mission.finishing and mission.stage == "actor_entry" and not mission.actorsSeated then
            local vehicle = mission.entities.vehicle
            local seats = {}
            for _, name in ipairs({"smoke", "sweet", "ryder"}) do
                local ped = mission.entities[name]
                seats[#seats + 1] = ("%s=%s/%s"):format(name, tostring(isElement(ped) and getPedOccupiedVehicle(ped) == vehicle),
                                                        tostring(isElement(ped) and getPedOccupiedVehicleSeat(ped) or "none"))
            end
            failMission("The native passenger tasks did not seat the full crew in time: " .. table.concat(seats, ", "))
        end
    end, DRIVETHRU.actorEntryTimeout, 1))
end

local function startFileCutscene()
    mission.cutsceneSerial = mission.cutsceneSerial + 1
    mission.cutscene = {id = mission.cutsceneSerial, ready = false, started = false, finished = false}
    mission.stage = "cutscene"
    triggerClientEvent(mission.leader, "drivethru:cutscenePrepare", resourceRoot, mission.cutscene.id)
    rememberTimer(setTimer(function(expectedId)
        if mission.cutscene and mission.cutscene.id == expectedId and not mission.cutscene.ready then
            failMission("SWEET2A loading timed out")
        end
    end, DRIVETHRU.cutscene.loadTimeout + DRIVETHRU.cutscene.appearanceTimeout, 1, mission.cutscene.id))
end

local function startMission(player)
    if mission.running then
        outputChatBox("Drive-Thru est deja en cours.", player, 255, 190, 80)
        return
    end
    mission.running = true
    mission.leader = player
    mission.snapshot = snapshotPlayer(player)
    mission.snapshot.cjAppearanceApplied = true
    local applied, details = applyMissionCJ(player)
    if not applied then
        outputDebugString("[drive-thru] CJ appearance failed: " .. tostring(details), 1)
        cleanupMission("appearance_failed", true)
        return
    end
    removePedFromVehicle(player)
    setElementInterior(player, 0)
    setElementDimension(player, DRIVETHRU.dimension)
    setElementFrozen(player, true)
    takeAllWeapons(player)
    triggerClientEvent(player, "drivethru:start", resourceRoot)
    outputDebugString(("[drive-thru] Starting SWEET2A for leader %s in dimension %d"):format(getPlayerName(player), DRIVETHRU.dimension))
    startMissionWatchdog()
    startFileCutscene()
end

addEvent("drivethru:cutsceneReady", true)
addEventHandler("drivethru:cutsceneReady", resourceRoot, function(sceneId, result, details)
    local scene = mission.cutscene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or scene.ready then
        return
    end
    if result ~= "ready" then
        return failMission("SWEET2A preparation failed: " .. tostring(result) .. " " .. tostring(details or ""))
    end
    scene.ready = true
    scene.started = true
    triggerClientEvent(mission.leader, "drivethru:cutsceneStart", resourceRoot, scene.id)
end)

addEvent("drivethru:cutsceneSkipRequest", true)
addEventHandler("drivethru:cutsceneSkipRequest", resourceRoot, function(sceneId)
    local scene = mission.cutscene
    if source == resourceRoot and client == mission.leader and scene and scene.id == tonumber(sceneId) and not scene.skipRequested then
        scene.skipRequested = true
        triggerClientEvent(mission.leader, "drivethru:cutsceneSkip", resourceRoot, scene.id)
    end
end)

addEvent("drivethru:cutsceneFinished", true)
addEventHandler("drivethru:cutsceneFinished", resourceRoot, function(sceneId, result, skipped, elapsed)
    local scene = mission.cutscene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or scene.finished then
        return
    end
    if result ~= "finished" then
        return failMission("SWEET2A playback failed: " .. tostring(result))
    end
    scene.finished = true
    outputDebugString(("[drive-thru] SWEET2A finished skipped=%s elapsed=%s ms"):format(tostring(skipped == true), tostring(elapsed or "?")))
    triggerClientEvent(mission.leader, "drivethru:cutsceneRelease", resourceRoot, scene.id)
    rememberTimer(setTimer(function(expectedId)
        if mission.cutscene and mission.cutscene.id == expectedId then
            failMission("SWEET2A cleanup timed out")
        end
    end, DRIVETHRU.cutscene.releaseTimeout, 1, scene.id))
end)

addEvent("drivethru:cutsceneReleased", true)
addEventHandler("drivethru:cutsceneReleased", resourceRoot, function(sceneId, result)
    local scene = mission.cutscene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) then
        return
    end
    if result ~= "released" then
        return failMission("SWEET2A native state was not released")
    end
    mission.cutscene = nil
    beginWorldAfterCutscene()
end)

addEvent("drivethru:actorTasksReady", true)
addEventHandler("drivethru:actorTasksReady", resourceRoot, function(result, details)
    if source ~= resourceRoot or client ~= mission.leader or mission.stage ~= "actor_entry" then
        return
    end
    if result ~= "accepted" then
        return failMission("Passenger task assignment failed: " .. tostring(details or result))
    end
    for _, name in ipairs({"smoke", "sweet", "ryder"}) do
        local ped = mission.entities[name]
        if not isElement(ped) or getElementSyncer(ped) ~= client then
            return failMission("Passenger task assignment was reported without authoritative ped ownership: " .. name)
        end
    end
    mission.actorTasksAccepted = true
    outputDebugString("[drive-thru] All three native passenger entry tasks accepted")
    queueAudio(DRIVETHRU.audio.intro, "intro")
end)

addEvent("drivethru:audioReady", true)
addEventHandler("drivethru:audioReady", resourceRoot, function(audioId, result, details)
    local audio = mission.audio
    if source ~= resourceRoot or client ~= mission.leader or not audio or audio.id ~= tonumber(audioId) then
        return
    end
    if result ~= "ready" then
        mission.audio = nil
        return failMission("Mission audio load failed: " .. tostring(result) .. " " .. tostring(details or ""))
    end
    triggerClientEvent(mission.leader, "drivethru:audioStart", resourceRoot, audio.id)
end)

addEvent("drivethru:audioFinished", true)
addEventHandler("drivethru:audioFinished", resourceRoot, function(audioId, result, details)
    local audio = mission.audio
    if source ~= resourceRoot or client ~= mission.leader or not audio or audio.id ~= tonumber(audioId) then
        return
    end
    if result ~= "finished" then
        mission.audio = nil
        return failMission("Mission audio playback failed: " .. tostring(result) .. " " .. tostring(details or ""))
    end
    mission.audio = nil
    if audio.purpose == "intro" then
        mission.introFinished = true
        outputDebugString("[drive-thru] SWE2_AA finished; waiting for authoritative passenger seats")
        advanceAfterIntroAndEntry()
    elseif audio.purpose == "drive" then
        mission.driveLineIndex = audio.index
        rememberTimer(setTimer(queueNextDriveLine, DRIVETHRU.audio.gap, 1))
    end
end)

addEvent("drivethru:arrivalReport", true)
addEventHandler("drivethru:arrivalReport", resourceRoot, function(onAllWheels)
    local player, vehicle = client, mission.entities.vehicle
    if source ~= resourceRoot or player ~= mission.leader or mission.stage ~= "drive" or onAllWheels ~= true or
        not isElement(vehicle) or getPedOccupiedVehicle(player) ~= vehicle or getPedOccupiedVehicleSeat(player) ~= 0 then
        return
    end
    local x, y, z = getElementPosition(vehicle)
    local destination = DRIVETHRU.destination
    if math.abs(x - destination.x) > destination.radiusX or math.abs(y - destination.y) > destination.radiusY or
        math.abs(z - destination.z) > destination.radiusZ then
        outputDebugString("[drive-thru] Rejected stale 09D0 arrival report outside the SCM box", 2)
        return
    end
    mission.stage = "checkpoint"
    triggerClientEvent(player, "drivethru:checkpointReached", resourceRoot)
    outputDebugString(("[drive-thru] CHECKPOINT PASSED: restaurant gate position=(%.2f, %.2f, %.2f) driver=true allWheels=true"):format(x, y, z))
end)

addEventHandler("onVehicleEnter", root, function(ped, seat)
    if not mission.running or source ~= mission.entities.vehicle then
        return
    end
    if ped == mission.leader and seat == 0 then
        mission.leaderInVehicle = true
        if mission.stage == "enter_car" then
            beginDrive()
        elseif mission.stage == "drive" then
            triggerClientEvent(ped, "drivethru:stage", resourceRoot, "drive", mission.entities)
            rememberTimer(setTimer(queueNextDriveLine, DRIVETHRU.audio.gap, 1))
        end
    else
        for _, name in ipairs({"smoke", "sweet", "ryder"}) do
            if ped == mission.entities[name] then
                outputDebugString(("[drive-thru] %s entered authoritative Greenwood seat %d"):format(name, seat))
                observeActorSeats()
                break
            end
        end
    end
end)

addEventHandler("onVehicleExit", root, function(ped, seat)
    if mission.running and source == mission.entities.vehicle and ped == mission.leader and seat == 0 then
        mission.leaderInVehicle = false
        if mission.stage == "drive" then
            triggerClientEvent(ped, "drivethru:stage", resourceRoot, "return_car", mission.entities)
        end
    end
end)

addEventHandler("onVehicleExplode", root, function()
    if mission.running and source == mission.entities.vehicle then
        failMission("The Greenwood was destroyed", "SWE3_D")
    end
end)

addEventHandler("onPedWasted", root, function()
    if not mission.running then
        return
    end
    if source == mission.leader then
        failMission("CJ died")
    elseif source == mission.entities.sweet then
        failMission("Sweet died", "SWE3_E")
    elseif source == mission.entities.ryder then
        failMission("Ryder died", "SWE3_F")
    elseif source == mission.entities.smoke then
        failMission("Smoke died", "SWE3_G")
    end
end)

addEventHandler("onPlayerWasted", root, function()
    if mission.running and source == mission.leader then
        failMission("CJ died")
    end
end)

addEventHandler("onPlayerQuit", root, function()
    if mission.running and source == mission.leader then
        cleanupMission("leader_disconnect", false)
    end
end)

addCommandHandler("drivethru", function(player)
    if player then
        startMission(player)
    end
end)

addCommandHandler("drivethruabort", function(player)
    if mission.running and player == mission.leader then
        cleanupMission("leader_abort", true)
    end
end)

addCommandHandler("drivethruskip", function(player)
    local scene = mission.cutscene
    if mission.running and player == mission.leader and scene and not scene.skipRequested then
        scene.skipRequested = true
        triggerClientEvent(player, "drivethru:cutsceneSkip", resourceRoot, scene.id)
    end
end)

addEventHandler("onResourceStart", resourceRoot, function()
    outputDebugString("[drive-thru] Resource ready. Use /drivethru to start the SWEET2A to restaurant checkpoint.")
end)

addEventHandler("onResourceStop", resourceRoot, function()
    if mission.running then
        local leader, snapshot = mission.leader, mission.snapshot
        clearMissionTimers()
        destroyMissionEntities()
        if isElement(leader) then
            restorePlayer(leader, snapshot)
        end
        resetMissionState()
    end
end)
