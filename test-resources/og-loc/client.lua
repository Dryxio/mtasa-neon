local state = {active = false, timers = {}, text = false, cutscene = nil, camera = nil, marker = nil, blip = nil,
               audio = nil, audioGeneration = 0}

local function rememberTimer(timer)
    state.timers[#state.timers + 1] = timer
    return timer
end

local function textApi(name, ...)
    local api = _G[name]
    if type(api) ~= "function" then return false end
    local ok, result = pcall(api, ...)
    return ok and result == true
end

local function ensureText()
    if not state.text then state.text = textApi("acquireMissionText", OGL.gxt) end
    return state.text
end

local function showText(key, duration)
    return key and ensureText() and textApi("showMissionText", key, duration or 7000, 1)
end

local function releaseAudioSlot()
    local audio = state.audio
    state.audio = nil
    if not audio then return end
    if isTimer(audio.loadTimer) then killTimer(audio.loadTimer) end
    if isTimer(audio.finishTimer) then killTimer(audio.finishTimer) end
    if audio.handle and type(releaseMissionAudio) == "function" then pcall(releaseMissionAudio, audio.handle) end
end

local function clearAudio()
    state.audioGeneration = state.audioGeneration + 1
    releaseAudioSlot()
end

local function playQueue(lines, finished)
    clearAudio()
    local generation = state.audioGeneration
    local function play(index)
        if not state.active or generation ~= state.audioGeneration then return end
        releaseAudioSlot()
        local line = lines[index]
        if not line then if finished then finished() end return end
        if type(requestMissionAudio) ~= "function" then
            showText(line[2], 4500)
            return rememberTimer(setTimer(function() play(index + 1) end, 4700, 1))
        end
        local handle = requestMissionAudio(line[1])
        if not handle then
            showText(line[2], 4500)
            return rememberTimer(setTimer(function() play(index + 1) end, 4700, 1))
        end
        local audio = {handle = handle, requestedAt = getTickCount()}
        state.audio = audio
        audio.loadTimer = setTimer(function()
            if state.audio ~= audio or generation ~= state.audioGeneration then return end
            if isMissionAudioLoaded(handle) then
                killTimer(audio.loadTimer)
                if not playMissionAudio(handle) then releaseAudioSlot() return play(index + 1) end
                showText(line[2], 4500)
                audio.startedAt = getTickCount()
                audio.finishTimer = setTimer(function()
                    if state.audio ~= audio or generation ~= state.audioGeneration then return end
                    if isMissionAudioFinished(handle) or getTickCount() - audio.startedAt > 30000 then
                        releaseAudioSlot()
                        play(index + 1)
                    end
                end, 50, 0)
            elseif getTickCount() - audio.requestedAt > 10000 then
                releaseAudioSlot()
                play(index + 1)
            end
        end, 50, 0)
    end
    play(1)
end

local function releaseCamera()
    if state.camera then
        pcall(setScriptCameraWidescreen, state.camera, false)
        pcall(releaseScriptCamera, state.camera, false)
        state.camera = nil
    end
    setCameraTarget(localPlayer)
end

local function fixedCamera(position, target)
    releaseCamera()
    if type(acquireScriptCamera) ~= "function" then return false end
    local token = acquireScriptCamera(true)
    if not token then return false end
    state.camera = token
    setScriptCameraWidescreen(token, true)
    setScriptCameraFixed(token, Vector3(position[1], position[2], position[3]),
                         Vector3(target[1], target[2], target[3]), Vector3(0, 0, 0), true)
    fadeScriptCamera(token, true, 750)
    return true
end

local function clearObjective()
    if isElement(state.marker) then destroyElement(state.marker) end
    if isElement(state.blip) then destroyElement(state.blip) end
    state.marker, state.blip = nil, nil
end

local function setObjective(objective)
    clearObjective()
    if type(objective) ~= "table" then return end
    state.marker = createMarker(objective[1], objective[2], objective[3] - 1, "cylinder", 3, 255, 220, 80, 110)
    state.blip = createBlip(objective[1], objective[2], objective[3], 0, 2, 255, 220, 80, 255)
    if isElement(state.marker) then setElementDimension(state.marker, OGL.dimension) end
    if isElement(state.blip) then setElementDimension(state.blip, OGL.dimension) end
end

local function reportBarrier(id, ok, reason)
    triggerServerEvent("ogl:barrierDone", resourceRoot, id, ok == true, reason)
end

local function startVehicleProbe(vehicle, scriptZ, actors, probeStage)
    if localPlayer ~= state.leader or not isElement(vehicle) then return end
    scriptZ = tonumber(scriptZ)
    local sample = 0
    local timer
    local function report()
        if not state.active or state.stage ~= probeStage or not isElement(vehicle) then
            if isTimer(timer) then killTimer(timer) end
            return
        end
        sample = sample + 1
        local x, y, z = getElementPosition(vehicle)
        local vx, vy, vz = getElementVelocity(vehicle)
        local streamed = isElementStreamedIn(vehicle)
        local baseOffset = streamed and tonumber(getElementDistanceFromCentreOfMassToBaseOfModel(vehicle)) or nil
        local groundZ = streamed and getGroundPosition(x, y, z + 50) or 0
        local groundValid = baseOffset and groundZ ~= 0
        triggerServerEvent("ogl:vehicleProbe", resourceRoot, vehicle, {
            sample = sample,
            x = x, y = y, z = z,
            groundZ = groundValid and groundZ or nil,
            baseOffset = baseOffset,
            bottomClearance = groundValid and (z - baseOffset - groundZ) or nil,
            scriptPlacementError = baseOffset and scriptZ and (z - baseOffset - scriptZ) or nil,
            health = getElementHealth(vehicle), playerHealth = getElementHealth(localPlayer),
            vx = vx, vy = vy, vz = vz,
            streamed = streamed, syncing = isElementSyncer(vehicle),
            onGround = isVehicleOnGround(vehicle), blown = isVehicleBlown(vehicle),
            collisions = getElementCollisionsEnabled(vehicle), frozen = isElementFrozen(vehicle),
            playerCollisions = getElementCollisionsEnabled(localPlayer), playerFrozen = isElementFrozen(localPlayer),
            playerSeat = getPedOccupiedVehicle(localPlayer) == vehicle and getPedOccupiedVehicleSeat(localPlayer) or -1,
            smokeSeat = isElement(actors and actors.smoke) and getPedOccupiedVehicle(actors.smoke) == vehicle and
                getPedOccupiedVehicleSeat(actors.smoke) or -1,
            smokeCollisions = isElement(actors and actors.smoke) and getElementCollisionsEnabled(actors.smoke) or nil,
            sweetSeat = isElement(actors and actors.sweet) and getPedOccupiedVehicle(actors.sweet) == vehicle and
                getPedOccupiedVehicleSeat(actors.sweet) or -1,
            sweetCollisions = isElement(actors and actors.sweet) and getElementCollisionsEnabled(actors.sweet) or nil,
        })
    end
    -- The first sample runs in the stage event itself so headless mode cannot
    -- teleport past an invalid physical spawn before the harness observes it.
    report()
    timer = rememberTimer(setTimer(report, 250, 39))
end

local function clearTimers()
    for _, timer in ipairs(state.timers) do if isTimer(timer) then killTimer(timer) end end
    state.timers = {}
end

local function cleanup()
    clearTimers()
    clearAudio()
    clearObjective()
    releaseCamera()
    if state.cutscene then
        if isTimer(state.cutscene.timer) then killTimer(state.cutscene.timer) end
        pcall(releaseFileCutscene, state.cutscene.token)
    end
    if state.text then textApi("clearMissionTexts") end
    state = {active = false, timers = {}, text = false, cutscene = nil, camera = nil, marker = nil, blip = nil,
             audio = nil, audioGeneration = 0}
end

addEvent("ogl:start", true)
addEventHandler("ogl:start", resourceRoot, function(id, leader, headless)
    cleanup()
    state.active, state.id, state.leader, state.headless = true, id, leader, headless == true
    ensureText()
end)

addEvent("ogl:fileCutscene", true)
addEventHandler("ogl:fileCutscene", resourceRoot, function(id, name, leader)
    if not state.active or state.headless then return reportBarrier(id, true) end
    -- The SCM clears both mission-audio slots before loading a file cutscene.
    -- Leaving the drive dialogue lease alive while SMOKE1B acquires native
    -- cutscene audio can stall the managed cutscene lifecycle.
    clearAudio()
    if type(requestFileCutscene) ~= "function" then return reportBarrier(id, false, "API cutscene absente") end
    triggerServerEvent("ogl:cutsceneProbe", resourceRoot, id, name, "handler")
    local token = requestFileCutscene(name)
    if not token then return reportBarrier(id, false, "requestFileCutscene refuse") end
    triggerServerEvent("ogl:cutsceneProbe", resourceRoot, id, name, "requested")
    local scene = {id = id, token = token, requestedAt = getTickCount(), leader = leader}
    state.cutscene = scene
    scene.timer = setTimer(function()
        if state.cutscene ~= scene then return end
        if not scene.started then
            if isFileCutsceneLoaded(token) then
                triggerServerEvent("ogl:cutsceneProbe", resourceRoot, id, name, "loaded")
                scene.started = startFileCutscene(token) == true
                if scene.started then
                    triggerServerEvent("ogl:cutsceneProbe", resourceRoot, id, name, "started")
                    fadeFileCutscene(token, true, 1000, 0, 0, 0)
                end
            elseif getTickCount() - scene.requestedAt > 60000 then
                killTimer(scene.timer)
                releaseFileCutscene(token)
                state.cutscene = nil
                reportBarrier(id, false, "chargement cutscene timeout")
            end
        else
            if localPlayer == leader and isFileCutsceneSkipInputPressed(token) then
                triggerServerEvent("ogl:skipRequest", resourceRoot, id)
            end
            if isFileCutsceneFinished(token) then
                triggerServerEvent("ogl:cutsceneProbe", resourceRoot, id, name, "finished")
                killTimer(scene.timer)
                -- Releasing without a preserved fade is the native lease's
                -- safety path: it deletes the DAT and restores gameplay with
                -- an immediate fade-in. Preserving the explicit black frame
                -- here stranded skipped cutscenes before the barrier ACK.
                local released, result = pcall(releaseFileCutscene, token, false)
                state.cutscene = nil
                triggerServerEvent("ogl:cutsceneProbe", resourceRoot, id, name,
                                   released and result == true and "released" or "release_failed")
                reportBarrier(id, released and result == true, "releaseFileCutscene refuse")
            elseif getTickCount() - scene.requestedAt > 140000 then
                killTimer(scene.timer)
                pcall(releaseFileCutscene, token, false)
                state.cutscene = nil
                reportBarrier(id, false, "lecture cutscene timeout")
            end
        end
    end, 50, 0)
end)

addEvent("ogl:skipCutscene", true)
addEventHandler("ogl:skipCutscene", resourceRoot, function(id)
    if state.cutscene and state.cutscene.id == id then pcall(skipFileCutscene, state.cutscene.token) end
end)

local scenes = {
    freddys_house_arrival = {
        camera = {2451.2, -1295.1, 24.8}, target = {2459.7, -1284.6, 26.5}, text = "SMK1_03", duration = 5500,
    },
    doorbell = {
        camera = {2461.7, -1273.7, 30.0}, target = {2470.1, -1277.1, 30.7}, text = "SMK1_04", duration = 5000,
    },
    freddy_dead = {
        camera = {2299.3, -1490.6, 22.8}, target = {2290.4, -1490.2, 22.3}, text = "SMK1_13", duration = 6500,
    },
    burger_shot = {
        camera = {790.2, -1627.0, 16.0}, target = {783.2, -1630.3, 12.2}, duration = 6500,
    },
}

local sceneAudio = {
    freddys_house_arrival = "houseArrival",
    doorbell = "doorbell",
    freddy_dead = "freddyDead",
    burger_shot = "burgerShot",
}

local function setSceneAnimation(ped, block, animation, duration, looped)
    if not isElement(ped) then return end
    setPedAnimation(ped, block, animation, duration or -1, looped == true, false, true, false, 250)
end

local function stageSceneActors(name, actors)
    actors = type(actors) == "table" and actors or {}
    if name == "freddys_house_arrival" then
        setSceneAnimation(actors.ogloc, "CAR_CHAT", "CAR_Sc1_BR")
        rememberTimer(setTimer(function()
            setSceneAnimation(localPlayer, "CAR_CHAT", "CAR_Sc1_FL")
            setSceneAnimation(actors.sweet, "CAR_CHAT", "CAR_Sc1_BL")
        end, 2500, 1))
        rememberTimer(setTimer(function()
            setSceneAnimation(actors.smoke, "CAR_CHAT", "CAR_Sc1_FR")
        end, 9000, 1))
    elseif name == "doorbell" then
        setSceneAnimation(localPlayer, "CRIB", "CRIB_Use_Switch", 800, false)
        rememberTimer(setTimer(function()
            setSceneAnimation(actors.ogloc, "GANGS", "prtial_gngtlkE", 2500, false)
        end, 1200, 1))
        rememberTimer(setTimer(function()
            setSceneAnimation(actors.ogloc, "MISC", "bng_wndw", 3500, false)
            setSceneAnimation(localPlayer, "RAPPING", "Laugh_01", 3500, false)
        end, 9000, 1))
    end
end

local function clearSceneActors(actors)
    setPedAnimation(localPlayer, false)
    for _, ped in pairs(type(actors) == "table" and actors or {}) do
        if isElement(ped) then setPedAnimation(ped, false) end
    end
end

addEvent("ogl:scene", true)
addEventHandler("ogl:scene", resourceRoot, function(id, name, leader, payload)
    if not state.active or state.headless then return reportBarrier(id, true) end
    local scene = scenes[name]
    if not scene then return reportBarrier(id, false, "scene inconnue: " .. tostring(name)) end
    state.leader = leader
    payload = type(payload) == "table" and payload or {}
    local actors = payload.actors
    clearObjective()
    if not fixedCamera(scene.camera, scene.target) then return reportBarrier(id, false, "camera refusee") end
    stageSceneActors(name, actors)
    local function finishScene()
        if not state.active then return end
        clearSceneActors(actors)
        releaseCamera()
        reportBarrier(id, true)
    end
    local audio = OGL.audio[sceneAudio[name]]
    if audio then
        playQueue(audio, finishScene)
    else
        if scene.text then showText(scene.text, scene.duration) end
        rememberTimer(setTimer(finishScene, scene.duration, 1))
    end
end)

addEvent("ogl:stage", true)
addEventHandler("ogl:stage", resourceRoot, function(stage, payload)
    if not state.active then return end
    state.stage = stage
    payload = type(payload) == "table" and payload or {}
    if isElement(payload.probeVehicle) then
        startVehicleProbe(payload.probeVehicle, payload.probeScriptZ, payload.probeActors, stage)
    end
    if state.headless then return end
    if payload.textKey then showText(payload.textKey, 7000) end
    if payload.objective then setObjective(payload.objective) else clearObjective() end
    if stage:find("^chase_recording:") or stage == "basketball_combat" then
        setCameraTarget(localPlayer)
    end
    if stage == "drive_police" or stage == "drive_house" then
        local expected = stage
        local lines = stage == "drive_police" and OGL.audio.drivePolice or OGL.audio.driveHouse
        rememberTimer(setTimer(function()
            if state.active and state.stage == expected then playQueue(lines) end
        end, 7000, 1))
    elseif stage:find("^chase_recording:") then
        local recordingId = tonumber(payload.recordingId)
        if recordingId == 30 then
            playQueue(OGL.audio.bikeIntro)
        elseif recordingId then
            local index = ((recordingId - 31) % #OGL.audio.taunts) + 1
            playQueue({OGL.audio.taunts[index]})
        end
    elseif stage == "basketball_combat" then
        playQueue(OGL.audio.combat)
    elseif stage == "return_burger_shot" then
        playQueue(OGL.audio.returnRide)
    elseif stage == "failed" then
        clearAudio()
        fadeCamera(true, 0.5)
        if payload.textKey then showText(payload.textKey, 5000) end
        textApi("showMissionBigText", "M_FAIL", 5000, 1)
    end
end)

addEvent("ogl:travelReleased", true)
addEventHandler("ogl:travelReleased", resourceRoot, function()
    if not state.active or state.headless then return end
    setCameraTarget(localPlayer)
    fadeCamera(true, 1.0)
end)

addEvent("ogl:passed", true)
addEventHandler("ogl:passed", resourceRoot, function(respect)
    clearObjective()
    textApi("showMissionBigText", "M_PASSR", 5000, 1, tonumber(respect) or 5)
    if type(playMissionPassedTune) == "function" then pcall(playMissionPassedTune, 1) end
end)

addEvent("ogl:cleanup", true)
addEventHandler("ogl:cleanup", resourceRoot, cleanup)
addEventHandler("onClientResourceStop", resourceRoot, cleanup)
