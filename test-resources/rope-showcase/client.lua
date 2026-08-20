local ANCHOR_MODEL = 1337
local CARGO_MODEL = 2912
local WRECK_TARGET_MODEL = 3175
local HARNESS_VEHICLE_MODEL = 422
local LAMP_MODEL = 1226

local ACT_MAGNET = 1
local ACT_WRECKING_BALL = 2
local ACT_HARNESS = 3

local ACT_DURATION = {
    [ACT_MAGNET] = 11500,
    [ACT_WRECKING_BALL] = 11000,
    [ACT_HARNESS] = 12000,
}

local FADE_MS = 450
local NATIVE_READY_TIMEOUT = 4000
local MAGNET_GROUND_OFFSET = 0.10
local MAGNET_FIRE_OFFSET_Z = 0.75

local show = {
    running = false,
    readySent = false,
    centerX = 0,
    centerY = 0,
    centerZ = 0,
    stageY = 0,
    dimension = 0,

    act = 0,
    actReady = false,
    actCreatedAt = 0,
    actStartedAt = 0,
    rope = nil,
    holder = nil,
    payload = nil,
    target = nil,
    targetPos = nil,
    fire = nil,
    breakEffect = nil,
    birds = {},
    elements = {},
    moveFlags = {},

    attached = false,
    released = false,
    impactTriggered = false,
    finished = false,

    savedTime = nil,
    lampLightIndex = nil,
    lamp2DFXTouched = false,
}

local function clamp(value, minimum, maximum)
    return math.max(minimum, math.min(maximum, value))
end

local function smoothstep(value)
    value = clamp(value, 0, 1)
    return value * value * (3 - 2 * value)
end

local function lerp(a, b, t)
    return a + (b - a) * t
end

local function track(element)
    if isElement(element) then
        show.elements[#show.elements + 1] = element
    end
    return element
end

local function configureElement(element)
    if not isElement(element) then
        return
    end
    setElementInterior(element, 0)
    setElementDimension(element, show.dimension)
end

local function setPresentationUI(visible)
    showPlayerHudComponent("all", visible)
    showChat(visible)
end

local function getGroundZ(x, y, referenceZ)
    local ground = getGroundPosition(x, y, referenceZ + 1000.0)
    if type(ground) ~= "number" then
        ground = getGroundPosition(x, y, referenceZ + 100.0)
    end
    return type(ground) == "number" and ground or referenceZ
end

local function placeOnGround(element, x, y, referenceZ, offset)
    if not isElement(element) then
        return false
    end

    local ground = getGroundZ(x, y, referenceZ)
    local _, _, minZ = getElementBoundingBox(element)
    if type(minZ) == "number" then
        setElementPosition(element, x, y, ground - minZ + (offset or 0.02))
    else
        setElementPosition(element, x, y, ground + (offset or 0.02))
    end
    return true
end

local function createHolder(x, y, z)
    local holder = track(createObject(ANCHOR_MODEL, x, y, z, 0, 0, 0))
    if not isElement(holder) then
        return false
    end

    configureElement(holder)
    setElementAlpha(holder, 0)
    setElementCollisionsEnabled(holder, false)
    setElementFrozen(holder, true)
    return holder
end

local function createManagedRope(holder, ropeType, length, winchHeight)
    if not isElement(holder) then
        return false
    end

    local x, y, z = getElementPosition(holder)
    local rope = track(createRope(x, y, z, {
        type = ropeType,
        holder = holder,
        holderOffset = {0, 0, 0},
        duration = 0,
        fixedNode = 0,
        sitOnGround = false,
        winchHeight = winchHeight or 0.5,
        length = length,
        physics = true,
    }))

    if not isElement(rope) then
        return false
    end

    configureElement(rope)
    return rope
end

local function detachPayload()
    if show.attached and isElement(show.rope) then
        detachElementFromRope(show.rope)
    end
    show.attached = false
end

local function resetLamp2DFX()
    if show.lamp2DFXTouched then
        pcall(resetModel2DFXEffects, LAMP_MODEL)
    end
    show.lamp2DFXTouched = false
    show.lampLightIndex = nil
end

local function cleanupAct()
    detachPayload()
    resetLamp2DFX()

    for i = #show.elements, 1, -1 do
        local element = show.elements[i]
        if isElement(element) then
            destroyElement(element)
        end
    end

    show.elements = {}
    show.birds = {}
    show.rope = nil
    show.holder = nil
    show.payload = nil
    show.target = nil
    show.targetPos = nil
    show.fire = nil
    show.breakEffect = nil
    show.moveFlags = {}
    show.attached = false
    show.released = false
    show.impactTriggered = false
    show.actReady = false
end

local function attachCurrentPayload()
    if show.attached or not isElement(show.rope) or not isElement(show.payload) then
        return
    end

    setElementVelocity(show.payload, 0, 0, 0)
    setElementAngularVelocity(show.payload, 0, 0, 0)
    setElementFrozen(show.payload, false)
    show.attached = attachElementToRope(show.rope, show.payload) == true

    if not show.attached then
        outputDebugString("[ROPE SHOWCASE] attachElementToRope failed in act " .. tostring(show.act), 1)
    end
end

local function moveHolderOnce(key, duration, x, y, z, easing)
    if show.moveFlags[key] or not isElement(show.holder) then
        return
    end

    show.moveFlags[key] = true
    moveObject(show.holder, duration, x, y, z, 0, 0, 0, easing or "InOutQuad")
end

local function createMagnetAct()
    local cx, y, z = show.centerX, show.stageY, show.centerZ

    show.holder = createHolder(cx, y, z + 11.5)
    -- Start with the magnet already in frame, then extend the rope slowly
    -- before pickup so the opening shot has a readable physical ramp.
    show.rope = createManagedRope(show.holder, "miniMagnet", 6.5, 0.80)
    show.payload = track(createObject(CARGO_MODEL, cx, y, z + 4.0, 0, 0, 18))

    if not isElement(show.holder) or not isElement(show.rope) or not isElement(show.payload) then
        return false
    end

    configureElement(show.payload)
    placeOnGround(show.payload, cx, y, z, MAGNET_GROUND_OFFSET)
    setElementCollisionsEnabled(show.payload, true)
    setElementFrozen(show.payload, true)
    setObjectProperty(show.payload, "mass", 45.0)
    setObjectProperty(show.payload, "turn_mass", 55.0)
    setObjectProperty(show.payload, "air_resistance", 0.995)
    setObjectProperty(show.payload, "elasticity", 0.18)
    return true
end

local function spawnWreckingBirds(targetX, targetY, targetZ)
    for i = 1, 16 do
        local angle = (i / 16) * math.pi * 2
        local radius = 3.5 + (i % 4) * 0.7
        local bird = track(createBird(
            targetX + math.cos(angle) * radius,
            targetY + math.sin(angle) * radius,
            targetZ + 3.2 + (i % 3) * 0.55,
            {
                preset = i % 6 == 0 and "water" or "normal",
                velocity = {0, 0, 0},
                targetVelocity = {-math.sin(angle) * 1.1, math.cos(angle) * 1.1, 0.05},
                size = i % 6 == 0 and 0.78 or 0.52,
                renderDistance = 130,
                wingBeatTime = 420 + (i % 5) * 55,
                curvedFlight = false,
                shootable = false,
            }
        ))
        if isElement(bird) then
            configureElement(bird)
            show.birds[#show.birds + 1] = bird
        end
    end
end

local function createWreckingBallAct()
    local cx, y, z = show.centerX, show.stageY, show.centerZ

    show.holder = createHolder(cx - 3.0, y, z + 12.5)
    show.rope = createManagedRope(show.holder, "wreckingBall", 9.0, 0.48)
    if not isElement(show.holder) or not isElement(show.rope) then
        return false
    end

    local targetX = cx + 4.0
    show.target = track(createObject(WRECK_TARGET_MODEL, targetX, y, z + 8.0, 0, 0, 90))
    if not isElement(show.target) then
        return false
    end

    configureElement(show.target)
    placeOnGround(show.target, targetX, y, z, 0.02)
    setElementFrozen(show.target, true)
    setElementCollisionsEnabled(show.target, true)

    local tx, ty, tz = getElementPosition(show.target)
    show.targetPos = {x = tx, y = ty, z = tz}
    spawnWreckingBirds(tx, ty, tz)
    return true
end

local function findLampLightIndex()
    local ok, count = pcall(getModel2DFXCount, LAMP_MODEL, false)
    if not ok or type(count) ~= "number" then
        return nil
    end

    for index = 0, count - 1 do
        local typeOk, effectType = pcall(getModel2DFXType, LAMP_MODEL, index)
        if typeOk and effectType == "light" then
            return index
        end
    end
    return nil
end

local function configureHarnessLights()
    local index = findLampLightIndex()
    if index == nil then
        return
    end

    local ok1 = pcall(setModel2DFXProperty, LAMP_MODEL, index, "color", tocolor(255, 165, 55, 255))
    local ok2 = pcall(setModel2DFXProperty, LAMP_MODEL, index, "showMode", "warnlight")
    local ok3 = pcall(setModel2DFXProperty, LAMP_MODEL, index, "coronaSize", 1.9)
    local ok4 = pcall(setModel2DFXProperty, LAMP_MODEL, index, "lightRange", 18.0)
    if ok1 or ok2 or ok3 or ok4 then
        show.lamp2DFXTouched = true
        show.lampLightIndex = index
    end
end

local function createHarnessAct()
    local cx, y, z = show.centerX, show.stageY, show.centerZ

    show.holder = createHolder(cx, y, z + 12.0)
    show.rope = createManagedRope(show.holder, "harness", 5.5, 0.56)
    show.payload = track(createVehicle(HARNESS_VEHICLE_MODEL, cx, y, z + 6.0, 0, 0, 180))

    if not isElement(show.holder) or not isElement(show.rope) or not isElement(show.payload) then
        return false
    end

    configureElement(show.payload)
    placeOnGround(show.payload, cx, y, z, 0.03)
    setElementCollisionsEnabled(show.payload, true)
    setElementFrozen(show.payload, true)

    for _, lampX in ipairs({cx - 8.5, cx + 8.5}) do
        local lamp = track(createObject(LAMP_MODEL, lampX, y + 4.0, z + 8.0, 0, 0, lampX < cx and 90 or 270))
        if isElement(lamp) then
            configureElement(lamp)
            placeOnGround(lamp, lampX, y + 4.0, z, 0.02)
            setElementFrozen(lamp, true)
            setElementCollisionsEnabled(lamp, false)
        end
    end

    configureHarnessLights()
    return true
end

local function setInitialCamera(act)
    local cx, y, z = show.centerX, show.stageY, show.centerZ

    if act == ACT_MAGNET then
        setCameraMatrix(cx + 11.5, y - 18.5, z + 6.4, cx, y, z + 2.6, 0, 58)
    elseif act == ACT_WRECKING_BALL then
        setCameraMatrix(cx + 12.5, y - 20.0, z + 8.0, cx + 1.5, y, z + 4.0, 0, 60)
    else
        setCameraMatrix(cx + 12.0, y - 20.0, z + 7.5, cx, y, z + 3.3, 0, 58)
    end
end

local function startAct(act)
    cleanupAct()

    show.act = act
    show.actCreatedAt = getTickCount()
    show.actStartedAt = 0
    show.actReady = false
    show.finished = false

    setTime(20, 0)

    local ok = false
    if act == ACT_MAGNET then
        ok = createMagnetAct()
    elseif act == ACT_WRECKING_BALL then
        ok = createWreckingBallAct()
    elseif act == ACT_HARNESS then
        ok = createHarnessAct()
    end

    setInitialCamera(act)
    return ok
end

local function igniteMagnetPayload()
    if isElement(show.fire) or not isElement(show.payload) then
        return
    end

    local x, y, z = getElementPosition(show.payload)
    show.fire = track(createFire(x, y, z + 0.6, {
        duration = 5200,
        strength = 1.65,
        damage = false,
        spread = false,
        target = show.payload,
    }))
    if isElement(show.fire) then
        configureElement(show.fire)
    end
end

local function updateMagnetFire()
    if not isElement(show.fire) or not isElement(show.payload) then
        return
    end

    -- `target` controls managed-fire damage; it does not parent the fire to
    -- the target. Keep the visual flame on the carried object while the rope
    -- moves it; otherwise the showcase leaves a fire suspended at the old
    -- pickup position and makes the sequence look stuck.
    local x, y, z = getElementPosition(show.payload)
    setElementPosition(show.fire, x, y, z + MAGNET_FIRE_OFFSET_Z)
end

local function animateMagnet(elapsed)
    local cx, y, z = show.centerX, show.stageY, show.centerZ
    local length = 6.5

    if elapsed >= 1200 and elapsed < 3500 then
        length = lerp(6.5, 10.0, smoothstep((elapsed - 1200) / 2300))
    elseif elapsed >= 3500 and elapsed < 3900 then
        length = 10.0
    elseif elapsed >= 3900 and elapsed < 6800 then
        attachCurrentPayload()
        length = lerp(10.0, 5.2, smoothstep((elapsed - 3900) / 2900))
    elseif elapsed >= 6800 then
        attachCurrentPayload()
        length = 5.2
    end

    if isElement(show.rope) then
        setRopeLength(show.rope, length)
    end

    updateMagnetFire()

    if elapsed >= 7000 then
        moveHolderOnce("magnet-carry", 2300, cx + 6.0, y, z + 11.5, "InOutQuad")
    end

    if elapsed >= 7600 then
        igniteMagnetPayload()
    end

    if elapsed >= 10200 and not show.released and show.attached and isElement(show.rope) and isElement(show.payload) then
        show.released = true
        detachElementFromRope(show.rope)
        show.attached = false
        setElementVelocity(show.payload, 0.030, 0.008, 0.010)
        setElementAngularVelocity(show.payload, 0.03, -0.04, 0.09)
    end
end

local function scatterBirds()
    if not show.targetPos then
        return
    end

    for i, bird in ipairs(show.birds) do
        if isElement(bird) then
            local x, y, z = getElementPosition(bird)
            local dx = x - show.targetPos.x
            local dy = y - show.targetPos.y
            local distance = math.max(0.5, math.sqrt(dx * dx + dy * dy))
            local speed = 7.5 + (i % 4) * 0.8
            setBirdMovementEnabled(bird, true)
            setBirdCurvedFlightEnabled(bird, false)
            setBirdWingBeatTime(bird, 170 + (i % 4) * 25)
            setBirdTargetVelocity(bird, Vector3(dx / distance * speed, dy / distance * speed, 3.5 + (i % 3) * 0.8))
        end
    end
end

local function triggerWreckingBallImpact(ballPos)
    if show.impactTriggered or not isElement(show.target) or not show.targetPos then
        return
    end
    show.impactTriggered = true

    local impact = ballPos or Vector3(show.targetPos.x - 1.0, show.targetPos.y, show.targetPos.z + 1.4)
    local effect = createObjectBreakEffect(show.target, {
        fragments = 56,
        force = 6.8,
        randomness = 1.1,
        velocity = {1.1, 0.0, 0.65},
        impactPosition = {impact.x, impact.y, impact.z},
        lifetime = 7600,
        gravity = 9.81,
        bounce = 0.18,
        drag = 0.10,
        renderDistance = 380,
        seed = 20260820,
        hideOriginal = true,
        disableOriginalCollision = true,
    })

    if isElement(effect) then
        show.breakEffect = track(effect)
    else
        outputDebugString("[ROPE SHOWCASE] createObjectBreakEffect failed for wrecking-ball target", 1)
    end

    scatterBirds()
end

local function updateWreckingBirds()
    if show.impactTriggered or not show.targetPos then
        return
    end

    for _, bird in ipairs(show.birds) do
        if isElement(bird) then
            local x, y = getElementPosition(bird)
            local dx = x - show.targetPos.x
            local dy = y - show.targetPos.y
            local distance = math.max(0.5, math.sqrt(dx * dx + dy * dy))
            local tangentX = -dy / distance
            local tangentY = dx / distance
            setBirdTargetVelocity(bird, Vector3(tangentX * 1.15, tangentY * 1.15, 0.05))
        end
    end
end

local function animateWreckingBall(elapsed)
    local cx, y, z = show.centerX, show.stageY, show.centerZ

    if isElement(show.rope) then
        setRopeLength(show.rope, 9.0)
    end

    if elapsed >= 1400 then
        moveHolderOnce("wreck-pullback", 1400, cx - 9.0, y, z + 12.5, "InOutQuad")
    end
    if elapsed >= 3200 then
        moveHolderOnce("wreck-launch", 1800, cx + 6.5, y, z + 12.5, "InOutQuad")
    end

    updateWreckingBirds()

    if elapsed >= 4200 and not show.impactTriggered and isElement(show.rope) and show.targetPos then
        local ballPos = getRopePositionAt(show.rope, 0.98)
        if ballPos then
            local dx = ballPos.x - show.targetPos.x
            local dy = ballPos.y - show.targetPos.y
            local dz = ballPos.z - (show.targetPos.z + 1.3)
            if dx * dx + dy * dy + dz * dz <= 12.96 then
                triggerWreckingBallImpact(ballPos)
            end
        end
    end

    if elapsed >= 7200 and not show.impactTriggered then
        local ballPos = isElement(show.rope) and getRopePositionAt(show.rope, 0.98) or nil
        triggerWreckingBallImpact(ballPos)
    end
end

local function animateHarness(elapsed)
    local cx, y, z = show.centerX, show.stageY, show.centerZ
    local length = 5.5

    if elapsed >= 1600 and elapsed < 3400 then
        length = lerp(5.5, 10.0, smoothstep((elapsed - 1600) / 1800))
    elseif elapsed >= 3400 and elapsed < 3750 then
        length = 10.0
    elseif elapsed >= 3750 and elapsed < 6800 then
        attachCurrentPayload()
        length = lerp(10.0, 5.2, smoothstep((elapsed - 3750) / 3050))
    elseif elapsed >= 6800 then
        attachCurrentPayload()
        length = 5.2
    end

    if isElement(show.rope) then
        setRopeLength(show.rope, length)
    end

    if elapsed >= 7000 then
        moveHolderOnce("harness-carry", 2600, cx + 6.5, y, z + 12.0, "InOutQuad")
    end
end

local function updateCamera(act, elapsed)
    local cx, y, z = show.centerX, show.stageY, show.centerZ

    if act == ACT_MAGNET then
        cameraLerp(
            cx + 11.5, y - 18.5, z + 7.5, cx + 1.0, y, z + 3.4,
            cx + 10.0, y - 16.0, z + 7.0, cx + 3.0, y, z + 4.0,
            elapsed / ACT_DURATION[act], 58, 55
        )
    elseif act == ACT_WRECKING_BALL then
        cameraLerp(
            cx + 12.5, y - 20.0, z + 8.0, cx + 1.5, y, z + 4.0,
            cx + 10.0, y - 16.5, z + 7.0, cx + 3.5, y, z + 3.8,
            elapsed / ACT_DURATION[act], 60, 56
        )
    else
        cameraLerp(
            cx + 12.0, y - 20.0, z + 7.5, cx, y, z + 3.3,
            cx + 9.0, y - 16.0, z + 7.0, cx + 3.5, y, z + 4.0,
            elapsed / ACT_DURATION[act], 58, 55
        )
    end
end

local function drawTitle(act, elapsed)
    if elapsed > 2300 then
        return
    end

    local sw, sh = guiGetScreenSize()
    local title, subtitle

    if act == ACT_MAGNET then
        title = "1 / 3   MINI MAGNET"
        subtitle = "PICK UP  /  MOVE  /  RELEASE"
    elseif act == ACT_WRECKING_BALL then
        title = "2 / 3   WRECKING BALL"
        subtitle = "NATIVE ROPE PHYSICS  +  RUNTIME FRACTURE"
    else
        title = "3 / 3   HARNESS"
        subtitle = "LIFT AND TRANSPORT VEHICLES"
    end

    dxDrawText(title, 0, sh * 0.10, sw, sh * 0.10 + 42, tocolor(255, 255, 255, 245), 1.35,
        "default-bold", "center", "center", false, false, true)
    dxDrawText(subtitle, 0, sh * 0.10 + 38, sw, sh * 0.10 + 76, tocolor(220, 220, 220, 235), 1.0,
        "default-bold", "center", "center", false, false, true)
end

local function drawFinalCard(elapsed)
    if show.act ~= ACT_HARNESS or elapsed < 9500 then
        return
    end

    local sw, sh = guiGetScreenSize()
    local t = smoothstep((elapsed - 9500) / 800)
    local alpha = math.floor(245 * t)

    dxDrawText("MANAGED ROPE API", 0, sh * 0.13, sw, sh * 0.13 + 48, tocolor(255, 255, 255, alpha), 1.55,
        "default-bold", "center", "center", false, false, true)
    dxDrawText("OBJECTS  |  VEHICLES  |  FRACTURE  |  FIRE  |  BIRDS  |  NATIVE 2DFX", 0, sh * 0.13 + 48,
        sw, sh * 0.13 + 88, tocolor(220, 220, 220, alpha), 0.95, "default-bold", "center", "center", false, false, true)
end

local function drawBlackOverlay(alpha)
    if alpha <= 0 then
        return
    end
    local sw, sh = guiGetScreenSize()
    dxDrawRectangle(0, 0, sw, sh, tocolor(0, 0, 0, clamp(math.floor(alpha), 0, 255)), true)
end

local function failShow(message)
    if not show.running then
        return
    end
    outputChatBox("[ROPE SHOWCASE] " .. tostring(message), 255, 90, 90)
    show.finished = true
    triggerServerEvent("ropeShowcase:finished", resourceRoot)
end

local function renderShow()
    if not show.running then
        return
    end

    local now = getTickCount()

    if not show.actReady then
        if isElement(show.rope) and isRopeActive(show.rope) then
            show.actReady = true
            show.actStartedAt = now
            setInitialCamera(show.act)

            if not show.readySent then
                show.readySent = true
                triggerServerEvent("ropeShowcase:ready", resourceRoot)
            end
        elseif now - show.actCreatedAt >= NATIVE_READY_TIMEOUT then
            failShow("Native rope slot did not become ready for act " .. tostring(show.act) .. ".")
        end

        drawBlackOverlay(255)
        return
    end

    local elapsed = now - show.actStartedAt
    local duration = ACT_DURATION[show.act]

    if show.act == ACT_MAGNET then
        animateMagnet(elapsed)
    elseif show.act == ACT_WRECKING_BALL then
        animateWreckingBall(elapsed)
    else
        animateHarness(elapsed)
    end

    updateCamera(show.act, elapsed)
    drawTitle(show.act, elapsed)
    drawFinalCard(elapsed)

    local overlayAlpha = 0
    if show.act > ACT_MAGNET and elapsed < FADE_MS then
        overlayAlpha = 255 * (1.0 - elapsed / FADE_MS)
    end
    if elapsed > duration - FADE_MS then
        overlayAlpha = math.max(overlayAlpha, 255 * ((elapsed - (duration - FADE_MS)) / FADE_MS))
    end
    drawBlackOverlay(overlayAlpha)

    if elapsed < duration or show.finished then
        return
    end

    if show.act < ACT_HARNESS then
        if not startAct(show.act + 1) then
            failShow("Could not create act " .. tostring(show.act + 1) .. ".")
        end
        return
    end

    show.finished = true
    triggerServerEvent("ropeShowcase:finished", resourceRoot)
end

local function stopShow()
    if show.running then
        removeEventHandler("onClientRender", root, renderShow)
    end

    show.running = false
    show.readySent = false
    show.finished = false
    cleanupAct()

    if show.savedTime then
        setTime(show.savedTime.hour, show.savedTime.minute)
    end
    show.savedTime = nil

    setCameraTarget(localPlayer)
    setPresentationUI(true)
end

addEvent("ropeShowcase:start", true)
addEventHandler("ropeShowcase:start", resourceRoot, function(centerX, centerY, centerZ, dimension)
    stopShow()

    local hour, minute = getTime()
    show.savedTime = {hour = hour, minute = minute}
    show.centerX = centerX
    show.centerY = centerY
    show.centerZ = centerZ
    show.stageY = centerY + 4.0
    show.dimension = dimension
    show.running = true
    show.readySent = false
    show.finished = false

    setTime(20, 0)
    setPresentationUI(false)
    addEventHandler("onClientRender", root, renderShow)

    if not startAct(ACT_MAGNET) then
        failShow("Could not create the mini-magnet act.")
    end
end)

addEvent("ropeShowcase:stop", true)
addEventHandler("ropeShowcase:stop", resourceRoot, stopShow)

addEventHandler("onClientResourceStop", resourceRoot, stopShow)
