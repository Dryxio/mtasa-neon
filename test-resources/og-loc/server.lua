local mission
local serial = 0
local failMission

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
            collisions = getElementCollisionsEnabled(player), health = getElementHealth(player), armor = getPedArmor(player)}
end

local function restorePlayer(player, state)
    if not isElement(player) then return end
    if isPedInVehicle(player) then removePedFromVehicle(player) end
    setElementInterior(player, state.interior)
    setElementDimension(player, state.dimension)
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
    if isElement(handle) then exports["native-task-runtime"]:cancelNativeTaskCohort(handle) end
    if mission then
        mission.cohorts[name] = nil
        mission.cohortStates[name] = nil
    end
end

local function cancelPlayback(handle)
    if isElement(handle) then exports["native-task-runtime"]:cancelNativeRecordedVehiclePlayback(handle) end
end

local function clearMission(restore)
    if not mission or mission.cleaning then return end
    mission.cleaning = true
    for _, timer in ipairs(mission.timers) do if isTimer(timer) then killTimer(timer) end end
    for _, handle in pairs(mission.playbacks) do cancelPlayback(handle) end
    for name in pairs(mission.cohorts) do cancelCohort(name) end
    for handle in pairs(mission.vehiclePlacements) do
        if isElement(handle) then exports["story-world-runtime"]:releaseStoryScmVehicle(handle) end
    end
    for handle in pairs(mission.worldTeardowns) do
        if isElement(handle) then exports["story-world-runtime"]:releaseStoryWorldTeardown(handle) end
    end
    triggerClientEvent(participants(), "ogl:cleanup", resourceRoot, mission.id)
    for player, state in pairs(mission.players) do if restore then restorePlayer(player, state) end end
    for _, element in ipairs(mission.entities) do if isElement(element) then destroyElement(element) end end
    mission = nil
end

failMission = function(reason, textKey)
    if not mission or mission.finishing then return end
    mission.finishing = true
    setStage("failed", {reason = reason, textKey = textKey})
    trace("FAIL", {reason = reason, textKey = textKey})
    outputServerLog("[og-loc] FAIL: " .. tostring(reason))
    rememberTimer(setTimer(function() clearMission(true) end, mission.headless and 500 or 5000, 1))
end

local function barrier(kind, name, callback, payload)
    mission.sceneSerial = mission.sceneSerial + 1
    local current = {id = mission.sceneSerial, kind = kind, name = name, waiting = {}, callback = callback}
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

local function allNear(x, y, z, radius)
    for player in pairs(mission.players) do
        if not isElement(player) then return false end
        local px, py, pz = getElementPosition(player)
        if getDistanceBetweenPoints3D(px, py, pz, x, y, z) > radius then return false end
    end
    return true
end

local function placeVehicle(vehicle, position)
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

local function createTravelActors()
    local c = OGL.smokeCar
    local smoke = OGL.smokeStart
    local sweet = OGL.sweetStart
    local placementHandles = {}
    local primaryHandle
    mission.smokeCar, primaryHandle = createScmVehicle(OGL.models.glendale, c, mission.leader)
    if primaryHandle then placementHandles[#placementHandles + 1] = primaryHandle end
    -- The SCM creates both actors beside the Glendale before assigning their
    -- entry tasks. Creating them at the vehicle centre made three collision
    -- bodies overlap and launched the occupied car when physics resumed.
    mission.smoke = createScmPed(OGL.models.smoke, smoke)
    mission.sweet = createScmPed(OGL.models.sweet, sweet)
    if not mission.smokeCar or not mission.smoke or not mission.sweet then return false end
    setVehicleColor(mission.smokeCar, 98, 14, 98, 14)
    setElementHealth(mission.smokeCar, 2000)
    -- Build the occupied car as a frozen simulation island. Automatic syncer
    -- assignment previously arrived after the warps and made GTA re-admit a
    -- live, overlapping physics state, launching the whole car into the air.
    setElementFrozen(mission.smokeCar, true)
    setElementCollisionsEnabled(mission.smokeCar, false)
    local syncersAccepted = setElementSyncer(mission.smokeCar, mission.leader, true, true)
    syncersAccepted = setElementSyncer(mission.smoke, mission.leader, true, true) and syncersAccepted
    syncersAccepted = setElementSyncer(mission.sweet, mission.leader, true, true) and syncersAccepted
    if not syncersAccepted then return false end
    warpPedIntoVehicle(mission.leader, mission.smokeCar, 0)
    warpPedIntoVehicle(mission.smoke, mission.smokeCar, 1)
    warpPedIntoVehicle(mission.sweet, mission.smokeCar, 2)
    local seat = 3
    local supportIndex = 0
    for _, player in ipairs(participants()) do
        if player ~= mission.leader then
            if seat <= 3 then
                warpPedIntoVehicle(player, mission.smokeCar, seat)
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
                warpPedIntoVehicle(player, vehicle, 0)
            end
        end
    end
    return placementHandles, primaryHandle
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
    local placementHandles = {}
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
    setElementFrozen(mission.smokeCar, true)
    setElementCollisionsEnabled(mission.smokeCar, false)
    local syncersAccepted = setElementSyncer(mission.smokeCar, mission.leader, true, true)
    for _, ped in ipairs({mission.smoke, mission.sweet, mission.ogloc}) do
        syncersAccepted = setElementSyncer(ped, mission.leader, true, true) and syncersAccepted
    end
    if not syncersAccepted then return false end
    warpPedIntoVehicle(mission.leader, mission.smokeCar, 0)
    warpPedIntoVehicle(mission.smoke, mission.smokeCar, 1)
    warpPedIntoVehicle(mission.sweet, mission.smokeCar, 2)
    warpPedIntoVehicle(mission.ogloc, mission.smokeCar, 3)
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
            warpPedIntoVehicle(player, vehicle, 0)
        end
    end
    return placementHandles, primaryHandle
end

local beginPoliceCutscene, beginHouseScene, setupChase, startNextRecording, beginCombat, beginBurgerReturn, passMission

local function beginDriveToPolice()
    local handles, primaryHandle = createTravelActors()
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
        setStage("drive_police", {objective = OGL.police, textKey = "SMK1_02", probeVehicle = mission.smokeCar,
                                  probeScriptZ = placement.scriptZ,
                                  probeActors = {smoke = mission.smoke, sweet = mission.sweet}})
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
            local handles, primaryHandle = createPoliceTravelActors()
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
                setStage("reconstruct_police", {probeVehicle = mission.smokeCar,
                                                probeScriptZ = placement.scriptZ,
                                                probeActors = {smoke = mission.smoke, sweet = mission.sweet}})
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
        if mission.headless then rememberTimer(setTimer(setupChase, 250, 1)) end
    end, {actors = {smoke = mission.smoke, sweet = mission.sweet, ogloc = mission.ogloc},
           vehicle = mission.smokeCar})
end

local function createFreddyCohort()
    local handle, reason = exports["native-task-runtime"]:createNativeTaskCohort(mission.leader, {
        members = {{ped = mission.freddy, vehicle = mission.freddyBike, seat = 0, missionActor = true,
                    proofs = {bullet = true, fire = true, explosion = false, collision = true, melee = true},
                    task = {type = "drive_by", target = mission.leader, radius = 300,
                            style = "ai_all_directions", rightHandSide = false, frequency = 100, reissue = true}}},
        vehicles = {{vehicle = mission.freddyBike, straightLineDistance = 10}},
        dependencies = {mission.leader},
    }, {fallbackOwners = {}})
    if not handle then return false, reason end
    mission.cohorts.freddy = handle
    return true
end

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
                for player, bike in pairs(mission.playerBikes) do
                    activateVehicle(bike)
                    setElementFrozen(player, false)
                    toggleAllControls(player, true, true, true)
                    warpPedIntoVehicle(player, bike, 0)
                end
                if isElement(mission.ogloc) then
                    warpPedIntoVehicle(mission.ogloc, mission.playerBikes[mission.leader], 1)
                end
                activateVehicle(mission.freddyBike)
                warpPedIntoVehicle(mission.freddy, mission.freddyBike, 0)
                setStage("chase_wait_authority", {textKey = "SMK1_04", target = mission.freddy})
                local ok, reason = createFreddyCohort()
                if not ok then failMission("cohorte Freddy refusee: " .. tostring(reason)) end
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
        setStage("chase_recording:" .. tostring(recordingId), {recordingId = recordingId, textKey = "SMK1_04"})
        local options = {}
        for key, value in pairs(OGL.playback) do options[key] = value end
        if mission.headless and recordingId ~= 30 then
            -- Recording 30 exercises the real distance formula. Subsequent headless
            -- segments run at the native API maximum so the bounded harness does not
            -- depend on simulated player input fighting the player's own sync stream.
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
    setElementPosition(mission.freddy, 2300.3, -1502.8, 24.3)
    mission.goons = {}
    for _, p in ipairs(OGL.goons) do
        local ped = createScmPed(OGL.models.goon, p)
        if not ped then return failMission("creation goon refusee") end
        giveWeapon(ped, 32, 30000, true)
        setElementHealth(ped, 500)
        mission.goons[#mission.goons + 1] = ped
    end
    local members = {{ped = mission.freddy, missionActor = true,
                      task = {type = "kill_on_foot", target = mission.leader, reissue = true}}}
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
    cancelCohort("combat")
    for _, ped in ipairs(mission.goons or {}) do if isElement(ped) then destroyElement(ped) end end
    setElementPosition(mission.ogloc, 2295.3, -1491.3, 22.3)
    if isElement(mission.playerBikes[mission.leader]) then
        warpPedIntoVehicle(mission.ogloc, mission.playerBikes[mission.leader], 1)
    end
    barrier("scene", "freddy_dead", function()
        setStage("return_burger_shot", {objective = OGL.burgerShot, textKey = "SMK1_13"})
        if mission.headless then
            rememberTimer(setTimer(function()
                if not mission or mission.stage ~= "return_burger_shot" then return end
                for _, bike in pairs(mission.playerBikes) do
                    placeVehicle(bike, {OGL.burgerShot[1], OGL.burgerShot[2], OGL.burgerShot[3] + 0.5, 0})
                end
                barrier("scene", "burger_shot", passMission, {actors = {ogloc = mission.ogloc}})
            end, 250, 1))
        end
    end, {actors = {ogloc = mission.ogloc}})
end

passMission = function()
    if not mission or mission.finishing then return end
    mission.finishing = true
    setStage("passed", {respect = 5})
    trace("PASS", {respect = 5, recordings = #OGL.chaseRecordings})
    outputServerLog("[og-loc] PASS: SMOKE1, 11 chase recordings, dynamic 06FD and co-op return completed")
    triggerClientEvent(participants(), "ogl:passed", resourceRoot, 5)
    rememberTimer(setTimer(function() clearMission(true) end, mission.headless and 500 or 5000, 1))
end

local function validateMission()
    if not mission or mission.finishing then return end
    for player in pairs(mission.players) do
        if not isElement(player) or isPedDead(player) then return failMission("participant mort ou absent") end
    end
    if mission.stage == "drive_police" then
        if not isElement(mission.smoke) or isPedDead(mission.smoke) then return failMission("Big Smoke tue", "SMK1_06") end
        if not isElement(mission.sweet) or isPedDead(mission.sweet) then return failMission("Sweet tue", "SMK1_07") end
        if not isElement(mission.smokeCar) or isVehicleBlown(mission.smokeCar) then return failMission("Glendale detruite", "SMK1_08") end
        if allNear(OGL.police[1], OGL.police[2], OGL.police[3], 10) then beginPoliceCutscene() end
    elseif mission.stage == "drive_house" then
        if not isElement(mission.ogloc) or isPedDead(mission.ogloc) then return failMission("OG Loc tue", "SMK1_05") end
        if allNear(OGL.house[1], OGL.house[2], OGL.house[3], 12) then beginHouseScene() end
    elseif mission.stage == "doorbell" then
        if allNear(OGL.doorbell[1], OGL.doorbell[2], OGL.doorbell[3], 5) then setupChase() end
    elseif mission.stage and mission.stage:find("^chase_") then
        for _, bike in pairs(mission.playerBikes or {}) do
            if not isElement(bike) or isVehicleBlown(bike) then return failMission("PCJ-600 detruite", "SMK1_14") end
        end
        if not isElement(mission.ogloc) or isPedDead(mission.ogloc) then return failMission("OG Loc tue", "SMK1_05") end
        if mission.headless and isElement(mission.freddyBike) then
            local fx, fy, fz = getElementPosition(mission.freddyBike)
            for index, bike in pairs(mission.playerBikes or {}) do
                if isElement(bike) then
                    local offset = index == mission.leader and 0 or 3
                    setElementPosition(bike, fx - 10 - offset, fy, fz + 0.2)
                end
            end
        end
        if isElement(mission.freddy) and not isPedDead(mission.freddy) and not mission.headless then
            local x, y = getElementPosition(mission.leader)
            local fx, fy = getElementPosition(mission.freddy)
            if getDistanceBetweenPoints2D(x, y, fx, fy) > 250 then return failMission("Freddy a fui", "SMK1_12") end
        end
    elseif mission.stage == "basketball_combat" then
        if isPedDead(mission.freddy) then return beginBurgerReturn() end
    elseif mission.stage == "return_burger_shot" and allNear(OGL.burgerShot[1], OGL.burgerShot[2], OGL.burgerShot[3], 12) then
        barrier("scene", "burger_shot", passMission, {actors = {ogloc = mission.ogloc}})
    end
end

local function startMission(leader, headless)
    if mission then return false, "une mission est deja active" end
    if not isElement(leader) or getElementType(leader) ~= "player" then return false, "aucun joueur disponible" end
    serial = serial + 1
    mission = {id = serial, leader = leader, headless = headless == true, stage = "starting", players = {},
               entities = {}, timers = {}, cohorts = {}, cohortStates = {}, playbacks = {}, auxVehicles = {},
               auxVehiclePositions = {},
               vehiclePlacements = {}, worldTeardowns = {}, sceneSerial = 0,
               recordingIndex = 0, lastPlaybackSamples = {}}
    for _, player in ipairs(getElementsByType("player")) do
        mission.players[player] = snapshotPlayer(player)
        if isPedInVehicle(player) then removePedFromVehicle(player) end
        setElementInterior(player, 0)
        setElementDimension(player, OGL.dimension)
        -- The native file cutscene owns presentation until its release
        -- barrier. Keep the player's pre-mission transform until then; the
        -- world reconstruction will place or warp it under the black frame.
        setElementFrozen(player, true)
        setElementCollisionsEnabled(player, false)
        toggleAllControls(player, false, true, true)
    end
    trace("start", {leader = getPlayerName(leader), players = #participants(), headless = mission.headless})
    triggerClientEvent(participants(), "ogl:start", resourceRoot, mission.id, leader, mission.headless)
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
    current.waiting[client] = nil
    for player in pairs(current.waiting) do if isElement(player) then return end end
    mission.barrier = nil
    current.callback()
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
addEventHandler("ogl:cutsceneProbe", resourceRoot, function(id, name, phase)
    local current = mission and mission.barrier
    if source ~= resourceRoot or not current or current.id ~= tonumber(id) or not current.waiting[client] then return end
    trace("cutscene_probe", {name = tostring(name), phase = tostring(phase), player = getPlayerName(client)})
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
        failMission(("vehicule reconstruit ejecte a Z %.3f"):format(tonumber(sample.z)))
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
                    if mission and mission.stage == "drive_house" then
                        placeVehicle(mission.smokeCar, {OGL.house[1], OGL.house[2] - 10, OGL.house[3], 0})
                        for _, auxVehicle in pairs(mission.auxVehicles) do
                            placeVehicle(auxVehicle, {OGL.house[1] - 5, OGL.house[2] - 10, OGL.house[3], 0})
                        end
                        beginHouseScene()
                    end
                end, 250, 1))
            end
        elseif mission.headless then
            rememberTimer(setTimer(function()
                if mission and mission.stage == "drive_police" then
                    placeVehicle(mission.smokeCar, OGL.police)
                    for _, auxVehicle in pairs(mission.auxVehicles) do placeVehicle(auxVehicle, OGL.police) end
                    beginPoliceCutscene()
                end
            end, 250, 1))
        end
    end
end)

local function skipCutscene()
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
    elseif name == "combat" and state == "active" and mission.headless then
        rememberTimer(setTimer(function()
            if mission and mission.stage == "basketball_combat" and isElement(mission.freddy) then
                setElementHealth(mission.freddy, 0)
            end
        end, 500, 1))
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

addCommandHandler(OGL.command, function(player)
    local ok, reason = startMission(player, false)
    if ok == false and isElement(player) then outputChatBox("[OG Loc] " .. tostring(reason), player, 255, 100, 80) end
end)
addCommandHandler("oglocnatural", function(player)
    local ok, reason = startMission(isElement(player) and player or getElementsByType("player")[1], false)
    if ok == false then outputServerLog("[og-loc] NATURAL NOT STARTED: " .. tostring(reason)) end
end)
addCommandHandler("ogloctest", function(player)
    local ok, reason = startMission(isElement(player) and player or getElementsByType("player")[1], true)
    if ok == false then outputServerLog("[og-loc] HEADLESS NOT STARTED: " .. tostring(reason)) end
end)
addCommandHandler("oglocskip", function(player) if not isElement(player) or (mission and player == mission.leader) then skipCutscene() end end)
addCommandHandler("oglocabort", function(player) if mission and (not isElement(player) or player == mission.leader) then clearMission(true) end end)

setTimer(function()
    local players = getElementsByType("player")
    if fileExists("skip.request") then fileDelete("skip.request") skipCutscene() end
    if #players >= 1 and fileExists("natural.request") then
        fileDelete("natural.request")
        local ok, reason = startMission(players[1], false)
        if ok == false then outputServerLog("[og-loc] NATURAL REQUEST REJECTED: " .. tostring(reason)) end
    elseif #players >= 1 and fileExists("headless.request") then
        fileDelete("headless.request")
        local ok, reason = startMission(players[1], true)
        if ok == false then outputServerLog("[og-loc] HEADLESS REQUEST REJECTED: " .. tostring(reason)) end
    end
end, 250, 0)

addEventHandler("onPlayerQuit", root, function() if mission and mission.players[source] then failMission("participant deconnecte") end end)
addEventHandler("onResourceStop", resourceRoot, function() clearMission(true) end)
outputServerLog("[og-loc] Ready: /ogloc or headless ogloctest.")
