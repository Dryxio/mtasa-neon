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
    demoWalk = nil,
    demoShoot = nil,
}

local screenWidth, screenHeight = guiGetScreenSize()
local TAG_PAINT_ALPHA_DATA = "tagup.paintAlpha"

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
end)

addEventHandler("onClientElementDataChange", root, function(dataName)
    if dataName == TAG_PAINT_ALPHA_DATA then
        applyGangTagAlpha(source)
    end
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    if type(setObjectGangTagAlpha) ~= "function" then
        outputDebugString("[tagging-up-turf] setObjectGangTagAlpha is unavailable; native tag material rendering is disabled", 1)
        return
    end
    for _, object in ipairs(getElementsByType("object", resourceRoot, true)) do
        if getElementData(object, TAG_PAINT_ALPHA_DATA) ~= false then
            applyGangTagAlpha(object)
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
        return reportDemoWalk("api_unavailable", "setPedGoTo absent du client Neon")
    end

    local profile = walk.profile
    local target = profile.target
    walk.accepted = setPedGoTo(walk.ped, Vector3(target.x, target.y, target.z), profile.movement, profile.radius, profile.slowdownRadius,
                              profile.timeout)
    if not walk.accepted then
        return reportDemoWalk("refused", "setPedGoTo a retourne false")
    end

    walk.acceptedAt = getTickCount()
    walk.seenNativeTask = false
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
    end
    state.demoShoot = nil
end

local function reportDemoShoot(result, details)
    local shoot = state.demoShoot
    if not shoot then
        return
    end
    local id, ped = shoot.id, shoot.ped
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
        return reportDemoShoot("api_unavailable", "setPedShootAt absent du client Neon")
    end
    if type(setPedWeaponShootingRate) ~= "function" or
        not setPedWeaponShootingRate(shoot.ped, shoot.profile.shootingRate) then
        return reportDemoShoot("shooting_rate_refused", "SET_CHAR_SHOOT_RATE 100 indisponible ou refuse")
    end
    if type(setPedWeaponAccuracy) ~= "function" or not setPedWeaponAccuracy(shoot.ped, shoot.profile.weaponAccuracy) then
        return reportDemoShoot("weapon_accuracy_refused", "SET_CHAR_ACCURACY 90 indisponible ou refuse")
    end

    local target, profile = shoot.target, shoot.profile
    shoot.accepted = setPedShootAt(shoot.ped, Vector3(target.x, target.y, target.z), profile.duration, profile.burstLength)
    if not shoot.accepted then
        return reportDemoShoot("refused", "setPedShootAt a retourne false")
    end

    shoot.acceptedAt = getTickCount()
    shoot.seenNativeTask = false
    shoot.observedReported = false
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
    state.demoShoot = {id = shootId, ped = ped, target = target, profile = profile, requestedAt = getTickCount(), accepted = false}
    beginDemoShoot()
end)

addEvent("tagup:sweetDemoShootCancel", true)
addEventHandler("tagup:sweetDemoShootCancel", resourceRoot, function(shootId, reason)
    if state.demoShoot and state.demoShoot.id == shootId then
        outputDebugString(("[tagging-up-turf] Cancelling Sweet native shoot #%d: %s"):format(shootId, tostring(reason)))
        clearDemoShoot(true)
    end
end)

addEvent("tagup:state", true)
addEventHandler("tagup:state", resourceRoot, function(payload)
    local previousStage = state.stage
    state.active = true
    state.stage = payload.stage
    state.vehicle = payload.vehicle
    state.sweet = payload.sweet
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
            clearDemoWalk(true)
            clearDemoShoot(true)
        end
        state.stageStarted = getTickCount()
        if state.stage == "intro" then
            startIntroCamera()
        else
            stopIntroCamera()
        end
        setStageNavigation(state.stage)
        playSoundFrontEnd(state.stage == "complete" and 43 or 11)
    end
end)

addEvent("tagup:stop", true)
addEventHandler("tagup:stop", resourceRoot, function()
    clearDemoWalk(true)
    clearDemoShoot(true)
    stopIntroCamera()
    destroyNavigation()
    state.active = false
    state.stage = nil
    state.vehicle = nil
    state.sweet = nil
    state.leader = nil
    state.tagProgress = {}
    state.completedTags = {}
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
    if state.demoWalk and source == state.demoWalk.ped then
        reportDemoWalk("streamed_out", "Sweet sorti du streaming pendant la task")
        return
    end
    if state.demoShoot and source == state.demoShoot.ped then
        reportDemoShoot("streamed_out", "Sweet sorti du streaming pendant la task")
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
    clearDemoWalk(true)
    clearDemoShoot(true)
    stopIntroCamera()
    destroyNavigation()
end)
