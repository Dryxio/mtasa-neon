local mission
local serial = 0
local failMission
local beginPoliceCutscene, beginHouseScene, setupChase, startNextRecording, beginCombat, beginBurgerReturn, passMission
local placeVehicle
local skipCutscene

local function isAutoAdvance()
    return mission and (mission.headless or mission.transition)
end

local function rememberTimer(timer)
    mission.timers[#mission.timers + 1] = timer
    return timer
end

local function participants()
    local result = {}
    if mission then
        for player in pairs(mission.players) do
            if isElement(player) then result[#result + 1] = player end
        end
    end
    return result
end

local function snapshotPlayer(player)
    local x, y, z = getElementPosition(player)
    local rx, ry, rz = getElementRotation(player)
    return {x = x, y = y, z = z, rx = rx, ry = ry, rz = rz, interior = getElementInterior(player),
            dimension = getElementDimension(player), frozen = isElementFrozen(player), alpha = getElementAlpha(player),
            collisions = getElementCollisionsEnabled(player), health = getElementHealth(player), armor = getPedArmor(player),
            model = getElementModel(player)}
end

local function restorePlayer(player, state)
    if not isElement(player) then return end
    if isPedInVehicle(player) then removePedFromVehicle(player) end
    setElementInterior(player, state.interior)
    setElementDimension(player, state.dimension)
    if state.model and getElementModel(player) ~= state.model then setElementModel(player, state.model) end
    setElementPosition(player, state.x, state.y, state.z)
    setElementRotation(player, state.rx, state.ry, state.rz)
    setElementFrozen(player, state.frozen)
    setElementAlpha(player, state.alpha)
    setElementCollisionsEnabled(player, state.collisions)
    setElementHealth(player, math.max(1, state.health))
    setPedArmor(player, state.armor)
    toggleAllControls(player, true, true, true)
end

local function trace(event, data)
    if not mission then return end
    local record = {event = event, run = mission.id, stage = mission.stage, tick = getTickCount()}
    for key, value in pairs(type(data) == "table" and data or {}) do
        if type(value) ~= "userdata" then record[key] = value end
    end
    outputServerLog("[og-loc-jsonl] " .. tostring(toJSON(record, true)):gsub("[\r\n]", ""))
end

local function transitionTrace(event, data)
    local record = {event = event, run = mission and mission.id, stage = mission and mission.stage,
                    tick = getTickCount()}
    for key, value in pairs(type(data) == "table" and data or {}) do
        if type(value) ~= "userdata" then record[key] = value end
    end
    outputServerLog("[og-loc-transition-jsonl] " .. tostring(toJSON(record, true)):gsub("[\r\n]", ""))
end

local function track(element)
    if not isElement(element) then return false end
    mission.entities[#mission.entities + 1] = element
    setElementDimension(element, OGL.dimension)
    return element
end

local function createScmPed(model, position)
    local ped, reason = exports["story-world-runtime"]:createStoryScmPed(
        model, position[1], position[2], position.scriptZ or position[3], position[4] or 0, OGL.dimension)
    if not ped then return false, reason end
    return track(ped)
end

local function createScmVehicle(model, position, syncer)
    local handle, vehicle, reason = exports["story-world-runtime"]:createStoryScmVehicle(
        model, position[1], position[2], position.scriptZ or position[3], position[4] or 0, OGL.dimension,
        syncer or mission.leader, {timeout = 15000})
    if not handle then return false, false, reason end
    track(vehicle)
    mission.vehiclePlacements[handle] = {vehicle = vehicle}
    return vehicle, handle
end

local function awaitVehiclePlacements(handles, callback)
    if #handles == 0 then return callback({}) end
    local remaining, results = #handles, {}
    for _, handle in ipairs(handles) do
        local placement = mission.vehiclePlacements[handle]
        if not placement then return failMission("placement SCM inconnu") end
        placement.callback = function(data)
            results[handle] = data
            remaining = remaining - 1
            if remaining == 0 then callback(results) end
        end
    end
end

local function awaitWorldTeardown(elements, fadeOut, callback)
    local handle, reason = exports["story-world-runtime"]:destroyStoryWorldElements(
        participants(), elements, {fadeOut = fadeOut, timeout = 15000})
    if not handle then return failMission("teardown du monde refuse: " .. tostring(reason)) end
    mission.worldTeardowns[handle] = callback
end

local function setStage(stage, payload)
    mission.stage = stage
    mission.stageStartedAt = getTickCount()
    trace("stage", {value = stage})
    triggerClientEvent(participants(), "ogl:stage", resourceRoot, stage, payload or {})
end

local function cancelCohort(name)
    local handle = mission and mission.cohorts[name]
    if isElement(handle) and not mission.nativeTaskStopping then
        exports["native-task-runtime"]:cancelNativeTaskCohort(handle)
    end
    if mission then
        mission.cohorts[name] = nil
        mission.cohortStates[name] = nil
    end
end

local function cancelPlayback(handle)
    if isElement(handle) and not (mission and mission.nativeTaskStopping) then
        exports["native-task-runtime"]:cancelNativeRecordedVehiclePlayback(handle)
    end
end

local function controlsRestored(player)
    for _, control in ipairs({"forwards", "accelerate", "vehicle_left", "vehicle_right", "enter_exit"}) do
        if not isControlEnabled(player, control) then return false, control end
    end
    return true
end

local function restoredPlayerMatches(player, expected)
    if not isElement(player) then return false, {field = "element", expected = true, actual = false} end
    if isPedInVehicle(player) then return false, {field = "vehicle", expected = false, actual = true} end
    local actualInterior = getElementInterior(player)
    if actualInterior ~= expected.interior then
        return false, {field = "interior", expected = expected.interior, actual = actualInterior}
    end
    local actualDimension = getElementDimension(player)
    if actualDimension ~= expected.dimension then
        return false, {field = "dimension", expected = expected.dimension, actual = actualDimension}
    end
    local actualFrozen = isElementFrozen(player)
    if actualFrozen ~= expected.frozen then
        return false, {field = "frozen", expected = expected.frozen, actual = actualFrozen}
    end
    local actualCollisions = getElementCollisionsEnabled(player)
    if actualCollisions ~= expected.collisions then
        return false, {field = "collisions", expected = expected.collisions, actual = actualCollisions}
    end
    local x, y, z = getElementPosition(player)
    local distance = getDistanceBetweenPoints3D(x, y, z, expected.x, expected.y, expected.z)
    if distance > 0.1 then
        return false, {field = "position", expectedX = expected.x, expectedY = expected.y, expectedZ = expected.z,
                       actualX = x, actualY = y, actualZ = z, distance = distance}
    end
    return true, {field = "none", distance = distance}
end

local function finishTransitionTerminal(ok, reason)
    if not mission then return end
    local profile = tostring(mission.transitionProfile or "unknown")
    trace(ok and "PASS" or "FAIL", {profile = profile, reason = reason, cleanup = true})
    transitionTrace(ok and "PASS" or "FAIL",
                    {profile = profile, players = mission.expectedPlayers or #participants(), reason = reason,
                     cleanup = true})
    outputServerLog(("[og-loc-transition] %s profile=%s players=%d%s"):format(
        ok and "PASS" or "FAIL", profile, mission.expectedPlayers or #participants(),
        reason and (" reason=" .. tostring(reason)) or ""))
    mission = nil
end

local function releasePlayerModelLease()
    if not mission or not isElement(mission.playerModelLease) then return end
    -- The dependency restores and destroys its own leases after publishing its
    -- stopping event. Calling an export from that event races an unavailable
    -- Lua VM, so the consumer only forgets its local handle in this path.
    if not mission.storyWorldStopping then
        exports["story-world-runtime"]:releaseStoryPlayerModelLease(mission.playerModelLease)
    end
    mission.playerModelLease = nil
end

local function beginTransitionTerminal(ok, reason)
    if not mission or mission.cleanupStarted then return end
    mission.cleanupStarted = true
    for _, timer in ipairs(mission.timers) do if isTimer(timer) then killTimer(timer) end end
    for _, handle in pairs(mission.playbacks) do cancelPlayback(handle) end
    for name in pairs(mission.cohorts) do cancelCohort(name) end
    for handle in pairs(mission.vehiclePlacements) do
        if isElement(handle) and not mission.storyWorldStopping then
            exports["story-world-runtime"]:releaseStoryScmVehicle(handle)
        end
    end
    for handle in pairs(mission.worldTeardowns) do
        if isElement(handle) and not mission.storyWorldStopping then
            exports["story-world-runtime"]:releaseStoryWorldTeardown(handle)
        end
    end
    for handle in pairs(mission.vehicleOccupancies) do
        if isElement(handle) and not mission.storyWorldStopping then
            exports["story-world-runtime"]:releaseStoryVehicleOccupancy(handle)
        end
    end
    mission.cleanupWaiting = {}
    releasePlayerModelLease()
    for player, snapshot in pairs(mission.players) do
        if isElement(player) then
            restorePlayer(player, snapshot)
            mission.cleanupWaiting[player] = true
        end
    end
    for _, element in ipairs(mission.entities) do if isElement(element) then destroyElement(element) end end
    mission.terminal = {ok = ok == true, reason = reason}
    triggerClientEvent(participants(), "ogl:cleanup", resourceRoot, mission.id, true)
    mission.cleanupTimeout = setTimer(function()
        if mission and mission.cleanupWaiting then finishTransitionTerminal(false, "cleanup client timeout") end
    end, 10000, 1)
end

local function clearMission(restore, terminal)
    if not mission or mission.cleaning then return end
    if mission.transition and terminal then
        mission.cleaning = true
        return beginTransitionTerminal(terminal.ok == true, terminal.reason)
    end
    mission.cleaning = true
    for _, timer in ipairs(mission.timers) do if isTimer(timer) then killTimer(timer) end end
    for _, handle in pairs(mission.playbacks) do cancelPlayback(handle) end
    for name in pairs(mission.cohorts) do cancelCohort(name) end
    for handle in pairs(mission.vehiclePlacements) do
        if isElement(handle) and not mission.storyWorldStopping then
            exports["story-world-runtime"]:releaseStoryScmVehicle(handle)
        end
    end
    for handle in pairs(mission.worldTeardowns) do
        if isElement(handle) and not mission.storyWorldStopping then
            exports["story-world-runtime"]:releaseStoryWorldTeardown(handle)
        end
    end
    for handle in pairs(mission.vehicleOccupancies) do
        if isElement(handle) and not mission.storyWorldStopping then
            exports["story-world-runtime"]:releaseStoryVehicleOccupancy(handle)
        end
    end
    releasePlayerModelLease()
    triggerClientEvent(participants(), "ogl:cleanup", resourceRoot, mission.id)
    for player, state in pairs(mission.players) do if restore then restorePlayer(player, state) end end
    for _, element in ipairs(mission.entities) do if isElement(element) then destroyElement(element) end end
    mission = nil
end

failMission = function(reason, textKey)
    if not mission or mission.finishing then return end
    mission.finishing = true
    setStage("failed", {reason = reason, textKey = textKey})
    trace("failure_requested", {reason = reason, textKey = textKey})
    if mission.transition then
        return rememberTimer(setTimer(function()
            clearMission(true, {ok = false, reason = reason})
        end, 500, 1))
    end
    trace("FAIL", {reason = reason, textKey = textKey})
    outputServerLog("[og-loc] FAIL: " .. tostring(reason))
    rememberTimer(setTimer(function() clearMission(true) end, mission.headless and 500 or 5000, 1))
end

local function barrier(kind, name, callback, payload)
    mission.sceneSerial = mission.sceneSerial + 1
    local current = {id = mission.sceneSerial, kind = kind, name = name, waiting = {}, callback = callback,
                     started = {}, finished = {}, camera = {}, audio = {}, released = {}}
    mission.barrier = current
    for _, player in ipairs(participants()) do current.waiting[player] = true end
    setStage(kind .. ":" .. name)
    if mission.headless then
        return rememberTimer(setTimer(function()
            if mission and mission.barrier == current then mission.barrier = nil callback() end
        end, 50, 1))
    end
    triggerClientEvent(participants(), kind == "cutscene" and "ogl:fileCutscene" or "ogl:scene", resourceRoot,
                       current.id, name, mission.leader, payload or {})
    rememberTimer(setTimer(function()
        if mission and mission.barrier == current then failMission(kind .. " " .. name .. " non termine") end
    -- SMOKE1's doorbell scene contains ten sequential speech events. Keep the
    -- same generous bound as file cutscenes so slower audio streaming cannot
    -- turn a valid presentation delay into a mission failure.
    end, (kind == "cutscene" or kind == "scene") and 150000 or 90000, 1))
end

local function transitionCheckpoint(name, kind, callback, payload)
    if not mission or not mission.transition then return callback() end
    if mission.transitionCheckpoint then return failMission("checkpoint transition concurrent: " .. tostring(name)) end
    mission.transitionProbeSerial = (mission.transitionProbeSerial or 0) + 1
    local checkpoint = {probeId = mission.transitionProbeSerial, name = name, kind = kind, callback = callback,
                        waiting = {}, startedAt = getTickCount()}
    mission.transitionCheckpoint = checkpoint
    for _, player in ipairs(participants()) do checkpoint.waiting[player] = true end
    trace("transition_checkpoint", {probeId = checkpoint.probeId, name = name, kind = kind, phase = "requested"})
    local input = kind == "observe" and "none" or (kind:find("vehicle") and "accelerate/w" or "forwards/w")
    outputServerLog(("[og-loc-transition] READY checkpoint=%s kind=%s players=%d input=%s"):format(
        name, kind, #participants(), input))
    triggerClientEvent(participants(), "ogl:transitionCheckpoint", resourceRoot, mission.id, checkpoint.probeId,
                       name, kind,
                       mission.leader, payload or {})
    checkpoint.timeout = rememberTimer(setTimer(function()
        if mission and mission.transitionCheckpoint == checkpoint then
            failMission("checkpoint transition timeout: " .. tostring(name))
        end
    end, 70000, 1))
end

local function autoAdvanceTravel(stage)
    if not mission or mission.stage ~= stage then return end
    if stage == "drive_police" then
        placeVehicle(mission.smokeCar, OGL.police)
        for _, auxVehicle in pairs(mission.auxVehicles) do placeVehicle(auxVehicle, OGL.police) end
        beginPoliceCutscene()
    elseif stage == "drive_house" then
        placeVehicle(mission.smokeCar, {OGL.house[1], OGL.house[2] - 10, OGL.house[3], 0})
        for _, auxVehicle in pairs(mission.auxVehicles) do
            placeVehicle(auxVehicle, {OGL.house[1] - 5, OGL.house[2] - 10, OGL.house[3], 0})
        end
        beginHouseScene()
    end
end

local function allNear(x, y, z, radius)
    for player in pairs(mission.players) do
        if not isElement(player) then return false end
        local px, py, pz = getElementPosition(player)
        if getDistanceBetweenPoints3D(px, py, pz, x, y, z) > radius then return false end
    end
    return true
end

local function elementNear(element, x, y, z, radius)
    if not isElement(element) then return false end
    local ex, ey, ez = getElementPosition(element)
    return getDistanceBetweenPoints3D(ex, ey, ez, x, y, z) <= radius
end

local function travelOccupantsReady(includeOgloc)
    if not isElement(mission.smokeCar) or getPedOccupiedVehicle(mission.leader) ~= mission.smokeCar or
        getPedOccupiedVehicleSeat(mission.leader) ~= 0 or getPedOccupiedVehicle(mission.smoke) ~= mission.smokeCar or
        getPedOccupiedVehicleSeat(mission.smoke) ~= 1 or getPedOccupiedVehicle(mission.sweet) ~= mission.smokeCar or
        getPedOccupiedVehicleSeat(mission.sweet) ~= 2 then
        return false
    end
    return not includeOgloc or
               (getPedOccupiedVehicle(mission.ogloc) == mission.smokeCar and
                   getPedOccupiedVehicleSeat(mission.ogloc) == 3)
end

local function leaderAtDoorbell()
    if not isElement(mission.leader) or isPedInVehicle(mission.leader) then return false end
    local x, y, z = getElementPosition(mission.leader)
    local vx, vy, vz = getElementVelocity(mission.leader)
    return math.abs(x - OGL.doorbell[1]) <= 1.2 and math.abs(y - OGL.doorbell[2]) <= 1.2 and
               math.abs(z - OGL.doorbell[3]) <= 3 and (vx * vx + vy * vy + vz * vz) <= 0.0025
end

placeVehicle = function(vehicle, position)
    setElementPosition(vehicle, position[1], position[2], position[3])
    setElementRotation(vehicle, 0, 0, position[4] or 0)
end

local function activateVehicle(vehicle)
    if not isElement(vehicle) then return false end
    setElementVelocity(vehicle, 0, 0, 0)
    setElementAngularVelocity(vehicle, 0, 0, 0)
    setElementCollisionsEnabled(vehicle, true)
    setElementFrozen(vehicle, false)
    return true
end

local function stageVehicleOccupant(warps, ped, vehicle, seat)
    warps[#warps + 1] = {ped = ped, vehicle = vehicle, seat = seat}
end

local function runStagedVehicleWarps(warps, callback)
    local handle, reason = exports["story-world-runtime"]:createStoryVehicleOccupancy(
        mission.leader, warps, {stepDelay = 100, timeout = 15000, stageActors = true})
    if not handle then return failMission("occupation du vehicule refusee: " .. tostring(reason)) end
    mission.vehicleOccupancies[handle] = {callback = callback, count = #warps}
    trace("travel_occupancy", {phase = "requested", count = #warps})
end

local function createTravelActors()
    local c = OGL.smokeCar
    local smoke = OGL.smokeStart
    local sweet = OGL.sweetStart
    local placementHandles, warps = {}, {}
    local primaryHandle
    trace("travel_build", {phase = "vehicle_begin"})
    mission.smokeCar, primaryHandle = createScmVehicle(OGL.models.glendale, c, mission.leader)
    trace("travel_build", {phase = "vehicle_created", vehicle = isElement(mission.smokeCar),
                            placement = isElement(primaryHandle)})
    if primaryHandle then placementHandles[#placementHandles + 1] = primaryHandle end
    -- The SCM creates both actors beside the Glendale before assigning their
    -- entry tasks. Creating them at the vehicle centre made three collision
    -- bodies overlap and launched the occupied car when physics resumed.
    mission.smoke = createScmPed(OGL.models.smoke, smoke)
    trace("travel_build", {phase = "smoke_created", ped = isElement(mission.smoke)})
    mission.sweet = createScmPed(OGL.models.sweet, sweet)
    trace("travel_build", {phase = "sweet_created", ped = isElement(mission.sweet)})
    if not mission.smokeCar or not mission.smoke or not mission.sweet then return false end
    setVehicleColor(mission.smokeCar, 98, 14, 98, 14)
    setElementHealth(mission.smokeCar, 2000)
    -- Build the occupied car as a frozen simulation island. Automatic syncer
    -- assignment previously arrived after the warps and made GTA re-admit a
    -- live, overlapping physics state, launching the whole car into the air.
    setElementFrozen(mission.smokeCar, true)
    setElementCollisionsEnabled(mission.smokeCar, false)
    trace("travel_build", {phase = "physics_staged"})
    local syncersAccepted = setElementSyncer(mission.smokeCar, mission.leader, true, true)
    trace("travel_build", {phase = "vehicle_syncer", accepted = syncersAccepted == true})
    syncersAccepted = setElementSyncer(mission.smoke, mission.leader, true, true) and syncersAccepted
    trace("travel_build", {phase = "smoke_syncer", accepted = syncersAccepted == true})
    syncersAccepted = setElementSyncer(mission.sweet, mission.leader, true, true) and syncersAccepted
    trace("travel_build", {phase = "sweet_syncer", accepted = syncersAccepted == true})
    if not syncersAccepted then return false end
    stageVehicleOccupant(warps, mission.leader, mission.smokeCar, 0)
    stageVehicleOccupant(warps, mission.smoke, mission.smokeCar, 1)
    stageVehicleOccupant(warps, mission.sweet, mission.smokeCar, 2)
    local seat = 3
    local supportIndex = 0
    for _, player in ipairs(participants()) do
        if player ~= mission.leader then
            if seat <= 3 then
                stageVehicleOccupant(warps, player, mission.smokeCar, seat)
                seat = seat + 1
            else
                supportIndex = supportIndex + 1
                local offset = supportIndex * 4
                local position = {c[1] - offset, c[2] - 4, c[3], c[4], scriptZ = c.scriptZ}
                local vehicle, handle = createScmVehicle(OGL.models.glendale, position, player)
                if not vehicle then return false end
                placementHandles[#placementHandles + 1] = handle
                mission.auxVehicles[player] = vehicle
                mission.auxVehiclePositions[vehicle] = position
                stageVehicleOccupant(warps, player, vehicle, 0)
            end
        end
    end
    trace("travel_build", {phase = "complete", placements = #placementHandles})
    return placementHandles, primaryHandle, warps
end

local function detachTravelWorld(includeOgloc)
    for player in pairs(mission.players) do
        if isElement(player) and isPedInVehicle(player) then removePedFromVehicle(player) end
    end
    local elements = {mission.smoke, mission.sweet, mission.smokeCar}
    if includeOgloc then elements[#elements + 1] = mission.ogloc end
    for _, vehicle in pairs(mission.auxVehicles) do
        elements[#elements + 1] = vehicle
    end
    mission.smoke, mission.sweet, mission.smokeCar = nil, nil, nil
    if includeOgloc then mission.ogloc = nil end
    mission.auxVehicles, mission.auxVehiclePositions = {}, {}
    return elements
end

local function createPoliceTravelActors()
    local p = OGL.police
    local placementHandles, warps = {}, {}
    local primaryHandle
    mission.smokeCar, primaryHandle = createScmVehicle(OGL.models.glendale, p, mission.leader)
    if primaryHandle then placementHandles[#placementHandles + 1] = primaryHandle end
    -- These spawn transforms reproduce the SCM values hidden under the black
    -- cutscene teardown frame; every actor is warped before the world returns.
    mission.smoke = createScmPed(OGL.models.smoke, {2072.3, -1697.2, 12.5, 255.7})
    mission.sweet = createScmPed(OGL.models.sweet, {2072.3, -1696.2, 12.5, 0})
    mission.ogloc = createScmPed(OGL.models.ogloc, {1543.2, -1687.0, 12.5, 97.2})
    if not mission.smokeCar or not mission.smoke or not mission.sweet or not mission.ogloc then return false end
    setVehicleColor(mission.smokeCar, 98, 14, 98, 14)
    setElementHealth(mission.smokeCar, 2000)
    setElementHealth(mission.ogloc, 2000)
    mission.oglocExpected = true
    setElementFrozen(mission.smokeCar, true)
    setElementCollisionsEnabled(mission.smokeCar, false)
    local syncersAccepted = setElementSyncer(mission.smokeCar, mission.leader, true, true)
    for _, ped in ipairs({mission.smoke, mission.sweet, mission.ogloc}) do
        syncersAccepted = setElementSyncer(ped, mission.leader, true, true) and syncersAccepted
    end
    if not syncersAccepted then return false end
    stageVehicleOccupant(warps, mission.leader, mission.smokeCar, 0)
    stageVehicleOccupant(warps, mission.smoke, mission.smokeCar, 1)
    stageVehicleOccupant(warps, mission.sweet, mission.smokeCar, 2)
    stageVehicleOccupant(warps, mission.ogloc, mission.smokeCar, 3)
    local supportIndex = 0
    for _, player in ipairs(participants()) do
        if player ~= mission.leader then
            supportIndex = supportIndex + 1
            local position = {p[1] + supportIndex * 4, p[2], p[3], p[4]}
            position.scriptZ = p.scriptZ
            local vehicle, handle = createScmVehicle(OGL.models.glendale, position, player)
            if not vehicle then return false end
            placementHandles[#placementHandles + 1] = handle
            mission.auxVehicles[player] = vehicle
            mission.auxVehiclePositions[vehicle] = position
            stageVehicleOccupant(warps, player, vehicle, 0)
        end
    end
    return placementHandles, primaryHandle, warps
end

local function armTravelReleaseTimeout(stage)
    local missionId = mission.id
    rememberTimer(setTimer(function()
        if mission and mission.id == missionId and mission.stage == stage and not mission.travelReleased then
            failMission("travelReleased timeout P0: " .. tostring(stage))
        end
    end, 12000, 1))
end

local function beginDriveToPolice()
    local handles, primaryHandle, warps = createTravelActors()
    if not handles then return failMission("creation des acteurs Grove refusee") end
    awaitVehiclePlacements(handles, function(results)
        for _, data in pairs(results) do
            local auxPosition = mission.auxVehiclePositions[data.vehicle]
            if auxPosition then auxPosition[3], auxPosition.scriptZ = data.centerZ, data.scriptZ end
        end
        local placement = results[primaryHandle]
        mission.travelPlacement = {OGL.smokeCar[1], OGL.smokeCar[2], placement.centerZ, OGL.smokeCar[4],
                                   scriptZ = placement.scriptZ, baseOffset = placement.baseOffset}
        mission.travelReleased = false
        mission.travelReleasedAt = nil
        runStagedVehicleWarps(warps, function()
            setStage("drive_police", {objective = OGL.police, textKey = "SMK1_02", probeVehicle = mission.smokeCar,
                                      probeScriptZ = placement.scriptZ,
                                      probeActors = {smoke = mission.smoke, sweet = mission.sweet}})
            armTravelReleaseTimeout("drive_police")
        end)
    end)
end

beginPoliceCutscene = function()
    if mission.stage ~= "drive_police" then return end
    setStage("police_cutscene_teardown")
    -- main.scm fades to black, moves CJ out of the car, then deletes Smoke,
    -- Sweet and the first Glendale before LOAD_CUTSCENE SMOKE1B. Keeping MTA
    -- instances alive lets the native cutscene remap the same special/model
    -- slots underneath synchronized entities. That ownership violation matches
    -- the observed SMOKE1B frame-loop stall.
    for player in pairs(mission.players) do
        setElementFrozen(player, true)
        toggleAllControls(player, false, true, true)
        setElementCollisionsEnabled(player, false)
    end
    trace("cutscene_world_teardown", {name = OGL.cutscenes.police})
    local oldWorld = detachTravelWorld(true)
    awaitWorldTeardown(oldWorld, 2.0, function()
        for player in pairs(mission.players) do setElementPosition(player, 1496.5, -1672.6, 14.2) end
        barrier("cutscene", OGL.cutscenes.police, function()
            local handles, primaryHandle, warps = createPoliceTravelActors()
            if not handles then return failMission("reconstruction des acteurs au commissariat refusee") end
            awaitVehiclePlacements(handles, function(results)
                for _, data in pairs(results) do
                    local auxPosition = mission.auxVehiclePositions[data.vehicle]
                    if auxPosition then auxPosition[3], auxPosition.scriptZ = data.centerZ, data.scriptZ end
                end
                local placement = results[primaryHandle]
                mission.travelPlacement = {OGL.police[1], OGL.police[2], placement.centerZ, OGL.police[4],
                                           scriptZ = placement.scriptZ, baseOffset = placement.baseOffset}
                mission.travelReleased = false
                mission.travelReleasedAt = nil
                runStagedVehicleWarps(warps, function()
                    setStage("reconstruct_police", {probeVehicle = mission.smokeCar,
                                                    probeScriptZ = placement.scriptZ,
                                                    probeActors = {smoke = mission.smoke, sweet = mission.sweet}})
                    armTravelReleaseTimeout("reconstruct_police")
                end)
            end)
        end)
    end)
end

beginHouseScene = function()
    if mission.stage ~= "drive_house" then return end
    for player in pairs(mission.players) do toggleAllControls(player, false, true, true) end
    if isElement(mission.smokeCar) then
        setElementVelocity(mission.smokeCar, 0, 0, 0)
        setElementAngularVelocity(mission.smokeCar, 0, 0, 0)
        setElementFrozen(mission.smokeCar, true)
    end
    barrier("scene", "freddys_house_arrival", function()
        local index = 0
        for player in pairs(mission.players) do
            index = index + 1
            if isPedInVehicle(player) then removePedFromVehicle(player) end
            setElementCollisionsEnabled(player, true)
            setElementFrozen(player, false)
            toggleAllControls(player, true, true, true)
            setElementPosition(player, 2457.4 - (index - 1), -1286.1 - (index - 1), 23.0)
            setElementRotation(player, 0, 0, 230.2)
        end
        if isElement(mission.ogloc) then
            removePedFromVehicle(mission.ogloc)
            setElementPosition(mission.ogloc, 2467.8, -1277.1, 28.9)
            setElementRotation(mission.ogloc, 0, 0, 230.2)
        end
        setStage("doorbell", {objective = OGL.doorbell, textKey = "SMK1_03"})
        if mission.headless then
            rememberTimer(setTimer(setupChase, 250, 1))
        elseif mission.transition then
            transitionCheckpoint("doorbell_controls", "foot_all", function()
                if not mission or mission.stage ~= "doorbell" then return end
                setElementPosition(mission.leader, OGL.doorbell[1], OGL.doorbell[2], OGL.doorbell[3])
                setElementVelocity(mission.leader, 0, 0, 0)
            end)
        end
    end, {actors = {smoke = mission.smoke, sweet = mission.sweet, ogloc = mission.ogloc},
           vehicle = mission.smokeCar})
end

local function createFreddyCohort()
    local leaderBike = mission.playerBikes and mission.playerBikes[mission.leader]
    if not isElement(leaderBike) then return false, "PCJ-600 du leader absente" end
    local handle, reason = exports["native-task-runtime"]:createNativeTaskCohort(mission.leader, {
        members = {{ped = mission.freddy, vehicle = mission.freddyBike, seat = 0, missionActor = true,
                    proofs = {bullet = true, fire = true, explosion = false, collision = true, melee = true},
                    task = {type = "drive_by", target = mission.leader, radius = 300,
                            style = "ai_all_directions", rightHandSide = false, frequency = 100, reissue = true}},
                   {ped = mission.ogloc, vehicle = leaderBike, seat = 1, missionActor = true,
                    suffersCriticalHits = false, canBeDraggedOut = false, onlyDamagedByPlayer = true, neverTargeted = true,
                    task = {type = "drive_by", target = mission.freddy, radius = 300,
                            style = "ai_all_directions", rightHandSide = true, frequency = 100, reissue = true}}},
        vehicles = {{vehicle = mission.freddyBike, straightLineDistance = 10}},
        dependencies = {mission.leader},
    }, {fallbackOwners = {}})
    if not handle then return false, reason end
    mission.cohorts.freddy = handle
    return true
end

addEventHandler("onPedDamage", root, function(attacker, weapon, bodypart, loss)
    if not mission or source ~= mission.ogloc then return end
    trace("actor_damage", {actor = "ogloc", weapon = tonumber(weapon), bodypart = tonumber(bodypart),
                            loss = tonumber(loss), health = getElementHealth(source),
                            attacker = isElement(attacker) and getElementType(attacker) or false,
                            attackerIsFreddy = attacker == mission.freddy})
end)

setupChase = function()
    if not mission or mission.stage ~= "doorbell" then return end
    setStage("doorbell_prepare")
    for player in pairs(mission.players) do
        setElementFrozen(player, true)
        setElementCollisionsEnabled(player, false)
        toggleAllControls(player, false, true, true)
    end
    local oldTravelWorld = detachTravelWorld(false)
    awaitWorldTeardown(oldTravelWorld, 0.5, function()
        mission.playerBikes = {}
        local placementHandles = {}
        for index, player in ipairs(participants()) do
            local p = OGL.playerBikes[((index - 1) % #OGL.playerBikes) + 1]
            local bike, handle = createScmVehicle(OGL.models.pcj600, p, player)
            if not bike then return failMission("creation PCJ-600 de participant refusee") end
            placementHandles[#placementHandles + 1] = handle
            setElementHealth(bike, 2000)
            exports["native-task-runtime"]:setSynchronizedVehicleTyresCanBurst(bike, false)
            mission.playerBikes[player] = bike
        end
        local b, f = OGL.freddyBike, OGL.freddy
        local freddyPlacement
        mission.freddyBike, freddyPlacement = createScmVehicle(OGL.models.pcj600, b, mission.leader)
        if freddyPlacement then placementHandles[#placementHandles + 1] = freddyPlacement end
        mission.freddy = createScmPed(OGL.models.freddy, f)
        if not mission.freddyBike or not mission.freddy then return failMission("creation de Freddy refusee") end
        setElementHealth(mission.freddyBike, 1000)
        exports["native-task-runtime"]:setSynchronizedVehicleTyresCanBurst(mission.freddyBike, false)
        giveWeapon(mission.freddy, 32, 30000, true)
        setElementHealth(mission.freddy, 1000)
        local participantIndex = 0
        for player in pairs(mission.players) do
            participantIndex = participantIndex + 1
            setElementPosition(player, 2468.0, -1278.5 - (participantIndex - 1), 29.1)
            setElementRotation(player, 0, 0, 269.4)
        end
        if isElement(mission.ogloc) then
            setElementPosition(mission.ogloc, 2467.4, -1275.8, 28.8)
            setElementRotation(mission.ogloc, 0, 0, 282.8)
        end
        awaitVehiclePlacements(placementHandles, function()
            barrier("scene", "doorbell", function()
                local warps = {}
                for player, bike in pairs(mission.playerBikes) do
                    stageVehicleOccupant(warps, player, bike, 0)
                end
                if isElement(mission.ogloc) then
                    stageVehicleOccupant(warps, mission.ogloc, mission.playerBikes[mission.leader], 1)
                end
                stageVehicleOccupant(warps, mission.freddy, mission.freddyBike, 0)
                runStagedVehicleWarps(warps, function()
                    for player, bike in pairs(mission.playerBikes) do
                        activateVehicle(bike)
                        setElementFrozen(player, false)
                        toggleAllControls(player, true, true, true)
                    end
                    activateVehicle(mission.freddyBike)
                    setStage("chase_wait_authority", {textKey = "SMK1_04", target = mission.freddy})
                    local function dispatchFreddyCohort()
                        -- Occupancy readiness is emitted from a clean server tick.
                        -- Give that replication flush one more frame before the
                        -- cohort pins syncers and installs native tasks.
                        rememberTimer(setTimer(function()
                        if not mission or mission.stage ~= "chase_wait_authority" then return end
                        trace("cohort_create", {name = "freddy", phase = "begin"})
                        local ok, reason = createFreddyCohort()
                        trace("cohort_create", {name = "freddy", phase = "returned", accepted = ok == true})
                        if not ok then failMission("cohorte Freddy refusee: " .. tostring(reason)) end
                        end, 50, 1))
                    end
                    if mission.transition then
                        transitionCheckpoint("chase_controls", "vehicle_all", dispatchFreddyCohort)
                    else
                        dispatchFreddyCohort()
                    end
                end)
            end, {actors = {ogloc = mission.ogloc, freddy = mission.freddy}})
        end)
    end)
end

local function startObstacleRecordings(callback)
    if mission.obstaclesStarted then return callback() end
    mission.obstaclesStarted = true
    local entries, placementHandles = {}, {}
    for _, config in ipairs(OGL.obstacleRecordings) do
        local position = {config[3], config[4], config[5], 0, scriptZ = config[5]}
        local vehicle, placementHandle = createScmVehicle(config[2], position, mission.leader)
        if not vehicle then return failMission("creation obstacle refusee") end
        entries[#entries + 1] = {vehicle = vehicle, config = config}
        placementHandles[#placementHandles + 1] = placementHandle
    end
    awaitVehiclePlacements(placementHandles, function()
        for _, entry in ipairs(entries) do
            local vehicle, config = entry.vehicle, entry.config
            activateVehicle(vehicle)
            local handle, reason = exports["native-task-runtime"]:createNativeRecordedVehiclePlayback(
                vehicle, config[1], mission.leader, {pivotSpeed = 1, minimumSpeed = 1, maximumSpeed = 1,
                                                    loadTimeout = 15000, playbackTimeout = 120000})
            if handle then mission.playbacks["obstacle:" .. tostring(config[1])] = handle
            else return failMission("recording obstacle " .. tostring(config[1]) .. ": " .. tostring(reason)) end
        end
        callback()
    end)
end

startNextRecording = function()
    if not mission or mission.finishing then return end
    mission.recordingIndex = mission.recordingIndex + 1
    local recordingId = OGL.chaseRecordings[mission.recordingIndex]
    if not recordingId then return beginCombat() end
    if recordingId == 31 then placeVehicle(mission.freddyBike, OGL.chaseStart) end
    local function beginRecording()
        setStage("chase_recording:" .. tostring(recordingId),
                 {recordingId = recordingId, textKey = "SMK1_04", target = mission.freddy})
        local options = {}
        for key, value in pairs(OGL.playback) do options[key] = value end
        if isAutoAdvance() and recordingId ~= 30 then
            -- Recording 30 exercises the real distance formula. Subsequent headless
            -- and transition segments run at the native API maximum so the bounded
            -- harness does not depend on scripted teleports fighting a real syncer.
            options.pivotSpeed, options.minimumSpeed, options.maximumSpeed = 2, 2, 2
        else
            options.target = mission.leader
        end
        local handle, reason = exports["native-task-runtime"]:createNativeRecordedVehiclePlayback(
            mission.freddyBike, recordingId, mission.leader, options)
        if not handle then
            return failMission("recording " .. tostring(recordingId) .. " refuse: " .. tostring(reason))
        end
        mission.playbacks.main = handle
        mission.mainRecording = recordingId
    end
    if recordingId == 35 then return startObstacleRecordings(beginRecording) end
    beginRecording()
end

beginCombat = function()
    cancelCohort("freddy")
    if isPedInVehicle(mission.freddy) then removePedFromVehicle(mission.freddy) end
    if isPedInVehicle(mission.ogloc) then removePedFromVehicle(mission.ogloc) end
    setElementPosition(mission.freddy, 2300.3, -1502.8, 24.3)
    setElementPosition(mission.ogloc, 2295.3, -1491.3, 22.3)
    mission.goons = {}
    for _, p in ipairs(OGL.goons) do
        local ped = createScmPed(OGL.models.goon, p)
        if not ped then return failMission("creation goon refusee") end
        giveWeapon(ped, 32, 30000, true)
        setElementHealth(ped, 500)
        mission.goons[#mission.goons + 1] = ped
    end
    for _, ped in ipairs(mission.goons) do
        if not setElementSyncer(ped, mission.leader, true, true) then
            return failMission("syncer de goon refuse")
        end
    end
    if not setElementSyncer(mission.freddy, mission.leader, true, true) then
        return failMission("syncer de Freddy au combat refuse")
    end
    if not setElementSyncer(mission.ogloc, mission.leader, true, true) then
        return failMission("syncer de OG Loc au combat refuse")
    end
    local members = {{ped = mission.freddy, missionActor = true,
                      task = {type = "kill_on_foot", target = mission.ogloc, reissue = true}},
                     {ped = mission.ogloc, missionActor = true, suffersCriticalHits = false,
                      canBeDraggedOut = false, onlyDamagedByPlayer = true, neverTargeted = true,
                      task = {type = "kill_on_foot", target = mission.freddy, reissue = true}}}
    for _, ped in ipairs(mission.goons) do
        members[#members + 1] = {ped = ped, missionActor = true,
                                task = {type = "kill_on_foot", target = mission.leader, reissue = true}}
    end
    setStage("basketball_combat", {target = mission.freddy, textKey = "SMK1_04"})
    local handle, reason = exports["native-task-runtime"]:createNativeTaskCohort(
        mission.leader, {members = members, dependencies = {mission.leader}}, {fallbackOwners = {}})
    if not handle then return failMission("cohorte de combat refusee: " .. tostring(reason)) end
    mission.cohorts.combat = handle
end

beginBurgerReturn = function()
    if not mission or mission.stage ~= "basketball_combat" then return end
    setStage("freddy_dead_prepare")
    cancelCohort("combat")
    for _, ped in ipairs(mission.goons or {}) do if isElement(ped) then destroyElement(ped) end end
    setElementPosition(mission.ogloc, 2295.3, -1491.3, 22.3)
    local leaderBike = mission.playerBikes[mission.leader]
    if not isElement(leaderBike) then return failMission("PCJ-600 du leader absente", "SMK1_14") end
    local function createReturnProtection()
        local returnHandle, returnReason = exports["native-task-runtime"]:createNativeTaskCohort(mission.leader, {
            members = {{ped = mission.ogloc, missionActor = true,
                        suffersCriticalHits = false, canBeDraggedOut = false, onlyDamagedByPlayer = true,
                        neverTargeted = true, task = {type = "none"}}},
        }, {fallbackOwners = {}})
        if not returnHandle then
            failMission("cohorte de protection du retour refusee: " .. tostring(returnReason))
            return false
        end
        mission.cohorts.return_trip = returnHandle
        return true
    end
    runStagedVehicleWarps({{ped = mission.ogloc, vehicle = leaderBike, seat = 1}}, function()
        if not mission or mission.stage ~= "freddy_dead_prepare" or not createReturnProtection() then return end
        barrier("scene", "freddy_dead", function()
            setStage("return_burger_shot", {objective = OGL.burgerShot, textKey = "SMK1_13"})
            local function autoBurgerReturn()
                rememberTimer(setTimer(function()
                    if not mission or mission.stage ~= "return_burger_shot" then return end
                    -- Transition automation relocates the bikes instead of
                    -- driving across Los Santos. Revoke the policy cohort
                    -- before that discontinuity, then use the ordinary
                    -- occupancy lease to re-establish OG Loc's passenger
                    -- seat and reacquire his policy-only cohort at arrival.
                    cancelCohort("return_trip")
                    for _, bike in pairs(mission.playerBikes) do
                        placeVehicle(bike, {OGL.burgerShot[1], OGL.burgerShot[2], OGL.burgerShot[3] + 0.5, 0})
                    end
                    runStagedVehicleWarps({{ped = mission.ogloc, vehicle = leaderBike, seat = 1}}, function()
                        if not mission or mission.stage ~= "return_burger_shot" or not createReturnProtection() then return end
                        barrier("scene", "burger_shot", passMission, {actors = {ogloc = mission.ogloc}})
                    end)
                end, 250, 1))
            end
            if mission.headless then
                autoBurgerReturn()
            elseif mission.transition then
                transitionCheckpoint("return_controls", "vehicle_all", autoBurgerReturn)
            end
        end, {actors = {ogloc = mission.ogloc}})
    end)
end

passMission = function()
    if not mission or mission.finishing then return end
    mission.finishing = true
    setStage("passed", {respect = 5})
    trace("mission_passed", {respect = 5, recordings = #OGL.chaseRecordings})
    triggerClientEvent(participants(), "ogl:passed", resourceRoot, 5)
    if mission.transition then
        return rememberTimer(setTimer(function()
            clearMission(true, {ok = true})
        end, 500, 1))
    end
    trace("PASS", {respect = 5, recordings = #OGL.chaseRecordings})
    outputServerLog("[og-loc] PASS: SMOKE1, 11 chase recordings, dynamic 06FD and co-op return completed")
    rememberTimer(setTimer(function() clearMission(true) end, mission.headless and 500 or 5000, 1))
end

local function validateMission()
    if not mission or mission.finishing then return end
    for player in pairs(mission.players) do
        if not isElement(player) or isPedDead(player) then return failMission("participant mort ou absent") end
    end
    local travelActorsRequired = mission.stage == "drive_police" or mission.stage == "reconstruct_police" or
                                     mission.stage == "drive_house"
    if travelActorsRequired then
        if not isElement(mission.smoke) or isPedDead(mission.smoke) then return failMission("Big Smoke tue", "SMK1_06") end
        if not isElement(mission.sweet) or isPedDead(mission.sweet) then return failMission("Sweet tue", "SMK1_07") end
        if not isElement(mission.smokeCar) or isVehicleBlown(mission.smokeCar) then return failMission("Glendale detruite", "SMK1_08") end
    end
    if mission.oglocExpected and (not isElement(mission.ogloc) or isPedDead(mission.ogloc)) then
        return failMission("OG Loc tue", "SMK1_05")
    end
    if mission.stage == "drive_police" then
        if travelOccupantsReady(false) and allNear(OGL.police[1], OGL.police[2], OGL.police[3], 4) then
            beginPoliceCutscene()
        end
    elseif mission.stage == "drive_house" then
        if travelOccupantsReady(true) and allNear(OGL.house[1], OGL.house[2], OGL.house[3], 4) then beginHouseScene() end
    elseif mission.stage == "doorbell" then
        if leaderAtDoorbell() then setupChase() end
    elseif mission.stage and mission.stage:find("^chase_") then
        local recordingId = tonumber(mission.stage:match("^chase_recording:(%d+)$"))
        if recordingId and recordingId >= 30 and recordingId <= 40 then
            for _, bike in pairs(mission.playerBikes or {}) do
                if not isElement(bike) or isVehicleBlown(bike) then return failMission("PCJ-600 detruite", "SMK1_14") end
            end
        end
        if recordingId and recordingId >= 30 and recordingId <= 40 and isElement(mission.freddy) and
            not isPedDead(mission.freddy) and not isAutoAdvance() then
            local x, y = getElementPosition(mission.leader)
            local fx, fy = getElementPosition(mission.freddy)
            local visibilityFresh = mission.freddyVisibilityAt and getTickCount() - mission.freddyVisibilityAt <= 1000
            if getDistanceBetweenPoints2D(x, y, fx, fy) > 250 and visibilityFresh and
                mission.freddyOnScreen == false then
                return failMission("Freddy a fui", "SMK1_12")
            end
        end
    elseif mission.stage == "basketball_combat" then
        if isPedDead(mission.freddy) then return beginBurgerReturn() end
    elseif mission.stage == "return_burger_shot" and
        elementNear(mission.leader, OGL.burgerShot[1], OGL.burgerShot[2], OGL.burgerShot[3], 4) and
        elementNear(mission.ogloc, OGL.burgerShot[1], OGL.burgerShot[2], OGL.burgerShot[3], 4) then
        barrier("scene", "burger_shot", passMission, {actors = {ogloc = mission.ogloc}})
    end
end

local function startMission(leader, headless, options)
    options = type(options) == "table" and options or {}
    if mission then return false, "une mission est deja active" end
    if not isElement(leader) or getElementType(leader) ~= "player" then return false, "aucun joueur disponible" end
    local connected = getElementsByType("player")
    if options.expectedPlayers and #connected ~= options.expectedPlayers then
        return false, ("profil exige %d joueur(s), %d connecte(s)"):format(options.expectedPlayers, #connected)
    end
    if options.minimumPlayers and #connected < options.minimumPlayers then
        return false, ("profil exige au moins %d joueur(s), %d connecte(s)"):format(options.minimumPlayers, #connected)
    end
    serial = serial + 1
    mission = {id = serial, leader = leader, headless = headless == true, stage = "starting", players = {},
               transition = options.transition == true, transitionSkip = options.skip == true,
               transitionProfile = options.profile, expectedPlayers = options.expectedPlayers,
               entities = {}, timers = {}, cohorts = {}, cohortStates = {}, playbacks = {}, auxVehicles = {},
               auxVehiclePositions = {},
               vehiclePlacements = {}, vehicleOccupancies = {}, worldTeardowns = {}, sceneSerial = 0,
               recordingIndex = 0, lastPlaybackSamples = {}}
    for _, player in ipairs(getElementsByType("player")) do
        mission.players[player] = snapshotPlayer(player)
        trace("participant_snapshot", {player = getPlayerName(player), model = mission.players[player].model})
    end
    local modelLease, modelLeaseReason = exports["story-world-runtime"]:createStoryPlayerModelLease(participants(), 0)
    if not modelLease then
        mission = nil
        return false, "preparation CJ refusee: " .. tostring(modelLeaseReason)
    end
    mission.playerModelLease = modelLease

    local stagingIndex = 0
    for _, player in ipairs(participants()) do
        stagingIndex = stagingIndex + 1
        if isPedInVehicle(player) then removePedFromVehicle(player) end
        setElementInterior(player, 0)
        setElementDimension(player, OGL.dimension)
        -- Stage every participant beside the future CREATE_CAR before the
        -- native lease snapshots its return camera. Restoring an arbitrary
        -- pre-command transform caused the visible long-distance camera pull,
        -- and an unstaged player could not stream the empty vehicle for its
        -- model-specific SCM height calibration.
        setElementPosition(player, OGL.grove[1] - (stagingIndex - 1) * 1.5,
                           OGL.grove[2] - (stagingIndex - 1) * 1.5, OGL.grove[3])
        setElementRotation(player, 0, 0, OGL.grove[4])
        setElementFrozen(player, true)
        setElementCollisionsEnabled(player, false)
        toggleAllControls(player, false, true, true)
    end
    trace("start", {leader = getPlayerName(leader), players = #participants(), headless = mission.headless,
                     transition = mission.transition, profile = mission.transitionProfile})
    triggerClientEvent(participants(), "ogl:start", resourceRoot, mission.id, leader, mission.headless,
                       {transition = mission.transition, profile = mission.transitionProfile})
    barrier("cutscene", OGL.cutscenes.intro, beginDriveToPolice)
    mission.monitor = rememberTimer(setTimer(validateMission, 250, 0))
    return true
end

addEvent("ogl:barrierDone", true)
addEventHandler("ogl:barrierDone", resourceRoot, function(id, ok, reason)
    local current = mission and mission.barrier
    if source ~= resourceRoot or not current or current.id ~= id or not current.waiting[client] then return end
    trace("barrier_done", {kind = current.kind, name = current.name, player = getPlayerName(client), ok = ok == true})
    if ok ~= true then return failMission(current.kind .. " " .. current.name .. ": " .. tostring(reason)) end
    if mission.transition then
        if current.kind == "cutscene" and
            (not current.started[client] or not current.finished[client] or not current.released[client]) then
            return failMission("preuve cutscene incomplete: " .. tostring(current.name))
        elseif current.kind == "scene" and
            (not current.camera[client] or not current.audio[client] or not current.released[client]) then
            return failMission("preuve presentation de scene incomplete: " .. tostring(current.name))
        end
    end
    current.waiting[client] = nil
    for player in pairs(current.waiting) do if isElement(player) then return end end
    mission.barrier = nil
    local missionId = mission.id
    -- Never rebuild synchronized world state from inside the client RPC that
    -- closes a presentation barrier. CREATE_CAR, syncer assignment and warps
    -- can re-enter replication before the RPC dispatch unwinds, leaving the
    -- server loop stuck with a visible but permanently frozen vehicle. The
    -- headless path was already safe because its callback ran from a timer.
    rememberTimer(setTimer(function()
        if not mission or mission.id ~= missionId or mission.finishing then return end
        trace("barrier_callback", {kind = current.kind, name = current.name})
        current.callback()
    end, 50, 1))
end)

-- Included runtimes may be stopped and restarted after this resource during a
-- plain `restart og-loc`. Declare the public event names locally as well so
-- handlers remain attachable regardless of which dependency is loaded first.
addEvent("onStoryScmVehicleStateChange", false)
addEvent("onStoryVehicleOccupancyStateChange", false)
addEvent("onStoryWorldTeardownStateChange", false)
addEvent("onNativeTaskCohortStateChange", false)
addEvent("onNativeRecordedVehiclePlaybackStateChange", false)
addEvent("onStoryWorldRuntimeStopping", false)
addEvent("onNativeTaskRuntimeStopping", false)

local function failForRuntimeLifecycle(reason)
    if not mission or mission.cleaning then return end
    mission.finishing = true
    setStage("failed", {reason = reason})
    trace("failure_requested", {reason = reason, lifecycle = true})
    if mission.transition then
        clearMission(true, {ok = false, reason = reason})
    else
        outputServerLog("[og-loc] FAIL: " .. reason)
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

addEventHandler("onStoryScmVehicleStateChange", root, function(state, data)
    local placement = mission and mission.vehiclePlacements[source]
    if not placement then return end
    if state == "failed" then
        mission.vehiclePlacements[source] = nil
        exports["story-world-runtime"]:releaseStoryScmVehicle(source)
        return failMission("placement SCM du vehicule refuse: " .. tostring(data and data.reason))
    end
    if state ~= "ready" then return end
    local callback = placement.callback
    mission.vehiclePlacements[source] = nil
    exports["story-world-runtime"]:releaseStoryScmVehicle(source)
    trace("scm_vehicle_ready", {model = data.model, scriptZ = data.scriptZ, centerZ = data.centerZ,
                                baseOffset = data.baseOffset})
    if callback then callback(data) end
end)

addEventHandler("onStoryVehicleOccupancyStateChange", root, function(state, data)
    local occupancy = mission and mission.vehicleOccupancies[source]
    if not occupancy then return end
    trace("travel_occupancy", {phase = state, count = occupancy.count, reason = data and data.reason})
    if state == "failed" then
        mission.vehicleOccupancies[source] = nil
        exports["story-world-runtime"]:releaseStoryVehicleOccupancy(source)
        return failMission("occupation synchronisee refusee: " .. tostring(data and data.reason))
    end
    if state ~= "ready" or not occupancy.callback then return end
    local callback = occupancy.callback
    mission.vehicleOccupancies[source] = nil
    exports["story-world-runtime"]:releaseStoryVehicleOccupancy(source)
    callback()
end)

addEventHandler("onStoryWorldTeardownStateChange", root, function(state, data)
    local callback = mission and mission.worldTeardowns[source]
    if not callback then return end
    if state == "failed" then
        mission.worldTeardowns[source] = nil
        exports["story-world-runtime"]:releaseStoryWorldTeardown(source)
        return failMission("teardown synchronise refuse: " .. tostring(data and data.reason))
    end
    if state ~= "ready" then return end
    mission.worldTeardowns[source] = nil
    exports["story-world-runtime"]:releaseStoryWorldTeardown(source)
    trace("world_teardown_ready", {clients = data.clients, elements = data.elements})
    callback()
end)

addEvent("ogl:cutsceneProbe", true)
addEventHandler("ogl:cutsceneProbe", resourceRoot, function(id, name, phase, data)
    local current = mission and mission.barrier
    if source ~= resourceRoot or not current or current.id ~= tonumber(id) or not current.waiting[client] then return end
    phase = tostring(phase)
    data = type(data) == "table" and data or {}
    trace("cutscene_probe", {name = tostring(name), phase = phase, player = getPlayerName(client),
                              targetLocal = data.targetLocal == true})
    if phase == "started" then
        current.started[client] = true
        if mission.transition and mission.transitionSkip and not current.skipIssued then
            for player in pairs(current.waiting) do if isElement(player) and not current.started[player] then return end end
            current.skipIssued = true
            rememberTimer(setTimer(function()
                if mission and mission.barrier == current then
                    trace("cutscene_skip", {name = current.name, source = "transition-profile"})
                    skipCutscene()
                end
            end, 250, 1))
        end
    elseif phase == "finished" then
        current.finished[client] = true
    elseif phase == "released" then
        current.released[client] = data.targetLocal == true
    end
end)

addEvent("ogl:sceneProbe", true)
addEventHandler("ogl:sceneProbe", resourceRoot, function(id, name, phase, data)
    local current = mission and mission.barrier
    if source ~= resourceRoot or not current or current.id ~= tonumber(id) or current.kind ~= "scene" or
        not current.waiting[client] then
        return
    end
    phase = tostring(phase)
    data = type(data) == "table" and data or {}
    trace("scene_probe", {name = tostring(name), phase = phase, player = getPlayerName(client),
                           cameraError = tonumber(data.cameraError), targetLocal = data.targetLocal == true,
                           audioComplete = data.complete == true})
    if phase == "camera" and tonumber(data.cameraError) and tonumber(data.cameraError) <= 1.5 then
        current.camera[client] = true
    elseif phase == "audio" and data.complete == true then
        current.audio[client] = true
    elseif phase == "released" and data.targetLocal == true then
        current.released[client] = true
    end
end)

addEvent("ogl:transitionCheckpointDone", true)
addEventHandler("ogl:transitionCheckpointDone", resourceRoot, function(id, probeId, name, ok, reason, data)
    local checkpoint = mission and mission.transitionCheckpoint
    if source ~= resourceRoot or not checkpoint or mission.id ~= tonumber(id) or
        checkpoint.probeId ~= tonumber(probeId) or checkpoint.name ~= tostring(name) or
        not checkpoint.waiting[client] then
        return
    end
    data = type(data) == "table" and data or {}
    local controlsOk, disabledControl = controlsRestored(client)
    local requiresPhysical = checkpoint.kind == "vehicle_all" or checkpoint.kind == "foot_all" or
                                 (checkpoint.kind == "vehicle_leader" and client == mission.leader)
    trace("transition_checkpoint", {probeId = checkpoint.probeId, name = checkpoint.name,
                                     kind = checkpoint.kind, phase = "client",
                                     player = getPlayerName(client), ok = ok == true, reason = reason,
                                     raw = tonumber(data.raw), processed = tonumber(data.processed),
                                     displacement = tonumber(data.displacement), samples = tonumber(data.samples),
                                     keyObserved = data.keyObserved == true,
                                     targetLocal = data.targetLocal == true, controls = controlsOk,
                                     occupied = data.occupied == true, seat = tonumber(data.seat),
                                     streamed = data.streamed == true, targetStreamed = data.targetStreamed == true,
                                     chatControl = isControlEnabled(client, "chatbox")})
    local function reject(detail)
        transitionTrace(requiresPhysical and "INPUT_FAIL" or "PROBE_FAIL",
                        {player = getPlayerName(client), probeId = checkpoint.probeId,
                         checkpoint = checkpoint.name, kind = checkpoint.kind, reason = detail,
                         raw = tonumber(data.raw), processed = tonumber(data.processed),
                         displacement = tonumber(data.displacement), samples = tonumber(data.samples),
                         keyObserved = data.keyObserved == true})
        return failMission(detail)
    end
    if ok ~= true then return reject("checkpoint " .. checkpoint.name .. ": " .. tostring(reason)) end
    if not controlsOk then return reject("controle encore desactive: " .. tostring(disabledControl)) end
    if data.targetLocal ~= true then return reject("preuve camera incomplete: " .. checkpoint.name) end
    if checkpoint.kind:find("vehicle") then
        local occupiedVehicle = getPedOccupiedVehicle(client)
        local serverSeat = isElement(occupiedVehicle) and getPedOccupiedVehicleSeat(client) or -1
        if not isElement(occupiedVehicle) or data.occupied ~= true or data.streamed ~= true or
            tonumber(data.seat) ~= serverSeat then
            return reject("preuve d'occupation incomplete: " .. checkpoint.name)
        end
        if requiresPhysical and serverSeat ~= 0 then
            return reject("preuve conducteur incomplete: " .. checkpoint.name)
        end
    elseif checkpoint.kind == "observe" and data.targetStreamed ~= true then
        return reject("preuve de visibilite incomplete: " .. checkpoint.name)
    end
    if requiresPhysical and
        (data.keyObserved ~= true or (tonumber(data.raw) or 0) <= 0.8 or (tonumber(data.processed) or 0) <= 0.8 or
            (tonumber(data.displacement) or 0) <= 0.5 or (tonumber(data.samples) or 0) < 3) then
        return reject("preuve physique incomplete: " .. checkpoint.name)
    end
    transitionTrace(requiresPhysical and "INPUT_PASS" or "PROBE_PASS",
                    {player = getPlayerName(client), probeId = checkpoint.probeId,
                     checkpoint = checkpoint.name, kind = checkpoint.kind, raw = tonumber(data.raw),
                     processed = tonumber(data.processed), displacement = tonumber(data.displacement),
                     samples = tonumber(data.samples), keyObserved = data.keyObserved == true})
    checkpoint.waiting[client] = nil
    for player in pairs(checkpoint.waiting) do if isElement(player) then return end end
    if isTimer(checkpoint.timeout) then killTimer(checkpoint.timeout) end
    transitionTrace("CHECKPOINT_PASS", {probeId = checkpoint.probeId, checkpoint = checkpoint.name,
                                         kind = checkpoint.kind, players = #participants()})
    mission.transitionCheckpoint = nil
    local callback = checkpoint.callback
    rememberTimer(setTimer(function()
        if mission and not mission.finishing then
            trace("transition_checkpoint", {probeId = checkpoint.probeId, name = checkpoint.name,
                                             kind = checkpoint.kind, phase = "complete"})
            callback()
        end
    end, 50, 1))
end)

addEvent("ogl:transitionInputReady", true)
addEventHandler("ogl:transitionInputReady", resourceRoot, function(id, probeId, name, control)
    local checkpoint = mission and mission.transitionCheckpoint
    if source ~= resourceRoot or not checkpoint or mission.id ~= tonumber(id) or
        checkpoint.probeId ~= tonumber(probeId) or checkpoint.name ~= tostring(name) or
        not checkpoint.waiting[client] then
        return
    end
    local requiresPhysical = checkpoint.kind == "vehicle_all" or checkpoint.kind == "foot_all" or
                                 (checkpoint.kind == "vehicle_leader" and client == mission.leader)
    local expectedControl = checkpoint.kind == "foot_all" and "forwards" or "accelerate"
    if not requiresPhysical or tostring(control) ~= expectedControl then return end
    transitionTrace("INPUT_READY", {player = getPlayerName(client), probeId = checkpoint.probeId,
                                     checkpoint = checkpoint.name, kind = checkpoint.kind,
                                     control = expectedControl, key = "w"})
end)

addEvent("ogl:cleanupDone", true)
addEventHandler("ogl:cleanupDone", resourceRoot, function(id, ok, reason, data)
    if source ~= resourceRoot or not mission or not mission.cleanupWaiting or mission.id ~= tonumber(id) or
        not mission.cleanupWaiting[client] then
        return
    end
    local expected = mission.players[client]
    local controlsOk, disabledControl = controlsRestored(client)
    local serverRestored, restoreDiagnostic = expected and restoredPlayerMatches(client, expected)
    data = type(data) == "table" and data or {}
    trace("cleanup", {player = getPlayerName(client), ok = ok == true, serverRestored = serverRestored,
                       controls = controlsOk, chatControl = isControlEnabled(client, "chatbox"),
                       targetLocal = data.targetLocal == true, reason = reason,
                       restoreDiagnostic = restoreDiagnostic})
    if ok ~= true or not serverRestored or not controlsOk or data.targetLocal ~= true then
        local detail = reason or (not serverRestored and
                           ("server state " .. tostring(restoreDiagnostic and restoreDiagnostic.field))) or
                           (not controlsOk and ("control " .. tostring(disabledControl))) or "camera target"
        return finishTransitionTerminal(false, "cleanup invalide: " .. tostring(detail))
    end
    mission.cleanupWaiting[client] = nil
    for player in pairs(mission.cleanupWaiting) do if isElement(player) then return end end
    if isTimer(mission.cleanupTimeout) then killTimer(mission.cleanupTimeout) end
    local terminal = mission.terminal
    finishTransitionTerminal(terminal and terminal.ok == true, terminal and terminal.reason)
end)

addEvent("ogl:vehicleProbe", true)
addEventHandler("ogl:vehicleProbe", resourceRoot, function(vehicle, sample)
    local placement = mission and mission.travelPlacement
    if source ~= resourceRoot or not mission or client ~= mission.leader or
        (mission.stage ~= "drive_police" and mission.stage ~= "reconstruct_police") or vehicle ~= mission.smokeCar or
        type(placement) ~= "table" or type(sample) ~= "table" then
        return
    end
    local x, y, z = getElementPosition(vehicle)
    local syncer = getElementSyncer(vehicle)
    local baseOffset = tonumber(sample.baseOffset)
    local configuredPlacementError = baseOffset and (placement[3] - baseOffset - placement.scriptZ) or nil
    trace("vehicle_probe", {
        sample = math.floor(tonumber(sample.sample) or -1),
        clientX = tonumber(sample.x), clientY = tonumber(sample.y), clientZ = tonumber(sample.z),
        groundZ = tonumber(sample.groundZ), baseOffset = baseOffset,
        bottomClearance = tonumber(sample.bottomClearance), scriptPlacementError = tonumber(sample.scriptPlacementError),
        configuredPlacementError = configuredPlacementError,
        clientHealth = tonumber(sample.health), playerHealth = tonumber(sample.playerHealth),
        vx = tonumber(sample.vx), vy = tonumber(sample.vy), vz = tonumber(sample.vz),
        streamed = sample.streamed == true, syncing = sample.syncing == true,
        onGround = sample.onGround == true, blown = sample.blown == true,
        collisions = sample.collisions == true, frozen = sample.frozen == true,
        playerCollisions = sample.playerCollisions == true, playerFrozen = sample.playerFrozen == true,
        playerSeat = tonumber(sample.playerSeat), smokeSeat = tonumber(sample.smokeSeat),
        smokeCollisions = sample.smokeCollisions == true, sweetSeat = tonumber(sample.sweetSeat),
        sweetCollisions = sample.sweetCollisions == true,
        serverX = x, serverY = y, serverZ = z, serverHealth = getElementHealth(vehicle),
        serverSyncer = isElement(syncer) and getPlayerName(syncer) or false,
    })
    if configuredPlacementError and math.abs(configuredPlacementError) > 0.05 then
        failMission(("conversion Z SCM du vehicule invalide: ecart %.3f m"):format(configuredPlacementError))
    elseif mission.travelReleased and mission.travelReleasedAt and getTickCount() - mission.travelReleasedAt <= 1500 and
        tonumber(sample.z) and tonumber(sample.z) - placement[3] > 2 then
        -- This probe exists to diagnose reconstruction impulses, not to add a
        -- gameplay failure rule. Once controls are restored, a legitimate
        -- jump or stunt can exceed the threshold and vanilla SMOKE1 keeps
        -- running.
        trace("vehicle_probe_diagnostic", {reason = "post-release-height", z = tonumber(sample.z)})
    elseif not mission.travelReleased and sample.streamed == true and sample.syncing == true then
        local releasedStage = mission.stage
        placeVehicle(mission.smokeCar, placement)
        setElementVelocity(mission.smokeCar, 0, 0, 0)
        setElementAngularVelocity(mission.smokeCar, 0, 0, 0)
        setElementCollisionsEnabled(mission.smokeCar, true)
        setElementFrozen(mission.smokeCar, false)
        for player, vehicle in pairs(mission.auxVehicles) do
            local auxPosition = mission.auxVehiclePositions[vehicle]
            if auxPosition then placeVehicle(vehicle, auxPosition) end
            setElementVelocity(vehicle, 0, 0, 0)
            setElementAngularVelocity(vehicle, 0, 0, 0)
            setElementCollisionsEnabled(vehicle, true)
            setElementFrozen(vehicle, false)
        end
        for player in pairs(mission.players) do
            setElementFrozen(player, false)
            -- Keep the explicit no-collision state while the player is an
            -- occupant. Re-enabling it after warpPedIntoVehicle overrides
            -- GTA's occupant suppression and is a material lifecycle difference
            -- from the stable native drive-by harness. The exit barrier
            -- restores collisions immediately after removePedFromVehicle.
            toggleAllControls(player, true, true, true)
        end
        mission.travelReleased = true
        mission.travelReleasedAt = getTickCount()
        trace("travel_release", {clientSample = math.floor(tonumber(sample.sample) or -1), releaseStage = releasedStage})
        triggerClientEvent(participants(), "ogl:travelReleased", resourceRoot, releasedStage)
        if releasedStage == "reconstruct_police" then
            setStage("drive_house", {objective = OGL.house, textKey = "SMK1_10"})
            if mission.headless then
                rememberTimer(setTimer(function()
                    autoAdvanceTravel("drive_house")
                end, 250, 1))
            elseif mission.transition then
                transitionCheckpoint("drive_house_controls", "vehicle_leader", function()
                    autoAdvanceTravel("drive_house")
                end)
            end
        elseif mission.headless then
            rememberTimer(setTimer(function()
                autoAdvanceTravel("drive_police")
            end, 250, 1))
        elseif mission.transition then
            transitionCheckpoint("drive_police_controls", "vehicle_leader", function()
                autoAdvanceTravel("drive_police")
            end)
        end
    end
end)

addEvent("ogl:visibilityProbe", true)
addEventHandler("ogl:visibilityProbe", resourceRoot, function(target, stage, onScreen)
    if source ~= resourceRoot or not mission or client ~= mission.leader or stage ~= mission.stage or
        target ~= mission.freddy or not tostring(stage):find("^chase_recording:") then
        return
    end
    mission.freddyOnScreen = onScreen == true
    mission.freddyVisibilityAt = getTickCount()
end)

skipCutscene = function()
    if not mission or not mission.barrier or mission.barrier.kind ~= "cutscene" then return false end
    triggerClientEvent(participants(), "ogl:skipCutscene", resourceRoot, mission.barrier.id)
    return true
end

addEvent("ogl:skipRequest", true)
addEventHandler("ogl:skipRequest", resourceRoot, function(id)
    if source == resourceRoot and mission and client == mission.leader and mission.barrier and mission.barrier.id == id then
        skipCutscene()
    end
end)

addEventHandler("onNativeTaskCohortStateChange", root, function(state, data)
    if not mission then return end
    local name
    for key, handle in pairs(mission.cohorts) do if source == handle then name = key break end end
    if not name then return end
    -- The acceptance event can race the export return that registers the handle.
    -- Treat the first authoritative sample as reconciliation instead of dropping
    -- every sample: subsequent samples stay quiet once activation was observed.
    if state == "active" and data.sample and mission.cohortStates[name] == "active" then return end
    mission.cohortStates[name] = state
    trace("cohort", {name = name, state = state, reason = data.reason})
    if state == "failed" then return failMission("cohorte " .. name .. ": " .. tostring(data.reason)) end
    if name == "freddy" and state == "active" and mission.stage == "chase_wait_authority" then
        startNextRecording()
    elseif name == "combat" and state == "active" and isAutoAdvance() then
        local function completeAutomatedCombat()
            rememberTimer(setTimer(function()
            if mission and mission.stage == "basketball_combat" and isElement(mission.freddy) then
                setElementHealth(mission.freddy, 0)
            end
            end, 500, 1))
        end
        if mission.transition then
            transitionCheckpoint("combat_observation", "observe", completeAutomatedCombat,
                                 {target = mission.freddy})
        else
            completeAutomatedCombat()
        end
    end
end)

addEventHandler("onNativeRecordedVehiclePlaybackStateChange", root, function(state, data)
    if not mission then return end
    local key
    for name, handle in pairs(mission.playbacks) do if source == handle then key = name break end end
    if not key then return end
    if state == "active" and data.sample then
        local previous = mission.lastPlaybackSamples[key] or 0
        if getTickCount() - previous < 5000 then return end
        mission.lastPlaybackSamples[key] = getTickCount()
    end
    trace("playback", {name = key, recordingId = data.recordingId, state = state, speed = data.speed,
                       distance = data.distance, elapsed = data.elapsed, reason = data.reason})
    if state == "failed" then return failMission("playback " .. key .. ": " .. tostring(data.reason)) end
    if key == "main" and state == "completed" then
        mission.playbacks.main = nil
        startNextRecording()
    elseif key ~= "main" and (state == "completed" or state == "cancelled") then
        mission.playbacks[key] = nil
    end
end)

local function commandLeader(source)
    if isElement(source) and getElementType(source) == "player" then return source end
    return getElementsByType("player")[1]
end

local function reportCommandFailure(source, prefix, reason)
    local message = ("[og-loc] %s NOT STARTED: %s"):format(prefix, tostring(reason))
    if isElement(source) and getElementType(source) == "player" then
        outputChatBox(message, source, 255, 100, 80)
    else
        outputServerLog(message)
    end
end

local function startTransition(source, skip, expectedText)
    local connected = getElementsByType("player")
    local expected = tonumber(expectedText) or #connected
    if expected ~= 1 and expected ~= 2 then
        return reportCommandFailure(source, "TRANSITION", "le nombre de clients doit etre 1 ou 2")
    end
    local profile = (expected == 1 and "solo" or "coop") .. (skip and "-skip" or "-natural")
    local ok, reason = startMission(commandLeader(source), false,
                                    {transition = true, skip = skip, expectedPlayers = expected, profile = profile})
    if ok == false then reportCommandFailure(source, "TRANSITION", reason) end
end

addCommandHandler(OGL.command, function(source)
    local ok, reason = startMission(commandLeader(source), false)
    if ok == false then reportCommandFailure(source, "NATURAL", reason) end
end)
addCommandHandler("oglocnatural", function(source)
    local ok, reason = startMission(commandLeader(source), false)
    if ok == false then reportCommandFailure(source, "NATURAL", reason) end
end)
addCommandHandler("ogloctest", function(source)
    local ok, reason = startMission(commandLeader(source), true, {minimumPlayers = 2})
    if ok == false then reportCommandFailure(source, "HEADLESS", reason) end
end)
addCommandHandler("ogloctransitionnatural", function(source, _, expected)
    startTransition(source, false, expected)
end)
addCommandHandler("ogloctransitionskip", function(source, _, expected)
    startTransition(source, true, expected)
end)
addCommandHandler("oglocskip", function(source)
    if not isElement(source) or (mission and source == mission.leader) then skipCutscene() end
end)
addCommandHandler("oglocabort", function(source)
    if mission and (not isElement(source) or source == mission.leader) then
        if mission.transition then clearMission(true, {ok = false, reason = "aborted"}) else clearMission(true) end
    end
end)

setTimer(function()
    local players = getElementsByType("player")
    if fileExists("skip.request") and skipCutscene() then fileDelete("skip.request") end
    if not mission and #players == 1 and fileExists("transition-natural-1.request") then
        fileDelete("transition-natural-1.request")
        startMission(players[1], false, {transition = true, expectedPlayers = 1, profile = "solo-natural"})
    elseif not mission and #players == 2 and fileExists("transition-natural-2.request") then
        fileDelete("transition-natural-2.request")
        startMission(players[1], false, {transition = true, expectedPlayers = 2, profile = "coop-natural"})
    elseif not mission and #players == 1 and fileExists("transition-skip-1.request") then
        fileDelete("transition-skip-1.request")
        startMission(players[1], false, {transition = true, skip = true, expectedPlayers = 1, profile = "solo-skip"})
    elseif not mission and #players == 2 and fileExists("transition-skip-2.request") then
        fileDelete("transition-skip-2.request")
        startMission(players[1], false, {transition = true, skip = true, expectedPlayers = 2, profile = "coop-skip"})
    elseif not mission and #players >= 1 and fileExists("natural.request") then
        fileDelete("natural.request")
        local ok, reason = startMission(players[1], false)
        if ok == false then outputServerLog("[og-loc] NATURAL REQUEST REJECTED: " .. tostring(reason)) end
    elseif not mission and #players >= 2 and fileExists("headless.request") then
        fileDelete("headless.request")
        local ok, reason = startMission(players[1], true, {minimumPlayers = 2})
        if ok == false then outputServerLog("[og-loc] HEADLESS REQUEST REJECTED: " .. tostring(reason)) end
    end
end, 250, 0)

addEventHandler("onPlayerQuit", root, function() if mission and mission.players[source] then failMission("participant deconnecte") end end)
addEventHandler("onResourceStop", resourceRoot, function() clearMission(true) end)
outputServerLog("[og-loc] Ready: /ogloc, ogloctest (2+ clients), or transition profiles.")
