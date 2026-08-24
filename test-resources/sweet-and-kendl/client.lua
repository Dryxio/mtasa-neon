local state = {active = false, timers = {}, audio = nil, camera = nil, cutscene = nil, text = false, recordings = {}}

local function rememberTimer(timer)
    state.timers[#state.timers + 1] = timer
    return timer
end

local function clearTimers()
    for _, timer in ipairs(state.timers) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    state.timers = {}
end

local function textApi(name, ...)
    local api = _G[name]
    if type(api) ~= "function" then
        return false
    end
    local ok, result = pcall(api, ...)
    return ok and result == true
end

local function ensureText()
    if not state.text then
        state.text = textApi("acquireMissionText", SAK.gxt)
    end
    return state.text
end

local function showText(key, duration)
    return ensureText() and textApi("showMissionText", key, duration or 5000, 1)
end

local function showHelp(key, permanent)
    return ensureText() and textApi("showMissionHelp", key, permanent == true)
end

local function stopSpeaker(speaker)
    if isElement(speaker) and type(stopPedFacialTalk) == "function" then
        pcall(stopPedFacialTalk, speaker)
    end
end

local function clearAudio()
    local audio = state.audio
    if not audio then
        return
    end
    state.audio = nil
    if isTimer(audio.loadTimer) then killTimer(audio.loadTimer) end
    if isTimer(audio.finishTimer) then killTimer(audio.finishTimer) end
    stopSpeaker(audio.speaker)
    if audio.handle and type(releaseMissionAudio) == "function" then
        pcall(releaseMissionAudio, audio.handle)
    end
end

local function resolveSpeaker(name, actors)
    if name == "cj" then
        return state.leader
    end
    if name == "smoke" and actors.smoke then
        return actors.smoke
    end
    if actors.gang and actors.gang[name] then
        return actors.gang[name].ped
    end
end

local function playQueue(lines, actors, finished, index, started)
    index = index or 1
    if not state.active or index > #lines then
        if finished then finished() end
        return
    end
    clearAudio()
    local line = lines[index]
    if type(requestMissionAudio) ~= "function" then
        showText(line[2], 3000)
        return rememberTimer(setTimer(function()
            playQueue(lines, actors, finished, index + 1, started)
        end, 3200, 1))
    end
    local handle = requestMissionAudio(line[1])
    if not handle then
        showText(line[2], 3000)
        return rememberTimer(setTimer(function()
            playQueue(lines, actors, finished, index + 1, started)
        end, 3200, 1))
    end
    local audio = {handle = handle, requestedAt = getTickCount()}
    state.audio = audio
    audio.loadTimer = setTimer(function()
        if state.audio ~= audio then return end
        if isMissionAudioLoaded(handle) then
            killTimer(audio.loadTimer)
            if not playMissionAudio(handle) then
                clearAudio()
                return playQueue(lines, actors, finished, index + 1, started)
            end
            showText(line[2], 5000)
            audio.speaker = resolveSpeaker(line[3], actors)
            if isElement(audio.speaker) and type(setPedFacialTalk) == "function" then
                pcall(setPedFacialTalk, audio.speaker, 6000)
            end
            if started then started(index, line) end
            audio.startedAt = getTickCount()
            audio.finishTimer = setTimer(function()
                if state.audio ~= audio then return end
                if isMissionAudioFinished(handle) or getTickCount() - audio.startedAt > 30000 then
                    clearAudio()
                    playQueue(lines, actors, finished, index + 1, started)
                end
            end, 50, 0)
        elseif getTickCount() - audio.requestedAt > 10000 then
            clearAudio()
            playQueue(lines, actors, finished, index + 1, started)
        end
    end, 50, 0)
end

local function releaseCamera()
    if state.camera then
        pcall(setScriptCameraWidescreen, state.camera, false)
        pcall(setScriptCameraNearClip, state.camera, false)
        pcall(releaseScriptCamera, state.camera, false)
        state.camera = nil
    end
end

local function fixedCamera(position, target, fadeIn)
    releaseCamera()
    if type(acquireScriptCamera) ~= "function" then
        return false
    end
    local token = acquireScriptCamera(true)
    if not token then
        return false
    end
    state.camera = token
    setScriptCameraWidescreen(token, true)
    setScriptCameraNearClip(token, 0.2)
    setScriptCameraFixed(token, Vector3(position[1], position[2], position[3]),
                         Vector3(target[1], target[2], target[3]), Vector3(0, 0, 0), true)
    if fadeIn then
        fadeScriptCamera(token, true, fadeIn)
    end
    return true
end

local function moveCamera(config)
    if not state.camera or not config.moveTo then
        return
    end
    if type(moveScriptCamera) == "function" then
        pcall(moveScriptCamera, state.camera, Vector3(config.position[1], config.position[2], config.position[3]),
              Vector3(config.moveTo[1], config.moveTo[2], config.moveTo[3]), config.duration, true)
    end
    if type(trackScriptCamera) == "function" then
        pcall(trackScriptCamera, state.camera, Vector3(config.target[1], config.target[2], config.target[3]),
              Vector3(config.trackTo[1], config.trackTo[2], config.trackTo[3]), config.duration, true)
    end
end

local function reportBarrier(id, ok, reason)
    triggerServerEvent("sak:barrierDone", resourceRoot, id, ok == true, reason)
end

local function stopRecording(id, vehicle)
    local recording = state.recordings[id]
    if isElement(vehicle) and type(isVehiclePlaybackActive) == "function" and isVehiclePlaybackActive(vehicle) then
        pcall(stopVehiclePlayback, vehicle)
    end
    if recording and isTimer(recording.timer) then
        killTimer(recording.timer)
    end
    state.recordings[id] = nil
end

local function startRecording(id, vehicle, speed, delay)
    if not isElement(vehicle) or not isElementStreamedIn(vehicle) or not isElementSyncer(vehicle) or
        type(requestVehicleRecording) ~= "function" then
        return false
    end
    stopRecording(id, vehicle)
    requestVehicleRecording(id)
    local recording = {requestedAt = getTickCount(), vehicle = vehicle}
    state.recordings[id] = recording
    recording.timer = setTimer(function()
        if state.recordings[id] ~= recording or not isElement(vehicle) then return end
        if isVehicleRecordingLoaded(id) then
            if getTickCount() - recording.requestedAt < (delay or 0) then return end
            killTimer(recording.timer)
            if startVehiclePlayback(vehicle, id) and speed and type(setVehiclePlaybackSpeed) == "function" then
                setVehiclePlaybackSpeed(vehicle, speed)
            end
        elseif getTickCount() - recording.requestedAt > 15000 then
            killTimer(recording.timer)
            state.recordings[id] = nil
        end
    end, 50, 0)
    return true
end

local function runSmokeScene(id, actors)
    local camera = SAK.cameras.smoke
    if not fixedCamera(camera.position, camera.target, 1.5) then
        return reportBarrier(id, false, "camera Smoke refusee")
    end
    playQueue(SAK.audio.smokeIntro, actors, function()
        releaseCamera()
        reportBarrier(id, true)
    end, nil, function(index)
        if index == 2 then
            moveCamera(camera)
        end
        if index == 3 and localPlayer == state.leader and isElement(actors.peren) and isPedInVehicle(localPlayer) and
            type(setPedDriveTo) == "function" then
            pcall(setPedDriveTo, localPlayer, actors.peren, Vector3(2414.68, -1656.38, 12.38), 10.0, "normal", "avoid_cars")
        end
    end)
end

local function runFuneralScene(id, actors)
    local camera = SAK.cameras.funeral
    if not fixedCamera(camera.position, camera.target, 0.5) then
        return reportBarrier(id, false, "camera funerailles refusee")
    end
    if actors.ballas then
        local b = actors.ballas
        if isElementSyncer(b.driver) then
            pcall(setPedPhysicalProofs, b.driver, true, true, true, true, true)
            pcall(setPedPhysicalProofs, b.passenger, true, true, true, true, true)
            startRecording(201, b.vehicle, 1.0, 3300)
        end
    end
    rememberTimer(setTimer(function()
        if state.active then fixedCamera(camera.attackPosition, camera.attackTarget, false) end
    end, 4700, 1))
    playQueue(SAK.audio.funeral, actors, function()
        if actors.ballas and isElementSyncer(actors.ballas.driver) then
            pcall(setPedPhysicalProofs, actors.ballas.driver, false, false, false, false, false)
            pcall(setPedPhysicalProofs, actors.ballas.passenger, false, false, false, false, false)
        end
        fixedCamera(camera.sweetPosition, camera.sweetTarget, false)
        rememberTimer(setTimer(function()
            releaseCamera()
            reportBarrier(id, true)
        end, 1000, 1))
    end)
end

local function runSplitScene(id, actors)
    local camera = SAK.cameras.split
    if not fixedCamera(camera.position, camera.target, 0.5) then
        return reportBarrier(id, false, "camera split refusee")
    end
    playQueue(SAK.audio.split, actors, function()
        releaseCamera()
        reportBarrier(id, true)
    end, nil, function(index)
        if index == 2 then
            fixedCamera(camera.ryderPosition, camera.ryderTarget, false)
        end
    end)
end

local function runFinaleScene(id, actors)
    local cameras = SAK.cameras.finale
    if not fixedCamera(cameras[1].position, cameras[1].target, 1.0) then
        return reportBarrier(id, false, "camera finale refusee")
    end
    if actors.gang and isElementSyncer(actors.gang.ryder.bike) then
        startRecording(206, actors.gang.ryder.bike, 1.0, 0)
        startRecording(205, actors.gang.sweet.bike, 0.8, 1500)
    end
    playQueue(SAK.audio.finale, actors, function()
        releaseCamera()
        reportBarrier(id, true)
    end, nil, function(index)
        local cameraIndex = index <= 2 and 1 or index <= 4 and 2 or index <= 6 and 3 or index == 7 and 4 or
            index <= 9 and 5 or 6
        local config = cameras[cameraIndex]
        fixedCamera(config.position, config.target, false)
    end)
end

local function runSaveTutorial(id)
    if not fixedCamera({2499.3057, -1678.9210, 13.0313}, {2499.0439, -1679.8518, 13.2865}, false) then
        return reportBarrier(id, false, "camera tutoriel sauvegarde refusee")
    end
    showHelp("SAVE_G", false)
    rememberTimer(setTimer(function()
        if not state.active then return end
        fixedCamera({2504.2800, -1682.8573, 15.1427}, {2504.3059, -1683.8514, 15.0385}, false)
        showHelp("SAVE_G2", false)
    end, 6000, 1))
    rememberTimer(setTimer(function()
        if not state.active then return end
        fixedCamera({2494.8633, -1683.0514, 13.0207}, {2494.9395, -1684.0370, 13.1717}, false)
        showHelp("SAVE_G3", false)
    end, 12000, 1))
    rememberTimer(setTimer(function()
        releaseCamera()
        reportBarrier(id, true)
    end, 18000, 1))
end

local function cleanup()
    clearTimers()
    clearAudio()
    releaseCamera()
    if state.cutscene then
        pcall(releaseFileCutscene, state.cutscene.token)
        state.cutscene = nil
    end
    for id, recording in pairs(state.recordings) do
        stopRecording(id, recording.vehicle)
    end
    if state.text then
        textApi("clearMissionTexts")
    end
    state = {active = false, timers = {}, audio = nil, camera = nil, cutscene = nil, text = false, recordings = {}}
    setCameraTarget(localPlayer)
end

addEvent("sak:start", true)
addEventHandler("sak:start", resourceRoot, function(id, leader, headless)
    cleanup()
    state.active, state.id, state.leader, state.headless = true, id, leader, headless == true
    ensureText()
end)

addEvent("sak:fileCutscene", true)
addEventHandler("sak:fileCutscene", resourceRoot, function(id, name, leader)
    if not state.active or state.headless then return reportBarrier(id, true) end
    if type(requestFileCutscene) ~= "function" then return reportBarrier(id, false, "API cutscene absente") end
    local token = requestFileCutscene(name)
    if not token then return reportBarrier(id, false, "requestFileCutscene refuse") end
    local scene = {id = id, token = token, requestedAt = getTickCount(), started = false, leader = leader}
    state.cutscene = scene
    scene.timer = setTimer(function()
        if state.cutscene ~= scene then return end
        if not scene.started then
            if isFileCutsceneLoaded(token) then
                scene.started = startFileCutscene(token) == true
                if scene.started then fadeFileCutscene(token, true, 1000, 0, 0, 0) end
            elseif getTickCount() - scene.requestedAt > 60000 then
                killTimer(scene.timer)
                releaseFileCutscene(token)
                state.cutscene = nil
                reportBarrier(id, false, "chargement cutscene timeout")
            end
        else
            if localPlayer == leader and isFileCutsceneSkipInputPressed(token) then
                triggerServerEvent("sak:skipRequest", resourceRoot, id)
            end
            if isFileCutsceneFinished(token) or getTickCount() - scene.requestedAt > 140000 then
                killTimer(scene.timer)
                releaseFileCutscene(token)
                state.cutscene = nil
                reportBarrier(id, true)
            end
        end
    end, 50, 0)
end)

addEvent("sak:skipCutscene", true)
addEventHandler("sak:skipCutscene", resourceRoot, function(id)
    if state.cutscene and state.cutscene.id == id then
        pcall(skipFileCutscene, state.cutscene.token)
    end
end)

addEvent("sak:scene", true)
addEventHandler("sak:scene", resourceRoot, function(id, name, leader, actors)
    if not state.active or state.headless then return reportBarrier(id, true) end
    state.leader = leader
    actors = type(actors) == "table" and actors or {}
    if name == "smoke" then return runSmokeScene(id, actors) end
    if name == "funeral" then return runFuneralScene(id, actors) end
    if name == "split" then return runSplitScene(id, actors) end
    if name == "finale" then return runFinaleScene(id, actors) end
    if name == "save_tutorial" then return runSaveTutorial(id) end
    reportBarrier(id, false, "scene inconnue")
end)

addEvent("sak:stage", true)
addEventHandler("sak:stage", resourceRoot, function(stage, extra)
    if not state.active then return end
    state.stage = stage
    if stage == "ride1" then
        showText("INTRO2E", 10000)
        setCameraTarget(localPlayer)
    elseif stage == "ride2" then
        showText("INTRO2K", 3000)
        setCameraTarget(localPlayer)
    elseif stage == "failed" then
        if type(extra) == "table" and extra.textKey then showText(extra.textKey, 5000) end
        textApi("showMissionBigText", "M_FAIL", 5000, 1)
    end
end)

addEvent("sak:ambientAudio", true)
addEventHandler("sak:ambientAudio", resourceRoot, function(name, actors)
    if not state.active or state.headless or type(SAK.audio[name]) ~= "table" then return end
    playQueue(SAK.audio[name], actors or {}, nil)
end)

addEvent("sak:stopRecording", true)
addEventHandler("sak:stopRecording", resourceRoot, function(id, vehicle)
    stopRecording(id, vehicle)
end)

addEvent("sak:passed", true)
addEventHandler("sak:passed", resourceRoot, function(respect)
    textApi("showMissionBigText", "M_PASSR", 5000, 1, tonumber(respect) or 3)
    if type(playMissionPassedTune) == "function" then pcall(playMissionPassedTune, 1) end
end)

addEvent("sak:cleanup", true)
addEventHandler("sak:cleanup", resourceRoot, cleanup)
addEventHandler("onClientResourceStop", resourceRoot, cleanup)
