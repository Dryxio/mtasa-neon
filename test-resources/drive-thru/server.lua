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
    chaseDialoguePhase = nil,
    chaseLineIndex = 0,
    chaseDamageThreshold = nil,
    footCombat = false,
    reminderIndex = 1,
    returnPhase = nil,
    returnLineIndex = 0,
    returnSceneSerial = 0,
    returnScene = nil,
    vehicleFailure = nil,
    hoodFailureSerial = 0,
    hoodFailure = nil,
    pursuitRouteTask = nil,
    pursuitRoutePollTimer = nil,
    pursuitRouteActivated = false,
    pursuitRouteIndex = nil,
}

local function nativeTaskRuntimeRunning()
    local runtime = getResourceFromName("native-task-runtime")
    return runtime and getResourceState(runtime) == "running"
end

local function cancelPursuitRoute(reason, retainOwnership)
    local task = mission.pursuitRouteTask
    if isTimer(mission.pursuitRoutePollTimer) then
        killTimer(mission.pursuitRoutePollTimer)
    end
    mission.pursuitRouteTask = nil
    mission.pursuitRoutePollTimer = nil
    mission.pursuitRouteActivated = false
    mission.pursuitRouteIndex = nil
    if isElement(task) and nativeTaskRuntimeRunning() then
        local cancelled = exports["native-task-runtime"]:cancelNativeDriveRoute(task)
        outputDebugString(("[drive-thru] Native route handle cancelled=%s reason=%s"):format(tostring(cancelled),
                                                                                              tostring(reason or "transition")))
    end
    if retainOwnership and isElement(mission.leader) then
        if isElement(mission.entities.ballas_driver) then
            setElementSyncer(mission.entities.ballas_driver, mission.leader, true)
        end
        if isElement(mission.entities.voodoo) then
            setElementSyncer(mission.entities.voodoo, mission.leader, true)
        end
    end
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
    cancelPursuitRoute("entity_cleanup", false)
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
    mission.chaseDialoguePhase = nil
    mission.chaseLineIndex = 0
    mission.chaseDamageThreshold = nil
    mission.footCombat = false
    mission.reminderIndex = 1
    mission.returnPhase = nil
    mission.returnLineIndex = 0
    mission.returnScene = nil
    mission.vehicleFailure = nil
    mission.hoodFailure = nil
    mission.pursuitRouteTask = nil
    mission.pursuitRoutePollTimer = nil
    mission.pursuitRouteActivated = false
    mission.pursuitRouteIndex = nil
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
    cancelPursuitRoute("mission_failed", false)
    if mission.hoodFailure and isElement(mission.hoodFailure.frozenElement) then
        setElementFrozen(mission.hoodFailure.frozenElement, false)
        mission.hoodFailure.frozenElement = nil
    end
    outputDebugString("[drive-thru] Mission failed: " .. tostring(reason), 1)
    if isElement(mission.leader) then
        triggerClientEvent(mission.leader, "drivethru:failed", resourceRoot, textKey, reason)
    end
    rememberTimer(setTimer(function()
        cleanupMission("failed", true)
    end, 5000, 1))
end

local greenwoodFailureStages = {
    actor_entry = {},
    enter_car = {healthText = "SWE2_KC"},
    drive = {healthText = "SWE2_KC"},
    chase = {healthText = "SWE2_KA"},
    return_grove_drive = {healthText = "SWE2_KB"},
    return_smoke_drive = {healthText = "SWE2_KB"},
}

local actorFailureStages = {
    enter_car = {sweet = true, ryder = true, smoke = true},
    drive = {sweet = true, ryder = true, smoke = true},
    chase = {sweet = true, ryder = true, smoke = true},
    return_grove_drive = {sweet = true, ryder = true, smoke = true},
    return_smoke_drive = {smoke = true},
}

local actorFailureText = {sweet = "SWE3_E", ryder = "SWE3_F", smoke = "SWE3_G"}

local beginGreenwoodFailure
local beginHoodFailure
local observePursuitRouteState

local function startMissionWatchdog()
    rememberTimer(setTimer(function()
        if not mission.running or mission.finishing then
            return
        end
        local vehiclePolicy = greenwoodFailureStages[mission.stage]
        local vehicle = mission.entities.vehicle
        if vehiclePolicy and not isElement(vehicle) then
            return failMission("The Greenwood element disappeared during an SCM-monitored stage", "SWE3_D")
        end
        if vehiclePolicy and vehiclePolicy.healthText and isElement(vehicle) and getElementHealth(vehicle) <= 250 then
            return beginGreenwoodFailure(vehiclePolicy)
        end
        local actorPolicy = actorFailureStages[mission.stage]
        for actor in pairs(actorPolicy or {}) do
            if not isElement(mission.entities[actor]) then
                return failMission(actor .. " element disappeared during an SCM-monitored stage", actorFailureText[actor])
            end
        end
    end, 250, 0))
end

local function setStoryVehicleState(vehicle, role, profile)
    setElementDimension(vehicle, DRIVETHRU.dimension)
    setVehicleColor(vehicle, profile.primaryColor[1], profile.primaryColor[2], profile.primaryColor[3], profile.secondaryColor[1],
                    profile.secondaryColor[2], profile.secondaryColor[3])
    if profile.plate then
        setVehiclePlateText(vehicle, profile.plate)
    end
    setVehicleDamageProof(vehicle, false)
    setVehicleLocked(vehicle, false)
    setVehicleEngineState(vehicle, false)
    setElementData(vehicle, DRIVETHRU.vehicleRoleData, role, true)
    setElementSyncer(vehicle, mission.leader, true, true)
end

local createMissionActor

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
    setElementData(vehicle, DRIVETHRU.vehicleRoleData, "greenwood_intro", true)
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

local function createRestaurantVehicle(role, profile)
    local position = profile.position
    local vehicle = createVehicle(profile.model, position.x, position.y, position.z, 0, 0, position.heading)
    if not vehicle then
        return false
    end
    setStoryVehicleState(vehicle, role, profile)
    mission.entities[role] = vehicle
    if role == "greenwood" then
        setElementData(vehicle, DRIVETHRU.vehicleData, true, true)
        mission.entities.vehicle = vehicle
    end
    return vehicle
end

local function createRestaurantProtagonist(name, profile, vehicle)
    local vehiclePosition = DRIVETHRU.restaurant.greenwood.position
    local pedProfile = {
        model = profile.model,
        position = {x = vehiclePosition.x, y = vehiclePosition.y, scriptZ = vehiclePosition.scriptZ, heading = profile.position.heading},
        walkingStyle = profile.walkingStyle,
    }
    if not createMissionActor(name, pedProfile) then
        return false
    end
    setElementData(mission.entities[name], DRIVETHRU.actorRoleData, "protagonist", true)
    return warpPedIntoVehicle(mission.entities[name], vehicle, DRIVETHRU.restaurant.passengerSeats[name])
end

local function createRestaurantBallasDriver(vehicle)
    local profile = DRIVETHRU.restaurant.ballasDriver
    local position = DRIVETHRU.restaurant.voodoo.position
    local ped = createPed(profile.model, position.x, position.y, position.z + 1.0, position.heading)
    if not ped then
        return false
    end
    setElementDimension(ped, DRIVETHRU.dimension)
    giveWeapon(ped, DRIVETHRU.weapon.id, DRIVETHRU.weapon.ammo, true)
    setElementData(ped, DRIVETHRU.missionActorData, true, true)
    setElementData(ped, DRIVETHRU.actorRoleData, "ballas_driver", true)
    setElementData(ped, "drivethru.actor", "ballas_driver", true)
    setElementSyncer(ped, mission.leader, true, true)
    mission.entities.ballas_driver = ped
    return warpPedIntoVehicle(ped, vehicle, profile.seat)
end

local function createChaseBallasPassenger(vehicle)
    local profile = DRIVETHRU.chase.ballasPassenger
    local position = DRIVETHRU.restaurant.voodoo.position
    local ped = createPed(profile.model, position.x, position.y, position.z + 1.0, position.heading)
    if not ped then
        return false
    end
    setElementDimension(ped, DRIVETHRU.dimension)
    giveWeapon(ped, DRIVETHRU.weapon.id, DRIVETHRU.weapon.ammo, true)
    setElementData(ped, DRIVETHRU.missionActorData, true, true)
    setElementData(ped, DRIVETHRU.actorRoleData, "ballas_passenger", true)
    setElementData(ped, "drivethru.actor", "ballas_passenger", true)
    setElementSyncer(ped, mission.leader, true, true)
    mission.entities.ballas_passenger = ped
    return warpPedIntoVehicle(ped, vehicle, profile.seat)
end

local function createChaseSupportActor(name, profile)
    local position = profile.position
    local ped = createPed(profile.model, position.x, position.y, position.scriptZ + 1.0, position.heading)
    if not ped then
        return false
    end
    setElementDimension(ped, DRIVETHRU.dimension)
    setElementHealth(ped, profile.health)
    giveWeapon(ped, DRIVETHRU.weapon.id, DRIVETHRU.weapon.ammo, true)
    setElementData(ped, DRIVETHRU.missionActorData, true, true)
    setElementData(ped, DRIVETHRU.actorRoleData, "grove_support", true)
    setElementData(ped, "drivethru.actor", name, true)
    setElementSyncer(ped, mission.leader, true, true)
    mission.entities[name] = ped
    return true
end

local function createChaseActors()
    if not createChaseBallasPassenger(mission.entities.voodoo) then
        return false, "Ballas passenger"
    end
    for _, name in ipairs({"mate1", "mate2"}) do
        if not createChaseSupportActor(name, DRIVETHRU.chase.support[name]) then
            return false, name
        end
    end
    return true
end

local function rebuildRestaurantWorld()
    if not mission.running or mission.stage ~= "restaurant_rebuild" then
        return
    end
    local profile = DRIVETHRU.restaurant
    local greenwood = createRestaurantVehicle("greenwood", profile.greenwood)
    if not greenwood then
        return failMission("Restaurant Greenwood could not be reconstructed")
    end
    for _, name in ipairs({"smoke", "sweet", "ryder"}) do
        if not createRestaurantProtagonist(name, DRIVETHRU.actors[name], greenwood) then
            return failMission("Restaurant protagonist reconstruction failed: " .. name)
        end
    end
    if not warpPedIntoVehicle(mission.leader, greenwood, 0) then
        return failMission("CJ could not be warped into the reconstructed Greenwood")
    end
    local voodoo = createRestaurantVehicle("voodoo", profile.voodoo)
    if not voodoo then
        return failMission("Pursuit Voodoo could not be reconstructed")
    end
    setElementHealth(voodoo, profile.voodoo.health)
    if not createRestaurantBallasDriver(voodoo) then
        return failMission("Ballas pursuit driver could not be reconstructed")
    end
    mission.stage = "restaurant_barrier"
    triggerClientEvent(mission.leader, "drivethru:restaurantRebuilt", resourceRoot, mission.entities)
    outputDebugString("[drive-thru] Restaurant reconstruction created; waiting for native policy and streaming barrier")
end

createMissionActor = function(name, profile)
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

local function interruptDialogueForReminder()
    local audio = mission.audio
    if not audio then
        return
    end
    if audio.purpose == "drive" then
        mission.driveLineIndex = audio.index
    elseif audio.purpose == "chase" then
        mission.chaseLineIndex = audio.index
    elseif audio.purpose == "return_drive" then
        mission.returnLineIndex = audio.index
    end
    -- SWEET3 clears conversation channel 1 before loading the channel 2
    -- reminder. Treat its interrupted line as consumed so re-entry continues
    -- at the same next index as the native finished-channel observation.
    mission.audio = nil
end

local function finishGreenwoodFailureWhenReady()
    local failure = mission.vehicleFailure
    if not failure or mission.stage ~= "greenwood_failure" or not failure.audioFinished or not failure.tasksReady then
        return
    end
    failMission("The Greenwood health reached the vanilla 250 threshold after the crew bailed out", "SWE3_D")
end

beginGreenwoodFailure = function(vehiclePolicy)
    if not mission.running or mission.finishing or mission.vehicleFailure or type(vehiclePolicy) ~= "table" then
        return
    end
    local profile = DRIVETHRU.audio.vehicleFailure[vehiclePolicy.healthText]
    if not profile then
        return failMission("The Greenwood failure audio profile is missing", "SWE3_D")
    end
    local originalStage = mission.stage
    local actors = originalStage == "return_smoke_drive" and {"smoke"} or {"sweet", "ryder", "smoke"}
    interruptDialogueForReminder()
    mission.stage = "greenwood_failure"
    mission.vehicleFailure = {
        originalStage = originalStage,
        actors = actors,
        profile = profile,
        tasksReady = false,
        audioFinished = false,
    }
    if not queueAudio(profile, "vehicle_failure") then
        mission.vehicleFailure = nil
        return failMission("The Greenwood failure warning could not be queued", "SWE3_D")
    end
    outputDebugString(("[drive-thru] VANILLA GREENWOOD FAILURE: stage=%s health=%.1f warning=%s actors=%s"):format(
                          originalStage, getElementHealth(mission.entities.vehicle), profile.key, table.concat(actors, ",")))
end

beginHoodFailure = function()
    if not mission.running or mission.finishing or mission.stage ~= "chase" or mission.hoodFailure then
        return
    end
    interruptDialogueForReminder()
    mission.hoodFailureSerial = mission.hoodFailureSerial + 1
    mission.stage = "hood_failure_prepare"
    mission.hoodFailure = {
        id = mission.hoodFailureSerial,
        mate1Dead = false,
        mate2Dead = false,
        routeHandoff = false,
        frozenElement = getPedOccupiedVehicle(mission.leader),
    }
    if not isElement(mission.hoodFailure.frozenElement) then
        mission.hoodFailure.frozenElement = mission.leader
    end
    setElementFrozen(mission.hoodFailure.frozenElement, true)
    triggerClientEvent(mission.leader, "drivethru:hoodFailurePrepare", resourceRoot, mission.hoodFailure.id, mission.entities)
    outputDebugString("[drive-thru] VANILLA HOOD FAILURE: the Voodoo reached Grove with both Ballas alive")
    rememberTimer(setTimer(function(expectedId)
        local failure = mission.hoodFailure
        if mission.running and not mission.finishing and failure and failure.id == expectedId then
            failMission("The Grove Street failure scene exceeded its native-task guard", "TW2_Y")
        end
    end, DRIVETHRU.chase.hoodFailure.guardTimeout, 1, mission.hoodFailure.id))
end

local function startHoodFailureEscapeWhenReady()
    local failure = mission.hoodFailure
    if not failure or failure.escapeStarted or not failure.mate1Dead or not failure.mate2Dead then
        return
    end
    failure.escapeStarted = true
    mission.stage = "hood_failure_escape"
    triggerClientEvent(mission.leader, "drivethru:hoodFailureEscape", resourceRoot, failure.id, mission.entities)
    outputDebugString("[drive-thru] Both scripted Grove deaths observed; the Voodoo begins its 30.0 escape task")
end

local function observeHoodFailureDeath(name, proof)
    local failure = mission.hoodFailure
    local flag = name == "mate1" and "mate1Dead" or name == "mate2" and "mate2Dead" or nil
    if not failure or not flag or failure[flag] then
        return false
    end
    failure[flag] = true
    outputDebugString(("[drive-thru] Native Grove death observed: %s (%s)"):format(name, tostring(proof or "server wasted event")))
    startHoodFailureEscapeWhenReady()
    return true
end

local function queueReturnToCarReminder()
    local profiles = mission.stage == "return_smoke_drive" and DRIVETHRU.audio.returnToCar.smoke or DRIVETHRU.audio.returnToCar.crew
    if type(profiles) ~= "table" or #profiles == 0 then
        return false
    end
    interruptDialogueForReminder()
    local index = (mission.reminderIndex - 1) % #profiles + 1
    mission.reminderIndex = index % #profiles + 1
    local queued = queueAudio(profiles[index], "vehicle_reminder", index)
    if queued then
        outputDebugString(('[drive-thru] Return-to-car reminder queued: stage=%s index=%d key=%s event=%d'):format(
                              mission.stage, index, profiles[index].key, profiles[index].event))
    end
    return queued
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

local function finishChaseDialoguePhase()
    local vehicle = mission.entities.vehicle
    if mission.chaseDialoguePhase == "chase" then
        mission.chaseDialoguePhase = "await_first_damage"
        mission.chaseDamageThreshold = isElement(vehicle) and getElementHealth(vehicle) - 60 or nil
    elseif mission.chaseDialoguePhase == "chaseDamageFirst" then
        mission.chaseDialoguePhase = "await_second_damage"
        mission.chaseDamageThreshold = isElement(vehicle) and getElementHealth(vehicle) - 60 or nil
    elseif mission.chaseDialoguePhase == "chaseDamageSecond" then
        mission.chaseDialoguePhase = "done"
        mission.chaseDamageThreshold = nil
    end
end

local function queueNextChaseLine()
    if not mission.running or mission.stage ~= "chase" or mission.audio or not mission.leaderInVehicle then
        return
    end
    local profiles = DRIVETHRU.audio[mission.chaseDialoguePhase]
    if type(profiles) ~= "table" then
        return
    end
    local nextIndex = mission.chaseLineIndex + 1
    local profile = profiles[nextIndex]
    if profile then
        queueAudio(profile, "chase", nextIndex)
    else
        finishChaseDialoguePhase()
    end
end

local function startChaseDialoguePhase(phase)
    mission.chaseDialoguePhase = phase
    mission.chaseLineIndex = 0
    mission.chaseDamageThreshold = nil
    queueNextChaseLine()
end

local beginFootCombat
local completeChase
local startReturnDrive

local function monitorChase()
    if not mission.running or mission.finishing or mission.stage ~= "chase" then
        return
    end
    local driver = mission.entities.ballas_driver
    local passenger = mission.entities.ballas_passenger
    local driverDead = not isElement(driver) or isPedDead(driver)
    local passengerDead = not isElement(passenger) or isPedDead(passenger)
    if driverDead and passengerDead then
        return completeChase()
    end
    if driverDead or passengerDead then
        beginFootCombat("one Ballas died during the vehicle chase")
    end

    for _, name in ipairs({"mate1", "mate2"}) do
        local ped = mission.entities[name]
        if not isElement(ped) or isPedDead(ped) then
            return failMission("The Grove support actors were killed", "TW2_Y")
        end
    end

    local voodoo = mission.entities.voodoo
    if not isElement(voodoo) then
        return failMission("The Voodoo element disappeared")
    end
    if getElementHealth(voodoo) <= 250 then
        beginFootCombat("Voodoo reached the vanilla 250 health threshold")
    elseif not mission.footCombat then
        local x, y, z = getElementPosition(voodoo)
        local hub = DRIVETHRU.chase.hub
        if math.abs(x - hub.x) <= hub.radiusX and math.abs(y - hub.y) <= hub.radiusY and math.abs(z - hub.z) <= hub.radiusZ then
            return beginHoodFailure()
        end
    end

    if not mission.audio and mission.leaderInVehicle and isElement(mission.entities.vehicle) and mission.chaseDamageThreshold then
        local health = getElementHealth(mission.entities.vehicle)
        if health <= mission.chaseDamageThreshold then
            if mission.chaseDialoguePhase == "await_first_damage" then
                startChaseDialoguePhase("chaseDamageFirst")
            elseif mission.chaseDialoguePhase == "await_second_damage" then
                startChaseDialoguePhase("chaseDamageSecond")
            end
        end
    end
end

beginFootCombat = function(reason)
    if not mission.running or mission.finishing or mission.stage ~= "chase" or mission.footCombat then
        return
    end
    cancelPursuitRoute("foot_combat_handoff", true)
    mission.footCombat = true
    outputDebugString("[drive-thru] Vehicle-to-foot combat transition: " .. tostring(reason))
    triggerClientEvent(mission.leader, "drivethru:footCombat", resourceRoot, mission.entities, reason)
end

completeChase = function()
    if not mission.running or mission.finishing or mission.stage ~= "chase" then
        return
    end
    cancelPursuitRoute("chase_complete", true)
    mission.audio = nil
    outputDebugString(("[drive-thru] CHECKPOINT PASSED: both Ballas dead after native route and three drive-bys; footCombat=%s"):format(
                          tostring(mission.footCombat)))
    triggerClientEvent(mission.leader, "drivethru:chaseCheckpoint", resourceRoot, mission.entities)
    startReturnDrive("grove")
end

local function beginChase()
    if not mission.running or mission.stage ~= "pursuit_task_barrier" then
        return
    end
    mission.stage = "chase"
    mission.leaderInVehicle = getPedOccupiedVehicle(mission.leader) == mission.entities.vehicle and
                                  getPedOccupiedVehicleSeat(mission.leader) == 0
    mission.footCombat = false
    setElementFrozen(mission.leader, false)
    setVehicleEngineState(mission.entities.vehicle, true)
    setVehicleEngineState(mission.entities.voodoo, true)
    triggerClientEvent(mission.leader, "drivethru:pursuitStarted", resourceRoot, mission.entities)
    outputDebugString("[drive-thru] Native pursuit active; three drive-by tasks observed before fade-in")
    rememberTimer(setTimer(function()
        if mission.running and mission.stage == "chase" then
            startChaseDialoguePhase("chase")
        end
    end, 2000, 1))
    rememberTimer(setTimer(function()
        if mission.running and mission.stage == "chase" and isElement(mission.leader) then
            triggerClientEvent(mission.leader, "drivethru:chaseHelp", resourceRoot)
        end
    end, DRIVETHRU.chase.helpDelay, 1))
    rememberTimer(setTimer(monitorChase, DRIVETHRU.chase.monitorInterval, 0))
end

local function getReturnAudioProfiles(phase, scene)
    if phase == "grove" then
        return scene and DRIVETHRU.audio.returnGroveScene or DRIVETHRU.audio.returnGroveDrive
    end
    return scene and DRIVETHRU.audio.returnSmokeScene or DRIVETHRU.audio.returnSmokeDrive
end

local function queueNextReturnDriveLine()
    local phase = mission.returnPhase
    local profile = phase and DRIVETHRU.returnTrip[phase]
    if not mission.running or mission.finishing or not profile or mission.stage ~= profile.stage or mission.audio or not mission.leaderInVehicle then
        return
    end
    local nextIndex = mission.returnLineIndex + 1
    local line = getReturnAudioProfiles(phase, false)[nextIndex]
    if line then
        queueAudio(line, "return_drive", nextIndex)
    end
end

local requestReturnSceneRelease

local function startReturnSceneDepartures(scene)
    if mission.returnScene ~= scene or scene.releasing or scene.departuresStartedAt then
        return
    end
    scene.departuresStartedAt = getTickCount()
    scene.departuresReady = false
    triggerClientEvent(mission.leader, "drivethru:returnSceneDepartures", resourceRoot, scene.id, scene.phase, mission.entities)
    local lastDelay = 0
    for _, departure in ipairs(DRIVETHRU.returnTrip[scene.phase].scene.departures) do
        lastDelay = math.max(lastDelay, departure.delay)
    end
    local timelineDuration = lastDelay + DRIVETHRU.returnTrip[scene.phase].scene.postDepartureDelay
    rememberTimer(setTimer(function(expectedId)
        local active = mission.returnScene
        if not active or active.id ~= expectedId or active.releasing then
            return
        end
        if not active.departuresReady then
            return failMission("Return-scene native departure tasks were not active before the SCM wait elapsed")
        end
        requestReturnSceneRelease(active, false)
    end, timelineDuration, 1, scene.id))
end

local function queueNextReturnSceneLine(scene)
    if mission.returnScene ~= scene or scene.releasing or mission.audio or
        mission.stage ~= "return_" .. scene.phase .. "_scene" then
        return
    end
    local nextIndex = scene.lineIndex + 1
    local line = getReturnAudioProfiles(scene.phase, true)[nextIndex]
    if line then
        queueAudio(line, "return_scene", nextIndex)
    else
        startReturnSceneDepartures(scene)
    end
end

local function beginReturnSceneTimeline(scene)
    if mission.returnScene ~= scene or scene.releasing then
        return
    end
    mission.stage = "return_" .. scene.phase .. "_scene"
    local sceneProfile = DRIVETHRU.returnTrip[scene.phase].scene
    local function beginAudio()
        if mission.returnScene ~= scene or scene.releasing then
            return
        end
        if sceneProfile.lookAt and not sceneProfile.lookAt.afterLine then
            triggerClientEvent(mission.leader, "drivethru:returnSceneLookAt", resourceRoot, scene.id, sceneProfile.lookAt.actor,
                               sceneProfile.lookAt.duration)
        end
        if sceneProfile.skippableFromStart then
            scene.skippable = true
            triggerClientEvent(mission.leader, "drivethru:returnSceneSkippable", resourceRoot, scene.id)
        end
        queueNextReturnSceneLine(scene)
    end
    if (sceneProfile.cameraLead or 0) > 0 then
        rememberTimer(setTimer(beginAudio, sceneProfile.cameraLead, 1))
    else
        beginAudio()
    end
end

local function startReturnScene(phase)
    if not mission.running or mission.finishing or mission.returnPhase ~= phase then
        return
    end
    mission.audio = nil
    mission.returnSceneSerial = mission.returnSceneSerial + 1
    local scene = {
        id = mission.returnSceneSerial,
        phase = phase,
        lineIndex = 0,
        skippable = false,
        releasing = false,
    }
    mission.returnScene = scene
    mission.stage = "return_" .. phase .. "_scene_prepare"
    triggerClientEvent(mission.leader, "drivethru:returnScenePrepare", resourceRoot, scene.id, phase, mission.entities)
    rememberTimer(setTimer(function(expectedId)
        local active = mission.returnScene
        if active and active.id == expectedId and not active.ready then
            failMission("Return-scene camera preparation timed out: " .. active.phase)
        end
    end, DRIVETHRU.returnTrip.sceneGuardTimeout, 1, scene.id))
end

startReturnDrive = function(phase)
    if not mission.running or mission.finishing or not DRIVETHRU.returnTrip[phase] then
        return
    end
    mission.returnScene = nil
    mission.returnPhase = phase
    mission.returnLineIndex = 0
    mission.stage = DRIVETHRU.returnTrip[phase].stage
    mission.leaderInVehicle = isElement(mission.leader) and getPedOccupiedVehicle(mission.leader) == mission.entities.vehicle and
                                  getPedOccupiedVehicleSeat(mission.leader) == 0
    triggerClientEvent(mission.leader, "drivethru:returnDriveStarted", resourceRoot, phase, mission.entities)
    outputDebugString(("[drive-thru] Return drive started: %s; waiting for the exact grounded arrival gate"):format(phase))
    rememberTimer(setTimer(queueNextReturnDriveLine, DRIVETHRU.returnTrip.dialogueDelay, 1))
end

requestReturnSceneRelease = function(scene, skipped)
    if mission.returnScene ~= scene or scene.releasing then
        return
    end
    scene.releasing = true
    scene.skipped = skipped == true
    mission.audio = nil
    mission.stage = "return_" .. scene.phase .. "_scene_release"
    triggerClientEvent(mission.leader, "drivethru:returnSceneRelease", resourceRoot, scene.id, scene.skipped)
    rememberTimer(setTimer(function(expectedId)
        local active = mission.returnScene
        if active and active.id == expectedId then
            failMission("Return-scene camera release timed out: " .. active.phase)
        end
    end, DRIVETHRU.returnTrip.sceneGuardTimeout, 1, scene.id))
end

local function finishReturnMission()
    if not mission.running or mission.finishing then
        return
    end
    mission.finishing = true
    mission.stage = "complete"
    givePlayerMoney(mission.leader, DRIVETHRU.returnTrip.reward)
    triggerClientEvent(mission.leader, "drivethru:passed", resourceRoot, DRIVETHRU.returnTrip.reward, DRIVETHRU.returnTrip.tune)
    outputDebugString(("[drive-thru] MISSION PASSED: return scenes complete; visible reward=$%d tune=%d"):format(
                          DRIVETHRU.returnTrip.reward, DRIVETHRU.returnTrip.tune))
    rememberTimer(setTimer(function()
        cleanupMission("complete", true)
    end, DRIVETHRU.returnTrip.completionDisplayDuration, 1))
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

local function startFileCutscene(purpose)
    local name = DRIVETHRU.cutscenes[purpose]
    mission.cutsceneSerial = mission.cutsceneSerial + 1
    mission.cutscene = {id = mission.cutsceneSerial, name = name, purpose = purpose, ready = false, started = false, finished = false}
    mission.stage = "cutscene"
    triggerClientEvent(mission.leader, "drivethru:cutscenePrepare", resourceRoot, mission.cutscene.id, name, purpose == "intro")
    rememberTimer(setTimer(function(expectedId)
        if mission.cutscene and mission.cutscene.id == expectedId and not mission.cutscene.ready then
            failMission(name .. " loading timed out")
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
    triggerClientEvent(player, "drivethru:start", resourceRoot)
    outputDebugString(("[drive-thru] Starting SWEET2A for leader %s in dimension %d"):format(getPlayerName(player), DRIVETHRU.dimension))
    startMissionWatchdog()
    startFileCutscene("intro")
end

addEvent("drivethru:cutsceneReady", true)
addEventHandler("drivethru:cutsceneReady", resourceRoot, function(sceneId, result, details)
    local scene = mission.cutscene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or scene.ready then
        return
    end
    if result ~= "ready" then
        return failMission(scene.name .. " preparation failed: " .. tostring(result) .. " " .. tostring(details or ""))
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
        return failMission(scene.name .. " playback failed: " .. tostring(result))
    end
    scene.finished = true
    outputDebugString(("[drive-thru] %s finished skipped=%s elapsed=%s ms"):format(scene.name, tostring(skipped == true),
                                                                                   tostring(elapsed or "?")))
    triggerClientEvent(mission.leader, "drivethru:cutsceneRelease", resourceRoot, scene.id)
    rememberTimer(setTimer(function(expectedId)
        if mission.cutscene and mission.cutscene.id == expectedId then
            failMission(scene.name .. " cleanup timed out")
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
        return failMission(scene.name .. " native state was not released")
    end
    local purpose = scene.purpose
    mission.cutscene = nil
    if purpose == "intro" then
        beginWorldAfterCutscene()
    else
        mission.stage = "restaurant_rebuild"
        rebuildRestaurantWorld()
    end
end)

addEvent("drivethru:restaurantCameraReady", true)
addEventHandler("drivethru:restaurantCameraReady", resourceRoot, function(result, details)
    if source ~= resourceRoot or client ~= mission.leader or mission.stage ~= "restaurant_camera" then
        return
    end
    if result ~= "ready" then
        return failMission("Restaurant camera transition failed: " .. tostring(result) .. " " .. tostring(details or ""))
    end
    mission.stage = "restaurant_teardown"
    local leader = mission.leader
    local staging = DRIVETHRU.restaurant.cjStaging
    removePedFromVehicle(leader)
    setElementPosition(leader, staging.x, staging.y, staging.scriptZ + 1.0)
    setElementFrozen(leader, true)
    destroyMissionEntities()
    triggerClientEvent(leader, "drivethru:restaurantCameraRelease", resourceRoot)
    outputDebugString("[drive-thru] Vanilla restaurant teardown complete under native black fade")
end)

addEvent("drivethru:restaurantCameraReleased", true)
addEventHandler("drivethru:restaurantCameraReleased", resourceRoot, function(result, details)
    if source ~= resourceRoot or client ~= mission.leader or mission.stage ~= "restaurant_teardown" then
        return
    end
    if result ~= "released" then
        return failMission("Restaurant camera release failed: " .. tostring(result) .. " " .. tostring(details or ""))
    end
    startFileCutscene("restaurant")
end)

addEvent("drivethru:restaurantRebuildReady", true)
addEventHandler("drivethru:restaurantRebuildReady", resourceRoot, function(result, details)
    if source ~= resourceRoot or client ~= mission.leader or mission.stage ~= "restaurant_barrier" then
        return
    end
    if result ~= "ready" then
        return failMission("Restaurant reconstruction barrier failed: " .. tostring(result) .. " " .. tostring(details or ""))
    end
    setVehicleEngineState(mission.entities.vehicle, true)
    setVehicleEngineState(mission.entities.voodoo, true)
    -- Entity creation compresses vehicle health to 12 bits and therefore
    -- initially exposes 2047.5. Once the leader confirms the streamed native
    -- vehicle, this ordinary health RPC carries the full SWEET3 value.
    setElementHealth(mission.entities.voodoo, DRIVETHRU.restaurant.voodoo.health)
    mission.stage = "pursuit_route_barrier"
    mission.pursuitRouteActivated = false
    mission.pursuitRouteIndex = nil
    if not nativeTaskRuntimeRunning() then
        return failMission("The native task runtime is not running")
    end

    local route = {}
    for index, point in ipairs(DRIVETHRU.chase.route) do
        route[index] = {
            x = point.x,
            y = point.y,
            z = point.z,
            speed = point.speed,
            mode = DRIVETHRU.chase.drivingMode,
            vehicleModel = DRIVETHRU.chase.vehicleModel,
            drivingStyle = DRIVETHRU.chase.drivingStyle,
        }
    end

    triggerClientEvent(mission.leader, "drivethru:pursuitRoute", resourceRoot, mission.entities)
    local handle, routeError = exports["native-task-runtime"]:createNativeDriveRoute(
                                    mission.entities.ballas_driver, mission.entities.voodoo, route, mission.leader,
                                    {loadCollision = false, validZMin = 8, validZMax = 20, fallbackOwners = {mission.leader}})
    if not handle then
        return failMission("Ballas managed route creation failed: " .. tostring(routeError))
    end
    mission.pursuitRouteTask = handle
    -- Cross-resource events are an acceleration path, not the only source of
    -- truth. Polling the owned handle also catches a state transition emitted
    -- before this mission has associated the fresh custom element locally.
    mission.pursuitRoutePollTimer = rememberTimer(setTimer(function(expectedHandle)
        if not mission.running or mission.finishing or mission.pursuitRouteTask ~= expectedHandle or
            not isElement(expectedHandle) or not nativeTaskRuntimeRunning() then
            return
        end
        local routeState = exports["native-task-runtime"]:getNativeDriveRouteState(expectedHandle)
        if routeState then
            observePursuitRouteState(expectedHandle, routeState.state, routeState)
        end
    end, 250, 0, handle))
    outputDebugString("[drive-thru] SWEET2B reconstruction passed; Voodoo health 2700, 0587 FALSE and managed route requested")
    rememberTimer(setTimer(function(expectedHandle)
        if mission.running and mission.stage == "pursuit_route_barrier" and mission.pursuitRouteTask == expectedHandle and
            not mission.pursuitRouteActivated then
            failMission("The managed Ballas route did not become active before the mission guard")
        end
    end, DRIVETHRU.chase.routeActivationTimeout, 1, handle))
end)

observePursuitRouteState = function(handle, routeState, data)
    if handle ~= mission.pursuitRouteTask or type(data) ~= "table" or not mission.running or mission.finishing then
        return
    end

    if routeState == "active" then
        if data.routeIndex ~= mission.pursuitRouteIndex then
            outputDebugString(("[drive-thru] Managed route epoch=%d logical index=%d state=%s"):format(
                                  data.epoch, data.routeIndex, mission.stage))
        end
        mission.pursuitRouteIndex = data.routeIndex
        if mission.stage ~= "pursuit_route_barrier" or mission.pursuitRouteActivated then
            return
        end
        local driver, voodoo = mission.entities.ballas_driver, mission.entities.voodoo
        if data.owner ~= mission.leader or not isElement(driver) or not isElement(voodoo) or
            getElementSyncer(driver) ~= mission.leader or getElementSyncer(voodoo) ~= mission.leader or
            getPedOccupiedVehicle(driver) ~= voodoo or getPedOccupiedVehicleSeat(driver) ~= 0 then
            return failMission("Managed Ballas route became active without authoritative ownership and seat state")
        end
        mission.pursuitRouteActivated = true
        local created, failedActor = createChaseActors()
        if not created then
            return failMission("Pursuit actor creation failed: " .. tostring(failedActor))
        end
        setElementHealth(mission.entities.vehicle, DRIVETHRU.chase.greenwoodHealth)
        mission.stage = "pursuit_task_barrier"
        outputDebugString(("[drive-thru] Managed route active epoch=%d index=%d; pursuit actors created in SCM order"):format(
                              data.epoch, data.routeIndex))
        triggerClientEvent(mission.leader, "drivethru:pursuitActorsCreated", resourceRoot, mission.entities)
    elseif routeState == "failed" or routeState == "orphaned" then
        if mission.stage == "pursuit_route_barrier" or mission.stage == "pursuit_task_barrier" or mission.stage == "chase" then
            failMission("Managed Ballas route entered " .. routeState .. ": " .. tostring(data.reason or "no reason"))
        end
    elseif routeState == "completed" then
        if mission.stage == "chase" and not mission.footCombat then
            beginHoodFailure()
        elseif mission.stage == "pursuit_route_barrier" or mission.stage == "pursuit_task_barrier" then
            failMission("Managed Ballas route completed before the pursuit became active")
        end
    end
end

addEventHandler("onNativeDriveRouteStateChange", root, function(routeState, data)
    observePursuitRouteState(source, routeState, data)
end)

addEvent("drivethru:pursuitTasksReady", true)
addEventHandler("drivethru:pursuitTasksReady", resourceRoot, function(result, details)
    if source ~= resourceRoot or client ~= mission.leader or mission.stage ~= "pursuit_task_barrier" then
        return
    end
    if result ~= "active" then
        return failMission("Pursuit task assignment failed: " .. tostring(result) .. " " .. tostring(details or ""))
    end
    local expected = {
        ballas_passenger = {vehicle = mission.entities.voodoo, seat = DRIVETHRU.chase.ballasPassenger.seat},
        ryder = {vehicle = mission.entities.vehicle, seat = DRIVETHRU.restaurant.passengerSeats.ryder},
        sweet = {vehicle = mission.entities.vehicle, seat = DRIVETHRU.restaurant.passengerSeats.sweet},
    }
    for name, profile in pairs(expected) do
        local ped = mission.entities[name]
        if not isElement(ped) or getElementSyncer(ped) ~= client or getPedOccupiedVehicle(ped) ~= profile.vehicle or
            getPedOccupiedVehicleSeat(ped) ~= profile.seat then
            return failMission("Pursuit task report failed authoritative validation for " .. name)
        end
    end
    beginChase()
end)

addEvent("drivethru:footCombatReady", true)
addEventHandler("drivethru:footCombatReady", resourceRoot, function(result, details)
    if source ~= resourceRoot or client ~= mission.leader or mission.stage ~= "chase" or not mission.footCombat then
        return
    end
    if result ~= "active" then
        return failMission("Vehicle-to-foot combat task assignment failed: " .. tostring(result) .. " " .. tostring(details or ""))
    end
    outputDebugString("[drive-thru] Surviving Ballas and Grove shooters accepted the native on-foot retargeting")
end)

addEvent("drivethru:supportChatReady", true)
addEventHandler("drivethru:supportChatReady", resourceRoot, function(result)
    if source ~= resourceRoot or client ~= mission.leader or not mission.running or
        (mission.stage ~= "pursuit_task_barrier" and mission.stage ~= "chase") then
        return
    end
    if result ~= "accepted" then
        return failMission("Grove support chat assignment failed: " .. tostring(result))
    end
    outputDebugString("[drive-thru] Grove support chat tasks accepted after both distant actors streamed in")
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
    if audio.purpose == "vehicle_failure" then
        local failure = mission.vehicleFailure
        if not failure or mission.stage ~= "greenwood_failure" then
            return failMission("The Greenwood warning loaded without an active failure state", "SWE3_D")
        end
        triggerClientEvent(mission.leader, "drivethru:greenwoodFailureTasks", resourceRoot, failure.actors, mission.entities)
    end
end)

addEvent("drivethru:greenwoodFailureTasksReady", true)
addEventHandler("drivethru:greenwoodFailureTasksReady", resourceRoot, function(result, details)
    local failure = mission.vehicleFailure
    if source ~= resourceRoot or client ~= mission.leader or not failure or mission.stage ~= "greenwood_failure" or failure.tasksReady then
        return
    end
    if result ~= "active" then
        return failMission("Greenwood bail-out task assignment failed: " .. tostring(result) .. " " .. tostring(details or ""), "SWE3_D")
    end
    for _, name in ipairs(failure.actors) do
        local ped = mission.entities[name]
        if not isElement(ped) or getElementSyncer(ped) ~= client then
            return failMission("Greenwood bail-out task lacked authoritative ownership: " .. name, actorFailureText[name])
        end
    end
    failure.tasksReady = true
    outputDebugString("[drive-thru] Native immediate-leave and smart-flee sequences active: " .. tostring(details or ""))
    finishGreenwoodFailureWhenReady()
end)

addEvent("drivethru:hoodFailureLeasesReady", true)
addEventHandler("drivethru:hoodFailureLeasesReady", resourceRoot, function(failureId)
    local failure = mission.hoodFailure
    if source ~= resourceRoot or client ~= mission.leader or not failure or failure.id ~= tonumber(failureId) or
        mission.stage ~= "hood_failure_prepare" then
        return
    end
    if failure.routeHandoff then
        return triggerClientEvent(mission.leader, "drivethru:hoodFailureLeasesCommitted", resourceRoot, failure.id)
    end

    -- The client first overlaps the runtime leases. Only then may the route
    -- release its ownership, otherwise a completed off-stream route can put
    -- the driver back under automatic sync while the fixed scene is loading.
    cancelPursuitRoute("grove_failure_handoff", true)
    for _, name in ipairs({"ballas_driver", "voodoo"}) do
        local element = mission.entities[name]
        if not isElement(element) or getElementSyncer(element) ~= mission.leader then
            return failMission("Grove Street failure route handoff lacked authoritative ownership: " .. name, "TW2_Y")
        end
    end
    failure.routeHandoff = true
    triggerClientEvent(mission.leader, "drivethru:hoodFailureLeasesCommitted", resourceRoot, failure.id)
    outputDebugString("[drive-thru] Grove failure route ownership committed before camera preparation")
end)

addEvent("drivethru:hoodFailureBlack", true)
addEventHandler("drivethru:hoodFailureBlack", resourceRoot, function(failureId, result, details)
    local failure = mission.hoodFailure
    if source ~= resourceRoot or client ~= mission.leader or not failure or failure.id ~= tonumber(failureId) or
        mission.stage ~= "hood_failure_prepare" then
        return
    end
    if result ~= "ready" then
        return failMission("Grove Street failure camera preparation failed: " .. tostring(result) .. " " .. tostring(details or ""), "TW2_Y")
    end
    if not failure.routeHandoff then
        return failMission("Grove Street failure camera preparation completed before route ownership handoff", "TW2_Y")
    end
    mission.stage = "hood_failure_setup"
    triggerClientEvent(mission.leader, "drivethru:hoodFailureFrozen", resourceRoot, failure.id, mission.entities)
end)

addEvent("drivethru:hoodFailureActive", true)
addEventHandler("drivethru:hoodFailureActive", resourceRoot, function(failureId, result, details)
    local failure = mission.hoodFailure
    if source ~= resourceRoot or client ~= mission.leader or not failure or failure.id ~= tonumber(failureId) or
        (mission.stage ~= "hood_failure_setup" and mission.stage ~= "hood_failure_active") then
        return
    end
    if result ~= "active" then
        return failMission("Grove Street drive-by setup failed: " .. tostring(result) .. " " .. tostring(details or ""), "TW2_Y")
    end
    if mission.stage == "hood_failure_active" then
        return
    end
    for _, name in ipairs({"ballas_driver", "ballas_passenger"}) do
        local ped = mission.entities[name]
        if not isElement(ped) or getElementSyncer(ped) ~= client then
            return failMission("Grove Street failure task lacked authoritative ownership: " .. name, "TW2_Y")
        end
    end
    mission.stage = "hood_failure_active"
    outputDebugString("[drive-thru] Grove Street slow drive and coordinate drive-by active: " .. tostring(details or ""))
end)

addEvent("drivethru:hoodFailureDeathObserved", true)
addEventHandler("drivethru:hoodFailureDeathObserved", resourceRoot, function(failureId, name, health, taskEvidence)
    local failure = mission.hoodFailure
    if source ~= resourceRoot or client ~= mission.leader or not failure or failure.id ~= tonumber(failureId) or
        mission.stage ~= "hood_failure_active" or (name ~= "mate1" and name ~= "mate2") or type(health) ~= "number" or
        type(taskEvidence) ~= "string" then
        return
    end
    local ped = mission.entities[name]
    if not isElement(ped) or getElementSyncer(ped) ~= client then
        return failMission("Grove Street scripted death lacked authoritative ownership: " .. tostring(name), "TW2_Y")
    end
    -- TASK_DIE runs inside the primary sequence slot, while GTA's IsDead path
    -- only inspects its event-response death slot. Native task/progress
    -- evidence from the authoritative syncer is therefore the correct gate;
    -- the server then commits health 0 for every client.
    if getElementHealth(ped) > 0 and not setElementHealth(ped, 0) then
        return failMission("Grove Street scripted death could not be synchronized: " .. tostring(name), "TW2_Y")
    end
    observeHoodFailureDeath(name, ("client health=%.1f task=%s"):format(health, taskEvidence))
end)

addEvent("drivethru:hoodFailureEscapeBlack", true)
addEventHandler("drivethru:hoodFailureEscapeBlack", resourceRoot, function(failureId, result, details)
    local failure = mission.hoodFailure
    if source ~= resourceRoot or client ~= mission.leader or not failure or failure.id ~= tonumber(failureId) or
        mission.stage ~= "hood_failure_escape" then
        return
    end
    if result ~= "black" then
        return failMission("Grove Street escape fade failed: " .. tostring(result) .. " " .. tostring(details or ""), "TW2_Y")
    end
    for _, name in ipairs({"ballas_driver", "ballas_passenger", "voodoo"}) do
        if isElement(mission.entities[name]) then
            destroyElement(mission.entities[name])
        end
        mission.entities[name] = nil
    end
    if isElement(failure.frozenElement) then
        setElementFrozen(failure.frozenElement, false)
    end
    failure.frozenElement = nil
    mission.stage = "hood_failure_restore"
    triggerClientEvent(mission.leader, "drivethru:hoodFailureRestore", resourceRoot, failure.id)
end)

addEvent("drivethru:hoodFailureRestored", true)
addEventHandler("drivethru:hoodFailureRestored", resourceRoot, function(failureId, result)
    local failure = mission.hoodFailure
    if source ~= resourceRoot or client ~= mission.leader or not failure or failure.id ~= tonumber(failureId) or
        mission.stage ~= "hood_failure_restore" then
        return
    end
    if result ~= "restored" then
        return failMission("Grove Street failure cleanup was refused: " .. tostring(result), "TW2_Y")
    end
    failMission("The Ballas reached Grove Street before they were stopped", "TW2_Y")
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
    elseif audio.purpose == "chase" then
        mission.chaseLineIndex = audio.index
        rememberTimer(setTimer(queueNextChaseLine, DRIVETHRU.audio.gap, 1))
    elseif audio.purpose == "return_drive" then
        mission.returnLineIndex = audio.index
        rememberTimer(setTimer(queueNextReturnDriveLine, DRIVETHRU.audio.gap, 1))
    elseif audio.purpose == "return_scene" then
        local scene = mission.returnScene
        if not scene or scene.releasing then
            return
        end
        scene.lineIndex = audio.index
        local sceneProfile = DRIVETHRU.returnTrip[scene.phase].scene
        if sceneProfile.lookAt and sceneProfile.lookAt.afterLine == scene.lineIndex then
            triggerClientEvent(mission.leader, "drivethru:returnSceneLookAt", resourceRoot, scene.id, sceneProfile.lookAt.actor,
                               sceneProfile.lookAt.duration)
        end
        if sceneProfile.skippableAfterLine == scene.lineIndex then
            scene.skippable = true
            triggerClientEvent(mission.leader, "drivethru:returnSceneSkippable", resourceRoot, scene.id)
        end
        if sceneProfile.camera.vectorAfterLine == scene.lineIndex then
            triggerClientEvent(mission.leader, "drivethru:returnSceneVectorCamera", resourceRoot, scene.id)
        end
        rememberTimer(setTimer(function()
            queueNextReturnSceneLine(scene)
        end, DRIVETHRU.audio.gap, 1))
    elseif audio.purpose == "vehicle_reminder" then
        if mission.leaderInVehicle then
            if mission.stage == "drive" then
                rememberTimer(setTimer(queueNextDriveLine, DRIVETHRU.audio.gap, 1))
            elseif mission.stage == "chase" then
                rememberTimer(setTimer(queueNextChaseLine, DRIVETHRU.audio.gap, 1))
            elseif mission.returnPhase and mission.stage == DRIVETHRU.returnTrip[mission.returnPhase].stage then
                rememberTimer(setTimer(queueNextReturnDriveLine, DRIVETHRU.audio.gap, 1))
            end
        else
            triggerClientEvent(mission.leader, "drivethru:vehicleReminderFinished", resourceRoot, mission.stage)
        end
    elseif audio.purpose == "vehicle_failure" then
        local failure = mission.vehicleFailure
        if not failure or mission.stage ~= "greenwood_failure" then
            return
        end
        failure.audioFinished = true
        outputDebugString("[drive-thru] Greenwood failure warning finished naturally; waiting for native flee activation")
        finishGreenwoodFailureWhenReady()
    end
end)

addEvent("drivethru:returnArrivalReport", true)
addEventHandler("drivethru:returnArrivalReport", resourceRoot, function(phase, onAllWheels)
    local player, vehicle = client, mission.entities.vehicle
    local profile = type(phase) == "string" and DRIVETHRU.returnTrip[phase] or nil
    if source ~= resourceRoot or player ~= mission.leader or phase ~= mission.returnPhase or not profile or mission.stage ~= profile.stage or
        onAllWheels ~= true or not isElement(vehicle) or getPedOccupiedVehicle(player) ~= vehicle or getPedOccupiedVehicleSeat(player) ~= 0 then
        return
    end
    local x, y, z = getElementPosition(vehicle)
    local destination = profile.destination
    if math.abs(x - destination.x) > destination.radiusX or math.abs(y - destination.y) > destination.radiusY or
        math.abs(z - destination.z) > destination.radiusZ then
        outputDebugString("[drive-thru] Rejected stale return 09D0 arrival report outside the SCM box", 2)
        return
    end
    outputDebugString(("[drive-thru] RETURN GATE PASSED: %s position=(%.2f, %.2f, %.2f) driver=true allWheels=true"):format(
                          phase, x, y, z))
    startReturnScene(phase)
end)

addEvent("drivethru:returnSceneReady", true)
addEventHandler("drivethru:returnSceneReady", resourceRoot, function(sceneId, result, details)
    local scene = mission.returnScene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or scene.ready or
        mission.stage ~= "return_" .. scene.phase .. "_scene_prepare" then
        return
    end
    if result ~= "ready" then
        return failMission("Return-scene preparation failed: " .. tostring(result) .. " " .. tostring(details or ""))
    end
    local vehicle = mission.entities.vehicle
    if not isElement(vehicle) or getElementSyncer(vehicle) ~= client or getPedOccupiedVehicle(client) ~= vehicle or
        getPedOccupiedVehicleSeat(client) ~= 0 then
        return failMission("Return scene was reported ready without authoritative Greenwood ownership and driver state")
    end
    scene.ready = true
    outputDebugString(("[drive-thru] %s return camera ready: %s"):format(scene.phase, tostring(details or "")))
    beginReturnSceneTimeline(scene)
end)

addEvent("drivethru:returnSceneDeparturesReady", true)
addEventHandler("drivethru:returnSceneDeparturesReady", resourceRoot, function(sceneId, result, details)
    local scene = mission.returnScene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or scene.releasing or
        not scene.departuresStartedAt or scene.departuresReady then
        return
    end
    if result ~= "active" then
        return failMission("Return-scene departure task assignment failed: " .. tostring(result) .. " " .. tostring(details or ""))
    end
    for _, departure in ipairs(DRIVETHRU.returnTrip[scene.phase].scene.departures) do
        local ped = mission.entities[departure.actor]
        if not isElement(ped) or getElementSyncer(ped) ~= client then
            return failMission("Return-scene departure task lacked authoritative ownership: " .. departure.actor)
        end
    end
    scene.departuresReady = true
    outputDebugString(("[drive-thru] %s return native leave-and-walk sequences active: %s"):format(scene.phase,
                                                                                                  tostring(details or "")))
end)

addEvent("drivethru:returnSceneLeaseLost", true)
addEventHandler("drivethru:returnSceneLeaseLost", resourceRoot, function(sceneId)
    local scene = mission.returnScene
    if source == resourceRoot and client == mission.leader and scene and scene.id == tonumber(sceneId) then
        failMission("The native camera lease was lost during the " .. scene.phase .. " return scene")
    end
end)

addEvent("drivethru:returnSceneSkipRequest", true)
addEventHandler("drivethru:returnSceneSkipRequest", resourceRoot, function(sceneId)
    local scene = mission.returnScene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or not scene.skippable or scene.releasing then
        return
    end
    outputDebugString(("[drive-thru] %s return scene skip authorized by leader"):format(scene.phase))
    requestReturnSceneRelease(scene, true)
end)

addEvent("drivethru:returnSceneReleased", true)
addEventHandler("drivethru:returnSceneReleased", resourceRoot, function(sceneId, result)
    local scene = mission.returnScene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or not scene.releasing then
        return
    end
    if result ~= "released" then
        return failMission("Return-scene camera release failed: " .. tostring(result))
    end
    local phase, skipped = scene.phase, scene.skipped
    mission.returnScene = nil
    if phase == "grove" then
        for _, name in ipairs({"sweet", "ryder"}) do
            if isElement(mission.entities[name]) then
                destroyElement(mission.entities[name])
            end
            mission.entities[name] = nil
        end
        if skipped then
            triggerClientEvent(mission.leader, "drivethru:returnSceneReveal", resourceRoot)
        end
        outputDebugString(("[drive-thru] Grove return scene %s; Sweet and Ryder removed in SCM order"):format(
                              skipped and "skipped" or "completed"))
        startReturnDrive("smoke")
    else
        if isElement(mission.entities.smoke) then
            destroyElement(mission.entities.smoke)
        end
        mission.entities.smoke = nil
        if skipped then
            triggerClientEvent(mission.leader, "drivethru:returnSceneReveal", resourceRoot)
        end
        outputDebugString(("[drive-thru] Smoke return scene %s; Smoke removed before mission pass"):format(
                              skipped and "skipped" or "completed"))
        finishReturnMission()
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
    mission.stage = "restaurant_camera"
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
        elseif mission.stage == "chase" then
            triggerClientEvent(ped, "drivethru:chaseNavigation", resourceRoot, "target", mission.entities)
            rememberTimer(setTimer(queueNextChaseLine, DRIVETHRU.audio.gap, 1))
        elseif mission.returnPhase and mission.stage == DRIVETHRU.returnTrip[mission.returnPhase].stage then
            triggerClientEvent(ped, "drivethru:returnDriveStarted", resourceRoot, mission.returnPhase, mission.entities)
            rememberTimer(setTimer(queueNextReturnDriveLine, DRIVETHRU.audio.gap, 1))
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
            local reminderQueued = queueReturnToCarReminder()
            triggerClientEvent(ped, "drivethru:stage", resourceRoot, "return_car", mission.entities, reminderQueued)
        elseif mission.stage == "chase" then
            local reminderQueued = queueReturnToCarReminder()
            triggerClientEvent(ped, "drivethru:chaseNavigation", resourceRoot, "vehicle", mission.entities, reminderQueued)
        elseif mission.returnPhase and mission.stage == DRIVETHRU.returnTrip[mission.returnPhase].stage then
            local reminderQueued = queueReturnToCarReminder()
            triggerClientEvent(ped, "drivethru:returnDriveStarted", resourceRoot, mission.returnPhase, mission.entities, reminderQueued)
        end
    end
end)

addEventHandler("onVehicleExplode", root, function()
    if mission.running and source == mission.entities.vehicle and (greenwoodFailureStages[mission.stage] or mission.stage == "greenwood_failure") then
        failMission("The Greenwood was destroyed", "SWE3_D")
    elseif mission.running and mission.stage == "chase" and source == mission.entities.voodoo then
        beginFootCombat("Voodoo exploded")
    elseif mission.running and mission.hoodFailure and source == mission.entities.voodoo then
        failMission("The Voodoo exploded during the Grove Street failure scene", "TW2_Y")
    end
end)

addEventHandler("onPedWasted", root, function()
    if not mission.running then
        return
    end
    local actorStage = mission.vehicleFailure and mission.vehicleFailure.originalStage or mission.stage
    if source == mission.leader then
        failMission("CJ died")
    elseif source == mission.entities.sweet and actorFailureStages[actorStage] and actorFailureStages[actorStage].sweet then
        failMission("Sweet died", "SWE3_E")
    elseif source == mission.entities.ryder and actorFailureStages[actorStage] and actorFailureStages[actorStage].ryder then
        failMission("Ryder died", "SWE3_F")
    elseif source == mission.entities.smoke and actorFailureStages[actorStage] and actorFailureStages[actorStage].smoke then
        failMission("Smoke died", "SWE3_G")
    elseif mission.hoodFailure and (source == mission.entities.mate1 or source == mission.entities.mate2) then
        observeHoodFailureDeath(source == mission.entities.mate1 and "mate1" or "mate2")
    elseif mission.stage == "chase" and (source == mission.entities.ballas_driver or source == mission.entities.ballas_passenger) then
        local driverDead = not isElement(mission.entities.ballas_driver) or isPedDead(mission.entities.ballas_driver)
        local passengerDead = not isElement(mission.entities.ballas_passenger) or isPedDead(mission.entities.ballas_passenger)
        if driverDead and passengerDead then
            completeChase()
        else
            beginFootCombat("one Ballas was killed")
        end
    elseif mission.stage == "chase" and (source == mission.entities.mate1 or source == mission.entities.mate2) then
        failMission("The Grove support actors were killed", "TW2_Y")
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

addCommandHandler("drivethrusimfar", function(player)
    if not mission.running or player ~= mission.leader or mission.stage ~= "chase" or not isElement(mission.entities.vehicle) then
        outputChatBox("/drivethrusimfar requires the active Ballas chase.", player, 255, 180, 80)
        return
    end
    local vehicle = mission.entities.vehicle
    setElementPosition(vehicle, 1690.0, 1448.0, 10.8)
    setElementVelocity(vehicle, 0, 0, 0)
    local routeState = isElement(mission.pursuitRouteTask) and
                           exports["native-task-runtime"]:getNativeDriveRouteState(mission.pursuitRouteTask) or false
    outputChatBox("Drive-Thru simulation owner moved more than 3 km away; wait for the Grove failure scene.", player, 120, 220, 255)
    outputDebugString(("[drive-thru] OFF-STREAM INTEGRATION TEST: leader moved far; route epoch=%s index=%s state=%s"):format(
                          tostring(routeState and routeState.epoch or "none"),
                          tostring(routeState and routeState.routeIndex or "none"),
                          tostring(routeState and routeState.state or "none")))
end)

addCommandHandler("drivethruskip", function(player)
    local scene = mission.cutscene
    if mission.running and player == mission.leader and scene and not scene.skipRequested then
        scene.skipRequested = true
        triggerClientEvent(player, "drivethru:cutsceneSkip", resourceRoot, scene.id)
    elseif mission.running and player == mission.leader and mission.returnScene and mission.returnScene.skippable and
        not mission.returnScene.releasing then
        requestReturnSceneRelease(mission.returnScene, true)
    end
end)

addEventHandler("onResourceStart", resourceRoot, function()
    outputDebugString("[drive-thru] Resource ready. Use /drivethru to run the complete multiplayer-visible SWEET3 mission.")
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
