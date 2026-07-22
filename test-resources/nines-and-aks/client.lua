local state = {
    active = false,
    stage = nil,
    entities = {},
    timers = {},
    cameraToken = nil,
    cutscene = nil,
    audio = nil,
    missionText = false,
    navigation = {},
    bottles = {},
    round = 0,
    roundHits = 0,
    rangeOutside = false,
    phoneFinished = false,
}

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
        outputDebugString(("[nines-and-aks] Missing mission text API: %s"):format(name), 1)
        return false
    end
    local ok, result = pcall(api, ...)
    return ok and result == true
end

local function ensureText()
    if not state.missionText then
        state.missionText = textApi("acquireMissionText", NINES.gxt)
    end
    return state.missionText
end

local function showText(key, duration)
    return ensureText() and textApi("showMissionText", key, duration or 5000, 1)
end

local function showHelp(key, permanent)
    return ensureText() and textApi("showMissionHelp", key, permanent == true)
end

local function clearText()
    if state.missionText then
        textApi("clearMissionTexts")
    end
end

local function setControls(enabled)
    toggleAllControls(enabled == true, true, true)
end

local function stopSpeaker(speaker)
    if not isElement(speaker) then
        return
    end
    if type(stopPedFacialTalk) == "function" then
        pcall(stopPedFacialTalk, speaker)
    end
    if type(setPedScriptedSpeechMuted) == "function" then
        pcall(setPedScriptedSpeechMuted, speaker, false)
    end
end

local function resolveSpeaker(name)
    if name == "cj" then
        return localPlayer
    end
    return state.entities[name]
end

local function clearAudio(reason)
    local audio = state.audio
    if not audio then
        return
    end
    state.audio = nil
    for _, timer in ipairs({audio.loadTimer, audio.finishTimer}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    stopSpeaker(audio.speaker)
    if audio.handle and type(releaseMissionAudio) == "function" then
        pcall(releaseMissionAudio, audio.handle)
    end
    outputDebugString(("[nines-and-aks] Audio cleared: %s"):format(tostring(reason)))
end

local function playQueue(lines, finished, index, started)
    index = index or 1
    if not state.active or index > #lines then
        if finished then
            finished()
        end
        return
    end
    clearAudio("next_line")
    local line = lines[index]
    if type(requestMissionAudio) ~= "function" or type(isMissionAudioLoaded) ~= "function" or
        type(playMissionAudio) ~= "function" or type(isMissionAudioFinished) ~= "function" or
        type(releaseMissionAudio) ~= "function" then
        if line[2] then
            showText(line[2], 3000)
        end
        return rememberTimer(setTimer(function()
            playQueue(lines, finished, index + 1, started)
        end, 3200, 1))
    end
    local ok, handle = pcall(requestMissionAudio, line[1])
    if not ok or not handle then
        outputDebugString(("[nines-and-aks] Mission audio %d refused"):format(line[1]), 1)
        if line[2] then
            showText(line[2], 3000)
        end
        return rememberTimer(setTimer(function()
            playQueue(lines, finished, index + 1, started)
        end, 3200, 1))
    end
    local audio = {handle = handle, line = line, requestedAt = getTickCount()}
    state.audio = audio
    audio.loadTimer = setTimer(function()
        if state.audio ~= audio then
            return
        end
        local queried, loaded = pcall(isMissionAudioLoaded, handle)
        if queried and loaded then
            killTimer(audio.loadTimer)
            if not playMissionAudio(handle) then
                clearAudio("play_refused")
                return playQueue(lines, finished, index + 1, started)
            end
            if line[2] then
                showText(line[2], 4000)
            end
            audio.speaker = resolveSpeaker(line[3])
            if isElement(audio.speaker) then
                if type(setPedScriptedSpeechMuted) == "function" then
                    pcall(setPedScriptedSpeechMuted, audio.speaker, true)
                end
                if type(setPedFacialTalk) == "function" then
                    pcall(setPedFacialTalk, audio.speaker, 3000)
                end
            end
            audio.startedAt = getTickCount()
            if started then
                started(line, index)
            end
            audio.finishTimer = setTimer(function()
                if state.audio ~= audio then
                    return
                end
                local queryOk, done = pcall(isMissionAudioFinished, handle)
                if queryOk and done or getTickCount() - audio.startedAt > 30000 then
                    killTimer(audio.finishTimer)
                    clearAudio("finished")
                    playQueue(lines, finished, index + 1, started)
                end
            end, 50, 0)
        elseif getTickCount() - audio.requestedAt > 10000 then
            killTimer(audio.loadTimer)
            clearAudio("load_timeout")
            playQueue(lines, finished, index + 1, started)
        end
    end, 50, 0)
end

local function acquireCamera(position, target, fadeIn)
    if state.cameraToken then
        pcall(releaseScriptCamera, state.cameraToken, true)
        state.cameraToken = nil
    end
    if type(acquireScriptCamera) ~= "function" or type(setScriptCameraFixed) ~= "function" then
        return false
    end
    local token = acquireScriptCamera(true)
    if not token then
        return false
    end
    state.cameraToken = token
    setScriptCameraWidescreen(token, true)
    setScriptCameraNearClip(token, 0.2)
    if not setScriptCameraFixed(token, Vector3(position[1], position[2], position[3]),
                                Vector3(target[1], target[2], target[3]), Vector3(0, 0, 0), true) then
        releaseScriptCamera(token)
        state.cameraToken = nil
        return false
    end
    if fadeIn then
        fadeScriptCamera(token, true, fadeIn)
    end
    return true
end

local function releaseCamera(preserveFade)
    if not state.cameraToken then
        return
    end
    local token = state.cameraToken
    state.cameraToken = nil
    if type(setScriptCameraWidescreen) == "function" then
        pcall(setScriptCameraWidescreen, token, false)
    end
    if type(setScriptCameraNearClip) == "function" then
        pcall(setScriptCameraNearClip, token, false)
    end
    pcall(releaseScriptCamera, token, preserveFade == true)
end

local function destroyNavigation()
    for _, element in pairs(state.navigation) do
        if isElement(element) then
            destroyElement(element)
        end
    end
    state.navigation = {}
end

local function showDestination(destination)
    destroyNavigation()
    local profile = NINES.destinations[destination]
    state.navigation.blip = createBlip(profile[1], profile[2], profile[3], destination == "binco" and 45 or 0, 2, 226, 192, 99, 255)
    if type(renderScriptImportantArea) ~= "function" then
        state.navigation.marker = createMarker(profile[1], profile[2], profile[3] - 1.0, "cylinder", 3.5, 255, 0, 0, 180)
    end
    setElementDimension(state.navigation.blip, NINES.dimension)
    if isElement(state.navigation.marker) then
        setElementDimension(state.navigation.marker, NINES.dimension)
    end
    state.navigation.destination = destination
    state.navigation.mode = "destination"
end

local function showVehicleNavigation(vehicle)
    destroyNavigation()
    if isElement(vehicle) then
        state.navigation.blip = createBlipAttachedTo(vehicle, 0, 2, 0, 0, 255, 255)
        setElementDimension(state.navigation.blip, NINES.dimension)
        state.navigation.mode = "vehicle"
    end
end

local function destroyBottles()
    for object in pairs(state.bottles) do
        if isElement(object) then
            destroyElement(object)
        end
    end
    state.bottles = {}
end

local function createBottle(index, playerTarget)
    local layout = NINES.bottleRounds[state.round]
    local p = layout and layout[playerTarget and "player" or "demo"][index]
    if not p then
        return nil
    end
    local object = createObject(NINES.bottleModel, p[1], p[2], p[3], 0, 0, 90)
    if not object then
        return nil
    end
    setElementDimension(object, NINES.dimension)
    setObjectBreakable(object, true)
    setElementData(object, "nines.bottle", playerTarget and index or false, false)
    state.bottles[object] = {index = index, playerTarget = playerTarget == true}
    return object
end

local function applyActorPolicies()
    for _, name in ipairs({"smoke", "emmet"}) do
        local ped = state.entities[name]
        if isElement(ped) and isElementStreamedIn(ped) and isElementSyncer(ped) then
            if type(setPedMissionActor) == "function" then
                pcall(setPedMissionActor, ped, true)
            end
            if type(setPedSuffersCriticalHits) == "function" then
                pcall(setPedSuffersCriticalHits, ped, false)
            end
            if type(setPedNeverTargeted) == "function" then
                pcall(setPedNeverTargeted, ped, true)
            end
        end
    end
    local smoke = state.entities.smoke
    if isElement(smoke) and type(setPedWeaponAccuracy) == "function" then
        pcall(setPedWeaponAccuracy, smoke, 100)
        pcall(setPedWeaponShootingRate, smoke, 30)
    end
end

local function finishCutscene(scene, result)
    if state.cutscene ~= scene then
        return
    end
    if scene.token and type(releaseFileCutscene) == "function" then
        local released, releaseResult = pcall(releaseFileCutscene, scene.token, true)
        if not released or releaseResult ~= true then
            result = "release_failed"
        end
    end
    state.cutscene = nil
    triggerServerEvent("nines:cutsceneFinished", resourceRoot, scene.kind, result)
end

local function beginFileCutscene(kind, name)
    if not ensureText() then
        return triggerServerEvent("nines:cutsceneFinished", resourceRoot, kind, "mission_text_unavailable")
    end
    if type(requestFileCutscene) ~= "function" or type(isFileCutsceneLoaded) ~= "function" or
        type(startFileCutscene) ~= "function" or type(isFileCutsceneFinished) ~= "function" then
        return triggerServerEvent("nines:cutsceneFinished", resourceRoot, kind, "api_unavailable")
    end
    local token = requestFileCutscene(name, NINES.cutsceneVisibleAreas[kind])
    if not token then
        return triggerServerEvent("nines:cutsceneFinished", resourceRoot, kind, "request_refused")
    end
    local scene = {kind = kind, name = name, token = token, requestedAt = getTickCount()}
    state.cutscene = scene
    scene.loadTimer = rememberTimer(setTimer(function()
        if state.cutscene ~= scene then
            return
        end
        if isFileCutsceneLoaded(token) then
            killTimer(scene.loadTimer)
            if not startFileCutscene(token) then
                return finishCutscene(scene, "start_refused")
            end
            fadeFileCutscene(token, true, 1.0, 0, 0, 0)
            scene.startedAt = getTickCount()
            scene.finishTimer = rememberTimer(setTimer(function()
                if state.cutscene ~= scene then
                    return
                end
                if type(isFileCutsceneSkipInputPressed) == "function" and isFileCutsceneSkipInputPressed(token) then
                    pcall(skipFileCutscene, token)
                end
                if isFileCutsceneFinished(token) then
                    local skipped = type(wasFileCutsceneSkipped) == "function" and wasFileCutsceneSkipped(token)
                    fadeFileCutscene(token, false, 0, 0, 0, 0)
                    killTimer(scene.finishTimer)
                    finishCutscene(scene, skipped and "skipped" or "finished")
                elseif getTickCount() - scene.startedAt > 120000 then
                    killTimer(scene.finishTimer)
                    finishCutscene(scene, "finish_timeout")
                end
            end, 50, 0))
        elseif getTickCount() - scene.requestedAt > 60000 then
            killTimer(scene.loadTimer)
            finishCutscene(scene, "load_timeout")
        end
    end, 50, 0))
end

local function monitorDrive(destination, car, smoke, fadeDuration)
    state.stage = "drive_" .. destination
    setControls(true)
    setCameraTarget(localPlayer)
    fadeCamera(true, fadeDuration or 1.0)
    showVehicleNavigation(car)
    showText(destination == "emmet" and "SWE2_A" or "SWE2_D", 10000)
    local startedAt, dialogueStarted, reminderShown, radioSelected = getTickCount(), false, false, false
    local returnHelpIndex, returnHelpAt, enteredAt, emmetHelpShown = 1, 0, nil, false
    local driveTimer
    driveTimer = rememberTimer(setTimer(function()
        if not state.active or state.stage ~= "drive_" .. destination or not isElement(car) or not isElement(smoke) then
            return killTimer(driveTimer)
        end
        applyActorPolicies()
        if getPedOccupiedVehicle(smoke) ~= car and isElementStreamedIn(smoke) and isElementSyncer(smoke) and
            type(setPedEnterVehicle) == "function" then
            pcall(setPedEnterVehicle, smoke, car, 1)
        end
        local playerInCar = getPedOccupiedVehicle(localPlayer) == car
        if playerInCar and not enteredAt then
            enteredAt = getTickCount()
        elseif not playerInCar then
            enteredAt = nil
        end
        if playerInCar and not radioSelected then
            radioSelected = true
            setRadioChannel(4)
        end
        if playerInCar and state.navigation.mode ~= "destination" then
            showDestination(destination)
            state.navigation.mode = "destination"
        elseif not playerInCar and state.navigation.mode ~= "vehicle" then
            showVehicleNavigation(car)
            state.navigation.mode = "vehicle"
        end
        if playerInCar and not dialogueStarted and getTickCount() - startedAt >= (destination == "emmet" and 5000 or 6000) then
            dialogueStarted = true
            playQueue(destination == "emmet" and NINES.audio.driveOut or NINES.audio.driveBack)
        elseif not playerInCar and not reminderShown then
            reminderShown = true
            clearAudio("left_vehicle")
            local reminder = NINES.audio.reminders[((state.reminderIndex or 1) - 1) % #NINES.audio.reminders + 1]
            state.reminderIndex = ((state.reminderIndex or 1) % #NINES.audio.reminders) + 1
            playQueue({reminder})
            showText("SWE2_L", 6000)
        elseif playerInCar then
            if reminderShown then
                clearAudio("reentered_vehicle")
                dialogueStarted = false
                startedAt = getTickCount()
            end
            reminderShown = false
        end
        if destination == "smoke" and not playerInCar and getTickCount() >= returnHelpAt then
            local key = ({"HELP53", "HOOD2D", "HOOD2E"})[returnHelpIndex]
            showHelp(key, false)
            returnHelpIndex = returnHelpIndex % 3 + 1
            returnHelpAt = getTickCount() + 5000
        elseif destination == "smoke" and playerInCar and enteredAt and not emmetHelpShown and getTickCount() - enteredAt >= 2000 then
            emmetHelpShown = true
            showHelp("EMMET_G", false)
            state.navigation.emmet = createBlip(2447.3643, -1974.4963, 12.5469, 6, 2, 255, 255, 255, 255)
            setElementDimension(state.navigation.emmet, NINES.dimension)
        end
        local profile = NINES.destinations[destination]
        local x, y, z = getElementPosition(car)
        local near = math.abs(x - profile[1]) <= profile[4] and math.abs(y - profile[2]) <= profile[4] and
                         math.abs(z - profile[3]) <= profile[4]
        local wheels = type(isVehicleOnAllWheels) ~= "function" or isVehicleOnAllWheels(car)
        if near and getPedOccupiedVehicle(smoke) == car and wheels then
            state.stage = "transition"
            destroyNavigation()
            clearAudio("arrival")
            if destination == "emmet" then
                setControls(false)
                acquireCamera({2452.9490, -2011.8243, 16.3096}, {2452.9805, -2010.9032, 15.9217}, false)
                if type(setPedTaskSequence) == "function" then
                    pcall(setPedTaskSequence, smoke, {
                        {task = "leave_car", vehicle = car},
                        {task = "go_to", x = 2453.5151, y = -1980.7709, z = 12.5547, movement = "walk", timeout = 5000},
                        {task = "go_to", x = 2450.4863, y = -1981.6593, z = 12.5547, movement = "walk", timeout = 5000},
                    }, false)
                end
                rememberTimer(setTimer(function()
                    if type(setPedTaskSequence) == "function" then
                        pcall(setPedTaskSequence, localPlayer, {
                            {task = "leave_car", vehicle = car},
                            {task = "go_to", x = 2453.29, y = -1983.71, z = 12.54, movement = "walk", timeout = 5000},
                        }, false)
                    elseif type(setPedExitVehicle) == "function" then
                        pcall(setPedExitVehicle, localPlayer)
                    end
                end, 800, 1))
                rememberTimer(setTimer(function()
                    if state.cameraToken then
                        fadeScriptCamera(state.cameraToken, false, 1.0, 0, 0, 0)
                    end
                end, 2800, 1))
                rememberTimer(setTimer(function()
                    releaseCamera(true)
                    triggerServerEvent("nines:arrived", resourceRoot, destination)
                end, 3800, 1))
            else
                triggerServerEvent("nines:arrived", resourceRoot, destination)
            end
        end
    end, 100, 0))
end

local roundProfiles = {
    [1] = {
        count = 1,
        camera = {{2445.7966, -1976.5854, 14.8864}, {2444.9832, -1977.1511, 14.7514}},
        smokeCamera = {{2451.3645, -1980.3950, 14.2880}, {2450.3652, -1980.3633, 14.2636}},
        shotCameras = {
            {{2439.8589, -1979.6781, 14.4278}, {2440.8330, -1979.7941, 14.2350}},
        },
        help = "SWE2_G", conditionalHelp = "HOOD2A", demoAudio = {NINES.audio.range.smoke1}, praise = NINES.audio.range.praise1,
    },
    [2] = {
        count = 3,
        camera = {{2447.3789, -1977.0619, 15.2851}, {2446.3914, -1977.0115, 15.1366}},
        move = {
            from = {2451.3879, -1981.1035, 14.5206}, to = {2451.7380, -1980.5852, 14.5765},
            trackFrom = {2450.4265, -1980.9240, 14.3127}, trackTo = {2450.8577, -1980.1874, 14.3180}, duration = 8000,
        },
        help = "SWE2_H", conditionalHelp = "HOOD2B", demoAudio = {NINES.audio.range.smoke2}, praise = NINES.audio.range.praise2,
    },
    [3] = {
        count = 5,
        camera = {{2453.7554, -1978.9587, 15.3890}, {2452.8372, -1978.6106, 15.1996}},
        smokeCamera = {{2449.0413, -1977.7316, 12.9667}, {2449.8354, -1978.3256, 13.0934}},
        shotCameras = {
            {{2452.8652, -1977.8590, 13.7390}, {2451.9014, -1978.1173, 13.6752}},
            {{2452.9795, -1980.7344, 13.8660}, {2452.0886, -1980.2820, 13.8231}},
            {{2453.3035, -1979.4598, 14.4320}, {2452.3540, -1979.2430, 14.2053}},
            {{2449.3628, -1971.9142, 13.7680}, {2449.4919, -1972.8959, 13.6288}},
            {{2442.5032, -1967.3361, 14.1262}, {2443.0920, -1968.1328, 13.9918}},
        },
        help = "SWE2_I", conditionalHelp = "HOOD2F", demoAudio = NINES.audio.range.smoke3, praise = NINES.audio.range.praise3,
    },
}

local function setCameraShot(shot)
    if not state.cameraToken or not shot then
        return false
    end
    return setScriptCameraFixed(state.cameraToken, Vector3(shot[1][1], shot[1][2], shot[1][3]),
                                Vector3(shot[2][1], shot[2][2], shot[2][3]), Vector3(0, 0, 0), true)
end

local function breakDemoBottle(index)
    for object, bottle in pairs(state.bottles) do
        if not bottle.playerTarget and bottle.index == index and isElement(object) then
            breakObject(object)
            return
        end
    end
end

local function startSmokeShots(round)
    local profile, smoke = roundProfiles[round], state.entities.smoke
    if not isElement(smoke) or type(setPedTaskSequence) ~= "function" then
        return false
    end
    local sequence, layout = {}, NINES.bottleRounds[round].demo
    for index = 1, profile.count do
        local p = layout[index]
        sequence[#sequence + 1] = {
            task = "shoot_at", x = p[1], y = p[2], z = p[3] - NINES.bottleCenterOffset,
            duration = round == 3 and 1400 or 1300, burstLength = 5,
        }
    end
    local ok, result = pcall(setPedTaskSequence, smoke, sequence, false)
    return ok and result == true
end

local function finishRoundDemo(round, delay, demo)
    rememberTimer(setTimer(function()
        if not state.active or state.stage ~= "range" or state.round ~= round then
            return
        end
        demo.finished = true
        if isTimer(demo.progressTimer) then
            killTimer(demo.progressTimer)
        end
        destroyBottles()
        triggerServerEvent("nines:stagePlayerRound", resourceRoot, round)
    end, delay, 1))
end

local function beginPlayerRound(round)
    if not state.active or state.stage ~= "range" then
        return
    end
    local profile = roundProfiles[round]
    destroyBottles()
    releaseCamera(false)
    setControls(true)
    state.round, state.roundHits = round, 0
    for index = 1, profile.count do
        createBottle(index, true)
    end
    showHelp(profile.conditionalHelp, true)
    showText(profile.help, 10000)
end

local function beginRoundDemo(round)
    local profile = roundProfiles[round]
    state.stage = "range"
    state.round, state.roundHits = round, 0
    setControls(false)
    destroyBottles()
    acquireCamera(profile.camera[1], profile.camera[2], round == 1 and 1.0 or false)
    for index = 1, profile.count do
        createBottle(index, false)
    end
    applyActorPolicies()
    local demo = {round = round, progress = -1, handled = {}, audioPlaying = false, nextLine = 1}

    local function valid()
        return not demo.finished and state.active and state.stage == "range" and state.round == round
    end

    local function watchProgress(handler)
        demo.progressTimer = rememberTimer(setTimer(function()
            if not valid() then
                return killTimer(demo.progressTimer)
            end
            if not demo.sequenceBegun then
                return
            end
            local progress = type(getPedTaskSequenceProgress) == "function" and
                                 getPedTaskSequenceProgress(state.entities.smoke) or -1
            if progress >= 0 then
                demo.progress = math.max(demo.progress, progress)
            elseif demo.taskStarted and demo.progress >= 0 then
                demo.progress = profile.count - 1
                if round == 3 and not demo.postTaskStarted and type(setPedGoTo) == "function" then
                    demo.postTaskStarted = true
                    pcall(setPedGoTo, state.entities.smoke, Vector3(2450.48, -1981.65, 12.55), "walk", 0.5, 2.0, 4000)
                end
            end
            handler()
        end, 50, 0))
    end

    rememberTimer(setTimer(function()
        if not valid() then
            return
        end
        if round == 1 then
            setCameraShot(profile.smokeCamera)
            playQueue(profile.demoAudio, function()
                if not valid() then
                    return
                end
                demo.sequenceBegun = true
                demo.taskStarted = startSmokeShots(round)
                if not demo.taskStarted then
                    demo.progress = 0
                end
            end)
            watchProgress(function()
                if demo.progress >= 0 and not demo.handled[1] then
                    demo.handled[1] = true
                    setCameraShot(profile.shotCameras[1])
                    rememberTimer(setTimer(function()
                        breakDemoBottle(1)
                    end, 500, 1))
                    finishRoundDemo(round, 1500, demo)
                end
            end)
        elseif round == 2 then
            setElementRotation(localPlayer, 0, 0, 65.0)
            if state.cameraToken then
                local move = profile.move
                resetScriptCamera(state.cameraToken)
                setScriptCameraPersist(state.cameraToken, true, true)
                moveScriptCamera(state.cameraToken, Vector3(move.from[1], move.from[2], move.from[3]),
                                 Vector3(move.to[1], move.to[2], move.to[3]), move.duration, true)
                trackScriptCamera(state.cameraToken, Vector3(move.trackFrom[1], move.trackFrom[2], move.trackFrom[3]),
                                  Vector3(move.trackTo[1], move.trackTo[2], move.trackTo[3]), move.duration, true)
            end
            if isElement(state.entities.smoke) then
                setPedAnimation(state.entities.smoke, "COLT45", "colt45_fire", 2000, false, false, false, true)
            end
            -- The public sequence surface starts with the first shot, so keep
            -- vanilla's preceding 2000 ms Colt animation outside the sequence.
            rememberTimer(setTimer(function()
                if valid() then
                    demo.sequenceBegun = true
                    demo.taskStarted = startSmokeShots(round)
                    if not demo.taskStarted then
                        demo.progress = profile.count - 1
                    end
                end
            end, 2000, 1))
            watchProgress(function()
                if demo.progress >= 0 and not demo.handled[1] then
                    demo.handled[1] = true
                    rememberTimer(setTimer(function()
                        breakDemoBottle(1)
                    end, 700, 1))
                end
                if demo.progress >= 1 and not demo.handled[2] then
                    demo.handled[2] = true
                    rememberTimer(setTimer(function()
                        breakDemoBottle(2)
                    end, 500, 1))
                end
                if demo.progress >= 2 and not demo.handled[3] then
                    demo.handled[3] = true
                    playQueue(profile.demoAudio, function()
                        if valid() then
                            finishRoundDemo(round, 1000, demo)
                        end
                    end, nil, function()
                        rememberTimer(setTimer(function()
                            breakDemoBottle(3)
                        end, 500, 1))
                    end)
                end
            end)
        else
            setElementRotation(localPlayer, 0, 0, 65.0)
            setCameraShot(profile.smokeCamera)
            if isElement(state.entities.smoke) then
                setPedAnimation(state.entities.smoke, "PED", "Crouch_Roll_R", 1000, false, true, true, false)
            end
            -- Lua progress 0..4 maps to the five vanilla shot children at
            -- progress 2..6; the unavailable duck child and roll are staged here.
            rememberTimer(setTimer(function()
                if valid() then
                    demo.sequenceBegun = true
                    demo.taskStarted = startSmokeShots(round)
                    if not demo.taskStarted then
                        demo.progress = profile.count - 1
                    end
                end
            end, 1000, 1))
            local function advanceAudio()
                if demo.audioPlaying or demo.nextLine > profile.count or demo.progress < demo.nextLine - 1 then
                    return
                end
                local index = demo.nextLine
                demo.audioPlaying = true
                setCameraShot(profile.shotCameras[index])
                playQueue({profile.demoAudio[index]}, function()
                    if not valid() then
                        return
                    end
                    demo.audioPlaying = false
                    demo.nextLine = index + 1
                    if demo.nextLine > profile.count then
                        return playQueue({profile.demoAudio[6]}, function()
                            if valid() then
                                finishRoundDemo(round, 0, demo)
                            end
                        end)
                    end
                    advanceAudio()
                end, nil, function()
                    rememberTimer(setTimer(function()
                        breakDemoBottle(index)
                    end, 500, 1))
                end)
            end
            watchProgress(advanceAudio)
        end
    end, 2500, 1))
end

local function completeRound()
    local round, profile = state.round, roundProfiles[state.round]
    state.stage = "range_transition"
    clearText()
    textApi("clearMissionHelp")
    playQueue({profile.praise}, function()
        if not state.active then
            return
        end
        setControls(false)
        state.stage = "range_transition"
        if round < 3 then
            rememberTimer(setTimer(function()
                if state.active and state.stage == "range_transition" then
                    triggerServerEvent("nines:stageDemoRound", resourceRoot, round + 1)
                end
            end, 2000, 1))
        else
            destroyBottles()
            rememberTimer(setTimer(function()
                if state.active and state.stage == "range_transition" then
                    triggerServerEvent("nines:rangeFinished", resourceRoot)
                end
            end, 2000, 1))
        end
    end)
end

local function beginRange(entities)
    state.entities = entities
    state.stage = "range"
    setControls(false)
    applyActorPolicies()
    beginRoundDemo(1)
    local rangeTimer
    rangeTimer = rememberTimer(setTimer(function()
        if not state.active or (state.stage ~= "range" and state.stage ~= "range_transition" and state.stage ~= "gas_tank") then
            return killTimer(rangeTimer)
        end
        applyActorPolicies()
        local x, y, z = getElementPosition(localPlayer)
        local c, r = NINES.range.center, NINES.range.radius
        local outside = math.abs(x - c[1]) > r[1] or math.abs(y - c[2]) > r[2] or math.abs(z - c[3]) > r[3]
        if outside and not state.rangeOutside then
            state.rangeOutside = true
            triggerServerEvent("nines:rangePresence", resourceRoot, true)
            showText("SWE2_J", 4000)
            state.navigation.range = createBlip(c[1], c[2], c[3], 0, 2, 0, 255, 0)
            setElementDimension(state.navigation.range, NINES.dimension)
        elseif not outside and state.rangeOutside then
            state.rangeOutside = false
            triggerServerEvent("nines:rangePresence", resourceRoot, false)
            if isElement(state.navigation.range) then
                destroyElement(state.navigation.range)
            end
            state.navigation.range = nil
        end
    end, 250, 0))
end

local function beginGasTank(entities)
    state.entities = entities
    state.stage = "gas_tank"
    destroyBottles()
    textApi("clearMissionHelp")
    clearAudio("gas_tank")
    acquireCamera({2447.2234, -1971.3845, 14.5714}, {2447.0703, -1970.4343, 14.2998}, false)
    showHelp("SWE2_F", false)
    rememberTimer(setTimer(function()
        if state.cameraToken then
            setScriptCameraFixed(state.cameraToken, Vector3(2448.2490, -1968.6077, 13.7351),
                                 Vector3(2447.9189, -1967.7069, 13.4528), Vector3(0, 0, 0), true)
        end
    end, 3000, 1))
    rememberTimer(setTimer(function()
        releaseCamera(false)
        setControls(true)
        showHelp("HOOD2C", false)
        local tampa = state.entities.tampa
        if isElement(tampa) and type(setVehiclePhysicalProofs) == "function" then
            pcall(setVehiclePhysicalProofs, tampa, false, false, false, false, false)
        end
    end, 5000, 1))
    local gasTimer
    gasTimer = rememberTimer(setTimer(function()
        if not state.active or state.stage ~= "gas_tank" then
            return killTimer(gasTimer)
        end
        local tampa = state.entities.tampa
        if isElement(tampa) and isVehicleBlown(tampa) then
            state.stage = "gas_done"
            setControls(false)
            textApi("clearMissionHelp")
            rememberTimer(setTimer(function()
                if state.active and state.stage == "gas_done" then
                    fadeCamera(false, 0.5, 0, 0, 0)
                end
            end, 2000, 1))
            rememberTimer(setTimer(function()
                if state.active and state.stage == "gas_done" then
                    if type(enginePreloadWorldAreaInDirection) == "function" then
                        pcall(enginePreloadWorldAreaInDirection, Vector3(2450.5669, -1975.6414, 12.5469), 288.0)
                    end
                    triggerServerEvent("nines:tampaDestroyed", resourceRoot)
                end
            end, 2500, 1))
        end
    end, 100, 0))
end

local function skipRequested()
    return getPedControlState(localPlayer, "enter_exit") or getPedControlState(localPlayer, "jump")
end

local function beginEmmetLeave(entities)
    state.entities = entities
    state.stage = "emmet_leave"
    setControls(false)
    clearAudio("emmet_leave")
    acquireCamera({2450.1067, -1977.5526, 14.0919}, {2450.7771, -1976.8383, 13.8915}, 0.5)
    if type(setPedTurnToFace) == "function" then
        pcall(setPedTurnToFace, state.entities.smoke, localPlayer)
        pcall(setPedTurnToFace, state.entities.emmet, localPlayer)
    end
    if type(setPedLookAt) == "function" and isElement(state.entities.emmet) then
        local x, y, z = getElementPosition(localPlayer)
        pcall(setPedLookAt, state.entities.emmet, Vector3(x, y, z + 0.7), 30000, localPlayer)
    end
    local finished, allowSkip, skipWasDown = false, false, skipRequested()
    local function done(skipped)
        if finished then
            return
        end
        finished = true
        clearAudio("emmet_scene_done")
        if skipped and state.cameraToken then
            fadeScriptCamera(state.cameraToken, false, 0.5, 0, 0, 0)
            return rememberTimer(setTimer(function()
                releaseCamera(true)
                rememberTimer(setTimer(function()
                    triggerServerEvent("nines:leaveEmmetFinished", resourceRoot, true)
                end, 100, 1))
            end, 500, 1))
        end
        releaseCamera(false)
        setControls(true)
        triggerServerEvent("nines:leaveEmmetFinished", resourceRoot, false)
    end
    local function playLine(index)
        if finished then
            return
        end
        if index > #NINES.audio.emmetLeave then
            return done(false)
        end
        playQueue({NINES.audio.emmetLeave[index]}, function()
            if finished then
                return
            end
            if index == 1 then
                allowSkip = true
            elseif index == 5 and type(setPedGoTo) == "function" then
                pcall(setPedGoTo, localPlayer, Vector3(2452.6416, -1998.2188, 12.5540), "walk", 0.5, 2.0, 20000)
                pcall(setPedGoTo, state.entities.smoke, Vector3(2454.9512, -1998.6581, 12.5540), "walk", 0.5, 2.0, 20000)
            elseif index == 6 and type(setPedGoTo) == "function" then
                pcall(setPedGoTo, state.entities.emmet, Vector3(2453.3206, -1987.7144, 12.5469), "walk", 0.5, 2.0, 20000)
                setElementPosition(localPlayer, 2452.0786, -1978.8445, 13.5469)
                setElementRotation(localPlayer, 0, 0, 180.0)
                pcall(setPedGoTo, localPlayer, Vector3(2452.6416, -1998.2188, 12.5540), "walk", 0.5, 2.0, 20000)
            elseif index == 7 and state.cameraToken then
                setScriptCameraFixed(state.cameraToken, Vector3(2453.5879, -1992.3597, 14.7070),
                                     Vector3(2453.5383, -1991.4121, 14.3914), Vector3(0, 0, 0), true)
            elseif index == 11 then
                if isElement(state.entities.emmet) and type(setPedStandStill) == "function" then
                    pcall(setPedStandStill, state.entities.emmet, 0)
                end
                if state.cameraToken then
                    setScriptCameraFixed(state.cameraToken, Vector3(2453.5691, -1992.4054, 16.0824),
                                         Vector3(2453.6028, -1993.3175, 15.6741), Vector3(0, 0, 0), true)
                end
                if type(setPedEnterVehicle) == "function" then
                    pcall(setPedEnterVehicle, state.entities.smoke, state.entities.glendale, 1)
                    pcall(setPedEnterVehicle, localPlayer, state.entities.glendale, 0)
                end
            end
            if index == 12 then
                local waitingSince, fallbackRequested = getTickCount(), false
                local seatTimer
                seatTimer = rememberTimer(setTimer(function()
                    if finished or not state.active or state.stage ~= "emmet_leave" then
                        return killTimer(seatTimer)
                    end
                    local car, smoke = state.entities.glendale, state.entities.smoke
                    if isElement(car) and getPedOccupiedVehicle(localPlayer) == car and getPedOccupiedVehicle(smoke) == car then
                        killTimer(seatTimer)
                        if state.cameraToken then
                            setScriptCameraFixed(state.cameraToken, Vector3(2456.7581, -1999.2529, 15.2969),
                                                 Vector3(2456.2307, -2000.0271, 14.9470), Vector3(0, 0, 0), true)
                        end
                        return rememberTimer(setTimer(function()
                            playLine(index + 1)
                        end, 200, 1))
                    end
                    if not fallbackRequested and getTickCount() - waitingSince >= 50000 then
                        fallbackRequested = true
                        triggerServerEvent("nines:departureSeatFallback", resourceRoot)
                    end
                end, 50, 0))
                return
            end
            rememberTimer(setTimer(function()
                playLine(index + 1)
            end, 200, 1))
        end)
    end
    playLine(1)
    local skipTimer
    skipTimer = rememberTimer(setTimer(function()
        if finished or not state.active or state.stage ~= "emmet_leave" then
            return killTimer(skipTimer)
        end
        local down = skipRequested()
        if allowSkip and down and not skipWasDown then
            done(true)
        end
        skipWasDown = down
    end, 100, 0))
end

local function beginGoodbye(entities)
    state.entities = entities
    state.stage = "goodbye"
    setControls(false)
    clearAudio("goodbye")
    acquireCamera({2086.0100, -1699.6982, 17.7495}, {2085.0596, -1699.8313, 17.4682}, false)
    if state.cameraToken then
        fadeScriptCamera(state.cameraToken, false, 1.0, 0, 0, 0)
    end
    rememberTimer(setTimer(function()
        if not state.active or state.stage ~= "goodbye" or not state.cameraToken then
            return
        end
        setScriptCameraFixed(state.cameraToken, Vector3(2069.6067, -1704.6298, 14.2313),
                             Vector3(2070.4746, -1704.1737, 14.0353), Vector3(0, 0, 0), true)
        fadeScriptCamera(state.cameraToken, true, 1.0, 0, 0, 0)
        if type(setPedTurnToFace) == "function" then
            pcall(setPedTurnToFace, state.entities.smoke, localPlayer)
        end
        rememberTimer(setTimer(function()
            playQueue({NINES.audio.goodbye[1]}, function()
                if not state.active then
                    return
                end
                if type(setPedGoTo) == "function" then
                    pcall(setPedGoTo, state.entities.smoke, Vector3(2065.1, -1703.4, 12.5547), "walk", 0.5, 2.0, 10000)
                    pcall(setPedGoTo, localPlayer, Vector3(2073.2866, -1702.1045, 12.5547), "walk", 0.5, 2.0, 10000)
                end
                rememberTimer(setTimer(function()
                    if state.cameraToken then
                        setScriptCameraFixed(state.cameraToken, Vector3(2072.7229, -1699.7369, 14.1283),
                                             Vector3(2072.9346, -1700.6702, 13.8382), Vector3(0, 0, 0), true)
                    end
                    playQueue({NINES.audio.goodbye[2]}, function()
                        releaseCamera(false)
                        triggerServerEvent("nines:goodbyeFinished", resourceRoot)
                    end)
                end, 1000, 1))
            end)
        end, 1000, 1))
    end, 1000, 1))
end

local function beginBinco()
    state.stage = "binco_outside"
    setControls(true)
    showDestination("binco")
    showText("COLORS", 6000)
    rememberTimer(setTimer(function()
        if not state.active or state.stage ~= "binco_outside" then
            return
        end
        local p = NINES.binco.outside
        local x, y, z = getElementPosition(localPlayer)
        if getElementInterior(localPlayer) == 0 and getDistanceBetweenPoints3D(x, y, z, p[1], p[2], p[3]) <= 4.0 then
            state.stage = "binco_entry_scene"
            setControls(false)
            destroyNavigation()
            acquireCamera({2253.0986, -1644.2246, 16.6501}, {2252.7742, -1645.1615, 16.5203}, false)
            showText("S2HELP1", 5000)
            rememberTimer(setTimer(function()
                releaseCamera(false)
                setControls(true)
                state.stage = "binco_enter_ready"
                showHelp("HELWARD", false)
            end, 2500, 1))
        end
    end, 100, 0))
end

local function beginPhone()
    state.stage = "phone"
    setControls(false)
    playQueue({{23000, false, "cj"}})
    rememberTimer(setTimer(function()
        if not state.active or state.stage ~= "phone" then
            return
        end
        setPedAnimation(localPlayer, "ped", "phone_talk", -1, true, false, false, true)
        rememberTimer(setTimer(function()
            if not state.active or state.stage ~= "phone" then
                return
            end
            playQueue({NINES.audio.phone[1]}, function()
                if not state.active then
                    return
                end
                beginBinco()
                local remaining = {}
                for index = 2, #NINES.audio.phone do
                    remaining[#remaining + 1] = NINES.audio.phone[index]
                end
                playQueue(remaining, function()
                    state.phoneFinished = true
                    setPedAnimation(localPlayer, false)
                end)
            end)
        end, 1800, 1))
    end, 1500, 1))
end

local function clearState(reason)
    clearTimers()
    clearAudio(reason)
    destroyNavigation()
    destroyBottles()
    releaseCamera(false)
    if state.cutscene and state.cutscene.token and type(releaseFileCutscene) == "function" then
        pcall(releaseFileCutscene, state.cutscene.token, false)
    end
    state.cutscene = nil
    if state.missionText then
        textApi("clearMissionTexts")
        textApi("releaseMissionText")
    end
    setPedAnimation(localPlayer, false)
    setControls(true)
    state.active = false
    state.stage = nil
    state.entities = {}
    state.missionText = false
    state.round = 0
    state.roundHits = 0
    state.rangeOutside = false
    state.phoneFinished = false
end

addEvent("nines:start", true)
addEventHandler("nines:start", resourceRoot, function()
    clearState("replaced")
    state.active = true
    state.stage = "intro"
    ensureText()
end)

addEvent("nines:cutscene", true)
addEventHandler("nines:cutscene", resourceRoot, function(kind, name)
    if source == resourceRoot and state.active then
        state.stage = kind .. "_cutscene"
        beginFileCutscene(kind, name)
    end
end)

addEvent("nines:drive", true)
addEventHandler("nines:drive", resourceRoot, function(destination, car, smoke, skippedScene)
    if source ~= resourceRoot or not state.active then
        return
    end
    state.entities.glendale, state.entities.smoke = car, smoke
    applyActorPolicies()
    monitorDrive(destination, car, smoke, skippedScene == true and 0.5 or nil)
end)

addEvent("nines:range", true)
addEventHandler("nines:range", resourceRoot, function(entities)
    if source == resourceRoot and state.active then
        beginRange(entities)
    end
end)

addEvent("nines:playerRoundReady", true)
addEventHandler("nines:playerRoundReady", resourceRoot, function(round)
    if source == resourceRoot and state.active and state.stage == "range" and tonumber(round) == state.round then
        beginPlayerRound(tonumber(round))
    end
end)

addEvent("nines:demoRoundReady", true)
addEventHandler("nines:demoRoundReady", resourceRoot, function(round)
    round = tonumber(round)
    if source == resourceRoot and state.active and state.stage == "range_transition" and round == state.round + 1 then
        beginRoundDemo(round)
    end
end)

addEvent("nines:gasTank", true)
addEventHandler("nines:gasTank", resourceRoot, function(entities)
    if source == resourceRoot and state.active then
        beginGasTank(entities)
    end
end)

addEvent("nines:emmetLeave", true)
addEventHandler("nines:emmetLeave", resourceRoot, function(entities)
    if source == resourceRoot and state.active then
        beginEmmetLeave(entities)
    end
end)

addEvent("nines:goodbye", true)
addEventHandler("nines:goodbye", resourceRoot, function(entities)
    if source == resourceRoot and state.active then
        beginGoodbye(entities)
    end
end)

addEvent("nines:phone", true)
addEventHandler("nines:phone", resourceRoot, function()
    if source == resourceRoot and state.active then
        beginPhone()
    end
end)

addEvent("nines:failed", true)
addEventHandler("nines:failed", resourceRoot, function(key)
    if source == resourceRoot and state.active then
        clearAudio("failed")
        releaseCamera(false)
        showText(key or "M_FAIL", 10000)
        textApi("showMissionBigText", "M_FAIL", 5000, 1)
    end
end)

addEvent("nines:passed", true)
addEventHandler("nines:passed", resourceRoot, function()
    if source == resourceRoot and state.active then
        clearAudio("passed")
        releaseCamera(false)
        textApi("showMissionBigText", "M_PASSR", 5000, 1, 4)
        if type(playMissionPassedTune) == "function" then
            pcall(playMissionPassedTune, 1)
        end
    end
end)

addEvent("nines:stop", true)
addEventHandler("nines:stop", resourceRoot, function(reason)
    if source == resourceRoot then
        clearState(reason or "server_stop")
    end
end)

addEventHandler("onClientObjectDamage", root, function(_, attacker)
    local bottle = state.bottles[source]
    if not state.active or state.stage ~= "range" or not bottle or not bottle.playerTarget or attacker ~= localPlayer then
        return
    end
    cancelEvent()
    state.bottles[source] = nil
    if isElement(source) then
        destroyElement(source)
    end
    state.roundHits = state.roundHits + 1
    triggerServerEvent("nines:bottleHit", resourceRoot, state.round, bottle.index)
    if state.roundHits >= roundProfiles[state.round].count then
        completeRound()
    end
end)

addEventHandler("onClientElementStreamIn", root, function()
    if state.active and (source == state.entities.smoke or source == state.entities.emmet) then
        rememberTimer(setTimer(applyActorPolicies, 0, 1))
    end
end)

addEventHandler("onClientRender", root, function()
    if not state.active then
        return
    end
    if state.navigation.mode == "destination" and state.navigation.destination and type(renderScriptImportantArea) == "function" then
        local destination = NINES.destinations[state.navigation.destination]
        renderScriptImportantArea(Vector3(destination[1], destination[2], destination[3]), destination[4], destination[4],
                                  state.navigation.destination == "emmet" and 1 or 2)
    end
    if state.stage == "binco_enter_ready" then
        local p = NINES.binco.outside
        local x, y, z = getElementPosition(localPlayer)
        local enterDown = getPedControlState(localPlayer, "enter_exit")
        if enterDown and not state.enterWasDown and getElementInterior(localPlayer) == 0 and
            getDistanceBetweenPoints3D(x, y, z, p[1], p[2], p[3]) <= 5.0 then
            state.stage = "binco_enter_pending"
            triggerServerEvent("nines:bincoInterior", resourceRoot, true)
        end
        state.enterWasDown = enterDown
    elseif state.stage == "binco_clothes" and getElementInterior(localPlayer) == NINES.binco.insideSpawn[4] then
        local p = NINES.binco.clothes
        local x, y, z = getElementPosition(localPlayer)
        if getDistanceBetweenPoints3D(x, y, z, p[1], p[2], p[3]) <= 2.0 then
            state.stage = "binco_tutorial"
            setControls(false)
            acquireCamera({213.0277, -102.9124, 1006.3765}, {213.7582, -102.2797, 1006.1193}, false)
            rememberTimer(setTimer(function()
                showText("S2HELP2", 6000)
                rememberTimer(setTimer(function()
                    releaseCamera(false)
                    setControls(true)
                    state.stage = "binco_exit"
                    showHelp("HELWARD", false)
                end, 4000, 1))
            end, 1500, 1))
        end
    elseif state.stage == "binco_exit" then
        local p = NINES.binco.exit
        local x, y, z = getElementPosition(localPlayer)
        if getDistanceBetweenPoints3D(x, y, z, p[1], p[2], p[3]) <= 2.0 then
            state.stage = "binco_exit_pending"
            triggerServerEvent("nines:bincoInterior", resourceRoot, false)
        end
    end
end)

addEvent("nines:bincoEntered", true)
addEventHandler("nines:bincoEntered", resourceRoot, function()
    if source == resourceRoot and state.active and state.stage == "binco_enter_pending" then
        state.stage = "binco_clothes"
    end
end)

addEvent("nines:bincoExited", true)
addEventHandler("nines:bincoExited", resourceRoot, function()
    if source == resourceRoot and state.active and state.stage == "binco_exit_pending" then
        state.stage = "binco_return"
        rememberTimer(setTimer(function()
            if state.active then
                triggerServerEvent("nines:passed", resourceRoot)
            end
        end, 2000, 1))
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    clearState("resource_stop")
end)
