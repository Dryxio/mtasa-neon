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
    restaurantCamera = nil,
    restaurantRebuildTimer = nil,
    pursuitBlip = nil,
    pursuitPedBlips = {},
    pursuitTimers = {},
    footCombat = false,
    supportChatAccepted = false,
}

local SCM_DESTINATION_BLIP_COLOR = {226, 192, 99, 255}
local SCM_FRIENDLY_BLIP_COLOR = {0, 0, 255, 255}
local SCM_THREAT_BLIP_COLOR = {255, 0, 0, 255}

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

local function printMissionHelp(key, permanent)
    return ensureMissionText() and callMissionTextApi("showMissionHelp", key, permanent == true)
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
    if type(setPedMissionActor) ~= "function" then
        return false
    end
    if setPedMissionActor(ped, true) ~= true then
        return false
    end
    local role = getElementData(ped, DRIVETHRU.actorRoleData)
    if role == "ballas_driver" or role == "ballas_passenger" then
        if type(setPedSuffersCriticalHits) ~= "function" or type(getPedSuffersCriticalHits) ~= "function" or
            type(setPedWeaponAccuracy) ~= "function" then
            return false
        end
        local profile = role == "ballas_driver" and DRIVETHRU.restaurant.ballasDriver or DRIVETHRU.chase.ballasPassenger
        return setPedSuffersCriticalHits(ped, false) == true and getPedSuffersCriticalHits(ped) == false and
                   setPedWeaponAccuracy(ped, profile.accuracy) == true
    elseif role == "grove_support" then
        -- The isolated mission dimension has no ambient attackers. Keep the
        -- support actors at GTA's mission-ped classification without adding
        -- protagonist protection flags they did not receive in SWEET3.
        return true
    end
    return type(setPedStoryProtected) == "function" and setPedStoryProtected(ped, true) == true
end

local function applyGreenwoodPolicies(vehicle)
    if not isElement(vehicle) then
        return false
    end
    local role = getElementData(vehicle, DRIVETHRU.vehicleRoleData)
    local profile = role == "voodoo" and DRIVETHRU.restaurant.voodoo or
                        (role == "greenwood" and DRIVETHRU.restaurant.greenwood or DRIVETHRU.vehicle)
    if type(setVehicleTyresCanBurst) ~= "function" or type(setVehicleDoorLockMode) ~= "function" then
        return false
    end
    local tyres = type(getVehicleTyresCanBurst) == "function" and getVehicleTyresCanBurst(vehicle) == profile.tyresCanBurst or
                       setVehicleTyresCanBurst(vehicle, profile.tyresCanBurst)
    local doors = type(getVehicleDoorLockMode) == "function" and
                      getVehicleDoorLockMode(vehicle) == profile.doorLockMode or setVehicleDoorLockMode(vehicle, profile.doorLockMode)
    local proofs = true
    if profile.proofs then
        proofs = type(setVehiclePhysicalProofs) == "function" and
                     setVehiclePhysicalProofs(vehicle, profile.proofs.bullet, profile.proofs.fire, profile.proofs.explosion,
                                              profile.proofs.collision, profile.proofs.melee) == true
    end
    if role ~= "voodoo" and tyres == true and doors == true and proofs == true and not state.greenwoodPolicyLogged then
        state.greenwoodPolicyLogged = true
        local x, y, z = getElementPosition(vehicle)
        local colourOk, primaryR, primaryG, primaryB, secondaryR, secondaryG, secondaryB = pcall(getVehicleColor, vehicle, true)
        outputDebugString(("[drive-thru] Greenwood streamed position=(%.3f, %.3f, %.3f) colours=%s plate=%s tyresCanBurst=false doorLockMode=%d"):format(
                              x, y, z,
                              colourOk and ("(%d,%d,%d)/(%d,%d,%d)"):format(primaryR, primaryG, primaryB, secondaryR, secondaryG,
                                                                            secondaryB) or "unavailable",
                              tostring(getVehiclePlateText(vehicle)), profile.doorLockMode))
    end
    return tyres == true and doors == true and proofs == true
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
    outputDebugString(("[drive-thru] %s #%d cleared released=%s reason=%s"):format(
                          tostring(scene.name or "cutscene"), tonumber(scene.id) or -1, tostring(released), tostring(reason or "cleanup")))
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
    local ok, token = pcall(requestFileCutscene, scene.name)
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

local function clearRestaurantCamera(reason, preserveFade)
    local camera = state.restaurantCamera
    if not camera then
        return true
    end
    for _, timer in ipairs({camera.fadeTimer, camera.leaseTimer}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    local released = true
    if camera.token and type(releaseScriptCamera) == "function" then
        local ok, result = pcall(releaseScriptCamera, camera.token, preserveFade == true)
        released = ok and result == true
    end
    state.restaurantCamera = nil
    outputDebugString(("[drive-thru] Restaurant camera cleared released=%s reason=%s"):format(tostring(released),
                                                                                              tostring(reason or "cleanup")))
    return released
end

local function clearClientState(reason)
    if isTimer(state.entryTimer) then
        killTimer(state.entryTimer)
    end
    state.entryTimer = nil
    if isTimer(state.restaurantRebuildTimer) then
        killTimer(state.restaurantRebuildTimer)
    end
    state.restaurantRebuildTimer = nil
    for _, timer in ipairs(state.pursuitTimers) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    state.pursuitTimers = {}
    clearAudio(reason)
    clearFileCutscene(reason, false)
    clearRestaurantCamera(reason, false)
    destroyNavigation()
    if isElement(state.pursuitBlip) then
        destroyElement(state.pursuitBlip)
    end
    state.pursuitBlip = nil
    for _, blip in ipairs(state.pursuitPedBlips) do
        if isElement(blip) then
            destroyElement(blip)
        end
    end
    state.pursuitPedBlips = {}
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
        callMissionTextApi("clearMissionHelp")
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
    state.footCombat = false
    state.supportChatAccepted = false
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
addEventHandler("drivethru:cutscenePrepare", resourceRoot, function(sceneId, name, requiresAppearance)
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
    local scene = {id = sceneId, name = name, requestedAt = getTickCount(), appearanceStableSamples = 0}
    state.cutscene = scene
    state.stage = "cutscene"
    if requiresAppearance ~= true then
        requestNativeCutscene(scene)
        return
    end
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
    outputDebugString(("[drive-thru] %s native playback started"):format(scene.name))
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
        elseif getTickCount() - scene.startedAt >=
            (DRIVETHRU.cutscene.finishTimeoutByName[scene.name] or DRIVETHRU.cutscene.finishTimeout) then
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
    state.stage = "restaurant_camera"
    state.checkpointReached = true
    destroyNavigation()
    clearAudio("restaurant_transition")

    local required = {"acquireScriptCamera", "releaseScriptCamera", "isScriptCameraLeaseActive", "resetScriptCamera",
                      "setScriptCameraWidescreen", "setScriptCameraFixed", "fadeScriptCamera", "isScriptCameraFading"}
    for _, name in ipairs(required) do
        if type(_G[name]) ~= "function" then
            return triggerServerEvent("drivethru:restaurantCameraReady", resourceRoot, "api_unavailable", name)
        end
    end
    clearRestaurantCamera("replaced", false)
    local acquired, token = pcall(acquireScriptCamera, true)
    if not acquired or not token then
        return triggerServerEvent("drivethru:restaurantCameraReady", resourceRoot, "camera_acquire_refused", tostring(token))
    end
    local camera = {token = token, startedAt = getTickCount()}
    state.restaurantCamera = camera
    local profile = DRIVETHRU.restaurant.camera
    local ready = resetScriptCamera(token) and setScriptCameraWidescreen(token, true) and
                      setScriptCameraFixed(token, Vector3(profile.position.x, profile.position.y, profile.position.z),
                                           Vector3(profile.target.x, profile.target.y, profile.target.z), Vector3(0, 0, 0), true) and
                      fadeScriptCamera(token, false, profile.fadeOutDuration, 0, 0, 0)
    if not ready then
        clearRestaurantCamera("setup_refused", false)
        return triggerServerEvent("drivethru:restaurantCameraReady", resourceRoot, "camera_setup_refused")
    end
    camera.leaseTimer = setTimer(function()
        if state.restaurantCamera == camera and not isScriptCameraLeaseActive(camera.token) then
            clearRestaurantCamera("lease_lost", false)
            triggerServerEvent("drivethru:restaurantCameraReady", resourceRoot, "lease_lost")
        end
    end, 100, 0)
    camera.fadeTimer = setTimer(function()
        if state.restaurantCamera ~= camera or not isScriptCameraLeaseActive(camera.token) then
            return
        end
        local ok, fading = pcall(isScriptCameraFading, camera.token)
        local minimumElapsed = math.floor(profile.fadeOutDuration * 1000 + 0.5)
        if not ok then
            killTimer(camera.fadeTimer)
            camera.fadeTimer = nil
            triggerServerEvent("drivethru:restaurantCameraReady", resourceRoot, "fade_query_failed", tostring(fading))
        elseif not fading and getTickCount() - camera.startedAt >= minimumElapsed then
            killTimer(camera.fadeTimer)
            camera.fadeTimer = nil
            triggerServerEvent("drivethru:restaurantCameraReady", resourceRoot, "ready",
                               ("fade=%dms"):format(getTickCount() - camera.startedAt))
        end
    end, 50, 0)
end)

addEvent("drivethru:restaurantCameraRelease", true)
addEventHandler("drivethru:restaurantCameraRelease", resourceRoot, function()
    if source ~= resourceRoot or state.stage ~= "restaurant_camera" then
        return
    end
    local preload = DRIVETHRU.restaurant.preload
    if type(enginePreloadWorldAreaInDirection) ~= "function" then
        return triggerServerEvent("drivethru:restaurantCameraReleased", resourceRoot, "api_unavailable",
                                  "enginePreloadWorldAreaInDirection")
    end
    local preloaded, preloadResult = pcall(enginePreloadWorldAreaInDirection, Vector3(preload.x, preload.y, preload.z), preload.heading)
    if not preloaded or preloadResult == false then
        return triggerServerEvent("drivethru:restaurantCameraReleased", resourceRoot, "preload_refused", tostring(preloadResult))
    end
    local released = clearRestaurantCamera("handoff_to_SWEET2B", true)
    triggerServerEvent("drivethru:restaurantCameraReleased", resourceRoot, released and "released" or "release_failed")
end)

local function headingDistance(a, b)
    return math.abs((a - b + 180) % 360 - 180)
end

local function vehicleMatchesReconstruction(vehicle, profile)
    if not isElement(vehicle) or not isElementStreamedIn(vehicle) or not isElementSyncer(vehicle) or not applyGreenwoodPolicies(vehicle) then
        return false, "not streamed, syncer-owned, or policy-ready"
    end
    local x, y, z = getElementPosition(vehicle)
    local _, _, heading = getElementRotation(vehicle)
    local expected = profile.position
    if math.abs(x - expected.x) > 0.35 or math.abs(y - expected.y) > 0.35 or math.abs(z - expected.z) > 0.35 or
        headingDistance(heading, expected.heading) > 2.0 then
        return false, ("position=(%.3f,%.3f,%.3f) heading=%.2f"):format(x, y, z, heading)
    end
    return true, ("position=(%.3f,%.3f,%.3f) heading=%.2f"):format(x, y, z, heading)
end

local function rememberPursuitTimer(timer)
    state.pursuitTimers[#state.pursuitTimers + 1] = timer
    return timer
end

local function clearPursuitNavigation()
    if isElement(state.pursuitBlip) then
        destroyElement(state.pursuitBlip)
    end
    state.pursuitBlip = nil
    for _, blip in ipairs(state.pursuitPedBlips) do
        if isElement(blip) then
            destroyElement(blip)
        end
    end
    state.pursuitPedBlips = {}
end

local function showPursuitVehicleNavigation(voodoo)
    destroyNavigation()
    clearPursuitNavigation()
    if not isElement(voodoo) then
        return
    end
    state.pursuitBlip = createBlipAttachedTo(voodoo, 0, 2, unpack(SCM_THREAT_BLIP_COLOR))
    if isElement(state.pursuitBlip) then
        setElementDimension(state.pursuitBlip, DRIVETHRU.dimension)
    end
end

local function showPursuitPedNavigation()
    destroyNavigation()
    clearPursuitNavigation()
    for _, name in ipairs({"ballas_driver", "ballas_passenger"}) do
        local ped = state.actors[name]
        if isElement(ped) and not isPedDead(ped) then
            local blip = createBlipAttachedTo(ped, 0, 2, unpack(SCM_THREAT_BLIP_COLOR))
            if isElement(blip) then
                setElementDimension(blip, DRIVETHRU.dimension)
                state.pursuitPedBlips[#state.pursuitPedBlips + 1] = blip
            end
        end
    end
end

local function dispatchDriveBy(ped, target, profile)
    return isElement(ped) and isElement(target) and type(setPedDriveBy) == "function" and
               setPedDriveBy(ped, target, profile.abortRange, profile.style, profile.seatRHS, profile.frequency) == true
end

local function tryStartSupportChat()
    if not state.active or state.supportChatAccepted then
        return state.supportChatAccepted
    end
    local mate1, mate2 = state.actors.mate1, state.actors.mate2
    if not isElement(mate1) or not isElement(mate2) or not isElementStreamedIn(mate1) or not isElementStreamedIn(mate2) or
        not isElementSyncer(mate1) or not isElementSyncer(mate2) or not applyActorPolicies(mate1) or not applyActorPolicies(mate2) then
        return false
    end
    if type(setPedChatWith) ~= "function" then
        triggerServerEvent("drivethru:supportChatReady", resourceRoot, "api_unavailable")
        return false
    end
    local lead = setPedChatWith(mate1, mate2, true, true, true)
    local reply = setPedChatWith(mate2, mate1, false, true, true)
    state.supportChatAccepted = lead == true and reply == true
    outputDebugString(("[drive-thru] Grove support chat acceptance=%s/%s"):format(tostring(lead), tostring(reply)))
    triggerServerEvent("drivethru:supportChatReady", resourceRoot, state.supportChatAccepted and "accepted" or "refused")
    return state.supportChatAccepted
end

addEvent("drivethru:restaurantRebuilt", true)
addEventHandler("drivethru:restaurantRebuilt", resourceRoot, function(entities)
    if source ~= resourceRoot or not state.active or type(entities) ~= "table" then
        return
    end
    state.stage = "restaurant_barrier"
    state.vehicle = entities.vehicle
    state.actors = {
        smoke = entities.smoke,
        sweet = entities.sweet,
        ryder = entities.ryder,
        ballas_driver = entities.ballas_driver,
    }
    local voodoo = entities.voodoo
    showPursuitVehicleNavigation(voodoo)

    local startedAt = getTickCount()
    local stableSamples = 0
    local lastDetails = "not sampled"
    state.restaurantRebuildTimer = setTimer(function()
        if not state.active or state.stage ~= "restaurant_barrier" then
            return
        end
        local greenwoodReady, greenwoodDetails = vehicleMatchesReconstruction(state.vehicle, DRIVETHRU.restaurant.greenwood)
        local voodooReady, voodooDetails = vehicleMatchesReconstruction(voodoo, DRIVETHRU.restaurant.voodoo)
        if voodooReady and math.abs(getElementHealth(voodoo) - DRIVETHRU.restaurant.voodoo.initialStreamHealth) > 0.5 then
            voodooReady = false
            voodooDetails = voodooDetails .. (" health=%.1f"):format(getElementHealth(voodoo))
        end
        local actorsReady = true
        local seatDetails = {}
        for _, name in ipairs({"smoke", "sweet", "ryder", "ballas_driver"}) do
            local ped = state.actors[name]
            local expectedVehicle = name == "ballas_driver" and voodoo or state.vehicle
            local expectedSeat = name == "ballas_driver" and DRIVETHRU.restaurant.ballasDriver.seat or DRIVETHRU.actors[name].seat
            local ready = isElement(ped) and isElementStreamedIn(ped) and isElementSyncer(ped) and applyActorPolicies(ped) and
                              getPedOccupiedVehicle(ped) == expectedVehicle and getPedOccupiedVehicleSeat(ped) == expectedSeat
            actorsReady = actorsReady and ready
            seatDetails[#seatDetails + 1] = ("%s=%s/%s"):format(name, tostring(ready),
                                                                 tostring(isElement(ped) and getPedOccupiedVehicleSeat(ped) or "none"))
        end
        local cjReady = getPedOccupiedVehicle(localPlayer) == state.vehicle and getPedOccupiedVehicleSeat(localPlayer) == 0
        local ready = greenwoodReady and voodooReady and actorsReady and cjReady and isElement(state.pursuitBlip)
        stableSamples = ready and stableSamples + 1 or 0
        lastDetails = ("greenwood[%s] voodoo[%s] cj=%s %s stable=%d/%d"):format(
                          greenwoodDetails, voodooDetails, tostring(cjReady), table.concat(seatDetails, ","), stableSamples,
                          DRIVETHRU.restaurant.stableSamples)
        if stableSamples >= DRIVETHRU.restaurant.stableSamples then
            killTimer(state.restaurantRebuildTimer)
            state.restaurantRebuildTimer = nil
            outputDebugString("[drive-thru] PRE-PURSUIT RECONSTRUCTION READY: " .. lastDetails)
            triggerServerEvent("drivethru:restaurantRebuildReady", resourceRoot, "ready", lastDetails)
        elseif getTickCount() - startedAt >= DRIVETHRU.restaurant.reconstructionTimeout then
            killTimer(state.restaurantRebuildTimer)
            state.restaurantRebuildTimer = nil
            triggerServerEvent("drivethru:restaurantRebuildReady", resourceRoot, "timeout", lastDetails)
        end
    end, 100, 0)
end)

addEvent("drivethru:pursuitRoute", true)
addEventHandler("drivethru:pursuitRoute", resourceRoot, function(entities)
    if source ~= resourceRoot or not state.active or type(entities) ~= "table" then
        return
    end
    state.stage = "pursuit_route_barrier"
    state.vehicle = entities.vehicle
    state.actors.ballas_driver = entities.ballas_driver
    local driver, voodoo = entities.ballas_driver, entities.voodoo
    local requestedAt = getTickCount()
    local accepted = false
    local lastIndex = nil
    local timer
    timer = rememberPursuitTimer(setTimer(function()
        if not state.active or state.stage ~= "pursuit_route_barrier" then
            return
        end
        if not isElement(driver) or not isElement(voodoo) then
            killTimer(timer)
            return triggerServerEvent("drivethru:pursuitRouteReady", resourceRoot, "elements_missing")
        end
        local ready = isElementStreamedIn(driver) and isElementStreamedIn(voodoo) and isElementSyncer(driver) and
                          isElementSyncer(voodoo) and applyActorPolicies(driver) and applyGreenwoodPolicies(voodoo) and
                          getPedOccupiedVehicle(driver) == voodoo and getPedOccupiedVehicleSeat(driver) == 0 and
                          math.abs(getElementHealth(voodoo) - DRIVETHRU.restaurant.voodoo.health) <= 0.5
        if ready and not accepted then
            if type(setPedTaskSequence) ~= "function" or type(getPedTaskSequenceProgress) ~= "function" then
                killTimer(timer)
                return triggerServerEvent("drivethru:pursuitRouteReady", resourceRoot, "api_unavailable")
            end
            local sequence = {}
            for index, point in ipairs(DRIVETHRU.chase.route) do
                sequence[index] = {
                    task = "drive_to",
                    x = point.x,
                    y = point.y,
                    z = point.z,
                    speed = point.speed,
                    mode = DRIVETHRU.chase.drivingMode,
                    vehicleModel = DRIVETHRU.chase.vehicleModel,
                    drivingStyle = DRIVETHRU.chase.drivingStyle,
                }
            end
            accepted = setPedTaskSequence(driver, sequence, false) == true
            outputDebugString("[drive-thru] Ballas eight-point route acceptance=" .. tostring(accepted))
            if not accepted then
                killTimer(timer)
                return triggerServerEvent("drivethru:pursuitRouteReady", resourceRoot, "refused")
            end
        end
        if accepted then
            local index = getPedTaskSequenceProgress(driver)
            if index ~= lastIndex then
                lastIndex = index
                outputDebugString(("[drive-thru] Ballas route native sequence index=%d"):format(index))
            end
            if index >= 0 then
                killTimer(timer)
                triggerServerEvent("drivethru:pursuitRouteReady", resourceRoot, "active",
                                   ("index=%d elapsed=%dms"):format(index, getTickCount() - requestedAt))
                return
            end
        end
        if getTickCount() - requestedAt >= DRIVETHRU.chase.routeActivationTimeout then
            killTimer(timer)
            triggerServerEvent("drivethru:pursuitRouteReady", resourceRoot, "timeout",
                               ("accepted=%s index=%s health=%.1f"):format(tostring(accepted), tostring(lastIndex),
                                                                           getElementHealth(voodoo)))
        end
    end, DRIVETHRU.chase.monitorInterval, 0))
end)

addEvent("drivethru:pursuitActorsCreated", true)
addEventHandler("drivethru:pursuitActorsCreated", resourceRoot, function(entities)
    if source ~= resourceRoot or not state.active or type(entities) ~= "table" then
        return
    end
    state.stage = "pursuit_task_barrier"
    state.vehicle = entities.vehicle
    for _, name in ipairs({"smoke", "sweet", "ryder", "ballas_driver", "ballas_passenger", "mate1", "mate2"}) do
        state.actors[name] = entities[name]
    end
    tryStartSupportChat()
    local voodoo = entities.voodoo
    local requestedAt = getTickCount()
    local assigned = false
    local timer
    timer = rememberPursuitTimer(setTimer(function()
        if not state.active or state.stage ~= "pursuit_task_barrier" then
            return
        end
        local ready = isElement(state.vehicle) and isElement(voodoo) and isElementStreamedIn(state.vehicle) and
                          isElementStreamedIn(voodoo) and isElementSyncer(state.vehicle) and isElementSyncer(voodoo) and
                          applyGreenwoodPolicies(state.vehicle) and applyGreenwoodPolicies(voodoo)
        for _, name in ipairs({"sweet", "ryder", "ballas_driver", "ballas_passenger"}) do
            local ped = state.actors[name]
            ready = ready and isElement(ped) and isElementStreamedIn(ped) and isElementSyncer(ped) and applyActorPolicies(ped)
        end
        ready = ready and getPedOccupiedVehicle(state.actors.ballas_driver) == voodoo and
                    getPedOccupiedVehicleSeat(state.actors.ballas_driver) == 0 and
                    getPedOccupiedVehicle(state.actors.ballas_passenger) == voodoo and
                    getPedOccupiedVehicleSeat(state.actors.ballas_passenger) == DRIVETHRU.chase.ballasPassenger.seat and
                    getPedOccupiedVehicle(state.actors.ryder) == state.vehicle and
                    getPedOccupiedVehicleSeat(state.actors.ryder) == DRIVETHRU.actors.ryder.seat and
                    getPedOccupiedVehicle(state.actors.sweet) == state.vehicle and
                    getPedOccupiedVehicleSeat(state.actors.sweet) == DRIVETHRU.actors.sweet.seat

        if ready and not assigned then
            if type(isPedDoingTask) ~= "function" or type(getPedTaskSequenceProgress) ~= "function" then
                killTimer(timer)
                return triggerServerEvent("drivethru:pursuitTasksReady", resourceRoot, "api_unavailable")
            end
            local passenger = dispatchDriveBy(state.actors.ballas_passenger, state.vehicle, DRIVETHRU.chase.driveBy.ballasPassenger)
            local ryder = dispatchDriveBy(state.actors.ryder, voodoo, DRIVETHRU.chase.driveBy.ryder)
            local sweet = dispatchDriveBy(state.actors.sweet, voodoo, DRIVETHRU.chase.driveBy.sweet)
            assigned = passenger and ryder and sweet
            outputDebugString(("[drive-thru] Pursuit drive-by assignment=%s/%s/%s"):format(tostring(passenger), tostring(ryder),
                                                                                              tostring(sweet)))
            if not assigned then
                killTimer(timer)
                return triggerServerEvent("drivethru:pursuitTasksReady", resourceRoot, "refused")
            end
        end
        if assigned then
            local passengerActive = isPedDoingTask(state.actors.ballas_passenger, "TASK_SIMPLE_GANG_DRIVEBY")
            local ryderActive = isPedDoingTask(state.actors.ryder, "TASK_SIMPLE_GANG_DRIVEBY")
            local sweetActive = isPedDoingTask(state.actors.sweet, "TASK_SIMPLE_GANG_DRIVEBY")
            local routeIndex = getPedTaskSequenceProgress(state.actors.ballas_driver)
            if passengerActive and ryderActive and sweetActive and routeIndex >= 0 then
                killTimer(timer)
                outputDebugString(("[drive-thru] Three native drive-bys active with Ballas route index=%d"):format(routeIndex))
                triggerServerEvent("drivethru:pursuitTasksReady", resourceRoot, "active",
                                   ("route=%d elapsed=%dms"):format(routeIndex, getTickCount() - requestedAt))
                return
            end
        end
        if getTickCount() - requestedAt >= DRIVETHRU.chase.taskActivationTimeout then
            killTimer(timer)
            triggerServerEvent("drivethru:pursuitTasksReady", resourceRoot, "timeout", "native tasks were not all observed active")
        end
    end, DRIVETHRU.chase.monitorInterval, 0))
end)

addEvent("drivethru:pursuitStarted", true)
addEventHandler("drivethru:pursuitStarted", resourceRoot, function(entities)
    if source ~= resourceRoot or not state.active or type(entities) ~= "table" then
        return
    end
    state.stage = "chase"
    state.footCombat = false
    state.vehicle = entities.vehicle
    showPursuitVehicleNavigation(entities.voodoo)
    setCameraTarget(localPlayer)
    fadeCamera(true, 1.0)
    printMissionText("SWE3_B", 3000)
    outputDebugString("[drive-thru] CHASE STARTED after native route and drive-by activation barrier")
end)

addEvent("drivethru:chaseHelp", true)
addEventHandler("drivethru:chaseHelp", resourceRoot, function()
    if source == resourceRoot and state.active and state.stage == "chase" then
        printMissionHelp("SWE3_H")
    end
end)

addEvent("drivethru:chaseNavigation", true)
addEventHandler("drivethru:chaseNavigation", resourceRoot, function(mode, entities)
    if source ~= resourceRoot or not state.active or state.stage ~= "chase" then
        return
    end
    if mode == "vehicle" then
        clearPursuitNavigation()
        showVehicleNavigation()
        printMissionText("TW2_X", 3000)
    elseif state.footCombat then
        showPursuitPedNavigation()
    else
        showPursuitVehicleNavigation(type(entities) == "table" and entities.voodoo or nil)
        printMissionText("SWE3_B", 3000)
    end
end)

addEvent("drivethru:footCombat", true)
addEventHandler("drivethru:footCombat", resourceRoot, function(entities, reason)
    if source ~= resourceRoot or not state.active or state.stage ~= "chase" or type(entities) ~= "table" then
        return
    end
    state.footCombat = true
    for _, name in ipairs({"ballas_driver", "ballas_passenger"}) do
        state.actors[name] = entities[name]
    end
    showPursuitPedNavigation()
    callMissionTextApi("clearMissionHelp")
    printMissionText("K_BALLA", 6000)

    local expected = {}
    local accepted = true
    if type(setPedKillOnFoot) ~= "function" or type(isPedDoingTask) ~= "function" then
        return triggerServerEvent("drivethru:footCombatReady", resourceRoot, "api_unavailable")
    end
    for _, name in ipairs({"ballas_driver", "ballas_passenger"}) do
        local ped = state.actors[name]
        if isElement(ped) and not isPedDead(ped) then
            expected[name] = "kill"
            accepted = setPedKillOnFoot(ped, localPlayer) == true and accepted
        end
    end
    local driver, passenger = state.actors.ballas_driver, state.actors.ballas_passenger
    if isElement(driver) and not isPedDead(driver) then
        expected.ryder = "driveby"
        accepted = dispatchDriveBy(state.actors.ryder, driver, DRIVETHRU.chase.driveBy.ryderOnFoot) and accepted
    end
    if isElement(passenger) and not isPedDead(passenger) then
        expected.sweet = "driveby"
        accepted = dispatchDriveBy(state.actors.sweet, passenger, DRIVETHRU.chase.driveBy.sweetOnFoot) and accepted
    end
    outputDebugString(("[drive-thru] Foot-combat acceptance=%s trigger=%s"):format(tostring(accepted), tostring(reason)))
    if not accepted then
        return triggerServerEvent("drivethru:footCombatReady", resourceRoot, "refused")
    end

    local requestedAt = getTickCount()
    local timer
    timer = rememberPursuitTimer(setTimer(function()
        if not state.active or state.stage ~= "chase" then
            return
        end
        local active = true
        for name, kind in pairs(expected) do
            local ped = state.actors[name]
            if isElement(ped) and not isPedDead(ped) then
                local task = kind == "kill" and "TASK_COMPLEX_KILL_PED_ON_FOOT" or "TASK_SIMPLE_GANG_DRIVEBY"
                active = isPedDoingTask(ped, task) and active
            end
        end
        if active then
            killTimer(timer)
            triggerServerEvent("drivethru:footCombatReady", resourceRoot, "active",
                               ("elapsed=%dms"):format(getTickCount() - requestedAt))
        elseif getTickCount() - requestedAt >= DRIVETHRU.chase.taskActivationTimeout then
            killTimer(timer)
            triggerServerEvent("drivethru:footCombatReady", resourceRoot, "timeout")
        end
    end, DRIVETHRU.chase.monitorInterval, 0))
end)

addEvent("drivethru:chaseCheckpoint", true)
addEventHandler("drivethru:chaseCheckpoint", resourceRoot, function(entities)
    if source ~= resourceRoot or not state.active then
        return
    end
    state.stage = "chase_checkpoint"
    for _, timer in ipairs(state.pursuitTimers) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    state.pursuitTimers = {}
    clearAudio("chase_complete")
    callMissionTextApi("clearMissionHelp")
    destroyNavigation()
    clearPursuitNavigation()
    for _, name in ipairs({"sweet", "ryder"}) do
        local ped = state.actors[name]
        if isElement(ped) and type(killPedTask) == "function" then
            pcall(killPedTask, ped, "primary", 3, false)
        end
    end
    outputChatBox("Drive-Thru: poursuite native validee, les deux Ballas sont morts.", 80, 220, 120)
    outputChatBox("Checkpoint arrete avant le retour a Grove. Utilise /drivethruabort apres verification.", 220, 220, 220)
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
        if getElementData(source, DRIVETHRU.actorRoleData) == "grove_support" then
            tryStartSupportChat()
        end
    elseif getElementData(source, DRIVETHRU.vehicleData) == true or type(getElementData(source, DRIVETHRU.vehicleRoleData)) == "string" then
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
