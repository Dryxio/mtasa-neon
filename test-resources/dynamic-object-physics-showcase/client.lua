local BALL_MODEL = 2114
local BALL_RADIUS = 0.12
local BALL_MASS = 0.62
local BALL_TURN_MASS = 0.4 * BALL_MASS * BALL_RADIUS * BALL_RADIUS
local BALL_AIR_RESISTANCE = 0.995
local BALL_ELASTICITY = 0.72

local CHARGE_DURATION_MS = 1000
local RETICLE_MAX_RADIUS = 38
local RETICLE_MIN_RADIUS = 9
local RETICLE_SEGMENTS = 40
local RETICLE_X_RATIO = 0.61
local RETICLE_Y_RATIO = 0.46

local TARGET_HIT_MS = 650
local MARKER_RADIUS = 0.80
local RING_RADIUS = 1.00
local RING_SEGMENTS = 48
local TARGET_ORANGE = { 255, 145, 40 }
local TARGET_GREEN = { 80, 235, 120 }

local ballCol = false
local aiming = false
local charging = false
local chargeStart = 0
local markerTarget = false
local ringTarget = false
local previousBallPositions = {}
local screenW, screenH = guiGetScreenSize()

local function clamp(value, minimum, maximum)
    return math.max(minimum, math.min(maximum, value))
end

local function smoothstep(value)
    value = clamp(value, 0, 1)
    return value * value * (3 - 2 * value)
end

local function getChargePower()
    if not charging then
        return 0
    end
    return smoothstep((getTickCount() - chargeStart) / CHARGE_DURATION_MS)
end

local function configureBall(object)
    if not isElement(object) or getElementType(object) ~= "object" or not getElementData(object, "dynamicPhysicsShowcase") then
        return
    end

    setObjectProperty(object, "mass", BALL_MASS)
    setObjectProperty(object, "turn_mass", BALL_TURN_MASS)
    setObjectProperty(object, "air_resistance", BALL_AIR_RESISTANCE)
    setObjectProperty(object, "elasticity", BALL_ELASTICITY)
end

local function installCollision()
    ballCol = engineLoadCOL({
        spheres = {
            {
                position = { 0, 0, 0 },
                radius = BALL_RADIUS,
                material = 1
            }
        }
    })

    if not isElement(ballCol) then
        outputDebugString("[physics-showcase] engineLoadCOL(table) failed", 1)
        return false
    end

    if not engineReplaceCOL(ballCol, BALL_MODEL) then
        outputDebugString("[physics-showcase] engineReplaceCOL failed for basketball model", 1)
        destroyElement(ballCol)
        ballCol = false
        return false
    end

    return true
end

local function getReticlePosition()
    return screenW * RETICLE_X_RATIO, screenH * RETICLE_Y_RATIO
end

local function getAimDirection()
    local reticleX, reticleY = getReticlePosition()
    local targetX, targetY, targetZ = getWorldFromScreenPosition(reticleX, reticleY, 100)
    if not targetX then
        return false
    end

    local playerX, playerY, playerZ = getElementPosition(localPlayer)
    local releaseZ = playerZ + 1.25
    local dx, dy, dz = targetX - playerX, targetY - playerY, targetZ - releaseZ
    local length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length < 0.001 then
        return false
    end

    dx, dy, dz = dx / length, dy / length, dz / length
    local horizontalLength = math.sqrt(dx * dx + dy * dy)
    if horizontalLength < 0.08 then
        return false
    end

    return dx, dy, clamp(dz, -0.75, 0.85)
end

local function getReticleWorldPoint(distance)
    distance = clamp(tonumber(distance) or 10, 2, 60)
    local reticleX, reticleY = getReticlePosition()
    local x, y, z = getWorldFromScreenPosition(reticleX, reticleY, distance)
    if not x then
        return false
    end
    return x, y, z, distance
end

local function throwBall(power)
    local dirX, dirY, dirZ = getAimDirection()
    if not dirX then
        return
    end

    triggerServerEvent("dynamicPhysicsShowcase:throw", resourceRoot, dirX, dirY, dirZ, clamp(power, 0, 1))
end

local function drawCircle(centerX, centerY, radius, color)
    local previousX, previousY
    local firstX, firstY
    for segment = 0, RETICLE_SEGMENTS do
        local angle = math.pi * 2 * segment / RETICLE_SEGMENTS
        local x = centerX + math.cos(angle) * radius
        local y = centerY + math.sin(angle) * radius
        if previousX then
            dxDrawLine(previousX, previousY, x, y, color, 2)
        else
            firstX, firstY = x, y
        end
        previousX, previousY = x, y
    end

    if previousX and firstX then
        dxDrawLine(previousX, previousY, firstX, firstY, color, 2)
    end
end

local function drawReticle()
    local power = getChargePower()
    local radius = RETICLE_MAX_RADIUS - (RETICLE_MAX_RADIUS - RETICLE_MIN_RADIUS) * power
    local centerX, centerY = getReticlePosition()
    local color = tocolor(245, 245, 245, 235)

    drawCircle(centerX, centerY, radius, color)
    dxDrawRectangle(centerX - 1.5, centerY - 1.5, 3, 3, color)
end

local function destroyMarkerTarget()
    if markerTarget and isElement(markerTarget.element) then
        destroyElement(markerTarget.element)
    end
    markerTarget = false
end

local function clearTargets()
    destroyMarkerTarget()
    ringTarget = false
    previousBallPositions = {}
end

local function placeMarkerTarget(distance)
    local x, y, z, resolvedDistance = getReticleWorldPoint(distance or 8)
    if not x then
        outputChatBox("[physics-showcase] could not resolve reticle position", 255, 120, 120)
        return
    end

    destroyMarkerTarget()
    local marker = createMarker(x, y, z, "corona", MARKER_RADIUS * 2, TARGET_ORANGE[1], TARGET_ORANGE[2], TARGET_ORANGE[3], 180)
    if not isElement(marker) then
        outputChatBox("[physics-showcase] failed to create marker target", 255, 120, 120)
        return
    end

    setElementInterior(marker, getElementInterior(localPlayer))
    setElementDimension(marker, getElementDimension(localPlayer))
    markerTarget = { element = marker, x = x, y = y, z = z, hitUntil = 0 }
    outputChatBox(("[physics-showcase] marker placed %.1fm along reticle"):format(resolvedDistance), 255, 180, 90)
end

local function makeRingBasis(nx, ny, nz)
    local rx, ry, rz = ny, -nx, 0
    local rightLength = math.sqrt(rx * rx + ry * ry + rz * rz)
    if rightLength < 0.001 then
        rx, ry, rz = 1, 0, 0
    else
        rx, ry, rz = rx / rightLength, ry / rightLength, rz / rightLength
    end

    local ux = ny * rz - nz * ry
    local uy = nz * rx - nx * rz
    local uz = nx * ry - ny * rx
    local upLength = math.sqrt(ux * ux + uy * uy + uz * uz)
    if upLength < 0.001 then
        return false
    end
    return rx, ry, rz, ux / upLength, uy / upLength, uz / upLength
end

local function placeRingTarget(distance)
    local x, y, z, resolvedDistance = getReticleWorldPoint(distance or 10)
    if not x then
        outputChatBox("[physics-showcase] could not resolve reticle position", 255, 120, 120)
        return
    end

    local cameraX, cameraY, cameraZ = getCameraMatrix()
    local nx, ny, nz = cameraX - x, cameraY - y, cameraZ - z
    local normalLength = math.sqrt(nx * nx + ny * ny + nz * nz)
    if normalLength < 0.001 then
        return
    end
    nx, ny, nz = nx / normalLength, ny / normalLength, nz / normalLength

    local rx, ry, rz, ux, uy, uz = makeRingBasis(nx, ny, nz)
    if not rx then
        return
    end

    ringTarget = {
        x = x, y = y, z = z,
        nx = nx, ny = ny, nz = nz,
        rx = rx, ry = ry, rz = rz,
        ux = ux, uy = uy, uz = uz,
        hitUntil = 0
    }
    outputChatBox(("[physics-showcase] ring placed %.1fm along reticle"):format(resolvedDistance), 255, 180, 90)
end

local function segmentHitsSphere(ax, ay, az, bx, by, bz, cx, cy, cz, radius)
    local abx, aby, abz = bx - ax, by - ay, bz - az
    local acx, acy, acz = cx - ax, cy - ay, cz - az
    local lengthSquared = abx * abx + aby * aby + abz * abz
    local t = 0
    if lengthSquared > 0.000001 then
        t = clamp((acx * abx + acy * aby + acz * abz) / lengthSquared, 0, 1)
    end

    local px = ax + abx * t
    local py = ay + aby * t
    local pz = az + abz * t
    local dx, dy, dz = px - cx, py - cy, pz - cz
    return dx * dx + dy * dy + dz * dz <= radius * radius
end

local function segmentHitsRing(ax, ay, az, bx, by, bz, target)
    local adx, ady, adz = ax - target.x, ay - target.y, az - target.z
    local bdx, bdy, bdz = bx - target.x, by - target.y, bz - target.z
    local da = adx * target.nx + ady * target.ny + adz * target.nz
    local db = bdx * target.nx + bdy * target.ny + bdz * target.nz

    local denominator = da - db
    local t
    if math.abs(denominator) > 0.000001 then
        t = da / denominator
        if t < 0 or t > 1 then
            return false
        end
    elseif math.abs(db) <= BALL_RADIUS then
        t = 1
    else
        return false
    end

    local px = ax + (bx - ax) * t - target.x
    local py = ay + (by - ay) * t - target.y
    local pz = az + (bz - az) * t - target.z
    local planeDistance = px * target.nx + py * target.ny + pz * target.nz
    px, py, pz = px - planeDistance * target.nx, py - planeDistance * target.ny, pz - planeDistance * target.nz
    local radialSquared = px * px + py * py + pz * pz
    local hitRadius = RING_RADIUS - BALL_RADIUS * 0.25
    return radialSquared <= hitRadius * hitRadius
end

local function updateTargets()
    if not markerTarget and not ringTarget then
        return
    end

    local now = getTickCount()
    for _, object in ipairs(getElementsByType("object", root, true)) do
        if getElementData(object, "dynamicPhysicsShowcase") then
            local x, y, z = getElementPosition(object)
            local previous = previousBallPositions[object]
            local ax, ay, az = x, y, z
            if previous then
                ax, ay, az = previous[1], previous[2], previous[3]
            end

            if markerTarget and segmentHitsSphere(ax, ay, az, x, y, z, markerTarget.x, markerTarget.y, markerTarget.z, MARKER_RADIUS + BALL_RADIUS) then
                markerTarget.hitUntil = now + TARGET_HIT_MS
            end
            if ringTarget and segmentHitsRing(ax, ay, az, x, y, z, ringTarget) then
                ringTarget.hitUntil = now + TARGET_HIT_MS
            end

            previousBallPositions[object] = { x, y, z }
        end
    end

    if markerTarget and isElement(markerTarget.element) then
        local color = now < markerTarget.hitUntil and TARGET_GREEN or TARGET_ORANGE
        setMarkerColor(markerTarget.element, color[1], color[2], color[3], 180)
    end
end

local function drawRingTarget()
    if not ringTarget then
        return
    end

    local now = getTickCount()
    local rgb = now < ringTarget.hitUntil and TARGET_GREEN or TARGET_ORANGE
    local color = tocolor(rgb[1], rgb[2], rgb[3], 230)
    local firstX, firstY, firstZ
    local previousX, previousY, previousZ

    for segment = 0, RING_SEGMENTS do
        local angle = math.pi * 2 * segment / RING_SEGMENTS
        local c, s = math.cos(angle), math.sin(angle)
        local x = ringTarget.x + (ringTarget.rx * c + ringTarget.ux * s) * RING_RADIUS
        local y = ringTarget.y + (ringTarget.ry * c + ringTarget.uy * s) * RING_RADIUS
        local z = ringTarget.z + (ringTarget.rz * c + ringTarget.uz * s) * RING_RADIUS
        if previousX then
            dxDrawLine3D(previousX, previousY, previousZ, x, y, z, color, 4)
        else
            firstX, firstY, firstZ = x, y, z
        end
        previousX, previousY, previousZ = x, y, z
    end

    if previousX and firstX then
        dxDrawLine3D(previousX, previousY, previousZ, firstX, firstY, firstZ, color, 4)
    end
end

bindKey("a", "down", function()
    aiming = not aiming
    charging = false
end)

bindKey("e", "both", function(_, state)
    if not aiming then
        charging = false
        return
    end

    if state == "down" then
        charging = true
        chargeStart = getTickCount()
        return
    end

    if state == "up" and charging then
        local power = getChargePower()
        charging = false
        throwBall(power)
    end
end)

addCommandHandler("showcaseaim", function()
    aiming = not aiming
    charging = false
end)

addCommandHandler("showcasethrow", function(_, value)
    if not aiming then
        aiming = true
    end
    throwBall(clamp(tonumber(value) or 0.7, 0, 1))
end)

addCommandHandler("showcaseclear", function()
    triggerServerEvent("dynamicPhysicsShowcase:clear", resourceRoot)
end)

addCommandHandler("showcasemarker", function(_, distance)
    placeMarkerTarget(tonumber(distance) or 8)
end)

addCommandHandler("showcasering", function(_, distance)
    placeRingTarget(tonumber(distance) or 10)
end)

addCommandHandler("showcasetargetclear", clearTargets)

addEventHandler("onClientResourceStart", resourceRoot, function()
    local collisionReady = installCollision()
    for _, object in ipairs(getElementsByType("object", root, true)) do
        configureBall(object)
    end

    if collisionReady then
        triggerServerEvent("dynamicPhysicsShowcase:clientReady", resourceRoot)
    else
        outputChatBox("[physics-showcase] collision setup failed", 255, 100, 100)
    end

    outputChatBox("[physics-showcase] A aim | hold/release E throw | /showcasemarker [m] | /showcasering [m]", 170, 220, 255)
end)

addEventHandler("onClientElementStreamIn", root, function()
    configureBall(source)
end)

addEventHandler("onClientElementDataChange", root, function(key)
    if key == "dynamicPhysicsShowcase" then
        configureBall(source)
    end
end)

addEventHandler("onClientElementDestroy", root, function()
    previousBallPositions[source] = nil
end)

addEventHandler("onClientRender", root, function()
    updateTargets()
    drawRingTarget()
    if aiming then
        drawReticle()
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    clearTargets()
    if isElement(ballCol) then
        destroyElement(ballCol)
    end
    ballCol = false
end)
