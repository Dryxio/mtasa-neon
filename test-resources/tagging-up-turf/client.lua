local state = {
    active = false,
    stage = nil,
    vehicle = nil,
    sweet = nil,
    demoTag = nil,
    leader = nil,
    tagProgress = {},
    completedTags = {},
    destination = nil,
    marker = nil,
    importantArea = nil,
    blip = nil,
    tagBlips = {},
    navigationMode = nil,
    rooftopTagRevealed = false,
    stageStarted = 0,
    lastVehicleReport = 0,
    arrivalGate = nil,
    lastArrivalAcquireAttempt = 0,
    allWheelsMismatchStage = nil,
    allWheelsPassedStage = nil,
    fileCutscene = nil,
    introScene = nil,
    demoLeave = nil,
    demoWalk = nil,
    demoShoot = nil,
    demoSequence = nil,
    demoEnter = nil,
    demoScene = nil,
    demoAudioPreload = nil,
    ballasDeparture = nil,
    ballasWanderPed = nil,
    ballasGangScene = nil,
    ballasEncounter = nil,
    ballasEncounterAudioPreload = nil,
    lastBallasGangTriggerReport = 0,
    vehiclePlayback = nil,
    vehicleRecordingPreloaded = false,
    postRoofScene = nil,
    finalScene = nil,
    transitionAudio = nil,
    lastOffscreenStorageReport = 0,
    vehiclePlayerOnlyLocked = false,
    greenwoodNativeLogMode = nil,
    storyProtectionLogged = false,
    missionTextReady = false,
    missionTextTimers = {},
    nativeTagHelpPhase = 0,
    nativeTagHelpStarted = 0,
    nativeHelpFlags = {},
    checkpointGroundProbeToken = nil,
    missionPassedTunePlayed = false,
    traceStarted = false,
    traceDemoTagActive = false,
    traceCurrentStep = nil,
}

local TAG_PAINT_ALPHA_DATA = "tagup.paintAlpha"
local getActiveTags
local nearestActiveTag
local startBallasEncounterAudioPreload

local function killMissionTextTimers()
    for _, timer in ipairs(state.missionTextTimers) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    state.missionTextTimers = {}
end

local function callMissionTextApi(name, ...)
    local api = _G[name]
    if type(api) ~= "function" then
        outputDebugString(("[tagging-up-turf] Native mission-text API unavailable: %s"):format(name), 1)
        return false
    end
    local ok, result = pcall(api, ...)
    if not ok or result ~= true then
        outputDebugString(("[tagging-up-turf] Native mission-text call refused: %s (%s)"):format(name, tostring(result)), 1)
        return false
    end
    return true
end

local function ensureMissionText()
    if state.missionTextReady then
        return true
    end
    state.missionTextReady = callMissionTextApi("acquireMissionText", "SWEET1")
    return state.missionTextReady
end

local function printMissionText(key, duration)
    return ensureMissionText() and callMissionTextApi("showMissionText", key, duration, 1)
end

local function printMissionHelp(key, permanent)
    return ensureMissionText() and callMissionTextApi("showMissionHelp", key, permanent == true)
end

local function scheduleMissionText(stage, delay, callback)
    local timer = setTimer(function(expectedStage)
        if state.active and state.stage == expectedStage and state.missionTextReady then
            callback()
        end
    end, delay, 1, stage)
    table.insert(state.missionTextTimers, timer)
end

local function beginMissionStageText(stage)
    killMissionTextTimers()
    state.nativeTagHelpPhase = 0
    state.nativeTagHelpStarted = getTickCount()
    state.nativeHelpFlags = {}

    if not ensureMissionText() then
        return
    end
    callMissionTextApi("clearMissionHelp")

    if stage == "enter_car" then
        printMissionText("SWE1_A", 7000)
    elseif stage == "drive_idlewood" then
        printMissionText("HOOD3_A", 5000)
    elseif stage == "tags_idlewood" then
        -- SWE1_CB is printed by the synchronized mission-audio cue once SCRIPT
        -- reports the event loaded on every participant.
    elseif stage == "return_car" or stage == "return_after_roof" then
        printMissionText("SWE1_S", 6000)
    elseif stage == "drive_ballas" then
        printMissionText("HOOD3_B", 5000)
    elseif stage == "tags_ballas" then
        scheduleMissionText(stage, 2500, function()
            printMissionText("SWE1_M", 6000)
            startBallasEncounterAudioPreload()
        end)
    elseif stage == "rooftop" then
        printMissionText("SWE1_Z", 5000)
        scheduleMissionText(stage, 2500, function()
            printMissionText("SWE1_M", 6000)
        end)
    elseif stage == "drive_home" then
        -- SWEX_AH and its follow-up are owned by transition mission audio.
    elseif stage == "failed" then
        callMissionTextApi("clearMissionTexts")
        callMissionTextApi("showMissionBigText", "M_FAIL", 5000, 1)
    elseif stage == "complete" then
        callMissionTextApi("clearMissionTexts")
        callMissionTextApi("showMissionBigText", "M_PASSS", 5000, 1, 200)
    end
end

local function clearTransitionAudio(reason)
    local audio = state.transitionAudio
    if not audio then
        return
    end
    state.transitionAudio = nil
    for _, timer in ipairs({audio.loadTimer, audio.loadGuardTimer, audio.finishTimer, audio.finishGuardTimer}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    if audio.handle and type(releaseMissionAudio) == "function" then
        local ok, released = pcall(releaseMissionAudio, audio.handle)
        outputDebugString(("[tagging-up-turf] Transition audio #%d released=%s reason=%s"):format(
                              audio.id, tostring(ok and released ~= false), tostring(reason or "cleanup")))
    end
end

local function reportTransitionAudio(audio, eventName, result, details)
    if state.transitionAudio ~= audio or audio.reported then
        return
    end
    if eventName == "tagup:transitionAudioResult" then
        audio.reported = true
    end
    triggerServerEvent(eventName, resourceRoot, audio.id, result, details)
end

addEvent("tagup:transitionAudioPrepare", true)
addEventHandler("tagup:transitionAudioPrepare", resourceRoot, function(audioId, purpose, profile)
    if source ~= resourceRoot or not state.active or type(audioId) ~= "number" or type(profile) ~= "table" or type(profile.event) ~= "number" then
        return
    end
    clearTransitionAudio("replaced")
    if type(requestMissionAudio) ~= "function" or type(isMissionAudioLoaded) ~= "function" or type(playMissionAudio) ~= "function" or
        type(isMissionAudioFinished) ~= "function" or type(releaseMissionAudio) ~= "function" then
        triggerServerEvent("tagup:transitionAudioReady", resourceRoot, audioId, "api_unavailable", "native mission-audio API unavailable")
        return
    end

    local requested, handle = pcall(requestMissionAudio, profile.event)
    if not requested or not handle then
        triggerServerEvent("tagup:transitionAudioReady", resourceRoot, audioId, "request_refused", tostring(handle))
        return
    end
    local audio = {id = audioId, purpose = purpose, profile = profile, handle = handle, requestedAt = getTickCount()}
    state.transitionAudio = audio
    audio.loadGuardTimer = setTimer(function()
        if state.transitionAudio == audio and not audio.ready then
            triggerServerEvent("tagup:transitionAudioReady", resourceRoot, audio.id, "load_timeout", tostring(audio.profile.event))
        end
    end, TAGUP.transitionAudio.loadTimeout, 1)
    audio.loadTimer = setTimer(function()
        if state.transitionAudio ~= audio or audio.ready then
            return
        end
        local ok, loaded = pcall(isMissionAudioLoaded, audio.handle)
        if ok and loaded == true then
            audio.ready = true
            if isTimer(audio.loadGuardTimer) then
                killTimer(audio.loadGuardTimer)
            end
            triggerServerEvent("tagup:transitionAudioReady", resourceRoot, audio.id, "ready",
                               ("event=%d load=%dms"):format(audio.profile.event, getTickCount() - audio.requestedAt))
        end
    end, 50, 0)
end)

addEvent("tagup:transitionAudioStart", true)
addEventHandler("tagup:transitionAudioStart", resourceRoot, function(audioId)
    local audio = state.transitionAudio
    if source ~= resourceRoot or not audio or audio.id ~= tonumber(audioId) or not audio.ready or audio.started then
        return
    end
    if isTimer(audio.loadTimer) then
        killTimer(audio.loadTimer)
    end
    local ok, played = pcall(playMissionAudio, audio.handle)
    if not ok or played ~= true then
        return reportTransitionAudio(audio, "tagup:transitionAudioResult", "play_refused", tostring(played))
    end
    audio.started = getTickCount()
    printMissionText(audio.profile.key, audio.profile.duration)
    audio.finishGuardTimer = setTimer(function()
        if state.transitionAudio == audio and not audio.reported then
            reportTransitionAudio(audio, "tagup:transitionAudioResult", "finish_timeout", tostring(audio.profile.event))
        end
    end, TAGUP.transitionAudio.finishTimeout, 1)
    audio.finishTimer = setTimer(function()
        if state.transitionAudio ~= audio or audio.reported then
            return
        end
        local queried, finished = pcall(isMissionAudioFinished, audio.handle)
        if queried and finished == true then
            if audio.profile.followUp then
                printMissionText(audio.profile.followUp, audio.profile.followUpDuration)
            elseif audio.purpose == "vehicle_reminder" then
                printMissionText("SWE1_S", 6000)
            end
            reportTransitionAudio(audio, "tagup:transitionAudioResult", "finished",
                                  ("event=%d play=%dms"):format(audio.profile.event, getTickCount() - audio.started))
        end
    end, 50, 0)
end)

addEvent("tagup:transitionAudioCancel", true)
addEventHandler("tagup:transitionAudioCancel", resourceRoot, function(audioId, reason)
    if source == resourceRoot and state.transitionAudio and state.transitionAudio.id == tonumber(audioId) then
        clearTransitionAudio(reason)
    end
end)

local function updateNativeMissionHelp()
    if not state.active or not state.missionTextReady then
        return
    end

    local now = getTickCount()
    if state.stage == "tags_idlewood" then
        local tag, distance = nearestActiveTag()
        if tag and distance <= 6 then
            if state.nativeTagHelpPhase == 0 and now - state.nativeTagHelpStarted >= 1000 then
                if printMissionHelp("SWE1_D") then
                    state.nativeTagHelpPhase = 1
                    state.nativeTagHelpStarted = now
                end
            elseif state.nativeTagHelpPhase == 1 and now - state.nativeTagHelpStarted >= 5000 then
                if printMissionHelp("SWE1_F") then
                    state.nativeTagHelpPhase = 2
                end
            end
        end
    elseif state.stage == "tags_ballas" then
        local x, y = getElementPosition(localPlayer)
        if not state.nativeHelpFlags.respect and math.abs(x - 2353.30) <= 3 and math.abs(y + 1508.18) <= 3 then
            state.nativeHelpFlags.respect = printMissionHelp("SWE1_G")
            if state.nativeHelpFlags.respect and localPlayer == state.leader then
                triggerServerEvent("tagup:ballasRespectHelpShown", resourceRoot)
            end
        end
    elseif state.stage == "rooftop" then
        local x, y, z = getElementPosition(localPlayer)
        if not state.nativeHelpFlags.jump1 and
            ((math.abs(x - 2374.0) <= 1.4 and math.abs(y + 1534.1) <= 1.4 and math.abs(z - 23.0) <= 2.0) or
             (math.abs(x - 2352.3) <= 3 and math.abs(y + 1552.1) <= 3) or
             (math.abs(x - 2420.7) <= 3 and math.abs(y + 1572.1) <= 3)) then
            state.nativeHelpFlags.jump1 = printMissionHelp("JUMPH1")
        elseif not state.nativeHelpFlags.radar and math.abs(x - 2373.5) <= 3 and math.abs(y + 1547.2) <= 3 then
            state.nativeHelpFlags.radar = printMissionHelp("RADAR5")
        elseif not state.nativeHelpFlags.jump2 and math.abs(x - 2378.7546) <= 3 and math.abs(y + 1555.8896) <= 3 then
            state.nativeHelpFlags.jump2 = printMissionHelp("JUMPH2")
        end
    end
end

local function applyMissionActor(ped)
    if not isElement(ped) or getElementType(ped) ~= "ped" or type(setPedMissionActor) ~= "function" then
        return false
    end
    return setPedMissionActor(ped, getElementData(ped, TAGUP.missionActorData) == true)
end

local function applyStoryActorProtection(ped)
    if not isElement(ped) or getElementType(ped) ~= "ped" or type(setPedStoryProtected) ~= "function" then
        return false
    end
    local enabled = getElementData(ped, TAGUP.missionActorData) == true
    local applied = setPedStoryProtected(ped, enabled)
    if applied == true and enabled and not state.storyProtectionLogged then
        state.storyProtectionLogged = true
        outputDebugString("[tagging-up-turf] Sweet native story protection applied: neverTargeted/noCriticalHits/cannotDragOut/stayWhenJacked/noUpsideDownExit")
    end
    return applied
end

local function applyGreenwoodNativeState()
    local vehicle = state.vehicle
    if not isElement(vehicle) then
        return false
    end
    if type(setVehicleTyresCanBurst) ~= "function" or type(setVehicleDoorLockMode) ~= "function" then
        return false
    end
    local mode = state.vehiclePlayerOnlyLocked and TAGUP.vehicleDoorLock.playerOnly or TAGUP.vehicleDoorLock.unlocked
    local tyresApplied = type(getVehicleTyresCanBurst) == "function" and getVehicleTyresCanBurst(vehicle) == false or
                              setVehicleTyresCanBurst(vehicle, false)
    local lockApplied = type(getVehicleDoorLockMode) == "function" and getVehicleDoorLockMode(vehicle) == mode or
                            setVehicleDoorLockMode(vehicle, mode)
    if tyresApplied == true and lockApplied == true and state.greenwoodNativeLogMode ~= mode then
        state.greenwoodNativeLogMode = mode
        outputDebugString(("[tagging-up-turf] Greenwood native state applied: tyresCanBurst=false doorLockMode=%d plate=%s"):format(
                              mode, tostring(getVehiclePlateText(vehicle))))
    end
    return tyresApplied == true and lockApplied == true
end

local function tuneGreenwoodRadio()
    if state.active and isElement(state.vehicle) and getPedOccupiedVehicle(localPlayer) == state.vehicle then
        local tuned = setRadioChannel(TAGUP.bounceRadioChannel)
        outputDebugString(("[tagging-up-turf] Greenwood radio Bounce FM channel=%d applied=%s"):format(
                              TAGUP.bounceRadioChannel, tostring(tuned)))
    end
end

addEvent("tagup:checkpointGroundProbe", true)
addEventHandler("tagup:checkpointGroundProbe", resourceRoot, function(token, x, y, expectedGroundZ)
    if source ~= resourceRoot or type(token) ~= "number" or type(x) ~= "number" or type(y) ~= "number" or type(expectedGroundZ) ~= "number" then
        return
    end

    state.checkpointGroundProbeToken = token
    local startedAt = getTickCount()
    local function probeGround()
        if state.checkpointGroundProbeToken ~= token then
            return
        end

        local groundZ = getGroundPosition(x, y, expectedGroundZ + 10.0)
        if type(groundZ) == "number" and math.abs(groundZ - expectedGroundZ) <= 2.0 then
            state.checkpointGroundProbeToken = nil
            triggerServerEvent("tagup:checkpointGroundReady", resourceRoot, token, groundZ)
            return
        end

        if getTickCount() - startedAt >= 10000 then
            state.checkpointGroundProbeToken = nil
            triggerServerEvent("tagup:checkpointGroundReady", resourceRoot, token, false)
            return
        end
        setTimer(probeGround, 100, 1)
    end
    setTimer(probeGround, 100, 1)
end)

-- This is presentation copy as well as an execution map. Keeping the friendly
-- explanation beside the exact primitive lets footage show how much native work
-- is running without turning the overlay into an opcode spreadsheet.
local function defineTraceStep(id, title, category, primitive, explanation, detail, originalTask)
    return {
        id = id,
        title = title,
        category = category,
        primitive = primitive,
        explanation = explanation,
        detail = detail,
        originalTask = originalTask,
    }
end

local MISSION_TRACE_SEQUENCE = {
    defineTraceStep("mission_start", "The server opens an authoritative mission run", "SERVER AUTHORITY", "MISSION STATE",
                    "The server owns party membership, stage changes, failures and rewards while each client reports observable native work.",
                    "Lua orchestration · run token accepted"),
    defineTraceStep("file_cutscene", "GTA plays the original SWEET1A cutscene", "NATIVE CUTSCENE", "DAT / CUT / IFP + 739",
                    "Neon leases GTA's global camera and synchronizes native loading, playback, skipping and cleanup across the party.",
                    "Native verified · managed file-cutscene lifecycle"),
    defineTraceStep("intro_camera", "The world intro continues outside the cutscene file", "NATIVE CAMERA", "FIXED + VECTOR CAMERA",
                    "GTA camera primitives, mission audio and actor tasks rebuild the original SCM transition from SWE1_AA through SWE1_AE.",
                    "Native verified · fixed shot + 13 s vector track"),
    defineTraceStep("enter_car", "The party boards Sweet's Greenwood", "CO-OP BARRIER", "SERVER OCCUPANTS",
                    "The mission waits for the leader to drive and for every participant to be seated before shared progression continues.",
                    "Server validated · leader driving + party seated"),
    defineTraceStep("drive_idlewood", "The Greenwood reaches the exact Idlewood gate", "NATIVE PREDICATE", "09D0 · ALL WHEELS",
                    "The server combines the original four-metre SCM box with GTA's real wheel-contact predicate instead of approximating arrival.",
                    "SCM gate · vehicle anchored after validation"),
    defineTraceStep("leave_car", "Sweet leaves the Greenwood through GTA's task system", "NATIVE TASK", "05CD · TASK_LEAVE_CAR",
                    "A verified CTaskComplexLeaveCar runs underneath MTA's server-confirmed occupant lifecycle.",
                    "Native verified · synchronized vehicle lifecycle", "CTaskComplexLeaveCar"),
    defineTraceStep("go_to", "Sweet walks to the demonstration tag", "NATIVE TASK", "05D3 · TASK_GO_STRAIGHT_TO_COORD",
                    "GTA owns pathfinding, movement and stand-still completion; Lua supplies the original target, move state and timeout.",
                    "Native verified · Sweet walking", "CTaskComplexGoToPointAndStandStillTimed"),
    defineTraceStep("go_to_wait", "Lua observes GTA's active walk child", "NATIVE SEQUENCE", "0646 · GET_SEQUENCE_PROGRESS",
                    "The mission reads the native task hierarchy instead of estimating movement with a timer or teleport.",
                    "GET_SEQUENCE_PROGRESS · child index 1", "CTaskComplexUseSequence"),
    defineTraceStep("demo_setup", "Neon composes Sweet's complete spray demonstration", "NATIVE SEQUENCE",
                    "0615 / 0616 / 0618 / 063F · OPEN / CLOSE / PERFORM_SEQUENCE_TASK",
                    "Leave-car, go-to and shoot descriptors become one GTA-owned task sequence with native child ownership.",
                    "Open · append · close · perform · clear", "CTaskComplexSequence -> CTaskComplexUseSequence"),
    defineTraceStep("accuracy", "Sweet receives the original weapon accuracy", "NATIVE PED STATE", "02E2 · SET_CHAR_ACCURACY",
                    "The verified SCM setter updates the byte consumed directly by GTA's gun tasks.", "Native verified · value 90"),
    defineTraceStep("shoot_rate", "Sweet receives the original shooting rate", "NATIVE PED STATE", "07DD · SET_CHAR_SHOOT_RATE",
                    "GTA's weapon task reads a native rate byte rather than a Lua-side firing interval.", "Native verified · value 100"),
    defineTraceStep("shoot", "Sweet sprays the wall with native gun control", "NATIVE TASK", "0668 · TASK_SHOOT_AT_COORD",
                    "CTaskSimpleGunControl aims and fires the spray can at the SCM coordinate as part of the composed sequence.",
                    "Native verified · burst 5 · 15 s safety ceiling", "CTaskSimpleGunControl -> CTaskSimpleUseGun"),
    defineTraceStep("shoot_wait", "Lua observes GTA's active shooting child", "NATIVE SEQUENCE", "0646 · GET_SEQUENCE_PROGRESS",
                    "The sequence index proves GTA has advanced from walking into gun control before the mission reacts.",
                    "GET_SEQUENCE_PROGRESS · child index 2", "CTaskComplexUseSequence"),
    defineTraceStep("demo_tag", "Real spray hits advance the demonstration tag", "NATIVE GAMEPLAY", "0702 / CShotInfo",
                    "GTA detects spray-can impacts and applies its original eight-alpha tag progression before the server accepts the report.",
                    "Native spray progress · demonstration 0%"),
    defineTraceStep("demo_wait", "The task is cancelled at completion, then SCM waits", "SCM FLOW", "CLEAR TASK + WAIT 1000",
                    "Neon tears down the native sequence cleanly and preserves the original one-second story beat without blocking gameplay.",
                    "Native lifecycle + original SCM timing"),
    defineTraceStep("demo_camera", "A leased script camera frames Sweet's demonstration", "NATIVE CAMERA", "FIXED + MOVE / TRACK",
                    "Resource ownership and generation tokens protect GTA's one global camera from stale scene callbacks.",
                    "Native verified · fixed and vector primitives"),
    defineTraceStep("demo_audio_ar", "GTA loads and plays Sweet's approach dialogue", "NATIVE AUDIO", "03CF / 03D1 · SWE1_AR",
                    "A resource-owned mission-audio slot is preloaded, played once and observed until its natural native finish.",
                    "Native mission audio · preload / play / finish"),
    defineTraceStep("demo_checkout", "Sweet performs the graffiti checkout animation", "SYNCED ANIMATION", "0605 / 062E · GRAFFITI_CHKOUT",
                    "The synchronized animation runs to its real endpoint while the mission observes both actor and scene lifecycle.",
                    "Natural animation finish required"),
    defineTraceStep("demo_audio_ca", "GTA plays Sweet's checkout dialogue", "NATIVE AUDIO", "03CF / 03D1 · SWE1_CA",
                    "The second native audio cue shares the same guarded slot lifecycle and finishes before camera cleanup.",
                    "Native mission audio · preload / play / finish"),
    defineTraceStep("enter_passenger", "Sweet re-enters as Greenwood passenger", "NATIVE TASK", "05CA · TASK_ENTER_CAR_AS_PASSENGER",
                    "GTA handles approach and entry while MTA confirms the authoritative seat using its synchronized occupant lifecycle.",
                    "Native verified · SCM seat 0 maps to MTA seat 1", "CTaskComplexEnterCarAsPassenger"),
    defineTraceStep("idlewood_tags", "Native spray impacts cover both Idlewood tags", "NATIVE GAMEPLAY", "0702 / CShotInfo",
                    "Each real spray hit advances GTA's tag material, then the server validates and mirrors accepted byte progress.",
                    "Native spray progress · Idlewood 0%"),
    defineTraceStep("return_car", "The party regroups with Sweet", "CO-OP BARRIER", "SERVER OCCUPANTS",
                    "Shared mission state pauses until the active party is back together in the Greenwood.", "Server validated · party regroup"),
    defineTraceStep("drive_ballas", "The Greenwood reaches the Ballas territory gate", "NATIVE PREDICATE", "09D0 · ALL WHEELS",
                    "The original position box and GTA wheel contacts must both pass before the arrival scene can take ownership.",
                    "SCM gate · vehicle anchored after validation"),
    defineTraceStep("ballas_camera", "A native camera establishes the Ballas arrival", "NATIVE CAMERA", "FIXED + WIDESCREEN",
                    "Every participant crosses a readiness barrier before the resource-owned fixed shot and widescreen state are revealed.",
                    "Native verified · synchronized camera barrier"),
    defineTraceStep("ballas_leave", "CJ leaves the Greenwood through the native task", "NATIVE TASK", "05CD · TASK_LEAVE_CAR",
                    "The local player follows GTA's real leave-car behavior while the server waits for the synchronized occupant result.",
                    "Native verified · player vehicle lifecycle", "CTaskComplexLeaveCar"),
    defineTraceStep("ballas_audio_av", "GTA plays the Ballas departure line", "NATIVE AUDIO", "03CF / 03D1 · SWE1_AV",
                    "The cue is requested before camera timing, broadcast through the party barrier and observed to natural completion.",
                    "Native mission audio · event 37420"),
    defineTraceStep("ballas_wander", "GTA road AI drives Sweet away", "NATIVE VEHICLE AI", "05D2 · TASK_CAR_DRIVE_WANDER",
                    "CTaskComplexCarDriveWander owns the Greenwood route with the original speed and driving style while Sweet stays passenger.",
                    "Native verified · speed 20 · style 2", "CTaskComplexCarDriveWander"),
    defineTraceStep("ballas_wait", "SCM yields after the driving task starts", "SCM FLOW", "WAIT 1000",
                    "The original delay lets native vehicle AI establish control before Lua releases the departure scene.",
                    "Original SCM timing · no gameplay stall"),
    defineTraceStep("spawn_ballas", "The server creates a synchronized Ballas group", "SCM ADAPTER", "NETWORK PEDS + GTA TASK AI",
                    "MTA owns shared actors while their current syncer assigns native partner-chat, seek, combat and wander behavior.",
                    "Synchronized actors · native task brain"),
    defineTraceStep("ballas_chat", "Both Flats enter GTA's paired conversation behavior", "NATIVE TASK",
                    "0677 · TASK_CHAT_WITH_CHAR",
                    "GTA receives two reciprocal script-command events and keeps both PartnerChat tasks active through an observation barrier.",
                    "Native verified · both actors · ten consecutive samples", "CTaskComplexPartnerChat"),
    defineTraceStep("ballas_gang_camera", "A leased camera reveals the two Flats", "NATIVE CAMERA", "FIXED SHOT + SCM WAITS",
                    "The shot preserves the original half-second lead-in and skippable 6.5-second hold across the co-op party.",
                    "Native verified · WAIT 500 + skippable 6500"),
    defineTraceStep("ballas_tags", "Native spray impacts cover both Ballas tags", "NATIVE GAMEPLAY", "0702 / CShotInfo",
                    "GTA's spray system, not a scripted progress bar, drives both territory tags under server validation.",
                    "Native spray progress · Ballas 0%"),
    defineTraceStep("ballas_follow", "Both Flats repeatedly seek offsets around CJ", "NATIVE TASK SET",
                    "0A09 SHUT_CHAR_UP_FOR_SCRIPTED_SPEECH + 05BA TASK_STAND_STILL + 06A8 TASK_GOTO_CHAR_OFFSET",
                    "The syncer mutes scripted speech, clears the actors into StandStill, then dispatches GTA's repeated native offset sequence.",
                    "Native verified · two actors · repeated mission sequence",
                    "CTaskSimpleStandStill + CTaskComplexSeekEntityRadiusAngleOffset -> CTaskComplexUseSequence"),
    defineTraceStep("ballas_attack", "Both Flats switch to GTA's on-foot combat AI", "NATIVE TASK",
                    "05E2 · TASK_KILL_CHAR_ON_FOOT",
                    "Each synchronized actor receives GTA's original on-foot kill task against the authoritative mission leader.",
                    "Native verified · two attackers · leader target", "CTaskComplexKillPedOnFoot"),
    defineTraceStep("rooftop_tag", "Native spray impacts cover the rooftop tag", "NATIVE GAMEPLAY", "0702 / CShotInfo",
                    "The final wall uses the same resource-owned native tag path and survives stream-out through synchronized element state.",
                    "Native spray progress · rooftop 0%"),
    defineTraceStep("request_carrec", "Neon requests GTA's Greenwood recording", "NATIVE RECORDING", "07C0 · REQUEST RRR",
                    "The resource claims recording 207 and one of GTA's sixteen guarded playback slots.",
                    "Native verified · recording 207 requested"),
    defineTraceStep("load_carrec", "GTA streams the recording into its RRR buffer", "NATIVE RECORDING", "07C1 · HAS RRR LOADED",
                    "Playback cannot start until GTA confirms the native recording buffer is resident.",
                    "Native verified · waiting for streamed buffer"),
    defineTraceStep("start_playback", "GTA starts recorded Greenwood movement", "NATIVE RECORDING", "05EB · START CAR RECORDING",
                    "The vehicle syncer starts direct non-looped playback while normal MTA vehicle replication remains in place.",
                    "Native verified · syncer-owned start"),
    defineTraceStep("playback_wait", "The mission observes recording 207 to its endpoint", "NATIVE RECORDING", "060E · RRR ACTIVE",
                    "GTA owns every frame of the drive; Lua only checks native activity and validates the final position.",
                    "Native verified · natural endpoint required"),
    defineTraceStep("post_roof_preload", "GTA preloads the destination world in one direction", "NATIVE STREAMING", "0A0B · LOAD SCENE",
                    "The renderer-facing SCM primitive loads objects around the destination and updates GTA's timer around blocking work.",
                    "Native verified · heading converted to renderer radians"),
    defineTraceStep("post_roof_horn", "The Greenwood emits Sweet's two scripted horns", "NATIVE VEHICLE AUDIO", "09F7 · EVENT 1147",
                    "Each horn is reported through the streamed vehicle's own GTA audio entity rather than a detached sound effect.",
                    "Native verified · two vehicle-attached events"),
    defineTraceStep("post_roof_audio", "GTA plays Sweet's post-rooftop dialogue", "NATIVE AUDIO", "03CF / 03D1 · SWE1_BH",
                    "The mission holds its transition until the resource-owned native cue reaches a natural finish.",
                    "Native mission audio · event 37430"),
    defineTraceStep("post_roof_wander", "Surviving Flats return to GTA's wander behavior", "NATIVE TASK", "05DE · TASK_WANDER_STANDARD",
                    "The syncer releases encounter combat into CTaskComplexWanderStandard for every surviving actor.",
                    "Native verified · surviving Flats", "CTaskComplexWanderStandard"),
    defineTraceStep("return_after_roof", "The party regroups in the Greenwood", "CO-OP BARRIER", "SERVER OCCUPANTS",
                    "Authoritative progression waits for Sweet and every active participant before the drive home.",
                    "Server validated · party in vehicle"),
    defineTraceStep("drive_home", "The Greenwood reaches the exact Grove Street gate", "NATIVE PREDICATE", "09D0 · ALL WHEELS",
                    "The four-metre SCM region and GTA's strict wheel-contact counter protect the finale transition.",
                    "SCM gate · position + native contact state"),
    defineTraceStep("final_camera", "A native 18-second camera performs the Grove finale", "NATIVE CAMERA", "VECTOR MOVE / TRACK",
                    "The leased camera, seven SWE1_BN–BU cues, facial controllers and actor staging reproduce the original closing scene.",
                    "Native verified · synchronized finale lifecycle"),
    defineTraceStep("final_handshake", "CJ and Sweet perform the GANGS handshake", "SYNCED ANIMATION", "0605 / 062E · GANGS",
                    "Both synchronized animations must reach their natural endpoints after collision-safe restaging.",
                    "Natural finish observed for both actors"),
    defineTraceStep("final_walk", "Sweet turns and walks away through GTA's task system", "NATIVE TASK", "05D3 · TASK_GO_STRAIGHT_TO_COORD",
                    "The final actor exit uses native pathfinding and the original twenty-second timeout instead of scripted movement.",
                    "Native verified · walk · 20000 ms", "CTaskComplexGoToPointAndStandStillTimed"),
    defineTraceStep("mission_end", "The server commits mission completion", "SERVER AUTHORITY", "0394 · PASSED TUNE",
                    "The authoritative reward, native completion tune and full camera, actor, vehicle, model and clothing restoration close the run.",
                    "Mission passed · $200 reward · state restore"),
}

local STAGE_TRACE_STEP = {
    sweet1a = "file_cutscene",
    intro = "intro_camera",
    enter_car = "enter_car",
    drive_idlewood = "drive_idlewood",
    demo = "leave_car",
    tags_idlewood = "idlewood_tags",
    return_car = "return_car",
    drive_ballas = "drive_ballas",
    ballas_departure = "ballas_leave",
    tags_ballas = "spawn_ballas",
    rooftop = "rooftop_tag",
    return_after_roof = "return_after_roof",
    drive_home = "drive_home",
    final_scene = "final_camera",
    complete = "mission_end",
}

local function traceStart()
    if state.traceStarted or type(TAGUP_TRACE) ~= "table" or (isElement(state.leader) and state.leader ~= localPlayer) then
        return
    end
    state.traceStarted = TAGUP_TRACE.setSequence(MISSION_TRACE_SEQUENCE, {
        title = "TAGGING UP TURF",
        subtitle = "LEADER/SYNCER LIVE · SCM / NATIVE / LUA",
        live = true,
    })
    if state.traceStarted then
        TAGUP_TRACE.setCurrent("mission_start")
    end
end

local function traceCurrent(step, detail)
    traceStart()
    if state.traceStarted then
        if TAGUP_TRACE.setCurrent(step, detail) then
            state.traceCurrentStep = step
        end
    end
end

local function traceSkipTo(step)
    traceStart()
    if state.traceStarted and TAGUP_TRACE.skipTo(step, "DEBUG SKIP · next stage") then
        state.traceCurrentStep = step
    end
end

local function traceFail(detail)
    if state.traceStarted and state.traceCurrentStep then
        TAGUP_TRACE.fail(state.traceCurrentStep, "MISSION FAILED · " .. tostring(detail or "unknown reason"))
    end
end

local function traceFailAt(step, detail)
    traceStart()
    if state.traceStarted and TAGUP_TRACE.fail(step, "MISSION FAILED · " .. tostring(detail or "unknown reason")) then
        state.traceCurrentStep = step
    end
end

local function traceProgress(step, progress, detail)
    traceStart()
    if state.traceStarted then
        TAGUP_TRACE.setProgress(step, progress, detail)
    end
end

local function averageTagProgress(ids)
    local total = 0
    for _, id in ipairs(ids) do
        total = total + (tonumber(state.tagProgress[id]) or 0) / 255
    end
    return total / #ids
end

local function updateTraceTagStage()
    if state.stage == "tags_idlewood" then
        local progress = averageTagProgress({1, 2})
        traceProgress("idlewood_tags", progress, ("NATIVE SPRAY PROGRESS · Idlewood %d%%"):format(math.floor(progress * 100 + 0.5)))
    elseif state.stage == "tags_ballas" then
        local progress = averageTagProgress({3, 4})
        traceProgress("ballas_tags", progress, ("NATIVE SPRAY PROGRESS · Ballas %d%%"):format(math.floor(progress * 100 + 0.5)))
    elseif state.stage == "rooftop" then
        local progress = (tonumber(state.tagProgress[5]) or 0) / 255
        traceProgress("rooftop_tag", progress, ("NATIVE SPRAY PROGRESS · rooftop %d%%"):format(math.floor(progress * 100 + 0.5)))
    end
end

local function getGangTagGroupForStage()
    if state.stage == "tags_idlewood" then
        return "idlewood"
    elseif state.stage == "tags_ballas" then
        return "ballas"
    elseif state.stage == "rooftop" then
        return "rooftop"
    end
    return nil
end

local function shouldEnableGangTag(object, alpha)
    if not state.active then
        return false
    end

    local tagId = tonumber(getElementData(object, "tagup.tagId"))
    if not tagId then
        return object == state.demoTag and (state.stage == "demo" or alpha == 255)
    end

    if state.completedTags[tagId] or alpha == 255 then
        return true
    end

    local tag = tagupGetTag(tagId)
    return tag and tag.group == getGangTagGroupForStage()
end

local function applyGangTagState(object)
    if not isElement(object) or getElementType(object) ~= "object" then
        return
    end
    local alpha = tonumber(getElementData(object, TAG_PAINT_ALPHA_DATA))
    if type(acquireObjectGangTag) ~= "function" or type(setObjectGangTagProgress) ~= "function" then
        return
    end
    if not alpha or not shouldEnableGangTag(object, alpha) then
        if type(releaseObjectGangTag) == "function" then
            releaseObjectGangTag(object)
        end
        return
    end

    alpha = math.max(0, math.min(255, math.floor(alpha + 0.5)))
    local predictedAlpha = type(getObjectGangTagProgress) == "function" and getObjectGangTagProgress(object) or false
    if type(predictedAlpha) == "number" and alpha < predictedAlpha then
        return
    end
    if not setObjectGangTagProgress(object, alpha) then
        acquireObjectGangTag(object, alpha)
    end
end

local function refreshGangTagStates()
    for _, object in ipairs(getElementsByType("object", resourceRoot, true)) do
        if getElementData(object, TAG_PAINT_ALPHA_DATA) ~= false then
            applyGangTagState(object)
        end
    end
end

local function releaseGangTagStates()
    if type(releaseObjectGangTag) ~= "function" then
        return
    end
    for _, object in ipairs(getElementsByType("object", resourceRoot, true)) do
        releaseObjectGangTag(object)
    end
end

addEventHandler("onClientElementStreamIn", root, function()
    if getElementData(source, TAG_PAINT_ALPHA_DATA) ~= false then
        applyGangTagState(source)
    end
    if getElementType(source) == "ped" and getElementData(source, TAGUP.missionActorData) ~= nil then
        applyMissionActor(source)
        applyStoryActorProtection(source)
    elseif source == state.vehicle then
        applyGreenwoodNativeState()
    end
end)

addEventHandler("onClientElementDataChange", root, function(dataName)
    if dataName == TAGUP.missionActorData then
        applyMissionActor(source)
        applyStoryActorProtection(source)
    elseif dataName == TAG_PAINT_ALPHA_DATA then
        applyGangTagState(source)
        if state.active and state.stage == "demo" and not getElementData(source, "tagup.tagId") then
            local alpha = tonumber(getElementData(source, TAG_PAINT_ALPHA_DATA))
            if alpha and alpha > 0 then
                local progress = math.max(0, math.min(1, alpha / 255))
                if not state.traceDemoTagActive and state.traceCurrentStep ~= "demo_wait" then
                    state.traceDemoTagActive = true
                    traceCurrent("demo_tag")
                end
                traceProgress("demo_tag", progress,
                              ("NATIVE SPRAY PROGRESS · demo %d%%"):format(math.floor(progress * 100 + 0.5)))
            end
        end
    end
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    if type(acquireObjectGangTag) ~= "function" or type(setObjectGangTagProgress) ~= "function" then
        outputDebugString("[tagging-up-turf] native gang-tag API is unavailable", 1)
    else
        refreshGangTagStates()
    end
    if type(setPedMissionActor) ~= "function" or type(isPedMissionActor) ~= "function" then
        outputDebugString("[tagging-up-turf] mission-actor API unavailable; native story-ped classification is disabled", 1)
        return
    end
    for _, ped in ipairs(getElementsByType("ped", resourceRoot, true)) do
        if getElementData(ped, TAGUP.missionActorData) ~= nil then
            applyMissionActor(ped)
        end
    end
end)

-- World prompts and local prediction stay client-side to avoid network chatter every
-- frame; only meaningful progress attempts are sent to the authoritative server.

local function destroyNavigation()
    if isElement(state.marker) then
        destroyElement(state.marker)
    end
    if isElement(state.blip) then
        destroyElement(state.blip)
    end
    for _, blip in pairs(state.tagBlips) do
        if isElement(blip) then
            destroyElement(blip)
        end
    end
    state.marker = nil
    state.importantArea = nil
    state.blip = nil
    state.tagBlips = {}
    state.navigationMode = nil
    state.destination = nil
end

-- SWEET1 swaps one shared navigation slot between the default destination
-- colour and Sweet's friendly car, while active tag sites use separate green
-- coordinate blips that disappear independently at 100 percent.
local SCM_DESTINATION_BLIP_COLOR = {226, 192, 99, 255}
local SCM_FRIENDLY_BLIP_COLOR = {0, 0, 255, 255}
local SCM_TAG_BLIP_COLOR = {0, 255, 0, 255}
local SCM_TAG_BLIP_POSITIONS = {
    [1] = {2068.31, -1654.00, 14.3},
    [2] = {2047.31, -1634.65, 13.8},
    [3] = {2396.21, -1469.80, 24.9},
    [4] = {2353.22, -1506.54, 24.7},
    [5] = {2395.43, -1551.69, 26.98},
}

local function setNavigation(position, size, color, importantArea, blipColor)
    destroyNavigation()
    if not position then
        return
    end
    state.destination = position
    if importantArea then
        state.importantArea = {
            center = Vector3(position[1], position[2], position[3]),
            radiusX = importantArea.radiusX,
            radiusY = importantArea.radiusY,
            localId = importantArea.localId,
            vehicleRequired = importantArea.vehicleRequired ~= false,
        }
        if type(renderScriptImportantArea) ~= "function" then
            -- Keep a visible fallback on an unmodified MTA client. Neon uses
            -- GTA's exact SCM important-area renderer instead.
            state.marker = createMarker(position[1], position[2], position[3] - 1, "cylinder", math.max(importantArea.radiusX, importantArea.radiusY),
                                        255, 0, 0, 255)
            setElementDimension(state.marker, TAGUP.dimension)
        end
    else
        state.marker = createMarker(position[1], position[2], position[3] - 1, "cylinder", size or 4, unpack(color or {80, 180, 255, 125}))
        setElementDimension(state.marker, TAGUP.dimension)
    end
    local radarColor = blipColor or SCM_DESTINATION_BLIP_COLOR
    state.blip = createBlip(position[1], position[2], position[3], 0, 2, unpack(radarColor))
    setElementDimension(state.blip, TAGUP.dimension)
    state.navigationMode = "destination"
end

local function setVehicleNavigation()
    destroyNavigation()
    if not isElement(state.vehicle) then
        return
    end
    state.blip = createBlipAttachedTo(state.vehicle, 0, 2, unpack(SCM_FRIENDLY_BLIP_COLOR))
    if isElement(state.blip) then
        setElementDimension(state.blip, TAGUP.dimension)
        state.navigationMode = "vehicle"
    end
end

local function syncTagBlips()
    local active = {}
    for _, tag in ipairs(getActiveTags()) do
        if state.stage ~= "rooftop" or state.rooftopTagRevealed then
            active[tag.id] = true
            if not isElement(state.tagBlips[tag.id]) then
                local position = SCM_TAG_BLIP_POSITIONS[tag.id]
                if position then
                    local blip = createBlip(position[1], position[2], position[3], 0, 2, unpack(SCM_TAG_BLIP_COLOR))
                    if isElement(blip) then
                        setElementDimension(blip, TAGUP.dimension)
                        state.tagBlips[tag.id] = blip
                    end
                end
            end
        end
    end
    for tagId, blip in pairs(state.tagBlips) do
        if not active[tagId] then
            if isElement(blip) then
                destroyElement(blip)
            end
            state.tagBlips[tagId] = nil
        end
    end
    if next(active) then
        state.navigationMode = "tags"
    end
end

local function setStageNavigation(stage)
    if stage == "enter_car" or stage == "return_car" or stage == "return_after_roof" then
        setVehicleNavigation()
    elseif stage == "drive_idlewood" then
        if isElement(state.leader) and getPedOccupiedVehicle(state.leader) == state.vehicle then
            setNavigation(TAGUP.idlewoodDestination, nil, nil,
                          {radiusX = TAGUP.idlewoodArrival.radiusX, radiusY = TAGUP.idlewoodArrival.radiusY, localId = 1})
        else
            setVehicleNavigation()
        end
    elseif stage == "drive_ballas" then
        if isElement(state.leader) and getPedOccupiedVehicle(state.leader) == state.vehicle then
            setNavigation(TAGUP.ballasDestination, nil, nil,
                          {radiusX = TAGUP.ballasArrival.radiusX, radiusY = TAGUP.ballasArrival.radiusY, localId = 2})
        else
            setVehicleNavigation()
        end
    elseif stage == "drive_home" then
        if isElement(state.leader) and getPedOccupiedVehicle(state.leader) == state.vehicle then
            setNavigation(TAGUP.homeDestination, nil, nil,
                          {radiusX = TAGUP.homeArrival.radiusX, radiusY = TAGUP.homeArrival.radiusY, localId = 3})
        else
            setVehicleNavigation()
        end
    elseif stage == "tags_idlewood" or stage == "tags_ballas" then
        destroyNavigation()
        syncTagBlips()
    elseif stage == "rooftop" then
        setNavigation({2374.0, -1534.1, 23.0}, nil, nil,
                      {radiusX = 1.4, radiusY = 1.4, localId = 4, vehicleRequired = false})
    else
        destroyNavigation()
    end
end

local function refreshStageNavigation()
    if state.stage == "drive_idlewood" or state.stage == "drive_ballas" or state.stage == "drive_home" then
        local desiredMode = isElement(state.leader) and getPedOccupiedVehicle(state.leader) == state.vehicle and "destination" or "vehicle"
        if state.navigationMode ~= desiredMode then
            if localPlayer == state.leader and state.navigationMode == "destination" and desiredMode == "vehicle" then
                triggerServerEvent("tagup:vehicleReminder", resourceRoot, state.stage)
            end
            setStageNavigation(state.stage)
        end
    elseif state.stage == "rooftop" and not state.rooftopTagRevealed and isElement(state.leader) and
        not isPedInVehicle(state.leader) then
        local x, y, z = getElementPosition(state.leader)
        if math.abs(x - 2374.0) <= 1.4 and math.abs(y + 1534.1) <= 1.4 and math.abs(z - 23.0) <= 2.0 then
            state.rooftopTagRevealed = true
            destroyNavigation()
            syncTagBlips()
        end
    end
end

local function renderNavigationImportantArea()
    local area = state.importantArea
    if not area or type(renderScriptImportantArea) ~= "function" or not isElement(state.leader) then
        return
    end
    if area.vehicleRequired and (not isElement(state.vehicle) or getPedOccupiedVehicle(state.leader) ~= state.vehicle) then
        return
    end
    renderScriptImportantArea(area.center, area.radiusX, area.radiusY, area.localId)
end

local function releaseArrivalGate(reason)
    local arrival = state.arrivalGate
    if not arrival then
        return true
    end
    state.arrivalGate = nil
    if isTimer(arrival.guardTimer) then
        killTimer(arrival.guardTimer)
    end
    if isTimer(arrival.resendTimer) then
        killTimer(arrival.resendTimer)
    end
    if not arrival.cameraToken then
        return true
    end
    local ok, released = pcall(releaseScriptCamera, arrival.cameraToken)
    outputDebugString(("[tagging-up-turf] SCM arrival lease stage=%s release=%s reason=%s"):format(
                          tostring(arrival.stage), tostring(ok and released ~= false), tostring(reason or "cleanup")),
                      ok and released ~= false and 3 or 2)
    return ok and released ~= false
end

local function consumeArrivalGate(stage)
    local arrival = state.arrivalGate
    if not arrival or arrival.stage ~= stage or not arrival.cameraToken or type(isScriptCameraLeaseActive) ~= "function" or
        not isScriptCameraLeaseActive(arrival.cameraToken) then
        return nil
    end
    state.arrivalGate = nil
    if isTimer(arrival.guardTimer) then
        killTimer(arrival.guardTimer)
    end
    if isTimer(arrival.resendTimer) then
        killTimer(arrival.resendTimer)
    end
    outputDebugString(("[tagging-up-turf] SCM arrival lease stage=%s promoted to scripted scene token=%s"):format(
                          tostring(stage), tostring(arrival.cameraToken)))
    return arrival.cameraToken
end

local function reportArrivalGate(arrival)
    triggerServerEvent("tagup:vehicleReady", resourceRoot, arrival.kind, arrival.hitX, arrival.hitY, arrival.hitZ)
end


local function enterArrivalGate(stage, kind, vehicle)
    if state.arrivalGate or getTickCount() - state.lastArrivalAcquireAttempt < 250 then
        return false
    end
    state.lastArrivalAcquireAttempt = getTickCount()
    if type(acquireScriptCamera) ~= "function" then
        return false
    end
    local ok, token = pcall(acquireScriptCamera, true)
    if not ok or token == false then
        outputDebugString(("[tagging-up-turf] SCM arrival lease refused at stage=%s: %s"):format(tostring(stage), tostring(token)), 2)
        return false
    end

    local vx, vy, vz = getElementVelocity(vehicle)
    local hitX, hitY, hitZ = getElementPosition(vehicle)
    local arrival = {
        stage = stage,
        kind = kind,
        cameraToken = token,
        hitAt = getTickCount(),
        hitX = hitX,
        hitY = hitY,
        hitZ = hitZ,
    }
    state.arrivalGate = arrival
    arrival.guardTimer = setTimer(function()
        if state.arrivalGate == arrival then
            releaseArrivalGate("server_transition_timeout")
        end
    end, 4000, 1)
    -- Element transforms and Lua events use independent network packets. Send
    -- the exact syncer-side LOCATE_CAR_3D hit with the idempotent request: a
    -- strict re-test against the server's older transform can otherwise reject
    -- a fast vehicle after native control inhibition has already stopped it.
    arrival.resendTimer = setTimer(function()
        if state.arrivalGate == arrival and state.stage == arrival.stage then
            reportArrivalGate(arrival)
        end
    end, 100, 0)
    outputDebugString(("[tagging-up-turf] SCM arrival hit stage=%s speed=%.1f km/h token=%s; controls inhibited before network ACK"):format(
                          tostring(stage), math.sqrt(vx * vx + vy * vy + vz * vz) * 180, tostring(token)))
    reportArrivalGate(arrival)
    return true
end

getActiveTags = function()
    local group
    if state.stage == "tags_idlewood" then
        group = "idlewood"
    elseif state.stage == "tags_ballas" then
        group = "ballas"
    elseif state.stage == "rooftop" then
        group = "rooftop"
    end
    local result = {}
    if not group then
        return result
    end
    for _, tag in ipairs(TAGUP.tags) do
        if tag.group == group and not state.completedTags[tag.id] then
            table.insert(result, tag)
        end
    end
    return result
end

nearestActiveTag = function()
    local px, py, pz = getElementPosition(localPlayer)
    local nearest, nearestDistance
    for _, tag in ipairs(getActiveTags()) do
        local distance = tagupDistance3D(px, py, pz, tag.x, tag.y, tag.z)
        if not nearestDistance or distance < nearestDistance then
            nearest, nearestDistance = tag, distance
        end
    end
    return nearest, nearestDistance
end

local function hasFileCutsceneLease(scene)
    return scene and scene.token and type(isFileCutsceneLeaseActive) == "function" and
               isFileCutsceneLeaseActive(scene.token)
end

local function clearFileCutscene(reason, preserveFade)
    local scene = state.fileCutscene
    if not scene then
        return true
    end
    for _, name in ipairs({"appearanceTimer", "loadTimer", "finishTimer", "fadeTimer"}) do
        if isTimer(scene[name]) then
            killTimer(scene[name])
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
    outputDebugString(("[tagging-up-turf] SWEET1A file cutscene #%d cleanup released=%s reason=%s"):format(
                          tonumber(scene.id) or -1, tostring(released), tostring(reason or "cleanup")),
                      released and 3 or 2)
    state.fileCutscene = nil
    return released
end

local function reportFileCutsceneReady(scene, result, details)
    if not scene or scene.readyReported then
        return
    end
    scene.readyReported = true
    triggerServerEvent("tagup:fileCutsceneReady", resourceRoot, scene.id, result, details)
end

local function reportFileCutsceneFinished(scene, result)
    if not scene or scene.finishedReported then
        return
    end
    scene.finishedReported = true
    local skipped = false
    if scene.token and type(wasFileCutsceneSkipped) == "function" then
        local ok, nativeSkipped = pcall(wasFileCutsceneSkipped, scene.token)
        skipped = ok and nativeSkipped == true
    end
    triggerServerEvent("tagup:fileCutsceneFinished", resourceRoot, scene.id, result, skipped,
                       scene.startedAt and getTickCount() - scene.startedAt or nil)
end

local function getFileCutsceneCJReadiness()
    local model = getElementModel(localPlayer)
    local alpha = getElementAlpha(localPlayer)
    local clothesReady = model == TAGUP.cj.model
    local clothes = {}
    if clothesReady then
        for _, expected in ipairs(TAGUP.cj.clothes) do
            local texture, clothingModel = getPedClothes(localPlayer, expected.type)
            clothes[#clothes + 1] = ("%d=%s/%s"):format(expected.type, tostring(texture), tostring(clothingModel))
            if type(texture) ~= "string" or type(clothingModel) ~= "string" or texture:lower() ~= expected.texture or
                clothingModel:lower() ~= expected.model then
                clothesReady = false
            end
        end
    end
    local boneX, boneY, boneZ = getElementBonePosition(localPlayer, 2)
    local boneReady = type(boneX) == "number" and type(boneY) == "number" and type(boneZ) == "number"
    local ready = model == TAGUP.cj.model and alpha == 255 and clothesReady and boneReady
    return ready, ("model=%d alpha=%d bone=%s clothes=%s"):format(
                      model, alpha, tostring(boneReady), table.concat(clothes, ","))
end

local function requestNativeFileCutscene(scene)
    local ok, token = pcall(requestFileCutscene, TAGUP.fileCutscene.name)
    if not ok or not token then
        return reportFileCutsceneReady(scene, "request_refused", tostring(token))
    end
    scene.token = token
    scene.loadTimer = setTimer(function()
        if state.fileCutscene ~= scene then
            return
        end
        if not hasFileCutsceneLease(scene) then
            killTimer(scene.loadTimer)
            scene.loadTimer = nil
            return reportFileCutsceneReady(scene, "lease_lost", "native cutscene lease ended during load")
        end
        local queried, loaded = pcall(isFileCutsceneLoaded, scene.token)
        if not queried then
            killTimer(scene.loadTimer)
            scene.loadTimer = nil
            return reportFileCutsceneReady(scene, "load_query_failed", tostring(loaded))
        end
        if loaded then
            killTimer(scene.loadTimer)
            scene.loadTimer = nil
            reportFileCutsceneReady(scene, "ready", ("loaded in %d ms after CJ readiness"):format(getTickCount() - scene.loadRequestedAt))
        elseif getTickCount() - scene.loadRequestedAt >= TAGUP.fileCutscene.loadTimeout then
            killTimer(scene.loadTimer)
            scene.loadTimer = nil
            reportFileCutsceneReady(scene, "load_timeout", tostring(TAGUP.fileCutscene.loadTimeout))
        end
    end, TAGUP.fileCutscene.pollInterval, 0)
end

addEvent("tagup:fileCutscenePrepare", true)
addEventHandler("tagup:fileCutscenePrepare", resourceRoot, function(sceneId, leaderCanSkip)
    if source ~= resourceRoot or not state.active or state.stage ~= "sweet1a" then
        return
    end
    clearFileCutscene("replaced", false)
    local required = {"requestFileCutscene", "releaseFileCutscene", "isFileCutsceneLeaseActive", "isFileCutsceneLoaded",
                      "startFileCutscene", "fadeFileCutscene", "isFileCutsceneFading", "isFileCutsceneFinished",
                      "isFileCutsceneSkipInputPressed", "wasFileCutsceneSkipped", "skipFileCutscene"}
    for _, name in ipairs(required) do
        if type(_G[name]) ~= "function" then
            state.fileCutscene = {id = sceneId}
            return reportFileCutsceneReady(state.fileCutscene, "api_unavailable", name)
        end
    end
    if not ensureMissionText() then
        state.fileCutscene = {id = sceneId}
        return reportFileCutsceneReady(state.fileCutscene, "mission_text_unavailable", "SWEET1")
    end

    local scene = {
        id = sceneId,
        leaderCanSkip = leaderCanSkip == true,
        requestedAt = getTickCount(),
        appearanceStableSamples = 0,
    }
    state.fileCutscene = scene
    scene.appearanceTimer = setTimer(function()
        if state.fileCutscene ~= scene then
            return
        end
        local ready, details = getFileCutsceneCJReadiness()
        scene.appearanceStableSamples = ready and scene.appearanceStableSamples + 1 or 0
        if scene.appearanceStableSamples >= TAGUP.fileCutscene.appearanceStableSamples then
            killTimer(scene.appearanceTimer)
            scene.appearanceTimer = nil
            scene.loadRequestedAt = getTickCount()
            outputDebugString(("[tagging-up-turf] SWEET1A CJ render barrier passed after %d ms: %s"):format(
                                  scene.loadRequestedAt - scene.requestedAt, details))
            requestNativeFileCutscene(scene)
        elseif getTickCount() - scene.requestedAt >= TAGUP.fileCutscene.appearanceTimeout then
            killTimer(scene.appearanceTimer)
            scene.appearanceTimer = nil
            reportFileCutsceneReady(scene, "cj_appearance_timeout", details)
        end
    end, TAGUP.fileCutscene.pollInterval, 0)
end)

addEvent("tagup:fileCutsceneStart", true)
addEventHandler("tagup:fileCutsceneStart", resourceRoot, function(sceneId)
    local scene = state.fileCutscene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or not hasFileCutsceneLease(scene) or scene.startedAt then
        return
    end
    local startedOk, started = pcall(startFileCutscene, scene.token)
    if not startedOk or started ~= true then
        triggerServerEvent("tagup:fileCutsceneStarted", resourceRoot, scene.id, "start_refused")
        return
    end
    scene.startedAt = getTickCount()
    local fadeOk, faded = pcall(fadeFileCutscene, scene.token, true, TAGUP.fileCutscene.fadeInDuration, 0, 0, 0)
    if not fadeOk or faded ~= true then
        triggerServerEvent("tagup:fileCutsceneStarted", resourceRoot, scene.id, "fade_in_refused")
        return
    end
    triggerServerEvent("tagup:fileCutsceneStarted", resourceRoot, scene.id, "started")
    outputDebugString(("[tagging-up-turf] SWEET1A file cutscene #%d native playback started"):format(scene.id))

    scene.finishTimer = setTimer(function()
        if state.fileCutscene ~= scene or scene.finishedReported then
            return
        end
        if not hasFileCutsceneLease(scene) then
            killTimer(scene.finishTimer)
            scene.finishTimer = nil
            return reportFileCutsceneFinished(scene, "lease_lost")
        end
        local queried, finished = pcall(isFileCutsceneFinished, scene.token)
        if not queried then
            killTimer(scene.finishTimer)
            scene.finishTimer = nil
            return reportFileCutsceneFinished(scene, "finish_query_failed")
        end
        if finished then
            killTimer(scene.finishTimer)
            scene.finishTimer = nil
            local fadeOutOk, fadeOut = pcall(fadeFileCutscene, scene.token, false, 0, 0, 0, 0)
            if not fadeOutOk or fadeOut ~= true then
                return reportFileCutsceneFinished(scene, "fade_out_refused")
            end
            scene.fadeTimer = setTimer(function()
                if state.fileCutscene ~= scene or scene.finishedReported then
                    return
                end
                local fadeQueried, fading = pcall(isFileCutsceneFading, scene.token)
                if not fadeQueried then
                    killTimer(scene.fadeTimer)
                    scene.fadeTimer = nil
                    return reportFileCutsceneFinished(scene, "fade_query_failed")
                end
                if not fading then
                    killTimer(scene.fadeTimer)
                    scene.fadeTimer = nil
                    reportFileCutsceneFinished(scene, "finished")
                end
            end, TAGUP.fileCutscene.pollInterval, 0)
        elseif getTickCount() - scene.startedAt >= TAGUP.fileCutscene.finishTimeout then
            killTimer(scene.finishTimer)
            scene.finishTimer = nil
            reportFileCutsceneFinished(scene, "finish_timeout")
        end
    end, TAGUP.fileCutscene.pollInterval, 0)
end)

addEvent("tagup:fileCutsceneSkip", true)
addEventHandler("tagup:fileCutsceneSkip", resourceRoot, function(sceneId)
    local scene = state.fileCutscene
    if source == resourceRoot and scene and scene.id == sceneId and hasFileCutsceneLease(scene) then
        pcall(skipFileCutscene, scene.token)
    end
end)

addEvent("tagup:fileCutsceneRelease", true)
addEventHandler("tagup:fileCutsceneRelease", resourceRoot, function(sceneId)
    local scene = state.fileCutscene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId then
        return
    end
    local released = clearFileCutscene("completed", true)
    triggerServerEvent("tagup:fileCutsceneReleased", resourceRoot, sceneId, released and "released" or "release_failed")
end)

addEvent("tagup:fileCutsceneCancel", true)
addEventHandler("tagup:fileCutsceneCancel", resourceRoot, function(sceneId, reason)
    if state.fileCutscene and state.fileCutscene.id == sceneId then
        clearFileCutscene(reason, false)
    end
end)

local function hasIntroSceneLease(scene)
    return scene and scene.cameraToken and type(isScriptCameraLeaseActive) == "function" and
               isScriptCameraLeaseActive(scene.cameraToken)
end

local function releaseIntroSceneAudio(scene)
    if not scene or not scene.audioHandle then
        return true
    end
    local handle = scene.audioHandle
    scene.audioHandle = nil
    if type(releaseMissionAudio) ~= "function" then
        return false
    end
    local ok, result = pcall(releaseMissionAudio, handle)
    return ok and result ~= false
end

local function releaseIntroSceneCamera(scene)
    if not scene or not scene.cameraToken then
        return true
    end
    local token = scene.cameraToken
    scene.cameraToken = nil
    if type(releaseScriptCamera) ~= "function" then
        return false
    end
    local ok, result = pcall(releaseScriptCamera, token)
    return ok and result ~= false
end

local function clearIntroScene(reason)
    local scene = state.introScene
    if not scene then
        return true
    end
    for _, name in ipairs({"prepareTimer", "audioLoadTimer", "audioFinishTimer", "entryTimer", "leaseTimer"}) do
        if isTimer(scene[name]) then
            killTimer(scene[name])
        end
    end
    -- SWEET1 clears CJ's primary task before restoring gameplay. Without this,
    -- the final scripted walk can continue steering the local player.
    if localPlayer == state.leader and type(killPedTask) == "function" then
        pcall(killPedTask, localPlayer, "primary", 3, false)
    end
    local audioReleased = releaseIntroSceneAudio(scene)
    local cameraReleased = releaseIntroSceneCamera(scene)
    outputDebugString(("[tagging-up-turf] Intro world scene #%d cleanup camera=%s audio=%s reason=%s"):format(
                          scene.id, tostring(cameraReleased), tostring(audioReleased), tostring(reason or "cleanup")),
                      cameraReleased and audioReleased and 3 or 2)
    state.introScene = nil
    return cameraReleased and audioReleased
end

local function reportIntroSceneReady(scene, result, details)
    if not scene or scene.readyReported then
        return
    end
    scene.readyReported = true
    triggerServerEvent("tagup:introSceneReady", resourceRoot, scene.id, result, details)
end

local function requestIntroSceneAudio(scene, lineIndex, initial)
    if state.introScene ~= scene or not hasIntroSceneLease(scene) then
        return
    end
    local line = TAGUP.introScene.audio[lineIndex]
    if not line then
        return
    end
    releaseIntroSceneAudio(scene)
    local ok, handle = pcall(requestMissionAudio, line.event)
    if not ok or not handle then
        if initial then
            return reportIntroSceneReady(scene, "audio_request_refused", ("event=%d"):format(line.event))
        end
        return triggerServerEvent("tagup:introSceneAudioReady", resourceRoot, scene.id, lineIndex, "request_refused")
    end
    scene.audioHandle = handle
    scene.lineIndex = lineIndex
    scene.audioRequestedAt = getTickCount()
    scene.audioLoadTimer = setTimer(function()
        local active = state.introScene
        if active ~= scene or active.lineIndex ~= lineIndex then
            return
        end
        if not hasIntroSceneLease(active) then
            killTimer(active.audioLoadTimer)
            active.audioLoadTimer = nil
            if initial then
                reportIntroSceneReady(active, "camera_lost", "lease lost during audio load")
            else
                triggerServerEvent("tagup:introSceneAudioReady", resourceRoot, active.id, lineIndex, "camera_lost")
            end
            return
        end
        local queried, loaded = pcall(isMissionAudioLoaded, active.audioHandle)
        if not queried then
            killTimer(active.audioLoadTimer)
            active.audioLoadTimer = nil
            if initial then
                reportIntroSceneReady(active, "audio_query_failed", tostring(loaded))
            else
                triggerServerEvent("tagup:introSceneAudioReady", resourceRoot, active.id, lineIndex, "query_failed")
            end
        elseif loaded then
            local actorsReady = localPlayer ~= state.leader or
                                    (isElementStreamedIn(active.sweet) and isElementSyncer(active.sweet) and
                                        isElementStreamedIn(active.smoke) and isElementSyncer(active.smoke) and
                                        isElement(state.vehicle) and isElementStreamedIn(state.vehicle))
            if not actorsReady then
                return
            end
            killTimer(active.audioLoadTimer)
            active.audioLoadTimer = nil
            local details = ("event=%d loaded in %d ms"):format(line.event, getTickCount() - active.audioRequestedAt)
            if initial then
                reportIntroSceneReady(active, "ready", details)
            else
                triggerServerEvent("tagup:introSceneAudioReady", resourceRoot, active.id, lineIndex, "ready", details)
            end
        end
    end, 100, 0)
end

addEvent("tagup:introScenePrepare", true)
addEventHandler("tagup:introScenePrepare", resourceRoot, function(sceneId, sweet, smoke)
    if source ~= resourceRoot or not state.active or state.stage ~= "intro" or sweet ~= state.sweet or not isElement(smoke) then
        return
    end
    clearIntroScene("replaced")
    local required = {"acquireScriptCamera", "releaseScriptCamera", "isScriptCameraLeaseActive", "resetScriptCamera",
                      "setScriptCameraWidescreen", "setScriptCameraNearClip", "setScriptCameraFixed", "setScriptCameraPersist",
                      "moveScriptCamera", "trackScriptCamera", "fadeScriptCamera", "enginePreloadWorldArea", "setPedGoTo", "setPedEnterVehicle",
                      "setPedLookAt", "setPedTurnToFace", "killPedTask",
                      "requestMissionAudio", "isMissionAudioLoaded", "playMissionAudio", "isMissionAudioFinished", "releaseMissionAudio"}
    for _, name in ipairs(required) do
        if type(_G[name]) ~= "function" then
            state.introScene = {id = sceneId}
            return reportIntroSceneReady(state.introScene, "api_unavailable", name)
        end
    end

    local scene = {id = sceneId, sweet = sweet, smoke = smoke, requestedAt = getTickCount()}
    state.introScene = scene
    local acquired, token = pcall(acquireScriptCamera, true)
    if not acquired or not token then
        return reportIntroSceneReady(scene, "camera_acquire_refused", tostring(token))
    end
    scene.cameraToken = token
    local camera = TAGUP.introScene.camera
    local cameraReady = resetScriptCamera(token) and setScriptCameraWidescreen(token, true) and
                            setScriptCameraNearClip(token, camera.nearClip) and
                            setScriptCameraFixed(token, Vector3(camera.fixed.position.x, camera.fixed.position.y, camera.fixed.position.z),
                                                 Vector3(camera.fixed.target.x, camera.fixed.target.y, camera.fixed.target.z), Vector3(0, 0, 0), true) and
                            fadeScriptCamera(token, false, 0, 0, 0, 0)
    if not cameraReady then
        clearIntroScene("camera_setup_refused")
        return reportIntroSceneReady(scene, "camera_setup_refused", "SCM fixed shot refused")
    end
    local preload = TAGUP.introScene.preload
    local preloaded, preloadError = pcall(enginePreloadWorldArea, preload.x, preload.y, preload.z, "models")
    if not preloaded then
        clearIntroScene("scene_preload_failed")
        return reportIntroSceneReady(scene, "scene_preload_failed", tostring(preloadError))
    end
    scene.leaseTimer = setTimer(function()
        local active = state.introScene
        if active == scene and not hasIntroSceneLease(active) then
            triggerServerEvent("tagup:introSceneLeaseLost", resourceRoot, active.id)
            clearIntroScene("lease_lost")
        end
    end, 100, 0)
    requestIntroSceneAudio(scene, 1, true)
end)

addEvent("tagup:introSceneStart", true)
addEventHandler("tagup:introSceneStart", resourceRoot, function(sceneId)
    local scene = state.introScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or not hasIntroSceneLease(scene) then
        return
    end
    scene.startedAt = getTickCount()
    fadeScriptCamera(scene.cameraToken, true, TAGUP.introScene.camera.fadeInDuration, 0, 0, 0)
    if localPlayer == state.leader then
        local profile = TAGUP.introScene
        local smokeAccepted = setPedGoTo(scene.smoke, Vector3(profile.smoke.walk.x, profile.smoke.walk.y, profile.smoke.walk.z), "walk", 0.5,
                                             2.0, 10000)
        local sweetAccepted = setPedGoTo(scene.sweet, Vector3(profile.sweetWalk.x, profile.sweetWalk.y, profile.sweetWalk.z), "walk", 0.5, 2.0,
                                             10000)
        local leaderAccepted = setPedGoTo(localPlayer, Vector3(profile.leaderWalk.x, profile.leaderWalk.y, profile.leaderWalk.z), "walk", 0.5,
                                              2.0, 10000)
        triggerServerEvent("tagup:introSceneTasksStarted", resourceRoot, scene.id, smokeAccepted == true, sweetAccepted == true,
                           leaderAccepted == true)
    end
    outputDebugString(("[tagging-up-turf] Intro world scene #%d native fixed shot started"):format(scene.id))
end)

addEvent("tagup:introSceneTrack", true)
addEventHandler("tagup:introSceneTrack", resourceRoot, function(sceneId)
    local scene = state.introScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or not hasIntroSceneLease(scene) then
        return
    end
    local move, track = TAGUP.introScene.camera.move, TAGUP.introScene.camera.track
    local ok = resetScriptCamera(scene.cameraToken) and setScriptCameraPersist(scene.cameraToken, true, true) and
                   moveScriptCamera(scene.cameraToken, Vector3(move.from.x, move.from.y, move.from.z),
                                    Vector3(move.to.x, move.to.y, move.to.z), move.duration, true) and
                   trackScriptCamera(scene.cameraToken, Vector3(track.from.x, track.from.y, track.from.z),
                                     Vector3(track.to.x, track.to.y, track.to.z), track.duration, true)
    if not ok then
        triggerServerEvent("tagup:introSceneLeaseLost", resourceRoot, scene.id)
    end
end)

addEvent("tagup:introScenePrepareAudio", true)
addEventHandler("tagup:introScenePrepareAudio", resourceRoot, function(sceneId, lineIndex)
    local scene = state.introScene
    if source == resourceRoot and scene and scene.id == sceneId then
        requestIntroSceneAudio(scene, lineIndex, false)
    end
end)

addEvent("tagup:introScenePlayAudio", true)
addEventHandler("tagup:introScenePlayAudio", resourceRoot, function(sceneId, lineIndex, leaderCanSkip)
    local scene = state.introScene
    local line = TAGUP.introScene.audio[lineIndex]
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or scene.lineIndex ~= lineIndex or not line or
        not hasIntroSceneLease(scene) or not scene.audioHandle then
        return
    end
    scene.skippable = true
    scene.leaderCanSkip = leaderCanSkip == true
    local played, result = pcall(playMissionAudio, scene.audioHandle)
    if not played or result == false then
        return triggerServerEvent("tagup:introSceneAudioFinished", resourceRoot, scene.id, lineIndex, "play_refused")
    end
    printMissionText(line.key, 4000)
    scene.audioStartedAt = getTickCount()
    if lineIndex == 1 then
        local sx, sy, sz = getElementPosition(scene.sweet)
        setPedLookAt(localPlayer, Vector3(sx, sy, sz + 0.7), 8000, scene.sweet)
        if localPlayer == state.leader then
            local px, py, pz = getElementPosition(localPlayer)
            setPedLookAt(scene.sweet, Vector3(px, py, pz + 0.7), 8000, localPlayer)
        end
    elseif lineIndex == 5 and localPlayer == state.leader then
        local profile = TAGUP.introScene
        setPedGoTo(localPlayer, Vector3(profile.leaderFinalWalk.x, profile.leaderFinalWalk.y, profile.leaderFinalWalk.z), "walk", 0.5, 2.0, 10000)
        -- MTA correctly refuses enter-car requests while a synchronized
        -- animation is still running. SCM replaces IDLE_CHAT with the new
        -- scripted task, so clear that animation and retry the request while
        -- retaining one authoritative server completion observation.
        setPedAnimation(scene.sweet, false)
        pcall(killPedTask, scene.sweet, "primary", 3, false)
        local requestedAt = getTickCount()
        local function requestEntry()
            local active = state.introScene
            if active ~= scene or active.lineIndex ~= lineIndex then
                return
            end
            local accepted = setPedEnterVehicle(active.sweet, state.vehicle, 1)
            if accepted or getTickCount() - requestedAt >= 5000 then
                triggerServerEvent("tagup:introSceneEntryRequested", resourceRoot, active.id, active.sweet, state.vehicle, accepted == true)
                active.entryTimer = nil
                return
            end
            active.entryTimer = setTimer(requestEntry, 250, 1)
        end
        requestEntry()
    end
    scene.audioFinishTimer = setTimer(function()
        local active = state.introScene
        if active ~= scene or active.lineIndex ~= lineIndex then
            return
        end
        local queried, finished = pcall(isMissionAudioFinished, active.audioHandle)
        if not queried then
            killTimer(active.audioFinishTimer)
            active.audioFinishTimer = nil
            triggerServerEvent("tagup:introSceneAudioFinished", resourceRoot, active.id, lineIndex, "query_failed")
        elseif finished then
            killTimer(active.audioFinishTimer)
            active.audioFinishTimer = nil
            local elapsed = getTickCount() - active.audioStartedAt
            releaseIntroSceneAudio(active)
            outputDebugString(("[tagging-up-turf] Intro world scene #%d %s finished naturally after %d ms"):format(
                                  active.id, line.key, elapsed))
            if lineIndex == 1 and localPlayer == state.leader then
                local turnAccepted = setPedTurnToFace(localPlayer, active.sweet)
                outputDebugString(("[tagging-up-turf] Intro world scene #%d native 0639 CJ -> Sweet accepted=%s"):format(
                                      active.id, tostring(turnAccepted)), turnAccepted and 3 or 2)
                if not turnAccepted then
                    triggerServerEvent("tagup:introSceneAudioFinished", resourceRoot, active.id, lineIndex, "turn_refused", elapsed)
                    return
                end
            end
            triggerServerEvent("tagup:introSceneAudioFinished", resourceRoot, active.id, lineIndex, "finished", elapsed)
        end
    end, 100, 0)
end)

addEvent("tagup:introSceneRelease", true)
addEventHandler("tagup:introSceneRelease", resourceRoot, function(sceneId)
    local scene = state.introScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId then
        return
    end
    local released = clearIntroScene("completed")
    triggerServerEvent("tagup:introSceneReleased", resourceRoot, sceneId, released and "released" or "release_failed")
end)

addEvent("tagup:introSceneCancel", true)
addEventHandler("tagup:introSceneCancel", resourceRoot, function(sceneId, reason)
    local scene = state.introScene
    if source == resourceRoot and scene and scene.id == sceneId then
        clearIntroScene(reason or "server_cancelled")
    end
end)

local function hasFinalSceneLease(scene)
    return scene and scene.cameraToken and type(isScriptCameraLeaseActive) == "function" and
               isScriptCameraLeaseActive(scene.cameraToken)
end

local function releaseFinalSceneAudio(scene)
    if not scene or not scene.audioHandle then
        return true
    end
    local handle = scene.audioHandle
    scene.audioHandle = nil
    if type(releaseMissionAudio) ~= "function" then
        return false
    end
    local ok, result = pcall(releaseMissionAudio, handle)
    return ok and result ~= false
end

local function stopFinalSceneFacialTalk(scene, reason)
    if not scene or not scene.facialTalkPed then
        return true
    end

    local ped = scene.facialTalkPed
    local lineKey = scene.facialTalkLineKey
    local speechMuted = scene.facialTalkSpeechMuted == true
    scene.facialTalkPed = nil
    scene.facialTalkLineKey = nil
    scene.facialTalkSpeechMuted = nil

    local stopped = true
    if isElement(ped) then
        if speechMuted and type(setPedScriptedSpeechMuted) == "function" then
            local unmutedOk, unmuted = pcall(setPedScriptedSpeechMuted, ped, false)
            stopped = stopped and unmutedOk and unmuted == true
        end
        if type(stopPedFacialTalk) == "function" then
            local stopOk, stopResult = pcall(stopPedFacialTalk, ped)
            stopped = stopped and stopOk and stopResult == true
        else
            stopped = false
        end
    end

    outputDebugString(("[tagging-up-turf] Final Grove scene #%d facial stop line=%s result=%s reason=%s"):format(
                          scene.id, tostring(lineKey), tostring(stopped), tostring(reason or "cleanup")), stopped and 3 or 2)
    return stopped
end

local function startFinalSceneFacialTalk(scene, line)
    stopFinalSceneFacialTalk(scene, "next_line")

    local ped = line.speaker == "leader" and scene.leader or scene.sweet
    -- GTA owns a remote player's facial controller on that player's client.
    -- The leader starts CJ's request locally and normal player synchronization
    -- carries that presentation to the other mission participants.
    if line.speaker == "leader" and ped ~= localPlayer then
        outputDebugString(("[tagging-up-turf] Final Grove scene #%d facial start line=%s delegated to leader client"):format(
                              scene.id, line.key))
        return true
    end
    if not isElement(ped) or type(setPedFacialTalk) ~= "function" then
        return false
    end

    local speechMuted = false
    if type(setPedScriptedSpeechMuted) == "function" and (ped == localPlayer or isElementSyncer(ped)) then
        local mutedOk, muted = pcall(setPedScriptedSpeechMuted, ped, true)
        speechMuted = mutedOk and muted == true
    end

    local facialOk, facialStarted = pcall(setPedFacialTalk, ped, TAGUP.finalScene.facialTalkDuration)
    if not facialOk or facialStarted ~= true then
        if speechMuted then
            pcall(setPedScriptedSpeechMuted, ped, false)
        end
        return false
    end

    scene.facialTalkPed = ped
    scene.facialTalkLineKey = line.key
    scene.facialTalkSpeechMuted = speechMuted
    outputDebugString(("[tagging-up-turf] Final Grove scene #%d facial start line=%s speaker=%s duration=%d"):format(
                          scene.id, line.key, line.speaker, TAGUP.finalScene.facialTalkDuration))
    return true
end

local function releaseFinalSceneCamera(scene, preserveFade)
    if not scene or not scene.cameraToken then
        return true
    end
    local token = scene.cameraToken
    scene.cameraToken = nil
    if type(releaseScriptCamera) ~= "function" then
        return false
    end
    local ok, result = pcall(releaseScriptCamera, token, preserveFade == true)
    return ok and result ~= false
end

local function clearFinalScene(reason, preserveFade)
    local scene = state.finalScene
    if not scene then
        return true
    end
    for _, name in ipairs({"fadeTimer", "audioLoadTimer", "audioFinishTimer", "handshakeTimer", "releaseTimer", "leaseTimer"}) do
        if isTimer(scene[name]) then
            killTimer(scene[name])
        end
    end
    if type(scene.visualReadyHandler) == "function" then
        removeEventHandler("onClientPreRender", root, scene.visualReadyHandler)
        scene.visualReadyHandler = nil
    end
    if scene.actorsCollidable ~= nil and isElement(scene.leader) and isElement(scene.sweet) then
        setElementCollidableWith(scene.leader, scene.sweet, scene.actorsCollidable)
    end
    if scene.walkAcceptedLocal and isElement(scene.sweet) and isElementSyncer(scene.sweet) and type(killPedTask) == "function" then
        killPedTask(scene.sweet, "primary", 3, false)
    end
    local facialStopped = stopFinalSceneFacialTalk(scene, reason or "cleanup")
    local audioReleased = releaseFinalSceneAudio(scene)
    local cameraReleased = releaseFinalSceneCamera(scene, preserveFade)
    outputDebugString(("[tagging-up-turf] Final Grove scene #%d cleanup camera=%s audio=%s facial=%s reason=%s"):format(
                          scene.id, tostring(cameraReleased), tostring(audioReleased), tostring(facialStopped),
                          tostring(reason or "cleanup")), cameraReleased and audioReleased and facialStopped and 3 or 2)
    state.finalScene = nil
    return cameraReleased and audioReleased and facialStopped
end

local function reportFinalSceneReady(scene, result, details)
    if not scene or scene.readyReported then
        return
    end
    scene.readyReported = true
    triggerServerEvent("tagup:finalSceneReady", resourceRoot, scene.id, result, details)
end

local function tryReportFinalSceneReady(scene)
    if state.finalScene ~= scene or scene.readyReported or not scene.fadeReady or not scene.audioReady then
        return
    end
    local actorsReady = isElement(state.leader) and isElement(scene.sweet) and
                            (state.leader == localPlayer or isElementStreamedIn(state.leader)) and
                            isElementStreamedIn(scene.sweet) and
                            (localPlayer ~= state.leader or isElementSyncer(scene.sweet))
    if not actorsReady then
        return
    end
    reportFinalSceneReady(scene, "ready", ("fade black + event=%d loaded in %d ms"):format(
                              TAGUP.finalScene.audio[1].event, getTickCount() - scene.requestedAt))
end

local function requestFinalSceneAudio(scene, lineIndex, initial)
    if state.finalScene ~= scene or not hasFinalSceneLease(scene) then
        return
    end
    local line = TAGUP.finalScene.audio[lineIndex]
    if not line then
        return
    end
    releaseFinalSceneAudio(scene)
    local ok, handle = pcall(requestMissionAudio, line.event)
    if not ok or not handle then
        if initial then
            return reportFinalSceneReady(scene, "audio_request_refused", ("event=%d"):format(line.event))
        end
        return triggerServerEvent("tagup:finalSceneAudioReady", resourceRoot, scene.id, lineIndex, "request_refused")
    end
    scene.audioHandle = handle
    scene.lineIndex = lineIndex
    scene.audioReady = false
    scene.audioRequestedAt = getTickCount()
    scene.audioLoadTimer = setTimer(function()
        local active = state.finalScene
        if active ~= scene or active.lineIndex ~= lineIndex then
            return
        end
        if not hasFinalSceneLease(active) then
            killTimer(active.audioLoadTimer)
            active.audioLoadTimer = nil
            if initial then
                reportFinalSceneReady(active, "camera_lost", "lease lost during audio load")
            else
                triggerServerEvent("tagup:finalSceneAudioReady", resourceRoot, active.id, lineIndex, "camera_lost")
            end
            return
        end
        local queried, loaded = pcall(isMissionAudioLoaded, active.audioHandle)
        if not queried then
            killTimer(active.audioLoadTimer)
            active.audioLoadTimer = nil
            if initial then
                reportFinalSceneReady(active, "audio_query_failed", tostring(loaded))
            else
                triggerServerEvent("tagup:finalSceneAudioReady", resourceRoot, active.id, lineIndex, "query_failed")
            end
        elseif loaded then
            killTimer(active.audioLoadTimer)
            active.audioLoadTimer = nil
            active.audioReady = true
            local details = ("event=%d loaded in %d ms"):format(line.event, getTickCount() - active.audioRequestedAt)
            if initial then
                tryReportFinalSceneReady(active)
            else
                triggerServerEvent("tagup:finalSceneAudioReady", resourceRoot, active.id, lineIndex, "ready", details)
            end
        end
    end, 100, 0)
end

addEvent("tagup:finalScenePrepare", true)
addEventHandler("tagup:finalScenePrepare", resourceRoot, function(sceneId, sweet, leaderCanSkip)
    if source ~= resourceRoot or not state.active or state.stage ~= "final_scene" or sweet ~= state.sweet or not isElement(sweet) then
        return
    end
    clearFinalScene("replaced", false)
    local required = {"acquireScriptCamera", "releaseScriptCamera", "isScriptCameraLeaseActive", "resetScriptCamera",
                      "setScriptCameraWidescreen", "setScriptCameraNearClip", "setScriptCameraFixed", "setScriptCameraPersist",
                      "moveScriptCamera", "trackScriptCamera", "fadeScriptCamera", "isScriptCameraFading", "setPedLookAt", "setPedGoTo",
                      "getElementBonePosition", "requestMissionAudio", "isMissionAudioLoaded", "playMissionAudio", "isMissionAudioFinished",
                      "releaseMissionAudio", "setPedFacialTalk", "stopPedFacialTalk", "setPedScriptedSpeechMuted"}
    for _, name in ipairs(required) do
        if type(_G[name]) ~= "function" then
            state.finalScene = {id = sceneId}
            return reportFinalSceneReady(state.finalScene, "api_unavailable", name)
        end
    end

    local scene = {
        id = sceneId,
        sweet = sweet,
        requestedAt = getTickCount(),
        leaderCanSkip = leaderCanSkip == true,
    }
    state.finalScene = scene
    local acquired, token = pcall(acquireScriptCamera, true)
    if not acquired or not token then
        return reportFinalSceneReady(scene, "camera_acquire_refused", tostring(token))
    end
    scene.cameraToken = token
    local faded, fadeResult = pcall(fadeScriptCamera, token, false, TAGUP.finalScene.camera.fadeOutDuration, 0, 0, 0)
    if not faded or fadeResult == false then
        clearFinalScene("fade_out_refused", false)
        return reportFinalSceneReady(scene, "fade_out_refused", tostring(fadeResult))
    end
    scene.leaseTimer = setTimer(function()
        local active = state.finalScene
        if active == scene and not hasFinalSceneLease(active) then
            triggerServerEvent("tagup:finalSceneLeaseLost", resourceRoot, active.id)
            clearFinalScene("lease_lost", false)
        elseif active == scene then
            -- Streaming can finish after both the fade and first audio load.
            -- Recheck actors until the readiness barrier has been reported.
            tryReportFinalSceneReady(active)
        end
    end, 100, 0)
    scene.fadeTimer = setTimer(function()
        local active = state.finalScene
        if active ~= scene or not hasFinalSceneLease(active) then
            return
        end
        local queried, fading = pcall(isScriptCameraFading, active.cameraToken)
        if not queried then
            killTimer(active.fadeTimer)
            active.fadeTimer = nil
            return reportFinalSceneReady(active, "fade_query_failed", tostring(fading))
        end
        local minimumElapsed = math.floor(TAGUP.finalScene.camera.fadeOutDuration * 1000 + 0.5)
        if not fading and getTickCount() - active.requestedAt >= minimumElapsed then
            killTimer(active.fadeTimer)
            active.fadeTimer = nil
            active.fadeReady = true
            tryReportFinalSceneReady(active)
        end
    end, 50, 0)
    requestFinalSceneAudio(scene, 1, true)
end)

addEvent("tagup:finalSceneStart", true)
addEventHandler("tagup:finalSceneStart", resourceRoot, function(sceneId)
    local scene = state.finalScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or not hasFinalSceneLease(scene) then
        return
    end
    local camera = TAGUP.finalScene.camera
    local ok = resetScriptCamera(scene.cameraToken) and setScriptCameraWidescreen(scene.cameraToken, true) and
                   setScriptCameraNearClip(scene.cameraToken, camera.nearClip) and setScriptCameraPersist(scene.cameraToken, true, true) and
                   setScriptCameraFixed(scene.cameraToken, Vector3(camera.fixed.position.x, camera.fixed.position.y, camera.fixed.position.z),
                                        Vector3(camera.fixed.target.x, camera.fixed.target.y, camera.fixed.target.z), Vector3(0, 0, 0), true)
    if not ok then
        return triggerServerEvent("tagup:finalSceneLeaseLost", resourceRoot, scene.id)
    end

    scene.leader = state.leader
    -- Clothing RPC readback can become correct before GTA has rebuilt CJ's
    -- render clump. Keep the fade black until the staged camera has observed
    -- the complete actor for several consecutive frames.
    scene.actorsCollidable = isElementCollidableWith(scene.leader, scene.sweet)
    setElementCollidableWith(scene.leader, scene.sweet, false)
    scene.visualStableSamples = 0
    scene.visualReadyHandler = function()
        local active = state.finalScene
        if active ~= scene or not hasFinalSceneLease(active) then
            return
        end

        local leader = active.leader
        local profile = TAGUP.finalScene
        local model = isElement(leader) and getElementModel(leader) or -1
        local alpha = isElement(leader) and getElementAlpha(leader) or -1
        local streamed = isElement(leader) and (leader == localPlayer or isElementStreamedIn(leader))
        local onScreen = streamed and isElementOnScreen(leader)
        local boneX, boneY, boneZ = false, false, false
        if streamed then
            boneX, boneY, boneZ = getElementBonePosition(leader, 2)
        end
        local boneReady = type(boneX) == "number" and type(boneY) == "number" and type(boneZ) == "number"
        local sweetStreamed = isElement(active.sweet) and isElementStreamedIn(active.sweet)
        local sweetBoneX, sweetBoneY, sweetBoneZ = false, false, false
        if sweetStreamed then
            sweetBoneX, sweetBoneY, sweetBoneZ = getElementBonePosition(active.sweet, 2)
        end
        local sweetBoneReady = type(sweetBoneX) == "number" and type(sweetBoneY) == "number" and type(sweetBoneZ) == "number"
        local clothesReady = streamed and model == TAGUP.cj.model
        local clothes = {}
        if clothesReady then
            for _, expected in ipairs(TAGUP.cj.clothes) do
                local texture, clothingModel = getPedClothes(leader, expected.type)
                clothes[#clothes + 1] = ("%d=%s/%s"):format(expected.type, tostring(texture), tostring(clothingModel))
                if type(texture) ~= "string" or type(clothingModel) ~= "string" or texture:lower() ~= expected.texture or
                    clothingModel:lower() ~= expected.model then
                    clothesReady = false
                end
            end
        end

        local leaderDistance, sweetDistance = math.huge, math.huge
        local placementZOffset = profile.placementZOffset
        if isElement(leader) then
            local x, y, z = getElementPosition(leader)
            leaderDistance = tagupDistance3D(x, y, z, profile.leader.x, profile.leader.y, profile.leader.z + placementZOffset)
        end
        if isElement(active.sweet) then
            local x, y, z = getElementPosition(active.sweet)
            sweetDistance = tagupDistance3D(x, y, z, profile.sweet.x, profile.sweet.y, profile.sweet.z + placementZOffset)
        end

        -- On-screen and position flags describe camera/frustum and physics,
        -- not clothing reconstruction. A valid bone proves GTA has a usable
        -- CJ clump; consecutive pre-render frames let the deferred rebuild run.
        -- This barrier exists to wait for CJ's deferred clothing clump rebuild.
        -- Sweet can briefly leave the client streamer when the server warps both
        -- actors out of their vehicle, even though the scene remains valid.
        local ready = streamed and model == TAGUP.cj.model and alpha == 255 and clothesReady and boneReady
        active.visualStableSamples = ready and active.visualStableSamples + 1 or 0
        local details = ("model=%d alpha=%d streamed=%s bone=%s sweetStreamed=%s sweetBone=%s onScreen=%s clothes=%s " ..
                            "leaderError=%.3f sweetError=%.3f stable=%d/%d"):format(
                            model, alpha, tostring(streamed), tostring(boneReady), tostring(sweetStreamed), tostring(sweetBoneReady),
                            tostring(onScreen), table.concat(clothes, ","), leaderDistance, sweetDistance, active.visualStableSamples,
                            profile.visualStableSamples)
        if details ~= active.lastVisualDetails and
            (not active.lastVisualLogAt or getTickCount() - active.lastVisualLogAt >= 500) then
            active.lastVisualDetails = details
            active.lastVisualLogAt = getTickCount()
            outputDebugString("[tagging-up-turf] Final CJ render barrier: " .. details)
        end
        if active.visualStableSamples >= profile.visualStableSamples then
            removeEventHandler("onClientPreRender", root, active.visualReadyHandler)
            active.visualReadyHandler = nil
            active.visualReadyReported = true
            triggerServerEvent("tagup:finalSceneVisualReady", resourceRoot, active.id, "ready", details)
        end
    end
    addEventHandler("onClientPreRender", root, scene.visualReadyHandler)
end)

addEvent("tagup:finalSceneReveal", true)
addEventHandler("tagup:finalSceneReveal", resourceRoot, function(sceneId)
    local scene = state.finalScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or not scene.visualReadyReported or not hasFinalSceneLease(scene) then
        return
    end
    local camera = TAGUP.finalScene.camera
    local ok = resetScriptCamera(scene.cameraToken) and setScriptCameraPersist(scene.cameraToken, true, true) and
                   moveScriptCamera(scene.cameraToken, Vector3(camera.move.from.x, camera.move.from.y, camera.move.from.z),
                                Vector3(camera.move.to.x, camera.move.to.y, camera.move.to.z), camera.move.duration, true) and
                   trackScriptCamera(scene.cameraToken, Vector3(camera.track.from.x, camera.track.from.y, camera.track.from.z),
                                     Vector3(camera.track.to.x, camera.track.to.y, camera.track.to.z), camera.track.duration, true) and
                   fadeScriptCamera(scene.cameraToken, true, camera.fadeInDuration, 0, 0, 0)
    if not ok then
        return triggerServerEvent("tagup:finalSceneLeaseLost", resourceRoot, scene.id)
    end
    scene.started = true
    scene.startedAt = getTickCount()
    traceCurrent("final_camera", "NATIVE VERIFIED · 18 s vector move/track started")
    outputDebugString(("[tagging-up-turf] Final Grove scene #%d CJ render-ready; 18-second vector camera started"):format(scene.id))
end)

addEvent("tagup:finalScenePrepareAudio", true)
addEventHandler("tagup:finalScenePrepareAudio", resourceRoot, function(sceneId, lineIndex)
    local scene = state.finalScene
    if source == resourceRoot and scene and scene.id == sceneId then
        requestFinalSceneAudio(scene, lineIndex, false)
    end
end)

addEvent("tagup:finalScenePlayAudio", true)
addEventHandler("tagup:finalScenePlayAudio", resourceRoot, function(sceneId, lineIndex, leaderCanSkip)
    local scene = state.finalScene
    local line = TAGUP.finalScene.audio[lineIndex]
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or scene.lineIndex ~= lineIndex or not line or
        not hasFinalSceneLease(scene) or not scene.audioHandle then
        return
    end
    scene.skippable = true
    scene.leaderCanSkip = leaderCanSkip == true
    local played, result = pcall(playMissionAudio, scene.audioHandle)
    if not played or result == false then
        return triggerServerEvent("tagup:finalSceneAudioFinished", resourceRoot, scene.id, lineIndex, "play_refused")
    end
    printMissionText(line.key, 4000)
    if not startFinalSceneFacialTalk(scene, line) then
        releaseFinalSceneAudio(scene)
        return triggerServerEvent("tagup:finalSceneAudioFinished", resourceRoot, scene.id, lineIndex, "facial_start_refused")
    end
    scene.audioStartedAt = getTickCount()
    if localPlayer == state.leader and lineIndex == 1 then
        local sx, sy, sz = getElementPosition(scene.sweet)
        setPedLookAt(localPlayer, Vector3(sx, sy, sz + 0.7), 20000, scene.sweet)
    elseif localPlayer == state.leader and lineIndex == TAGUP.finalScene.walkLine then
        local sx, sy, sz = getElementPosition(scene.sweet)
        setPedLookAt(localPlayer, Vector3(sx, sy, sz + 0.7), 2000, scene.sweet)
        local walk = TAGUP.finalScene.sweetWalk
        local accepted = setPedGoTo(scene.sweet, Vector3(walk.target.x, walk.target.y, walk.target.z), walk.movement, walk.radius,
                                    walk.slowdownRadius, walk.timeout)
        scene.walkAcceptedLocal = accepted == true
        if scene.walkAcceptedLocal then
            traceCurrent("final_walk", "NATIVE VERIFIED · Sweet walk task accepted")
        end
        triggerServerEvent("tagup:finalSceneWalkResult", resourceRoot, scene.id, scene.sweet, accepted == true)
    end
    scene.audioFinishTimer = setTimer(function()
        local active = state.finalScene
        if active ~= scene or active.lineIndex ~= lineIndex then
            return
        end
        local queried, finished = pcall(isMissionAudioFinished, active.audioHandle)
        if not queried then
            killTimer(active.audioFinishTimer)
            active.audioFinishTimer = nil
            stopFinalSceneFacialTalk(active, "audio_query_failed")
            triggerServerEvent("tagup:finalSceneAudioFinished", resourceRoot, active.id, lineIndex, "query_failed")
        elseif finished then
            killTimer(active.audioFinishTimer)
            active.audioFinishTimer = nil
            local elapsed = getTickCount() - active.audioStartedAt
            stopFinalSceneFacialTalk(active, "natural_audio_finish")
            releaseFinalSceneAudio(active)
            outputDebugString(("[tagging-up-turf] Final Grove scene #%d %s finished naturally after %d ms"):format(
                                  active.id, line.key, elapsed))
            triggerServerEvent("tagup:finalSceneAudioFinished", resourceRoot, active.id, lineIndex, "finished", elapsed)
        end
    end, 100, 0)
end)

addEvent("tagup:finalSceneObserveHandshake", true)
addEventHandler("tagup:finalSceneObserveHandshake", resourceRoot, function(sceneId, sweet)
    local scene = state.finalScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or localPlayer ~= state.leader or sweet ~= scene.sweet then
        return
    end
    scene.handshakeLeaderSeen = false
    scene.handshakeSweetSeen = false
    traceCurrent("final_handshake", "SYNCED ANIMATION TASK · observing both actors")
    scene.handshakeTimer = setTimer(function()
        local active = state.finalScene
        if active ~= scene or not isElement(sweet) then
            return
        end
        local leaderBlock, leaderName = getPedAnimation(localPlayer)
        local sweetBlock, sweetName = getPedAnimation(sweet)
        local function isHandshake(block, name)
            return type(block) == "string" and type(name) == "string" and block:lower() == "gangs" and name:lower() == "hndshkfa"
        end
        local leaderRunning = isHandshake(leaderBlock, leaderName)
        local sweetRunning = isHandshake(sweetBlock, sweetName)
        active.handshakeLeaderSeen = active.handshakeLeaderSeen or leaderRunning
        active.handshakeSweetSeen = active.handshakeSweetSeen or sweetRunning
        if active.handshakeLeaderSeen and active.handshakeSweetSeen and not leaderRunning and not sweetRunning then
            killTimer(active.handshakeTimer)
            active.handshakeTimer = nil
            traceProgress("final_handshake", 1, "NATIVE VERIFIED · both GANGS animations finished")
            triggerServerEvent("tagup:finalSceneHandshakeResult", resourceRoot, active.id, sweet, "finished", "both animations ended")
        end
    end, 50, 0)
end)

addEvent("tagup:finalSceneRelease", true)
addEventHandler("tagup:finalSceneRelease", resourceRoot, function(sceneId, skipped)
    local scene = state.finalScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId then
        return
    end
    scene.skippable = false
    if skipped then
        local duration = TAGUP.finalScene.camera.skipFadeDuration
        if not hasFinalSceneLease(scene) or not fadeScriptCamera(scene.cameraToken, false, duration, 0, 0, 0) then
            clearFinalScene("skip_fade_refused", false)
            return triggerServerEvent("tagup:finalSceneReleased", resourceRoot, sceneId, "release_failed")
        end
        scene.releaseTimer = setTimer(function()
            local released = clearFinalScene("skipped", true)
            if released then
                fadeCamera(true, 0.5, 0, 0, 0)
            end
            triggerServerEvent("tagup:finalSceneReleased", resourceRoot, sceneId, released and "released" or "release_failed")
        end, math.floor(duration * 1000 + 50), 1)
    else
        local released = clearFinalScene("completed", false)
        triggerServerEvent("tagup:finalSceneReleased", resourceRoot, sceneId, released and "released" or "release_failed")
    end
end)

addEvent("tagup:finalSceneCancel", true)
addEventHandler("tagup:finalSceneCancel", resourceRoot, function(sceneId, reason)
    if state.finalScene and state.finalScene.id == sceneId then
        clearFinalScene(reason or "server_cancelled", false)
    end
end)

local SWEET_GO_TO_TASK = "TASK_COMPLEX_GO_TO_POINT_AND_STAND_STILL"

local SWEET_LEAVE_CAR_TASK = "TASK_COMPLEX_LEAVE_CAR"

local function clearDemoLeave(cancelNative)
    local leave = state.demoLeave
    if not leave then
        return
    end
    if isTimer(leave.retryTimer) then
        killTimer(leave.retryTimer)
    end
    if isTimer(leave.monitorTimer) then
        killTimer(leave.monitorTimer)
    end
    if cancelNative and leave.seenNativeTask and isElement(leave.ped) and isElementSyncer(leave.ped) then
        killPedTask(leave.ped, "primary", 3, false)
    end
    state.demoLeave = nil
end

local function reportDemoLeave(result, details)
    local leave = state.demoLeave
    if not leave then
        return
    end
    local id, ped, vehicle = leave.id, leave.ped, leave.vehicle
    if result ~= "exited" then
        traceFail(result .. " · " .. tostring(details or ""))
    end
    clearDemoLeave(false)
    triggerServerEvent("tagup:sweetDemoLeaveResult", resourceRoot, id, ped, vehicle, result, details)
end

local function beginDemoLeave()
    local leave = state.demoLeave
    if not leave then
        return
    end
    if not isElement(leave.ped) or not isElement(leave.vehicle) then
        return reportDemoLeave("destroyed", "ped ou vehicule absent avant setPedExitVehicle")
    end
    if not isElementStreamedIn(leave.ped) or not isElementStreamedIn(leave.vehicle) or not isElementSyncer(leave.ped) then
        if getTickCount() - leave.requestedAt < leave.profile.observationTimeout then
            leave.retryTimer = setTimer(beginDemoLeave, 250, 1)
            return
        end
        return reportDemoLeave("ownership_refused", "ped/vehicule non streame ou leader non-syncer")
    end
    if getPedOccupiedVehicle(leave.ped) ~= leave.vehicle then
        return reportDemoLeave("not_in_vehicle", "Sweet n'occupe plus la Greenwood avant la task")
    end
    if type(setPedExitVehicle) ~= "function" then
        return reportDemoLeave("api_unavailable", "setPedExitVehicle absent du client Neon")
    end

    traceCurrent("leave_car", "NATIVE VERIFIED · 05CD / MTA vehicle lifecycle")
    leave.accepted = setPedExitVehicle(leave.ped)
    if not leave.accepted then
        return reportDemoLeave("refused", "setPedExitVehicle a retourne false")
    end

    leave.acceptedAt = getTickCount()
    leave.seenNativeTask = false
    leave.monitorTimer = setTimer(function()
        local active = state.demoLeave
        if not active then
            return
        end
        if not isElement(active.ped) or not isElement(active.vehicle) then
            return reportDemoLeave("destroyed", "ped ou vehicule detruit pendant la task")
        end
        if not isElementStreamedIn(active.ped) then
            return reportDemoLeave("streamed_out", "Sweet sorti du streaming pendant la task")
        end
        if not isElementSyncer(active.ped) then
            return reportDemoLeave("ownership_lost", "leader n'est plus syncer pendant la task")
        end

        local running = isPedDoingTask(active.ped, SWEET_LEAVE_CAR_TASK)
        active.seenNativeTask = active.seenNativeTask or running
        local elapsed = getTickCount() - active.acceptedAt
        local occupiedVehicle = getPedOccupiedVehicle(active.ped)

        if active.seenNativeTask and not running then
            if not occupiedVehicle then
                return reportDemoLeave("exited", ("elapsed=%d ms, native task observed"):format(elapsed))
            end
            return reportDemoLeave("ended_in_vehicle", ("elapsed=%d ms, vehicle still occupied"):format(elapsed))
        end
        if not active.seenNativeTask and elapsed > active.profile.observationTimeout then
            return reportDemoLeave("not_observed", "TASK_COMPLEX_LEAVE_CAR jamais observee")
        end
        if elapsed > active.profile.guardTimeout then
            return reportDemoLeave("client_timeout", ("elapsed=%d ms"):format(elapsed))
        end
    end, 50, 0)
end

addEvent("tagup:sweetDemoLeaveStart", true)
addEventHandler("tagup:sweetDemoLeaveStart", resourceRoot, function(leaveId, ped, vehicle, profile)
    clearDemoLeave(true)
    if not state.active or state.stage ~= "demo" or localPlayer ~= state.leader or ped ~= state.sweet or vehicle ~= state.vehicle or
        type(profile) ~= "table" then
        return
    end
    state.demoLeave = {id = leaveId, ped = ped, vehicle = vehicle, profile = profile, requestedAt = getTickCount(), accepted = false}
    beginDemoLeave()
end)

addEvent("tagup:sweetDemoLeaveCancel", true)
addEventHandler("tagup:sweetDemoLeaveCancel", resourceRoot, function(leaveId, reason)
    if state.demoLeave and state.demoLeave.id == leaveId then
        outputDebugString(("[tagging-up-turf] Cancelling Sweet native leave-car #%d: %s"):format(leaveId, tostring(reason)))
        clearDemoLeave(reason ~= "completed")
    end
end)

local function clearDemoWalk(cancelNative)
    local walk = state.demoWalk
    if not walk then
        return
    end
    if isTimer(walk.retryTimer) then
        killTimer(walk.retryTimer)
    end
    if isTimer(walk.monitorTimer) then
        killTimer(walk.monitorTimer)
    end
    if cancelNative and walk.accepted and isElement(walk.ped) and isElementSyncer(walk.ped) then
        killPedTask(walk.ped, "primary", 3, false)
    end
    state.demoWalk = nil
end

local function reportDemoWalk(result, details)
    local walk = state.demoWalk
    if not walk then
        return
    end
    local id, ped = walk.id, walk.ped
    if result ~= "arrived" and result ~= "timeout_relocated" then
        traceFail(result .. " · " .. tostring(details or ""))
    end
    clearDemoWalk(false)
    triggerServerEvent("tagup:sweetDemoWalkResult", resourceRoot, id, ped, result, details)
end

local function beginDemoWalk()
    local walk = state.demoWalk
    if not walk then
        return
    end
    if not isElement(walk.ped) then
        return reportDemoWalk("destroyed", "ped absent avant setPedGoTo")
    end
    if not isElementStreamedIn(walk.ped) or not isElementSyncer(walk.ped) then
        if getTickCount() - walk.requestedAt < 5000 then
            walk.retryTimer = setTimer(beginDemoWalk, 250, 1)
            return
        end
        return reportDemoWalk("ownership_refused", "ped non streame ou leader non-syncer apres 5000 ms")
    end
    if type(setPedGoTo) ~= "function" then
        traceCurrent("go_to")
        return reportDemoWalk("api_unavailable", "setPedGoTo absent du client Neon")
    end

    local profile = walk.profile
    local target = profile.target
    traceCurrent("go_to", "NATIVE VERIFIED · Sweet / walk / timeout 20000")
    walk.accepted = setPedGoTo(walk.ped, Vector3(target.x, target.y, target.z), profile.movement, profile.radius, profile.slowdownRadius,
                              profile.timeout)
    if not walk.accepted then
        return reportDemoWalk("refused", "setPedGoTo a retourne false")
    end

    walk.acceptedAt = getTickCount()
    walk.seenNativeTask = false
    local startX, startY = getElementPosition(walk.ped)
    walk.initialDistance2D = math.max(0.001, getDistanceBetweenPoints2D(startX, startY, target.x, target.y))
    traceCurrent("go_to_wait")
    outputDebugString(("[tagging-up-turf] Client accepted Sweet native go-to #%d"):format(walk.id))
    walk.monitorTimer = setTimer(function()
        local active = state.demoWalk
        if not active then
            return
        end
        if not isElement(active.ped) then
            return reportDemoWalk("destroyed", "ped detruit pendant la task")
        end
        if not isElementStreamedIn(active.ped) then
            return reportDemoWalk("streamed_out", "ped sorti du streaming pendant la task")
        end
        if not isElementSyncer(active.ped) then
            return reportDemoWalk("ownership_lost", "leader n'est plus syncer pendant la task")
        end

        local running = isPedDoingTask(active.ped, SWEET_GO_TO_TASK)
        active.seenNativeTask = active.seenNativeTask or running
        local x, y, z = getElementPosition(active.ped)
        local distance2D = getDistanceBetweenPoints2D(x, y, active.profile.target.x, active.profile.target.y)
        local elapsed = getTickCount() - active.acceptedAt
        active.maxTraceProgress = math.max(active.maxTraceProgress or 0, 1 - math.min(1, distance2D / active.initialDistance2D))
        traceProgress("go_to_wait", active.maxTraceProgress,
                      ("TASK OBSERVATION · %.2f m remaining"):format(distance2D))

        if active.seenNativeTask and not running then
            local details = ("distance2D=%.2f m, deltaZ=%.2f m, elapsed=%d ms"):format(distance2D, math.abs(z - active.profile.target.z), elapsed)
            if distance2D <= 0.75 then
                return reportDemoWalk(elapsed >= active.profile.timeout - 250 and "timeout_relocated" or "arrived", details)
            end
            return reportDemoWalk("ended_outside_radius", details)
        end
        if not active.seenNativeTask and elapsed > 1500 then
            return reportDemoWalk("not_observed", "task native jamais observee dans le task manager")
        end
        if elapsed > active.profile.timeout + 5000 then
            return reportDemoWalk("client_timeout", ("distance2D=%.2f m, elapsed=%d ms"):format(distance2D, elapsed))
        end
    end, 100, 0)
end

addEvent("tagup:sweetDemoWalkStart", true)
addEventHandler("tagup:sweetDemoWalkStart", resourceRoot, function(walkId, ped, profile)
    clearDemoWalk(true)
    if not state.active or state.stage ~= "demo" or localPlayer ~= state.leader or ped ~= state.sweet or type(profile) ~= "table" then
        return
    end
    state.demoWalk = {id = walkId, ped = ped, profile = profile, requestedAt = getTickCount(), accepted = false}
    beginDemoWalk()
end)

addEvent("tagup:sweetDemoWalkCancel", true)
addEventHandler("tagup:sweetDemoWalkCancel", resourceRoot, function(walkId, reason)
    if state.demoWalk and state.demoWalk.id == walkId then
        outputDebugString(("[tagging-up-turf] Cancelling Sweet native go-to #%d: %s"):format(walkId, tostring(reason)))
        clearDemoWalk(true)
    end
end)

local SWEET_SHOOT_TASK = "TASK_SIMPLE_GUN_CTRL"

local function clearDemoShoot(cancelNative)
    local shoot = state.demoShoot
    if not shoot then
        return
    end
    if isTimer(shoot.retryTimer) then
        killTimer(shoot.retryTimer)
    end
    if isTimer(shoot.monitorTimer) then
        killTimer(shoot.monitorTimer)
    end
    if cancelNative and shoot.accepted and isElement(shoot.ped) and isElementSyncer(shoot.ped) then
        killPedTask(shoot.ped, "primary", 3, false)
        -- GunControl owns a TASK_SIMPLE_USE_GUN in the secondary attack slot.
        -- MTA rejects EnterVehicle while that task survives, so cancelling the
        -- demonstration must release both halves of the native gun lifecycle.
        killPedTask(shoot.ped, "secondary", 0, false)
    end
    state.demoShoot = nil
end

local function reportDemoShoot(result, details)
    local shoot = state.demoShoot
    if not shoot then
        return
    end
    local id, ped = shoot.id, shoot.ped
    traceFail(result .. " · " .. tostring(details or ""))
    clearDemoShoot(false)
    triggerServerEvent("tagup:sweetDemoShootResult", resourceRoot, id, ped, result, details)
end

local function beginDemoShoot()
    local shoot = state.demoShoot
    if not shoot then
        return
    end
    if not isElement(shoot.ped) then
        return reportDemoShoot("destroyed", "ped absent avant setPedShootAt")
    end
    if not isElementStreamedIn(shoot.ped) or not isElementSyncer(shoot.ped) or getPedWeapon(shoot.ped) ~= TAGUP.sprayWeapon then
        if getTickCount() - shoot.requestedAt < 5000 then
            shoot.retryTimer = setTimer(beginDemoShoot, 250, 1)
            return
        end
        return reportDemoShoot("not_ready", ("streamed=%s, syncer=%s, weapon=%d apres 5000 ms"):format(
            tostring(isElementStreamedIn(shoot.ped)), tostring(isElementSyncer(shoot.ped)), getPedWeapon(shoot.ped)))
    end
    if type(setPedShootAt) ~= "function" then
        traceFailAt("shoot", "setPedShootAt unavailable")
        return reportDemoShoot("api_unavailable", "setPedShootAt absent du client Neon")
    end
    -- Preserve SWEET1's original side-effect order. These writes are independent,
    -- but keeping the runtime order exact also keeps the presentation trace honest.
    traceCurrent("accuracy")
    if type(setPedWeaponAccuracy) ~= "function" or not setPedWeaponAccuracy(shoot.ped, shoot.profile.weaponAccuracy) then
        return reportDemoShoot("weapon_accuracy_refused", "SET_CHAR_ACCURACY 90 indisponible ou refuse")
    end
    traceCurrent("shoot_rate")
    if type(setPedWeaponShootingRate) ~= "function" or
        not setPedWeaponShootingRate(shoot.ped, shoot.profile.shootingRate) then
        return reportDemoShoot("shooting_rate_refused", "SET_CHAR_SHOOT_RATE 100 indisponible ou refuse")
    end

    local target, profile = shoot.target, shoot.profile
    local tag = state.demoTag
    if not isElement(tag) then
        return reportDemoShoot("native_tag_missing", "demoTag absent avant TASK_SHOOT_AT_COORD")
    end
    applyGangTagState(tag)
    local tagProgress = type(getObjectGangTagProgress) == "function" and getObjectGangTagProgress(tag) or false
    local pedX, pedY, pedZ = getElementPosition(shoot.ped)
    local _, _, pedHeading = getElementRotation(shoot.ped)
    local tagX, tagY, tagZ = getElementPosition(tag)
    outputDebugString(("[tagging-up-turf] Sweet shoot geometry ped=(%.3f, %.3f, %.3f, heading=%.1f) target=(%.3f, %.3f, %.3f) tag=(%.3f, %.3f, %.3f) streamed=%s progress=%s"):format(
                          pedX, pedY, pedZ, pedHeading, target.x, target.y, target.z, tagX, tagY, tagZ,
                          tostring(isElementStreamedIn(tag)), tostring(tagProgress)))
    if not isElementStreamedIn(tag) or type(tagProgress) ~= "number" then
        return reportDemoShoot("native_tag_unavailable", ("streamed=%s progress=%s"):format(tostring(isElementStreamedIn(tag)),
                                                                                              tostring(tagProgress)))
    end
    traceCurrent("shoot")
    shoot.accepted = setPedShootAt(shoot.ped, Vector3(target.x, target.y, target.z), profile.duration, profile.burstLength)
    if not shoot.accepted then
        return reportDemoShoot("refused", "setPedShootAt a retourne false")
    end

    shoot.acceptedAt = getTickCount()
    shoot.seenNativeTask = false
    shoot.observedReported = false
    traceCurrent("shoot_wait")
    outputDebugString(("[tagging-up-turf] Client accepted Sweet native shoot #%d"):format(shoot.id))
    shoot.monitorTimer = setTimer(function()
        local active = state.demoShoot
        if not active then
            return
        end
        if not isElement(active.ped) then
            return reportDemoShoot("destroyed", "ped detruit pendant la task")
        end
        if not isElementStreamedIn(active.ped) then
            return reportDemoShoot("streamed_out", "ped sorti du streaming pendant la task")
        end
        if not isElementSyncer(active.ped) then
            return reportDemoShoot("ownership_lost", "leader n'est plus syncer pendant la task")
        end

        local running = isPedDoingTask(active.ped, SWEET_SHOOT_TASK)
        active.seenNativeTask = active.seenNativeTask or running
        local elapsed = getTickCount() - active.acceptedAt
        if running and not active.observedReported then
            active.observedReported = true
            outputDebugString(("[tagging-up-turf] Client observed Sweet native shoot #%d after %d ms"):format(active.id, elapsed))
            triggerServerEvent("tagup:sweetDemoShootObserved", resourceRoot, active.id, active.ped)
        end
        if active.seenNativeTask and not running then
            local details = ("elapsed=%d ms, weapon=%d"):format(elapsed, getPedWeapon(active.ped))
            if elapsed >= active.profile.duration - 500 then
                return reportDemoShoot("duration_expired", details)
            end
            return reportDemoShoot("ended_early", details)
        end
        if not active.seenNativeTask and elapsed > 1500 then
            return reportDemoShoot("not_observed", "TASK_SIMPLE_GUN_CTRL jamais observee dans le task manager")
        end
        if elapsed > active.profile.duration + 5000 then
            return reportDemoShoot("client_timeout", ("elapsed=%d ms, weapon=%d"):format(elapsed, getPedWeapon(active.ped)))
        end
    end, 100, 0)
end

addEvent("tagup:sweetDemoShootStart", true)
addEventHandler("tagup:sweetDemoShootStart", resourceRoot, function(shootId, ped, target, profile)
    clearDemoShoot(true)
    if not state.active or state.stage ~= "demo" or localPlayer ~= state.leader or ped ~= state.sweet or type(target) ~= "table" or
        type(profile) ~= "table" then
        return
    end
    traceCurrent("demo_setup")
    state.demoShoot = {id = shootId, ped = ped, target = target, profile = profile, requestedAt = getTickCount(), accepted = false}
    beginDemoShoot()
end)

addEvent("tagup:sweetDemoShootCancel", true)
addEventHandler("tagup:sweetDemoShootCancel", resourceRoot, function(shootId, reason)
    if state.demoShoot and state.demoShoot.id == shootId then
        outputDebugString(("[tagging-up-turf] Cancelling Sweet native shoot #%d: %s"):format(shootId, tostring(reason)))
        clearDemoShoot(true)
        if reason == "authoritative_tag_complete" then
            traceCurrent("demo_wait")
        end
    end
end)

local function clearDemoSequence(cancelNative)
    local sequence = state.demoSequence
    if not sequence then
        return
    end
    if isTimer(sequence.retryTimer) then
        killTimer(sequence.retryTimer)
    end
    if isTimer(sequence.monitorTimer) then
        killTimer(sequence.monitorTimer)
    end
    if cancelNative and sequence.accepted and isElement(sequence.ped) and isElementSyncer(sequence.ped) then
        killPedTask(sequence.ped, "primary", 3, false)
        killPedTask(sequence.ped, "secondary", 0, false)
    end
    state.demoSequence = nil
end

local function reportDemoSequence(result, details)
    local sequence = state.demoSequence
    if not sequence then
        return
    end
    local id, ped = sequence.id, sequence.ped
    traceFail(result .. " · " .. tostring(details or ""))
    clearDemoSequence(false)
    triggerServerEvent("tagup:sweetDemoSequenceResult", resourceRoot, id, ped, result, details)
end

local function beginDemoSequence()
    local sequence = state.demoSequence
    if not sequence then
        return
    end
    if not isElement(sequence.ped) or not isElement(sequence.vehicle) or not isElement(state.demoTag) then
        return reportDemoSequence("destroyed", "Sweet, Greenwood ou demoTag absent avant la sequence")
    end
    if not isElementStreamedIn(sequence.ped) or not isElementStreamedIn(sequence.vehicle) or not isElementSyncer(sequence.ped) or
        getPedWeapon(sequence.ped) ~= TAGUP.sprayWeapon then
        if getTickCount() - sequence.requestedAt < 5000 then
            sequence.retryTimer = setTimer(beginDemoSequence, 250, 1)
            return
        end
        return reportDemoSequence("not_ready", "ped/vehicule non streame, syncer perdu ou spraycan absent")
    end
    if type(setPedTaskSequence) ~= "function" or type(getPedTaskSequenceProgress) ~= "function" then
        return reportDemoSequence("api_unavailable", "API de sequence native absente du client Neon")
    end

    traceCurrent("demo_setup", "NATIVE VERIFIED · OPEN/CLOSE/PERFORM/CLEAR sequence")
    traceCurrent("accuracy")
    if type(setPedWeaponAccuracy) ~= "function" or not setPedWeaponAccuracy(sequence.ped, sequence.shootProfile.weaponAccuracy) then
        return reportDemoSequence("weapon_accuracy_refused", "SET_CHAR_ACCURACY 90 refuse")
    end
    traceCurrent("shoot_rate")
    if type(setPedWeaponShootingRate) ~= "function" or not setPedWeaponShootingRate(sequence.ped, sequence.shootProfile.shootingRate) then
        return reportDemoSequence("shooting_rate_refused", "SET_CHAR_SHOOT_RATE 100 refuse")
    end

    applyGangTagState(state.demoTag)
    local tagProgress = type(getObjectGangTagProgress) == "function" and getObjectGangTagProgress(state.demoTag) or false
    if not isElementStreamedIn(state.demoTag) or type(tagProgress) ~= "number" then
        return reportDemoSequence("native_tag_unavailable", "demoTag non streame ou progression native indisponible")
    end

    local walk, target, shoot = sequence.walkProfile, sequence.target, sequence.shootProfile
    sequence.accepted = setPedTaskSequence(sequence.ped, {
        {task = "leave_car", vehicle = sequence.vehicle},
        {
            task = "go_to",
            x = walk.target.x,
            y = walk.target.y,
            z = walk.target.z,
            movement = walk.movement,
            radius = walk.radius,
            slowdownRadius = walk.slowdownRadius,
            timeout = walk.timeout,
        },
        {task = "shoot_at", x = target.x, y = target.y, z = target.z, duration = shoot.duration, burstLength = shoot.burstLength},
    }, false)
    if not sequence.accepted then
        return reportDemoSequence("refused", "setPedTaskSequence a retourne false")
    end

    sequence.acceptedAt = getTickCount()
    sequence.seenNativeTask = false
    sequence.lastProgress = -1
    outputDebugString(('[tagging-up-turf] Client accepted Sweet native sequence #%d: leave_car -> go_to -> shoot_at'):format(sequence.id))
    sequence.monitorTimer = setTimer(function()
        local active = state.demoSequence
        if not active then
            return
        end
        if not isElement(active.ped) or not isElementStreamedIn(active.ped) or not isElementSyncer(active.ped) then
            return reportDemoSequence("ownership_lost", "Sweet detruit, sorti du streaming ou plus syncer")
        end

        local progress = getPedTaskSequenceProgress(active.ped)
        local elapsed = getTickCount() - active.acceptedAt
        if type(progress) == "number" and progress >= 0 then
            active.seenNativeTask = true
            if progress ~= active.lastProgress then
                active.lastProgress = progress
                outputDebugString(('[tagging-up-turf] Sweet native sequence #%d progress=%d after %d ms'):format(active.id, progress, elapsed))
                if progress == 1 then
                    traceCurrent("go_to")
                    traceCurrent("go_to_wait", "NATIVE VERIFIED · GET_SEQUENCE_PROGRESS = 1")
                elseif progress == 2 then
                    traceCurrent("shoot")
                    traceCurrent("shoot_wait", "NATIVE VERIFIED · GET_SEQUENCE_PROGRESS = 2")
                    triggerServerEvent("tagup:sweetDemoSequenceShootObserved", resourceRoot, active.id, active.ped)
                end
            end
        elseif active.seenNativeTask then
            return reportDemoSequence("ended_before_tag", ("elapsed=%d ms, lastProgress=%d"):format(elapsed, active.lastProgress))
        elseif elapsed > 1500 then
            return reportDemoSequence("not_observed", "TASK_COMPLEX_USE_SEQUENCE jamais observee")
        end

        if elapsed > active.shootProfile.sequenceGuardTimeout then
            return reportDemoSequence("client_timeout", ("elapsed=%d ms, lastProgress=%d"):format(elapsed, active.lastProgress))
        end
    end, 50, 0)
end

addEvent("tagup:sweetDemoSequenceStart", true)
addEventHandler("tagup:sweetDemoSequenceStart", resourceRoot, function(sequenceId, ped, vehicle, target, walkProfile, shootProfile)
    clearDemoSequence(true)
    if not state.active or state.stage ~= "demo" or localPlayer ~= state.leader or ped ~= state.sweet or vehicle ~= state.vehicle or
        type(target) ~= "table" or type(walkProfile) ~= "table" or type(shootProfile) ~= "table" then
        return
    end
    state.demoSequence = {
        id = sequenceId,
        ped = ped,
        vehicle = vehicle,
        target = target,
        walkProfile = walkProfile,
        shootProfile = shootProfile,
        requestedAt = getTickCount(),
        accepted = false,
    }
    beginDemoSequence()
end)

addEvent("tagup:sweetDemoSequenceCancel", true)
addEventHandler("tagup:sweetDemoSequenceCancel", resourceRoot, function(sequenceId, reason)
    if state.demoSequence and state.demoSequence.id == sequenceId then
        outputDebugString(('[tagging-up-turf] Cancelling Sweet native sequence #%d: %s'):format(sequenceId, tostring(reason)))
        clearDemoSequence(true)
        if reason == "authoritative_tag_complete" then
            traceCurrent("demo_wait")
        end
    end
end)

local SWEET_ENTER_TASK = "TASK_COMPLEX_ENTER_CAR_AS_PASSENGER"

local function clearSweetReturnEnter(cancelNative)
    local enter = state.demoEnter
    if not enter then
        return
    end
    if isTimer(enter.retryTimer) then
        killTimer(enter.retryTimer)
    end
    if isTimer(enter.monitorTimer) then
        killTimer(enter.monitorTimer)
    end
    if cancelNative and enter.accepted and isElement(enter.ped) and isElementSyncer(enter.ped) then
        killPedTask(enter.ped, "primary", 3, false)
    end
    state.demoEnter = nil
end

local function reportSweetReturnEnter(result, details)
    local enter = state.demoEnter
    if not enter then
        return
    end
    local id, ped, vehicle = enter.id, enter.ped, enter.vehicle
    if result == "entered" then
        if type(TAGUP_TRACE) == "table" then
            TAGUP_TRACE.setStatus("enter_passenger", "done", "NATIVE TASK + SERVER OCCUPANT PENDING · MTA seat 1")
        end
        if state.stage == "tags_idlewood" then
            traceCurrent("idlewood_tags")
            updateTraceTagStage()
        end
    else
        traceFail(result .. " · " .. tostring(details or ""))
    end
    clearSweetReturnEnter(false)
    triggerServerEvent("tagup:sweetReturnEnterResult", resourceRoot, id, ped, vehicle, result, details)
end

local function beginSweetReturnEnter()
    local enter = state.demoEnter
    if not enter then
        return
    end
    if not isElement(enter.ped) or not isElement(enter.vehicle) then
        return reportSweetReturnEnter("destroyed", "Sweet ou la Greenwood absent avant setPedEnterVehicle")
    end
    if not isElementStreamedIn(enter.ped) or not isElementStreamedIn(enter.vehicle) or not isElementSyncer(enter.ped) then
        if getTickCount() - enter.requestedAt < enter.profile.observationTimeout then
            enter.retryTimer = setTimer(beginSweetReturnEnter, 250, 1)
            return
        end
        return reportSweetReturnEnter("ownership_refused", "Sweet/Greenwood non streame ou leader non-syncer")
    end
    if getPedOccupiedVehicle(enter.ped) then
        return reportSweetReturnEnter("already_in_vehicle", "Sweet occupe deja un vehicule")
    end
    if getVehicleOccupant(enter.vehicle, enter.profile.seat) then
        return reportSweetReturnEnter("seat_occupied", "siege passager MTA 1 deja occupe")
    end
    if type(setPedEnterVehicle) ~= "function" then
        return reportSweetReturnEnter("api_unavailable", "setPedEnterVehicle absent du client Neon")
    end

    traceCurrent("enter_passenger", "NATIVE VERIFIED · SCM seat 0 -> MTA seat 1 / timeout guard 15000")
    enter.attempts = (enter.attempts or 0) + 1
    enter.accepted = setPedEnterVehicle(enter.ped, enter.vehicle, enter.profile.seat)
    if not enter.accepted then
        if enter.attempts == 1 or getTickCount() - enter.requestedAt >= enter.profile.observationTimeout - 500 then
            local primaryTask = getPedTask(enter.ped, "primary", 3)
            local attackTask = getPedTask(enter.ped, "secondary", 0)
            outputDebugString(("[tagging-up-turf] Sweet passenger entry #%d refused attempt=%d primary=%s secondaryAttack=%s"):format(
                                  enter.id, enter.attempts, tostring(primaryTask), tostring(attackTask)), 2)
        end
        -- The cancelled gun task can keep MTA's primary/weapon state busy for a
        -- frame. Retry briefly instead of turning that hand-off into a false fail.
        if getTickCount() - enter.requestedAt < enter.profile.observationTimeout then
            enter.retryTimer = setTimer(beginSweetReturnEnter, 250, 1)
            return
        end
        return reportSweetReturnEnter("refused", "setPedEnterVehicle a retourne false pendant 5 s")
    end

    enter.acceptedAt = getTickCount()
    enter.seenNativeTask = false
    outputDebugString(("[tagging-up-turf] Client accepted Sweet native passenger entry #%d"):format(enter.id))
    enter.monitorTimer = setTimer(function()
        local active = state.demoEnter
        if not active then
            return
        end
        if not isElement(active.ped) or not isElement(active.vehicle) then
            return reportSweetReturnEnter("destroyed", "Sweet ou la Greenwood detruit pendant la task")
        end
        if not isElementStreamedIn(active.ped) or not isElementStreamedIn(active.vehicle) then
            return reportSweetReturnEnter("streamed_out", "Sweet ou la Greenwood sorti du streaming")
        end
        if not isElementSyncer(active.ped) then
            return reportSweetReturnEnter("ownership_lost", "leader non-syncer pendant la task")
        end

        local running = isPedDoingTask(active.ped, SWEET_ENTER_TASK)
        active.seenNativeTask = active.seenNativeTask or running
        local elapsed = getTickCount() - active.acceptedAt
        local occupiedVehicle = getPedOccupiedVehicle(active.ped)
        local occupiedSeat = occupiedVehicle and getPedOccupiedVehicleSeat(active.ped) or -1

        if active.seenNativeTask and not running then
            local details = ("elapsed=%d ms, occupied=%s, seat=%d"):format(elapsed, tostring(occupiedVehicle == active.vehicle), occupiedSeat)
            if occupiedVehicle == active.vehicle and occupiedSeat == active.profile.seat then
                return reportSweetReturnEnter("entered", details)
            end
            return reportSweetReturnEnter("ended_outside_vehicle", details)
        end
        if not active.seenNativeTask and elapsed > active.profile.observationTimeout then
            return reportSweetReturnEnter("not_observed", "TASK_COMPLEX_ENTER_CAR_AS_PASSENGER jamais observee")
        end
        if elapsed > active.profile.scmTimeout then
            return reportSweetReturnEnter("client_timeout", ("elapsed=%d ms, occupied=%s, seat=%d"):format(
                                                      elapsed, tostring(occupiedVehicle == active.vehicle), occupiedSeat))
        end
    end, 50, 0)
end

addEvent("tagup:sweetReturnEnterStart", true)
addEventHandler("tagup:sweetReturnEnterStart", resourceRoot, function(enterId, ped, vehicle, profile)
    clearSweetReturnEnter(true)
    if not state.active or (state.stage ~= "tags_idlewood" and state.stage ~= "return_car") or localPlayer ~= state.leader or ped ~= state.sweet or
        vehicle ~= state.vehicle or type(profile) ~= "table" then
        return
    end
    state.demoEnter = {
        id = enterId,
        ped = ped,
        vehicle = vehicle,
        profile = profile,
        requestedAt = getTickCount(),
        accepted = false,
    }
    beginSweetReturnEnter()
end)

addEvent("tagup:sweetReturnEnterCancel", true)
addEventHandler("tagup:sweetReturnEnterCancel", resourceRoot, function(enterId, reason)
    if state.demoEnter and state.demoEnter.id == enterId then
        outputDebugString(("[tagging-up-turf] Cancelling Sweet native passenger entry #%d: %s"):format(enterId, tostring(reason)))
        clearSweetReturnEnter(reason ~= "completed")
    end
end)

local BALLAS_LEAVE_TASK = "TASK_COMPLEX_LEAVE_CAR"
local BALLAS_WANDER_TASK = "TASK_COMPLEX_CAR_DRIVE_WANDER"

local function releaseBallasCamera(departure, reason)
    if not departure or not departure.cameraToken then
        return
    end

    local token = departure.cameraToken
    departure.cameraToken = nil
    local released = false
    -- The lease restores camera, widescreen, and the independent control
    -- inhibitor together; legacy setters here would race other resources.
    if type(releaseScriptCamera) == "function" then
        local ok, result = pcall(releaseScriptCamera, token)
        released = ok and result ~= false
    end
    outputDebugString(("[tagging-up-turf] Ballas camera #%d release=%s token=%s reason=%s elapsed=%d ms"):format(
                          departure.id, tostring(released), tostring(token), tostring(reason or "cleanup"),
                          getTickCount() - (departure.preparedAt or getTickCount())), released and 3 or 2)
end

local function releaseBallasAudio(departure, reason)
    if not departure or not departure.audioHandle then
        return true
    end

    local handle = departure.audioHandle
    departure.audioHandle = nil
    local released = false
    if type(releaseMissionAudio) == "function" then
        local ok, result = pcall(releaseMissionAudio, handle)
        released = ok and result ~= false
    end
    outputDebugString(("[tagging-up-turf] Ballas SWE1_AV #%d release=%s handle=%s reason=%s"):format(
                          departure.id, tostring(released), tostring(handle), tostring(reason or "cleanup")),
                      released and 3 or 2)
    return released
end

local function clearBallasDeparture(killWander, reason)
    local departure = state.ballasDeparture
    if departure then
        if isTimer(departure.exitStartTimer) then
            killTimer(departure.exitStartTimer)
        end
        if isTimer(departure.cameraLeaseMonitorTimer) then
            killTimer(departure.cameraLeaseMonitorTimer)
        end
        if isTimer(departure.audioLoadTimer) then
            killTimer(departure.audioLoadTimer)
        end
        if isTimer(departure.audioFinishTimer) then
            killTimer(departure.audioFinishTimer)
        end
        if isTimer(departure.exitRetryTimer) then
            killTimer(departure.exitRetryTimer)
        end
        if isTimer(departure.exitMonitorTimer) then
            killTimer(departure.exitMonitorTimer)
        end
        if isTimer(departure.wanderRetryTimer) then
            killTimer(departure.wanderRetryTimer)
        end
        if isTimer(departure.wanderMonitorTimer) then
            killTimer(departure.wanderMonitorTimer)
        end
        releaseBallasAudio(departure, reason)
        releaseBallasCamera(departure, reason)
    end
    if killWander and isElement(state.ballasWanderPed) and isElementSyncer(state.ballasWanderPed) then
        killPedTask(state.ballasWanderPed, "primary", 3, false)
        state.ballasWanderPed = nil
    end
    state.ballasDeparture = nil
end

local function reportBallasCameraReady(departure, result, details)
    if not departure or departure.cameraReadyReported then
        return
    end
    departure.cameraReadyReported = true
    triggerServerEvent("tagup:ballasCameraReady", resourceRoot, departure.id, departure.vehicle, result, details)
end

local function callBallasCameraApi(name, ...)
    local fn = _G[name]
    if type(fn) ~= "function" then
        return false, ("API absente: %s"):format(name)
    end
    local ok, result = pcall(fn, ...)
    if not ok then
        return false, ("%s a leve une erreur: %s"):format(name, tostring(result))
    end
    if result == false then
        return false, ("%s a retourne false"):format(name)
    end
    return true, result
end

local function hasSweetDemoSceneLease(scene)
    return scene and scene.cameraToken and type(isScriptCameraLeaseActive) == "function" and isScriptCameraLeaseActive(scene.cameraToken)
end

local function releaseSweetDemoAudio(scene)
    if not scene or type(releaseMissionAudio) ~= "function" then
        return true
    end
    local released = true
    for cue, handle in pairs(scene.audioHandles or {}) do
        if handle then
            local ok, result = pcall(releaseMissionAudio, handle)
            released = released and ok and result ~= false
            scene.audioHandles[cue] = nil
        end
    end
    return released
end

local function clearSweetDemoAudioPreload(reason)
    local preload = state.demoAudioPreload
    if not preload then
        return true
    end
    state.demoAudioPreload = nil
    local released = releaseSweetDemoAudio(preload)
    outputDebugString(("[tagging-up-turf] Sweet demo early audio preload release=%s reason=%s"):format(
                          tostring(released), tostring(reason or "cleanup")),
                      released and 3 or 2)
    return released
end

local function releaseSweetDemoCamera(scene, preserveFade)
    if not scene or not scene.cameraToken then
        return true
    end
    local token = scene.cameraToken
    scene.cameraToken = nil
    local ok, result = pcall(releaseScriptCamera, token, preserveFade == true)
    return ok and result ~= false
end

local function stopSweetDemoFacialTalk(scene, reason)
    if not scene or not scene.facialTalkActive then
        return true
    end

    local cue = scene.facialTalkCue
    scene.facialTalkActive = nil
    scene.facialTalkCue = nil
    local ok, stopped = false, false
    if isElement(scene.sweet) and type(stopPedFacialTalk) == "function" then
        ok, stopped = pcall(stopPedFacialTalk, scene.sweet)
    end
    local success = ok and stopped == true
    outputDebugString(("[tagging-up-turf] Sweet demo scene #%d facial stop cue=%s result=%s reason=%s"):format(
                          scene.id, tostring(cue), tostring(success), tostring(reason or "cleanup")), success and 3 or 2)
    return success
end

local function clearSweetDemoScene(reason, preserveFade)
    local scene = state.demoScene
    if not scene then
        return true
    end
    for _, timerName in ipairs({"prepareTimer", "leaseTimer", "fadeTimer", "audioTimer", "animationTimer", "releaseTimer",
                                "playerExitMonitorTimer"}) do
        if isTimer(scene[timerName]) then
            killTimer(scene[timerName])
        end
    end
    stopSweetDemoFacialTalk(scene, reason or "cleanup")
    local cameraReleased = releaseSweetDemoCamera(scene, preserveFade)
    local audioReleased = releaseSweetDemoAudio(scene)
    outputDebugString(("[tagging-up-turf] Sweet demo scene #%d cleanup camera=%s audio=%s reason=%s"):format(
                          scene.id, tostring(cameraReleased), tostring(audioReleased), tostring(reason or "cleanup")),
                      cameraReleased and audioReleased and 3 or 2)
    state.demoScene = nil
    return cameraReleased and audioReleased
end

local function reportSweetDemoSceneReady(scene, result, details)
    if not scene or scene.readyReported then
        return
    end
    scene.readyReported = true
    triggerServerEvent("tagup:sweetDemoSceneReady", resourceRoot, scene.id, result, details)
end

local function requestSweetDemoAudio(scene, cue, eventId)
    local requested, handle = pcall(requestMissionAudio, eventId)
    if not requested or not handle then
        return false, ("%s event=%d"):format(cue, eventId)
    end
    scene.audioHandles[cue] = handle
    outputDebugString(("[tagging-up-turf] Sweet demo scene #%d requested %s event=%d handle=%s"):format(
                          scene.id, cue, eventId, tostring(handle)))
    return true
end

local function startSweetDemoAudioPreload()
    if state.demoAudioPreload then
        return true
    end
    if type(requestMissionAudio) ~= "function" or type(releaseMissionAudio) ~= "function" then
        return false
    end

    local preload = {requestedAt = getTickCount(), audioHandles = {}}
    for _, request in ipairs({
        {cue = "checkout", eventId = TAGUP.sweetDemoScene.audio.checkout},
        {cue = "approach", eventId = TAGUP.sweetDemoScene.audio.approach},
    }) do
        local requested, handle = pcall(requestMissionAudio, request.eventId)
        if not requested or not handle then
            releaseSweetDemoAudio(preload)
            outputDebugString(("[tagging-up-turf] Sweet demo early audio preload refused cue=%s event=%d"):format(request.cue,
                                                                                                                  request.eventId),
                              2)
            return false
        end
        preload.audioHandles[request.cue] = handle
        outputDebugString(("[tagging-up-turf] Sweet demo early audio preload requested %s event=%d handle=%s stage=%s"):format(
                              request.cue, request.eventId, tostring(handle), tostring(state.stage)))
    end
    state.demoAudioPreload = preload
    return true
end

local function finishSweetDemoScenePrepare(scene)
    if state.demoScene ~= scene then
        return
    end
    if not hasSweetDemoSceneLease(scene) then
        if not scene.readyReported then
            clearSweetDemoScene("lease_lost_during_prepare")
            reportSweetDemoSceneReady(scene, "camera_lost", "lease perdue pendant le preload audio")
        end
        return
    end

    local elapsed = getTickCount() - scene.requestedAt
    local allLoaded = true
    for _, cue in ipairs({"checkout", "approach"}) do
        local handle = scene.audioHandles[cue]
        local ok, loaded = pcall(isMissionAudioLoaded, handle)
        if not ok then
            clearSweetDemoScene("audio_query_failed")
            return reportSweetDemoSceneReady(scene, "audio_query_failed", tostring(loaded))
        end
        if not loaded then
            allLoaded = false
        end
    end

    if not allLoaded then
        return
    end
    if isTimer(scene.prepareTimer) then
        killTimer(scene.prepareTimer)
        scene.prepareTimer = nil
    end
    scene.preparedAt = getTickCount()
    outputDebugString(("[tagging-up-turf] Sweet demo scene #%d ordered SWE1_CA/SWE1_AR handles loaded after %d ms"):format(scene.id,
                                                                                                                         elapsed))
    traceCurrent("demo_camera")
    reportSweetDemoSceneReady(scene, "ready", "camera active; ordered SWE1_CA/SWE1_AR preload complete")
end

local function prepareSweetDemoScene(scene)
    if type(requestMissionAudio) ~= "function" or type(isMissionAudioLoaded) ~= "function" or type(playMissionAudio) ~= "function" or
        type(isMissionAudioFinished) ~= "function" or type(releaseMissionAudio) ~= "function" then
        return reportSweetDemoSceneReady(scene, "audio_api_unavailable", "API mission-audio native absente")
    end
    local camera = TAGUP.sweetDemoScene.camera.establishing
    local result = consumeArrivalGate("drive_idlewood")
    local ok = result ~= nil
    if not ok then
        ok, result = callBallasCameraApi("acquireScriptCamera", true)
        if not ok then
            return reportSweetDemoSceneReady(scene, "camera_acquire_refused", result)
        end
    end
    scene.cameraToken = result

    local preload = state.demoAudioPreload
    if preload then
        state.demoAudioPreload = nil
        scene.audioHandles = preload.audioHandles
        preload.audioHandles = {}
        outputDebugString(("[tagging-up-turf] Sweet demo scene #%d adopted early CA/AR preload after %d ms"):format(
                              scene.id, getTickCount() - preload.requestedAt))
    else
        scene.audioHandles = {}
        -- Preserve the original immediate path for a late-joining client that
        -- did not observe the preceding enter-car/drive stage.
        for _, request in ipairs({
            {cue = "checkout", eventId = TAGUP.sweetDemoScene.audio.checkout},
            {cue = "approach", eventId = TAGUP.sweetDemoScene.audio.approach},
        }) do
            local requested, details = requestSweetDemoAudio(scene, request.cue, request.eventId)
            if not requested then
                clearSweetDemoScene("audio_request_refused")
                return reportSweetDemoSceneReady(scene, "audio_request_refused", details)
            end
        end
    end

    ok, result = callBallasCameraApi("resetScriptCamera", scene.cameraToken)
    if ok then
        ok, result = callBallasCameraApi("setScriptCameraWidescreen", scene.cameraToken, true)
    end
    if ok then
        ok, result = callBallasCameraApi("setScriptCameraFixed", scene.cameraToken,
                                        Vector3(camera.position.x, camera.position.y, camera.position.z),
                                        Vector3(camera.target.x, camera.target.y, camera.target.z), Vector3(0, 0, 0), true)
    end
    if not ok then
        clearSweetDemoScene("camera_setup_refused")
        return reportSweetDemoSceneReady(scene, "camera_setup_refused", result)
    end

    scene.leaseTimer = setTimer(function()
        local active = state.demoScene
        if active == scene and not active.leaseLost and not hasSweetDemoSceneLease(active) then
            active.leaseLost = true
            triggerServerEvent("tagup:sweetDemoSceneLeaseLost", resourceRoot, active.id)
        end
    end, 100, 0)
    scene.prepareTimer = setTimer(function()
        finishSweetDemoScenePrepare(scene)
    end, 100, 0)
    finishSweetDemoScenePrepare(scene)
end

local function playSweetDemoAudio(scene, cue)
    if state.demoScene ~= scene or scene.playingCue or not scene.audioHandles or not scene.audioHandles[cue] then
        return
    end
    local handle = scene.audioHandles[cue]
    if not playMissionAudio(handle) then
        triggerServerEvent("tagup:sweetDemoSceneAudioFinished", resourceRoot, scene.id, cue, "play_refused")
        return
    end
    printMissionText(cue == "approach" and "SWE1_AR" or "SWE1_CA", 10000)
    local facialOk, facialStarted = false, false
    if isElement(scene.sweet) and type(setPedFacialTalk) == "function" then
        facialOk, facialStarted = pcall(setPedFacialTalk, scene.sweet, TAGUP.sweetDemoScene.facialTalkDuration)
    end
    if not facialOk or facialStarted ~= true then
        callMissionTextApi("clearMissionTexts")
        triggerServerEvent("tagup:sweetDemoSceneAudioFinished", resourceRoot, scene.id, cue, "facial_start_refused")
        return
    end
    scene.facialTalkActive = true
    scene.facialTalkCue = cue
    scene.playingCue = cue
    outputDebugString(("[tagging-up-turf] Sweet demo scene #%d facial start cue=%s duration=%d"):format(
                          scene.id, cue, TAGUP.sweetDemoScene.facialTalkDuration))
    traceCurrent(cue == "approach" and "demo_audio_ar" or "demo_audio_ca")
    scene.audioTimer = setTimer(function()
        local active = state.demoScene
        if active ~= scene or active.playingCue ~= cue then
            return
        end
        local ok, finished = pcall(isMissionAudioFinished, handle)
        if not ok then
            killTimer(active.audioTimer)
            active.audioTimer = nil
            stopSweetDemoFacialTalk(active, "audio_query_failed")
            callMissionTextApi("clearMissionTexts")
            triggerServerEvent("tagup:sweetDemoSceneAudioFinished", resourceRoot, active.id, cue, "query_failed")
        elseif finished then
            killTimer(active.audioTimer)
            active.audioTimer = nil
            local facialStopped
            if cue == "approach" then
                callMissionTextApi("clearMissionTexts")
                facialStopped = stopSweetDemoFacialTalk(active, "natural_audio_finish")
            else
                facialStopped = stopSweetDemoFacialTalk(active, "natural_audio_finish")
                callMissionTextApi("clearMissionTexts")
            end
            active.playingCue = nil
            if not facialStopped then
                triggerServerEvent("tagup:sweetDemoSceneAudioFinished", resourceRoot, active.id, cue, "facial_stop_refused")
                return
            end
            traceProgress(cue == "approach" and "demo_audio_ar" or "demo_audio_ca", 1,
                          ("NATIVE MISSION AUDIO + FACTALK · %s natural finish"):format(cue))
            triggerServerEvent("tagup:sweetDemoSceneAudioFinished", resourceRoot, active.id, cue, "finished")
        end
    end, 100, 0)
end

addEvent("tagup:sweetDemoScenePrepare", true)
addEventHandler("tagup:sweetDemoScenePrepare", resourceRoot, function(sceneId, sweet)
    if source ~= resourceRoot or not state.active or state.stage ~= "demo" or type(sceneId) ~= "number" or sweet ~= state.sweet then
        return
    end
    if state.demoScene then
        if state.demoScene.id >= sceneId then
            return
        end
        clearSweetDemoScene("replaced_by_newer_scene")
    end
    state.demoScene = {id = sceneId, sweet = sweet, requestedAt = getTickCount()}
    prepareSweetDemoScene(state.demoScene)
end)

addEvent("tagup:sweetDemoSceneStart", true)
addEventHandler("tagup:sweetDemoSceneStart", resourceRoot, function(sceneId)
    local scene = state.demoScene
    if source == resourceRoot and scene and scene.id == sceneId and hasSweetDemoSceneLease(scene) then
        scene.started = true
        scene.startedAt = getTickCount()
    end
end)

addEvent("tagup:sweetDemoPlayerExitStart", true)
addEventHandler("tagup:sweetDemoPlayerExitStart", resourceRoot, function(sceneId, vehicle)
    local scene = state.demoScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or not hasSweetDemoSceneLease(scene) or vehicle ~= state.vehicle then
        return
    end
    if getPedOccupiedVehicle(localPlayer) ~= vehicle then
        outputDebugString(("[tagging-up-turf] Sweet demo scene #%d player already outside before SCM TASK_LEAVE_CAR"):format(scene.id))
        return
    end
    if type(setPedExitVehicle) ~= "function" or not setPedExitVehicle(localPlayer) then
        outputDebugString(("[tagging-up-turf] Sweet demo scene #%d player native TASK_LEAVE_CAR refused; black-screen staging remains armed")
                              :format(scene.id),
                          2)
        return
    end

    scene.playerExitAcceptedAt = getTickCount()
    scene.playerExitSawNative = false
    outputDebugString(("[tagging-up-turf] Sweet demo scene #%d accepted player native TASK_LEAVE_CAR at SCM +600 ms"):format(scene.id))
    scene.playerExitMonitorTimer = setTimer(function()
        local active = state.demoScene
        if active ~= scene then
            return
        end
        local running = isPedDoingTask(localPlayer, "TASK_COMPLEX_LEAVE_CAR")
        active.playerExitSawNative = active.playerExitSawNative or running
        local elapsed = getTickCount() - active.playerExitAcceptedAt
        if active.playerExitSawNative and not running and getPedOccupiedVehicle(localPlayer) ~= vehicle then
            killTimer(active.playerExitMonitorTimer)
            active.playerExitMonitorTimer = nil
            outputDebugString(("[tagging-up-turf] Sweet demo scene #%d player native TASK_LEAVE_CAR completed after %d ms"):format(
                                  active.id, elapsed))
        elseif elapsed > TAGUP.sweetDemoScene.playerExitObservationTimeout then
            killTimer(active.playerExitMonitorTimer)
            active.playerExitMonitorTimer = nil
            outputDebugString(("[tagging-up-turf] Sweet demo scene #%d player TASK_LEAVE_CAR was not naturally observed before staging")
                                  :format(active.id),
                              2)
        end
    end, 50, 0)
end)

addEvent("tagup:sweetDemoSceneFadeOut", true)
addEventHandler("tagup:sweetDemoSceneFadeOut", resourceRoot, function(sceneId)
    local scene = state.demoScene
    if source == resourceRoot and scene and scene.id == sceneId and hasSweetDemoSceneLease(scene) then
        fadeScriptCamera(scene.cameraToken, false, TAGUP.sweetDemoScene.fadeOutDuration, 0, 0, 0)
    end
end)

addEvent("tagup:sweetDemoSceneStaged", true)
addEventHandler("tagup:sweetDemoSceneStaged", resourceRoot, function(sceneId)
    local scene = state.demoScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or not hasSweetDemoSceneLease(scene) then
        return
    end
    local camera = TAGUP.sweetDemoScene.camera.staged
    if not setScriptCameraFixed(scene.cameraToken, Vector3(camera.position.x, camera.position.y, camera.position.z),
                                Vector3(camera.target.x, camera.target.y, camera.target.z), Vector3(0, 0, 0), true) then
        return triggerServerEvent("tagup:sweetDemoSceneLeaseLost", resourceRoot, scene.id)
    end
    scene.fadeTimer = setTimer(function()
        local active = state.demoScene
        if active == scene and hasSweetDemoSceneLease(active) then
            fadeScriptCamera(active.cameraToken, true, TAGUP.sweetDemoScene.fadeInDuration, 0, 0, 0)
        end
    end, TAGUP.sweetDemoScene.blackHold, 1)
end)

addEvent("tagup:sweetDemoSceneDialogue", true)
addEventHandler("tagup:sweetDemoSceneDialogue", resourceRoot, function(sceneId, leaderCanSkip)
    local scene = state.demoScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or not hasSweetDemoSceneLease(scene) then
        return
    end
    local camera = TAGUP.sweetDemoScene.camera.dialogue
    if not setScriptCameraFixed(scene.cameraToken, Vector3(camera.position.x, camera.position.y, camera.position.z),
                                Vector3(camera.target.x, camera.target.y, camera.target.z), Vector3(0, 0, 0), true) then
        return triggerServerEvent("tagup:sweetDemoSceneLeaseLost", resourceRoot, scene.id)
    end
    scene.skippable = true
    scene.leaderCanSkip = leaderCanSkip == true
    playSweetDemoAudio(scene, "approach")
end)

addEvent("tagup:sweetDemoSceneSprayCamera", true)
addEventHandler("tagup:sweetDemoSceneSprayCamera", resourceRoot, function(sceneId)
    local scene = state.demoScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or not hasSweetDemoSceneLease(scene) then
        return
    end
    local move, track = TAGUP.sweetDemoScene.camera.sprayMove, TAGUP.sweetDemoScene.camera.sprayTrack
    if not resetScriptCamera(scene.cameraToken) or not setScriptCameraPersist(scene.cameraToken, true, true) or
        not moveScriptCamera(scene.cameraToken, Vector3(move.from.x, move.from.y, move.from.z), Vector3(move.to.x, move.to.y, move.to.z), move.duration,
                             true) or
        not trackScriptCamera(scene.cameraToken, Vector3(track.from.x, track.from.y, track.from.z), Vector3(track.to.x, track.to.y, track.to.z),
                              track.duration, true) then
        triggerServerEvent("tagup:sweetDemoSceneLeaseLost", resourceRoot, scene.id)
    end
end)

addEvent("tagup:sweetDemoScenePlayAudio", true)
addEventHandler("tagup:sweetDemoScenePlayAudio", resourceRoot, function(sceneId, cue)
    local scene = state.demoScene
    if source == resourceRoot and scene and scene.id == sceneId then
        playSweetDemoAudio(scene, cue)
    end
end)

addEvent("tagup:sweetDemoCheckoutObserve", true)
addEventHandler("tagup:sweetDemoCheckoutObserve", resourceRoot, function(sceneId, ped)
    local scene = state.demoScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or localPlayer ~= state.leader or ped ~= state.sweet then
        return
    end
    scene.checkoutSawRunning = false
    traceCurrent("demo_checkout")
    scene.animationTimer = setTimer(function()
        local active = state.demoScene
        if active ~= scene or not isElement(ped) then
            return
        end
        local block, name = getPedAnimation(ped)
        local running = type(block) == "string" and type(name) == "string" and block:lower() == "graffiti" and
                            name:lower() == "graffiti_chkout"
        if running then
            active.checkoutSawRunning = true
        elseif active.checkoutSawRunning then
            killTimer(active.animationTimer)
            active.animationTimer = nil
            traceProgress("demo_checkout", 1, "SYNCED ANIMATION TASK · natural finish observed by syncer")
            triggerServerEvent("tagup:sweetDemoCheckoutResult", resourceRoot, active.id, ped, "finished", "animation no longer running")
        elseif block then
            killTimer(active.animationTimer)
            active.animationTimer = nil
            triggerServerEvent("tagup:sweetDemoCheckoutResult", resourceRoot, active.id, ped, "interrupted", tostring(block) .. "/" .. tostring(name))
        end
    end, 50, 0)
end)

addEvent("tagup:sweetDemoSceneFinalCheck", true)
addEventHandler("tagup:sweetDemoSceneFinalCheck", resourceRoot, function(sceneId)
    local scene = state.demoScene
    if source == resourceRoot and scene and scene.id == sceneId then
        triggerServerEvent("tagup:sweetDemoSceneFinalResult", resourceRoot, scene.id,
                           hasSweetDemoSceneLease(scene) and "ready" or "lost")
    end
end)

addEvent("tagup:sweetDemoSceneRelease", true)
addEventHandler("tagup:sweetDemoSceneRelease", resourceRoot, function(sceneId, skipped)
    local scene = state.demoScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId then
        return
    end
    scene.skippable = false
    if skipped then
        if not hasSweetDemoSceneLease(scene) or not fadeScriptCamera(scene.cameraToken, false, 0.5, 0, 0, 0) then
            clearSweetDemoScene("skip_fade_refused")
            return triggerServerEvent("tagup:sweetDemoSceneReleased", resourceRoot, sceneId, "release_failed")
        end
        scene.releaseTimer = setTimer(function()
            local released = clearSweetDemoScene("skipped", true)
            if released then
                fadeCamera(true, 0.7, 0, 0, 0)
            end
            triggerServerEvent("tagup:sweetDemoSceneReleased", resourceRoot, sceneId, released and "released" or "release_failed")
        end, 550, 1)
    else
        local released = clearSweetDemoScene("completed", false)
        triggerServerEvent("tagup:sweetDemoSceneReleased", resourceRoot, sceneId, released and "released" or "release_failed")
    end
end)

addEvent("tagup:sweetDemoSceneCancel", true)
addEventHandler("tagup:sweetDemoSceneCancel", resourceRoot, function(sceneId, reason)
    if state.demoScene and state.demoScene.id == sceneId then
        clearSweetDemoScene(reason, false)
    end
end)

local function hasBallasCameraLease(departure)
    return departure and departure.cameraToken and type(isScriptCameraLeaseActive) == "function" and
               isScriptCameraLeaseActive(departure.cameraToken)
end

local function reportBallasCameraLeaseLost(departure)
    if not departure or departure.cameraLeaseLost then
        return
    end
    departure.cameraLeaseLost = true
    triggerServerEvent("tagup:ballasCameraLeaseLost", resourceRoot, departure.id, departure.vehicle)
end

local function prepareBallasCamera(departure)
    local camera = TAGUP.ballasDeparture.camera
    local setupStartedAt = getTickCount()
    if type(requestMissionAudio) ~= "function" or type(isMissionAudioLoaded) ~= "function" or type(playMissionAudio) ~= "function" or
        type(isMissionAudioFinished) ~= "function" or type(releaseMissionAudio) ~= "function" then
        return reportBallasCameraReady(departure, "audio_api_unavailable", "API mission-audio native absente")
    end

    departure.audioRequestedAt = getTickCount()
    local requested, handle = pcall(requestMissionAudio, departure.profile.audio.event)
    if not requested or not handle then
        return reportBallasCameraReady(departure, "audio_request_refused",
                                       ("SWE1_AV event=%d result=%s"):format(departure.profile.audio.event, tostring(handle)))
    end
    departure.audioHandle = handle

    local result = consumeArrivalGate("drive_ballas")
    local ok = result ~= nil
    if not ok then
        ok, result = callBallasCameraApi("acquireScriptCamera", true)
        if not ok then
            releaseBallasAudio(departure, "camera_acquire_refused")
            return reportBallasCameraReady(departure, "acquire_refused", result)
        end
    end

    departure.cameraToken = result
    ok, result = callBallasCameraApi("resetScriptCamera", departure.cameraToken)
    if ok then
        ok, result = callBallasCameraApi("setScriptCameraWidescreen", departure.cameraToken, true)
    end
    if ok then
        ok, result = callBallasCameraApi("setScriptCameraFixed", departure.cameraToken,
                                        Vector3(camera.position.x, camera.position.y, camera.position.z),
                                        Vector3(camera.target.x, camera.target.y, camera.target.z), Vector3(0, 0, 0), true)
    end
    if not ok then
        releaseBallasAudio(departure, "camera_setup_refused")
        releaseBallasCamera(departure, "setup_refused")
        return reportBallasCameraReady(departure, "setup_refused", result)
    end

    departure.preparedAt = getTickCount()
    departure.cameraLeaseMonitorTimer = setTimer(function()
        local active = state.ballasDeparture
        if active ~= departure or active.cameraLeaseLost or not active.cameraToken then
            return
        end
        if not hasBallasCameraLease(active) then
            reportBallasCameraLeaseLost(active)
        end
    end, 100, 0)
    traceCurrent("ballas_camera")
    outputDebugString(("[tagging-up-turf] Ballas camera #%d ready token=%s in %d ms; SWE1_AV event=%d handle=%s requested"):format(
                          departure.id, tostring(departure.cameraToken), getTickCount() - setupStartedAt,
                          departure.profile.audio.event, tostring(departure.audioHandle)))
    reportBallasCameraReady(departure, "ready", "native fixed camera active; SWE1_AV requested")
end

local function reportBallasAudioReady(departure, result, details)
    if not departure or departure.audioReadyReported then
        return
    end
    departure.audioReadyReported = true
    if isTimer(departure.audioLoadTimer) then
        killTimer(departure.audioLoadTimer)
        departure.audioLoadTimer = nil
    end
    triggerServerEvent("tagup:ballasAudioReady", resourceRoot, departure.id, departure.vehicle, result, details)
end

local function waitForBallasAudioLoad(departure)
    if not departure or departure.audioReadyReported then
        return
    end

    local function poll()
        local active = state.ballasDeparture
        if active ~= departure or active.audioReadyReported then
            return
        end
        local ok, loaded = pcall(isMissionAudioLoaded, active.audioHandle)
        if not ok then
            return reportBallasAudioReady(active, "query_failed", tostring(loaded))
        end
        if loaded then
            active.audioLoadedAt = getTickCount()
            outputDebugString(("[tagging-up-turf] Ballas SWE1_AV #%d loaded after leave request; total preload=%d ms"):format(
                                  active.id, active.audioLoadedAt - active.audioRequestedAt))
            return reportBallasAudioReady(active, "ready", "native event 37420 loaded after leave request")
        end
        if getTickCount() - active.audioRequestedAt >= active.profile.audio.loadTimeout then
            return reportBallasAudioReady(active, "load_timeout",
                                          ("event=%d non charge apres %d ms"):format(
                                              active.profile.audio.event, active.profile.audio.loadTimeout))
        end
    end

    departure.audioLoadTimer = setTimer(poll, 100, 0)
    poll()
end

local function reportBallasAudio(departure, result, details)
    if not departure or departure.audioResultReported then
        return
    end
    departure.audioResultReported = true
    if isTimer(departure.audioFinishTimer) then
        killTimer(departure.audioFinishTimer)
        departure.audioFinishTimer = nil
    end
    triggerServerEvent("tagup:ballasAudioResult", resourceRoot, departure.id, departure.vehicle, result, details)
end

local function startBallasAudio(departure)
    if not departure or departure.audioStarted then
        return departure and departure.audioStarted
    end
    if not departure.audioHandle then
        reportBallasAudio(departure, "missing_handle", "SWE1_AV handle absent")
        return false
    end

    local ok, played = pcall(playMissionAudio, departure.audioHandle)
    if not ok or played ~= true then
        reportBallasAudio(departure, "play_refused", tostring(played))
        return false
    end

    departure.audioStarted = true
    departure.audioStartedAt = getTickCount()
    printMissionText("SWE1_AV", 10000)
    traceCurrent("ballas_audio_av", "NATIVE MISSION AUDIO · event 37420 / waiting for natural finish")
    outputDebugString(("[tagging-up-turf] Ballas SWE1_AV #%d started event=%d handle=%s"):format(
                          departure.id, departure.profile.audio.event, tostring(departure.audioHandle)))
    departure.audioFinishTimer = setTimer(function()
        local active = state.ballasDeparture
        if active ~= departure or active.audioResultReported then
            return
        end
        local queried, finished = pcall(isMissionAudioFinished, active.audioHandle)
        local elapsed = getTickCount() - active.audioStartedAt
        if not queried then
            return reportBallasAudio(active, "query_failed", tostring(finished))
        end
        if finished then
            traceProgress("ballas_audio_av", 1, ("NATIVE MISSION AUDIO · natural finish after %d ms"):format(elapsed))
            outputDebugString(("[tagging-up-turf] Ballas SWE1_AV #%d finished naturally after %d ms"):format(active.id, elapsed))
            return reportBallasAudio(active, "finished", ("natural finish after %d ms"):format(elapsed))
        end
        if elapsed >= active.profile.audio.finishTimeout then
            return reportBallasAudio(active, "finish_timeout", ("still active after %d ms"):format(elapsed))
        end
    end, 100, 0)
    return true
end

local function reportBallasPlayerExit(result, details)
    local departure = state.ballasDeparture
    if not departure or departure.exitReported then
        return
    end
    departure.exitReported = true
    if isTimer(departure.exitRetryTimer) then
        killTimer(departure.exitRetryTimer)
    end
    if isTimer(departure.exitMonitorTimer) then
        killTimer(departure.exitMonitorTimer)
    end
    triggerServerEvent("tagup:ballasPlayerExitResult", resourceRoot, departure.id, departure.vehicle, result, details)
end

local function beginBallasPlayerExit()
    local departure = state.ballasDeparture
    if not departure or departure.exitReported then
        return
    end
    if not hasBallasCameraLease(departure) then
        reportBallasCameraLeaseLost(departure)
        return
    end
    if not isElement(departure.vehicle) then
        return reportBallasPlayerExit("destroyed", "Greenwood absente avant la sortie")
    end
    if getPedOccupiedVehicle(localPlayer) ~= departure.vehicle then
        waitForBallasAudioLoad(departure)
        return reportBallasPlayerExit("already_out", "joueur deja hors de la Greenwood")
    end
    if not isElementStreamedIn(departure.vehicle) then
        if getTickCount() - departure.requestedAt < 5000 then
            departure.exitRetryTimer = setTimer(beginBallasPlayerExit, 250, 1)
            return
        end
        return reportBallasPlayerExit("streamed_out", "Greenwood non streamee apres 5 s")
    end
    if type(setPedExitVehicle) ~= "function" or not setPedExitVehicle(localPlayer) then
        return reportBallasPlayerExit("refused", "setPedExitVehicle(localPlayer) refusee")
    end

    waitForBallasAudioLoad(departure)

    departure.exitAcceptedAt = getTickCount()
    departure.exitSeenNative = false
    departure.exitMonitorTimer = setTimer(function()
        local active = state.ballasDeparture
        if not active or active.exitReported then
            return
        end
        local running = isPedDoingTask(localPlayer, BALLAS_LEAVE_TASK)
        active.exitSeenNative = active.exitSeenNative or running
        local elapsed = getTickCount() - active.exitAcceptedAt
        if active.exitSeenNative and not running and getPedOccupiedVehicle(localPlayer) ~= active.vehicle then
            traceProgress("ballas_leave", 1, ("NATIVE VERIFIED · player exited in %d ms"):format(elapsed))
            return reportBallasPlayerExit("exited", ("TASK_COMPLEX_LEAVE_CAR observee, elapsed=%d ms"):format(elapsed))
        end
        if not active.exitSeenNative and elapsed > active.profile.observationTimeout then
            return reportBallasPlayerExit("not_observed", "TASK_COMPLEX_LEAVE_CAR jamais observee")
        end
        if elapsed > active.profile.exitTimeout then
            return reportBallasPlayerExit("timeout", ("joueur encore en vehicule apres %d ms"):format(elapsed))
        end
    end, 50, 0)
end

local function reportBallasWander(result, details)
    local departure = state.ballasDeparture
    if not departure or departure.wanderReported then
        return
    end
    departure.wanderReported = true
    triggerServerEvent("tagup:ballasDriveWanderResult", resourceRoot, departure.id, departure.ped, departure.vehicle, result, details)
end

local function beginBallasDriveWander()
    local departure = state.ballasDeparture
    if not departure or departure.wanderReported then
        return
    end
    if not isElement(departure.ped) or not isElement(departure.vehicle) then
        return reportBallasWander("destroyed", "Sweet ou Greenwood absent avant 05D2")
    end
    if not isElementStreamedIn(departure.ped) or not isElementStreamedIn(departure.vehicle) or not isElementSyncer(departure.ped) or
        not isElementSyncer(departure.vehicle) then
        if getTickCount() - departure.wanderRequestedAt < 5000 then
            departure.wanderRetryTimer = setTimer(beginBallasDriveWander, 250, 1)
            return
        end
        return reportBallasWander("ownership_refused", "leader pas double syncer apres 5 s")
    end
    if getPedOccupiedVehicle(departure.ped) ~= departure.vehicle or getVehicleController(departure.vehicle) then
        return reportBallasWander("invalid_state", "Sweet doit rester passager et le conducteur doit etre vide")
    end
    if type(setPedMissionActor) ~= "function" or type(isPedMissionActor) ~= "function" or not setPedMissionActor(departure.ped, true) or
        not isPedMissionActor(departure.ped) then
        return reportBallasWander("mission_actor_refused", "Sweet n'a pas pu recevoir la classification PED_MISSION")
    end
    if type(setPedDriveWander) ~= "function" or
        not setPedDriveWander(departure.ped, departure.vehicle, departure.profile.speed, departure.profile.drivingStyle) then
        return reportBallasWander("refused", "setPedDriveWander a retourne false")
    end

    state.ballasWanderPed = departure.ped
    departure.wanderAcceptedAt = getTickCount()
    -- Start the SCM WAIT 1000 clock at native constructor acceptance; task
    -- observation remains an independent proof before the server advances.
    triggerServerEvent("tagup:ballasDriveWanderAccepted", resourceRoot, departure.id, departure.ped, departure.vehicle)
    traceCurrent("ballas_wander")
    departure.wanderMonitorTimer = setTimer(function()
        local active = state.ballasDeparture
        if not active then
            return
        end
        local running = isElement(active.ped) and isPedDoingTask(active.ped, BALLAS_WANDER_TASK)
        local elapsed = getTickCount() - active.wanderAcceptedAt
        if running and not active.wanderReported then
            traceProgress("ballas_wander", 1, "NATIVE VERIFIED · Sweet passenger / speed 20 / style 2")
            traceCurrent("ballas_wait")
            return reportBallasWander("observed", ("TASK_COMPLEX_CAR_DRIVE_WANDER apres %d ms"):format(elapsed))
        end
        if not active.wanderReported and elapsed > active.profile.observationTimeout then
            return reportBallasWander("not_observed", "TASK_COMPLEX_CAR_DRIVE_WANDER jamais observee")
        end
        if active.wanderReported and not running then
            active.wanderReported = false
            return reportBallasWander("ended", "task Wander indefinite disparue avant la suite")
        end
    end, 50, 0)
end

addEvent("tagup:ballasCameraPrepare", true)
addEventHandler("tagup:ballasCameraPrepare", resourceRoot, function(departureId, vehicle)
    if source ~= resourceRoot or not state.active or state.stage ~= "ballas_departure" or not isElement(vehicle) or vehicle ~= state.vehicle or
        type(departureId) ~= "number" then
        return
    end
    if state.ballasDeparture then
        -- Validate before replacing: a delayed scene event must never tear down
        -- the newer generation that currently owns the camera lease.
        if state.ballasDeparture.id >= departureId then
            outputDebugString(("[tagging-up-turf] Ignoring stale Ballas camera prepare #%s"):format(tostring(departureId)), 2)
            return
        end
        clearBallasDeparture(true, "replaced_by_newer_scene")
    end
    state.ballasDeparture = {id = departureId, vehicle = vehicle, profile = TAGUP.ballasDeparture}
    prepareBallasCamera(state.ballasDeparture)
end)

addEvent("tagup:ballasPlayerExitStart", true)
addEventHandler("tagup:ballasPlayerExitStart", resourceRoot, function(departureId, vehicle)
    local departure = state.ballasDeparture
    if source ~= resourceRoot or not departure or departure.id ~= departureId or departure.vehicle ~= vehicle or not departure.cameraReadyReported or
        not departure.cameraToken or departure.exitStarted or not state.active or state.stage ~= "ballas_departure" then
        return
    end
    if not hasBallasCameraLease(departure) then
        reportBallasCameraLeaseLost(departure)
        return
    end
    departure.exitStarted = true
    departure.requestedAt = getTickCount()
    traceCurrent("ballas_leave")
    local elapsed = getTickCount() - departure.preparedAt
    local delay = math.max(0, departure.profile.camera.minimumLeadTime - elapsed)
    if delay > 0 then
        departure.exitStartTimer = setTimer(beginBallasPlayerExit, delay, 1)
    else
        beginBallasPlayerExit()
    end
end)

addEvent("tagup:ballasAudioStart", true)
addEventHandler("tagup:ballasAudioStart", resourceRoot, function(departureId, vehicle)
    local departure = state.ballasDeparture
    if source ~= resourceRoot or not departure or departure.id ~= departureId or departure.vehicle ~= vehicle or
        not departure.audioReadyReported or departure.audioStarted or not state.active or state.stage ~= "ballas_departure" then
        return
    end
    startBallasAudio(departure)
end)

addEvent("tagup:ballasCameraFinalCheck", true)
addEventHandler("tagup:ballasCameraFinalCheck", resourceRoot, function(departureId, vehicle)
    local departure = state.ballasDeparture
    if source ~= resourceRoot or not departure or departure.id ~= departureId or departure.vehicle ~= vehicle or not state.active or
        state.stage ~= "ballas_departure" then
        return
    end

    local active = hasBallasCameraLease(departure)
    if not active then
        departure.cameraLeaseLost = true
    end
    triggerServerEvent("tagup:ballasCameraFinalResult", resourceRoot, departure.id, departure.vehicle, active and "ready" or "lost")
end)

addEvent("tagup:ballasDriveWanderStart", true)
addEventHandler("tagup:ballasDriveWanderStart", resourceRoot, function(departureId, ped, vehicle, profile)
    local departure = state.ballasDeparture
    if not departure or departure.id ~= departureId or localPlayer ~= state.leader or departure.vehicle ~= vehicle or type(profile) ~= "table" then
        return
    end
    departure.ped = ped
    departure.profile = profile
    departure.wanderRequestedAt = getTickCount()
    beginBallasDriveWander()
end)

addEvent("tagup:ballasDepartureCancel", true)
addEventHandler("tagup:ballasDepartureCancel", resourceRoot, function(departureId, reason)
    if state.ballasDeparture and state.ballasDeparture.id == departureId then
        local keepWandering = reason == "keep_wandering"
        outputDebugString(("[tagging-up-turf] Ballas departure #%d closed: %s"):format(departureId, tostring(reason)))
        clearBallasDeparture(not keepWandering, reason)
    end
end)

local function reportBallasEncounterTask(encounter, phase, result, details)
    if not encounter or encounter[phase .. "Reported"] then
        return
    end
    encounter[phase .. "Reported"] = true
    triggerServerEvent("tagup:ballasEncounterTaskResult", resourceRoot, encounter.id, phase, result, details)
end

local function releaseBallasEncounterAudio(encounter)
    if not encounter or not encounter.audioHandles then
        return
    end
    if type(releaseMissionAudio) == "function" then
        for _, handle in pairs(encounter.audioHandles) do
            pcall(releaseMissionAudio, handle)
        end
    end
    encounter.audioHandles = {}
end

local function releaseBallasEncounterAudioCue(encounter, cue)
    local handle = encounter and encounter.audioHandles and encounter.audioHandles[cue]
    if not handle then
        return false
    end
    encounter.audioHandles[cue] = nil
    if type(releaseMissionAudio) ~= "function" then
        return false
    end
    local ok, released = pcall(releaseMissionAudio, handle)
    outputDebugString(('[tagging-up-turf] Ballas optional audio released %s handle=%s result=%s'):format(
                          tostring(cue), tostring(handle), tostring(ok and released ~= false)))
    return ok and released ~= false
end

local function clearBallasEncounterAudioPreload()
    if state.ballasEncounterAudioPreload then
        releaseBallasEncounterAudio(state.ballasEncounterAudioPreload)
        state.ballasEncounterAudioPreload = nil
    end
end

startBallasEncounterAudioPreload = function()
    local target = state.ballasEncounter
    if target and next(target.audioHandles or {}) then
        return
    end
    if not target then
        if state.ballasEncounterAudioPreload then
            return
        end
        target = {audioHandles = {}}
        state.ballasEncounterAudioPreload = target
    end
    if type(requestMissionAudio) ~= "function" then
        return
    end
    for _, request in ipairs({
        {cue = "whatTheFuck", eventId = TAGUP.ballasGangScene.audio.whatTheFuck},
        {cue = "getThatFool", eventId = TAGUP.ballasGangScene.audio.getThatFool},
    }) do
        local requested, handle = pcall(requestMissionAudio, request.eventId)
        if requested and handle then
            target.audioHandles[request.cue] = handle
            outputDebugString(('[tagging-up-turf] Ballas optional audio requested %s event=%d handle=%s'):format(
                                  request.cue, request.eventId, tostring(handle)))
        else
            outputDebugString(('[tagging-up-turf] Ballas optional audio refused: %s'):format(request.cue), 2)
        end
    end
end

local function restoreBallasEncounterSpeech(encounter)
    if not encounter or not encounter.speechMuted or localPlayer ~= state.leader then
        return
    end
    encounter.speechMuted = false
    for _, ped in ipairs(encounter.enemies) do
        if isElement(ped) and not isPedDead(ped) and isElementSyncer(ped) and type(setPedScriptedSpeechMuted) == "function" then
            setPedScriptedSpeechMuted(ped, false)
        end
    end
end

local function clearBallasEncounter(reason, stopNative)
    local encounter = state.ballasEncounter
    if not encounter then
        return
    end
    if isTimer(encounter.chatRetryTimer) then
        killTimer(encounter.chatRetryTimer)
    end
    if isTimer(encounter.chatObservationTimer) then
        killTimer(encounter.chatObservationTimer)
    end
    for _, timer in pairs(encounter.audioFinishTimers or {}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    encounter.audioFinishTimers = {}
    restoreBallasEncounterSpeech(encounter)
    if stopNative and localPlayer == state.leader and type(killPedTask) == "function" then
        for _, ped in ipairs(encounter.enemies) do
            if isElement(ped) and isElementSyncer(ped) then
                killPedTask(ped, "primary", 3, false)
            end
        end
    end
    releaseBallasEncounterAudio(encounter)
    outputDebugString(('[tagging-up-turf] Ballas encounter #%d closed: %s'):format(encounter.id, tostring(reason)))
    state.ballasEncounter = nil
end

local function playBallasEncounterAudio(encounter, cue, textKey)
    local handle = encounter and encounter.audioHandles and encounter.audioHandles[cue]
    if not handle or type(isMissionAudioLoaded) ~= "function" or type(playMissionAudio) ~= "function" then
        return false
    end
    local ok, loaded = pcall(isMissionAudioLoaded, handle)
    if not ok or not loaded then
        return false
    end
    ok, loaded = pcall(playMissionAudio, handle)
    if not ok or loaded ~= true then
        return false
    end
    printMissionText(textKey, 2000)
    return true
end

local function traceBallasPartnerState(encounter, phase)
    if not encounter or not encounter.enemies then
        return
    end

    local parts = {}
    for index, ped in ipairs(encounter.enemies) do
        if not isElement(ped) then
            parts[index] = ("Flat%d=missing"):format(index)
        else
            local x, y, z = getElementPosition(ped)
            local _, _, rotation = getElementRotation(ped)
            local chatActive = type(isPedDoingTask) == "function" and isPedDoingTask(ped, "TASK_COMPLEX_PARTNER_CHAT") or false
            local primaryTask = type(getPedTask) == "function" and getPedTask(ped, "primary", 3) or nil
            local hierarchy = type(getPedTask) == "function" and {getPedTask(ped, "primary", 3)} or {}
            if hierarchy[1] == false then
                hierarchy = {}
            end
            parts[index] = ("Flat%d model=%d pos=(%.3f,%.3f,%.3f) rot=%.1f chat=%s primary=%s hierarchy=%s"):format(
                               index, getElementModel(ped), x, y, z, rotation, tostring(chatActive), tostring(primaryTask),
                               #hierarchy > 0 and table.concat(hierarchy, ">") or "nil")
        end
    end

    local separation = -1
    if isElement(encounter.enemies[1]) and isElement(encounter.enemies[2]) then
        local x1, y1, z1 = getElementPosition(encounter.enemies[1])
        local x2, y2, z2 = getElementPosition(encounter.enemies[2])
        separation = getDistanceBetweenPoints3D(x1, y1, z1, x2, y2, z2)
    end
    outputDebugString(("[tagging-up-turf] Ballas PartnerChat #%d %s separation=%.3f m; %s; %s"):format(
                          encounter.id, tostring(phase), separation, parts[1] or "Flat1=?", parts[2] or "Flat2=?"))
end

local function beginBallasEncounterChat(encounter)
    if state.ballasEncounter ~= encounter or localPlayer ~= state.leader or encounter.chatReported then
        return
    end
    if getTickCount() - encounter.requestedAt > TAGUP.ballasGangScene.readyTimeout then
        return reportBallasEncounterTask(encounter, "chat", "timeout", "les deux Flats ne sont pas devenus syncer-owned et streames")
    end

    for _, ped in ipairs(encounter.enemies) do
        if not isElement(ped) or isPedDead(ped) then
            return reportBallasEncounterTask(encounter, "chat", "actor_unavailable", "un Flat est absent avant 0677")
        end
        if not isElementStreamedIn(ped) or not isElementSyncer(ped) then
            if not isTimer(encounter.chatRetryTimer) then
                encounter.chatRetryTimer = setTimer(function()
                    encounter.chatRetryTimer = nil
                    beginBallasEncounterChat(encounter)
                end, 100, 1)
            end
            return
        end
        applyMissionActor(ped)
    end

    -- The installed speech containers have silent samples. Select GTA's own
    -- timed PartnerChat fallback explicitly so actor positioning and chat idle
    -- behavior remain native without depending on an audible conversation.
    traceCurrent("ballas_chat", "NATIVE VERIFIED · assigning reciprocal 0677 events to both Flats")
    if type(setPedChatWith) ~= "function" or not setPedChatWith(encounter.enemies[2], encounter.enemies[1], false, true, false) or
        not setPedChatWith(encounter.enemies[1], encounter.enemies[2], true, true, false) then
        return reportBallasEncounterTask(encounter, "chat", "refused", "TASK_COMPLEX_PARTNER_CHAT refusee")
    end
    traceBallasPartnerState(encounter, "assigned")
    for _, delay in ipairs({100, 500, 1500, 3000}) do
        local expected = encounter
        local elapsed = delay
        setTimer(function()
            if state.ballasEncounter == expected then
                traceBallasPartnerState(expected, ("after_%d_ms"):format(elapsed))
            end
        end, elapsed, 1)
    end

    -- The native scripted-task dispatcher queues a CEventScriptCommand. Its
    -- return value confirms ownership transfer, not that GTA has processed the
    -- event and installed both paired tasks. Requiring ten consecutive native
    -- observations also rejects the former direct-assignment failure, where one
    -- PartnerChat disappeared before the camera started.
    encounter.chatActiveSamples = 0
    encounter.chatObservationTimer = setTimer(function()
        if state.ballasEncounter ~= encounter or encounter.chatReported then
            if isTimer(encounter.chatObservationTimer) then
                killTimer(encounter.chatObservationTimer)
            end
            encounter.chatObservationTimer = nil
            return
        end

        local bothActive = true
        for _, ped in ipairs(encounter.enemies) do
            bothActive = bothActive and isElement(ped) and not isPedDead(ped) and isElementStreamedIn(ped) and
                             isElementSyncer(ped) and type(isPedDoingTask) == "function" and
                             isPedDoingTask(ped, "TASK_COMPLEX_PARTNER_CHAT")
        end
        encounter.chatActiveSamples = bothActive and encounter.chatActiveSamples + 1 or 0
        if encounter.chatActiveSamples >= 10 then
            killTimer(encounter.chatObservationTimer)
            encounter.chatObservationTimer = nil
            traceBallasPartnerState(encounter, "active_barrier")
            return reportBallasEncounterTask(encounter, "chat", "ready",
                                             "deux PartnerChat silencieuses natives actives pendant dix observations")
        end

        if getTickCount() - encounter.requestedAt >= TAGUP.ballasGangScene.readyTimeout then
            killTimer(encounter.chatObservationTimer)
            encounter.chatObservationTimer = nil
            traceBallasPartnerState(encounter, "activation_timeout")
            reportBallasEncounterTask(encounter, "chat", "timeout", "les deux PartnerChat ne sont pas restees actives")
        end
    end, 50, 0)
end

addEvent("tagup:ballasEncounterPrepare", true)
addEventHandler("tagup:ballasEncounterPrepare", resourceRoot, function(encounterId, enemies)
    if source ~= resourceRoot or not state.active or state.stage ~= "tags_ballas" or type(encounterId) ~= "number" or type(enemies) ~= "table" or
        #enemies ~= 2 then
        return
    end
    if state.ballasEncounter then
        if state.ballasEncounter.id >= encounterId then
            return
        end
        clearBallasEncounter("replaced_by_newer_encounter", true)
    end

    local preload = state.ballasEncounterAudioPreload
    state.ballasEncounterAudioPreload = nil
    local encounter = {
        id = encounterId,
        enemies = enemies,
        phase = "chat",
        requestedAt = getTickCount(),
        audioHandles = preload and preload.audioHandles or {},
    }
    state.ballasEncounter = encounter
    beginBallasEncounterChat(encounter)
end)

addEvent("tagup:ballasEncounterApproachEnabled", true)
addEventHandler("tagup:ballasEncounterApproachEnabled", resourceRoot, function(encounterId)
    local encounter = state.ballasEncounter
    if source ~= resourceRoot or not encounter or encounter.id ~= encounterId then
        return
    end
    encounter.phase = "awaiting_approach"
    encounter.approachLastReportedAt = nil
    encounter.approachReportCount = 0
    printMissionHelp("SWE1_I", true)
    traceCurrent("ballas_tags")
end)

addEvent("tagup:ballasEncounterFollow", true)
addEventHandler("tagup:ballasEncounterFollow", resourceRoot, function(encounterId)
    local encounter = state.ballasEncounter
    if source ~= resourceRoot or not encounter or encounter.id ~= encounterId then
        return
    end
    encounter.phase = "following"
    outputDebugString(('[tagging-up-turf] Ballas encounter #%d approach ACK after %d report(s)'):format(
                          encounter.id, encounter.approachReportCount or 0))
    if localPlayer ~= state.leader then
        return
    end

    local profile = TAGUP.ballasGangScene.follow
    encounter.speechMuted = true
    traceCurrent("ballas_follow", "NATIVE VERIFIED · 0A09 + 05BA + repeated 06A8 for both Flats")
    for index, ped in ipairs(encounter.enemies) do
        if isElement(ped) and not isPedDead(ped) then
            if not isElementStreamedIn(ped) or not isElementSyncer(ped) or type(setPedScriptedSpeechMuted) ~= "function" or
                type(setPedStandStill) ~= "function" or type(setPedGoToOffset) ~= "function" or not setPedScriptedSpeechMuted(ped, true) or
                not setPedStandStill(ped, 0) or
                not setPedGoToOffset(ped, localPlayer, profile.timeout, profile.radius, profile.angles[index], true) then
                return reportBallasEncounterTask(encounter, "follow", "refused", "05BA ou sequence 06A8 refusee pour Flat " .. index)
            end
        end
    end
    reportBallasEncounterTask(encounter, "follow", "ready", "speech mutee, StandStill puis UseSequence 06A8 repetee")
end)

addEvent("tagup:ballasEncounterAttack", true)
addEventHandler("tagup:ballasEncounterAttack", resourceRoot, function(encounterId, reason)
    local encounter = state.ballasEncounter
    if source ~= resourceRoot or not encounter or encounter.id ~= encounterId then
        return
    end
    encounter.phase = "attacking"
    if localPlayer == state.leader then
        traceCurrent("ballas_attack", "NATIVE VERIFIED · assigning 05E2 against the leader to both Flats")
        for index, ped in ipairs(encounter.enemies) do
            if isElement(ped) and not isPedDead(ped) and
                (not isElementStreamedIn(ped) or not isElementSyncer(ped) or type(setPedKillOnFoot) ~= "function" or
                 not setPedKillOnFoot(ped, localPlayer)) then
                return reportBallasEncounterTask(encounter, "attack", "refused", "TASK_COMPLEX_KILL_PED_ON_FOOT refusee pour Flat " .. index)
            end
        end
        reportBallasEncounterTask(encounter, "attack", "ready", "deux 05E2 ciblent le leader; trigger=" .. tostring(reason))
    end
end)

addEvent("tagup:ballasEncounterAudioCue", true)
addEventHandler("tagup:ballasEncounterAudioCue", resourceRoot, function(encounterId, cue)
    local encounter = state.ballasEncounter
    if source ~= resourceRoot or not encounter or encounter.id ~= encounterId or (cue ~= "whatTheFuck" and cue ~= "getThatFool") then
        return
    end
    local audioStarted = playBallasEncounterAudio(encounter, cue, cue == "whatTheFuck" and "SWE1_BA" or "SWE1_BE")
    if not audioStarted or type(isMissionAudioFinished) ~= "function" then
        releaseBallasEncounterAudioCue(encounter, cue)
        if cue == "getThatFool" then
            restoreBallasEncounterSpeech(encounter)
        end
        return
    end
    local handle = encounter.audioHandles[cue]
    encounter.audioFinishTimers = encounter.audioFinishTimers or {}
    encounter.audioFinishTimers[cue] = setTimer(function()
        if state.ballasEncounter ~= encounter then
            return
        end
        local ok, finished = pcall(isMissionAudioFinished, handle)
        if not ok or finished then
            local timer = encounter.audioFinishTimers and encounter.audioFinishTimers[cue]
            if isTimer(timer) then
                killTimer(timer)
            end
            encounter.audioFinishTimers[cue] = nil
            releaseBallasEncounterAudioCue(encounter, cue)
            if cue == "getThatFool" then
                restoreBallasEncounterSpeech(encounter)
            end
        end
    end, 50, 0)
end)

addEvent("tagup:ballasEncounterSpeechRestore", true)
addEventHandler("tagup:ballasEncounterSpeechRestore", resourceRoot, function(encounterId)
    local encounter = state.ballasEncounter
    if source == resourceRoot and encounter and encounter.id == encounterId then
        restoreBallasEncounterSpeech(encounter)
    end
end)

addEvent("tagup:ballasEncounterCancel", true)
addEventHandler("tagup:ballasEncounterCancel", resourceRoot, function(encounterId, reason)
    if state.ballasEncounter and state.ballasEncounter.id == encounterId then
        clearBallasEncounter(reason, true)
    end
end)

local function hasBallasGangSceneLease(scene)
    return scene and scene.cameraToken and type(isScriptCameraLeaseActive) == "function" and
               isScriptCameraLeaseActive(scene.cameraToken)
end

local function releaseBallasGangSceneCamera(scene, reason)
    if not scene or not scene.cameraToken then
        return true
    end

    local token = scene.cameraToken
    scene.cameraToken = nil
    local released = false
    if type(releaseScriptCamera) == "function" then
        local ok, result = pcall(releaseScriptCamera, token)
        released = ok and result ~= false
    end
    outputDebugString(("[tagging-up-turf] Ballas gang camera #%d release=%s token=%s reason=%s elapsed=%d ms"):format(
                          scene.id, tostring(released), tostring(token), tostring(reason or "cleanup"),
                          getTickCount() - (scene.preparedAt or getTickCount())), released and 3 or 2)
    return released
end

local function clearBallasGangScene(reason)
    local scene = state.ballasGangScene
    if not scene then
        return true
    end
    if isTimer(scene.actorReadyTimer) then
        killTimer(scene.actorReadyTimer)
    end
    if isTimer(scene.cameraLeaseMonitorTimer) then
        killTimer(scene.cameraLeaseMonitorTimer)
    end
    local released = releaseBallasGangSceneCamera(scene, reason)
    state.ballasGangScene = nil
    return released
end

local function reportBallasGangSceneReady(scene, result, details)
    if not scene or scene.readyReported then
        return
    end
    scene.readyReported = true
    triggerServerEvent("tagup:ballasGangSceneReady", resourceRoot, scene.id, result, details)
end

local function finishBallasGangSceneSetup(scene)
    if state.ballasGangScene ~= scene or scene.readyReported then
        return
    end

    local allActorsReady = true
    for _, ped in ipairs(scene.enemies) do
        if not isElement(ped) or isPedDead(ped) then
            clearBallasGangScene("actor_unavailable")
            return reportBallasGangSceneReady(scene, "actor_unavailable", "un des deux Flats est absent ou mort")
        end
        allActorsReady = allActorsReady and isElementStreamedIn(ped)
    end
    if not allActorsReady then
        if getTickCount() - scene.requestedAt >= TAGUP.ballasGangScene.readyTimeout - 500 then
            clearBallasGangScene("actors_not_streamed")
            return reportBallasGangSceneReady(scene, "actors_not_streamed", "les deux Flats ne sont pas streames")
        end
        return
    end

    if not hasBallasGangSceneLease(scene) then
        scene.leaseLost = true
        return triggerServerEvent("tagup:ballasGangSceneLeaseLost", resourceRoot, scene.id)
    end

    if isTimer(scene.actorReadyTimer) then
        killTimer(scene.actorReadyTimer)
        scene.actorReadyTimer = nil
    end
    scene.preparedAt = getTickCount()
    traceCurrent("ballas_gang_camera")
    outputDebugString(("[tagging-up-turf] Ballas gang camera #%d ready token=%s in %d ms"):format(
                          scene.id, tostring(scene.cameraToken), getTickCount() - scene.requestedAt))
    reportBallasGangSceneReady(scene, "ready", "fixed camera, widescreen, controls and both Flats ready")
end

local function prepareBallasGangScene(scene)
    local camera = TAGUP.ballasGangScene.camera
    traceBallasPartnerState(state.ballasEncounter, "camera_prepare")
    local ok, result = callBallasCameraApi("acquireScriptCamera", true)
    if not ok then
        return reportBallasGangSceneReady(scene, "acquire_refused", result)
    end

    scene.cameraToken = result
    ok, result = callBallasCameraApi("resetScriptCamera", scene.cameraToken)
    if ok then
        ok, result = callBallasCameraApi("setScriptCameraWidescreen", scene.cameraToken, true)
    end
    if ok then
        ok, result = callBallasCameraApi("setScriptCameraFixed", scene.cameraToken,
                                        Vector3(camera.position.x, camera.position.y, camera.position.z),
                                        Vector3(camera.target.x, camera.target.y, camera.target.z), Vector3(0, 0, 0), true)
    end
    if not ok then
        clearBallasGangScene("setup_refused")
        return reportBallasGangSceneReady(scene, "setup_refused", result)
    end

    for _, delay in ipairs({50, 250}) do
        local expected = scene
        local elapsed = delay
        setTimer(function()
            if state.ballasGangScene ~= expected then
                return
            end
            local px, py, pz, tx, ty, tz, roll, fov = getCameraMatrix()
            outputDebugString(("[tagging-up-turf] Ballas camera #%d after_%d_ms pos=(%.4f,%.4f,%.4f) target=(%.4f,%.4f,%.4f) roll=%.2f fov=%.2f"):format(
                                  expected.id, elapsed, px, py, pz, tx, ty, tz, roll, fov))
        end, elapsed, 1)
    end

    -- Monitor from the moment native setup succeeds, including the actor
    -- streaming wait, so a competing camera cannot produce a false ready ACK.
    scene.cameraLeaseMonitorTimer = setTimer(function()
        local active = state.ballasGangScene
        if active ~= scene or active.leaseLost or not active.cameraToken then
            return
        end
        if not hasBallasGangSceneLease(active) then
            active.leaseLost = true
            triggerServerEvent("tagup:ballasGangSceneLeaseLost", resourceRoot, active.id)
        end
    end, 100, 0)

    scene.actorReadyTimer = setTimer(function()
        finishBallasGangSceneSetup(scene)
    end, 100, 0)
    finishBallasGangSceneSetup(scene)
end

addEvent("tagup:ballasGangScenePrepare", true)
addEventHandler("tagup:ballasGangScenePrepare", resourceRoot, function(sceneId, enemies)
    if source ~= resourceRoot or not state.active or state.stage ~= "tags_ballas" or type(sceneId) ~= "number" or type(enemies) ~= "table" or
        #enemies ~= 2 then
        return
    end
    if state.ballasGangScene then
        if state.ballasGangScene.id >= sceneId then
            outputDebugString(("[tagging-up-turf] Ignoring stale Ballas gang scene prepare #%s"):format(tostring(sceneId)), 2)
            return
        end
        clearBallasGangScene("replaced_by_newer_scene")
    end

    state.ballasGangScene = {id = sceneId, enemies = enemies, requestedAt = getTickCount()}
    prepareBallasGangScene(state.ballasGangScene)
end)

addEvent("tagup:ballasGangSceneStart", true)
addEventHandler("tagup:ballasGangSceneStart", resourceRoot, function(sceneId)
    local scene = state.ballasGangScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or scene.started or not hasBallasGangSceneLease(scene) then
        return
    end
    scene.started = true
    scene.startedAt = getTickCount()
    printMissionHelp("SWE1_H")
    outputDebugString(("[tagging-up-turf] Ballas gang scene #%d timeline started"):format(scene.id))
end)

addEvent("tagup:ballasGangSceneSkippable", true)
addEventHandler("tagup:ballasGangSceneSkippable", resourceRoot, function(sceneId, leaderCanSkip)
    local scene = state.ballasGangScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or not scene.started then
        return
    end
    scene.skippable = true
    scene.leaderCanSkip = leaderCanSkip == true
end)

addEvent("tagup:ballasGangSceneFinalCheck", true)
addEventHandler("tagup:ballasGangSceneFinalCheck", resourceRoot, function(sceneId)
    local scene = state.ballasGangScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId then
        return
    end
    local active = hasBallasGangSceneLease(scene)
    if not active then
        scene.leaseLost = true
    end
    triggerServerEvent("tagup:ballasGangSceneFinalResult", resourceRoot, scene.id, active and "ready" or "lost")
end)

addEvent("tagup:ballasGangSceneRelease", true)
addEventHandler("tagup:ballasGangSceneRelease", resourceRoot, function(sceneId, reason)
    local scene = state.ballasGangScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId then
        return
    end
    local released = clearBallasGangScene(reason)
    triggerServerEvent("tagup:ballasGangSceneReleased", resourceRoot, sceneId, released and "released" or "release_failed")
end)

addEvent("tagup:ballasGangSceneCancel", true)
addEventHandler("tagup:ballasGangSceneCancel", resourceRoot, function(sceneId, reason)
    if state.ballasGangScene and state.ballasGangScene.id == sceneId then
        outputDebugString(("[tagging-up-turf] Ballas gang scene #%d closed: %s"):format(sceneId, tostring(reason)))
        clearBallasGangScene(reason)
    end
end)

addEvent("tagup:stopBallasWander", true)
addEventHandler("tagup:stopBallasWander", resourceRoot, function()
    if isElement(state.ballasWanderPed) and isElementSyncer(state.ballasWanderPed) then
        killPedTask(state.ballasWanderPed, "primary", 3, false)
    end
    state.ballasWanderPed = nil
end)

local function hasPostRoofCameraLease(scene)
    return scene and scene.cameraToken and type(isScriptCameraLeaseActive) == "function" and
               isScriptCameraLeaseActive(scene.cameraToken)
end

local function clearPostRoofScene(reason)
    local scene = state.postRoofScene
    if not scene then
        return true
    end
    for _, timer in ipairs({scene.fadeTimer, scene.leaseTimer, scene.audioLoadTimer, scene.audioFinishTimer}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end

    local audioReleased = true
    if scene.audioHandle then
        local ok, result = pcall(releaseMissionAudio, scene.audioHandle)
        audioReleased = ok and result ~= false
        scene.audioHandle = nil
    end
    local cameraReleased = true
    if scene.cameraToken then
        local ok, result = pcall(releaseScriptCamera, scene.cameraToken)
        cameraReleased = ok and result ~= false
        scene.cameraToken = nil
    end
    outputDebugString(('[tagging-up-turf] Post-roof scene #%d cleared reason=%s camera=%s audio=%s'):format(
                          scene.id, tostring(reason or "cleanup"), tostring(cameraReleased), tostring(audioReleased)),
                      cameraReleased and audioReleased and 3 or 2)
    state.postRoofScene = nil
    return cameraReleased and audioReleased
end

local function failPostRoofScene(scene, result, details)
    if state.postRoofScene ~= scene or scene.failed then
        return
    end
    scene.failed = true
    triggerServerEvent("tagup:postRoofSceneFailure", resourceRoot, scene.id, result, details)
    clearPostRoofScene(result)
end

local function reportPostRoofAudioReady(scene, result, details)
    if state.postRoofScene ~= scene or scene.audioReadyReported then
        return
    end
    scene.audioReadyReported = true
    triggerServerEvent("tagup:postRoofAudioReady", resourceRoot, scene.id, result, details)
end

local function waitForPostRoofAudio(scene)
    local function poll()
        if state.postRoofScene ~= scene or scene.audioReadyReported then
            return
        end
        local ok, loaded = pcall(isMissionAudioLoaded, scene.audioHandle)
        if not ok then
            return reportPostRoofAudioReady(scene, "query_failed", tostring(loaded))
        end
        if loaded then
            return reportPostRoofAudioReady(scene, "ready", ("event=%d loaded in %d ms"):format(
                                                scene.profile.audio.dialogueEvent, getTickCount() - scene.audioRequestedAt))
        end
        if getTickCount() - scene.audioRequestedAt >= scene.profile.audio.loadTimeout then
            return reportPostRoofAudioReady(scene, "load_timeout", ("event=%d"):format(scene.profile.audio.dialogueEvent))
        end
    end
    scene.audioLoadTimer = setTimer(poll, 100, 0)
    poll()
end

local function finishPostRoofPrepare(scene)
    if state.postRoofScene ~= scene or not hasPostRoofCameraLease(scene) then
        return failPostRoofScene(scene, "camera_lost", "lease absente avant 0A0B")
    end
    local profile, preload, camera = scene.profile, scene.profile.preload, scene.profile.camera
    local preloadStartedAt = getTickCount()
    outputDebugString(('[tagging-up-turf] Post-roof scene #%d entering native 0A0B'):format(scene.id))
    local ok, result = pcall(enginePreloadWorldAreaInDirection, Vector3(preload.x, preload.y, preload.z), preload.heading)
    if not ok or result ~= true then
        return failPostRoofScene(scene, "preload_refused", tostring(result))
    end
    outputDebugString(('[tagging-up-turf] Post-roof scene #%d native 0A0B returned after %d ms'):format(
                          scene.id, getTickCount() - preloadStartedAt))
    traceCurrent("post_roof_preload", ("NATIVE VERIFIED · 0A0B point=(%.4f, %.4f, %.4f) heading=%.4f"):format(
                     preload.x, preload.y, preload.z, preload.heading))

    local cameraOk = resetScriptCamera(scene.cameraToken) and setScriptCameraWidescreen(scene.cameraToken, true) and
                         setScriptCameraFixed(scene.cameraToken, Vector3(camera.position.x, camera.position.y, camera.position.z),
                                              Vector3(camera.target.x, camera.target.y, camera.target.z), Vector3(0, 0, 0), true) and
                         fadeScriptCamera(scene.cameraToken, true, camera.fadeDuration, 0, 0, 0)
    if not cameraOk then
        return failPostRoofScene(scene, "camera_setup_refused", "fixed camera ou fade-in refuse")
    end

    local requested, handle = pcall(requestMissionAudio, profile.audio.dialogueEvent)
    if not requested or not handle then
        return failPostRoofScene(scene, "audio_request_refused", tostring(handle))
    end
    scene.audioHandle = handle
    scene.audioRequestedAt = getTickCount()
    scene.leaseTimer = setTimer(function()
        if state.postRoofScene == scene and not hasPostRoofCameraLease(scene) then
            failPostRoofScene(scene, "camera_lost", "lease perdue pendant la scene")
        end
    end, 100, 0)
    waitForPostRoofAudio(scene)
    triggerServerEvent("tagup:postRoofSceneReady", resourceRoot, scene.id, scene.vehicle, "ready",
                       ("0A0B + fixed camera; SWE1_BH handle=%s"):format(tostring(handle)))
end

addEvent("tagup:postRoofScenePrepare", true)
addEventHandler("tagup:postRoofScenePrepare", resourceRoot, function(sceneId, vehicle, profile)
    if source ~= resourceRoot or not state.active or state.stage ~= "rooftop" or not isElement(vehicle) or type(profile) ~= "table" then
        return
    end
    clearPostRoofScene("replaced")
    local required = {"acquireScriptCamera", "releaseScriptCamera", "fadeScriptCamera", "resetScriptCamera", "setScriptCameraWidescreen",
                      "setScriptCameraFixed", "enginePreloadWorldAreaInDirection", "requestMissionAudio", "isMissionAudioLoaded",
                      "playMissionAudio", "isMissionAudioFinished", "releaseMissionAudio", "reportVehicleMissionAudioEvent"}
    for _, name in ipairs(required) do
        if type(_G[name]) ~= "function" then
            triggerServerEvent("tagup:postRoofSceneReady", resourceRoot, sceneId, vehicle, "api_unavailable", name)
            return
        end
    end

    local scene = {id = sceneId, vehicle = vehicle, profile = profile, requestedAt = getTickCount()}
    state.postRoofScene = scene
    local acquired, token = pcall(acquireScriptCamera, true)
    if not acquired or not token then
        return failPostRoofScene(scene, "camera_acquire_refused", tostring(token))
    end
    scene.cameraToken = token
    if not fadeScriptCamera(token, false, profile.camera.fadeDuration, 0, 0, 0) then
        return failPostRoofScene(scene, "fade_out_refused", "DO_FADE 300")
    end
    -- MTA serializes timer arguments through CLuaArguments, which deep-copies
    -- tables. Capture the live scene in a closure so the identity guard in
    -- finishPostRoofPrepare sees the resource-owned state object.
    scene.fadeTimer = setTimer(function()
        finishPostRoofPrepare(scene)
    end, math.floor(profile.camera.fadeDuration * 1000) + 50, 1)
end)

addEvent("tagup:postRoofFirstHorn", true)
addEventHandler("tagup:postRoofFirstHorn", resourceRoot, function(sceneId, vehicle)
    local scene = state.postRoofScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or scene.vehicle ~= vehicle then
        return
    end
    local ok, result = pcall(reportVehicleMissionAudioEvent, vehicle, scene.profile.audio.hornEvent)
    if not ok or result ~= true then
        return failPostRoofScene(scene, "first_horn_refused", tostring(result))
    end
    traceCurrent("post_roof_horn", "NATIVE VERIFIED · first 09F7 / event 1147")
end)

addEvent("tagup:postRoofAudioStart", true)
addEventHandler("tagup:postRoofAudioStart", resourceRoot, function(sceneId, vehicle)
    local scene = state.postRoofScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or scene.vehicle ~= vehicle or scene.audioStarted then
        return
    end
    local hornOk, hornResult = pcall(reportVehicleMissionAudioEvent, vehicle, scene.profile.audio.hornEvent)
    local playOk, playResult = pcall(playMissionAudio, scene.audioHandle)
    if not hornOk or hornResult ~= true or not playOk or playResult ~= true then
        return failPostRoofScene(scene, "audio_start_refused", ("horn=%s play=%s"):format(tostring(hornResult), tostring(playResult)))
    end
    scene.audioStarted = true
    scene.audioStartedAt = getTickCount()
    printMissionText("SWE1_BH", 10000)
    traceProgress("post_roof_horn", 1, "NATIVE VERIFIED · second 09F7 / event 1147")
    traceCurrent("post_roof_audio", "NATIVE MISSION AUDIO · event 37430 / natural finish")
    scene.audioFinishTimer = setTimer(function()
        if state.postRoofScene ~= scene then
            return
        end
        local ok, finished = pcall(isMissionAudioFinished, scene.audioHandle)
        local elapsed = getTickCount() - scene.audioStartedAt
        if not ok then
            return failPostRoofScene(scene, "audio_query_failed", tostring(finished))
        end
        if finished then
            killTimer(scene.audioFinishTimer)
            scene.audioFinishTimer = nil
            traceProgress("post_roof_audio", 1, ("NATIVE MISSION AUDIO · natural finish after %d ms"):format(elapsed))
            triggerServerEvent("tagup:postRoofAudioResult", resourceRoot, scene.id, "finished", ("elapsed=%d ms"):format(elapsed))
        elseif elapsed >= scene.profile.audio.finishTimeout then
            failPostRoofScene(scene, "audio_finish_timeout", ("elapsed=%d ms"):format(elapsed))
        end
    end, 100, 0)
end)

addEvent("tagup:postRoofFlatsWander", true)
addEventHandler("tagup:postRoofFlatsWander", resourceRoot, function(sceneId, flats)
    local scene = state.postRoofScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId or localPlayer ~= state.leader or type(flats) ~= "table" then
        return
    end
    traceCurrent("post_roof_wander")
    for _, ped in ipairs(flats) do
        if not isElement(ped) or isPedDead(ped) or not isElementSyncer(ped) then
            return triggerServerEvent("tagup:postRoofFlatsResult", resourceRoot, scene.id, "ownership_refused", tostring(ped))
        end
        killPedTask(ped, "primary", 3, false)
        if not setPedWander(ped, "walk") then
            return triggerServerEvent("tagup:postRoofFlatsResult", resourceRoot, scene.id, "task_refused", tostring(ped))
        end
    end
    traceProgress("post_roof_wander", 1, ("NATIVE VERIFIED · %d surviving Flats"):format(#flats))
    triggerServerEvent("tagup:postRoofFlatsResult", resourceRoot, scene.id, "ready", ("count=%d"):format(#flats))
end)

addEvent("tagup:postRoofSceneRelease", true)
addEventHandler("tagup:postRoofSceneRelease", resourceRoot, function(sceneId)
    local scene = state.postRoofScene
    if source ~= resourceRoot or not scene or scene.id ~= sceneId then
        return
    end
    local released = clearPostRoofScene("completed")
    triggerServerEvent("tagup:postRoofSceneReleased", resourceRoot, sceneId, released and "released" or "release_failed")
end)

addEvent("tagup:postRoofSceneCancel", true)
addEventHandler("tagup:postRoofSceneCancel", resourceRoot, function(sceneId, reason)
    if state.postRoofScene and state.postRoofScene.id == sceneId then
        clearPostRoofScene(reason)
    end
end)

local function clearVehiclePlayback(stopNative)
    local playback = state.vehiclePlayback
    if not playback then
        return
    end
    if isTimer(playback.prepareTimer) then
        killTimer(playback.prepareTimer)
    end
    if isTimer(playback.startTimer) then
        killTimer(playback.startTimer)
    end
    if isTimer(playback.monitorTimer) then
        killTimer(playback.monitorTimer)
    end
    if stopNative and isElement(playback.vehicle) and type(isVehiclePlaybackActive) == "function" and
        isVehiclePlaybackActive(playback.vehicle) and type(stopVehiclePlayback) == "function" then
        stopVehiclePlayback(playback.vehicle)
    end
    state.vehiclePlayback = nil
end

local function reportVehiclePlayback(result, details, terminal)
    local playback = state.vehiclePlayback
    if not playback then
        return
    end
    local elapsed = playback.startedAt and getTickCount() - playback.startedAt or nil
    triggerServerEvent("tagup:vehiclePlaybackResult", resourceRoot, playback.id, playback.ped, playback.vehicle, result, details, elapsed)
    if terminal then
        clearVehiclePlayback(false)
    end
end

local function prepareVehiclePlayback()
    local playback = state.vehiclePlayback
    if not playback then
        return
    end
    if not isElement(playback.ped) or not isElement(playback.vehicle) then
        return reportVehiclePlayback("destroyed", "Sweet ou Greenwood absent pendant la preparation", true)
    end
    if not isElementStreamedIn(playback.ped) or not isElementStreamedIn(playback.vehicle) or not isElementSyncer(playback.ped) or
        not isElementSyncer(playback.vehicle) then
        if getTickCount() - playback.requestedAt < playback.profile.ownershipTimeout then
            playback.prepareTimer = setTimer(prepareVehiclePlayback, 250, 1)
            return
        end
        return reportVehiclePlayback("ownership_refused", "leader non double-syncer avant le recording", true)
    end
    if type(requestVehicleRecording) ~= "function" or type(isVehicleRecordingLoaded) ~= "function" or type(startVehiclePlayback) ~= "function" or
        type(stopVehiclePlayback) ~= "function" or type(isVehiclePlaybackActive) ~= "function" then
        return reportVehiclePlayback("api_unavailable", "API recorded-car absente du client Neon", true)
    end

    if isElement(state.ballasWanderPed) and isElementSyncer(state.ballasWanderPed) then
        killPedTask(state.ballasWanderPed, "primary", 3, false)
    end
    state.ballasWanderPed = nil
    if isPedDoingTask(playback.ped, BALLAS_WANDER_TASK) then
        if getTickCount() - playback.requestedAt < playback.profile.ownershipTimeout then
            playback.prepareTimer = setTimer(prepareVehiclePlayback, 50, 1)
            return
        end
        return reportVehiclePlayback("wander_not_stopped", "TASK_COMPLEX_CAR_DRIVE_WANDER toujours active", true)
    end

    traceCurrent("request_carrec")
    if not playback.requestAccepted then
        if not requestVehicleRecording(playback.profile.id) then
            return reportVehiclePlayback("request_refused", "requestVehicleRecording a retourne false", true)
        end
        playback.requestAccepted = true
        traceProgress("request_carrec", 1, "NATIVE VERIFIED · recording 207 requested")
        traceCurrent("load_carrec")
    end
    if not isVehicleRecordingLoaded(playback.profile.id) then
        if getTickCount() - playback.requestedAt < playback.profile.loadTimeout then
            playback.prepareTimer = setTimer(prepareVehiclePlayback, 100, 1)
            return
        end
        return reportVehiclePlayback("load_timeout", "recording 207 non charge apres le delai", true)
    end

    traceProgress("load_carrec", 1, "NATIVE VERIFIED · recording 207 loaded")
    playback.ready = true
    reportVehiclePlayback("ready", "Wander arrete et recording 207 charge", false)
end

local function beginVehiclePlayback()
    local playback = state.vehiclePlayback
    if not playback or not playback.ready then
        return
    end
    if not isElement(playback.ped) or not isElement(playback.vehicle) then
        return reportVehiclePlayback("destroyed", "Sweet ou Greenwood absent avant 05EB", true)
    end
    if not isElementStreamedIn(playback.ped) or not isElementStreamedIn(playback.vehicle) or not isElementSyncer(playback.ped) or
        not isElementSyncer(playback.vehicle) then
        if getTickCount() - playback.startRequestedAt < playback.profile.ownershipTimeout then
            playback.startTimer = setTimer(beginVehiclePlayback, 100, 1)
            return
        end
        return reportVehiclePlayback("ownership_refused", "double sync perdu avant 05EB", true)
    end
    if getVehicleOccupant(playback.vehicle, 0) ~= playback.ped then
        if getTickCount() - playback.startRequestedAt < playback.profile.ownershipTimeout then
            playback.startTimer = setTimer(beginVehiclePlayback, 100, 1)
            return
        end
        return reportVehiclePlayback("driver_missing", "Sweet non observe au volant avant 05EB", true)
    end

    traceCurrent("start_playback")
    if not startVehiclePlayback(playback.vehicle, playback.profile.id) then
        return reportVehiclePlayback("start_refused", "startVehiclePlayback a retourne false", true)
    end
    playback.startedAt = getTickCount()
    playback.observedActive = isVehiclePlaybackActive(playback.vehicle)
    traceProgress("start_playback", 1, "NATIVE VERIFIED · 05EB accepted by vehicle syncer")
    traceCurrent("playback_wait")
    reportVehiclePlayback("started", "05EB actif; attente de la fin naturelle", false)

    playback.monitorTimer = setTimer(function()
        local active = state.vehiclePlayback
        if not active then
            return
        end
        if not isElement(active.vehicle) or not isElement(active.ped) then
            return reportVehiclePlayback("destroyed", "Sweet ou Greenwood detruit pendant 05EB", true)
        end
        if not isElementStreamedIn(active.vehicle) then
            return reportVehiclePlayback("streamed_out", "Greenwood sortie du streaming pendant 05EB", true)
        end
        if not isElementSyncer(active.vehicle) then
            if isVehiclePlaybackActive(active.vehicle) then
                stopVehiclePlayback(active.vehicle)
            end
            return reportVehiclePlayback("ownership_lost", "sync vehicule perdu pendant 05EB", true)
        end

        local running = isVehiclePlaybackActive(active.vehicle)
        active.observedActive = active.observedActive or running
        local elapsed = getTickCount() - active.startedAt
        traceProgress("playback_wait", math.min(0.99, elapsed / active.profile.nominalElapsed),
                      ("NATIVE VERIFIED · recording 207 / %d ms"):format(elapsed))
        if active.observedActive and not running then
            traceProgress("playback_wait", 1, ("NATIVE VERIFIED · natural end after %d ms"):format(elapsed))
            return reportVehiclePlayback("completed", "fin naturelle du recording 207 observee", true)
        end
        if elapsed > active.profile.maximumElapsed then
            if running then
                stopVehiclePlayback(active.vehicle)
            end
            return reportVehiclePlayback("playback_timeout", "recording 207 encore actif apres le plafond", true)
        end
    end, 50, 0)
end

addEvent("tagup:vehiclePlaybackPrepare", true)
addEventHandler("tagup:vehiclePlaybackPrepare", resourceRoot, function(playbackId, ped, vehicle, profile)
    clearVehiclePlayback(true)
    if not state.active or state.stage ~= "rooftop" or localPlayer ~= state.leader or not isElement(ped) or not isElement(vehicle) or
        type(profile) ~= "table" then
        return
    end
    state.vehiclePlayback = {
        id = playbackId,
        ped = ped,
        vehicle = vehicle,
        profile = profile,
        requestedAt = getTickCount(),
    }
    prepareVehiclePlayback()
end)

addEvent("tagup:vehiclePlaybackStart", true)
addEventHandler("tagup:vehiclePlaybackStart", resourceRoot, function(playbackId, ped, vehicle, profile)
    local playback = state.vehiclePlayback
    if not playback or playback.id ~= playbackId or playback.ped ~= ped or playback.vehicle ~= vehicle or type(profile) ~= "table" then
        return
    end
    playback.profile = profile
    playback.startRequestedAt = getTickCount()
    beginVehiclePlayback()
end)

addEvent("tagup:vehiclePlaybackCancel", true)
addEventHandler("tagup:vehiclePlaybackCancel", resourceRoot, function(playbackId, reason)
    local playback = state.vehiclePlayback
    if playback and playback.id == playbackId then
        outputDebugString(("[tagging-up-turf] Recording 207 #%d closed: %s"):format(playbackId, tostring(reason)))
        clearVehiclePlayback(reason ~= "completed")
    end
end)

addEvent("tagup:state", true)
addEventHandler("tagup:state", resourceRoot, function(payload)
    local previousStage = state.stage
    state.active = true
    state.stage = payload.stage
    state.vehicle = payload.vehicle
    state.sweet = payload.sweet
    state.demoTag = payload.demoTag
    if isElement(state.sweet) then
        applyMissionActor(state.sweet)
        applyStoryActorProtection(state.sweet)
    end
    state.vehiclePlayerOnlyLocked = payload.vehiclePlayerOnlyLocked == true
    if isElement(state.vehicle) then
        applyGreenwoodNativeState()
        tuneGreenwoodRadio()
    end
    state.leader = payload.leader
    state.tagProgress = payload.tagProgress or {}
    state.completedTags = payload.completedTags or {}
    refreshGangTagStates()

    if previousStage ~= state.stage then
        if previousStage == "sweet1a" and state.stage ~= "sweet1a" and state.fileCutscene then
            clearFileCutscene("stage_changed_to_" .. tostring(state.stage), state.stage == "intro")
        end
        state.allWheelsMismatchStage = nil
        state.allWheelsPassedStage = nil
        state.rooftopTagRevealed = false
        local arrival = state.arrivalGate
        local expectedArrivalStage = arrival and ({drive_idlewood = "demo", drive_ballas = "ballas_departure"})[arrival.stage]
        if arrival and (previousStage ~= arrival.stage or state.stage ~= expectedArrivalStage) then
            releaseArrivalGate("unexpected_stage_" .. tostring(state.stage))
        elseif arrival and isTimer(arrival.resendTimer) then
            killTimer(arrival.resendTimer)
            arrival.resendTimer = nil
        end
        if previousStage == "demo" and state.stage ~= "demo" then
            clearSweetDemoScene("stage_changed_to_" .. tostring(state.stage), false)
            clearDemoLeave(true)
            clearDemoWalk(true)
            clearDemoShoot(true)
            clearDemoSequence(true)
        end
        if previousStage == "ballas_departure" and state.stage ~= "ballas_departure" and state.ballasDeparture then
            clearBallasDeparture(state.stage ~= "tags_ballas", "stage_changed_to_" .. tostring(state.stage))
        end
        if previousStage == "tags_ballas" and state.stage ~= "tags_ballas" and state.ballasGangScene then
            clearBallasGangScene("stage_changed_to_" .. tostring(state.stage))
        end
        if previousStage == "tags_ballas" and state.stage ~= "tags_ballas" and not state.ballasEncounter then
            clearBallasEncounterAudioPreload()
        end
        if previousStage == "rooftop" and state.stage ~= "rooftop" and state.postRoofScene then
            clearPostRoofScene("stage_changed_to_" .. tostring(state.stage))
        end
        if previousStage == "final_scene" and state.stage ~= "final_scene" and state.finalScene then
            clearFinalScene("stage_changed_to_" .. tostring(state.stage), false)
        end
        state.stageStarted = getTickCount()
        state.traceDemoTagActive = false
        local traceStep = STAGE_TRACE_STEP[state.stage]
        if state.stage == "failed" then
            traceFail(payload.failureReason)
        elseif traceStep and not payload.deferTraceStep then
            if payload.traceSkipped then
                traceSkipTo(traceStep)
            else
                traceCurrent(traceStep)
            end
            if state.stage == "complete" then
                TAGUP_TRACE.setStatus("mission_end", "done", "SERVER AUTHORITY · reward granted / state restore queued")
            end
        end
        if previousStage == "intro" and state.stage ~= "intro" then
            clearIntroScene("stage_changed_to_" .. tostring(state.stage))
        end
        if state.stage == "enter_car" or state.stage == "drive_idlewood" then
            startSweetDemoAudioPreload()
        elseif state.stage ~= "demo" then
            clearSweetDemoAudioPreload("stage_changed_to_" .. tostring(state.stage))
        end
        setStageNavigation(state.stage)
        beginMissionStageText(state.stage)
        if state.stage == "complete" and not state.missionPassedTunePlayed then
            state.missionPassedTunePlayed = true
            local ok, played = pcall(playMissionPassedTune, 1)
            outputDebugString(('[tagging-up-turf] Native PLAY_MISSION_PASSED_TUNE 1: %s'):format(tostring(ok and played == true)))
        end
    end
    if state.stage == "tags_idlewood" or state.stage == "tags_ballas" or (state.stage == "rooftop" and state.rooftopTagRevealed) then
        syncTagBlips()
    end
    if state.stage == "rooftop" and localPlayer == state.leader and not state.vehicleRecordingPreloaded and
        type(requestVehicleRecording) == "function" then
        state.vehicleRecordingPreloaded = requestVehicleRecording(TAGUP.vehicleRecording207.id)
        outputDebugString(("[tagging-up-turf] Recording 207 rooftop preload: %s"):format(tostring(state.vehicleRecordingPreloaded)))
    end
    if state.stage == "tags_ballas" and payload.enemies then
        traceCurrent("spawn_ballas")
    end
    updateTraceTagStage()
end)

addEvent("tagup:stop", true)
addEventHandler("tagup:stop", resourceRoot, function()
    clearTransitionAudio("mission_stopped")
    clearFileCutscene("mission_stopped", false)
    releaseGangTagStates()
    releaseArrivalGate("mission_stopped")
    clearSweetDemoAudioPreload("mission_stopped")
    clearSweetDemoScene("mission_stopped", false)
    clearDemoLeave(true)
    clearDemoWalk(true)
    clearDemoShoot(true)
    clearDemoSequence(true)
    clearSweetReturnEnter(true)
    clearBallasDeparture(true)
    clearBallasGangScene("mission_stopped")
    clearBallasEncounter("mission_stopped", true)
    clearBallasEncounterAudioPreload()
    clearVehiclePlayback(true)
    clearPostRoofScene("mission_stopped")
    clearFinalScene("mission_stopped", false)
    killMissionTextTimers()
    clearIntroScene("mission_stopped")
    destroyNavigation()
    if isElement(state.sweet) and type(setPedMissionActor) == "function" then
        setPedMissionActor(state.sweet, false)
    end
    if isElement(state.sweet) and type(setPedStoryProtected) == "function" then
        setPedStoryProtected(state.sweet, false)
    end
    state.active = false
    state.stage = nil
    state.vehicle = nil
    state.sweet = nil
    state.demoTag = nil
    state.leader = nil
    state.tagProgress = {}
    state.completedTags = {}
    state.traceStarted = false
    state.traceDemoTagActive = false
    state.traceCurrentStep = nil
    state.demoScene = nil
    state.demoAudioPreload = nil
    state.ballasEncounter = nil
    state.ballasEncounterAudioPreload = nil
    state.vehicleRecordingPreloaded = false
    state.postRoofScene = nil
    state.fileCutscene = nil
    state.finalScene = nil
    state.transitionAudio = nil
    state.vehiclePlayerOnlyLocked = false
    state.lastOffscreenStorageReport = 0
    state.greenwoodNativeLogMode = nil
    state.storyProtectionLogged = false
    state.missionPassedTunePlayed = false
    if state.missionTextReady then
        callMissionTextApi("releaseMissionText")
    end
    state.missionTextReady = false
    state.nativeTagHelpPhase = 0
    state.nativeTagHelpStarted = 0
    state.nativeHelpFlags = {}
    state.rooftopTagRevealed = false
    state.allWheelsMismatchStage = nil
    state.allWheelsPassedStage = nil
    if type(TAGUP_TRACE) == "table" then
        TAGUP_TRACE.toggle(false)
        TAGUP_TRACE.reset()
    end
end)

local function passesAllWheelsGate(vehicle, stage, insideBox)
    if not insideBox or not isElementStreamedIn(vehicle) or not isElementSyncer(vehicle) or
        type(isVehicleOnAllWheels) ~= "function" then
        return false
    end

    local onAllWheels = isVehicleOnAllWheels(vehicle)
    if not onAllWheels and state.allWheelsMismatchStage ~= stage and isVehicleOnGround(vehicle) then
        state.allWheelsMismatchStage = stage
        outputDebugString(('[tagging-up-turf] SCM 09D0 stage=%s blocked: legacy onGround=true, native allWheels=false'):format(stage))
    elseif onAllWheels and state.allWheelsPassedStage ~= stage then
        state.allWheelsPassedStage = stage
        outputDebugString(('[tagging-up-turf] SCM 09D0 stage=%s passed: native contact-wheel count is exactly four'):format(stage))
    end
    return onAllWheels
end

local function reportVehicleProgress()
    if not state.active or localPlayer ~= state.leader then
        return
    end
    local vehicle = getPedOccupiedVehicle(localPlayer)
    if vehicle ~= state.vehicle or getVehicleController(vehicle) ~= localPlayer then
        return
    end

    -- SWEET1 evaluates both LOCATE_CAR_3D gates after every WAIT 0. Detect the
    -- inclusive cube locally on the syncer and acquire the native control
    -- inhibitor in that same frame; the server still authorizes progression.
    if state.stage == "drive_idlewood" or state.stage == "drive_ballas" then
        if state.arrivalGate then
            return
        end
        local x, y, z = getElementPosition(vehicle)
        local target = state.stage == "drive_idlewood" and TAGUP.idlewoodDestination or TAGUP.ballasDestination
        local gate = state.stage == "drive_idlewood" and TAGUP.idlewoodArrival or TAGUP.ballasArrival
        local insideBox = math.abs(x - target[1]) <= gate.radiusX and math.abs(y - target[2]) <= gate.radiusY and
                              math.abs(z - target[3]) <= gate.radiusZ
        if passesAllWheelsGate(vehicle, state.stage, insideBox) then
            enterArrivalGate(state.stage, state.stage == "drive_idlewood" and "idlewood" or "ballas", vehicle)
        end
        return
    end

    if state.stage == "drive_home" then
        local x, y, z = getElementPosition(vehicle)
        local target, gate = TAGUP.homeDestination, TAGUP.homeArrival
        local insideBox = math.abs(x - target[1]) <= gate.radiusX and math.abs(y - target[2]) <= gate.radiusY and
                              math.abs(z - target[3]) <= gate.radiusZ
        if getTickCount() - state.lastVehicleReport >= 100 and passesAllWheelsGate(vehicle, state.stage, insideBox) then
            state.lastVehicleReport = getTickCount()
            triggerServerEvent("tagup:vehicleReady", resourceRoot, "home", x, y, z)
        end
        return
    end

    if getTickCount() - state.lastVehicleReport < 500 then
        return
    end
    state.lastVehicleReport = getTickCount()

    if state.stage == "enter_car" then
        triggerServerEvent("tagup:vehicleReady", resourceRoot, "party")
    elseif state.stage == "return_car" then
        triggerServerEvent("tagup:vehicleReady", resourceRoot, "returned")
    elseif state.stage == "return_after_roof" then
        triggerServerEvent("tagup:vehicleReady", resourceRoot, "roof_return")
    end
end

addEventHandler("onClientObjectGangTagProgress", root, function(previousAlpha, currentAlpha, creator)
    if not state.active or type(previousAlpha) ~= "number" or type(currentAlpha) ~= "number" then
        return
    end

    local reportsLocalPlayer = creator == localPlayer
    local reportsSynchronizedSweet = creator == state.sweet and localPlayer == state.leader and isElement(creator) and isElementSyncer(creator)
    if not reportsLocalPlayer and not reportsSynchronizedSweet then
        return
    end

    triggerServerEvent("tagup:nativeTagProgress", resourceRoot, source, creator, previousAlpha, currentAlpha)
end)

local function reportBallasGangTrigger()
    local now = getTickCount()
    if not state.active or state.stage ~= "tags_ballas" or localPlayer ~= state.leader or state.ballasGangScene or
        now - state.lastBallasGangTriggerReport < 250 then
        return
    end

    local x, y = getElementPosition(localPlayer)
    local trigger = TAGUP.ballasGangScene.trigger
    if math.abs(x - trigger.x) <= trigger.spawnRadiusX and math.abs(y - trigger.y) <= trigger.spawnRadiusY then
        state.lastBallasGangTriggerReport = now
        triggerServerEvent("tagup:ballasGangTrigger", resourceRoot)
    end
end

local function reportBallasApproachTrigger()
    local encounter = state.ballasEncounter
    if not state.active or state.stage ~= "tags_ballas" or localPlayer ~= state.leader or not encounter or
        encounter.phase ~= "awaiting_approach" then
        return
    end

    local now = getTickCount()
    local approach = TAGUP.ballasGangScene.approach
    if encounter.approachLastReportedAt and now - encounter.approachLastReportedAt < approach.retryInterval then
        return
    end

    local x, y = getElementPosition(localPlayer)
    if math.abs(x - approach.x) <= approach.radiusX and math.abs(y - approach.y) <= approach.radiusY then
        encounter.approachLastReportedAt = now
        encounter.approachReportCount = (encounter.approachReportCount or 0) + 1
        if encounter.approachReportCount == 1 or encounter.approachReportCount % 4 == 1 then
            outputDebugString(('[tagging-up-turf] Ballas encounter #%d reporting SCM 5x5 approach attempt=%d client=(%.2f, %.2f)'):format(
                                  encounter.id, encounter.approachReportCount, x, y))
        end
        triggerServerEvent("tagup:ballasEncounterApproach", resourceRoot, encounter.id)
    end
end


addEventHandler("onClientKey", root, function(button, pressed)
    if pressed and (button == "space" or button == "enter") and state.finalScene and state.finalScene.skippable and
        state.finalScene.leaderCanSkip and not state.finalScene.skipRequested then
        state.finalScene.skipRequested = true
        triggerServerEvent("tagup:finalSceneSkipRequest", resourceRoot, state.finalScene.id)
        cancelEvent()
        return
    end
    if pressed and (button == "space" or button == "enter") and state.demoScene and state.demoScene.skippable and state.demoScene.leaderCanSkip and
        not state.demoScene.skipRequested then
        state.demoScene.skipRequested = true
        triggerServerEvent("tagup:sweetDemoSceneSkipRequest", resourceRoot, state.demoScene.id)
        cancelEvent()
        return
    end
    if pressed and (button == "space" or button == "enter") and state.ballasGangScene and state.ballasGangScene.skippable and
        state.ballasGangScene.leaderCanSkip and not state.ballasGangScene.skipRequested then
        state.ballasGangScene.skipRequested = true
        triggerServerEvent("tagup:ballasGangSceneSkipRequest", resourceRoot, state.ballasGangScene.id)
        cancelEvent()
        return
    end
end)

addEventHandler("onClientPreRender", root, function()
    local fileCutscene = state.fileCutscene
    if fileCutscene and fileCutscene.startedAt and fileCutscene.leaderCanSkip and not fileCutscene.skipRequested and
        hasFileCutsceneLease(fileCutscene) then
        local ok, pressed = pcall(isFileCutsceneSkipInputPressed, fileCutscene.token)
        if ok and pressed == true then
            fileCutscene.skipRequested = true
            triggerServerEvent("tagup:fileCutsceneSkipRequest", resourceRoot, fileCutscene.id)
        end
    end
    refreshStageNavigation()
    renderNavigationImportantArea()
    updateNativeMissionHelp()
    reportVehicleProgress()
    reportBallasGangTrigger()
    reportBallasApproachTrigger()
    if state.active and isElement(state.vehicle) then
        applyGreenwoodNativeState()
    end
    if state.active and localPlayer == state.leader and (state.stage == "tags_ballas" or state.stage == "rooftop") and isElement(state.vehicle) and
        (not isElementStreamedIn(state.vehicle) or not isElementOnScreen(state.vehicle)) then
        local now = getTickCount()
        if now - state.lastOffscreenStorageReport >= TAGUP.offscreenStorage.reportInterval then
            local px, py, pz = getElementPosition(localPlayer)
            local vx, vy, vz = getElementPosition(state.vehicle)
            if getDistanceBetweenPoints3D(px, py, pz, vx, vy, vz) >= TAGUP.offscreenStorage.minimumDistance then
                state.lastOffscreenStorageReport = now
                triggerServerEvent("tagup:storeOffscreenActors", resourceRoot, state.stage)
            end
        end
    end
end)

addEventHandler("onClientVehicleEnter", root, function(player)
    if player == localPlayer and source == state.vehicle then
        tuneGreenwoodRadio()
    end
end)

addEventHandler("onClientPlayerDamage", localPlayer, function(attacker)
    local scene = state.ballasGangScene
    if not scene or not isElement(attacker) then
        return
    end
    for _, ped in ipairs(scene.enemies) do
        if attacker == ped then
            -- SET_PLAYER_CONTROL OFF calls MakePlayerSafe in GTA. The camera
            -- lease inhibits input, while this guard reproduces the relevant
            -- safety property until the post-release co-op ACK completes.
            cancelEvent()
            return
        end
    end
end)

addEventHandler("onClientElementStreamOut", root, function()
    if state.finalScene and (source == state.finalScene.sweet or source == state.leader) then
        local scene = state.finalScene
        local actor = source == scene.sweet and "sweet" or (source == localPlayer and "local_leader" or "remote_leader")
        if source == localPlayer or not scene.started then
            -- Changing a synchronized actor's vehicle/model state can recreate
            -- its GTA entity for a frame. Before reveal the render barrier owns
            -- recovery; the local player is never truly lost from MTA streaming.
            outputDebugString(("[tagging-up-turf] Final Grove scene #%d transient stream-out actor=%s phase=%s"):format(
                                  scene.id, actor, scene.started and "started" or "render_barrier"))
            return
        end
        triggerServerEvent("tagup:finalSceneLeaseLost", resourceRoot, scene.id)
        clearFinalScene("actor_streamed_out", false)
        return
    end
    if state.introScene and (source == state.introScene.sweet or source == state.introScene.smoke) then
        local scene = state.introScene
        triggerServerEvent("tagup:introSceneLeaseLost", resourceRoot, scene.id)
        clearIntroScene("actor_streamed_out")
        return
    end
    if state.arrivalGate and source == state.vehicle then
        releaseArrivalGate("vehicle_streamed_out")
    end
    if state.vehiclePlayback and (source == state.vehiclePlayback.ped or source == state.vehiclePlayback.vehicle) then
        if isElement(state.vehiclePlayback.vehicle) and isVehiclePlaybackActive(state.vehiclePlayback.vehicle) then
            stopVehiclePlayback(state.vehiclePlayback.vehicle)
        end
        reportVehiclePlayback("streamed_out", "Sweet ou Greenwood sorti du streaming pendant 05EB", true)
        return
    end
    if state.demoLeave and (source == state.demoLeave.ped or source == state.demoLeave.vehicle) then
        reportDemoLeave("streamed_out", "Sweet ou la Greenwood est sorti du streaming pendant la task")
        return
    end
    if state.demoWalk and source == state.demoWalk.ped then
        reportDemoWalk("streamed_out", "Sweet sorti du streaming pendant la task")
        return
    end
    if state.demoShoot and source == state.demoShoot.ped then
        reportDemoShoot("streamed_out", "Sweet sorti du streaming pendant la task")
        return
    end
    if state.demoEnter and (source == state.demoEnter.ped or source == state.demoEnter.vehicle) then
        reportSweetReturnEnter("streamed_out", "Sweet ou la Greenwood sorti du streaming pendant la task")
        return
    end
    if state.ballasDeparture and (source == state.ballasDeparture.ped or source == state.ballasDeparture.vehicle) then
        if state.ballasDeparture.wanderAcceptedAt then
            reportBallasWander("streamed_out", "Sweet ou Greenwood sorti du streaming pendant 05D2")
        else
            reportBallasPlayerExit("streamed_out", "Greenwood sortie du streaming pendant la sortie joueur")
        end
        return
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    state.checkpointGroundProbeToken = nil
    clearFileCutscene("resource_stopped", false)
    releaseArrivalGate("resource_stopped")
    clearSweetDemoAudioPreload("resource_stopped")
    clearSweetDemoScene("resource_stopped", false)
    if type(releaseObjectGangTag) == "function" then
        for _, object in ipairs(getElementsByType("object", resourceRoot, true)) do
            releaseObjectGangTag(object)
        end
    end
    clearDemoLeave(true)
    clearDemoWalk(true)
    clearDemoShoot(true)
    clearDemoSequence(true)
    clearSweetReturnEnter(true)
    clearBallasDeparture(true)
    clearBallasGangScene("resource_stopped")
    clearBallasEncounter("resource_stopped", true)
    clearBallasEncounterAudioPreload()
    clearVehiclePlayback(true)
    clearFinalScene("resource_stopped", false)
    if isElement(state.sweet) and type(setPedMissionActor) == "function" then
        setPedMissionActor(state.sweet, false)
    end
    clearIntroScene("resource_stopped")
    destroyNavigation()
end)
