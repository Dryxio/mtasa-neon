local CARGO_MODEL = 2912
local SHOW_DURATION = 33000

local show = {
    active = false,
    finished = false,
    startedAt = 0,
    centerX = 0,
    centerY = 0,
    centerZ = 0,
    dimension = 0,
    cargo = nil,
    heroAnchor = nil,
    leftAnchor = nil,
    rightAnchor = nil,
    heroRope = nil,
    leftRope = nil,
    rightRope = nil,
    elements = {},
    cargoReleased = false,
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

local function destroyScene()
    if isElement(show.heroRope) and isElement(show.cargo) then
        detachElementFromRope(show.heroRope)
    end

    for i = #show.elements, 1, -1 do
        local element = show.elements[i]
        if isElement(element) then
            destroyElement(element)
        end
    end

    show.elements = {}
    show.cargo = nil
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
    local anchor = track(createObject(CARGO_MODEL, x, y, z, 0, 0, 0))
    if not isElement(anchor) then
        return false
    end

    configureElement(anchor)
    setElementAlpha(anchor, 0)
    setElementCollisionsEnabled(anchor, false)
    setElementFrozen(anchor, true)
    return anchor
end

local function createRopeForAnchor(anchor, ropeType, winchHeight)
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
        winchHeight = winchHeight or 0.35,
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

    -- Keep the showcase cargo local. Local elements are authoritative by
    -- construction in CClientRopeManager, and native CRope::PickUpObject turns a
    -- static GTA object into a moving physical object when it is attached.
    show.cargo = track(createObject(CARGO_MODEL, cx, cy, cz + 0.9, 0, 0, 18))
    if not isElement(show.cargo) then
        outputDebugString("[ROPE SHOWCASE] Failed to create local cargo object", 1)
        return false
    end

    configureElement(show.cargo)
    setElementCollisionsEnabled(show.cargo, true)
    setElementFrozen(show.cargo, false)
    setObjectProperty(show.cargo, "mass", 45.0)
    setObjectProperty(show.cargo, "turn_mass", 55.0)
    setObjectProperty(show.cargo, "air_resistance", 0.995)
    setObjectProperty(show.cargo, "elasticity", 0.18)
    setElementVelocity(show.cargo, 0, 0, 0)
    setElementAngularVelocity(show.cargo, 0, 0, 0)

    -- GTA's winch/crane rope types dereference m_pRopeHolder during CRope::Update.
    -- Use explicit invisible local objects as stable physical holders instead of
    -- creating free world-anchored magnet/harness ropes.
    show.heroAnchor = createAnchor(cx, cy, cz + 12.5)
    show.leftAnchor = createAnchor(cx - 7.0, cy + 1.5, cz + 12.0)
    show.rightAnchor = createAnchor(cx + 7.0, cy + 1.5, cz + 12.0)

    if not isElement(show.heroAnchor) or not isElement(show.leftAnchor) or not isElement(show.rightAnchor) then
        outputDebugString("[ROPE SHOWCASE] Failed to create one or more rope anchors", 1)
        return false
    end

    show.heroRope = createRopeForAnchor(show.heroAnchor, "miniMagnet", 0.22)
    show.leftRope = createRopeForAnchor(show.leftAnchor, "wreckingBall", 0.45)
    show.rightRope = createRopeForAnchor(show.rightAnchor, "harness", 0.60)

    if not isElement(show.heroRope) or not isElement(show.leftRope) or not isElement(show.rightRope) then
        outputDebugString("[ROPE SHOWCASE] Failed to create one or more ropes", 1)
        return false
    end

    if not attachElementToRope(show.heroRope, show.cargo) then
        outputDebugString("[ROPE SHOWCASE] attachElementToRope failed", 1)
        return false
    end

    return true
end

local function animateRopes(elapsed)
    local cx, cy, cz = show.centerX, show.centerY, show.centerZ
    local time = elapsed / 1000.0

    if isElement(show.leftAnchor) and isElement(show.leftRope) then
        setElementPosition(show.leftAnchor, cx - 7.0 + math.sin(time * 0.55) * 0.45, cy + 1.5, cz + 12.0)
        setRopeWinchHeight(show.leftRope, 0.30 + (math.sin(time * 0.85) * 0.5 + 0.5) * 0.42)
    end

    if isElement(show.rightAnchor) and isElement(show.rightRope) then
        setElementPosition(show.rightAnchor, cx + 7.0 + math.sin(time * 0.48 + 1.7) * 0.35, cy + 1.5, cz + 12.0)
        setRopeWinchHeight(show.rightRope, 0.25 + (math.sin(time * 0.72 + 2.2) * 0.5 + 0.5) * 0.48)
    end

    if not isElement(show.heroRope) or not isElement(show.heroAnchor) then
        return
    end

    local anchorX, anchorY, anchorZ = cx, cy, cz + 12.5
    local winch = 0.22

    if elapsed < 6000 then
        winch = 0.22
    elseif elapsed < 12000 then
        local t = smoothstep((elapsed - 6000) / 6000)
        winch = lerp(0.22, 0.82, t)
    elseif elapsed < 20000 then
        local t = (elapsed - 12000) / 8000
        anchorX = cx + math.sin(t * math.pi * 1.25) * 6.5
        anchorY = cy + math.sin(t * math.pi * 2.0) * 1.4
        anchorZ = cz + 12.5 + math.sin(t * math.pi) * 1.2
        winch = 0.82
    elseif elapsed < 24500 then
        local t = smoothstep((elapsed - 20000) / 4500)
        anchorX = lerp(cx, cx + 2.0, t)
        anchorZ = cz + 12.5
        winch = lerp(0.82, 0.18, t)
    else
        anchorX = cx + 2.0 + math.sin(time * 1.1) * 0.55
        winch = 0.18
    end

    setElementPosition(show.heroAnchor, anchorX, anchorY, anchorZ)
    setRopeWinchHeight(show.heroRope, winch)

    if elapsed >= 24750 and not show.cargoReleased and isElement(show.cargo) then
        show.cargoReleased = true
        detachElementFromRope(show.heroRope)
        setElementFrozen(show.cargo, false)
        setElementVelocity(show.cargo, 0.045, 0.015, 0.035)
        setElementAngularVelocity(show.cargo, 0.04, -0.07, 0.12)
    end
end

local function updateCamera(elapsed)
    local cx, cy, cz = show.centerX, show.centerY, show.centerZ

    if elapsed < 7000 then
        local t = elapsed / 7000
        cameraLerp(
            cx, cy - 24.0, cz + 7.0,
            cx, cy, cz + 5.5,
            cx + 3.0, cy - 21.0, cz + 6.0,
            cx, cy, cz + 5.0,
            t,
            78, 70
        )
    elseif elapsed < 15000 then
        local t = (elapsed - 7000) / 8000
        cameraLerp(
            cx + 3.0, cy - 21.0, cz + 6.0,
            cx, cy, cz + 5.0,
            cx + 15.0, cy - 14.0, cz + 8.5,
            cx, cy, cz + 4.0,
            t,
            70, 64
        )
    elseif elapsed < 24500 then
        local cargoX, cargoY, cargoZ = cx, cy, cz + 2.0
        if isElement(show.cargo) then
            local x, y, z = getElementPosition(show.cargo)
            if type(x) == "number" then
                cargoX, cargoY, cargoZ = x, y, z
            end
        end

        local t = smoothstep((elapsed - 15000) / 9500)
        cameraLerp(
            cx + 15.0, cy - 14.0, cz + 8.5,
            cx, cy, cz + 4.0,
            cargoX + 8.5, cargoY - 10.0, cargoZ + 5.0,
            cargoX, cargoY, cargoZ + 1.0,
            t,
            64, 58
        )
    elseif elapsed < 29500 then
        local cargoX, cargoY, cargoZ = cx + 2.0, cy, cz
        if isElement(show.cargo) then
            local x, y, z = getElementPosition(show.cargo)
            if type(x) == "number" then
                cargoX, cargoY, cargoZ = x, y, z
            end
        end

        setCameraMatrix(cargoX + 8.0, cargoY - 10.5, cargoZ + 5.0, cargoX, cargoY, cargoZ + 0.8, 0, 58)
    else
        local t = smoothstep((elapsed - 29500) / 3500)
        cameraLerp(
            cx + 10.0, cy - 13.0, cz + 5.5,
            cx, cy, cz + 3.0,
            cx, cy - 25.0, cz + 8.0,
            cx, cy, cz + 5.0,
            t,
            62, 76
        )
    end
end

local function renderShow()
    if not show.active then
        return
    end

    local elapsed = getTickCount() - show.startedAt
    animateRopes(elapsed)
    updateCamera(elapsed)

    if elapsed >= SHOW_DURATION and not show.finished then
        show.finished = true
        triggerServerEvent("ropeShowcase:finished", resourceRoot)
    end
end

local function stopShow()
    if show.active then
        removeEventHandler("onClientRender", root, renderShow)
    end

    show.active = false
    show.finished = false
    destroyScene()
    setCameraTarget(localPlayer)
    setPresentationUI(true)
end

addEvent("ropeShowcase:start", true)
addEventHandler("ropeShowcase:start", resourceRoot, function(centerX, centerY, centerZ, dimension)
    stopShow()

    show.centerX = centerX
    show.centerY = centerY
    show.centerZ = centerZ
    show.dimension = dimension
    show.startedAt = getTickCount()
    show.finished = false

    if not createScene() then
        outputChatBox("[ROPE SHOWCASE] Scene creation failed; run /ropetest all first.", 255, 90, 90)
        triggerServerEvent("ropeShowcase:finished", resourceRoot)
        return
    end

    show.active = true
    setPresentationUI(false)
    addEventHandler("onClientRender", root, renderShow)
end)

addEvent("ropeShowcase:stop", true)
addEventHandler("ropeShowcase:stop", resourceRoot, stopShow)

addEventHandler("onClientResourceStop", resourceRoot, stopShow)
