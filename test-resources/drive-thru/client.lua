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
    returnPhase = nil,
    returnScene = nil,
    hoodFailure = nil,
}

local SCM_DESTINATION_BLIP_COLOR = {226, 192, 99, 255}
local SCM_FRIENDLY_BLIP_COLOR = {0, 0, 255, 255}
local SCM_THREAT_BLIP_COLOR = {255, 0, 0, 255}

local MISSION_TRACE_SEQUENCE = {
    {id = "mission_start", title = "The server opens an authoritative SWEET3 run", category = "SERVER AUTHORITY", primitive = "MISSION STATE"},
    {id = "sweet2a", title = "GTA plays the original SWEET2A file cutscene", category = "NATIVE CUTSCENE", primitive = "DAT / CUT / IFP + SWEET2A"},
    {id = "crew", title = "The server creates CJ's crew and the Greenwood", category = "SCM ADAPTER", primitive = "009A / 00A5 · CREATE_CHAR / CREATE_CAR"},
    {id = "entry", title = "Sweet, Ryder and Smoke enter their exact seats", category = "NATIVE TASK", primitive = "05CA · TASK_ENTER_CAR_AS_PASSENGER", originalTask = "CTaskComplexEnterCarAsPassenger"},
    {id = "intro_audio", title = "GTA plays the initial SWEET3 dialogue chain", category = "NATIVE AUDIO", primitive = "03CF / 03D1 · SWE2_AA..."},
    {id = "drive_restaurant", title = "The Greenwood reaches the exact Cluckin' Bell gate", category = "NATIVE PREDICATE", primitive = "09D0 · LOCATE_CAR_3D + ALL WHEELS"},
    {id = "restaurant_camera", title = "A fixed wide shot owns the restaurant transition", category = "NATIVE CAMERA", primitive = "015F / 0160 + DO_FADE"},
    {id = "sweet2b", title = "GTA plays the original SWEET2B file cutscene", category = "NATIVE CUTSCENE", primitive = "DAT / CUT / IFP + SWEET2B"},
    {id = "rebuild", title = "The server reconstructs every pursuit actor and vehicle", category = "SCM ADAPTER", primitive = "DELETE + CREATE + WARP UNDER BLACK"},
    {id = "route", title = "The Ballas driver receives the exact eight-point route", category = "NATIVE SEQUENCE", primitive = "0615 / 05D1 / 0618 · DRIVE_TO", originalTask = "CTaskComplexCarDriveToPoint -> CTaskComplexUseSequence"},
    {id = "driveby", title = "Ballas, Ryder and Sweet receive native drive-by tasks", category = "NATIVE TASK", primitive = "0713 · TASK_DRIVE_BY", originalTask = "CTaskSimpleGangDriveBy"},
    {id = "chase", title = "The synchronized pursuit is visible and damage-active", category = "SERVER AUTHORITY", primitive = "CHASE HEALTH / DEATH GATES"},
    {id = "foot_combat", title = "Surviving Ballas transition to on-foot combat", category = "NATIVE TASK", primitive = "05E2 · TASK_KILL_CHAR_ON_FOOT", originalTask = "CTaskComplexKillPedOnFoot"},
    {id = "return_grove", title = "CJ drives Sweet and Ryder to the Grove gate", category = "NATIVE PREDICATE", primitive = "09D0 · LOCATE_CAR_3D + ALL WHEELS"},
    {id = "grove_scene", title = "The Grove camera and departure sequences run", category = "NATIVE CAMERA / TASK", primitive = "VECTOR CAMERA + 05CD / 05D3", originalTask = "CTaskComplexLeaveCar -> CTaskComplexGoToPointAndStandStillTimed"},
    {id = "return_smoke", title = "CJ drives Smoke to his exact home gate", category = "NATIVE PREDICATE", primitive = "09D0 · LOCATE_CAR_3D + ALL WHEELS"},
    {id = "smoke_scene", title = "Smoke leaves during the original final camera", category = "NATIVE CAMERA / TASK", primitive = "FIXED CAMERA + 05CD / 05D3", originalTask = "CTaskComplexLeaveCar -> CTaskComplexGoToPointAndStandStillTimed"},
    {id = "mission_end", title = "The server awards $200 and plays the native tune", category = "SERVER AUTHORITY", primitive = "0394 · MISSION PASSED TUNE"},
    {id = "vehicle_warning", title = "GTA plays the phase-specific wreck warning", category = "NATIVE AUDIO", primitive = "03CF / 03D1 · SWE2_KA / KB / KC"},
    {id = "flee", title = "The surviving crew bails out and flees from CJ", category = "NATIVE SEQUENCE", primitive = "0622 + 05DD · LEAVE IMMEDIATELY / SMART FLEE", originalTask = "CTaskComplexLeaveCar -> CTaskComplexSmartFleeEntity"},
    {id = "hood_camera", title = "The Ballas arrival takes over the fixed camera", category = "NATIVE CAMERA", primitive = "DO_FADE + LOAD_SCENE"},
    {id = "hood_driveby", title = "The Voodoo passenger fires at the Grove coordinate", category = "NATIVE TASK", primitive = "0713 · TASK_DRIVE_BY COORD", originalTask = "CTaskSimpleGangDriveBy"},
    {id = "hood_deaths", title = "The two Grove actors receive timed scripted deaths", category = "NATIVE TASK", primitive = "05BE · TASK_DIE", originalTask = "CTaskComplexDie"},
    {id = "failure_restore", title = "Camera, control and frozen state are restored", category = "NATIVE CLEANUP", primitive = "CAMERA BEHIND + CONTROL ON"},
    {id = "mission_failed", title = "The server commits the vanilla failure result", category = "SERVER AUTHORITY", primitive = "PRINT_BIG · M_FAIL"},
}

local function traceCurrent(step)
    if type(DRIVETHRU_TRACE) == "table" then
        DRIVETHRU_TRACE.setCurrent(step)
    end
end

local function traceStart()
    if type(DRIVETHRU_TRACE) == "table" and DRIVETHRU_TRACE.setSequence(MISSION_TRACE_SEQUENCE) then
        traceCurrent("mission_start")
    end
end

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

local function showReturnDestinationNavigation(phase)
    destroyNavigation()
    local profile = DRIVETHRU.returnTrip[phase]
    if not profile then
        return
    end
    local destination = profile.navigation
    state.blip = createBlip(destination.x, destination.y, destination.z, 0, 2, unpack(SCM_DESTINATION_BLIP_COLOR))
    if isElement(state.blip) then
        setElementDimension(state.blip, DRIVETHRU.dimension)
    end
    local area = profile.destination
    if type(renderScriptImportantArea) ~= "function" then
        state.marker = createMarker(area.x, area.y, area.z - 1.0, "cylinder", math.max(area.radiusX, area.radiusY), 255, 0, 0, 180)
        if isElement(state.marker) then
            setElementDimension(state.marker, DRIVETHRU.dimension)
        end
    end
    state.navigation = "return_destination"
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
        if type(setPedStayInSamePlace) ~= "function" or type(getPedStayInSamePlace) ~= "function" or
            type(setPedNeverTargeted) ~= "function" or type(isPedNeverTargeted) ~= "function" then
            return false
        end
        -- SWEET3 applies these two scalar CPed flags before TASK_CHAT. Keep
        -- them independent from the broader protagonist protection policy.
        return setPedStayInSamePlace(ped, true) == true and getPedStayInSamePlace(ped) == true and
                   setPedNeverTargeted(ped, true) == true and isPedNeverTargeted(ped) == true
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

local function clearReturnScene(reason, preserveFade)
    local scene = state.returnScene
    if not scene then
        return true
    end
    for _, timer in ipairs({scene.leaseTimer, scene.cameraReadyTimer, scene.departureMonitor, scene.releaseTimer}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    for _, timer in ipairs(scene.departureTimers or {}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    local released = true
    if scene.cameraToken and type(releaseScriptCamera) == "function" then
        local ok, result = pcall(releaseScriptCamera, scene.cameraToken, preserveFade == true)
        released = ok and result == true
    end
    state.returnScene = nil
    outputDebugString(("[drive-thru] Return scene cleared released=%s reason=%s"):format(tostring(released),
                                                                                         tostring(reason or "cleanup")))
    return released
end

local function clearHoodFailure(reason, preserveFade)
    local failure = state.hoodFailure
    if not failure then
        return true
    end
    for _, timer in ipairs(failure.timers or {}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    local released = true
    if failure.cameraToken and type(releaseScriptCamera) == "function" then
        local ok, result = pcall(releaseScriptCamera, failure.cameraToken, preserveFade == true)
        released = ok and result == true
    end
    for _, token in ipairs(failure.streamingLeases or {}) do
        if type(releaseElementStreamingLease) == "function" then
            local ok, result = pcall(releaseElementStreamingLease, token)
            released = ok and result == true and released
        else
            released = false
        end
    end
    state.hoodFailure = nil
    outputDebugString(("[drive-thru] Hood failure cleared released=%s reason=%s"):format(tostring(released),
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
    clearReturnScene(reason, false)
    clearHoodFailure(reason, false)
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
    state.returnPhase = nil
end

addEvent("drivethru:start", true)
addEventHandler("drivethru:start", resourceRoot, function(vehicle)
    clearClientState("replaced")
    state.active = true
    state.stage = "preparing"
    state.vehicle = vehicle
    ensureMissionText()
    traceStart()
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
    traceCurrent(name == "SWEET2A" and "sweet2a" or "sweet2b")
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
addEventHandler("drivethru:stage", resourceRoot, function(stage, entities, suppressInstruction)
    if source ~= resourceRoot or not state.active then
        return
    end
    state.stage = stage
    if type(entities) == "table" then
        state.vehicle = entities.vehicle or state.vehicle
    end
    if stage == "actor_entry" then
        traceCurrent("crew")
        traceCurrent("entry")
        beginActorEntry(entities)
    elseif stage == "enter_car" then
        printMissionText("TWAR2_A", 6000)
        showVehicleNavigation()
    elseif stage == "drive" then
        traceCurrent("drive_restaurant")
        showDestinationNavigation()
        printMissionText("TWAR2_C", 6000)
        if getPedOccupiedVehicle(localPlayer) == state.vehicle then
            setRadioChannel(DRIVETHRU.vehicle.bounceRadioChannel)
        end
    elseif stage == "return_car" then
        showVehicleNavigation()
        if suppressInstruction ~= true then
            printMissionText("TW2_X", 3000)
        end
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
    if state.stage == "actor_entry" or state.stage == "enter_car" or state.stage == "drive" then
        traceCurrent("intro_audio")
    end
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
            callMissionTextApi("clearMissionTexts")
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
    traceCurrent("restaurant_camera")
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
    traceCurrent("rebuild")
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
    local lastWaitReportAt = startedAt
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
            local expectedSeat = name == "ballas_driver" and DRIVETHRU.restaurant.ballasDriver.seat or
                                     DRIVETHRU.restaurant.passengerSeats[name]
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
        local now = getTickCount()
        if stableSamples >= DRIVETHRU.restaurant.stableSamples then
            killTimer(state.restaurantRebuildTimer)
            state.restaurantRebuildTimer = nil
            outputDebugString("[drive-thru] PRE-PURSUIT RECONSTRUCTION READY: " .. lastDetails)
            triggerServerEvent("drivethru:restaurantRebuildReady", resourceRoot, "ready", lastDetails)
        elseif now - startedAt >= DRIVETHRU.restaurant.reconstructionTimeout then
            killTimer(state.restaurantRebuildTimer)
            state.restaurantRebuildTimer = nil
            triggerServerEvent("drivethru:restaurantRebuildReady", resourceRoot, "timeout", lastDetails)
        elseif now - lastWaitReportAt >= 1000 then
            lastWaitReportAt = now
            outputDebugString(("[drive-thru] Reconstruction waiting after %d ms: %s"):format(now - startedAt, lastDetails))
        end
    end, 100, 0)
end)

addEvent("drivethru:pursuitRoute", true)
addEventHandler("drivethru:pursuitRoute", resourceRoot, function(entities)
    if source ~= resourceRoot or not state.active or type(entities) ~= "table" then
        return
    end
    state.stage = "pursuit_route_barrier"
    traceCurrent("route")
    state.vehicle = entities.vehicle
    state.actors.ballas_driver = entities.ballas_driver
    outputDebugString("[drive-thru] Ballas route delegated to the server-owned native task runtime")
end)

addEvent("drivethru:pursuitActorsCreated", true)
addEventHandler("drivethru:pursuitActorsCreated", resourceRoot, function(entities)
    if source ~= resourceRoot or not state.active or type(entities) ~= "table" then
        return
    end
    state.stage = "pursuit_task_barrier"
    traceCurrent("driveby")
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
                    getPedOccupiedVehicleSeat(state.actors.ryder) == DRIVETHRU.restaurant.passengerSeats.ryder and
                    getPedOccupiedVehicle(state.actors.sweet) == state.vehicle and
                    getPedOccupiedVehicleSeat(state.actors.sweet) == DRIVETHRU.restaurant.passengerSeats.sweet

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
    traceCurrent("chase")
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
addEventHandler("drivethru:chaseNavigation", resourceRoot, function(mode, entities, suppressInstruction)
    if source ~= resourceRoot or not state.active or state.stage ~= "chase" then
        return
    end
    if mode == "vehicle" then
        clearPursuitNavigation()
        showVehicleNavigation()
        if suppressInstruction ~= true then
            printMissionText("TW2_X", 3000)
        end
    elseif state.footCombat then
        showPursuitPedNavigation()
    else
        showPursuitVehicleNavigation(type(entities) == "table" and entities.voodoo or nil)
        printMissionText("SWE3_B", 3000)
    end
end)

addEvent("drivethru:greenwoodFailureTasks", true)
addEventHandler("drivethru:greenwoodFailureTasks", resourceRoot, function(actorNames, entities)
    if source ~= resourceRoot or not state.active or type(actorNames) ~= "table" or type(entities) ~= "table" then
        return
    end
    state.stage = "greenwood_failure"
    state.vehicle = entities.vehicle or state.vehicle
    destroyNavigation()
    clearPursuitNavigation()
    traceCurrent("vehicle_warning")
    traceCurrent("flee")
    if type(setPedTaskSequence) ~= "function" or type(getPedTaskSequenceProgress) ~= "function" then
        return triggerServerEvent("drivethru:greenwoodFailureTasksReady", resourceRoot, "api_unavailable")
    end
    local requestedAt = getTickCount()
    local accepted, observed = {}, {}
    local timer
    timer = rememberPursuitTimer(setTimer(function()
        if not state.active or state.stage ~= "greenwood_failure" then
            return
        end
        local allReady, allActive = true, true
        local details = {}
        for _, name in ipairs(actorNames) do
            local ped = entities[name]
            state.actors[name] = ped
            local ready = isElement(ped) and isElementStreamedIn(ped) and isElementSyncer(ped) and isElement(state.vehicle) and
                              isElementStreamedIn(state.vehicle) and applyActorPolicies(ped)
            allReady = allReady and ready
            if ready and not accepted[name] then
                accepted[name] = setPedTaskSequence(ped, {
                    {task = "leave_car_immediately", vehicle = state.vehicle},
                    {
                        task = "smart_flee",
                        target = localPlayer,
                        safeDistance = 100.0,
                        duration = 10000,
                    },
                }, false) == true
                if not accepted[name] then
                    killTimer(timer)
                    return triggerServerEvent("drivethru:greenwoodFailureTasksReady", resourceRoot, "refused", name)
                end
            end
            local index = accepted[name] and getPedTaskSequenceProgress(ped) or -1
            if index >= 0 then
                observed[name] = true
            end
            allActive = allActive and observed[name] == true
            details[#details + 1] = ("%s=%s/%s"):format(name, tostring(accepted[name] == true), tostring(index))
        end
        if allReady and allActive then
            killTimer(timer)
            triggerServerEvent("drivethru:greenwoodFailureTasksReady", resourceRoot, "active", table.concat(details, ","))
        elseif getTickCount() - requestedAt >= DRIVETHRU.chase.taskActivationTimeout then
            killTimer(timer)
            triggerServerEvent("drivethru:greenwoodFailureTasksReady", resourceRoot, "timeout", table.concat(details, ","))
        end
    end, DRIVETHRU.chase.monitorInterval, 0))
end)

local function rememberHoodFailureTimer(failure, timer)
    failure.timers[#failure.timers + 1] = timer
    return timer
end

local function getObservedDeathTask(ped)
    for _, taskName in ipairs({"TASK_COMPLEX_DIE", "TASK_SIMPLE_DIE", "TASK_SIMPLE_DEAD"}) do
        if isPedDoingTask(ped, taskName) then
            return taskName
        end
    end
    return nil
end

local function acquireHoodFailureStreamingLease(failure, name)
    if failure.streamingLeaseByName[name] then
        return true
    end
    local element = failure.entities[name]
    local lease = isElement(element) and acquireElementStreamingLease(element) or false
    if not lease then
        return false
    end
    failure.streamingLeaseByName[name] = lease
    failure.streamingLeases[#failure.streamingLeases + 1] = lease
    return true
end

local function hoodFailureReadiness(failure)
    local entities = failure.entities
    local profile = DRIVETHRU.chase.hoodFailure
    local voodoo = entities.voodoo
    local driver = entities.ballas_driver
    local passenger = entities.ballas_passenger
    local ready = isElement(voodoo) and isElementStreamedIn(voodoo) and isElementSyncer(voodoo) and
                      applyGreenwoodPolicies(voodoo)
    local details = {}

    local vx, vy, vz = 0, 0, 0
    local voodooDistance = math.huge
    if isElement(voodoo) then
        vx, vy, vz = getElementPosition(voodoo)
        local hub = DRIVETHRU.chase.hub
        voodooDistance = getDistanceBetweenPoints3D(vx, vy, vz, hub.x, hub.y, hub.z)
    end
    ready = ready and voodooDistance <= profile.sceneRadius
    details[#details + 1] = ("voodoo=%.2f@(%.2f,%.2f,%.2f)"):format(voodooDistance, vx, vy, vz)

    for _, entry in ipairs({
        {name = "ballas_driver", ped = driver, seat = 0},
        {name = "ballas_passenger", ped = passenger, seat = DRIVETHRU.chase.ballasPassenger.seat},
    }) do
        local exists = isElement(entry.ped)
        local streamed = exists and isElementStreamedIn(entry.ped)
        local authoritative = streamed and isElementSyncer(entry.ped)
        local policiesReady = authoritative and applyActorPolicies(entry.ped)
        local correctVehicle = exists and getPedOccupiedVehicle(entry.ped) == voodoo
        local seat = exists and getPedOccupiedVehicleSeat(entry.ped) or "none"
        local pedReady = exists and streamed and authoritative and policiesReady and correctVehicle and seat == entry.seat
        ready = ready and pedReady
        details[#details + 1] = ("%s=%s/stream:%s/sync:%s/policy:%s/vehicle:%s/seat:%s"):format(
                                    entry.name, tostring(pedReady), tostring(streamed), tostring(authoritative),
                                    tostring(policiesReady), tostring(correctVehicle), tostring(seat))
    end

    for _, name in ipairs({"mate1", "mate2"}) do
        local ped = entities[name]
        local expected = DRIVETHRU.chase.support[name].position
        local px, py, pz = 0, 0, 0
        local distance = math.huge
        if isElement(ped) then
            px, py, pz = getElementPosition(ped)
            distance = getDistanceBetweenPoints3D(px, py, pz, expected.x, expected.y, expected.scriptZ + 1.0)
        end
        local bx, by, bz
        if isElement(ped) then
            bx, by, bz = getElementBonePosition(ped, 2)
        end
        local boneReady = type(bx) == "number" and type(by) == "number" and type(bz) == "number"
        local pedReady = isElement(ped) and isElementStreamedIn(ped) and isElementSyncer(ped) and not isPedDead(ped) and
                             applyActorPolicies(ped) and distance <= profile.supportPositionTolerance and boneReady
        ready = ready and pedReady
        details[#details + 1] = ("%s=%s/%.2f@(%.2f,%.2f,%.2f)/bone:%s"):format(name, tostring(pedReady), distance,
                                                                                 px, py, pz, tostring(boneReady))
    end

    local chatReady = ready and tryStartSupportChat()
    return ready and chatReady, table.concat(details, " ") .. " chat=" .. tostring(chatReady)
end

addEvent("drivethru:hoodFailurePrepare", true)
addEventHandler("drivethru:hoodFailurePrepare", resourceRoot, function(failureId, entities)
    if source ~= resourceRoot or not state.active or state.stage ~= "chase" or type(entities) ~= "table" then
        return
    end
    local required = {"acquireScriptCamera", "releaseScriptCamera", "isScriptCameraLeaseActive", "resetScriptCamera",
                      "setScriptCameraWidescreen", "setScriptCameraFixed", "fadeScriptCamera", "isScriptCameraFading",
                      "enginePreloadWorldArea", "enginePreloadWorldAreaInDirection", "acquireElementStreamingLease",
                      "releaseElementStreamingLease", "setPedStayInSamePlace", "getPedStayInSamePlace",
                      "setPedNeverTargeted", "isPedNeverTargeted"}
    for _, name in ipairs(required) do
        if type(_G[name]) ~= "function" then
            return triggerServerEvent("drivethru:hoodFailureBlack", resourceRoot, failureId, "api_unavailable", name)
        end
    end
    clearAudio("hood_failure")
    destroyNavigation()
    clearPursuitNavigation()
    callMissionTextApi("clearMissionHelp")
    state.stage = "hood_failure_prepare"
    traceCurrent("hood_camera")
    local acquired, token = pcall(acquireScriptCamera, true)
    if not acquired or type(token) ~= "number" then
        return triggerServerEvent("drivethru:hoodFailureBlack", resourceRoot, failureId, "camera_refused", tostring(token))
    end
    local failure = {
        id = tonumber(failureId),
        entities = entities,
        cameraToken = token,
        timers = {},
        streamingLeases = {},
        streamingLeaseByName = {},
    }
    state.hoodFailure = failure
    -- Overlap the runtime's route leases before it can complete or be
    -- cancelled. The distant support actors are intentionally deferred until
    -- Grove's camera and collision are loaded under black.
    for _, name in ipairs({"ballas_driver", "voodoo"}) do
        if not acquireHoodFailureStreamingLease(failure, name) then
            clearHoodFailure("streaming_lease_refused", false)
            return triggerServerEvent("drivethru:hoodFailureBlack", resourceRoot, failureId, "streaming_lease_refused", name)
        end
    end
    failure.beginCameraPreparation = function()
        if state.hoodFailure ~= failure or failure.cameraPreparationStarted then
            return
        end
        failure.cameraPreparationStarted = true
        if not fadeScriptCamera(token, false, DRIVETHRU.chase.hoodFailure.fadeDuration, 0, 0, 0) then
            clearHoodFailure("fade_out_refused", false)
            return triggerServerEvent("drivethru:hoodFailureBlack", resourceRoot, failureId, "fade_refused")
        end
        local fadeMonitor
        fadeMonitor = rememberHoodFailureTimer(failure, setTimer(function()
        if state.hoodFailure ~= failure then
            return
        end
        if not isScriptCameraLeaseActive(failure.cameraToken) then
            clearHoodFailure("lease_lost", false)
            return triggerServerEvent("drivethru:hoodFailureBlack", resourceRoot, failure.id, "lease_lost")
        end
        local queried, fading = pcall(isScriptCameraFading, failure.cameraToken)
        if not queried or fading ~= false then
            return
        end
        killTimer(fadeMonitor)
        local profile = DRIVETHRU.chase.hoodFailure
        local camera = profile.camera
        local cameraReady = resetScriptCamera(failure.cameraToken) and setScriptCameraWidescreen(failure.cameraToken, true) and
                                setScriptCameraFixed(failure.cameraToken,
                                                     Vector3(camera.position.x, camera.position.y, camera.position.z),
                                                     Vector3(camera.target.x, camera.target.y, camera.target.z))
        local collisionReady = pcall(enginePreloadWorldArea,
                                     Vector3(profile.collision.x, profile.collision.y, profile.collision.z), "collisions")
        local preloadCalled, preloadReady = pcall(enginePreloadWorldAreaInDirection,
                                                  Vector3(profile.preload.x, profile.preload.y, profile.preload.z),
                                                  profile.preload.heading)
        if not cameraReady or not collisionReady or not preloadCalled or preloadReady ~= true then
            clearHoodFailure("world_setup_refused", false)
            return triggerServerEvent("drivethru:hoodFailureBlack", resourceRoot, failure.id, "world_setup_refused")
        end
        rememberHoodFailureTimer(failure, setTimer(function()
            if state.hoodFailure ~= failure then
                return
            end
            for _, name in ipairs({"ballas_driver", "ballas_passenger", "mate1", "mate2", "voodoo"}) do
                if not acquireHoodFailureStreamingLease(failure, name) then
                    clearHoodFailure("scene_lease_refused", false)
                    return triggerServerEvent("drivethru:hoodFailureBlack", resourceRoot, failure.id, "scene_lease_refused", name)
                end
            end

            local startedAt = getTickCount()
            local lastReportAt = startedAt
            local stableSamples = 0
            local lastDetails = "not sampled"
            local readinessTimer
            readinessTimer = rememberHoodFailureTimer(failure, setTimer(function()
                if state.hoodFailure ~= failure then
                    return
                end
                local sampleReady
                sampleReady, lastDetails = hoodFailureReadiness(failure)
                stableSamples = sampleReady and stableSamples + 1 or 0
                local now = getTickCount()
                if stableSamples >= profile.stableSamples then
                    killTimer(readinessTimer)
                    outputDebugString(("[drive-thru] HOOD FAILURE SCENE READY: %s stable=%d/%d"):format(
                                          lastDetails, stableSamples, profile.stableSamples))
                    triggerServerEvent("drivethru:hoodFailureBlack", resourceRoot, failure.id, "ready", lastDetails)
                elseif now - startedAt >= profile.setupTimeout then
                    killTimer(readinessTimer)
                    triggerServerEvent("drivethru:hoodFailureBlack", resourceRoot, failure.id, "scene_timeout", lastDetails)
                elseif now - lastReportAt >= 1000 then
                    lastReportAt = now
                    outputDebugString(("[drive-thru] Hood scene waiting after %d ms: %s stable=%d/%d"):format(
                                          now - startedAt, lastDetails, stableSamples, profile.stableSamples))
                end
            end, DRIVETHRU.chase.monitorInterval, 0))
        end, profile.loadSceneWait, 1))
        end, DRIVETHRU.chase.monitorInterval, 0))
    end
    triggerServerEvent("drivethru:hoodFailureLeasesReady", resourceRoot, failure.id)
end)

addEvent("drivethru:hoodFailureLeasesCommitted", true)
addEventHandler("drivethru:hoodFailureLeasesCommitted", resourceRoot, function(failureId)
    local failure = state.hoodFailure
    if source ~= resourceRoot or not failure or failure.id ~= tonumber(failureId) or
        type(failure.beginCameraPreparation) ~= "function" then
        return
    end
    failure.beginCameraPreparation()
end)

addEvent("drivethru:hoodFailureFrozen", true)
addEventHandler("drivethru:hoodFailureFrozen", resourceRoot, function(failureId, entities)
    local failure = state.hoodFailure
    if source ~= resourceRoot or not failure or failure.id ~= tonumber(failureId) or type(entities) ~= "table" then
        return
    end
    state.stage = "hood_failure_setup"
    failure.entities = entities
    local driver, passenger, voodoo = entities.ballas_driver, entities.ballas_passenger, entities.voodoo
    for _, pair in ipairs({{driver, "ballas_driver"}, {passenger, "ballas_passenger"}}) do
        if not isElement(pair[1]) or not isElementStreamedIn(pair[1]) or not isElementSyncer(pair[1]) or not applyActorPolicies(pair[1]) then
            return triggerServerEvent("drivethru:hoodFailureActive", resourceRoot, failure.id, "ownership_missing", pair[2])
        end
    end
    if not isElement(voodoo) or not isElementStreamedIn(voodoo) or not isElementSyncer(voodoo) or
        type(setPedDriveTo) ~= "function" or type(setPedDriveBy) ~= "function" or type(setPedWeaponShootingRate) ~= "function" or
        type(setPedWeaponAccuracy) ~= "function" or type(setPedTaskSequence) ~= "function" or
        type(getPedTaskSequenceProgress) ~= "function" or type(isPedDoingTask) ~= "function" then
        return triggerServerEvent("drivethru:hoodFailureActive", resourceRoot, failure.id, "api_or_vehicle_unavailable")
    end
    if type(killPedTask) == "function" then
        pcall(killPedTask, passenger, "primary", 3, false)
    end
    local escape = DRIVETHRU.chase.hoodFailure.escape
    local slowDrive = setPedDriveTo(driver, voodoo, Vector3(escape.target.x, escape.target.y, escape.target.z), escape.slowSpeed,
                                    escape.drivingMode, escape.drivingStyle)
    if slowDrive ~= true or not fadeScriptCamera(failure.cameraToken, true, DRIVETHRU.chase.hoodFailure.fadeDuration, 0, 0, 0) then
        return triggerServerEvent("drivethru:hoodFailureActive", resourceRoot, failure.id, "slow_drive_or_fade_refused")
    end
    rememberHoodFailureTimer(failure, setTimer(function()
        if state.hoodFailure ~= failure then
            return
        end
        local driveBy = DRIVETHRU.chase.hoodFailure.driveBy
        setPedWeaponAccuracy(passenger, driveBy.accuracy)
        setPedWeaponShootingRate(passenger, driveBy.shootRate)
        local accepted = setPedDriveBy(passenger, Vector3(driveBy.target.x, driveBy.target.y, driveBy.target.z), driveBy.abortRange,
                                       driveBy.style, driveBy.seatRHS, driveBy.frequency)
        if accepted ~= true then
            return triggerServerEvent("drivethru:hoodFailureActive", resourceRoot, failure.id, "driveby_refused")
        end
        traceCurrent("hood_driveby")
        triggerServerEvent("drivethru:hoodFailureActive", resourceRoot, failure.id, "active", "slow_drive=true driveby=true")
        failure.deathAccepted = {}
        failure.deathTaskObserved = {}
        failure.deathReported = {}
        local function dispatchDeath(name)
            local ped = failure.entities[name]
            local accepted = isElement(ped) and isElementStreamedIn(ped) and isElementSyncer(ped) and
                                 setPedTaskSequence(ped, {{task = "die"}}, false) == true
            failure.deathAccepted[name] = accepted
            if not accepted then
                triggerServerEvent("drivethru:hoodFailureActive", resourceRoot, failure.id, "die_refused", name)
            end
        end
        local deathMonitor
        deathMonitor = rememberHoodFailureTimer(failure, setTimer(function()
            if state.hoodFailure ~= failure then
                return
            end
            for _, name in ipairs({"mate1", "mate2"}) do
                local ped = failure.entities[name]
                if failure.deathAccepted[name] and not failure.deathReported[name] and isElement(ped) and isElementStreamedIn(ped) and
                    isElementSyncer(ped) then
                    local sequenceIndex = getPedTaskSequenceProgress(ped)
                    local taskName = getObservedDeathTask(ped)
                    if sequenceIndex >= 0 or taskName then
                        failure.deathTaskObserved[name] = taskName or ("sequence:" .. tostring(sequenceIndex))
                    end
                    local health = getElementHealth(ped)
                    if failure.deathTaskObserved[name] and type(health) == "number" then
                        failure.deathReported[name] = true
                        triggerServerEvent("drivethru:hoodFailureDeathObserved", resourceRoot, failure.id, name, health,
                                           failure.deathTaskObserved[name])
                    end
                end
            end
            if failure.deathReported.mate1 and failure.deathReported.mate2 and isTimer(deathMonitor) then
                killTimer(deathMonitor)
            end
        end, DRIVETHRU.chase.monitorInterval, 0))
        traceCurrent("hood_deaths")
        rememberHoodFailureTimer(failure, setTimer(dispatchDeath, DRIVETHRU.chase.hoodFailure.mate1DeathDelay, 1, "mate1"))
        rememberHoodFailureTimer(failure, setTimer(dispatchDeath, DRIVETHRU.chase.hoodFailure.mate2DeathDelay, 1, "mate2"))
    end, DRIVETHRU.chase.hoodFailure.revealDelay, 1))
end)

addEvent("drivethru:hoodFailureEscape", true)
addEventHandler("drivethru:hoodFailureEscape", resourceRoot, function(failureId, entities)
    local failure = state.hoodFailure
    if source ~= resourceRoot or not failure or failure.id ~= tonumber(failureId) or type(entities) ~= "table" then
        return
    end
    state.stage = "hood_failure_escape"
    local escape = DRIVETHRU.chase.hoodFailure.escape
    if not isElement(entities.ballas_driver) or not isElement(entities.voodoo) or
        setPedDriveTo(entities.ballas_driver, entities.voodoo, Vector3(escape.target.x, escape.target.y, escape.target.z), escape.fastSpeed,
                      escape.drivingMode, escape.drivingStyle) ~= true then
        return triggerServerEvent("drivethru:hoodFailureEscapeBlack", resourceRoot, failure.id, "drive_refused")
    end
    rememberHoodFailureTimer(failure, setTimer(function()
        if state.hoodFailure ~= failure or
            not fadeScriptCamera(failure.cameraToken, false, DRIVETHRU.chase.hoodFailure.fadeDuration, 0, 0, 0) then
            return triggerServerEvent("drivethru:hoodFailureEscapeBlack", resourceRoot, failure.id, "fade_refused")
        end
        local fadeStartedAt = getTickCount()
        local fadeTimer
        fadeTimer = rememberHoodFailureTimer(failure, setTimer(function()
            if state.hoodFailure ~= failure then
                return
            end
            local queried, fading = pcall(isScriptCameraFading, failure.cameraToken)
            if not queried then
                killTimer(fadeTimer)
                return triggerServerEvent("drivethru:hoodFailureEscapeBlack", resourceRoot, failure.id, "fade_query_refused")
            end
            if fading == false then
                killTimer(fadeTimer)
                triggerServerEvent("drivethru:hoodFailureEscapeBlack", resourceRoot, failure.id, "black")
            elseif getTickCount() - fadeStartedAt >= DRIVETHRU.chase.taskActivationTimeout then
                killTimer(fadeTimer)
                triggerServerEvent("drivethru:hoodFailureEscapeBlack", resourceRoot, failure.id, "fade_timeout")
            end
        end, DRIVETHRU.chase.monitorInterval, 0))
    end, DRIVETHRU.chase.hoodFailure.postDeathDrive, 1))
end)

addEvent("drivethru:hoodFailureRestore", true)
addEventHandler("drivethru:hoodFailureRestore", resourceRoot, function(failureId)
    local failure = state.hoodFailure
    if source ~= resourceRoot or not failure or failure.id ~= tonumber(failureId) then
        return
    end
    state.stage = "hood_failure_restore"
    local x, y, z = getElementPosition(localPlayer)
    pcall(enginePreloadWorldArea, Vector3(x, y, z), "all")
    rememberHoodFailureTimer(failure, setTimer(function()
        if state.hoodFailure ~= failure then
            return
        end
        local released = clearHoodFailure("restored", true)
        setCameraTarget(localPlayer)
        fadeCamera(true, DRIVETHRU.chase.hoodFailure.fadeDuration)
        traceCurrent("failure_restore")
        triggerServerEvent("drivethru:hoodFailureRestored", resourceRoot, failure.id, released and "restored" or "release_failed")
    end, DRIVETHRU.chase.hoodFailure.loadSceneWait, 1))
end)

addEvent("drivethru:footCombat", true)
addEventHandler("drivethru:footCombat", resourceRoot, function(entities, reason)
    if source ~= resourceRoot or not state.active or state.stage ~= "chase" or type(entities) ~= "table" then
        return
    end
    state.footCombat = true
    traceCurrent("foot_combat")
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
    outputDebugString("[drive-thru] Chase checkpoint complete; handing off to the SWEET3 return drive")
end)

addEvent("drivethru:returnDriveStarted", true)
addEventHandler("drivethru:returnDriveStarted", resourceRoot, function(phase, entities, suppressInstruction)
    local profile = type(phase) == "string" and DRIVETHRU.returnTrip[phase] or nil
    if source ~= resourceRoot or not state.active or not profile or type(entities) ~= "table" then
        return
    end
    state.returnPhase = phase
    state.stage = profile.stage
    traceCurrent(phase == "grove" and "return_grove" or "return_smoke")
    state.vehicle = entities.vehicle or state.vehicle
    for _, name in ipairs({"smoke", "sweet", "ryder"}) do
        state.actors[name] = entities[name] or state.actors[name]
    end
    clearPursuitNavigation()
    callMissionTextApi("clearMissionHelp")
    if getPedOccupiedVehicle(localPlayer) == state.vehicle and getPedOccupiedVehicleSeat(localPlayer) == 0 then
        showReturnDestinationNavigation(phase)
        printMissionText(profile.instruction, 6000)
    else
        showVehicleNavigation()
        if suppressInstruction ~= true then
            printMissionText("TW2_X", 6000)
        end
    end
end)

addEvent("drivethru:vehicleReminderFinished", true)
addEventHandler("drivethru:vehicleReminderFinished", resourceRoot, function(stage)
    local matchingStage = state.stage == stage or (state.stage == "return_car" and stage == "drive")
    if source ~= resourceRoot or not state.active or not matchingStage or getPedOccupiedVehicle(localPlayer) == state.vehicle then
        return
    end
    local returning = stage == "return_grove_drive" or stage == "return_smoke_drive"
    printMissionText("TW2_X", returning and 6000 or 3000)
end)

addEvent("drivethru:returnScenePrepare", true)
addEventHandler("drivethru:returnScenePrepare", resourceRoot, function(sceneId, phase, entities)
    local trip = type(phase) == "string" and DRIVETHRU.returnTrip[phase] or nil
    if source ~= resourceRoot or not state.active or not trip or type(entities) ~= "table" then
        return
    end
    clearAudio("return_scene_prepare")
    destroyNavigation()
    clearReturnScene("replaced", false)
    state.returnPhase = phase
    state.stage = "return_" .. phase .. "_scene_prepare"
    traceCurrent(phase == "grove" and "grove_scene" or "smoke_scene")
    state.vehicle = entities.vehicle or state.vehicle
    for _, name in ipairs({"smoke", "sweet", "ryder"}) do
        state.actors[name] = entities[name] or state.actors[name]
    end
    local required = {"acquireScriptCamera", "releaseScriptCamera", "isScriptCameraLeaseActive", "resetScriptCamera",
                      "setScriptCameraWidescreen", "setScriptCameraPersist", "setScriptCameraFixed", "moveScriptCamera",
                      "trackScriptCamera", "fadeScriptCamera"}
    for _, name in ipairs(required) do
        if type(_G[name]) ~= "function" then
            return triggerServerEvent("drivethru:returnSceneReady", resourceRoot, sceneId, "api_unavailable", name)
        end
    end
    if trip.scene.vehicleViewMode and type(setCameraViewMode) == "function" then
        setCameraViewMode(trip.scene.vehicleViewMode)
    end
    local acquired, token = pcall(acquireScriptCamera, true)
    if not acquired or not token then
        return triggerServerEvent("drivethru:returnSceneReady", resourceRoot, sceneId, "camera_acquire_refused", tostring(token))
    end
    local scene = {id = sceneId, phase = phase, cameraToken = token, departureTimers = {}}
    state.returnScene = scene
    local camera = trip.scene.camera
    local ready = resetScriptCamera(token) and setScriptCameraWidescreen(token, true)
    if ready and camera.fixed then
        ready = setScriptCameraFixed(token, Vector3(camera.fixed.position.x, camera.fixed.position.y, camera.fixed.position.z),
                                     Vector3(camera.fixed.target.x, camera.fixed.target.y, camera.fixed.target.z), Vector3(0, 0, 0), true)
    elseif ready then
        ready = setScriptCameraPersist(token, true, true) and
                    moveScriptCamera(token, Vector3(camera.move.from.x, camera.move.from.y, camera.move.from.z),
                                     Vector3(camera.move.to.x, camera.move.to.y, camera.move.to.z), camera.move.duration, true) and
                    trackScriptCamera(token, Vector3(camera.track.from.x, camera.track.from.y, camera.track.from.z),
                                      Vector3(camera.track.to.x, camera.track.to.y, camera.track.to.z), camera.track.duration, true)
    end
    if not ready then
        clearReturnScene("camera_setup_refused", false)
        return triggerServerEvent("drivethru:returnSceneReady", resourceRoot, sceneId, "camera_setup_refused")
    end
    scene.leaseTimer = setTimer(function()
        if state.returnScene ~= scene then
            return
        end
        if not isScriptCameraLeaseActive(scene.cameraToken) then
            triggerServerEvent("drivethru:returnSceneLeaseLost", resourceRoot, scene.id)
            clearReturnScene("lease_lost", false)
        end
    end, 100, 0)
    -- A successful native setter only proves dispatch. Observe the rendered
    -- matrix before the server starts dialogue so zeroed scriptable vectors
    -- can never masquerade as a valid camera scene.
    scene.cameraReadyTimer = setTimer(function()
        if state.returnScene ~= scene or not isScriptCameraLeaseActive(scene.cameraToken) then
            return
        end
        scene.cameraReadyTimer = nil
        local px, py, pz, tx, ty, tz = getCameraMatrix()
        local expectedPosition = camera.fixed and camera.fixed.position or camera.move.from
        local expectedTarget = camera.fixed and camera.fixed.target or camera.track.from
        local positionError = getDistanceBetweenPoints3D(px, py, pz, expectedPosition.x, expectedPosition.y, expectedPosition.z)
        local targetError = getDistanceBetweenPoints3D(tx, ty, tz, expectedTarget.x, expectedTarget.y, expectedTarget.z)
        local details = ("position=(%.4f,%.4f,%.4f) target=(%.4f,%.4f,%.4f) error=%.3f/%.3f"):format(
                            px, py, pz, tx, ty, tz, positionError, targetError)
        outputDebugString(("[drive-thru] %s return camera observed: %s"):format(phase, details))
        if positionError > DRIVETHRU.returnTrip.cameraObservationTolerance or
            targetError > DRIVETHRU.returnTrip.cameraObservationTolerance then
            clearReturnScene("camera_observation_failed", false)
            return triggerServerEvent("drivethru:returnSceneReady", resourceRoot, scene.id, "camera_observation_failed", details)
        end
        triggerServerEvent("drivethru:returnSceneReady", resourceRoot, scene.id, "ready", details)
    end, DRIVETHRU.returnTrip.cameraObservationDelay, 1)
end)

addEvent("drivethru:returnSceneLookAt", true)
addEventHandler("drivethru:returnSceneLookAt", resourceRoot, function(sceneId, actorName, duration)
    local scene, actor = state.returnScene, state.actors[actorName]
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) or not isElement(actor) or type(setPedLookAt) ~= "function" then
        return
    end
    local x, y, z = getElementPosition(actor)
    setPedLookAt(localPlayer, Vector3(x, y, z + 0.7), tonumber(duration) or 15000, actor)
end)

addEvent("drivethru:returnSceneSkippable", true)
addEventHandler("drivethru:returnSceneSkippable", resourceRoot, function(sceneId)
    local scene = state.returnScene
    if source == resourceRoot and scene and scene.id == tonumber(sceneId) then
        scene.skippable = true
    end
end)

addEvent("drivethru:returnSceneVectorCamera", true)
addEventHandler("drivethru:returnSceneVectorCamera", resourceRoot, function(sceneId)
    local scene = state.returnScene
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) or not isScriptCameraLeaseActive(scene.cameraToken) then
        return
    end
    local camera = DRIVETHRU.returnTrip[scene.phase].scene.camera
    local ready = resetScriptCamera(scene.cameraToken) and setScriptCameraPersist(scene.cameraToken, true, true) and
                      moveScriptCamera(scene.cameraToken, Vector3(camera.move.from.x, camera.move.from.y, camera.move.from.z),
                                       Vector3(camera.move.to.x, camera.move.to.y, camera.move.to.z), camera.move.duration, true) and
                      trackScriptCamera(scene.cameraToken, Vector3(camera.track.from.x, camera.track.from.y, camera.track.from.z),
                                        Vector3(camera.track.to.x, camera.track.to.y, camera.track.to.z), camera.track.duration, true)
    if not ready then
        triggerServerEvent("drivethru:returnSceneLeaseLost", resourceRoot, scene.id)
    end
end)

addEvent("drivethru:returnSceneDepartures", true)
addEventHandler("drivethru:returnSceneDepartures", resourceRoot, function(sceneId, phase, entities)
    local scene = state.returnScene
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) or scene.phase ~= phase or type(entities) ~= "table" then
        return
    end
    if type(setPedTaskSequence) ~= "function" or type(getPedTaskSequenceProgress) ~= "function" then
        return triggerServerEvent("drivethru:returnSceneDeparturesReady", resourceRoot, scene.id, "api_unavailable")
    end
    scene.departureRequestedAt = getTickCount()
    scene.departureObserved = {}
    scene.departureAccepted = {}
    local profile = DRIVETHRU.returnTrip[phase].scene
    local failed = false
    local function reportFailure(result, details)
        if failed or state.returnScene ~= scene then
            return
        end
        failed = true
        triggerServerEvent("drivethru:returnSceneDeparturesReady", resourceRoot, scene.id, result, details)
    end
    local function assignDeparture(departure)
        if failed or state.returnScene ~= scene then
            return
        end
        local ped = state.actors[departure.actor]
        if not isElement(ped) or not isElement(state.vehicle) or not isElementStreamedIn(ped) or not isElementSyncer(ped) or
            not applyActorPolicies(ped) then
            return reportFailure("not_ready", departure.actor)
        end
        local accepted = setPedTaskSequence(ped, {
            {task = "leave_car", vehicle = state.vehicle},
            {
                task = "go_to",
                x = departure.target.x,
                y = departure.target.y,
                z = departure.target.z,
                movement = "walk",
                radius = 0.5,
                slowdownRadius = 2.0,
                timeout = departure.timeout,
            },
        }, false)
        if accepted ~= true then
            return reportFailure("refused", departure.actor)
        end
        scene.departureAccepted[departure.actor] = true
        outputDebugString(("[drive-thru] %s return sequence accepted for %s"):format(phase, departure.actor))
    end
    for _, departure in ipairs(profile.departures) do
        if departure.delay > 0 then
            scene.departureTimers[#scene.departureTimers + 1] = setTimer(assignDeparture, departure.delay, 1, departure)
        else
            assignDeparture(departure)
        end
    end
    scene.departureMonitor = setTimer(function()
        if failed or state.returnScene ~= scene then
            return
        end
        local allActive = true
        local details = {}
        for _, departure in ipairs(profile.departures) do
            local ped = state.actors[departure.actor]
            local index = scene.departureAccepted[departure.actor] and isElement(ped) and getPedTaskSequenceProgress(ped) or -1
            if index >= 0 then
                scene.departureObserved[departure.actor] = true
            end
            allActive = allActive and scene.departureObserved[departure.actor] == true
            details[#details + 1] = ("%s=%s/%s"):format(departure.actor, tostring(scene.departureAccepted[departure.actor] == true),
                                                         tostring(index))
        end
        if allActive then
            killTimer(scene.departureMonitor)
            scene.departureMonitor = nil
            triggerServerEvent("drivethru:returnSceneDeparturesReady", resourceRoot, scene.id, "active", table.concat(details, ","))
        elseif getTickCount() - scene.departureRequestedAt >= DRIVETHRU.returnTrip.departureActivationTimeout then
            killTimer(scene.departureMonitor)
            scene.departureMonitor = nil
            reportFailure("timeout", table.concat(details, ","))
        end
    end, 100, 0)
end)

addEvent("drivethru:returnSceneRelease", true)
addEventHandler("drivethru:returnSceneRelease", resourceRoot, function(sceneId, skipped)
    local scene = state.returnScene
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) then
        return
    end
    scene.skippable = false
    clearAudio(skipped and "return_scene_skipped" or "return_scene_complete")
    if skipped then
        local duration = DRIVETHRU.returnTrip.skipFadeOutDuration
        if not isScriptCameraLeaseActive(scene.cameraToken) or not fadeScriptCamera(scene.cameraToken, false, duration, 0, 0, 0) then
            clearReturnScene("skip_fade_refused", false)
            return triggerServerEvent("drivethru:returnSceneReleased", resourceRoot, scene.id, "release_failed")
        end
        scene.releaseTimer = setTimer(function()
            local released = clearReturnScene("skipped", true)
            triggerServerEvent("drivethru:returnSceneReleased", resourceRoot, scene.id, released and "released" or "release_failed")
        end, math.floor(duration * 1000 + DRIVETHRU.returnTrip.skipBlackHold), 1)
    else
        local released = clearReturnScene("completed", false)
        triggerServerEvent("drivethru:returnSceneReleased", resourceRoot, scene.id, released and "released" or "release_failed")
    end
end)

addEvent("drivethru:returnSceneReveal", true)
addEventHandler("drivethru:returnSceneReveal", resourceRoot, function()
    if source == resourceRoot and state.active then
        fadeCamera(true, DRIVETHRU.returnTrip.skipFadeOutDuration)
    end
end)

addEvent("drivethru:passed", true)
addEventHandler("drivethru:passed", resourceRoot, function(reward, tune)
    if source ~= resourceRoot or not state.active then
        return
    end
    state.stage = "complete"
    traceCurrent("mission_end")
    clearAudio("mission_passed")
    clearReturnScene("mission_passed", false)
    destroyNavigation()
    callMissionTextApi("showMissionBigText", "M_PASSS", DRIVETHRU.returnTrip.completionDisplayDuration, 1, tonumber(reward) or 200)
    local ok, played = pcall(playMissionPassedTune, tonumber(tune) or 1)
    outputDebugString(("[drive-thru] Mission-passed tune result=%s reward=$%d"):format(tostring(ok and played == true),
                                                                                       tonumber(reward) or 200))
end)

addEvent("drivethru:failed", true)
addEventHandler("drivethru:failed", resourceRoot, function(textKey, reason)
    if source ~= resourceRoot or not state.active then
        return
    end
    state.stage = "failed"
    if type(DRIVETHRU_TRACE) == "table" then
        DRIVETHRU_TRACE.fail("mission_failed")
    end
    if type(textKey) == "string" then
        printMissionText(textKey, 2000)
    end
    clearAudio("mission_failed")
    clearReturnScene("mission_failed", false)
    clearHoodFailure("mission_failed", false)
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

addEventHandler("onClientKey", root, function(button, pressed)
    local scene = state.returnScene
    if pressed and (button == "space" or button == "enter") and scene and scene.skippable and not scene.skipRequested then
        scene.skipRequested = true
        triggerServerEvent("drivethru:returnSceneSkipRequest", resourceRoot, scene.id)
        cancelEvent()
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
    if type(renderScriptImportantArea) == "function" then
        if state.navigation == "destination" then
            local destination = DRIVETHRU.destination
            renderScriptImportantArea(Vector3(destination.x, destination.y, destination.z), destination.radiusX, destination.radiusY, 1)
        elseif state.navigation == "return_destination" and state.returnPhase then
            local destination = DRIVETHRU.returnTrip[state.returnPhase].destination
            local localId = state.returnPhase == "grove" and 2 or 3
            renderScriptImportantArea(Vector3(destination.x, destination.y, destination.z), destination.radiusX, destination.radiusY, localId)
        end
    end
end)

state.arrivalTimer = setTimer(function()
    if not state.active or not isElement(state.vehicle) or getPedOccupiedVehicle(localPlayer) ~= state.vehicle or
        getPedOccupiedVehicleSeat(localPlayer) ~= 0 then
        return
    end
    if state.returnPhase and state.stage == DRIVETHRU.returnTrip[state.returnPhase].stage then
        local x, y, z = getElementPosition(state.vehicle)
        local destination = DRIVETHRU.returnTrip[state.returnPhase].destination
        if math.abs(x - destination.x) <= destination.radiusX and math.abs(y - destination.y) <= destination.radiusY and
            math.abs(z - destination.z) <= destination.radiusZ and type(isVehicleOnAllWheels) == "function" then
            local ok, onAllWheels = pcall(isVehicleOnAllWheels, state.vehicle)
            if ok and onAllWheels == true then
                triggerServerEvent("drivethru:returnArrivalReport", resourceRoot, state.returnPhase, true)
            end
        end
        return
    end
    if state.stage ~= "drive" then
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
