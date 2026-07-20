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
    mission.chaseDialoguePhase = nil
    mission.chaseLineIndex = 0
    mission.chaseDamageThreshold = nil
    mission.footCombat = false
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
        local allowsMissingGreenwood = mission.stage == "cutscene" or mission.stage == "restaurant_teardown" or
                                           mission.stage == "restaurant_rebuild"
        if not allowsMissingGreenwood and not isElement(vehicle) then
            return failMission("The Greenwood element disappeared", "SWE3_D")
        end
        if isElement(vehicle) and mission.stage ~= "cutscene" and getElementHealth(vehicle) <= 250 then
            local textKey = mission.stage == "chase" and "SWE2_KA" or "SWE2_KC"
            return failMission("The Greenwood health reached the vanilla 250 threshold", textKey)
        end
        for _, actor in ipairs({"sweet", "ryder", "smoke"}) do
            if mission.entities[actor] and not isElement(mission.entities[actor]) then
                return failMission(actor .. " element disappeared")
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
    return warpPedIntoVehicle(mission.entities[name], vehicle, profile.seat)
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
            return failMission("The Ballas reached Grove Street before they were stopped", "TW2_Y")
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
    mission.footCombat = true
    outputDebugString("[drive-thru] Vehicle-to-foot combat transition: " .. tostring(reason))
    triggerClientEvent(mission.leader, "drivethru:footCombat", resourceRoot, mission.entities, reason)
end

completeChase = function()
    if not mission.running or mission.finishing or mission.stage ~= "chase" then
        return
    end
    mission.stage = "chase_checkpoint"
    mission.audio = nil
    outputDebugString(("[drive-thru] CHECKPOINT PASSED: both Ballas dead after native route and three drive-bys; footCombat=%s"):format(
                          tostring(mission.footCombat)))
    triggerClientEvent(mission.leader, "drivethru:chaseCheckpoint", resourceRoot, mission.entities)
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
    takeAllWeapons(player)
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
    outputDebugString("[drive-thru] SWEET2B reconstruction passed; Voodoo health rearmed to 2700 before native route assignment")
    triggerClientEvent(mission.leader, "drivethru:pursuitRoute", resourceRoot, mission.entities)
end)

addEvent("drivethru:pursuitRouteReady", true)
addEventHandler("drivethru:pursuitRouteReady", resourceRoot, function(result, details)
    if source ~= resourceRoot or client ~= mission.leader or mission.stage ~= "pursuit_route_barrier" then
        return
    end
    if result ~= "active" then
        return failMission("Ballas route assignment failed: " .. tostring(result) .. " " .. tostring(details or ""))
    end
    local driver, voodoo = mission.entities.ballas_driver, mission.entities.voodoo
    if not isElement(driver) or not isElement(voodoo) or getElementSyncer(driver) ~= client or getElementSyncer(voodoo) ~= client or
        getPedOccupiedVehicle(driver) ~= voodoo or getPedOccupiedVehicleSeat(driver) ~= 0 then
        return failMission("Ballas route was reported active without authoritative driver ownership and seat state")
    end
    local created, failedActor = createChaseActors()
    if not created then
        return failMission("Pursuit actor creation failed: " .. tostring(failedActor))
    end
    setElementHealth(mission.entities.vehicle, DRIVETHRU.chase.greenwoodHealth)
    mission.stage = "pursuit_task_barrier"
    outputDebugString("[drive-thru] Driver route active; Ballas passenger and two Grove support actors created in SCM order")
    triggerClientEvent(mission.leader, "drivethru:pursuitActorsCreated", resourceRoot, mission.entities)
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
        ryder = {vehicle = mission.entities.vehicle, seat = DRIVETHRU.actors.ryder.seat},
        sweet = {vehicle = mission.entities.vehicle, seat = DRIVETHRU.actors.sweet.seat},
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
        elseif mission.stage == "chase" then
            triggerClientEvent(ped, "drivethru:chaseNavigation", resourceRoot, "vehicle", mission.entities)
        end
    end
end)

addEventHandler("onVehicleExplode", root, function()
    if mission.running and source == mission.entities.vehicle then
        failMission("The Greenwood was destroyed", "SWE3_D")
    elseif mission.running and source == mission.entities.voodoo then
        beginFootCombat("Voodoo exploded")
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
    elseif source == mission.entities.ballas_driver or source == mission.entities.ballas_passenger then
        local driverDead = not isElement(mission.entities.ballas_driver) or isPedDead(mission.entities.ballas_driver)
        local passengerDead = not isElement(mission.entities.ballas_passenger) or isPedDead(mission.entities.ballas_passenger)
        if driverDead and passengerDead then
            completeChase()
        else
            beginFootCombat("one Ballas was killed")
        end
    elseif source == mission.entities.mate1 or source == mission.entities.mate2 then
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

addCommandHandler("drivethruskip", function(player)
    local scene = mission.cutscene
    if mission.running and player == mission.leader and scene and not scene.skipRequested then
        scene.skipRequested = true
        triggerClientEvent(player, "drivethru:cutsceneSkip", resourceRoot, scene.id)
    end
end)

addEventHandler("onResourceStart", resourceRoot, function()
    outputDebugString("[drive-thru] Resource ready. Use /drivethru to run SWEET2A, SWEET2B and the native Ballas chase checkpoint.")
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
