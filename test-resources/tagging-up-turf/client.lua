local state = {
    active = false,
    stage = nil,
    vehicle = nil,
    sweet = nil,
    leader = nil,
    tagProgress = {},
    completedTags = {},
    destination = nil,
    marker = nil,
    blip = nil,
    message = nil,
    messageUntil = 0,
    stageStarted = 0,
    lastSpray = 0,
    sprayInput = false,
    sprayPulseUntil = 0,
    lastVehicleReport = 0,
    introCamera = false,
    demoLeave = nil,
    demoWalk = nil,
    demoShoot = nil,
    demoEnter = nil,
    ballasDeparture = nil,
    ballasWanderPed = nil,
    vehiclePlayback = nil,
    vehicleRecordingPreloaded = false,
    traceStarted = false,
    traceDemoTagActive = false,
    traceCurrentStep = nil,
}

local screenWidth, screenHeight = guiGetScreenSize()
local TAG_PAINT_ALPHA_DATA = "tagup.paintAlpha"

local function applyMissionActor(ped)
    if not isElement(ped) or getElementType(ped) ~= "ped" or type(setPedMissionActor) ~= "function" then
        return false
    end
    return setPedMissionActor(ped, getElementData(ped, TAGUP.missionActorData) == true)
end

-- This ordered list is a presentation trace of the prototype's real execution.
-- The labels deliberately distinguish verified GTA primitives from temporary Lua
-- substitutes so footage cannot imply that the whole SCM runtime already exists.
local MISSION_TRACE_SEQUENCE = {
    {id = "mission_start", title = "MISSION START", detail = "LUA ORCHESTRATION · server authority"},
    {id = "intro_camera", title = "INTRO CAMERA + TIMER", detail = "LUA SUBSTITUTE · camera 6500 / gate 7000 ms"},
    {id = "enter_car", title = "PARTY IN GREENWOOD", detail = "CO-OP CONDITION · leader driving + party seated"},
    {id = "drive_idlewood", title = "LOCATE CAR AT IDLEWOOD", detail = "LUA CO-OP CONDITION · SCM-derived target / < 11 m"},
    {id = "leave_car", title = "05CD · TASK LEAVE CAR", detail = "NATIVE VERIFIED · MTA vehicle lifecycle"},
    {id = "go_to", title = "05D3 · GO STRAIGHT TO COORD", detail = "NATIVE VERIFIED · Sweet / walk"},
    {id = "go_to_wait", title = "OBSERVE NATIVE GO-TO", detail = "LUA TASK POLL · approximates sequence progress"},
    {id = "demo_setup", title = "PREPARE SPRAY DEMO", detail = "LUA SUBSTITUTE · weapon + heading"},
    {id = "accuracy", title = "02E2 · SET CHAR ACCURACY", detail = "NATIVE VERIFIED · value 90"},
    {id = "shoot_rate", title = "07DD · SET CHAR SHOOT RATE", detail = "NATIVE VERIFIED · value 100"},
    {id = "shoot", title = "0668 · SHOOT AT COORD", detail = "NATIVE VERIFIED · burst 5 / ceiling 15 s"},
    {id = "shoot_wait", title = "OBSERVE NATIVE GUN CTRL", detail = "LUA TASK POLL · TASK_SIMPLE_GUN_CTRL"},
    {id = "demo_tag", title = "0702 SUBSTITUTE · TAG PERCENT", detail = "SERVER COUNTER + NATIVE TAG ALPHA · demo 0%"},
    {id = "demo_wait", title = "CANCEL TASK + WAIT 1000", detail = "NEON LIFECYCLE + SCM FLOW"},
    {id = "enter_passenger", title = "05CA · ENTER CAR AS PASSENGER", detail = "NATIVE VERIFIED · SCM seat 0 / MTA seat 1"},
    {id = "idlewood_tags", title = "0702 SUBSTITUTE · TAG PERCENT", detail = "SERVER COUNTER + NATIVE TAG ALPHA · Idlewood 0%"},
    {id = "return_car", title = "RETURN TO GREENWOOD", detail = "CO-OP CONDITION · party regroup"},
    {id = "drive_ballas", title = "LOCATE CAR IN BALLAS", detail = "LUA CO-OP CONDITION · SCM-derived target / < 13 m"},
    {id = "ballas_leave", title = "05CD · CJ LEAVES CAR", detail = "NATIVE VERIFIED · local player vehicle lifecycle"},
    {id = "ballas_wander", title = "05D2 · CAR DRIVE WANDER", detail = "NATIVE VERIFIED · Sweet passenger / speed 20 / style 2"},
    {id = "ballas_wait", title = "WAIT 1000", detail = "SCM FLOW · release control after native start"},
    {id = "spawn_ballas", title = "CREATE BALLAS GROUP", detail = "LUA SUBSTITUTE · temporary ped AI"},
    {id = "ballas_tags", title = "0702 SUBSTITUTE · TAG PERCENT", detail = "SERVER COUNTER + NATIVE TAG ALPHA · Ballas 0%"},
    {id = "rooftop_tag", title = "0702 SUBSTITUTE · TAG PERCENT", detail = "SERVER COUNTER + NATIVE TAG ALPHA · rooftop 0%"},
    {id = "request_carrec", title = "07C0 · REQUEST CAR RECORDING", detail = "NATIVE VERIFIED · recording 207"},
    {id = "load_carrec", title = "07C1 · HAS CAR RECORDING LOADED", detail = "NATIVE VERIFIED · streamed RRR buffer"},
    {id = "start_playback", title = "05EB · START RECORDED CAR", detail = "NATIVE VERIFIED · vehicle syncer only"},
    {id = "playback_wait", title = "060E · CAR PLAYBACK ACTIVE", detail = "NATIVE VERIFIED · recording 207 / natural end"},
    {id = "return_after_roof", title = "REGROUP WITH SWEET", detail = "CO-OP CONDITION · party in vehicle"},
    {id = "drive_home", title = "LOCATE CAR AT GROVE", detail = "LUA CO-OP CONDITION · SCM-derived target / < 12 m"},
    {id = "mission_end", title = "MISSION PASSED", detail = "SERVER AUTHORITY · reward + restore"},
}

local STAGE_TRACE_STEP = {
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
        total = total + (tonumber(state.tagProgress[id]) or 0)
    end
    return total / #ids
end

local function updateTraceTagStage()
    if state.stage == "tags_idlewood" then
        local progress = averageTagProgress({1, 2})
        traceProgress("idlewood_tags", progress, ("SERVER COUNTER + NATIVE TAG ALPHA · Idlewood %d%%"):format(math.floor(progress * 100 + 0.5)))
    elseif state.stage == "tags_ballas" then
        local progress = averageTagProgress({3, 4})
        traceProgress("ballas_tags", progress, ("SERVER COUNTER + NATIVE TAG ALPHA · Ballas %d%%"):format(math.floor(progress * 100 + 0.5)))
    elseif state.stage == "rooftop" then
        local progress = tonumber(state.tagProgress[5]) or 0
        traceProgress("rooftop_tag", progress, ("SERVER COUNTER + NATIVE TAG ALPHA · rooftop %d%%"):format(math.floor(progress * 100 + 0.5)))
    end
end

local function applyGangTagAlpha(object)
    if not isElement(object) or getElementType(object) ~= "object" or not isElementStreamedIn(object) then
        return
    end
    local alpha = tonumber(getElementData(object, TAG_PAINT_ALPHA_DATA))
    if type(setObjectGangTagAlpha) ~= "function" then
        return
    end
    if alpha then
        setObjectGangTagAlpha(object, math.max(0, math.min(255, math.floor(alpha + 0.5))))
    else
        setObjectGangTagAlpha(object, false)
    end
end

addEventHandler("onClientElementStreamIn", root, function()
    if getElementData(source, TAG_PAINT_ALPHA_DATA) ~= false then
        applyGangTagAlpha(source)
    end
    if getElementType(source) == "ped" and getElementData(source, TAGUP.missionActorData) ~= nil then
        applyMissionActor(source)
    end
end)

addEventHandler("onClientElementDataChange", root, function(dataName)
    if dataName == TAGUP.missionActorData then
        applyMissionActor(source)
    elseif dataName == TAG_PAINT_ALPHA_DATA then
        applyGangTagAlpha(source)
        if state.active and state.stage == "demo" and not getElementData(source, "tagup.tagId") then
            local alpha = tonumber(getElementData(source, TAG_PAINT_ALPHA_DATA))
            if alpha and alpha > 0 then
                local progress = math.max(0, math.min(1, alpha / 255))
                if not state.traceDemoTagActive and state.traceCurrentStep ~= "demo_wait" then
                    state.traceDemoTagActive = true
                    traceCurrent("demo_tag")
                end
                traceProgress("demo_tag", progress,
                              ("SERVER COUNTER + NATIVE TAG ALPHA · demo %d%%"):format(math.floor(progress * 100 + 0.5)))
            end
        end
    end
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    if type(setObjectGangTagAlpha) ~= "function" then
        outputDebugString("[tagging-up-turf] setObjectGangTagAlpha is unavailable; native tag material rendering is disabled", 1)
    else
        for _, object in ipairs(getElementsByType("object", resourceRoot, true)) do
            if getElementData(object, TAG_PAINT_ALPHA_DATA) ~= false then
                applyGangTagAlpha(object)
            end
        end
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
    state.marker = nil
    state.blip = nil
    state.destination = nil
end

local function setNavigation(position, size, color)
    destroyNavigation()
    if not position then
        return
    end
    state.destination = position
    state.marker = createMarker(position[1], position[2], position[3] - 1, "cylinder", size or 4, unpack(color or {80, 180, 255, 125}))
    setElementDimension(state.marker, TAGUP.dimension)
    state.blip = createBlip(position[1], position[2], position[3], 0, 2, 80, 180, 255, 255)
    setElementDimension(state.blip, TAGUP.dimension)
end

local function setStageNavigation(stage)
    if stage == "enter_car" or stage == "return_car" or stage == "return_after_roof" then
        if isElement(state.vehicle) then
            local x, y, z = getElementPosition(state.vehicle)
            setNavigation({x, y, z}, 3, {80, 180, 255, 125})
        end
    elseif stage == "drive_idlewood" then
        setNavigation(TAGUP.idlewoodDestination, 7, {80, 180, 255, 125})
    elseif stage == "drive_ballas" then
        setNavigation(TAGUP.ballasDestination, 8, {190, 80, 255, 125})
    elseif stage == "drive_home" then
        setNavigation(TAGUP.homeDestination, 7, {80, 200, 100, 125})
    else
        destroyNavigation()
    end
end

local function getActiveTags()
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

local function nearestActiveTag()
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

local function startIntroCamera()
    state.introCamera = true
    setCameraMatrix(2545.0, -1710.0, 24.0, 2514.0, -1668.0, 13.5)
end

local function stopIntroCamera()
    if state.introCamera then
        setCameraTarget(localPlayer)
        state.introCamera = false
    end
end

local function updateIntroCamera()
    if not state.introCamera then
        return
    end

    -- Reasserting the camera every frame prevents freeroam/spawn resources from
    -- snapping it back to the player's pre-mission position during the intro.
    local progress = math.min(1, (getTickCount() - state.stageStarted) / 6500)
    local x, y, z = interpolateBetween(2545.0, -1710.0, 24.0, 2496.0, -1648.0, 18.0, progress, "InOutQuad")
    local lookX, lookY, lookZ = interpolateBetween(2518.0, -1669.0, 13.5, 2508.0, -1666.0, 13.2, progress, "InOutQuad")
    setCameraMatrix(x, y, z, lookX, lookY, lookZ)
end

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

local function clearBallasDeparture(killWander)
    local departure = state.ballasDeparture
    if departure then
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
    end
    if killWander and isElement(state.ballasWanderPed) and isElementSyncer(state.ballasWanderPed) then
        killPedTask(state.ballasWanderPed, "primary", 3, false)
        state.ballasWanderPed = nil
    end
    state.ballasDeparture = nil
    toggleAllControls(true)
    if state.active then
        setCameraTarget(localPlayer)
    end
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
    if not isElement(departure.vehicle) then
        return reportBallasPlayerExit("destroyed", "Greenwood absente avant la sortie")
    end
    if getPedOccupiedVehicle(localPlayer) ~= departure.vehicle then
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

addEvent("tagup:ballasPlayerExitStart", true)
addEventHandler("tagup:ballasPlayerExitStart", resourceRoot, function(departureId, vehicle, profile)
    clearBallasDeparture(false)
    if not state.active or state.stage ~= "ballas_departure" or not isElement(vehicle) or type(profile) ~= "table" then
        return
    end
    state.ballasDeparture = {id = departureId, vehicle = vehicle, profile = profile, requestedAt = getTickCount()}
    toggleAllControls(false, true, false)
    setCameraMatrix(2329.6750, -1499.9113, 25.8505, 2330.6533, -1499.9260, 25.6440)
    traceCurrent("ballas_leave")
    beginBallasPlayerExit()
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
        clearBallasDeparture(not keepWandering)
    end
end)

addEvent("tagup:stopBallasWander", true)
addEventHandler("tagup:stopBallasWander", resourceRoot, function()
    if isElement(state.ballasWanderPed) and isElementSyncer(state.ballasWanderPed) then
        killPedTask(state.ballasWanderPed, "primary", 3, false)
    end
    state.ballasWanderPed = nil
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
    if isElement(state.sweet) then
        applyMissionActor(state.sweet)
    end
    state.leader = payload.leader
    state.tagProgress = payload.tagProgress or {}
    state.completedTags = payload.completedTags or {}
    if payload.message then
        state.message = payload.message
        state.messageUntil = getTickCount() + 3500
    end
    if payload.failureReason then
        state.message = payload.failureReason
        state.messageUntil = getTickCount() + 5000
    end

    if previousStage ~= state.stage then
        if previousStage == "demo" and state.stage ~= "demo" then
            clearDemoLeave(true)
            clearDemoWalk(true)
            clearDemoShoot(true)
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
        if state.stage == "intro" then
            startIntroCamera()
        else
            stopIntroCamera()
        end
        setStageNavigation(state.stage)
        playSoundFrontEnd(state.stage == "complete" and 43 or 11)
    end
    if state.stage == "rooftop" and localPlayer == state.leader and not state.vehicleRecordingPreloaded and
        type(requestVehicleRecording) == "function" then
        state.vehicleRecordingPreloaded = requestVehicleRecording(TAGUP.vehicleRecording207.id)
        outputDebugString(("[tagging-up-turf] Recording 207 rooftop preload: %s"):format(tostring(state.vehicleRecordingPreloaded)))
    end
    if state.stage == "tags_ballas" and payload.enemies then
        traceCurrent("ballas_tags")
    end
    updateTraceTagStage()
end)

addEvent("tagup:stop", true)
addEventHandler("tagup:stop", resourceRoot, function()
    clearDemoLeave(true)
    clearDemoWalk(true)
    clearDemoShoot(true)
    clearSweetReturnEnter(true)
    clearBallasDeparture(true)
    clearVehiclePlayback(true)
    stopIntroCamera()
    destroyNavigation()
    if isElement(state.sweet) and type(setPedMissionActor) == "function" then
        setPedMissionActor(state.sweet, false)
    end
    state.active = false
    state.stage = nil
    state.vehicle = nil
    state.sweet = nil
    state.leader = nil
    state.tagProgress = {}
    state.completedTags = {}
    state.traceStarted = false
    state.traceDemoTagActive = false
    state.traceCurrentStep = nil
    state.vehicleRecordingPreloaded = false
    if type(TAGUP_TRACE) == "table" then
        TAGUP_TRACE.toggle(false)
        TAGUP_TRACE.reset()
    end
end)

local function drawWorldTag(tag)
    local sx, sy = getScreenFromWorldPosition(tag.x, tag.y, tag.z + 0.35, 0.08)
    if not sx then
        return
    end
    local progress = state.tagProgress[tag.id] or 0
    local width, height = 120, 10
    dxDrawRectangle(sx - width / 2 - 2, sy - 2, width + 4, height + 4, tocolor(0, 0, 0, 190))
    dxDrawRectangle(sx - width / 2, sy, width * progress, height, tocolor(85, 200, 105, 230))
    dxDrawText(("TAG  %d%%"):format(math.floor(progress * 100)), sx - 70, sy - 29, sx + 70, sy - 5, tocolor(255, 255, 255, 235), 1, "default-bold", "center", "bottom")
end

local function drawMissionHud()
    if not state.active or not state.stage then
        return
    end
    local info = TAGUP.stages[state.stage] or {title = state.stage, objective = ""}
    local boxWidth = math.min(620, screenWidth - 50)
    local left = (screenWidth - boxWidth) / 2
    dxDrawRectangle(left, 35, boxWidth, 78, tocolor(0, 0, 0, 175))
    dxDrawText(info.title, left + 18, 43, left + boxWidth - 18, 70, tocolor(103, 206, 112, 255), 1.35, "pricedown", "center", "center")
    dxDrawText(info.objective, left + 18, 72, left + boxWidth - 18, 104, tocolor(245, 245, 245, 245), 1, "default-bold", "center", "center", true)

    if state.message and getTickCount() < state.messageUntil then
        dxDrawText(state.message, 0, screenHeight * 0.70, screenWidth, screenHeight * 0.78, tocolor(255, 215, 90, 255), 1.3, "default-bold", "center", "center", true, true)
    end


    local nearestTag, distance = nearestActiveTag()
    if nearestTag and distance <= TAGUP.sprayRange + 1 then
        local hint = distance <= TAGUP.sprayRange and "Maintenez TIR pour recouvrir le tag" or "Approchez-vous encore du tag"
        dxDrawText(hint, 0, screenHeight - 155, screenWidth, screenHeight - 105, tocolor(255, 255, 255, 245), 1.15, "default-bold", "center", "center", true)
    end

    for _, tag in ipairs(getActiveTags()) do
        drawWorldTag(tag)
    end
end
addEventHandler("onClientRender", root, drawMissionHud)
addEventHandler("onClientRender", root, updateIntroCamera, true, "low-10")

local function reportVehicleProgress()
    if not state.active or localPlayer ~= state.leader or getTickCount() - state.lastVehicleReport < 500 then
        return
    end
    local vehicle = getPedOccupiedVehicle(localPlayer)
    if vehicle ~= state.vehicle or getVehicleController(vehicle) ~= localPlayer then
        return
    end
    state.lastVehicleReport = getTickCount()

    if state.stage == "enter_car" then
        triggerServerEvent("tagup:vehicleReady", resourceRoot, "party")
    elseif state.stage == "drive_idlewood" then
        triggerServerEvent("tagup:vehicleReady", resourceRoot, "idlewood")
    elseif state.stage == "return_car" then
        triggerServerEvent("tagup:vehicleReady", resourceRoot, "returned")
    elseif state.stage == "drive_ballas" then
        triggerServerEvent("tagup:vehicleReady", resourceRoot, "ballas")
    elseif state.stage == "return_after_roof" then
        triggerServerEvent("tagup:vehicleReady", resourceRoot, "roof_return")
    elseif state.stage == "drive_home" then
        triggerServerEvent("tagup:vehicleReady", resourceRoot, "home")
    end
end

local function reportSpraying()
    local now = getTickCount()
    if not state.active or now - state.lastSpray < 110 or getPedWeapon(localPlayer) ~= TAGUP.sprayWeapon or isPedInVehicle(localPlayer) then
        return
    end
    local isFiring = state.sprayInput or now < state.sprayPulseUntil or getPedControlState(localPlayer, "fire") or getKeyState("mouse1") or
                         getKeyState("lctrl") or getKeyState("rctrl")
    if not isFiring then
        return
    end
    local tag, distance = nearestActiveTag()
    if not tag or distance > TAGUP.sprayRange then
        return
    end

    -- Spray cans do not expose the same target endpoint as firearms in MTA. Range
    -- plus an actual fire input reproduces SCM tag progress without CTagManager.
    state.lastSpray = now
    triggerServerEvent("tagup:spray", resourceRoot, tag.id)
end


addEventHandler("onClientKey", root, function(button, pressed)
    if button == "mouse1" or button == "lctrl" or button == "rctrl" then
        state.sprayInput = pressed
        if pressed then
            state.sprayPulseUntil = getTickCount() + 250
        end
    end
end)

addEventHandler("onClientPlayerWeaponFire", localPlayer, function(weapon)
    if weapon == TAGUP.sprayWeapon then
        state.sprayPulseUntil = getTickCount() + 250
    end
end)

addEventHandler("onClientPreRender", root, function()
    reportVehicleProgress()
    reportSpraying()
end)

local function nearestLivingPlayer(ped)
    local px, py, pz = getElementPosition(ped)
    local nearest, nearestDistance
    for _, player in ipairs(getElementsByType("player", root, true)) do
        if getElementDimension(player) == TAGUP.dimension and not isPedDead(player) then
            local x, y, z = getElementPosition(player)
            local distance = tagupDistance3D(px, py, pz, x, y, z)
            if not nearestDistance or distance < nearestDistance then
                nearest, nearestDistance = player, distance
            end
        end
    end
    return nearest, nearestDistance
end

setTimer(function()
    if not state.active then
        return
    end
    for _, ped in ipairs(getElementsByType("ped", root, true)) do
        -- MTA assigns one client as each ped's syncer. Running AI only there prevents
        -- competing clients from issuing different movement and firing controls.
        if getElementData(ped, "tagup.enemy") and getElementData(ped, "tagup.active") and isElementSyncer(ped) and not isPedDead(ped) then
            local target, distance = nearestLivingPlayer(ped)
            if target then
                local x, y, z = getElementPosition(target)
                local px, py = getElementPosition(ped)
                setElementRotation(ped, 0, 0, -math.deg(math.atan2(x - px, y - py)))
                setPedAimTarget(ped, x, y, z + 0.5)
                setPedControlState(ped, "aim_weapon", true)
                setPedControlState(ped, "fire", distance < 18)
                setPedControlState(ped, "forwards", distance > 7)
            end
        end
    end
end, 180, 0)

addEventHandler("onClientElementStreamOut", root, function()
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
    if getElementType(source) == "ped" and getElementData(source, "tagup.enemy") and isElementSyncer(source) then
        setPedControlState(source, "aim_weapon", false)
        setPedControlState(source, "fire", false)
        setPedControlState(source, "forwards", false)
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    if type(setObjectGangTagAlpha) == "function" then
        for _, object in ipairs(getElementsByType("object", resourceRoot, true)) do
            if isElementStreamedIn(object) then
                setObjectGangTagAlpha(object, false)
            end
        end
    end
    clearDemoLeave(true)
    clearDemoWalk(true)
    clearDemoShoot(true)
    clearSweetReturnEnter(true)
    clearBallasDeparture(true)
    clearVehiclePlayback(true)
    if isElement(state.sweet) and type(setPedMissionActor) == "function" then
        setPedMissionActor(state.sweet, false)
    end
    stopIntroCamera()
    destroyNavigation()
end)
