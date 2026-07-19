local state = {
    active = false,
    stage = nil,
    vehicle = nil,
    actors = {},
    cutscene = nil,
    audio = nil,
    missionTextReady = false,
    navigation = nil,
    blip = nil,
    marker = nil,
    entryTimer = nil,
    arrivalTimer = nil,
    checkpointReached = false,
    greenwoodPolicyLogged = false,
}

local SCM_DESTINATION_BLIP_COLOR = {226, 192, 99, 255}
local SCM_FRIENDLY_BLIP_COLOR = {0, 0, 255, 255}

local function callMissionTextApi(name, ...)
    local api = _G[name]
    if type(api) ~= "function" then
        outputDebugString(("[drive-thru] Native mission-text API unavailable: %s"):format(name), 1)
        return false
    end
    local ok, result = pcall(api, ...)
    return ok and result == true
end

local function ensureMissionText()
    if state.missionTextReady then
        return true
    end
    state.missionTextReady = callMissionTextApi("acquireMissionText", "SWEET3")
    return state.missionTextReady
end

local function printMissionText(key, duration)
    return ensureMissionText() and callMissionTextApi("showMissionText", key, duration, 1)
end

local function destroyNavigation()
    for _, element in ipairs({state.blip, state.marker}) do
        if isElement(element) then
            destroyElement(element)
        end
    end
    state.blip = nil
    state.marker = nil
    state.navigation = nil
end

local function showVehicleNavigation()
    destroyNavigation()
    if not isElement(state.vehicle) then
        return
    end
    state.blip = createBlipAttachedTo(state.vehicle, 0, 2, unpack(SCM_FRIENDLY_BLIP_COLOR))
    if isElement(state.blip) then
        setElementDimension(state.blip, DRIVETHRU.dimension)
    end
    state.navigation = "vehicle"
end

local function showDestinationNavigation()
    destroyNavigation()
    local destination = DRIVETHRU.destination
    state.blip = createBlip(destination.x, destination.y, destination.z, 0, 2, unpack(SCM_DESTINATION_BLIP_COLOR))
    if isElement(state.blip) then
        setElementDimension(state.blip, DRIVETHRU.dimension)
    end
    if type(renderScriptImportantArea) ~= "function" then
        state.marker = createMarker(destination.x, destination.y, destination.z - 1.0, "cylinder",
                                    math.max(destination.radiusX, destination.radiusY), 255, 0, 0, 180)
        if isElement(state.marker) then
            setElementDimension(state.marker, DRIVETHRU.dimension)
        end
    end
    state.navigation = "destination"
end

local function applyActorPolicies(ped)
    if not isElement(ped) or getElementType(ped) ~= "ped" or getElementData(ped, DRIVETHRU.missionActorData) ~= true then
        return false
    end
    if type(setPedMissionActor) ~= "function" or type(setPedStoryProtected) ~= "function" then
        return false
    end
    return setPedMissionActor(ped, true) == true and setPedStoryProtected(ped, true) == true
end

local function applyGreenwoodPolicies(vehicle)
    if not isElement(vehicle) or getElementData(vehicle, DRIVETHRU.vehicleData) ~= true then
        return false
    end
    if type(setVehicleTyresCanBurst) ~= "function" or type(setVehicleDoorLockMode) ~= "function" then
        return false
    end
    local tyres = type(getVehicleTyresCanBurst) == "function" and getVehicleTyresCanBurst(vehicle) == false or
                       setVehicleTyresCanBurst(vehicle, false)
    local doors = type(getVehicleDoorLockMode) == "function" and
                      getVehicleDoorLockMode(vehicle) == DRIVETHRU.vehicle.doorLockMode or
                      setVehicleDoorLockMode(vehicle, DRIVETHRU.vehicle.doorLockMode)
    if tyres == true and doors == true and not state.greenwoodPolicyLogged then
        state.greenwoodPolicyLogged = true
        local x, y, z = getElementPosition(vehicle)
        local colourOk, primaryR, primaryG, primaryB, secondaryR, secondaryG, secondaryB = pcall(getVehicleColor, vehicle, true)
        outputDebugString(("[drive-thru] Greenwood streamed position=(%.3f, %.3f, %.3f) colours=%s plate=%s tyresCanBurst=false doorLockMode=%d"):format(
                              x, y, z,
                              colourOk and ("(%d,%d,%d)/(%d,%d,%d)"):format(primaryR, primaryG, primaryB, secondaryR, secondaryG,
                                                                            secondaryB) or "unavailable",
                              tostring(getVehiclePlateText(vehicle)), DRIVETHRU.vehicle.doorLockMode))
    end
    return tyres == true and doors == true
end

local function stopSpeaker(audio)
    local speaker = audio and audio.speaker
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

local function clearAudio(reason)
    local audio = state.audio
    if not audio then
        return
    end
    state.audio = nil
    for _, timer in ipairs({audio.loadTimer, audio.finishTimer, audio.guardTimer}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    stopSpeaker(audio)
    if audio.handle and type(releaseMissionAudio) == "function" then
        pcall(releaseMissionAudio, audio.handle)
    end
    outputDebugString(("[drive-thru] Audio #%d cleared: %s"):format(tonumber(audio.id) or -1, tostring(reason or "cleanup")))
end

local function hasFileCutsceneLease(scene)
    return scene and scene.token and type(isFileCutsceneLeaseActive) == "function" and isFileCutsceneLeaseActive(scene.token)
end

local function clearFileCutscene(reason, preserveFade)
    local scene = state.cutscene
    if not scene then
        return true
    end
    for _, timer in ipairs({scene.appearanceTimer, scene.loadTimer, scene.finishTimer, scene.fadeTimer}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    local released = true
    if scene.token then
        if type(releaseFileCutscene) ~= "function" then
            released = false
        else
            local ok, result = pcall(releaseFileCutscene, scene.token, preserveFade == true)
            released = ok and result == true
        end
    end
    outputDebugString(("[drive-thru] SWEET2A #%d cleared released=%s reason=%s"):format(
                          tonumber(scene.id) or -1, tostring(released), tostring(reason or "cleanup")))
    state.cutscene = nil
    return released
end

local function getCJReadiness()
    if getElementModel(localPlayer) ~= DRIVETHRU.cj.model or getElementAlpha(localPlayer) ~= 255 then
        return false, ("model=%d alpha=%d"):format(getElementModel(localPlayer), getElementAlpha(localPlayer))
    end
    for _, expected in ipairs(DRIVETHRU.cj.clothes) do
        local texture, model = getPedClothes(localPlayer, expected.type)
        if type(texture) ~= "string" or type(model) ~= "string" or texture:lower() ~= expected.texture or
            model:lower() ~= expected.model then
            return false, ("slot=%d %s/%s"):format(expected.type, tostring(texture), tostring(model))
        end
    end
    local x, y, z = getElementBonePosition(localPlayer, 2)
    return type(x) == "number" and type(y) == "number" and type(z) == "number", "CJ model, clothes and bone ready"
end

local function requestNativeCutscene(scene)
    local ok, token = pcall(requestFileCutscene, DRIVETHRU.cutscene.name)
    if not ok or not token then
        triggerServerEvent("drivethru:cutsceneReady", resourceRoot, scene.id, "request_refused", tostring(token))
        return
    end
    scene.token = token
    scene.loadRequestedAt = getTickCount()
    scene.loadTimer = setTimer(function()
        if state.cutscene ~= scene then
            return
        end
        if not hasFileCutsceneLease(scene) then
            killTimer(scene.loadTimer)
            return triggerServerEvent("drivethru:cutsceneReady", resourceRoot, scene.id, "lease_lost")
        end
        local queried, loaded = pcall(isFileCutsceneLoaded, scene.token)
        if not queried then
            killTimer(scene.loadTimer)
            return triggerServerEvent("drivethru:cutsceneReady", resourceRoot, scene.id, "load_query_failed", tostring(loaded))
        end
        if loaded then
            killTimer(scene.loadTimer)
            triggerServerEvent("drivethru:cutsceneReady", resourceRoot, scene.id, "ready",
                               ("load=%dms"):format(getTickCount() - scene.loadRequestedAt))
        elseif getTickCount() - scene.loadRequestedAt >= DRIVETHRU.cutscene.loadTimeout then
            killTimer(scene.loadTimer)
            triggerServerEvent("drivethru:cutsceneReady", resourceRoot, scene.id, "load_timeout")
        end
    end, DRIVETHRU.cutscene.pollInterval, 0)
end

local function clearClientState(reason)
    if isTimer(state.entryTimer) then
        killTimer(state.entryTimer)
    end
    state.entryTimer = nil
    clearAudio(reason)
    clearFileCutscene(reason, false)
    destroyNavigation()
    for _, ped in pairs(state.actors) do
        if isElement(ped) then
            if type(setPedMissionActor) == "function" then
                pcall(setPedMissionActor, ped, false)
            end
            if type(setPedStoryProtected) == "function" then
                pcall(setPedStoryProtected, ped, false)
            end
        end
    end
    if state.missionTextReady then
        callMissionTextApi("clearMissionTexts")
        callMissionTextApi("releaseMissionText")
    end
    state.active = false
    state.stage = nil
    state.vehicle = nil
    state.actors = {}
    state.missionTextReady = false
    state.checkpointReached = false
    state.greenwoodPolicyLogged = false
end

addEvent("drivethru:start", true)
addEventHandler("drivethru:start", resourceRoot, function(vehicle)
    clearClientState("replaced")
    state.active = true
    state.stage = "preparing"
    state.vehicle = vehicle
    ensureMissionText()
end)

addEvent("drivethru:cutscenePrepare", true)
addEventHandler("drivethru:cutscenePrepare", resourceRoot, function(sceneId)
    if source ~= resourceRoot or not state.active then
        return
    end
    local required = {"requestFileCutscene", "releaseFileCutscene", "isFileCutsceneLeaseActive", "isFileCutsceneLoaded",
                      "startFileCutscene", "fadeFileCutscene", "isFileCutsceneFading", "isFileCutsceneFinished",
                      "isFileCutsceneSkipInputPressed", "wasFileCutsceneSkipped", "skipFileCutscene"}
    for _, name in ipairs(required) do
        if type(_G[name]) ~= "function" then
            return triggerServerEvent("drivethru:cutsceneReady", resourceRoot, sceneId, "api_unavailable", name)
        end
    end
    if not ensureMissionText() then
        return triggerServerEvent("drivethru:cutsceneReady", resourceRoot, sceneId, "mission_text_unavailable", "SWEET3")
    end
    local scene = {id = sceneId, requestedAt = getTickCount(), appearanceStableSamples = 0}
    state.cutscene = scene
    state.stage = "cutscene"
    scene.appearanceTimer = setTimer(function()
        if state.cutscene ~= scene then
            return
        end
        local ready, details = getCJReadiness()
        scene.appearanceStableSamples = ready and scene.appearanceStableSamples + 1 or 0
        if scene.appearanceStableSamples >= DRIVETHRU.cutscene.appearanceStableSamples then
            killTimer(scene.appearanceTimer)
            requestNativeCutscene(scene)
        elseif getTickCount() - scene.requestedAt >= DRIVETHRU.cutscene.appearanceTimeout then
            killTimer(scene.appearanceTimer)
            triggerServerEvent("drivethru:cutsceneReady", resourceRoot, scene.id, "cj_appearance_timeout", details)
        end
    end, DRIVETHRU.cutscene.pollInterval, 0)
end)

addEvent("drivethru:cutsceneStart", true)
addEventHandler("drivethru:cutsceneStart", resourceRoot, function(sceneId)
    local scene = state.cutscene
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) or not hasFileCutsceneLease(scene) then
        return
    end
    local startedOk, started = pcall(startFileCutscene, scene.token)
    if not startedOk or started ~= true then
        return triggerServerEvent("drivethru:cutsceneFinished", resourceRoot, scene.id, "start_refused")
    end
    scene.startedAt = getTickCount()
    local fadeOk, faded = pcall(fadeFileCutscene, scene.token, true, DRIVETHRU.cutscene.fadeInDuration, 0, 0, 0)
    if not fadeOk or faded ~= true then
        return triggerServerEvent("drivethru:cutsceneFinished", resourceRoot, scene.id, "fade_in_refused")
    end
    outputDebugString("[drive-thru] SWEET2A native playback started")
    scene.finishTimer = setTimer(function()
        if state.cutscene ~= scene then
            return
        end
        local queried, finished = pcall(isFileCutsceneFinished, scene.token)
        if not queried then
            killTimer(scene.finishTimer)
            return triggerServerEvent("drivethru:cutsceneFinished", resourceRoot, scene.id, "finish_query_failed")
        end
        if finished then
            killTimer(scene.finishTimer)
            local fadeOutOk, fadeOut = pcall(fadeFileCutscene, scene.token, false, 0, 0, 0, 0)
            if not fadeOutOk or fadeOut ~= true then
                return triggerServerEvent("drivethru:cutsceneFinished", resourceRoot, scene.id, "fade_out_refused")
            end
            scene.fadeTimer = setTimer(function()
                if state.cutscene ~= scene then
                    return
                end
                local fadeQueried, fading = pcall(isFileCutsceneFading, scene.token)
                if fadeQueried and not fading then
                    killTimer(scene.fadeTimer)
                    local skipped = false
                    local skippedOk, skippedResult = pcall(wasFileCutsceneSkipped, scene.token)
                    if skippedOk then
                        skipped = skippedResult == true
                    end
                    triggerServerEvent("drivethru:cutsceneFinished", resourceRoot, scene.id, "finished", skipped,
                                       getTickCount() - scene.startedAt)
                end
            end, DRIVETHRU.cutscene.pollInterval, 0)
        elseif getTickCount() - scene.startedAt >= DRIVETHRU.cutscene.finishTimeout then
            killTimer(scene.finishTimer)
            triggerServerEvent("drivethru:cutsceneFinished", resourceRoot, scene.id, "finish_timeout")
        end
    end, DRIVETHRU.cutscene.pollInterval, 0)
end)

addEvent("drivethru:cutsceneSkip", true)
addEventHandler("drivethru:cutsceneSkip", resourceRoot, function(sceneId)
    local scene = state.cutscene
    if source == resourceRoot and scene and scene.id == tonumber(sceneId) and hasFileCutsceneLease(scene) then
        pcall(skipFileCutscene, scene.token)
    end
end)

addEvent("drivethru:cutsceneRelease", true)
addEventHandler("drivethru:cutsceneRelease", resourceRoot, function(sceneId)
    local scene = state.cutscene
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) then
        return
    end
    local released = clearFileCutscene("completed", true)
    triggerServerEvent("drivethru:cutsceneReleased", resourceRoot, sceneId, released and "released" or "release_failed")
end)

local function beginActorEntry(entities)
    state.vehicle = entities.vehicle
    state.actors = {smoke = entities.smoke, sweet = entities.sweet, ryder = entities.ryder}
    for _, ped in pairs(state.actors) do
        applyActorPolicies(ped)
    end
    applyGreenwoodPolicies(state.vehicle)
    if type(enginePreloadWorldAreaInDirection) == "function" then
        pcall(enginePreloadWorldAreaInDirection, Vector3(DRIVETHRU.cj.world.x, DRIVETHRU.cj.world.y, DRIVETHRU.cj.world.scriptZ),
              DRIVETHRU.cj.world.heading)
    end
    setCameraTarget(localPlayer)
    fadeCamera(true, 1.0)
    showVehicleNavigation()

    local accepted = {}
    local startedAt = getTickCount()
    local vehicleStableSamples = 0
    local previousVehiclePosition = nil
    local lastReadiness = "not sampled"
    state.entryTimer = setTimer(function()
        if not state.active or state.stage ~= "actor_entry" then
            return
        end
        local vehicleReady = isElement(state.vehicle) and isElementStreamedIn(state.vehicle) and isElementSyncer(state.vehicle) and
                                 applyGreenwoodPolicies(state.vehicle)
        local positionReady = false
        if vehicleReady then
            local x, y, z = getElementPosition(state.vehicle)
            local expected = DRIVETHRU.vehicle.position
            local nearExpected = math.abs(x - expected.x) <= 1.0 and math.abs(y - expected.y) <= 1.0 and math.abs(z - expected.z) <= 1.0
            local stable = previousVehiclePosition and getDistanceBetweenPoints3D(x, y, z, previousVehiclePosition.x,
                                                                                  previousVehiclePosition.y,
                                                                                  previousVehiclePosition.z) <= 0.05
            vehicleStableSamples = nearExpected and stable and vehicleStableSamples + 1 or 0
            previousVehiclePosition = {x = x, y = y, z = z}
            positionReady = vehicleStableSamples >= DRIVETHRU.worldStableSamples
            lastReadiness = ("vehicle stream=true syncer=true position=(%.3f,%.3f,%.3f) stable=%d/%d"):format(
                                x, y, z, vehicleStableSamples, DRIVETHRU.worldStableSamples)
        else
            vehicleStableSamples = 0
            previousVehiclePosition = nil
            lastReadiness = ("vehicle element=%s stream=%s syncer=%s"):format(tostring(isElement(state.vehicle)),
                                                                               tostring(isElement(state.vehicle) and
                                                                                          isElementStreamedIn(state.vehicle)),
                                                                               tostring(isElement(state.vehicle) and
                                                                                          isElementSyncer(state.vehicle)))
        end

        local actorsReady = true
        for _, name in ipairs({"smoke", "sweet", "ryder"}) do
            local ped = state.actors[name]
            if not isElement(ped) or not isElementStreamedIn(ped) or not isElementSyncer(ped) or not applyActorPolicies(ped) then
                actorsReady = false
                lastReadiness = lastReadiness .. (" %s(element=%s stream=%s syncer=%s)"):format(
                                    name, tostring(isElement(ped)), tostring(isElement(ped) and isElementStreamedIn(ped)),
                                    tostring(isElement(ped) and isElementSyncer(ped)))
            end
        end

        if positionReady and actorsReady and type(setPedEnterVehicle) == "function" then
            for _, name in ipairs({"smoke", "sweet", "ryder"}) do
                if not accepted[name] then
                    local ped = state.actors[name]
                    local profile = DRIVETHRU.actors[name]
                    if setPedEnterVehicle(ped, state.vehicle, profile.seat) then
                        accepted[name] = true
                        outputDebugString(("[drive-thru] %s passenger task accepted for MTA seat %d after world streaming barrier"):format(
                                              name, profile.seat))
                    end
                end
            end
        end
        if accepted.smoke and accepted.sweet and accepted.ryder then
            killTimer(state.entryTimer)
            state.entryTimer = nil
            triggerServerEvent("drivethru:actorTasksReady", resourceRoot, "accepted")
        elseif getTickCount() - startedAt >= DRIVETHRU.worldStreamingTimeout then
            killTimer(state.entryTimer)
            state.entryTimer = nil
            triggerServerEvent("drivethru:actorTasksReady", resourceRoot, "timeout",
                               lastReadiness .. (" accepted=%s/%s/%s"):format(tostring(accepted.smoke == true),
                                                                              tostring(accepted.sweet == true),
                                                                              tostring(accepted.ryder == true)))
        end
    end, 100, 0)
end

addEvent("drivethru:stage", true)
addEventHandler("drivethru:stage", resourceRoot, function(stage, entities)
    if source ~= resourceRoot or not state.active then
        return
    end
    state.stage = stage
    if type(entities) == "table" then
        state.vehicle = entities.vehicle or state.vehicle
    end
    if stage == "actor_entry" then
        beginActorEntry(entities)
    elseif stage == "enter_car" then
        printMissionText("TWAR2_A", 6000)
        showVehicleNavigation()
    elseif stage == "drive" then
        showDestinationNavigation()
        printMissionText("TWAR2_C", 6000)
        if getPedOccupiedVehicle(localPlayer) == state.vehicle then
            setRadioChannel(DRIVETHRU.vehicle.bounceRadioChannel)
        end
    elseif stage == "return_car" then
        showVehicleNavigation()
        printMissionText("TW2_X", 3000)
    end
end)

local function resolveSpeaker(name)
    if name == "leader" then
        return localPlayer
    end
    return state.actors[name]
end

addEvent("drivethru:audioPrepare", true)
addEventHandler("drivethru:audioPrepare", resourceRoot, function(audioId, profile)
    if source ~= resourceRoot or not state.active or type(profile) ~= "table" or type(profile.event) ~= "number" then
        return
    end
    clearAudio("replaced")
    if type(requestMissionAudio) ~= "function" or type(isMissionAudioLoaded) ~= "function" or type(playMissionAudio) ~= "function" or
        type(isMissionAudioFinished) ~= "function" or type(releaseMissionAudio) ~= "function" then
        return triggerServerEvent("drivethru:audioReady", resourceRoot, audioId, "api_unavailable")
    end
    local requested, handle = pcall(requestMissionAudio, profile.event)
    if not requested or not handle then
        return triggerServerEvent("drivethru:audioReady", resourceRoot, audioId, "request_refused", tostring(handle))
    end
    local audio = {id = audioId, profile = profile, handle = handle, requestedAt = getTickCount()}
    state.audio = audio
    audio.loadTimer = setTimer(function()
        if state.audio ~= audio then
            return
        end
        local queried, loaded = pcall(isMissionAudioLoaded, audio.handle)
        if queried and loaded == true then
            killTimer(audio.loadTimer)
            triggerServerEvent("drivethru:audioReady", resourceRoot, audio.id, "ready",
                               ("event=%d load=%dms"):format(profile.event, getTickCount() - audio.requestedAt))
        elseif getTickCount() - audio.requestedAt >= DRIVETHRU.audio.loadTimeout then
            killTimer(audio.loadTimer)
            triggerServerEvent("drivethru:audioReady", resourceRoot, audio.id, "load_timeout", tostring(profile.event))
        end
    end, DRIVETHRU.audio.pollInterval, 0)
end)

addEvent("drivethru:audioStart", true)
addEventHandler("drivethru:audioStart", resourceRoot, function(audioId)
    local audio = state.audio
    if source ~= resourceRoot or not audio or audio.id ~= tonumber(audioId) or audio.startedAt then
        return
    end
    local playedOk, played = pcall(playMissionAudio, audio.handle)
    if not playedOk or played ~= true then
        return triggerServerEvent("drivethru:audioFinished", resourceRoot, audio.id, "play_refused", tostring(played))
    end
    audio.startedAt = getTickCount()
    printMissionText(audio.profile.key, audio.profile.duration or 4000)
    audio.speaker = resolveSpeaker(audio.profile.speaker)
    if isElement(audio.speaker) then
        if type(setPedScriptedSpeechMuted) == "function" then
            pcall(setPedScriptedSpeechMuted, audio.speaker, true)
        end
        if type(setPedFacialTalk) == "function" then
            pcall(setPedFacialTalk, audio.speaker, 3000)
        end
    end
    audio.finishTimer = setTimer(function()
        if state.audio ~= audio then
            return
        end
        local queried, finished = pcall(isMissionAudioFinished, audio.handle)
        if queried and finished == true then
            local elapsed = getTickCount() - audio.startedAt
            killTimer(audio.finishTimer)
            stopSpeaker(audio)
            pcall(releaseMissionAudio, audio.handle)
            audio.handle = nil
            state.audio = nil
            triggerServerEvent("drivethru:audioFinished", resourceRoot, audio.id, "finished",
                               ("event=%d play=%dms"):format(audio.profile.event, elapsed))
        elseif getTickCount() - audio.startedAt >= DRIVETHRU.audio.finishTimeout then
            killTimer(audio.finishTimer)
            triggerServerEvent("drivethru:audioFinished", resourceRoot, audio.id, "finish_timeout", tostring(audio.profile.event))
        end
    end, DRIVETHRU.audio.pollInterval, 0)
end)

addEvent("drivethru:checkpointReached", true)
addEventHandler("drivethru:checkpointReached", resourceRoot, function()
    if source ~= resourceRoot or not state.active then
        return
    end
    state.stage = "checkpoint"
    state.checkpointReached = true
    destroyNavigation()
    outputChatBox("Drive-Thru checkpoint valide: gate Cluckin' Bell + conducteur + quatre roues.", 80, 220, 120)
    outputChatBox("Reste ici pour observer, puis utilise /drivethruabort.", 220, 220, 220)
end)

addEvent("drivethru:failed", true)
addEventHandler("drivethru:failed", resourceRoot, function(textKey, reason)
    if source ~= resourceRoot or not state.active then
        return
    end
    state.stage = "failed"
    if type(textKey) == "string" then
        printMissionText(textKey, 2000)
    end
    callMissionTextApi("showMissionBigText", "M_FAIL", 5000, 1)
    outputDebugString("[drive-thru] Failure shown: " .. tostring(reason), 1)
end)

addEvent("drivethru:stop", true)
addEventHandler("drivethru:stop", resourceRoot, function(reason)
    if source == resourceRoot then
        clearClientState(reason)
        fadeCamera(true, 0.5)
    end
end)

addEventHandler("onClientElementStreamIn", root, function()
    if not state.active then
        return
    end
    if getElementData(source, DRIVETHRU.missionActorData) == true then
        applyActorPolicies(source)
    elseif getElementData(source, DRIVETHRU.vehicleData) == true then
        applyGreenwoodPolicies(source)
    end
end)

addEventHandler("onClientVehicleEnter", root, function(player, seat)
    if state.active and source == state.vehicle and player == localPlayer and seat == 0 then
        setRadioChannel(DRIVETHRU.vehicle.bounceRadioChannel)
    end
end)

addEventHandler("onClientPreRender", root, function()
    if not state.active then
        return
    end
    local scene = state.cutscene
    if scene and scene.startedAt and not scene.skipRequested and hasFileCutsceneLease(scene) then
        local ok, pressed = pcall(isFileCutsceneSkipInputPressed, scene.token)
        if ok and pressed == true then
            scene.skipRequested = true
            triggerServerEvent("drivethru:cutsceneSkipRequest", resourceRoot, scene.id)
        end
    end
    if state.navigation == "destination" and type(renderScriptImportantArea) == "function" then
        local destination = DRIVETHRU.destination
        renderScriptImportantArea(Vector3(destination.x, destination.y, destination.z), destination.radiusX, destination.radiusY, 1)
    end
end)

state.arrivalTimer = setTimer(function()
    if not state.active or state.stage ~= "drive" or not isElement(state.vehicle) or getPedOccupiedVehicle(localPlayer) ~= state.vehicle or
        getPedOccupiedVehicleSeat(localPlayer) ~= 0 then
        return
    end
    local x, y, z = getElementPosition(state.vehicle)
    local destination = DRIVETHRU.destination
    if math.abs(x - destination.x) <= destination.radiusX and math.abs(y - destination.y) <= destination.radiusY and
        math.abs(z - destination.z) <= destination.radiusZ and type(isVehicleOnAllWheels) == "function" then
        local ok, onAllWheels = pcall(isVehicleOnAllWheels, state.vehicle)
        if ok and onAllWheels == true then
            triggerServerEvent("drivethru:arrivalReport", resourceRoot, true)
        end
    end
end, DRIVETHRU.arrivalReportInterval, 0)

addEventHandler("onClientResourceStop", resourceRoot, function()
    clearClientState("resource_stop")
end)
