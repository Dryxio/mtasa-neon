local mission
local serial = 0

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

local function cancelCohort(name)
    local handle = mission and mission.cohorts[name]
    if isElement(handle) then
        exports["native-task-runtime"]:cancelNativeTaskCohort(handle)
    end
    if mission then
        mission.cohorts[name] = nil
    end
end

local function clearMission(restorePlayers)
    if not mission or mission.cleaning then
        return
    end
    mission.cleaning = true
    for _, timer in ipairs(mission.timers) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    for name in pairs(mission.cohorts) do
        cancelCohort(name)
    end
    triggerClientEvent(participants(), "sak:cleanup", resourceRoot, mission.id)
    for player, state in pairs(mission.players) do
        if restorePlayers and isElement(player) then
            restorePlayer(player, state)
        end
    end
    for _, element in ipairs(mission.entities) do
        if isElement(element) then
            destroyElement(element)
        end
    end
    mission = nil
end

local function failMission(reason, textKey)
    if not mission or mission.finishing then
        return
    end
    mission.finishing = true
    setStage("failed", {reason = reason, textKey = textKey})
    trace("FAIL", {reason = reason})
    outputServerLog("[sweet-and-kendl] FAIL: " .. tostring(reason))
    rememberTimer(setTimer(function()
        clearMission(true)
    end, mission.headless and 500 or 5000, 1))
end

local function beginBarrier(kind, name, callback, payload)
    mission.sceneSerial = mission.sceneSerial + 1
    mission.barrier = {id = mission.sceneSerial, kind = kind, name = name, waiting = {}, callback = callback}
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
    local eventName = kind == "cutscene" and "sak:fileCutscene" or "sak:scene"
    triggerClientEvent(participants(), eventName, resourceRoot, mission.sceneSerial, name, mission.leader, payload or mission.actors)
    rememberTimer(setTimer(function()
        if mission and mission.barrier == expected then
            failMission(kind .. " " .. name .. " non termine par toute la partie")
        end
    end, kind == "cutscene" and 150000 or 120000, 1))
end

local function positionVehicle(vehicle, values)
    setElementPosition(vehicle, values[1], values[2], values[3])
    setElementRotation(vehicle, 0, 0, values[4])
end

local function createGang()
    mission.gang = {}
    for _, role in ipairs({"smoke", "ryder", "sweet"}) do
        local config = SAK.gang[role]
        local bike = track(createVehicle(SAK.models.bmx, config.bike[1], config.bike[2], config.bike[3], 0, 0, config.bike[4]))
        local ped = track(createPed(config.model, config.ped[1], config.ped[2], config.ped[3], config.ped[4]))
        if not bike or not ped then
            return false
        end
        setElementHealth(bike, 2000)
        exports["native-task-runtime"]:setSynchronizedVehicleTyresCanBurst(bike, false)
        setElementHealth(ped, 2000)
        warpPedIntoVehicle(ped, bike, 0)
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
    local passenger = track(createPed(103, config.vehicle[1], config.vehicle[2], config.vehicle[3] + 1, config.vehicle[4]))
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
        members[#members + 1] = {
            ped = actor.ped, vehicle = actor.bike, seat = 0, missionActor = true,
            task = {type = "drive_route", route = routes[index], loop = false},
        }
        vehicles[#vehicles + 1] = {vehicle = actor.bike, straightLineDistance = 10}
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

local beginRide
local beginReturnRide
local beginFinale

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
        setElementFrozen(player, true)
        setElementAlpha(player, player == mission.leader and 255 or 0)
    end
    rememberTimer(setTimer(function()
        if mission and mission.stage == "scene:funeral" and isElement(mission.actors.peren) then
            blowVehicle(mission.actors.peren)
        end
    end, 6500, 1))
    beginBarrier("scene", "funeral", beginRide, mission.actors)
end

local function beginFuneralFile()
    for player in pairs(mission.players) do
        if isPedInVehicle(player) then
            removePedFromVehicle(player)
        end
        setElementPosition(player, 910.78, -1075.26, 23.29)
        setElementRotation(player, 0, 0, 265.0)
    end
    beginBarrier("cutscene", SAK.cutscenes.funeral, prepareFuneral)
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
    warpPedIntoVehicle(smoke, peren, 1)
    local leader = mission.leader
    setElementPosition(leader, 2495.59, -1686.96, 12.51)
    setElementRotation(leader, 0, 0, 32.0)
    warpPedIntoVehicle(leader, peren, 0)
    for player in pairs(mission.players) do
        setElementFrozen(player, true)
        setElementAlpha(player, player == leader and 255 or 0)
    end
    setElementSyncer(peren, leader, true, true)
    setElementSyncer(smoke, leader, true, true)
    mission.actors.peren, mission.actors.smoke = peren, smoke
    beginBarrier("scene", "smoke", beginFuneralFile, mission.actors)
end

beginRide = function()
    if not mission then
        return
    end
    setStage("ride1")
    triggerClientEvent(participants(), "sak:stopRecording", resourceRoot, 201, mission.ballas.vehicle)
    for index, player in ipairs(participants()) do
        local bike = mission.playerBikes[player]
        local position = SAK.playerBikes[((index - 1) % #SAK.playerBikes) + 1]
        setElementPosition(player, position[1], position[2], position[3] + 0.4)
        setElementAlpha(player, 255)
        setElementCollisionsEnabled(player, true)
        setElementFrozen(player, false)
        toggleAllControls(player, true, true, true)
        warpPedIntoVehicle(player, bike, 0)
    end
    for _, role in ipairs({"smoke", "ryder", "sweet"}) do
        local actor, config = mission.gang[role], SAK.gang[role]
        positionVehicle(actor.bike, config.bike)
        if getVehicleOccupant(actor.bike, 0) ~= actor.ped then
            warpPedIntoVehicle(actor.ped, actor.bike, 0)
        end
    end
    positionVehicle(mission.ballas.vehicle, SAK.ballas.chase)
    setElementFrozen(mission.ballas.vehicle, false)
    local routeOk, routeReason = createRouteCohort("gang", {"smoke", "ryder", "sweet"},
                                                   {SAK.route1, SAK.route1, SAK.route1})
    if not routeOk then
        return failMission("cohorte BMX refusee: " .. tostring(routeReason))
    end
    local ballasOk, ballasReason = startBallasCohort()
    if not ballasOk then
        return failMission("cohorte Ballas refusee: " .. tostring(ballasReason))
    end
    triggerClientEvent(participants(), "sak:ambientAudio", resourceRoot, "ride1", mission.actors)
    if mission.headless then
        rememberTimer(setTimer(function()
            if not mission or mission.stage ~= "ride1" then return end
            for player, bike in pairs(mission.playerBikes) do
                positionVehicle(bike, {SAK.split[1], SAK.split[2] - (player == mission.leader and 0 or 2), SAK.split[3], 250})
            end
            for _, actor in pairs(mission.gang) do
                positionVehicle(actor.bike, {SAK.split[1], SAK.split[2], SAK.split[3], 250})
            end
        end, 3000, 1))
    end
end

local function allPlayersNear(x, y, z, radius)
    for player in pairs(mission.players) do
        local px, py, pz = getElementPosition(player)
        if getDistanceBetweenPoints3D(px, py, pz, x, y, z) > radius then
            return false
        end
    end
    return true
end

local function startSplitScene()
    cancelCohort("gang")
    cancelCohort("ballas")
    setStage("split_setup")
    for player, bike in pairs(mission.playerBikes) do
        positionVehicle(bike, SAK.splitStaging.player)
        if getVehicleOccupant(bike, 0) ~= player then
            warpPedIntoVehicle(player, bike, 0)
        end
        setElementFrozen(player, true)
    end
    for _, role in ipairs({"smoke", "ryder", "sweet"}) do
        local actor = mission.gang[role]
        positionVehicle(actor.bike, SAK.splitStaging[role])
        if getVehicleOccupant(actor.bike, 0) ~= actor.ped then
            warpPedIntoVehicle(actor.ped, actor.bike, 0)
        end
    end
    positionVehicle(mission.ballas.vehicle, SAK.splitStaging.ballas)
    beginBarrier("scene", "split", beginReturnRide, mission.actors)
end

beginReturnRide = function()
    setStage("ride2")
    positionVehicle(mission.gang.sweet.bike, {1540.2981, -1159.1885, -50.3438, 170})
    setElementFrozen(mission.gang.sweet.bike, true)
    positionVehicle(mission.ballas.vehicle, {1540.2981, -1171.1885, -50.3438, 182})
    setElementFrozen(mission.ballas.vehicle, true)
    for player in pairs(mission.players) do
        setElementFrozen(player, false)
        toggleAllControls(player, true, true, true)
    end
    local ok, reason = createRouteCohort("gang", {"ryder", "smoke"}, {SAK.route2, SAK.smokeRoute2})
    if not ok then
        return failMission("cohorte de retour refusee: " .. tostring(reason))
    end
    triggerClientEvent(participants(), "sak:ambientAudio", resourceRoot, "returnRyder", mission.actors)
    if mission.headless then
        rememberTimer(setTimer(function()
            if not mission or mission.stage ~= "ride2" then return end
            for player, bike in pairs(mission.playerBikes) do
                positionVehicle(bike, {SAK.grove[1], SAK.grove[2], SAK.grove[3] + 0.6, 80})
            end
            positionVehicle(mission.gang.ryder.bike, {SAK.grove[1] + 3, SAK.grove[2], SAK.grove[3] + 0.6, 80})
            positionVehicle(mission.gang.smoke.bike, {SAK.grove[1] + 5, SAK.grove[2], SAK.grove[3] + 0.6, 80})
        end, 3000, 1))
    end
end

local function stageFinaleActors()
    cancelCohort("gang")
    cancelCohort("ballas")
    for player, bike in pairs(mission.playerBikes) do
        positionVehicle(bike, {2487.3093, -1668.3717, 12.8438, 80.0})
        if getVehicleOccupant(bike, 0) ~= player then
            warpPedIntoVehicle(player, bike, 0)
        end
        setElementFrozen(player, true)
        setElementAlpha(player, player == mission.leader and 255 or 0)
    end
    positionVehicle(mission.gang.smoke.bike, {2487.1721, -1666.3010, 12.8438, 124.2698})
    positionVehicle(mission.gang.ryder.bike, {2470.0, -1688.0, 12.84, 0})
    positionVehicle(mission.gang.sweet.bike, {2371.5002, -1654.8945, 12.8826, 80})
    for _, role in ipairs({"smoke", "ryder", "sweet"}) do
        local actor = mission.gang[role]
        setElementFrozen(actor.bike, false)
        setElementSyncer(actor.bike, mission.leader, true, true)
        setElementSyncer(actor.ped, mission.leader, true, true)
    end
end

local function passMission()
    if not mission or mission.finishing then
        return
    end
    mission.finishing = true
    setStage("passed", {respect = 3})
    trace("PASS", {respect = 3})
    outputServerLog("[sweet-and-kendl] PASS: INTRO1 gameplay, two rides and synchronized finale completed")
    triggerClientEvent(participants(), "sak:passed", resourceRoot, 3)
    rememberTimer(setTimer(function()
        clearMission(true)
    end, mission.headless and 500 or 5000, 1))
end

beginFinale = function()
    setStage("finale_setup")
    stageFinaleActors()
    beginBarrier("scene", "finale", function()
        beginBarrier("scene", "save_tutorial", passMission, mission.actors)
    end, mission.actors)
end

local function validateGameplay()
    if not mission or mission.finishing or (mission.stage ~= "ride1" and mission.stage ~= "ride2") then
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
    for role, actor in pairs(mission.gang) do
        if not isElement(actor.ped) or isPedDead(actor.ped) or not isElement(actor.bike) or isVehicleBlown(actor.bike) then
            local key = role == "smoke" and "INT2_F1" or role == "sweet" and "INT2_F2" or "INT2_F3"
            return failMission(role .. " est mort ou son BMX est detruit", key)
        end
    end
    if mission.stage == "ride1" then
        local sx, sy, sz = getElementPosition(mission.gang.sweet.ped)
        if allPlayersNear(SAK.split[1], SAK.split[2], SAK.split[3], 25) and
            getDistanceBetweenPoints3D(sx, sy, sz, SAK.split[1], SAK.split[2], SAK.split[3]) <= 25 then
            return startSplitScene()
        end
        local leaderBike, ballas = mission.playerBikes[mission.leader], mission.ballas
        if isElement(leaderBike) and ballas and isElement(ballas.vehicle) then
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
        if not mission.returnBallasStarted then
            local x, y, z = getElementPosition(mission.leader)
            if getDistanceBetweenPoints3D(x, y, z, 1981.2915, -1510.2186, 2.3844) <= 55 then
                mission.returnBallasStarted = true
                setElementFrozen(mission.ballas.vehicle, false)
                positionVehicle(mission.ballas.vehicle, SAK.ballas.returnChase)
                local ok, reason = startBallasCohort()
                if not ok then
                    return failMission("retour Ballas refuse: " .. tostring(reason))
                end
                trace("ballas_return", {})
            end
        end
        local rx, ry, rz = getElementPosition(mission.gang.ryder.ped)
        if allPlayersNear(SAK.grove[1], SAK.grove[2], SAK.grove[3], 12) and
            getDistanceBetweenPoints3D(rx, ry, rz, SAK.grove[1], SAK.grove[2], SAK.grove[3]) <= 60 then
            return beginFinale()
        end
    end
end

local function startMission(leader, headless)
    if mission then
        return false, "une mission est deja active"
    end
    if not isElement(leader) or getElementType(leader) ~= "player" then
        return false, "aucun joueur disponible"
    end
    serial = serial + 1
    mission = {
        id = serial, leader = leader, headless = headless == true, stage = "starting", players = {}, entities = {},
        timers = {}, cohorts = {}, actors = {}, sceneSerial = 0, finishing = false,
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
    trace("start", {leader = getPlayerName(leader), players = #participants(), headless = mission.headless})
    triggerClientEvent(participants(), "sak:start", resourceRoot, mission.id, leader, mission.headless)
    if mission.headless then
        prepareFuneral()
    else
        beginBarrier("cutscene", SAK.cutscenes.intro, prepareSmokeScene)
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

addEvent("sak:skipRequest", true)
local function broadcastCutsceneSkip()
    if not mission or not mission.barrier or mission.barrier.kind ~= "cutscene" then
        return false
    end
    triggerClientEvent(participants(), "sak:skipCutscene", resourceRoot, mission.barrier.id)
    return true
end

addEventHandler("sak:skipRequest", resourceRoot, function(id)
    if source == resourceRoot and mission and client == mission.leader and mission.barrier and mission.barrier.id == id and
        mission.barrier.kind == "cutscene" then
        broadcastCutsceneSkip()
    end
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

addCommandHandler(SAK.command, function(player)
    local ok, reason = startMission(player, false)
    if ok == false and isElement(player) then
        outputChatBox("[Sweet & Kendl] " .. tostring(reason), player, 255, 100, 80)
    end
end)

addCommandHandler("sweetandkendlnatural", function(player)
    local leader = isElement(player) and player or getElementsByType("player")[1]
    local ok, reason = startMission(leader, false)
    if ok == false then
        outputServerLog("[sweet-and-kendl] NATURAL NOT STARTED: " .. tostring(reason))
    end
end)

addCommandHandler("sweetandkendlskip", function(player)
    if not isElement(player) or (mission and player == mission.leader) then
        broadcastCutsceneSkip()
    end
end)

addCommandHandler("sweetandkendltest", function(player)
    local leader = isElement(player) and player or getElementsByType("player")[1]
    local ok, reason = startMission(leader, true)
    if ok == false then
        outputServerLog("[sweet-and-kendl] HEADLESS NOT STARTED: " .. tostring(reason))
    end
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
    if #players >= 2 and fileExists("natural.request") then
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
    if mission and (not isElement(player) or player == mission.leader) then
        clearMission(true)
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

outputServerLog("[sweet-and-kendl] Ready: /sweetandkendl or headless sweetandkendltest.")
