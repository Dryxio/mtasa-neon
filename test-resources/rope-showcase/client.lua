local ANCHOR_MODEL = 1337
local CARGO_MODEL = 2912
local HARNESS_VEHICLE_MODEL = 422
local SHOW_DURATION = 36000
local STAGE_SPACING = 18.0
local ANCHOR_Z_OFFSET = 18.0

local show = {
    active = false,
    finished = false,
    startedAt = 0,
    centerX = 0,
    centerY = 0,
    centerZ = 0,
    dimension = 0,
    stageY = 0,
    cargo = nil,
    harnessVehicle = nil,
    heroAnchor = nil,
    leftAnchor = nil,
    rightAnchor = nil,
    heroRope = nil,
    leftRope = nil,
    rightRope = nil,
    elements = {},
    cargoReleased = false,
    prepareTimer = nil,
    beginTimer = nil,
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

local function clearTimers()
    if isTimer(show.prepareTimer) then
        killTimer(show.prepareTimer)
    end
    if isTimer(show.beginTimer) then
        killTimer(show.beginTimer)
    end
    show.prepareTimer = nil
    show.beginTimer = nil
end

local function destroyScene()
    if isElement(show.heroRope) and isElement(show.cargo) then
        detachElementFromRope(show.heroRope)
    end
    if isElement(show.rightRope) and isElement(show.harnessVehicle) then
        detachElementFromRope(show.rightRope)
    end

    for i = #show.elements, 1, -1 do
        local element = show.elements[i]
        if isElement(element) then
            destroyElement(element)
        end
    end

    show.elements = {}
    show.cargo = nil
    show.harnessVehicle = nil
    show.heroAnchor = nil
    show.leftAnchor = nil
    show.rightAnchor = nil
    show.heroRope = nil
    show.leftRope = nil
    show.rightRope = nil
    show.cargoReleased = false
end

local function setPresentationUI(visible)
    showPlayerHudComponent("all", visible)
    showChat(visible)
end

local function cameraLerp(ax, ay, az, alx, aly, alz, bx, by, bz, blx, bly, blz, t, fovA, fovB)
    t = smoothstep(t)
    setCameraMatrix(
        lerp(ax, bx, t),
        lerp(ay, by, t),
        lerp(az, bz, t),
        lerp(alx, blx, t),
        lerp(aly, bly, t),
        lerp(alz, blz, t),
        0,
        lerp(fovA or 65, fovB or 65, t)
    )
end

local function createAnchor(x, y, z)
    local anchor = track(createObject(ANCHOR_MODEL, x, y, z, 0, 0, 0))
    if not isElement(anchor) then
        return false
    end

    configureElement(anchor)
    setElementAlpha(anchor, 0)
    setElementCollisionsEnabled(anchor, false)
    setElementFrozen(anchor, true)
    return anchor
end

local function createRopeForAnchor(anchor, ropeType, length, winchHeight)
    if not isElement(anchor) then
        return false
    end

    local x, y, z = getElementPosition(anchor)
    local rope = createRope(x, y, z, {
        type = ropeType,
        holder = anchor,
        holderOffset = {0, 0, 0},
        duration = 0,
        fixedNode = 0,
        sitOnGround = false,
        winchHeight = winchHeight or 0.5,
        length = length,
        physics = true,
    })
    if not isElement(rope) then
        return false
    end

    track(rope)
    configureElement(rope)
    return rope
end

local function createScene()
    destroyScene()

    local cx, cy, cz = show.centerX, show.centerY, show.centerZ
    local stageY = show.stageY
    local leftX = cx - STAGE_SPACING
    local rightX = cx + STAGE_SPACING
    local anchorZ = cz + ANCHOR_Z_OFFSET

    show.heroAnchor = createAnchor(cx, stageY, anchorZ)
    show.leftAnchor = createAnchor(leftX, stageY, anchorZ)
    show.rightAnchor = createAnchor(rightX, stageY, anchorZ)

    if not isElement(show.heroAnchor) or not isElement(show.leftAnchor) or not isElement(show.rightAnchor) then
        outputDebugString("[ROPE SHOWCASE] Failed to create rope holders", 1)
        return false
    end

    -- Explicit short lengths keep the native hook objects well above runway Z.
    -- WRECKING_BALL otherwise initializes with roughly 68 units of rope.
    show.leftRope = createRopeForAnchor(show.leftAnchor, "wreckingBall", 12.5, 0.48)
    show.heroRope = createRopeForAnchor(show.heroAnchor, "miniMagnet", 11.0, 0.52)
    show.rightRope = createRopeForAnchor(show.rightAnchor, "harness", 12.0, 0.56)

    if not isElement(show.heroRope) or not isElement(show.leftRope) or not isElement(show.rightRope) then
        outputDebugString("[ROPE SHOWCASE] Failed to create one or more ropes", 1)
        return false
    end

    show.cargo = track(createObject(CARGO_MODEL, cx, stageY, cz + 6.0, 0, 0, 18))
    if not isElement(show.cargo) then
        outputDebugString("[ROPE SHOWCASE] Failed to create center cargo", 1)
        return false
    end
    configureElement(show.cargo)
    setElementCollisionsEnabled(show.cargo, true)
    setElementFrozen(show.cargo, true)
    setObjectProperty(show.cargo, "mass", 45.0)
    setObjectProperty(show.cargo, "turn_mass", 55.0)
    setObjectProperty(show.cargo, "air_resistance", 0.995)
    setObjectProperty(show.cargo, "elasticity", 0.18)

    show.harnessVehicle = track(createVehicle(HARNESS_VEHICLE_MODEL, rightX, stageY, cz + 5.0, 0, 0, 180))
    if not isElement(show.harnessVehicle) then
        outputDebugString("[ROPE SHOWCASE] Failed to create harness vehicle", 1)
        return false
    end
    configureElement(show.harnessVehicle)
    setElementCollisionsEnabled(show.harnessVehicle, true)
    setElementFrozen(show.harnessVehicle, true)

    if not attachElementToRope(show.heroRope, show.cargo) then
        outputDebugString("[ROPE SHOWCASE] Center miniMagnet attach failed", 1)
        return false
    end
    if not attachElementToRope(show.rightRope, show.harnessVehicle) then
        outputDebugString("[ROPE SHOWCASE] Harness vehicle attach failed", 1)
        return false
    end

    return true
end

local function animateRopes(elapsed)
    local cx, cz = show.centerX, show.centerZ
    local stageY = show.stageY
    local leftX = cx - STAGE_SPACING
    local rightX = cx + STAGE_SPACING
    local anchorZ = cz + ANCHOR_Z_OFFSET
    local time = elapsed / 1000.0

    -- LEFT: the native wrecking-ball hook is the payload. Moving the holder
    -- laterally makes its weight/swing immediately distinguishable from a bare rope.
    if isElement(show.leftAnchor) and isElement(show.leftRope) then
        local swing = math.sin(time * 1.05)
        setElementPosition(show.leftAnchor, leftX + swing * 3.7, stageY + math.sin(time * 0.52) * 0.7, anchorZ)
        setRopeLength(show.leftRope, 12.5 + math.sin(time * 0.7) * 0.35)
        setRopeWinchHeight(show.leftRope, 0.48)
    end

    -- CENTER: visibly hoist the crate, translate it, then release it in the final shot.
    if isElement(show.heroAnchor) and isElement(show.heroRope) then
        local heroX, heroY, heroZ = cx, stageY, anchorZ
        local heroLength = 11.0

        if elapsed >= 9000 and elapsed < 16000 then
            heroLength = lerp(11.0, 6.6, smoothstep((elapsed - 9000) / 7000))
        elseif elapsed >= 16000 and elapsed < 24500 then
            local t = (elapsed - 16000) / 8500
            heroX = cx + math.sin(t * math.pi * 1.35) * 5.2
            heroY = stageY + math.sin(t * math.pi * 2.0) * 1.5
            heroZ = anchorZ + math.sin(t * math.pi) * 1.0
            heroLength = 6.6
        elseif elapsed >= 24500 and elapsed < 30200 then
            heroLength = lerp(6.6, 9.2, smoothstep((elapsed - 24500) / 5700))
            heroX = cx + 1.5
        elseif elapsed >= 30200 then
            heroLength = 9.2
            heroX = cx + 1.5
        end

        setElementPosition(show.heroAnchor, heroX, heroY, heroZ)
        setRopeLength(show.heroRope, heroLength)
        setRopeWinchHeight(show.heroRope, 0.52)
    end

    -- RIGHT: the harness carries a real local Bobcat. It starts lower, then the
    -- rope shortens during the right-side close-up so the whole vehicle lifts.
    if isElement(show.rightAnchor) and isElement(show.rightRope) then
        local rightLength = 12.0
        local rightZ = anchorZ
        if elapsed >= 19000 and elapsed < 28500 then
            rightLength = lerp(12.0, 8.0, smoothstep((elapsed - 19000) / 9500))
            rightZ = anchorZ + smoothstep((elapsed - 19000) / 9500) * 1.2
        elseif elapsed >= 28500 then
            rightLength = 8.0
            rightZ = anchorZ + 1.2
        end
        setElementPosition(show.rightAnchor, rightX + math.sin(time * 0.55) * 1.4, stageY, rightZ)
        setRopeLength(show.rightRope, rightLength)
        setRopeWinchHeight(show.rightRope, 0.56)
    end

    if elapsed >= 30600 and not show.cargoReleased and isElement(show.cargo) and isElement(show.heroRope) then
        show.cargoReleased = true
        detachElementFromRope(show.heroRope)
        setElementFrozen(show.cargo, false)
        setElementVelocity(show.cargo, 0.035, 0.010, 0.020)
        setElementAngularVelocity(show.cargo, 0.03, -0.05, 0.10)
    end
end

local function updateCamera(elapsed)
    local cx, cz = show.centerX, show.centerZ
    local stageY = show.stageY
    local leftX = cx - STAGE_SPACING
    local rightX = cx + STAGE_SPACING

    -- 0-7s: establish all three demonstrations at once.
    if elapsed < 7000 then
        cameraLerp(
            cx, stageY - 50.0, cz + 10.5,
            cx, stageY, cz + 8.0,
            cx + 2.0, stageY - 43.0, cz + 11.5,
            cx, stageY, cz + 8.0,
            elapsed / 7000,
            76, 70
        )
        return
    end

    -- 7-13s: wrecking ball close-up.
    if elapsed < 13000 then
        local t = (elapsed - 7000) / 6000
        cameraLerp(
            cx + 2.0, stageY - 43.0, cz + 11.5,
            cx, stageY, cz + 8.0,
            leftX + 8.0, stageY - 19.0, cz + 9.0,
            leftX, stageY, cz + 7.5,
            t,
            70, 58
        )
        return
    end

    -- 13-23s: mini magnet + crate hoist.
    if elapsed < 23000 then
        local t = (elapsed - 13000) / 10000
        local cargoX, cargoY, cargoZ = cx, stageY, cz + 5.0
        if isElement(show.cargo) then
            cargoX, cargoY, cargoZ = getElementPosition(show.cargo)
        end
        cameraLerp(
            leftX + 8.0, stageY - 19.0, cz + 9.0,
            leftX, stageY, cz + 7.5,
            cargoX + 9.5, cargoY - 18.0, cargoZ + 6.5,
            cargoX, cargoY, cargoZ + 1.0,
            t,
            58, 56
        )
        return
    end

    -- 23-30s: harness + vehicle lift.
    if elapsed < 30000 then
        local t = (elapsed - 23000) / 7000
        local vx, vy, vz = rightX, stageY, cz + 4.0
        if isElement(show.harnessVehicle) then
            vx, vy, vz = getElementPosition(show.harnessVehicle)
        end
        cameraLerp(
            cx + 9.5, stageY - 18.0, cz + 11.0,
            cx, stageY, cz + 6.0,
            vx - 8.5, vy - 19.0, vz + 6.5,
            vx, vy, vz + 1.2,
            t,
            58, 58
        )
        return
    end

    -- 30-36s: final wide shot, including the center crate release.
    cameraLerp(
        rightX - 8.5, stageY - 19.0, cz + 10.0,
        rightX, stageY, cz + 6.0,
        cx, stageY - 48.0, cz + 12.0,
        cx, stageY, cz + 7.0,
        (elapsed - 30000) / 6000,
        58, 74
    )
end

local function worldLabel(text, x, y, z)
    local sx, sy = getScreenFromWorldPosition(x, y, z, 0.05)
    if not sx then
        return
    end

    dxDrawText(text, sx - 150, sy - 30, sx + 150, sy + 30, tocolor(255, 255, 255, 235), 1.0,
        "default-bold", "center", "center", false, false, true, true)
end

local function drawLabels(elapsed)
    local cx, cz = show.centerX, show.centerZ
    local stageY = show.stageY
    local leftX = cx - STAGE_SPACING
    local rightX = cx + STAGE_SPACING

    if elapsed < 7000 then
        local sw = guiGetScreenSize()
        dxDrawText("MANAGED ROPE API  -  3 NATIVE ROPE TYPES", 0, 45, sw, 85, tocolor(255, 255, 255, 235), 1.25,
            "default-bold", "center", "center", false, false, true, true)
    end

    local leftPos = isElement(show.leftRope) and getRopePositionAt(show.leftRope, 0.96) or nil
    if leftPos then
        worldLabel("WRECKING BALL\nnative weighted hook", leftPos.x, leftPos.y, leftPos.z + 3.0)
    else
        worldLabel("WRECKING BALL", leftX, stageY, cz + 8.0)
    end

    if isElement(show.cargo) then
        local x, y, z = getElementPosition(show.cargo)
        worldLabel("MINI MAGNET\nscripted object pickup", x, y, z + 3.0)
    end

    if isElement(show.harnessVehicle) then
        local x, y, z = getElementPosition(show.harnessVehicle)
        worldLabel("HARNESS\nvehicle lift", x, y, z + 3.4)
    else
        worldLabel("HARNESS", rightX, stageY, cz + 8.0)
    end
end

local function renderShow()
    if not show.active then
        return
    end

    local elapsed = getTickCount() - show.startedAt
    animateRopes(elapsed)
    updateCamera(elapsed)
    drawLabels(elapsed)

    if elapsed >= SHOW_DURATION and not show.finished then
        show.finished = true
        triggerServerEvent("ropeShowcase:finished", resourceRoot)
    end
end

local function stopShow()
    clearTimers()
    if show.active then
        removeEventHandler("onClientRender", root, renderShow)
    end

    show.active = false
    show.finished = false
    destroyScene()
    setCameraTarget(localPlayer)
    setPresentationUI(true)
end

local function beginWhenNativeReady()
    local attempts = 0
    show.prepareTimer = setTimer(function()
        attempts = attempts + 1
        local ready = isElement(show.leftRope) and isElement(show.heroRope) and isElement(show.rightRope) and
            isRopeActive(show.leftRope) and isRopeActive(show.heroRope) and isRopeActive(show.rightRope)

        if ready then
            if isTimer(show.prepareTimer) then
                killTimer(show.prepareTimer)
            end
            show.prepareTimer = nil

            -- Reassert display lengths after native creation and let the hooks settle
            -- for a few frames before the fade-in starts.
            setRopeLength(show.leftRope, 12.5)
            setRopeLength(show.heroRope, 11.0)
            setRopeLength(show.rightRope, 12.0)
            setElementFrozen(show.cargo, false)
            setElementFrozen(show.harnessVehicle, false)

            setCameraMatrix(show.centerX, show.stageY - 50.0, show.centerZ + 10.5,
                show.centerX, show.stageY, show.centerZ + 8.0, 0, 76)
            setPresentationUI(false)

            show.beginTimer = setTimer(function()
                show.beginTimer = nil
                show.startedAt = getTickCount()
                show.active = true
                addEventHandler("onClientRender", root, renderShow)
                triggerServerEvent("ropeShowcase:ready", resourceRoot)
            end, 450, 1)
            return
        end

        if attempts >= 30 then
            if isTimer(show.prepareTimer) then
                killTimer(show.prepareTimer)
            end
            show.prepareTimer = nil
            outputChatBox("[ROPE SHOWCASE] Native rope slots did not become ready.", 255, 90, 90)
            triggerServerEvent("ropeShowcase:finished", resourceRoot)
        end
    end, 100, 30)
end

addEvent("ropeShowcase:start", true)
addEventHandler("ropeShowcase:start", resourceRoot, function(centerX, centerY, centerZ, dimension)
    stopShow()

    show.centerX = centerX
    show.centerY = centerY
    show.centerZ = centerZ
    show.stageY = centerY + 4.0
    show.dimension = dimension
    show.finished = false

    if not createScene() then
        outputChatBox("[ROPE SHOWCASE] Scene creation failed; run /ropetest all first.", 255, 90, 90)
        triggerServerEvent("ropeShowcase:finished", resourceRoot)
        return
    end

    beginWhenNativeReady()
end)

addEvent("ropeShowcase:stop", true)
addEventHandler("ropeShowcase:stop", resourceRoot, stopShow)

addEventHandler("onClientResourceStop", resourceRoot, stopShow)
