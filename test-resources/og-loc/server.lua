local mission
local serial = 0

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

local function setStage(stage, payload)
    mission.stage = stage
    mission.stageStartedAt = getTickCount()
    trace("stage", {value = stage})
    triggerClientEvent(participants(), "ogl:stage", resourceRoot, stage, payload or {})
end

local function cancelCohort(name)
    local handle = mission and mission.cohorts[name]
    if isElement(handle) then exports["native-task-runtime"]:cancelNativeTaskCohort(handle) end
    if mission then mission.cohorts[name] = nil end
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
    triggerClientEvent(participants(), "ogl:cleanup", resourceRoot, mission.id)
    for player, state in pairs(mission.players) do if restore then restorePlayer(player, state) end end
    for _, element in ipairs(mission.entities) do if isElement(element) then destroyElement(element) end end
    mission = nil
end

local function failMission(reason, textKey)
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

local function createTravelActors()
    local c = OGL.smokeCar
    mission.smokeCar = track(createVehicle(OGL.models.glendale, c[1], c[2], c[3], 0, 0, c[4]))
    mission.smoke = track(createPed(OGL.models.smoke, c[1], c[2], c[3] + 1, c[4]))
    mission.sweet = track(createPed(OGL.models.sweet, c[1], c[2], c[3] + 1, c[4]))
    if not mission.smokeCar or not mission.smoke or not mission.sweet then return false end
    setVehicleColor(mission.smokeCar, 98, 14, 98, 14)
    setElementHealth(mission.smokeCar, 2000)
    warpPedIntoVehicle(mission.leader, mission.smokeCar, 0)
    warpPedIntoVehicle(mission.smoke, mission.smokeCar, 1)
    warpPedIntoVehicle(mission.sweet, mission.smokeCar, 2)
    local seat = 3
    for _, player in ipairs(participants()) do
        if player ~= mission.leader then
            if seat <= 3 then
                warpPedIntoVehicle(player, mission.smokeCar, seat)
                seat = seat + 1
            else
                local offset = #mission.auxVehicles * 4
                local vehicle = track(createVehicle(OGL.models.glendale, c[1] - offset, c[2] - 4, c[3], 0, 0, c[4]))
                mission.auxVehicles[player] = vehicle
                warpPedIntoVehicle(player, vehicle, 0)
            end
        end
    end
    return true
end

local beginPoliceCutscene, beginHouseScene, setupChase, startNextRecording, beginCombat, beginBurgerReturn, passMission

local function beginDriveToPolice()
    if not createTravelActors() then return failMission("creation des acteurs Grove refusee") end
    for player in pairs(mission.players) do
        setElementFrozen(player, false)
        setElementCollisionsEnabled(player, true)
        toggleAllControls(player, true, true, true)
    end
    setStage("drive_police", {objective = OGL.police, textKey = "SMK1_02"})
    if mission.headless then
        rememberTimer(setTimer(function()
            if mission and mission.stage == "drive_police" then
                placeVehicle(mission.smokeCar, OGL.police)
                for player, vehicle in pairs(mission.auxVehicles) do placeVehicle(vehicle, OGL.police) end
                beginPoliceCutscene()
            end
        end, 250, 1))
    end
end

beginPoliceCutscene = function()
    if mission.stage ~= "drive_police" then return end
    barrier("cutscene", OGL.cutscenes.police, function()
        local p = OGL.police
        placeVehicle(mission.smokeCar, p)
        mission.ogloc = track(createPed(OGL.models.ogloc, p[1] + 9, p[2] - 14, p[3] + 0.5, 97.2))
        if not mission.ogloc then return failMission("creation OG Loc refusee") end
        setElementHealth(mission.ogloc, 2000)
        warpPedIntoVehicle(mission.ogloc, mission.smokeCar, 3)
        -- The extra co-op rider drives a support Glendale after OG Loc occupies the retail fourth seat.
        for _, player in ipairs(participants()) do
            if player ~= mission.leader and getPedOccupiedVehicle(player) == mission.smokeCar then
                removePedFromVehicle(player)
                local vehicle = track(createVehicle(OGL.models.glendale, p[1] + 4, p[2], p[3], 0, 0, p[4]))
                mission.auxVehicles[player] = vehicle
                warpPedIntoVehicle(player, vehicle, 0)
            end
        end
        setStage("drive_house", {objective = OGL.house, textKey = "SMK1_10"})
        if mission.headless then
            rememberTimer(setTimer(function()
                if mission and mission.stage == "drive_house" then
                    placeVehicle(mission.smokeCar, {OGL.house[1], OGL.house[2] - 10, OGL.house[3], 0})
                    for _, vehicle in pairs(mission.auxVehicles) do
                        placeVehicle(vehicle, {OGL.house[1] - 5, OGL.house[2] - 10, OGL.house[3], 0})
                    end
                    beginHouseScene()
                end
            end, 250, 1))
        end
    end)
end

beginHouseScene = function()
    if mission.stage ~= "drive_house" then return end
    barrier("scene", "freddys_house_arrival", function()
        for player in pairs(mission.players) do
            if isPedInVehicle(player) then removePedFromVehicle(player) end
            setElementPosition(player, 2457.4, -1286.1, 23.0)
        end
        if isElement(mission.ogloc) then
            removePedFromVehicle(mission.ogloc)
            setElementPosition(mission.ogloc, 2463.9, -1278.4, 29.0)
        end
        setStage("doorbell", {objective = OGL.doorbell, textKey = "SMK1_03"})
        if mission.headless then rememberTimer(setTimer(setupChase, 250, 1)) end
    end, {actors = {smoke = mission.smoke, sweet = mission.sweet, ogloc = mission.ogloc}})
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
    barrier("scene", "doorbell", function()
        mission.playerBikes = {}
        for index, player in ipairs(participants()) do
            local p = OGL.playerBikes[((index - 1) % #OGL.playerBikes) + 1]
            local bike = track(createVehicle(OGL.models.pcj600, p[1], p[2], p[3], 0, 0, p[4]))
            if not bike then return failMission("creation PCJ-600 de participant refusee") end
            setElementHealth(bike, 2000)
            exports["native-task-runtime"]:setSynchronizedVehicleTyresCanBurst(bike, false)
            mission.playerBikes[player] = bike
            warpPedIntoVehicle(player, bike, 0)
        end
        if isElement(mission.ogloc) then warpPedIntoVehicle(mission.ogloc, mission.playerBikes[mission.leader], 1) end
        local b, f = OGL.freddyBike, OGL.freddy
        mission.freddyBike = track(createVehicle(OGL.models.pcj600, b[1], b[2], b[3], 0, 0, b[4]))
        mission.freddy = track(createPed(OGL.models.freddy, f[1], f[2], f[3], f[4]))
        if not mission.freddyBike or not mission.freddy then return failMission("creation de Freddy refusee") end
        setElementHealth(mission.freddyBike, 1000)
        exports["native-task-runtime"]:setSynchronizedVehicleTyresCanBurst(mission.freddyBike, false)
        giveWeapon(mission.freddy, 32, 30000, true)
        setElementHealth(mission.freddy, 1000)
        warpPedIntoVehicle(mission.freddy, mission.freddyBike, 0)
        setStage("chase_wait_authority", {textKey = "SMK1_04", target = mission.freddy})
        local ok, reason = createFreddyCohort()
        if not ok then failMission("cohorte Freddy refusee: " .. tostring(reason)) end
    end, {actors = {ogloc = mission.ogloc}})
end

local function startObstacleRecordings()
    if mission.obstaclesStarted then return end
    mission.obstaclesStarted = true
    for _, config in ipairs(OGL.obstacleRecordings) do
        local vehicle = track(createVehicle(config[2], config[3], config[4], config[5]))
        if vehicle then
            setElementSyncer(vehicle, mission.leader, true)
            local handle, reason = exports["native-task-runtime"]:createNativeRecordedVehiclePlayback(
                vehicle, config[1], mission.leader, {pivotSpeed = 1, minimumSpeed = 1, maximumSpeed = 1,
                                                    loadTimeout = 15000, playbackTimeout = 120000})
            if handle then mission.playbacks["obstacle:" .. tostring(config[1])] = handle
            else return failMission("recording obstacle " .. tostring(config[1]) .. ": " .. tostring(reason)) end
        else
            return failMission("creation obstacle refusee")
        end
    end
end

startNextRecording = function()
    if not mission or mission.finishing then return end
    mission.recordingIndex = mission.recordingIndex + 1
    local recordingId = OGL.chaseRecordings[mission.recordingIndex]
    if not recordingId then return beginCombat() end
    if recordingId == 31 then placeVehicle(mission.freddyBike, OGL.chaseStart) end
    if recordingId == 35 then startObstacleRecordings() end
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
    if not handle then return failMission("recording " .. tostring(recordingId) .. " refuse: " .. tostring(reason)) end
    mission.playbacks.main = handle
    mission.mainRecording = recordingId
end

beginCombat = function()
    cancelCohort("freddy")
    if isPedInVehicle(mission.freddy) then removePedFromVehicle(mission.freddy) end
    setElementPosition(mission.freddy, 2300.3, -1502.8, 24.3)
    mission.goons = {}
    for _, p in ipairs(OGL.goons) do
        local ped = track(createPed(OGL.models.goon, p[1], p[2], p[3], p[4]))
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
               entities = {}, timers = {}, cohorts = {}, playbacks = {}, auxVehicles = {}, sceneSerial = 0,
               recordingIndex = 0, lastPlaybackSamples = {}}
    for _, player in ipairs(getElementsByType("player")) do
        mission.players[player] = snapshotPlayer(player)
        if isPedInVehicle(player) then removePedFromVehicle(player) end
        setElementInterior(player, 0)
        setElementDimension(player, OGL.dimension)
        setElementPosition(player, OGL.grove[1], OGL.grove[2], OGL.grove[3])
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
    if ok ~= true then return failMission(current.kind .. " " .. current.name .. ": " .. tostring(reason)) end
    current.waiting[client] = nil
    for player in pairs(current.waiting) do if isElement(player) then return end end
    mission.barrier = nil
    current.callback()
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
    if state == "active" and data.sample then return end
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
    if #players >= 2 and fileExists("natural.request") then
        fileDelete("natural.request")
        local ok, reason = startMission(players[1], false)
        if ok == false then outputServerLog("[og-loc] NATURAL REQUEST REJECTED: " .. tostring(reason)) end
    elseif #players >= 2 and fileExists("headless.request") then
        fileDelete("headless.request")
        local ok, reason = startMission(players[1], true)
        if ok == false then outputServerLog("[og-loc] HEADLESS REQUEST REJECTED: " .. tostring(reason)) end
    end
end, 250, 0)

addEventHandler("onPlayerQuit", root, function() if mission and mission.players[source] then failMission("participant deconnecte") end end)
addEventHandler("onResourceStop", resourceRoot, function() clearMission(true) end)
outputServerLog("[og-loc] Ready: /ogloc or headless ogloctest.")
