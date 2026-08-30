local mission
local serial = 0
local failMission

addEvent("onStoryFileCutsceneStateChange", false)
addEvent("onStoryVehicleRelocationStateChange", false)
addEvent("onStoryWorldRuntimeStopping", false)
addEvent("onNativeTaskRuntimeStopping", false)
addEvent("onNativeTaskCohortStateChange", false)

local function rememberTimer(timer)
    mission.timers[#mission.timers + 1] = timer
    return timer
end

local function snapshotPlayer(player)
    local x, y, z = getElementPosition(player)
    local rx, ry, rz = getElementRotation(player)
    return {
        x = x, y = y, z = z, rx = rx, ry = ry, rz = rz,
        interior = getElementInterior(player), dimension = getElementDimension(player),
        model = getElementModel(player),
        alpha = getElementAlpha(player), frozen = isElementFrozen(player),
        collisions = getElementCollisionsEnabled(player), health = getElementHealth(player), armor = getPedArmor(player),
    }
end

local function restorePlayer(player, state)
    if not isElement(player) then
        return
    end
    if isPedInVehicle(player) then
        removePedFromVehicle(player)
    end
    setElementInterior(player, state.interior)
    setElementDimension(player, state.dimension)
    setElementPosition(player, state.x, state.y, state.z)
    setElementRotation(player, state.rx, state.ry, state.rz)
    setElementAlpha(player, state.alpha)
    setElementCollisionsEnabled(player, state.collisions)
    setElementFrozen(player, state.frozen)
    setElementHealth(player, math.max(1, state.health))
    setPedArmor(player, state.armor)
    toggleAllControls(player, true, true, true)
end

local function controlsRestored(player)
    for _, control in ipairs({"forwards", "accelerate", "vehicle_left", "vehicle_right", "enter_exit"}) do
        if not isControlEnabled(player, control) then
            return false, control
        end
    end
    return true
end

local function restoredPlayerMatches(player, expected)
    if not isElement(player) or isPedInVehicle(player) or getElementInterior(player) ~= expected.interior or
        getElementDimension(player) ~= expected.dimension or getElementModel(player) ~= expected.model or
        isElementFrozen(player) ~= expected.frozen or
        getElementCollisionsEnabled(player) ~= expected.collisions then
        return false
    end
    local x, y, z = getElementPosition(player)
    return getDistanceBetweenPoints3D(x, y, z, expected.x, expected.y, expected.z) <= 0.1
end

local function trace(event, data)
    if not mission then
        return
    end
    local record = {event = event, run = mission.id, stage = mission.stage, tick = getTickCount()}
    for key, value in pairs(type(data) == "table" and data or {}) do
        if type(value) ~= "userdata" then
            record[key] = value
        end
    end
    local encoded = toJSON(record, true)
    outputServerLog("[sweet-and-kendl-jsonl] " .. tostring(encoded):gsub("[\r\n]", ""))
end

local function transitionTrace(event, data)
    local record = {
        event = event,
        run = mission and mission.id,
        stage = mission and mission.stage,
        tick = getTickCount(),
    }
    for key, value in pairs(type(data) == "table" and data or {}) do
        if type(value) ~= "userdata" then
            record[key] = value
        end
    end
    outputServerLog("[sweet-and-kendl-transition-jsonl] " .. tostring(toJSON(record, true)):gsub("[\r\n]", ""))
end

local function participants()
    local result = {}
    if not mission then
        return result
    end
    for player in pairs(mission.players) do
        if isElement(player) then
            result[#result + 1] = player
        end
    end
    return result
end

local function track(element)
    if not isElement(element) then
        return false
    end
    mission.entities[#mission.entities + 1] = element
    setElementDimension(element, SAK.dimension)
    return element
end

local function setStage(stage, extra)
    mission.stage = stage
    mission.stageStartedAt = getTickCount()
    trace("stage", {value = stage})
    triggerClientEvent(participants(), "sak:stage", resourceRoot, stage, extra or {})
end

local function beginAmbientSequence(name, token, callback)
    if not mission or mission.ambientSequence then return false end
    local sequence = {name = name, token = token, waiting = {}, callback = callback}
    mission.ambientSequence = sequence
    if mission.headless then
        mission.ambientSequence = nil
        if callback then callback() end
        return true
    end
    for _, player in ipairs(participants()) do sequence.waiting[player] = true end
    triggerClientEvent(participants(), "sak:ambientAudio", resourceRoot, name, mission.actors, token)
    sequence.timeout = rememberTimer(setTimer(function()
        if mission and mission.ambientSequence == sequence then
            mission.ambientSequence = nil
            failMission("audio ambiant non termine: " .. tostring(name))
        end
    end, 90000, 1))
    trace("audio_gate", {name = name, token = token, proximity = true})
    return true
end

local function cancelCohort(name)
    local handle = mission and mission.cohorts[name]
    if isElement(handle) and not mission.nativeTaskStopping then
        exports["native-task-runtime"]:cancelNativeTaskCohort(handle)
    end
    if mission then
        mission.cohorts[name] = nil
    end
end

local function releasePlayerModelLease()
    if not mission or not isElement(mission.playerModelLease) then
        return
    end
    if not mission.storyWorldStopping then
        exports["story-world-runtime"]:releaseStoryPlayerModelLease(mission.playerModelLease)
    end
    mission.playerModelLease = nil
end

local function releaseFileCutscene()
    local active = mission and mission.fileCutscene
    if not active or not isElement(active.handle) then
        if mission then mission.fileCutscene = nil end
        return
    end
    if not mission.storyWorldStopping then
        exports["story-world-runtime"]:releaseStoryFileCutscene(active.handle)
    end
    if isElement(active.handle) and not mission.storyWorldStopping then
        destroyElement(active.handle)
    end
    mission.fileCutscene = nil
end

local function releaseVehicleRelocations()
    if not mission or not mission.vehicleRelocations then return end
    for handle in pairs(mission.vehicleRelocations) do
        if isElement(handle) and not mission.storyWorldStopping then
            exports["story-world-runtime"]:releaseStoryVehicleRelocation(handle)
            if isElement(handle) then destroyElement(handle) end
        end
    end
    mission.vehicleRelocations = {}
end

local function finishTransitionTerminal(ok, reason)
    if not mission then
        return
    end
    local profile = tostring(mission.transitionProfile or "unknown")
    trace(ok and "PASS" or "FAIL", {profile = profile, reason = reason, cleanup = true})
    transitionTrace(ok and "PASS" or "FAIL", {
        profile = profile,
        players = mission.expectedPlayers or #participants(),
        reason = reason,
        cleanup = true,
    })
    outputServerLog(("[sweet-and-kendl-transition] %s profile=%s players=%d%s"):format(
        ok and "PASS" or "FAIL", profile, mission.expectedPlayers or #participants(),
        reason and (" reason=" .. tostring(reason)) or ""))
    mission = nil
end

local function beginTransitionTerminal(ok, reason)
    if not mission or mission.cleanupStarted then
        return
    end
    mission.cleanupStarted = true
    for _, timer in ipairs(mission.timers) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    for name in pairs(mission.cohorts) do
        cancelCohort(name)
    end
    releaseFileCutscene()
    releaseVehicleRelocations()
    releasePlayerModelLease()
    mission.cleanupWaiting = {}
    for player, state in pairs(mission.players) do
        if isElement(player) then
            restorePlayer(player, state)
            mission.cleanupWaiting[player] = true
        end
    end
    for _, element in ipairs(mission.entities) do
        if isElement(element) then
            destroyElement(element)
        end
    end
    mission.terminal = {ok = ok == true, reason = reason}
    triggerClientEvent(participants(), "sak:cleanup", resourceRoot, mission.id, true)
    mission.cleanupTimeout = setTimer(function()
        if mission and mission.cleanupWaiting then
            finishTransitionTerminal(false, "cleanup client timeout")
        end
    end, 10000, 1)
end

local function clearMission(restorePlayers, terminal)
    if not mission or mission.cleaning then
        return
    end
    mission.cleaning = true
    if mission.transition and terminal then
        return beginTransitionTerminal(terminal.ok == true, terminal.reason)
    end
    for _, timer in ipairs(mission.timers) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    for name in pairs(mission.cohorts) do
        cancelCohort(name)
    end
    releaseFileCutscene()
    releaseVehicleRelocations()
    releasePlayerModelLease()
    triggerClientEvent(participants(), "sak:cleanup", resourceRoot, mission.id)
    for player, state in pairs(mission.players) do
        if restorePlayers and isElement(player) then
            restorePlayer(player, state)
        end
    end
    for _, element in ipairs(mission.entities) do
        if isElement(element) and not (not restorePlayers and mission.preserveOnSuccess and
            mission.preserveOnSuccess[element]) then
            destroyElement(element)
        end
    end
    mission = nil
end

failMission = function(reason, textKey)
    if not mission or mission.finishing then
        return
    end
    mission.finishing = true
    setStage("failed", {reason = reason, textKey = textKey})
    if mission.transition then
        trace("failure_requested", {reason = reason})
        return rememberTimer(setTimer(function()
            clearMission(true, {ok = false, reason = reason})
        end, 500, 1))
    end
    trace("FAIL", {reason = reason})
    outputServerLog("[sweet-and-kendl] FAIL: " .. tostring(reason))
    rememberTimer(setTimer(function()
        clearMission(true)
    end, mission.headless and 500 or 5000, 1))
end

local function beginBarrier(kind, name, callback, payload)
    if kind ~= "scene" then
        return failMission("barriere de presentation inconnue: " .. tostring(kind))
    end
    mission.sceneSerial = mission.sceneSerial + 1
    mission.barrier = {
        id = mission.sceneSerial,
        kind = kind,
        name = name,
        waiting = {},
        callback = callback,
        started = {},
        finished = {},
        released = {},
    }
    for _, player in ipairs(participants()) do
        mission.barrier.waiting[player] = true
    end
    setStage(kind .. ":" .. name)
    local expected = mission.barrier
    if mission.headless then
        return rememberTimer(setTimer(function()
            if mission and mission.barrier == expected then
                mission.barrier = nil
                callback()
            end
        end, 50, 1))
    end
    triggerClientEvent(participants(), "sak:scene", resourceRoot, mission.sceneSerial, name, mission.leader,
                       payload or mission.actors)
    rememberTimer(setTimer(function()
        if mission and mission.barrier == expected then
            failMission(kind .. " " .. name .. " non termine par toute la partie")
        end
    end, 120000, 1))
end

local function beginFileCutscene(name, visibleArea, callback)
    if not mission or mission.fileCutscene then
        return failMission("cutscene fichier concurrente: " .. tostring(name))
    end
    setStage("cutscene:" .. tostring(name))
    local handle, reason = exports["story-world-runtime"]:createStoryFileCutscene(
        participants(), mission.leader, name, visibleArea, {
            allowLeaderSkip = true,
            loadTimeout = 15000,
            playTimeout = 180000,
            releaseTimeout = 10000,
            fadeIn = 1.0,
        })
    if not handle then
        return failMission("cutscene fichier " .. tostring(name) .. " refusee: " .. tostring(reason))
    end
    mission.fileCutscene = {handle = handle, name = name, callback = callback, skipIssued = false}
end

local function transitionCheckpoint(name, kind, callback)
    if not mission or not mission.transition then
        return callback()
    end
    if mission.transitionCheckpoint then
        return failMission("checkpoint transition concurrent: " .. tostring(name))
    end
    mission.transitionProbeSerial = (mission.transitionProbeSerial or 0) + 1
    local checkpoint = {
        probeId = mission.transitionProbeSerial,
        name = name,
        kind = kind,
        control = kind == "foot_all" and "forwards" or "accelerate",
        callback = callback,
        waiting = {},
    }
    mission.transitionCheckpoint = checkpoint
    for _, player in ipairs(participants()) do
        checkpoint.waiting[player] = true
    end
    trace("transition_checkpoint", {probeId = checkpoint.probeId, name = name, kind = kind, phase = "requested"})
    outputServerLog(("[sweet-and-kendl-transition] READY checkpoint=%s kind=%s players=%d input=%s/w"):format(
        name, kind, #participants(), checkpoint.control))
    transitionTrace("READY", {probeId = checkpoint.probeId, checkpoint = name, kind = kind,
                               players = #participants(), control = checkpoint.control, key = "w"})
    triggerClientEvent(participants(), "sak:transitionCheckpoint", resourceRoot, mission.id, checkpoint.probeId,
                       name, kind, mission.leader)
    checkpoint.timeout = rememberTimer(setTimer(function()
        if mission and mission.transitionCheckpoint == checkpoint then
            failMission("checkpoint transition timeout: " .. tostring(name))
        end
    end, 70000, 1))
end

local function positionVehicle(vehicle, values, finalFrozen)
    if not isElement(vehicle) then
        return false
    end
    -- Short local placements and transactional cross-map relocations have
    -- different ownership. Keeping their records separate prevents cleanup
    -- from treating a real vehicle as a relocation handle and destroying it.
    mission.positionRelocations = mission.positionRelocations or {}
    local previous = mission.positionRelocations[vehicle]
    local generation = (previous and previous.generation or 0) + 1
    local restoreFrozen
    if finalFrozen ~= nil then
        restoreFrozen = finalFrozen == true
    else
        restoreFrozen = previous and previous.restoreFrozen or isElementFrozen(vehicle)
    end
    mission.positionRelocations[vehicle] = {generation = generation, restoreFrozen = restoreFrozen}
    setElementFrozen(vehicle, true)
    setElementVelocity(vehicle, 0, 0, 0)
    setElementAngularVelocity(vehicle, 0, 0, 0)
    setElementPosition(vehicle, values[1], values[2], values[3])
    setElementRotation(vehicle, 0, 0, values[4])
    rememberTimer(setTimer(function()
        local relocation = mission and mission.positionRelocations and mission.positionRelocations[vehicle]
        if not relocation or not isElement(vehicle) or relocation.generation ~= generation then
            return
        end
        setElementVelocity(vehicle, 0, 0, 0)
        setElementAngularVelocity(vehicle, 0, 0, 0)
        setElementFrozen(vehicle, relocation.restoreFrozen)
        mission.positionRelocations[vehicle] = nil
    end, 750, 1))
    return true
end

local function completeSeatMap(vehicle)
    local occupants = {}
    for seat, ped in pairs(getVehicleOccupants(vehicle) or {}) do
        occupants[#occupants + 1] = {ped = ped, seat = tonumber(seat)}
    end
    return occupants
end

local function beginVehicleRelocation(entries, callback, options)
    if not mission then return false end
    local handle, reason = exports["story-world-runtime"]:createStoryVehicleRelocation(
        participants(), mission.leader, entries, options or {timeout = 20000, stableSamples = 3})
    if not handle then
        failMission("relocalisation vehicule refusee: " .. tostring(reason))
        return false
    end
    mission.vehicleRelocations[handle] = {callback = callback}
    return true
end

local function transitionElementState(checkpoint, role, element)
    if not mission or not mission.transition or not isElement(element) then
        return
    end
    local x, y, z = getElementPosition(element)
    local elementType = getElementType(element)
    local occupiedVehicle = (elementType == "player" or elementType == "ped") and getPedOccupiedVehicle(element)
    transitionTrace("STATE", {
        checkpoint = checkpoint,
        role = role,
        elementType = elementType,
        model = getElementModel(element),
        x = x,
        y = y,
        z = z,
        frozen = isElementFrozen(element),
        alpha = getElementAlpha(element),
        vehicleModel = isElement(occupiedVehicle) and getElementModel(occupiedVehicle) or nil,
        seat = isElement(occupiedVehicle) and getPedOccupiedVehicleSeat(element) or -1,
    })
end

local function transitionParticipantStates(checkpoint)
    for _, player in ipairs(participants()) do
        transitionElementState(checkpoint, "participant:" .. getPlayerName(player), player)
    end
end

local function createGang()
    mission.gang = {}
    for _, role in ipairs({"smoke", "ryder", "sweet"}) do
        local config = SAK.gang[role]
        local funeral = SAK.funeralActors[role]
        local bike = track(createVehicle(SAK.models.bmx, config.bike[1], config.bike[2], config.bike[3], 0, 0, config.bike[4]))
        local ped = track(createPed(config.model, funeral.start[1], funeral.start[2], funeral.start[3], funeral.start[4]))
        if not bike or not ped then
            return false
        end
        setElementHealth(bike, 2000)
        exports["native-task-runtime"]:setSynchronizedVehicleTyresCanBurst(bike, false)
        setElementHealth(ped, 2000)
        setElementSyncer(bike, mission.leader, true, true)
        setElementSyncer(ped, mission.leader, true, true)
        mission.gang[role] = {ped = ped, bike = bike}
    end
    return true
end

local function createBallas()
    local config = SAK.ballas
    local vehicle = track(createVehicle(SAK.models.voodoo, config.vehicle[1], config.vehicle[2], config.vehicle[3], 0, 0,
                                        config.vehicle[4]))
    local driver = track(createPed(SAK.models.ballas, config.vehicle[1], config.vehicle[2], config.vehicle[3] + 1,
                                   config.vehicle[4]))
    local passenger = track(createPed(SAK.models.ballas, config.vehicle[1], config.vehicle[2],
                                      config.vehicle[3] + 1, config.vehicle[4]))
    if not vehicle or not driver or not passenger then
        return false
    end
    setVehicleColor(vehicle, 105, 30, 59, 105, 30, 59)
    setElementHealth(vehicle, config.health)
    setVehicleLocked(vehicle, true)
    warpPedIntoVehicle(driver, vehicle, 0)
    warpPedIntoVehicle(passenger, vehicle, 1)
    giveWeapon(driver, config.weapon, 30000, true)
    giveWeapon(passenger, config.weapon, 30000, true)
    mission.ballas = {vehicle = vehicle, driver = driver, passenger = passenger}
    for _, element in ipairs({vehicle, driver, passenger}) do
        setElementSyncer(element, mission.leader, true, true)
    end
    return true
end

local function createPlayerBikes()
    mission.playerBikes = {}
    for index, player in ipairs(participants()) do
        local position = SAK.playerBikes[((index - 1) % #SAK.playerBikes) + 1]
        local bike = track(createVehicle(SAK.models.bmx, position[1], position[2], position[3], 0, 0, position[4]))
        if not bike then
            return false
        end
        setElementHealth(bike, 2000)
        exports["native-task-runtime"]:setSynchronizedVehicleTyresCanBurst(bike, false)
        mission.playerBikes[player] = bike
    end
    return true
end

local function createRouteCohort(name, roles, routes)
    local members, vehicles = {}, {}
    for index, role in ipairs(roles) do
        local actor = mission.gang[role]
        if isElement(actor.bike) and not isVehicleBlown(actor.bike) then
            members[#members + 1] = {
                ped = actor.ped, vehicle = actor.bike, seat = 0, missionActor = true,
                task = {type = "drive_route", route = routes[index], loop = false},
            }
            vehicles[#vehicles + 1] = {vehicle = actor.bike, straightLineDistance = 10}
        else
            -- The SCM explicitly lets a living gang member continue on foot
            -- after losing an NPC BMX. Reuse the same route as a native run
            -- sequence instead of converting vehicle loss into mission fail.
            local sequence = {}
            for _, point in ipairs(routes[index]) do
                sequence[#sequence + 1] = {task = "go_to", x = point.x, y = point.y, z = point.z,
                                           movement = "run", radius = 1.0, slowdownRadius = 2.0}
            end
            members[#members + 1] = {ped = actor.ped, missionActor = true,
                                     task = {type = "sequence", sequence = sequence, loop = false}}
        end
    end
    local handle, reason = exports["native-task-runtime"]:createNativeTaskCohort(mission.leader,
        {members = members, vehicles = vehicles}, {fallbackOwners = {}})
    if not handle then
        return false, reason
    end
    mission.cohorts[name] = handle
    return true
end

local function startBallasCohort()
    cancelCohort("ballas")
    local targetBike = mission.playerBikes[mission.leader]
    local b = mission.ballas
    if not isElement(targetBike) or not b or not isElement(b.vehicle) then
        return false, "cible ou Ballas absents"
    end
    local handle, reason = exports["native-task-runtime"]:createNativeTaskCohort(mission.leader, {
        members = {
            {ped = b.driver, vehicle = b.vehicle, seat = 0, missionActor = true,
             task = {type = "drive_mission", targetVehicle = targetBike, mission = "escort_left", speed = 30,
                     drivingStyle = "avoid_cars"}},
            {ped = b.passenger, vehicle = b.vehicle, seat = 1, missionActor = true,
             task = {type = "drive_by", target = mission.leader, radius = 5000, style = "ai_all_directions",
                     rightHandSide = true, frequency = 40, reissue = true}},
        },
        dependencies = {targetBike, mission.leader},
    }, {fallbackOwners = {}})
    if not handle then
        return false, reason
    end
    mission.cohorts.ballas = handle
    return true
end

local function startSplitDepartureCohort()
    local sweet, ballas = mission.gang.sweet, mission.ballas
    if not sweet or not isElement(sweet.ped) or not isElement(sweet.bike) or not ballas or
        not isElement(ballas.driver) or not isElement(ballas.passenger) or not isElement(ballas.vehicle) or
        isVehicleBlown(ballas.vehicle) or isPedDead(ballas.driver) or isPedDead(ballas.passenger) then
        return false, "Ballas split branch unavailable"
    end
    local handle, reason = exports["native-task-runtime"]:createNativeTaskCohort(mission.leader, {
        members = {
            {ped = sweet.ped, vehicle = sweet.bike, seat = 0, missionActor = true,
             task = {type = "drive_route", route = {{x = 1540.2981, y = -1159.1885, z = 22.9062,
                                                       speed = 20.0}}, loop = false}},
            {ped = ballas.driver, vehicle = ballas.vehicle, seat = 0, missionActor = true,
             task = {type = "drive_mission", targetVehicle = sweet.bike, mission = "escort_left", speed = 40,
                     drivingStyle = "avoid_cars"}},
            {ped = ballas.passenger, vehicle = ballas.vehicle, seat = 1, missionActor = true,
             task = {type = "none"}},
        },
        vehicles = {{vehicle = sweet.bike, straightLineDistance = 10},
                    {vehicle = ballas.vehicle, straightLineDistance = 10}},
        dependencies = {sweet.bike},
    }, {fallbackOwners = {}})
    if not handle then return false, reason end
    mission.cohorts.split_departure = handle
    return true
end

local beginRide
local beginMountRide
local beginReturnRide
local beginFinale
local startFirstBallasAttack

local function waitTransitionCondition(name, predicate, callback)
    local startedAt = getTickCount()
    local timer
    timer = rememberTimer(setTimer(function()
        if not mission or not mission.transition or mission.finishing then
            if isTimer(timer) then killTimer(timer) end
            return
        end
        if predicate() then
            killTimer(timer)
            trace("transition_barrier", {name = name, completed = true})
            return callback()
        end
        if getTickCount() - startedAt >= 90000 then
            killTimer(timer)
            failMission("barriere transition timeout: " .. tostring(name))
        end
    end, 100, 0))
end

local function transitionAdvanceRide1()
    if mission and not mission.transitionRide1AudioStaged then
        mission.transitionRide1AudioStaged = true
        cancelCohort("gang")
        setStage("ride1_audio_relocation")
        local sweet = mission.gang and mission.gang.sweet
        local target = SAK.gang.sweet.bike
        if not sweet or not isElement(sweet.bike) then
            return failMission("Sweet absent de la preuve audio ride1")
        end
        return beginVehicleRelocation({{
            vehicle = sweet.bike, x = target[1], y = target[2], scriptZ = SAK.funeral[3], heading = target[4],
            occupants = completeSeatMap(sweet.bike),
        }}, function()
            setStage("ride1")
            trace("transition_audio_staging", {name = "ride1", proximity = true})
            transitionAdvanceRide1()
        end)
    end
    waitTransitionCondition("ride1_audio", function()
        return mission and mission.stage == "ride1" and mission.ride1AudioDone == true
    end, function()
        if not mission or mission.stage ~= "ride1" then
            return
        end
        if not mission.cohorts.gang then
            local routeOk, routeReason = createRouteCohort("gang", {"smoke", "ryder", "sweet"},
                                                           {SAK.route1, SAK.route1, SAK.route1})
            if not routeOk then return failMission("cohorte BMX refusee: " .. tostring(routeReason)) end
        end
        local function advanceToSplit()
            if not mission or mission.stage ~= "ride1" then return end
            -- Stop the observed native routes before relocating the cohort
            -- into the exact SCM objective box. Separate bike footprints keep
            -- co-op riders from colliding during this diagnostic shortcut.
            cancelCohort("gang")
            cancelCohort("ballas")
            setStage("split_objective_relocation")
            local entries = {}
            for index, player in ipairs(participants()) do
                local bike = mission.playerBikes[player]
                entries[#entries + 1] = {
                    vehicle = bike, x = SAK.split[1] - 6,
                    y = SAK.split[2] + (index - 1) * 3, scriptZ = SAK.split[3], heading = 250,
                    occupants = completeSeatMap(bike),
                }
            end
            local actorOffsets = {smoke = {-3, -3}, ryder = {3, 3}, sweet = {0, 0}}
            for role, actor in pairs(mission.gang) do
                local offset = actorOffsets[role]
                entries[#entries + 1] = {
                    vehicle = actor.bike, x = SAK.split[1] + offset[1], y = SAK.split[2] + offset[2],
                    scriptZ = SAK.split[3], heading = 250, occupants = completeSeatMap(actor.bike),
                }
            end
            beginVehicleRelocation(entries, function()
                setStage("ride1")
                trace("transition_advance", {objective = "split", afterPhysicalProbe = true,
                                              ballasExitGate = mission.ballasStarted == true})
                transitionParticipantStates("split_relocation_settled")
                for role, actor in pairs(mission.gang) do
                    transitionElementState("split_relocation_settled", role, actor.ped)
                    transitionElementState("split_relocation_settled", role .. "_bike", actor.bike)
                end
            end)
        end

        -- Exercise the real funeral-exit gate before the harness jumps to the
        -- distant split objective; otherwise a transition PASS could conceal
        -- a Voodoo that never enters the chase.
        setStage("ballas_exit_relocation")
        local exitEntries = {}
        local exitRoad = SAK.route1[2]
        for index, player in ipairs(participants()) do
            local bike = mission.playerBikes[player]
            exitEntries[#exitEntries + 1] = {
                -- Use an SCM route point whose road collision is known to be
                -- valid. Merely stepping outside the funeral volume can put a
                -- co-op lane on the neighbouring non-road footprint, which the
                -- transversal relocation barrier correctly refuses.
                vehicle = bike, x = exitRoad.x - (index - 1) * 2.5,
                y = exitRoad.y, scriptZ = exitRoad.z, heading = 90,
                occupants = completeSeatMap(bike),
            }
        end
        beginVehicleRelocation(exitEntries, function()
            setStage("ride1")
            startFirstBallasAttack(function()
                rememberTimer(setTimer(advanceToSplit, 500, 1))
            end)
        end)
    end)
end

local function transitionAdvanceRide2ToGrove()
    if not mission or mission.stage ~= "ride2" then
        return
    end
    -- The harness has already observed the real return-Ballas branch. Revoke
    -- both native owners before the discontinuous objective relocation so GTA
    -- cannot immediately drive Ryder or Smoke away from the Grove predicate.
    cancelCohort("gang")
    cancelCohort("ballas")
    setStage("grove_objective_relocation")
    local entries = {}
    local index = 0
    for _, player in ipairs(participants()) do
        index = index + 1
        local bike = mission.playerBikes[player]
        entries[#entries + 1] = {
            vehicle = bike,
            x = SAK.grove[1],
            y = SAK.grove[2] - (index - 1) * 3,
            scriptZ = SAK.grove[3],
            heading = 80,
            occupants = completeSeatMap(bike),
        }
    end
    for role, offset in pairs({ryder = 3, smoke = 5}) do
        local bike = mission.gang[role].bike
        entries[#entries + 1] = {
            vehicle = bike,
            x = SAK.grove[1] + offset,
            y = SAK.grove[2],
            scriptZ = SAK.grove[3],
            heading = 80,
            occupants = completeSeatMap(bike),
        }
    end
    beginVehicleRelocation(entries, function()
        setStage("ride2")
        mission.transitionGroveDiagnostic = 0
        trace("transition_advance", {objective = "grove", afterPhysicalProbe = true,
                                      returnBallas = mission.returnBallasStarted == true})
    end)
end

local function transitionAdvanceRide2()
    if mission and not mission.transitionReturnAudioStaged then
        mission.transitionReturnAudioStaged = true
        return waitTransitionCondition("return_ryder_audio", function()
            return mission and mission.stage == "ride2" and mission.returnRyderAudioDone == true
        end, function()
            cancelCohort("gang")
            setStage("return_smoke_audio_relocation")
            local smoke = mission.gang and mission.gang.smoke
            local target = SAK.splitStaging.smoke
            if not smoke or not isElement(smoke.bike) then
                return failMission("Smoke absent de la preuve audio retour")
            end
            beginVehicleRelocation({{
                vehicle = smoke.bike, x = target[1], y = target[2], scriptZ = target[3], heading = target[4],
                occupants = completeSeatMap(smoke.bike),
            }}, function()
                setStage("ride2")
                trace("transition_audio_staging", {name = "returnSmoke", proximity = true})
                transitionAdvanceRide2()
            end)
        end)
    end
    waitTransitionCondition("return_audio", function()
        return mission and mission.stage == "ride2" and mission.returnSmokeAudioDone == true
    end, function()
        if not mission or mission.stage ~= "ride2" then
            return
        end
        if not mission.cohorts.gang then
            local roles, routes = {"ryder", "smoke"}, {SAK.route2, SAK.smokeRoute2}
            if not mission.splitBallasActive then
                roles[#roles + 1], routes[#routes + 1] = "sweet", SAK.route2
            end
            local routeOk, routeReason = createRouteCohort("gang", roles, routes)
            if not routeOk then return failMission("cohorte de retour refusee: " .. tostring(routeReason)) end
        end
        local entries = {}
        for index, player in ipairs(participants()) do
            local bike = mission.playerBikes[player]
            entries[#entries + 1] = {
                vehicle = bike,
                x = 1981.2915 - index * 1.5,
                y = -1510.2186,
                scriptZ = 2.3844,
                heading = 270,
                occupants = completeSeatMap(bike),
            }
        end
        beginVehicleRelocation(entries, function()
            trace("transition_advance", {objective = "return_ballas_trigger", afterPhysicalProbe = true})
            local startedAt = getTickCount()
            local timer
            timer = rememberTimer(setTimer(function()
                if not mission or mission.stage ~= "ride2" then
                    if isTimer(timer) then killTimer(timer) end
                    return
                end
                if mission.returnBallasStarted then
                    killTimer(timer)
                    return rememberTimer(setTimer(transitionAdvanceRide2ToGrove, 500, 1))
                end
                if getTickCount() - startedAt > 10000 then
                    killTimer(timer)
                    failMission("transition: retour Ballas non declenche")
                end
            end, 100, 0))
        end)
    end)
end

local function prepareFuneral()
    setStage("funeral_setup")
    if not createGang() or not createBallas() or not createPlayerBikes() then
        return failMission("creation des acteurs des funerailles refusee")
    end
    mission.actors.gang = mission.gang
    mission.actors.ballas = mission.ballas
    for player in pairs(mission.players) do
        if isPedInVehicle(player) then
            removePedFromVehicle(player)
        end
        setElementPosition(player, 940.0, -1103.50, 22.85)
        setElementRotation(player, 0, 0, 272.5)
        setElementFrozen(player, player ~= mission.leader)
        setElementCollisionsEnabled(player, player == mission.leader)
        setElementAlpha(player, player == mission.leader and 255 or 0)
    end
    transitionParticipantStates("funeral_setup")
    for role, actor in pairs(mission.gang) do
        transitionElementState("funeral_setup", role, actor.ped)
        transitionElementState("funeral_setup", role .. "_bike", actor.bike)
    end
    transitionElementState("funeral_setup", "ballas_vehicle", mission.ballas.vehicle)
    beginBarrier("scene", "funeral", beginMountRide, mission.actors)
end

local function beginFuneralFile()
    local peren = mission.actors.peren
    if not isElement(peren) then return failMission("Peren absente avant les funerailles") end
    local smoke, leader = mission.actors.smoke, mission.leader
    -- The scene uses native enter-vehicle tasks on the owning client. Commit
    -- the synchronized seats here only as a bounded replication fallback,
    -- after the visual barrier has completed, never before it starts.
    if getPedOccupiedVehicle(leader) ~= peren then
        if isPedInVehicle(leader) then removePedFromVehicle(leader) end
        if not warpPedIntoVehicle(leader, peren, 0) then return failMission("entree CJ dans la Peren refusee") end
    end
    if isElement(smoke) and getPedOccupiedVehicle(smoke) ~= peren then
        if isPedInVehicle(smoke) then removePedFromVehicle(smoke) end
        if not warpPedIntoVehicle(smoke, peren, 1) then return failMission("entree Smoke dans la Peren refusee") end
    end
    setStage("funeral_vehicle_relocation")
    beginVehicleRelocation({{
        vehicle = peren,
        x = 956.60,
        y = -1099.47,
        scriptZ = 22.73,
        heading = 0,
        occupants = completeSeatMap(peren),
    }}, function()
        local smoke = mission.actors.smoke
        for player in pairs(mission.players) do
            if isPedInVehicle(player) then removePedFromVehicle(player) end
            setElementPosition(player, 910.78, -1075.26, 23.29)
            setElementRotation(player, 0, 0, 265.0)
        end
        if isElement(smoke) and isPedInVehicle(smoke) then removePedFromVehicle(smoke) end
        if isElement(smoke) then destroyElement(smoke) end
        mission.actors.smoke = nil
        beginFileCutscene(SAK.cutscenes.funeral, nil, prepareFuneral)
    end)
end

local function prepareSmokeScene()
    setStage("smoke_setup")
    local peren = track(createVehicle(SAK.models.peren, 2494.91, -1682.17, 12.82, 0, 0, 90.0))
    local smoke = track(createPed(SAK.models.smoke, 2498.91, -1680.5, 12.37, 90.0))
    if not peren or not smoke then
        return failMission("creation de la scene Smoke refusee")
    end
    setVehicleColor(peren, 0, 0, 0, 0, 0, 0)
    setElementHealth(peren, 2000)
    local leader = mission.leader
    setElementPosition(leader, 2495.59, -1686.96, 12.51)
    setElementRotation(leader, 0, 0, 32.0)
    for player in pairs(mission.players) do
        setElementFrozen(player, player ~= leader)
        setElementCollisionsEnabled(player, player == leader)
        setElementAlpha(player, player == leader and 255 or 0)
    end
    setElementSyncer(peren, leader, true, true)
    setElementSyncer(smoke, leader, true, true)
    mission.actors.peren, mission.actors.smoke = peren, smoke
    transitionParticipantStates("smoke_setup")
    transitionElementState("smoke_setup", "peren", peren)
    transitionElementState("smoke_setup", "smoke", smoke)
    beginBarrier("scene", "smoke", beginFuneralFile, mission.actors)
end

local function allPlayersMounted()
    for player, bike in pairs(mission.playerBikes or {}) do
        if not isElement(player) or not isElement(bike) or getPedOccupiedVehicle(player) ~= bike or
            getPedOccupiedVehicleSeat(player) ~= 0 then
            return false
        end
    end
    return true
end

local function releaseMountRide()
    if not mission then
        return
    end
    setStage("mount_bikes")
    for index, player in ipairs(participants()) do
        local position = SAK.playerBikes[((index - 1) % #SAK.playerBikes) + 1]
        if isPedInVehicle(player) then removePedFromVehicle(player) end
        setElementPosition(player, position[1] - 1.2, position[2], position[3])
        setElementRotation(player, 0, 0, index == 1 and SAK.funeral[4] or position[4])
        setElementAlpha(player, 255)
        setElementCollisionsEnabled(player, true)
        setElementFrozen(player, false)
        toggleAllControls(player, true, true, true)
    end
    for _, role in ipairs({"smoke", "ryder", "sweet"}) do
        local actor, config = mission.gang[role], SAK.gang[role]
        positionVehicle(actor.bike, config.bike)
        if getVehicleOccupant(actor.bike, 0) ~= actor.ped then
            warpPedIntoVehicle(actor.ped, actor.bike, 0)
        end
    end
    if not mission.cohorts.gang then
        local routeOk, routeReason = createRouteCohort("gang", {"smoke", "ryder", "sweet"},
                                                       {SAK.route1, SAK.route1, SAK.route1})
        if not routeOk then return failMission("cohorte BMX refusee: " .. tostring(routeReason)) end
    end
    triggerClientEvent(participants(), "sak:mountPrompt", resourceRoot)
    if mission.headless then
        for player, bike in pairs(mission.playerBikes) do warpPedIntoVehicle(player, bike, 0) end
        return beginRide()
    elseif mission.transition then
        return transitionCheckpoint("mount_bikes_controls", "foot_all", function()
            for player, bike in pairs(mission.playerBikes) do warpPedIntoVehicle(player, bike, 0) end
            beginRide()
        end)
    end
end

beginMountRide = function()
    if not mission or not mission.ballas or not isElement(mission.ballas.vehicle) then
        return failMission("Voodoo absente apres les funerailles")
    end
    local vehicle = mission.ballas.vehicle
    triggerClientEvent(participants(), "sak:stopRecording", resourceRoot, 201, vehicle)
    local x, y, z = getElementPosition(vehicle)
    local _, _, heading = getElementRotation(vehicle)
    setStage("post_funeral_relocation")
    beginVehicleRelocation({{
        vehicle = vehicle, x = x, y = y, centerZ = z - 50, heading = heading,
        occupants = completeSeatMap(vehicle), requireGround = false,
    }}, function()
        setElementFrozen(vehicle, true)
        releaseMountRide()
    end, {timeout = 20000, stableSamples = 3})
end

beginRide = function()
    if not mission or mission.stage ~= "mount_bikes" or not allPlayersMounted() then
        return
    end
    setStage("ride1")
    if not mission.cohorts.gang then
        local routeOk, routeReason = createRouteCohort("gang", {"smoke", "ryder", "sweet"},
                                                       {SAK.route1, SAK.route1, SAK.route1})
        if not routeOk then return failMission("cohorte BMX refusee: " .. tostring(routeReason)) end
    end
    transitionParticipantStates("ride1_release")
    if mission.headless then
        rememberTimer(setTimer(function()
            if not mission or mission.stage ~= "ride1" then return end
            -- Keep gameplay predicates paused until every short placement has
            -- restored its caller-owned frozen state. Entering split while a
            -- 750 ms placement timer is still live would race the following
            -- transactional relocation and produce a false physics failure.
            setStage("headless_split_staging")
            for player, bike in pairs(mission.playerBikes) do
                positionVehicle(bike, {SAK.split[1], SAK.split[2] - (player == mission.leader and 0 or 2), SAK.split[3], 250})
            end
            for _, actor in pairs(mission.gang) do
                positionVehicle(actor.bike, {SAK.split[1], SAK.split[2], SAK.split[3], 250})
            end
            rememberTimer(setTimer(function()
                if mission and mission.stage == "headless_split_staging" then setStage("ride1") end
            end, 850, 1))
        end, 3000, 1))
    elseif mission.transition then
        transitionCheckpoint("ride1_controls", "vehicle_all", transitionAdvanceRide1)
    end
end

local function elementInsideBox(element, x, y, z, halfX, halfY, halfZ)
    if not isElement(element) then return false end
    local ex, ey, ez = getElementPosition(element)
    return math.abs(ex - x) <= halfX and math.abs(ey - y) <= halfY and math.abs(ez - z) <= halfZ
end

local function allPlayersInsideBox(x, y, z, halfX, halfY, halfZ)
    for player in pairs(mission.players) do
        if not elementInsideBox(getPedOccupiedVehicle(player) or player, x, y, z, halfX, halfY, halfZ) then
            return false
        end
    end
    return true
end

startFirstBallasAttack = function(callback)
    if callback then
        mission.ballasStartCallbacks = mission.ballasStartCallbacks or {}
        mission.ballasStartCallbacks[#mission.ballasStartCallbacks + 1] = callback
    end
    if mission.ballasStarted then
        local callbacks = mission.ballasStartCallbacks or {}
        mission.ballasStartCallbacks = {}
        for _, queued in ipairs(callbacks) do queued() end
        return true
    end
    if mission.ballasStarting then return true end
    mission.ballasStarting = true
    local vehicle = mission.ballas and mission.ballas.vehicle
    if not isElement(vehicle) then
        mission.ballasStarting = false
        failMission("Voodoo absente au portail de sortie")
        return false
    end
    return beginVehicleRelocation({{
        vehicle = vehicle, x = SAK.ballas.chase[1], y = SAK.ballas.chase[2],
        scriptZ = SAK.ballas.chase[3], heading = SAK.ballas.chase[4],
        occupants = completeSeatMap(vehicle),
    }}, function()
        mission.ballasStarting, mission.ballasStarted = false, true
        -- The Voodoo is intentionally frozen while hidden. The relocation
        -- barrier proves its road contact first; only the mission releases it
        -- once both clients agree on the grounded transform and seats.
        setElementFrozen(vehicle, false)
        local ballasOk, ballasReason = startBallasCohort()
        if not ballasOk then
            mission.ballasStarted = false
            return failMission("cohorte Ballas refusee: " .. tostring(ballasReason))
        end
        trace("ballas_departure", {gate = "funeral_exit"})
        local callbacks = mission.ballasStartCallbacks or {}
        mission.ballasStartCallbacks = {}
        for _, queued in ipairs(callbacks) do queued() end
    end)
end

local function startReturnBallasAttack()
    if mission.returnBallasStarted or mission.returnBallasStarting then return true end
    local vehicle = mission.ballas and mission.ballas.vehicle
    if not isElement(vehicle) then
        failMission("Voodoo absente au retour Ballas")
        return false
    end
    mission.returnBallasStarting = true
    return beginVehicleRelocation({{
        vehicle = vehicle, x = SAK.ballas.returnChase[1], y = SAK.ballas.returnChase[2],
        scriptZ = SAK.ballas.returnChase[3], heading = SAK.ballas.returnChase[4],
        occupants = completeSeatMap(vehicle), requireGround = false,
    }}, function()
        mission.returnBallasStarting, mission.returnBallasStarted = false, true
        setElementFrozen(vehicle, false)
        local ok, reason = startBallasCohort()
        if not ok then return failMission("retour Ballas refuse: " .. tostring(reason)) end
        trace("ballas_return", {})
    end)
end

local function startSplitScene()
    cancelCohort("gang")
    cancelCohort("ballas")
    setStage("split_relocation")
    local entries = {}
    for index, player in ipairs(participants()) do
        local bike = mission.playerBikes[player]
        local staging = SAK.splitStaging.player
        toggleAllControls(player, false, true, true)
        entries[#entries + 1] = {
            vehicle = bike,
            x = staging[1],
            y = staging[2] + (index - 1) * 3,
            scriptZ = staging[3],
            heading = staging[4],
            occupants = completeSeatMap(bike),
        }
    end
    for _, role in ipairs({"smoke", "ryder", "sweet"}) do
        local actor = mission.gang[role]
        local staging = SAK.splitStaging[role]
        if isElement(actor.bike) and not isVehicleBlown(actor.bike) then
            entries[#entries + 1] = {
                vehicle = actor.bike,
                x = staging[1], y = staging[2], scriptZ = staging[3], heading = staging[4],
                occupants = completeSeatMap(actor.bike),
            }
        else
            setElementPosition(actor.ped, staging[1], staging[2], staging[3] + 1)
            setElementRotation(actor.ped, 0, 0, staging[4])
        end
    end
    local ballasStaging = SAK.splitStaging.ballas
    if mission.ballas and isElement(mission.ballas.vehicle) and not isVehicleBlown(mission.ballas.vehicle) then
        entries[#entries + 1] = {
            vehicle = mission.ballas.vehicle,
            x = ballasStaging[1], y = ballasStaging[2], scriptZ = ballasStaging[3], heading = ballasStaging[4],
            occupants = completeSeatMap(mission.ballas.vehicle),
        }
    end
    beginVehicleRelocation(entries, function()
        setStage("split_setup")
        for _, player in ipairs(participants()) do setElementFrozen(mission.playerBikes[player], true) end
        local splitOk, splitReason = startSplitDepartureCohort()
        mission.splitBallasActive = splitOk == true
        mission.actors.splitBallasActive = mission.splitBallasActive
        if not splitOk then trace("split_branch", {ballasActive = false, reason = splitReason})
        else trace("split_branch", {ballasActive = true}) end
        transitionParticipantStates("split_setup")
        for role, actor in pairs(mission.gang) do
            transitionElementState("split_setup", role, actor.ped)
            transitionElementState("split_setup", role .. "_bike", actor.bike)
        end
        if mission.ballas then transitionElementState("split_setup", "ballas_vehicle", mission.ballas.vehicle) end
        beginBarrier("scene", "split", beginReturnRide, mission.actors)
    end)
end

local function releaseReturnRide()
    setStage("ride2")
    for _, player in ipairs(participants()) do
        local bike = mission.playerBikes[player]
        if getVehicleOccupant(bike, 0) ~= player then
            warpPedIntoVehicle(player, bike, 0)
        end
        setElementFrozen(player, false)
        setElementFrozen(bike, false)
        setElementCollisionsEnabled(bike, true)
        setElementVelocity(bike, 0, 0, 0)
        setElementAngularVelocity(bike, 0, 0, 0)
        toggleAllControls(player, true, true, true)
    end
    local roles, routes = {"ryder", "smoke"}, {SAK.route2, SAK.smokeRoute2}
    if not mission.splitBallasActive then
        roles[#roles + 1], routes[#routes + 1] = "sweet", SAK.route2
    end
    local ok, reason = createRouteCohort("gang", roles, routes)
    if not ok then
        return failMission("cohorte de retour refusee: " .. tostring(reason))
    end
    transitionParticipantStates("ride2_release")
    if mission.headless then
        rememberTimer(setTimer(function()
            if not mission or mission.stage ~= "ride2" then return end
            setStage("headless_grove_staging")
            for player, bike in pairs(mission.playerBikes) do
                positionVehicle(bike, {SAK.grove[1], SAK.grove[2], SAK.grove[3] + 0.6, 80})
            end
            positionVehicle(mission.gang.ryder.bike, {SAK.grove[1] + 3, SAK.grove[2], SAK.grove[3] + 0.6, 80})
            positionVehicle(mission.gang.smoke.bike, {SAK.grove[1] + 5, SAK.grove[2], SAK.grove[3] + 0.6, 80})
            rememberTimer(setTimer(function()
                if mission and mission.stage == "headless_grove_staging" then setStage("ride2") end
            end, 850, 1))
        end, 3000, 1))
    elseif mission.transition then
        transitionCheckpoint("ride2_controls", "vehicle_all", transitionAdvanceRide2)
    end
end

beginReturnRide = function()
    cancelCohort("split_departure")
    if not mission.splitBallasActive then return releaseReturnRide() end
    local sweetBike = mission.gang.sweet.bike
    local ballasVehicle = mission.ballas and mission.ballas.vehicle
    if not isElement(sweetBike) or not isElement(ballasVehicle) then
        mission.splitBallasActive = false
        return releaseReturnRide()
    end
    setStage("post_split_relocation")
    beginVehicleRelocation({
        {vehicle = sweetBike, x = 1540.2981, y = -1159.1885, scriptZ = -50.3438, heading = 170,
         occupants = completeSeatMap(sweetBike), requireGround = false},
        {vehicle = ballasVehicle, x = 1540.2981, y = -1171.1885, scriptZ = -50.3438, heading = 182,
         occupants = completeSeatMap(ballasVehicle), requireGround = false},
    }, function()
        setElementFrozen(sweetBike, true)
        setElementFrozen(ballasVehicle, true)
        releaseReturnRide()
    end, {timeout = 20000, stableSamples = 3})
end

local function stageFinaleActors(callback)
    cancelCohort("gang")
    cancelCohort("ballas")
    local entries = {}
    for index, player in ipairs(participants()) do
        local bike = mission.playerBikes[player]
        toggleAllControls(player, false, true, true)
        if getPedOccupiedVehicle(player) == bike then
            entries[#entries + 1] = {
                vehicle = bike, x = 2487.3093, y = -1668.3717 - (index - 1) * 3,
                scriptZ = 12.3438, heading = 80.0, occupants = completeSeatMap(bike),
            }
        else
            if isPedInVehicle(player) then removePedFromVehicle(player) end
            setElementPosition(player, 2487.3093, -1668.3717 - (index - 1) * 3, 12.3438 + 1)
            setElementRotation(player, 0, 0, 80.0)
        end
    end
    for role, target in pairs({
        smoke = {2487.1721, -1666.3010, 12.3438, 124.2698},
        sweet = {2371.5002, -1654.8945, 12.3826, nil},
    }) do
        local bike = mission.gang[role].bike
        entries[#entries + 1] = {
            vehicle = bike,
            x = target[1], y = target[2], scriptZ = target[3], heading = target[4],
            occupants = completeSeatMap(bike),
            requireGround = role ~= "sweet",
        }
    end
    beginVehicleRelocation(entries, function()
        if mission.ballas then
            for _, element in ipairs({mission.ballas.driver, mission.ballas.passenger, mission.ballas.vehicle}) do
                if isElement(element) then destroyElement(element) end
            end
            mission.ballas = nil
            mission.actors.ballas = nil
        end
        for _, player in ipairs(participants()) do
            setElementFrozen(mission.playerBikes[player], true)
            setElementAlpha(player, player == mission.leader and 255 or 0)
        end
        for _, role in ipairs({"smoke", "ryder", "sweet"}) do
            local actor = mission.gang[role]
            setElementFrozen(actor.bike, false)
            setElementSyncer(actor.bike, mission.leader, true, true)
            setElementSyncer(actor.ped, mission.leader, true, true)
        end
        transitionParticipantStates("finale_setup")
        for role, actor in pairs(mission.gang) do
            transitionElementState("finale_setup", role, actor.ped)
            transitionElementState("finale_setup", role .. "_bike", actor.bike)
        end
        callback()
    end)
end

local function passMission()
    if not mission or mission.finishing then
        return
    end
    local finalOk = true
    mission.preserveOnSuccess = {}
    local allOccupied = true
    for index, player in ipairs(participants()) do
        local bike = mission.playerBikes[player]
        if not isElement(bike) then return failMission("BMX final de participant absent", "INT2_F4") end
        local occupied = getPedOccupiedVehicle(player) == bike and getPedOccupiedVehicleSeat(player) == 0
        allOccupied = allOccupied and occupied
        setElementInterior(bike, 0)
        setElementDimension(bike, 0)
        setElementInterior(player, 0)
        setElementDimension(player, 0)
        if occupied then setElementRotation(bike, 0, 0, 82.97)
        else setElementRotation(player, 0, 0, 82.97) end
        setElementVelocity(bike, 0, 0, 0)
        setElementAngularVelocity(bike, 0, 0, 0)
        setElementFrozen(bike, false)
        setElementCollisionsEnabled(bike, true)
        setElementAlpha(player, 255)
        setElementCollisionsEnabled(player, true)
        setElementFrozen(player, false)
        toggleAllControls(player, true, true, true)
        if type(setPlayerWantedLevel) == "function" then setPlayerWantedLevel(player, 0) end
        local spatial = occupied and bike or player
        local x, y, z = getElementPosition(spatial)
        local _, _, heading = getElementRotation(spatial)
        finalOk = finalOk and getElementInterior(player) == 0 and getElementDimension(player) == 0 and
                      getElementInterior(bike) == 0 and getElementDimension(bike) == 0 and
                      not isElementFrozen(player) and not isElementFrozen(bike) and
                      math.abs(x - 2487.3093) <= 0.2 and
                      math.abs(y - (-1668.3717 - (index - 1) * 3)) <= 0.2 and
                      math.abs(z - (12.3438 + 0.7)) <= 0.5 and
                      math.abs((heading - 82.97 + 180) % 360 - 180) <= 0.5
        mission.preserveOnSuccess[bike] = true
    end
    trace("vanilla_success_state", {ok = finalOk, heading = 82.97, occupied = allOccupied, dimension = 0})
    if not finalOk then return failMission("etat final Grove refuse") end
    mission.finishing = true
    setStage("passed", {respect = 3})
    if mission.transition then
        trace("pass_requested", {respect = 3, profile = mission.transitionProfile})
        triggerClientEvent(participants(), "sak:passed", resourceRoot, 3)
        return rememberTimer(setTimer(function()
            clearMission(true, {ok = true})
        end, 500, 1))
    end
    trace("PASS", {respect = 3})
    outputServerLog("[sweet-and-kendl] PASS: INTRO1 gameplay, two rides and synchronized finale completed")
    triggerClientEvent(participants(), "sak:passed", resourceRoot, 3)
    rememberTimer(setTimer(function()
        clearMission(false)
    end, mission.headless and 500 or 5000, 1))
end

beginFinale = function()
    setStage("finale_relocation")
    stageFinaleActors(function()
        setStage("finale_setup")
        beginBarrier("scene", "finale", function()
            for _, role in ipairs({"smoke", "ryder", "sweet"}) do
                local actor = mission.gang and mission.gang[role]
                if actor then
                    if isElement(actor.ped) then destroyElement(actor.ped) end
                    if isElement(actor.bike) then destroyElement(actor.bike) end
                end
            end
            trace("gang_cleanup", {beforeSaveTutorial = true})
            beginBarrier("scene", "save_tutorial", passMission, mission.actors)
        end, mission.actors)
    end)
end

local function validateGameplay()
    if not mission or mission.finishing then
        return
    end
    if mission.stage == "mount_bikes" then
        if allPlayersMounted() then beginRide() end
        return
    elseif mission.stage ~= "ride1" and mission.stage ~= "ride2" then
        return
    end
    local living = 0
    for player, bike in pairs(mission.playerBikes) do
        if isElement(player) and not isPedDead(player) then
            living = living + 1
        end
        if not isElement(bike) or isVehicleBlown(bike) then
            return failMission("BMX de participant detruit", "INT2_F4")
        end
    end
    if living == 0 then
        return failMission("toute la partie est morte")
    end
    local requiredGang = mission.stage == "ride1" and {"smoke", "ryder", "sweet"} or {"smoke", "ryder"}
    for _, role in ipairs(requiredGang) do
        local actor = mission.gang[role]
        if not actor or not isElement(actor.ped) or isPedDead(actor.ped) then
            local key = role == "smoke" and "INT2_F1" or role == "sweet" and "INT2_F2" or "INT2_F3"
            return failMission(role .. " est mort", key)
        end
    end
    if mission.stage == "ride1" then
        local sweetSpatial = isElement(mission.gang.sweet.bike) and mission.gang.sweet.bike or mission.gang.sweet.ped
        if not mission.ride1AudioStarted then
            local sx, sy, sz = getElementPosition(sweetSpatial)
            if allPlayersInsideBox(sx, sy, sz, 20, 20, 10) then
                mission.ride1AudioStarted = beginAmbientSequence("ride1", "ride1", function()
                    if mission and mission.stage == "ride1" then mission.ride1AudioDone = true end
                end)
            end
        end
        local leaderSpatial = getPedOccupiedVehicle(mission.leader) or mission.leader
        if not mission.ballasStarted and
            not elementInsideBox(leaderSpatial, SAK.funeral[1], SAK.funeral[2], SAK.funeral[3], 50, 60, 25) then
            if not startFirstBallasAttack() then return end
        end
        if mission.ballasStarting then return end
        if allPlayersInsideBox(SAK.split[1], SAK.split[2], SAK.split[3], 10, 12, 20) and
            elementInsideBox(sweetSpatial, SAK.split[1], SAK.split[2], SAK.split[3], 10, 12, 20) then
            return startSplitScene()
        end
        local leaderBike, ballas = mission.playerBikes[mission.leader], mission.ballas
        if mission.ballasStarted and isElement(leaderBike) and ballas and isElement(ballas.vehicle) then
            local px, py, pz = getElementPosition(leaderBike)
            local bx, by, bz = getElementPosition(ballas.vehicle)
            if getDistanceBetweenPoints3D(px, py, pz, bx, by, bz) > 90 then
                setElementPosition(ballas.vehicle, px, py - 40, pz + 1)
                setElementRotation(ballas.vehicle, 0, 0, 0)
                setElementVelocity(ballas.vehicle, 0, 0.28, 0)
                trace("ballas_rubberband", {distance = getDistanceBetweenPoints3D(px, py, pz, bx, by, bz)})
            end
        end
    else
        local ryderSpatial = isElement(mission.gang.ryder.bike) and mission.gang.ryder.bike or mission.gang.ryder.ped
        if not mission.returnRyderAudioStarted then
            local x, y, z = getElementPosition(ryderSpatial)
            if allPlayersInsideBox(x, y, z, 20, 20, 10) then
                mission.returnRyderAudioStarted = beginAmbientSequence("returnRyder", "returnRyder", function()
                    if mission and mission.stage == "ride2" then mission.returnRyderAudioDone = true end
                end)
            end
        elseif mission.returnRyderAudioDone and not mission.returnSmokeAudioStarted then
            local smokeSpatial = isElement(mission.gang.smoke.bike) and mission.gang.smoke.bike or
                                     mission.gang.smoke.ped
            local x, y, z = getElementPosition(smokeSpatial)
            if allPlayersInsideBox(x, y, z, 20, 20, 10) then
                mission.returnSmokeAudioStarted = beginAmbientSequence("returnSmoke", "returnSmoke", function()
                    if mission and mission.stage == "ride2" then mission.returnSmokeAudioDone = true end
                end)
            end
        end
        if not mission.returnBallasStarted then
            local x, y, z = getElementPosition(getPedOccupiedVehicle(mission.leader) or mission.leader)
            if getDistanceBetweenPoints3D(x, y, z, 1981.2915, -1510.2186, 2.3844) <= 55 then
                startReturnBallasAttack()
            end
        end
        if mission.returnBallasStarting then return end
        local rx, ry, rz = getElementPosition(ryderSpatial)
        if mission.transition and mission.transitionGroveDiagnostic and mission.transitionGroveDiagnostic < 8 then
            mission.transitionGroveDiagnostic = mission.transitionGroveDiagnostic + 1
            local distances = {}
            for _, player in ipairs(participants()) do
                local spatialElement = getPedOccupiedVehicle(player) or player
                local px, py, pz = getElementPosition(spatialElement)
                distances[getPlayerName(player)] = getDistanceBetweenPoints3D(px, py, pz,
                                                                               SAK.grove[1], SAK.grove[2], SAK.grove[3])
            end
            trace("grove_gate_sample", {
                sample = mission.transitionGroveDiagnostic,
                ryderDistance = getDistanceBetweenPoints3D(rx, ry, rz, SAK.grove[1], SAK.grove[2], SAK.grove[3]),
                participantDistances = toJSON(distances, true),
            })
        end
        if allPlayersInsideBox(SAK.grove[1], SAK.grove[2], SAK.grove[3], 4, 3.5, 4) and
            elementInsideBox(ryderSpatial, SAK.grove[1], SAK.grove[2], SAK.grove[3], 60, 60, 20) then
            return beginFinale()
        end
    end
end

local function startMission(leader, headless, options)
    options = type(options) == "table" and options or {}
    if mission then
        return false, "une mission est deja active"
    end
    if not isElement(leader) or getElementType(leader) ~= "player" then
        return false, "aucun joueur disponible"
    end
    local connected = getElementsByType("player")
    if options.expectedPlayers and #connected ~= options.expectedPlayers then
        return false, ("profil exige %d joueur(s), %d connecte(s)"):format(options.expectedPlayers, #connected)
    end
    serial = serial + 1
    mission = {
        id = serial, leader = leader, headless = headless == true, stage = "starting", players = {}, entities = {},
        transition = options.transition == true, transitionSkip = options.skip == true,
        transitionProfile = options.profile, expectedPlayers = options.expectedPlayers,
        timers = {}, cohorts = {}, vehicleRelocations = {}, positionRelocations = {}, actors = {}, sceneSerial = 0,
        finishing = false,
    }
    for _, player in ipairs(getElementsByType("player")) do
        mission.players[player] = snapshotPlayer(player)
        if isPedInVehicle(player) then removePedFromVehicle(player) end
        setElementInterior(player, 0)
        setElementDimension(player, SAK.dimension)
        setElementFrozen(player, true)
        setElementCollisionsEnabled(player, false)
        toggleAllControls(player, false, true, true)
    end
    local modelLease, modelLeaseReason = exports["story-world-runtime"]:createStoryPlayerModelLease(participants(), 0)
    if not modelLease then
        for player, state in pairs(mission.players) do
            restorePlayer(player, state)
        end
        mission = nil
        return false, "preparation du modele CJ refusee: " .. tostring(modelLeaseReason)
    end
    mission.playerModelLease = modelLease
    trace("start", {leader = getPlayerName(leader), players = #participants(), headless = mission.headless,
                     transition = mission.transition, profile = mission.transitionProfile})
    transitionParticipantStates("intro_staging")
    triggerClientEvent(participants(), "sak:start", resourceRoot, mission.id, leader, mission.headless,
                       {transition = mission.transition, profile = mission.transitionProfile})
    if mission.headless then
        prepareFuneral()
    else
        beginFileCutscene(SAK.cutscenes.intro, 3, prepareSmokeScene)
    end
    mission.monitor = rememberTimer(setTimer(validateGameplay, 250, 0))
    return true
end

addEvent("sak:barrierDone", true)
addEventHandler("sak:barrierDone", resourceRoot, function(id, ok, reason)
    local barrier = mission and mission.barrier
    if source ~= resourceRoot or not barrier or id ~= barrier.id or not barrier.waiting[client] then
        return
    end
    trace("barrier_done", {kind = barrier.kind, name = barrier.name, player = getPlayerName(client), ok = ok == true})
    if ok ~= true then
        return failMission(barrier.kind .. " " .. barrier.name .. ": " .. tostring(reason))
    end
    barrier.waiting[client] = nil
    for player in pairs(barrier.waiting) do
        if isElement(player) then
            return
        end
    end
    mission.barrier = nil
    barrier.callback()
end)

addEvent("sak:ambientAudioDone", true)
addEventHandler("sak:ambientAudioDone", resourceRoot, function(id, token, name)
    local sequence = mission and mission.ambientSequence
    if source ~= resourceRoot or not sequence or mission.id ~= tonumber(id) or
        sequence.token ~= tostring(token) or sequence.name ~= tostring(name) or not sequence.waiting[client] then
        return
    end
    sequence.waiting[client] = nil
    for player in pairs(sequence.waiting) do
        if isElement(player) then return end
    end
    if isTimer(sequence.timeout) then killTimer(sequence.timeout) end
    mission.ambientSequence = nil
    trace("audio_complete", {name = sequence.name, token = sequence.token})
    if sequence.callback then sequence.callback() end
end)

addEvent("sak:funeralExplosion", true)
addEventHandler("sak:funeralExplosion", resourceRoot, function(id)
    if source ~= resourceRoot or not mission or mission.id ~= tonumber(id) or client ~= mission.leader or
        mission.stage ~= "scene:funeral" or mission.funeralExplosionCommitted then
        return
    end
    local peren = mission.actors.peren
    if not isElement(peren) then return failMission("Peren absente au moment de l'explosion") end
    mission.funeralExplosionCommitted = true
    blowVehicle(peren)
    trace("funeral_explosion", {afterAI = true})
end)

local function broadcastCutsceneSkip()
    if not mission or not mission.fileCutscene or not isElement(mission.fileCutscene.handle) then
        return false
    end
    return exports["story-world-runtime"]:skipStoryFileCutscene(mission.fileCutscene.handle) == true
end

addEventHandler("onStoryFileCutsceneStateChange", root, function(state, snapshot)
    local active = mission and mission.fileCutscene
    if not active or source ~= active.handle then
        return
    end
    snapshot = type(snapshot) == "table" and snapshot or {}
    trace("file_cutscene", {
        name = active.name,
        state = state,
        loaded = snapshot.loaded,
        started = snapshot.started,
        finished = snapshot.finished,
        released = snapshot.released,
        skipped = snapshot.skipped == true,
        reason = snapshot.reason,
    })
    if state == "failed" then
        return failMission("cutscene fichier " .. tostring(active.name) .. ": " .. tostring(snapshot.reason))
    elseif state == "started" and mission.transition and mission.transitionSkip and not active.skipIssued then
        active.skipIssued = true
        rememberTimer(setTimer(function()
            if mission and mission.fileCutscene == active then
                trace("cutscene_skip", {name = active.name, source = "transition-profile"})
                if not broadcastCutsceneSkip() then
                    failMission("skip groupe refuse: " .. tostring(active.name))
                end
            end
        end, 250, 1))
    elseif state == "released" then
        local callback, handle = active.callback, active.handle
        mission.fileCutscene = nil
        if isElement(handle) then
            exports["story-world-runtime"]:releaseStoryFileCutscene(handle)
        end
        rememberTimer(setTimer(function()
            if mission and not mission.finishing then
                callback()
            end
        end, 50, 1))
    end
end)

addEventHandler("onStoryVehicleRelocationStateChange", root, function(state, snapshot)
    local active = mission and mission.vehicleRelocations and mission.vehicleRelocations[source]
    if not active then return end
    snapshot = type(snapshot) == "table" and snapshot or {}
    trace("vehicle_relocation", {
        state = state,
        phase = snapshot.phase,
        generation = snapshot.generation,
        reason = snapshot.reason,
        createdAt = snapshot.createdAt,
        movedAt = snapshot.movedAt,
        verifiedAt = snapshot.verifiedAt,
    })
    if state == "failed" then
        mission.vehicleRelocations[source] = nil
        if isElement(source) then exports["story-world-runtime"]:releaseStoryVehicleRelocation(source) end
        return failMission("relocalisation vehicule: " .. tostring(snapshot.reason))
    elseif state ~= "ready" then
        return
    end
    local callback, handle = active.callback, source
    mission.vehicleRelocations[handle] = nil
    if isElement(handle) then exports["story-world-runtime"]:releaseStoryVehicleRelocation(handle) end
    rememberTimer(setTimer(function()
        if mission and not mission.finishing then callback(snapshot) end
    end, 50, 1))
end)

addEvent("sak:transitionCheckpointDone", true)
addEventHandler("sak:transitionCheckpointDone", resourceRoot, function(id, probeId, name, ok, reason, data)
    local checkpoint = mission and mission.transitionCheckpoint
    if source ~= resourceRoot or not checkpoint or mission.id ~= tonumber(id) or
        checkpoint.probeId ~= tonumber(probeId) or checkpoint.name ~= tostring(name) or
        not checkpoint.waiting[client] then
        return
    end
    data = type(data) == "table" and data or {}
    local controlsOk, disabledControl = controlsRestored(client)
    trace("transition_checkpoint", {probeId = checkpoint.probeId, name = checkpoint.name, kind = checkpoint.kind,
                                     phase = "client", player = getPlayerName(client), ok = ok == true,
                                     reason = reason, raw = tonumber(data.raw), processed = tonumber(data.processed),
                                     displacement = tonumber(data.displacement), samples = tonumber(data.samples),
                                     keyObserved = data.keyObserved == true, targetLocal = data.targetLocal == true,
                                     controls = controlsOk, occupied = data.occupied == true,
                                     seat = tonumber(data.seat), streamed = data.streamed == true,
                                     chatControl = isControlEnabled(client, "chatbox")})
    local function reject(detail)
        transitionTrace("INPUT_FAIL", {player = getPlayerName(client), probeId = checkpoint.probeId,
                                        checkpoint = checkpoint.name, kind = checkpoint.kind, reason = detail,
                                        raw = tonumber(data.raw), processed = tonumber(data.processed),
                                        displacement = tonumber(data.displacement), samples = tonumber(data.samples),
                                        keyObserved = data.keyObserved == true})
        return failMission(detail)
    end
    if ok ~= true then
        return reject("checkpoint " .. checkpoint.name .. ": " .. tostring(reason))
    end
    if not controlsOk then
        return reject("controle encore desactive: " .. tostring(disabledControl))
    end
    if data.targetLocal ~= true then
        return reject("preuve camera incomplete: " .. checkpoint.name)
    end
    local occupiedVehicle = getPedOccupiedVehicle(client)
    local serverSeat = isElement(occupiedVehicle) and getPedOccupiedVehicleSeat(client) or -1
    if checkpoint.kind == "vehicle_all" then
        if not isElement(occupiedVehicle) or data.occupied ~= true or data.streamed ~= true or
            tonumber(data.seat) ~= serverSeat or serverSeat ~= 0 then
            return reject("preuve conducteur incomplete: " .. checkpoint.name)
        end
    elseif checkpoint.kind == "foot_all" and (isElement(occupiedVehicle) or data.occupied == true) then
        return reject("preuve a pied incomplete: " .. checkpoint.name)
    end
    if data.keyObserved ~= true or (tonumber(data.raw) or 0) <= 0.8 or
        (tonumber(data.processed) or 0) <= 0.8 or (tonumber(data.displacement) or 0) <= 0.5 or
        (tonumber(data.samples) or 0) < 3 then
        return reject("preuve physique incomplete: " .. checkpoint.name)
    end
    transitionTrace("INPUT_PASS", {player = getPlayerName(client), probeId = checkpoint.probeId,
                                    checkpoint = checkpoint.name, kind = checkpoint.kind,
                                    raw = tonumber(data.raw), processed = tonumber(data.processed),
                                    displacement = tonumber(data.displacement), samples = tonumber(data.samples),
                                    keyObserved = data.keyObserved == true})
    checkpoint.waiting[client] = nil
    for player in pairs(checkpoint.waiting) do
        if isElement(player) then
            return
        end
    end
    if isTimer(checkpoint.timeout) then
        killTimer(checkpoint.timeout)
    end
    transitionTrace("CHECKPOINT_PASS", {probeId = checkpoint.probeId, checkpoint = checkpoint.name,
                                         kind = checkpoint.kind, players = #participants()})
    mission.transitionCheckpoint = nil
    local callback = checkpoint.callback
    rememberTimer(setTimer(function()
        if mission and not mission.finishing then
            callback()
        end
    end, 50, 1))
end)

addEvent("sak:transitionInputReady", true)
addEventHandler("sak:transitionInputReady", resourceRoot, function(id, probeId, name, control)
    local checkpoint = mission and mission.transitionCheckpoint
    if source ~= resourceRoot or not checkpoint or mission.id ~= tonumber(id) or
        checkpoint.probeId ~= tonumber(probeId) or checkpoint.name ~= tostring(name) or
        not checkpoint.waiting[client] or tostring(control) ~= checkpoint.control then
        return
    end
    transitionTrace("INPUT_READY", {player = getPlayerName(client), probeId = checkpoint.probeId,
                                     checkpoint = checkpoint.name, kind = checkpoint.kind,
                                     control = checkpoint.control, key = "w"})
end)

addEvent("sak:cleanupDone", true)
addEventHandler("sak:cleanupDone", resourceRoot, function(id, ok, reason, data)
    if source ~= resourceRoot or not mission or not mission.cleanupWaiting or mission.id ~= tonumber(id) or
        not mission.cleanupWaiting[client] then
        return
    end
    local expected = mission.players[client]
    local controlsOk, disabledControl = controlsRestored(client)
    local serverRestored = expected and restoredPlayerMatches(client, expected)
    data = type(data) == "table" and data or {}
    trace("cleanup", {player = getPlayerName(client), ok = ok == true, serverRestored = serverRestored,
                       controls = controlsOk, chatControl = isControlEnabled(client, "chatbox"),
                       targetLocal = data.targetLocal == true, reason = reason})
    if ok ~= true or not serverRestored or not controlsOk or data.targetLocal ~= true then
        local detail = reason or (not serverRestored and "server state") or
                           (not controlsOk and ("control " .. tostring(disabledControl))) or "camera target"
        return finishTransitionTerminal(false, "cleanup invalide: " .. tostring(detail))
    end
    mission.cleanupWaiting[client] = nil
    for player in pairs(mission.cleanupWaiting) do
        if isElement(player) then
            return
        end
    end
    if isTimer(mission.cleanupTimeout) then
        killTimer(mission.cleanupTimeout)
    end
    local terminal = mission.terminal
    finishTransitionTerminal(terminal and terminal.ok == true, terminal and terminal.reason)
end)

-- Runtime dependencies can be restarted independently of this mission
-- resource. Declare their lifecycle events locally so load order never makes
-- the handlers disappear, then fail through the same bounded cleanup path.
local function failForRuntimeLifecycle(reason)
    if not mission or mission.cleaning then return end
    mission.finishing = true
    setStage("failed", {reason = reason})
    trace("failure_requested", {reason = reason, lifecycle = true})
    if mission.transition then
        clearMission(true, {ok = false, reason = reason})
    else
        outputServerLog("[sweet-and-kendl] FAIL: " .. reason)
        clearMission(true)
    end
end

addEventHandler("onStoryWorldRuntimeStopping", root, function()
    if mission then mission.storyWorldStopping = true end
    failForRuntimeLifecycle("story-world-runtime lifecycle stop")
end)

addEventHandler("onNativeTaskRuntimeStopping", root, function()
    if mission then mission.nativeTaskStopping = true end
    failForRuntimeLifecycle("native-task-runtime lifecycle stop")
end)

addEventHandler("onNativeTaskCohortStateChange", root, function(state, data)
    if not mission then
        return
    end
    local owned = false
    for _, handle in pairs(mission.cohorts) do
        owned = owned or source == handle
    end
    if not owned then
        return
    end
    trace("cohort", {state = state, epoch = data.epoch, reason = data.reason})
    if state == "failed" then
        failMission("runtime de cohorte: " .. tostring(data.reason))
    end
end)

local function commandLeader(source)
    if isElement(source) and getElementType(source) == "player" then
        return source
    end
    return getElementsByType("player")[1]
end

addCommandHandler(SAK.command, function(player)
    local ok, reason = startMission(commandLeader(player), false)
    if ok == false and isElement(player) and getElementType(player) == "player" then
        outputChatBox("[Sweet & Kendl] " .. tostring(reason), player, 255, 100, 80)
    elseif ok == false then
        outputServerLog("[sweet-and-kendl] NATURAL NOT STARTED: " .. tostring(reason))
    end
end)

addCommandHandler("sweetandkendlnatural", function(player)
    local leader = commandLeader(player)
    local ok, reason = startMission(leader, false)
    if ok == false then
        outputServerLog("[sweet-and-kendl] NATURAL NOT STARTED: " .. tostring(reason))
    end
end)

addCommandHandler("sweetandkendlskip", function(player)
    if not isElement(player) or getElementType(player) ~= "player" or (mission and player == mission.leader) then
        broadcastCutsceneSkip()
    end
end)

addCommandHandler("sweetandkendltest", function(player)
    local leader = commandLeader(player)
    local ok, reason = startMission(leader, true)
    if ok == false then
        outputServerLog("[sweet-and-kendl] HEADLESS NOT STARTED: " .. tostring(reason))
    end
end)

local function startTransition(source, skip, expectedText)
    local connected = getElementsByType("player")
    local expected = tonumber(expectedText) or #connected
    if expected ~= 1 and expected ~= 2 then
        return outputServerLog("[sweet-and-kendl] TRANSITION NOT STARTED: le nombre de clients doit etre 1 ou 2")
    end
    local profile = (expected == 1 and "solo" or "coop") .. (skip and "-skip" or "-natural")
    local ok, reason = startMission(commandLeader(source), false, {
        transition = true,
        skip = skip,
        expectedPlayers = expected,
        profile = profile,
    })
    if ok == false then
        outputServerLog("[sweet-and-kendl] TRANSITION NOT STARTED: " .. tostring(reason))
    end
end

addCommandHandler("sweetandkendltransitionnatural", function(source, _, expected)
    startTransition(source, false, expected)
end)

addCommandHandler("sweetandkendltransitionskip", function(source, _, expected)
    startTransition(source, true, expected)
end)

-- The VM harness can request the exact same bounded path as the console command
-- without depending on a graphical console or an interactive stdin bridge.
local headlessRequestPath = "headless.request"
setTimer(function()
    local players = getElementsByType("player")
    if fileExists("skip.request") then
        fileDelete("skip.request")
        broadcastCutsceneSkip()
    end
    if not mission and #players == 1 and fileExists("transition-natural-1.request") then
        fileDelete("transition-natural-1.request")
        startMission(players[1], false, {transition = true, expectedPlayers = 1, profile = "solo-natural"})
    elseif not mission and #players == 2 and fileExists("transition-natural-2.request") then
        fileDelete("transition-natural-2.request")
        startMission(players[1], false, {transition = true, expectedPlayers = 2, profile = "coop-natural"})
    elseif not mission and #players == 1 and fileExists("transition-skip-1.request") then
        fileDelete("transition-skip-1.request")
        startMission(players[1], false, {transition = true, skip = true, expectedPlayers = 1,
                                         profile = "solo-skip"})
    elseif not mission and #players == 2 and fileExists("transition-skip-2.request") then
        fileDelete("transition-skip-2.request")
        startMission(players[1], false, {transition = true, skip = true, expectedPlayers = 2,
                                         profile = "coop-skip"})
    elseif #players >= 2 and fileExists("natural.request") then
        fileDelete("natural.request")
        local ok, reason = startMission(players[1], false)
        if ok == false then
            outputServerLog("[sweet-and-kendl] NATURAL REQUEST REJECTED: " .. tostring(reason))
        end
    elseif #players >= 2 and fileExists(headlessRequestPath) then
        fileDelete(headlessRequestPath)
        local ok, reason = startMission(players[1], true)
        if ok == false then
            outputServerLog("[sweet-and-kendl] HEADLESS REQUEST REJECTED: " .. tostring(reason))
        end
    end
end, 250, 0)

addCommandHandler("sweetandkendlabort", function(player)
    if mission and (not isElement(player) or getElementType(player) ~= "player" or player == mission.leader) then
        if mission.transition then
            clearMission(true, {ok = false, reason = "aborted"})
        else
            clearMission(true)
        end
    end
end)

addEventHandler("onPlayerQuit", root, function()
    if mission and mission.players[source] then
        failMission("participant deconnecte")
    end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    clearMission(true)
end)

outputServerLog("[sweet-and-kendl] Ready: /sweetandkendl, headless sweetandkendltest, or transition profiles.")
