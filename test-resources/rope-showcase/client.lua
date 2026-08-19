local ANCHOR_MODEL = 1337
local CARGO_MODEL = 2912
local HARNESS_VEHICLE_MODEL = 422

local ACT_MAGNET = 1
local ACT_WRECKING_BALL = 2
local ACT_HARNESS = 3

local ACT_DURATION = {
    [ACT_MAGNET] = 10500,
    [ACT_WRECKING_BALL] = 9000,
    [ACT_HARNESS] = 11500,
}

local FADE_MS = 450
local NATIVE_READY_TIMEOUT = 4000

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
    props = {},
    elements = {},

    attached = false,
    released = false,
    impactTriggered = false,
    finished = false,
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

local function cleanupAct()
    detachPayload()

    for i = #show.elements, 1, -1 do
        local element = show.elements[i]
        if isElement(element) then
            destroyElement(element)
        end
    end

    show.elements = {}
    show.props = {}
    show.rope = nil
    show.holder = nil
    show.payload = nil
    show.attached = false
    show.released = false
    show.impactTriggered = false
    show.actReady = false
end

local function createMagnetAct()
    local cx, y, z = show.centerX, show.stageY, show.centerZ

    show.holder = createHolder(cx, y, z + 10.5)
    show.rope = createManagedRope(show.holder, "miniMagnet", 4.5, 0.52)
    show.payload = track(createObject(CARGO_MODEL, cx, y, z + 0.65, 0, 0, 18))

    if not isElement(show.holder) or not isElement(show.rope) or not isElement(show.payload) then
        return false
    end

    configureElement(show.payload)
    setElementCollisionsEnabled(show.payload, true)
    setElementFrozen(show.payload, true)
    setObjectProperty(show.payload, "mass", 45.0)
    setObjectProperty(show.payload, "turn_mass", 55.0)
    setObjectProperty(show.payload, "air_resistance", 0.995)
    setObjectProperty(show.payload, "elasticity", 0.18)
    return true
end

local function createWreckingBallAct()
    local cx, y, z = show.centerX, show.stageY, show.centerZ

    show.holder = createHolder(cx - 5.0, y, z + 12.0)
    show.rope = createManagedRope(show.holder, "wreckingBall", 7.0, 0.48)
    if not isElement(show.holder) or not isElement(show.rope) then
        return false
    end

    local positions = {
        {cx + 4.2, y, z + 0.65},
        {cx + 5.6, y, z + 0.65},
        {cx + 7.0, y, z + 0.65},
        {cx + 4.9, y, z + 1.85},
        {cx + 6.3, y, z + 1.85},
        {cx + 5.6, y, z + 3.05},
    }

    for _, position in ipairs(positions) do
        local box = track(createObject(CARGO_MODEL, position[1], position[2], position[3], 0, 0, 0))
        if not isElement(box) then
            return false
        end
        configureElement(box)
        setElementCollisionsEnabled(box, true)
        setElementFrozen(box, true)
        show.props[#show.props + 1] = box
    end

    return true
end

local function createHarnessAct()
    local cx, y, z = show.centerX, show.stageY, show.centerZ

    show.holder = createHolder(cx, y, z + 11.5)
    show.rope = createManagedRope(show.holder, "harness", 5.0, 0.56)
    show.payload = track(createVehicle(HARNESS_VEHICLE_MODEL, cx, y, z + 1.0, 0, 0, 180))

    if not isElement(show.holder) or not isElement(show.rope) or not isElement(show.payload) then
        return false
    end

    configureElement(show.payload)
    setElementCollisionsEnabled(show.payload, true)
    setElementFrozen(show.payload, true)
    return true
end

local function setInitialCamera(act)
    local cx, y, z = show.centerX, show.stageY, show.centerZ

    if act == ACT_MAGNET then
        setCameraMatrix(cx + 11.5, y - 18.5, z + 7.5, cx, y, z + 3.5, 0, 58)
    elseif act == ACT_WRECKING_BALL then
        setCameraMatrix(cx + 12.5, y - 20.0, z + 8.0, cx + 2.0, y, z + 4.2, 0, 60)
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

local function animateMagnet(elapsed)
    local cx, y, z = show.centerX, show.stageY, show.centerZ
    local length = 4.5
    local holderX = cx

    if elapsed >= 1500 and elapsed < 3200 then
        length = lerp(4.5, 9.0, smoothstep((elapsed - 1500) / 1700))
    elseif elapsed >= 3200 and elapsed < 3500 then
        length = 9.0
    elseif elapsed >= 3500 and elapsed < 5600 then
        attachCurrentPayload()
        length = lerp(9.0, 5.0, smoothstep((elapsed - 3500) / 2100))
    elseif elapsed >= 5600 and elapsed < 8000 then
        attachCurrentPayload()
        length = 5.0
        holderX = lerp(cx, cx + 6.0, smoothstep((elapsed - 5600) / 2400))
    elseif elapsed >= 8000 then
        attachCurrentPayload()
        length = 5.0
        holderX = cx + 6.0
    end

    if isElement(show.holder) then
        setElementPosition(show.holder, holderX, y, z + 10.5)
    end
    if isElement(show.rope) then
        setRopeLength(show.rope, length)
    end

    if elapsed >= 8750 and not show.released and show.attached and isElement(show.rope) and isElement(show.payload) then
        show.released = true
        detachElementFromRope(show.rope)
        show.attached = false
        setElementVelocity(show.payload, 0.035, 0.010, 0.015)
        setElementAngularVelocity(show.payload, 0.03, -0.04, 0.09)
    end
end

local function triggerWreckingBallImpact()
    if show.impactTriggered then
        return
    end
    show.impactTriggered = true

    local impulses = {
        {0.10, -0.02, 0.08, 0.04, 0.02, 0.10},
        {0.08, 0.01, 0.10, -0.03, 0.05, 0.08},
        {0.12, 0.03, 0.07, 0.05, -0.04, 0.12},
        {0.07, -0.03, 0.12, -0.05, 0.03, 0.09},
        {0.09, 0.02, 0.11, 0.04, -0.05, 0.11},
        {0.06, 0.00, 0.14, -0.02, 0.04, 0.13},
    }

    for index, box in ipairs(show.props) do
        if isElement(box) then
            local impulse = impulses[index] or impulses[1]
            setElementFrozen(box, false)
            setElementVelocity(box, impulse[1], impulse[2], impulse[3])
            setElementAngularVelocity(box, impulse[4], impulse[5], impulse[6])
        end
    end
end

local function animateWreckingBall(elapsed)
    local cx, y, z = show.centerX, show.stageY, show.centerZ
    local holderX = cx - 5.0

    if elapsed >= 1800 and elapsed < 4300 then
        holderX = lerp(cx - 5.0, cx + 5.5, smoothstep((elapsed - 1800) / 2500))
    elseif elapsed >= 4300 then
        holderX = cx + 5.5
    end

    if isElement(show.holder) then
        setElementPosition(show.holder, holderX, y, z + 12.0)
    end
    if isElement(show.rope) then
        setRopeLength(show.rope, 7.0)
    end

    if elapsed >= 5000 then
        triggerWreckingBallImpact()
    end
end

local function animateHarness(elapsed)
    local cx, y, z = show.centerX, show.stageY, show.centerZ
    local length = 5.0
    local holderX = cx

    if elapsed >= 1800 and elapsed < 3500 then
        length = lerp(5.0, 10.0, smoothstep((elapsed - 1800) / 1700))
    elseif elapsed >= 3500 and elapsed < 3800 then
        length = 10.0
    elseif elapsed >= 3800 and elapsed < 6600 then
        attachCurrentPayload()
        length = lerp(10.0, 5.5, smoothstep((elapsed - 3800) / 2800))
    elseif elapsed >= 6600 and elapsed < 9000 then
        attachCurrentPayload()
        length = 5.5
        holderX = lerp(cx, cx + 5.5, smoothstep((elapsed - 6600) / 2400))
    elseif elapsed >= 9000 then
        attachCurrentPayload()
        length = 5.5
        holderX = cx + 5.5
    end

    if isElement(show.holder) then
        setElementPosition(show.holder, holderX, y, z + 11.5)
    end
    if isElement(show.rope) then
        setRopeLength(show.rope, length)
    end
end

local function updateCamera(act, elapsed)
    local cx, y, z = show.centerX, show.stageY, show.centerZ

    if act == ACT_MAGNET then
        cameraLerp(
            cx + 11.5, y - 18.5, z + 7.5, cx + 1.0, y, z + 3.4,
            cx + 9.0, y - 15.5, z + 6.6, cx + 3.0, y, z + 4.0,
            elapsed / ACT_DURATION[act], 58, 55
        )
    elseif act == ACT_WRECKING_BALL then
        cameraLerp(
            cx + 12.5, y - 20.0, z + 8.0, cx + 2.0, y, z + 4.2,
            cx + 9.0, y - 16.0, z + 6.7, cx + 4.5, y, z + 3.5,
            elapsed / ACT_DURATION[act], 60, 56
        )
    else
        cameraLerp(
            cx + 12.0, y - 20.0, z + 7.5, cx, y, z + 3.3,
            cx + 9.0, y - 16.0, z + 6.8, cx + 3.5, y, z + 4.0,
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
        subtitle = "PICK UP OBJECTS"
    elseif act == ACT_WRECKING_BALL then
        title = "2 / 3   WRECKING BALL"
        subtitle = "NATIVE WEIGHT + ROPE PHYSICS"
    else
        title = "3 / 3   HARNESS"
        subtitle = "LIFT VEHICLES"
    end

    dxDrawText(title, 0, sh * 0.10, sw, sh * 0.10 + 42, tocolor(255, 255, 255, 245), 1.35,
        "default-bold", "center", "center", false, false, true)
    dxDrawText(subtitle, 0, sh * 0.10 + 38, sw, sh * 0.10 + 76, tocolor(220, 220, 220, 235), 1.0,
        "default-bold", "center", "center", false, false, true)
end

local function drawFinalCard(elapsed)
    if show.act ~= ACT_HARNESS or elapsed < 9000 then
        return
    end

    local sw, sh = guiGetScreenSize()
    local t = smoothstep((elapsed - 9000) / 800)
    local alpha = math.floor(245 * t)

    dxDrawText("MANAGED ROPE API", 0, sh * 0.13, sw, sh * 0.13 + 48, tocolor(255, 255, 255, alpha), 1.55,
        "default-bold", "center", "center", false, false, true)
    dxDrawText("OBJECTS  |  VEHICLES  |  NATIVE HOOKS  |  MOVING HOLDERS  |  PICKUP / RELEASE", 0, sh * 0.13 + 48,
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
    setCameraTarget(localPlayer)
    setPresentationUI(true)
end

addEvent("ropeShowcase:start", true)
addEventHandler("ropeShowcase:start", resourceRoot, function(centerX, centerY, centerZ, dimension)
    stopShow()

    show.centerX = centerX
    show.centerY = centerY
    show.centerZ = centerZ
    show.stageY = centerY + 4.0
    show.dimension = dimension
    show.running = true
    show.readySent = false
    show.finished = false

    setPresentationUI(false)
    addEventHandler("onClientRender", root, renderShow)

    if not startAct(ACT_MAGNET) then
        failShow("Could not create the mini-magnet act.")
    end
end)

addEvent("ropeShowcase:stop", true)
addEventHandler("ropeShowcase:stop", resourceRoot, stopShow)

addEventHandler("onClientResourceStop", resourceRoot, stopShow)
