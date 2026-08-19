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

local ballCol = false
local aiming = false
local charging = false
local chargeStart = 0
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

    outputChatBox("[physics-showcase] A aim | hold/release E throw | /showcaseclear", 170, 220, 255)
end)

addEventHandler("onClientElementStreamIn", root, function()
    configureBall(source)
end)

addEventHandler("onClientElementDataChange", root, function(key)
    if key == "dynamicPhysicsShowcase" then
        configureBall(source)
    end
end)

addEventHandler("onClientRender", root, function()
    if aiming then
        drawReticle()
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    if isElement(ballCol) then
        destroyElement(ballCol)
    end
    ballCol = false
end)
