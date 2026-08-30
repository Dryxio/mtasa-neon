local state = {active = false, timers = {}, audio = nil, camera = nil, text = false, recordings = {}}

local function cameraTargetsLocalPlayer()
    local target = getCameraTarget()
    if target == localPlayer then
        return true
    end
    local vehicle = getPedOccupiedVehicle(localPlayer)
    return isElement(vehicle) and target == vehicle
end

local function transitionLog(event, data)
    local record = {event = event}
    for key, value in pairs(type(data) == "table" and data or {}) do
        record[key] = value
    end
    outputDebugString("[sweet-and-kendl-transition-jsonl] " .. tostring(toJSON(record, true)):gsub("[\r\n]", ""))
end

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

local function playQueue(lines, actors, finished, index, started, maxDurations)
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
            playQueue(lines, actors, finished, index + 1, started, maxDurations)
        end, maxDurations and maxDurations[index] or 3200, 1))
    end
    local handle = requestMissionAudio(line[1])
    if not handle then
        showText(line[2], 3000)
        return rememberTimer(setTimer(function()
            playQueue(lines, actors, finished, index + 1, started, maxDurations)
        end, maxDurations and maxDurations[index] or 3200, 1))
    end
    local audio = {handle = handle, requestedAt = getTickCount()}
    state.audio = audio
    audio.loadTimer = setTimer(function()
        if state.audio ~= audio then return end
        if isMissionAudioLoaded(handle) then
            killTimer(audio.loadTimer)
            if not playMissionAudio(handle) then
                clearAudio()
                return playQueue(lines, actors, finished, index + 1, started, maxDurations)
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
                local elapsed = getTickCount() - audio.startedAt
                if isMissionAudioFinished(handle) or elapsed > 30000 or
                    (maxDurations and maxDurations[index] and elapsed >= maxDurations[index]) then
                    clearAudio()
                    playQueue(lines, actors, finished, index + 1, started, maxDurations)
                end
            end, 50, 0)
        elseif getTickCount() - audio.requestedAt > 10000 then
            clearAudio()
            playQueue(lines, actors, finished, index + 1, started, maxDurations)
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

local reportBarrier

local function sceneIsActive(scene)
    return state.active and state.scene == scene
end

local function beginScene(id, name)
    local scene = {id = id, name = name, streamingLeases = {}}
    state.scene = scene
    return scene
end

local function leaseSceneElement(scene, element)
    if not sceneIsActive(scene) or not isElement(element) or type(acquireElementStreamingLease) ~= "function" then
        return false
    end
    local ok, lease = pcall(acquireElementStreamingLease, element)
    if ok and lease then
        scene.streamingLeases[#scene.streamingLeases + 1] = lease
        return true
    end
    return false
end

local function releaseSceneLeases(scene)
    if not scene or type(releaseElementStreamingLease) ~= "function" then return end
    for _, lease in ipairs(scene.streamingLeases or {}) do pcall(releaseElementStreamingLease, lease) end
    scene.streamingLeases = {}
end

local function afterScene(scene, delay, callback)
    return rememberTimer(setTimer(function()
        if sceneIsActive(scene) then callback() end
    end, delay, 1))
end

local function finishScene(scene, ok, reason)
    if not sceneIsActive(scene) then return end
    releaseSceneLeases(scene)
    state.scene = nil
    reportBarrier(scene.id, ok ~= false, reason)
end

local function ownsPedPresentation(ped)
    return isElement(ped) and (ped == localPlayer or isElementSyncer(ped))
end

local function lookAtPed(ped, target, duration)
    if not ownsPedPresentation(ped) or not isElement(target) or type(setPedLookAt) ~= "function" then return end
    local x, y, z = getElementPosition(target)
    pcall(setPedLookAt, ped, Vector3(x, y, z + 0.7), duration or 4000, target)
end

local function playPedAnimation(ped, block, animation, duration, loop)
    if not ownsPedPresentation(ped) or type(setPedAnimation) ~= "function" then return end
    pcall(setPedAnimation, ped, block, animation, duration or -1, loop == true, false, false, false)
end

local function gangActor(actors, role)
    return actors.gang and actors.gang[role]
end

local function startFacial(ped, duration)
    if ownsPedPresentation(ped) and type(setPedFacialTalk) == "function" then
        pcall(setPedFacialTalk, ped, duration or 20000)
    end
end

reportBarrier = function(id, ok, reason)
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
    if not isElement(vehicle) or type(requestVehicleRecording) ~= "function" then
        return false
    end
    stopRecording(id, vehicle)
    requestVehicleRecording(id)
    local recording = {requestedAt = getTickCount(), vehicle = vehicle}
    state.recordings[id] = recording
    recording.timer = setTimer(function()
        if state.recordings[id] ~= recording or not isElement(vehicle) then return end
        if not isElementStreamedIn(vehicle) or not isElementSyncer(vehicle) then
            if getTickCount() - recording.requestedAt > 15000 then
                killTimer(recording.timer)
                state.recordings[id] = nil
            end
        elseif isVehicleRecordingLoaded(id) then
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
    local scene = beginScene(id, "smoke")
    for _, element in ipairs({actors.peren, actors.smoke, state.leader}) do leaseSceneElement(scene, element) end
    local camera = SAK.cameras.smoke
    if not fixedCamera(camera.position, camera.target, 1.5) then
        return finishScene(scene, false, "camera Smoke refusee")
    end
    if isElement(actors.peren) and type(setPedEnterVehicle) == "function" then
        if ownsPedPresentation(actors.smoke) then pcall(setPedEnterVehicle, actors.smoke, actors.peren, 1) end
        if localPlayer == state.leader then pcall(setPedEnterVehicle, localPlayer, actors.peren, 0) end
    end

    playQueue({SAK.audio.smokeIntro[1], SAK.audio.smokeIntro[2]}, actors, function()
        if not sceneIsActive(scene) then return end
        moveCamera(camera)
        local startedAt = getTickCount()
        local occupancyTimer
        occupancyTimer = rememberTimer(setTimer(function()
            if not sceneIsActive(scene) then
                if isTimer(occupancyTimer) then killTimer(occupancyTimer) end
                return
            end
            local occupied = isElement(actors.peren) and isElement(actors.smoke) and isElement(state.leader) and
                                 getPedOccupiedVehicle(state.leader) == actors.peren and
                                 getPedOccupiedVehicleSeat(state.leader) == 0 and
                                 getPedOccupiedVehicle(actors.smoke) == actors.peren and
                                 getPedOccupiedVehicleSeat(actors.smoke) == 1
            if not occupied and getTickCount() - startedAt <= 12000 then return end
            killTimer(occupancyTimer)
            if not occupied then
                releaseCamera()
                return finishScene(scene, false, "occupation Peren timeout")
            end
            if localPlayer == state.leader and type(setPedDriveTo) == "function" then
                pcall(setPedDriveTo, localPlayer, actors.peren, Vector3(2414.68, -1656.38, 12.38), 10.0, "normal",
                      "avoid_cars")
            end
            playQueue({SAK.audio.smokeIntro[3], SAK.audio.smokeIntro[4]}, actors, function()
                if not sceneIsActive(scene) then return end
                releaseCamera()
                finishScene(scene, true)
            end)
        end, 50, 0))
    end)
end

local function runFuneralScene(id, actors)
    local scene = beginScene(id, "funeral")
    leaseSceneElement(scene, actors.peren)
    if actors.ballas then
        for _, element in ipairs({actors.ballas.vehicle, actors.ballas.driver, actors.ballas.passenger}) do
            leaseSceneElement(scene, element)
        end
    end
    for _, actor in pairs(actors.gang or {}) do
        leaseSceneElement(scene, actor.ped)
        leaseSceneElement(scene, actor.bike)
    end
    local camera = SAK.cameras.funeral
    if not fixedCamera(camera.position, camera.target, 0.5) then
        return finishScene(scene, false, "camera funerailles refusee")
    end
    if actors.ballas then
        local ballas = actors.ballas
        if isElementSyncer(ballas.driver) then
            for _, ped in ipairs({ballas.driver, ballas.passenger}) do
                if type(setPedMissionActor) == "function" then pcall(setPedMissionActor, ped, true) end
                if type(setPedWeaponAccuracy) == "function" then pcall(setPedWeaponAccuracy, ped, 25) end
                if type(setPedWeaponShootingRate) == "function" then pcall(setPedWeaponShootingRate, ped, 30) end
            end
            pcall(setPedPhysicalProofs, ballas.driver, true, true, true, true, true)
            pcall(setPedPhysicalProofs, ballas.passenger, true, true, true, true, true)
            startRecording(201, ballas.vehicle, 1.0, 3300)
            afterScene(scene, 3300, function()
                if not isElement(ballas.vehicle) or type(setPedDriveBy) ~= "function" then return end
                pcall(setPedDriveBy, ballas.passenger, Vector3(952.92, -1102.99, 22.85), 100.0, "ai_side", true,
                      90)
                if isElement(actors.peren) then
                    pcall(setPedDriveBy, ballas.driver, actors.peren, 100.0, "fixed_lhs", false, 90)
                end
            end)
        end
    end
    for role, actor in pairs(actors.gang or {}) do
        local profile = SAK.funeralActors[role]
        if ownsPedPresentation(actor.ped) and profile then
            if type(setPedMissionActor) == "function" then pcall(setPedMissionActor, actor.ped, true) end
            if type(setPedNeverTargeted) == "function" then pcall(setPedNeverTargeted, actor.ped, true) end
            if type(setPedCanBeDraggedOut) == "function" then pcall(setPedCanBeDraggedOut, actor.ped, false) end
            if type(setPedCanBeKnockedOffBike) == "function" then pcall(setPedCanBeKnockedOffBike, actor.ped, false) end
            if type(setPedGoTo) == "function" then
                pcall(setPedGoTo, actor.ped, Vector3(profile.walk[1], profile.walk[2], profile.walk[3]), "walk", 0.5,
                      2.0, 10000)
            end
        end
    end

    afterScene(scene, 4800, function() fixedCamera(camera.attackPosition, camera.attackTarget, false) end)
    afterScene(scene, 7300, function()
        local ryder = gangActor(actors, "ryder")
        if ryder and actors.ballas then lookAtPed(ryder.ped, actors.ballas.vehicle, 4000) end
    end)
    afterScene(scene, 8300, function()
        playQueue({SAK.audio.funeral[1]}, actors, function()
            if not sceneIsActive(scene) then return end
            afterScene(scene, 1000, function()
                if localPlayer == state.leader then
                    triggerServerEvent("sak:funeralExplosion", resourceRoot, state.id)
                end
                playQueue({SAK.audio.funeral[2]}, actors, function()
                    if not sceneIsActive(scene) then return end
                    fixedCamera(camera.sweetPosition, camera.sweetTarget, false)
                    for _, role in ipairs({"smoke", "sweet"}) do
                        local actor, profile = gangActor(actors, role), SAK.funeralActors[role]
                        if actor and ownsPedPresentation(actor.ped) and profile then
                            setElementPosition(actor.ped, profile.restage[1], profile.restage[2], profile.restage[3])
                            setElementRotation(actor.ped, 0, 0, profile.restage[4])
                        end
                    end
                    for _, role in ipairs({"smoke", "ryder", "sweet"}) do
                        local actor = gangActor(actors, role)
                        if actor and ownsPedPresentation(actor.ped) and isElement(actor.bike) and
                            type(setPedEnterVehicle) == "function" then
                            pcall(setPedEnterVehicle, actor.ped, actor.bike, 0)
                        end
                    end
                    playQueue({SAK.audio.funeral[3]}, actors, function()
                        if not sceneIsActive(scene) then return end
                        playQueue({SAK.audio.funeral[4]}, actors, function()
                            if not sceneIsActive(scene) then return end
                            if actors.ballas and isElementSyncer(actors.ballas.driver) then
                                pcall(setPedPhysicalProofs, actors.ballas.driver, false, false, false, false, false)
                                pcall(setPedPhysicalProofs, actors.ballas.passenger, false, false, false, false, false)
                            end
                            for _, role in ipairs({"smoke", "sweet"}) do
                                local actor = gangActor(actors, role)
                                if actor and ownsPedPresentation(actor.ped) and type(setPedWeaponSlot) == "function" then
                                    pcall(setPedWeaponSlot, actor.ped, 0)
                                end
                            end
                            -- AQ belongs after the scripted presentation but before
                            -- INTRO2E. This party barrier is its synchronized ACK.
                            releaseCamera()
                            playQueue(SAK.audio.postFuneral, actors, function() finishScene(scene, true) end)
                        end)
                    end, nil, function()
                        if localPlayer == state.leader and type(setPedGoTo) == "function" then
                            pcall(setPedGoTo, localPlayer, Vector3(971.1810, -1108.1931, 22.8672), "run", 0.5,
                                  2.0, 10000)
                        end
                    end)
                end, nil, nil, {1600})
            end)
        end, nil, function()
            local ballasVehicle = actors.ballas and actors.ballas.vehicle
            if isElement(ballasVehicle) then
                for _, ped in ipairs({state.leader, gangActor(actors, "smoke") and gangActor(actors, "smoke").ped,
                                      gangActor(actors, "ryder") and gangActor(actors, "ryder").ped,
                                      gangActor(actors, "sweet") and gangActor(actors, "sweet").ped}) do
                    lookAtPed(ped, ballasVehicle, 4000)
                end
            end
            if camera.attackMoveTo and camera.attackTrackTo then
                moveCamera({position = camera.attackPosition, target = camera.attackTarget,
                            moveTo = camera.attackMoveTo, trackTo = camera.attackTrackTo,
                            duration = camera.attackDuration})
            end
            afterScene(scene, 1000, function()
                local driver = actors.ballas and actors.ballas.driver
                for _, role in ipairs({"smoke", "sweet"}) do
                    local actor = gangActor(actors, role)
                    if actor and ownsPedPresentation(actor.ped) and isElement(driver) and
                        type(setPedKillOnFoot) == "function" then
                        if type(givePedWeapon) == "function" then givePedWeapon(actor.ped, 22, 50, true) end
                        pcall(setPedKillOnFoot, actor.ped, driver)
                    end
                end
            end)
        end)
    end)
end

local function runSplitScene(id, actors)
    local scene = beginScene(id, "split")
    if actors.ballas then
        for _, element in ipairs({actors.ballas.vehicle, actors.ballas.driver, actors.ballas.passenger}) do
            leaseSceneElement(scene, element)
        end
    end
    for _, actor in pairs(actors.gang or {}) do
        leaseSceneElement(scene, actor.ped)
        leaseSceneElement(scene, actor.bike)
    end
    local camera = SAK.cameras.split
    if not fixedCamera(camera.position, camera.target, 0.5) then
        return finishScene(scene, false, "camera split refusee")
    end
    local sweet, ballas = gangActor(actors, "sweet"), actors.ballas
    local ballasAlive = ballas and isElement(ballas.vehicle) and not isVehicleBlown(ballas.vehicle) and
                            isElement(ballas.driver) and not isPedDead(ballas.driver) and
                            isElement(ballas.passenger) and not isPedDead(ballas.passenger)
    if sweet and ownsPedPresentation(sweet.ped) and isElement(sweet.bike) and type(setPedDriveTo) == "function" then
        local target = ballasAlive and Vector3(1540.2981, -1159.1885, 22.9062) or
                           Vector3(SAK.route2[1].x, SAK.route2[1].y, SAK.route2[1].z)
        pcall(setPedDriveTo, sweet.ped, sweet.bike, target, 20.0, "normal", "avoid_cars")
    end
    if ballasAlive and isElementSyncer(ballas.driver) and sweet and isElement(sweet.bike) and
        type(setPedDriveMission) == "function" then
        pcall(setPedDriveMission, ballas.driver, ballas.vehicle, sweet.bike, "escort_left", 40.0, "avoid_cars")
    end
    if camera.moveTo and camera.trackTo then moveCamera(camera) end

    local function playRyderLine()
        if not sceneIsActive(scene) then return end
        fixedCamera(camera.ryderPosition, camera.ryderTarget, false)
        playQueue({SAK.audio.split[2]}, actors, function()
            if not sceneIsActive(scene) then return end
            afterScene(scene, 1000, function()
                local ryder = gangActor(actors, "ryder")
                if ryder and ownsPedPresentation(ryder.ped) and type(setPedAnimation) == "function" then
                    pcall(setPedAnimation, ryder.ped, false)
                end
                releaseCamera()
                finishScene(scene, true)
            end)
        end, nil, function()
            local ryder = gangActor(actors, "ryder")
            if ryder then playPedAnimation(ryder.ped, "MISC", "BMX_COMEON", -1, false) end
        end)
    end
    afterScene(scene, 500, function()
        if not ballasAlive then return playRyderLine() end
        playQueue({SAK.audio.split[1]}, actors, function() afterScene(scene, 1000, playRyderLine) end)
    end)
end

local function runFinaleScene(id, actors)
    local scene = beginScene(id, "finale")
    for _, actor in pairs(actors.gang or {}) do
        leaseSceneElement(scene, actor.ped)
        leaseSceneElement(scene, actor.bike)
    end
    local cameras = SAK.cameras.finale
    if not fixedCamera(cameras[1].position, cameras[1].target, 1.0) then
        return finishScene(scene, false, "camera finale refusee")
    end
    if localPlayer == state.leader and isPedInVehicle(localPlayer) then
        playPedAnimation(localPlayer, "MISC", "BMX_CELEBRATE", -1, false)
    end

    afterScene(scene, 1000, function()
        local ryder, sweet, smoke = gangActor(actors, "ryder"), gangActor(actors, "sweet"),
                                    gangActor(actors, "smoke")
        if ryder and isElementSyncer(ryder.bike) then startRecording(206, ryder.bike, 1.0, 0) end
        playQueue({SAK.audio.finale[1]}, actors, function()
            if not sceneIsActive(scene) then return end
            afterScene(scene, 200, function()
                local function finishFinale()
                    if ryder and ownsPedPresentation(ryder.ped) and isElement(ryder.bike) and
                        type(setPedDriveTo) == "function" then
                        pcall(setPedDriveTo, ryder.ped, ryder.bike, Vector3(2466.27, -1690.28, 12.51), 2.0,
                              "accurate", "avoid_cars")
                    end
                    afterScene(scene, 3000, function()
                        releaseCamera()
                        finishScene(scene, true)
                    end)
                end

                local function startFarewellLines()
                    if not sceneIsActive(scene) then return end
                    fixedCamera(cameras[6].position, cameras[6].target, false)
                    if smoke and isElement(smoke.bike) and isElementSyncer(smoke.bike) then
                        setElementRotation(smoke.bike, 0, 0, 0)
                    end
                    playQueue({SAK.audio.finale[10], SAK.audio.finale[11]}, actors, finishFinale, nil,
                              function(index)
                        if ryder then
                            lookAtPed(ryder.ped, state.leader, index == 1 and 15000 or 12500)
                            startFacial(ryder.ped)
                        end
                        if index == 2 then
                            afterScene(scene, 3000, function()
                                if smoke and ownsPedPresentation(smoke.ped) and isElement(smoke.bike) and
                                    type(setPedDriveTo) == "function" then
                                    pcall(setPedDriveTo, smoke.ped, smoke.bike,
                                          Vector3(2501.2441, -1657.7692, 12.3949), 4.0, "accurate", "avoid_cars")
                                end
                                if sweet and ownsPedPresentation(sweet.ped) and isElement(sweet.bike) and
                                    type(setPedDriveTo) == "function" then
                                    pcall(setPedDriveTo, sweet.ped, sweet.bike,
                                          Vector3(2497.1692, -1680.4458, 12.3614), 6.0, "accurate", "avoid_cars")
                                end
                            end)
                        end
                    end)
                end

                local middle = {}
                for index = 2, 9 do middle[#middle + 1] = SAK.audio.finale[index] end
                playQueue(middle, actors, function()
                    afterScene(scene, 1500, startFarewellLines)
                end, nil, function(index)
                    if index == 2 or index == 4 or index == 6 or index == 8 then
                        local cameraIndex = index == 2 and 2 or index == 4 and 3 or index == 6 and 4 or 5
                        fixedCamera(cameras[cameraIndex].position, cameras[cameraIndex].target, false)
                    end
                    if index == 1 then
                        if sweet then
                            lookAtPed(sweet.ped, state.leader, 30000)
                            startFacial(sweet.ped)
                            playPedAnimation(sweet.ped, "MISC", "bmx_idleloop_01", -1, true)
                        end
                        if sweet then lookAtPed(state.leader, sweet.ped, 30000) end
                    elseif index == 2 or index == 3 then
                        startFacial(state.leader)
                        if sweet then lookAtPed(state.leader, sweet.ped, 30000) end
                        playPedAnimation(state.leader, "MISC",
                                         index == 2 and "bmx_talkright_loop" or "bmx_idleloop_01", -1, true)
                    elseif index == 4 or index == 6 then
                        if sweet then startFacial(sweet.ped) end
                        if smoke and sweet then lookAtPed(smoke.ped, sweet.ped, 15000) end
                    elseif index == 5 or index == 7 then
                        startFacial(state.leader)
                        playPedAnimation(state.leader, "MISC", "bmx_talkright_loop", -1, true)
                    elseif index == 8 then
                        if smoke then
                            lookAtPed(smoke.ped, state.leader, 15000)
                            startFacial(smoke.ped)
                            playPedAnimation(smoke.ped, "MISC", "bmx_talkleft_loop", -1, true)
                        end
                    end
                end)
            end)
        end, nil, function()
            if ryder then
                lookAtPed(ryder.ped, state.leader, 30000)
                startFacial(ryder.ped)
            end
            if ryder then lookAtPed(state.leader, ryder.ped, 30000) end
            if smoke then playPedAnimation(smoke.ped, "MISC", "bmx_idleloop_02", -1, true) end
            playPedAnimation(state.leader, "MISC", "bmx_idleloop_01", -1, true)
            if sweet and isElementSyncer(sweet.bike) then startRecording(205, sweet.bike, 0.8, 0) end
        end)
    end)
end

local function finishSaveTutorial(scene)
    if not sceneIsActive(scene) then return end
    state.saveTutorial = nil
    textApi("clearMissionHelp")
    releaseCamera()
    finishScene(scene, true)
end

local function runSaveTutorial(id)
    local scene = beginScene(id, "save_tutorial")
    state.saveTutorial = scene
    if not fixedCamera({2499.3057, -1678.9210, 13.0313}, {2499.0439, -1679.8518, 13.2865}, false) then
        state.saveTutorial = nil
        return finishScene(scene, false, "camera tutoriel sauvegarde refusee")
    end
    showHelp("SAVE_G", false)
    afterScene(scene, 6000, function()
        fixedCamera({2504.2800, -1682.8573, 15.1427}, {2504.3059, -1683.8514, 15.0385}, false)
        showHelp("SAVE_G2", false)
    end)
    afterScene(scene, 12000, function()
        fixedCamera({2494.8633, -1683.0514, 13.0207}, {2494.9395, -1684.0370, 13.1717}, false)
        showHelp("SAVE_G3", false)
    end)
    afterScene(scene, 18000, function() finishSaveTutorial(scene) end)
end

local function cleanup()
    clearTimers()
    clearAudio()
    releaseCamera()
    releaseSceneLeases(state.scene)
    state.scene = nil
    for id, recording in pairs(state.recordings) do
        stopRecording(id, recording.vehicle)
    end
    if state.text then
        textApi("clearMissionTexts")
    end
    state = {active = false, timers = {}, audio = nil, camera = nil, text = false, recordings = {}}
    setCameraTarget(localPlayer)
end

addEvent("sak:start", true)
addEventHandler("sak:start", resourceRoot, function(id, leader, headless, options)
    cleanup()
    state.active, state.id, state.leader, state.headless = true, id, leader, headless == true
    options = type(options) == "table" and options or {}
    state.transition, state.profile = options.transition == true, options.profile
    ensureText()
end)

local function reportTransitionCheckpoint(id, probeId, name, ok, reason, data)
    data = type(data) == "table" and data or {}
    local control = "accelerate"
    if state.transitionKeyProbe and state.transitionKeyProbe.probeId == probeId then
        control = state.transitionKeyProbe.control or control
        data.keyObserved = state.transitionKeyProbe.keyObserved == true
        state.transitionKeyProbe = nil
    end
    transitionLog(ok == true and "INPUT_PASS" or "INPUT_FAIL", {
        player = getPlayerName(localPlayer),
        probeId = probeId,
        checkpoint = name,
        control = control,
        reason = reason,
        raw = data.raw,
        processed = data.processed,
        displacement = data.displacement,
        samples = data.samples,
        keyObserved = data.keyObserved == true,
    })
    triggerServerEvent("sak:transitionCheckpointDone", resourceRoot, id, probeId, name, ok == true, reason, data)
end

local function beginTransitionCheckpoint(id, probeId, name, kind, leader, waitStartedAt)
    if not state.active or not state.transition or state.id ~= tonumber(id) or
        (kind ~= "vehicle_all" and kind ~= "foot_all") then
        return
    end
    waitStartedAt = tonumber(waitStartedAt) or getTickCount()
    local control = kind == "foot_all" and "forwards" or "accelerate"
    local vehicle = getPedOccupiedVehicle(localPlayer)
    local occupied = isElement(vehicle)
    local seat = occupied and getPedOccupiedVehicleSeat(localPlayer) or -1
    local streamed = occupied and isElementStreamedIn(vehicle)
    local readinessReason
    if kind == "foot_all" then
        if occupied then
            readinessReason = "joueur encore en vehicule"
        elseif isElementFrozen(localPlayer) or not getElementCollisionsEnabled(localPlayer) then
            readinessReason = "joueur a pied non pilotable"
        elseif not cameraTargetsLocalPlayer() then
            readinessReason = "camera non rendue au joueur"
        end
    elseif not occupied then
        readinessReason = "joueur hors vehicule"
    elseif seat ~= 0 then
        readinessReason = "joueur non conducteur"
    elseif not streamed then
        readinessReason = "vehicule non streame"
    elseif isElementFrozen(vehicle) or not getElementCollisionsEnabled(vehicle) then
        readinessReason = "vehicule non pilotable"
    elseif not cameraTargetsLocalPlayer() then
        readinessReason = "camera non rendue au joueur"
    end
    if readinessReason and getTickCount() - waitStartedAt < 5000 then
        return rememberTimer(setTimer(beginTransitionCheckpoint, 50, 1, id, probeId, name, kind, leader,
                                      waitStartedAt))
    end
    if readinessReason then
        return reportTransitionCheckpoint(id, probeId, name, false, readinessReason, {
            targetLocal = cameraTargetsLocalPlayer(),
            occupied = occupied,
            seat = seat,
            streamed = streamed,
        })
    end

    local spatialElement = kind == "foot_all" and localPlayer or vehicle
    local startX, startY, startZ = getElementPosition(spatialElement)
    local startedAt = getTickCount()
    local inputFrames, maxRaw, maxProcessed, maxDisplacement = 0, 0, 0, 0
    local keyProbe = {probeId = probeId, keyObserved = false, control = control}
    state.transitionKeyProbe = keyProbe
    outputDebugString(("[sweet-and-kendl-transition] INPUT_READY checkpoint=%s player=%s key=w control=%s"):format(
        tostring(name), getPlayerName(localPlayer), control))
    transitionLog("INPUT_READY", {player = getPlayerName(localPlayer), probeId = probeId, checkpoint = name,
                                   control = control, key = "w"})
    triggerServerEvent("sak:transitionInputReady", resourceRoot, id, probeId, name, control)
    local timer
    timer = rememberTimer(setTimer(function()
        if not state.active or state.id ~= tonumber(id) then
            if isTimer(timer) then
                killTimer(timer)
            end
            return
        end
        if (kind == "vehicle_all" and (not isElement(vehicle) or getPedOccupiedVehicle(localPlayer) ~= vehicle)) or
            (kind == "foot_all" and isPedInVehicle(localPlayer)) then
            killTimer(timer)
            return reportTransitionCheckpoint(id, probeId, name, false,
                                              kind == "foot_all" and "joueur remonte pendant la preuve" or
                                                  "occupation perdue pendant la preuve", {
                targetLocal = cameraTargetsLocalPlayer(),
                occupied = false,
                seat = -1,
                streamed = false,
                raw = maxRaw,
                processed = maxProcessed,
                displacement = maxDisplacement,
                samples = inputFrames,
            })
        end
        local raw = tonumber(getAnalogControlState(control, true)) or 0
        local processed = tonumber(getAnalogControlState(control, false)) or 0
        if getKeyState("w") == true then
            keyProbe.keyObserved = true
        end
        maxRaw, maxProcessed = math.max(maxRaw, raw), math.max(maxProcessed, processed)
        local x, y, z = getElementPosition(spatialElement)
        maxDisplacement = math.max(maxDisplacement, getDistanceBetweenPoints3D(x, y, z, startX, startY, startZ))
        if raw > 0.8 and processed > 0.8 then
            inputFrames = inputFrames + 1
        end
        if keyProbe.keyObserved and inputFrames >= 3 and maxDisplacement > 0.5 then
            killTimer(timer)
            local targetLocal = cameraTargetsLocalPlayer()
            local cameraReason
            if not targetLocal then
                cameraReason = "camera perdue pendant la preuve"
            end
            return reportTransitionCheckpoint(id, probeId, name, targetLocal, cameraReason, {
                targetLocal = targetLocal,
                raw = maxRaw,
                processed = maxProcessed,
                displacement = maxDisplacement,
                samples = inputFrames,
                occupied = kind == "vehicle_all",
                seat = kind == "vehicle_all" and getPedOccupiedVehicleSeat(localPlayer) or -1,
                streamed = kind ~= "vehicle_all" or isElementStreamedIn(vehicle),
            })
        end
        if getTickCount() - startedAt >= 60000 then
            killTimer(timer)
            reportTransitionCheckpoint(id, probeId, name, false, "entree physique W non observee", {
                targetLocal = cameraTargetsLocalPlayer(),
                raw = maxRaw,
                processed = maxProcessed,
                displacement = maxDisplacement,
                samples = inputFrames,
                occupied = kind == "vehicle_all",
                seat = kind == "vehicle_all" and getPedOccupiedVehicleSeat(localPlayer) or -1,
                streamed = kind ~= "vehicle_all" or isElementStreamedIn(vehicle),
            })
        end
    end, 50, 0))
end

addEvent("sak:transitionCheckpoint", true)
addEventHandler("sak:transitionCheckpoint", resourceRoot, function(id, probeId, name, kind, leader)
    beginTransitionCheckpoint(id, probeId, name, kind, leader, getTickCount())
end)

addEventHandler("onClientKey", root, function(button, pressed)
    if pressed == true and (button == "space" or button == "enter") and state.saveTutorial then
        local scene = state.saveTutorial
        finishSaveTutorial(scene)
        cancelEvent()
        return
    end
    if pressed == true and button == "w" and state.transitionKeyProbe then
        state.transitionKeyProbe.keyObserved = true
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
    if stage == "mount_bikes" then
        setCameraTarget(localPlayer)
    elseif stage == "ride1" then
        setCameraTarget(localPlayer)
    elseif stage == "ride2" then
        showText("INTRO2K", 3000)
        setCameraTarget(localPlayer)
    elseif stage == "failed" then
        if type(extra) == "table" and extra.textKey then showText(extra.textKey, 5000) end
        textApi("showMissionBigText", "M_FAIL", 5000, 1)
    end
end)

addEvent("sak:mountPrompt", true)
addEventHandler("sak:mountPrompt", resourceRoot, function()
    if state.active then showText("INTRO2E", 10000) end
end)

addEvent("sak:ambientAudio", true)
addEventHandler("sak:ambientAudio", resourceRoot, function(name, actors, token)
    if not state.active then return end
    local function acknowledge()
        if token ~= nil and state.active then
            triggerServerEvent("sak:ambientAudioDone", resourceRoot, state.id, token, name)
        end
    end
    if state.headless or type(SAK.audio[name]) ~= "table" then return acknowledge() end
    playQueue(SAK.audio[name], actors or {}, function()
        acknowledge()
    end)
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
addEventHandler("sak:cleanup", resourceRoot, function(id, report)
    cleanup()
    if report == true then
        setTimer(function()
            local targetLocal = cameraTargetsLocalPlayer()
            triggerServerEvent("sak:cleanupDone", resourceRoot, id, targetLocal,
                               targetLocal and nil or "camera target", {targetLocal = targetLocal})
        end, 50, 1)
    end
end)
addEventHandler("onClientResourceStop", resourceRoot, cleanup)
