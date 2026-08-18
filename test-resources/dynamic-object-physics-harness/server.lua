local BALL_MODEL = 2114

local playerBall = {}
local balls = {}

local function message(player, text, r, g, b)
    outputChatBox("[dynamic-physics] " .. text, player, r or 190, g or 225, b or 255)
end

local function clamp(value, minimum, maximum)
    return math.max(minimum, math.min(maximum, value))
end

local function getForward(player)
    local _, _, heading = getElementRotation(player)
    local radians = math.rad(heading)
    return -math.sin(radians), math.cos(radians), heading
end

local function forgetBall(object)
    if not isElement(object) then
        return
    end

    for player, ball in pairs(playerBall) do
        if ball == object then
            playerBall[player] = nil
        end
    end
    balls[object] = nil
end

local function destroyBall(object)
    if isElement(object) then
        forgetBall(object)
        destroyElement(object)
    end
end

local function createHarnessBall(player)
    destroyBall(playerBall[player])

    local x, y, z = getElementPosition(player)
    local forwardX, forwardY, heading = getForward(player)
    local object = createObject(BALL_MODEL, x + forwardX * 1.5, y + forwardY * 1.5, z + 1.5, 0, 0, heading)
    if not object then
        message(player, "createObject failed for model " .. BALL_MODEL, 255, 100, 100)
        return false
    end

    setElementInterior(object, getElementInterior(player))
    setElementDimension(object, getElementDimension(player))
    setElementData(object, "dynamicPhysicsHarness", true)
    setElementData(object, "dynamicPhysicsHarness:owner", player)
    setElementData(object, "dynamicPhysicsHarness:enabled", true)
    setElementCollisionsEnabled(object, true)
    setElementFrozen(object, false)

    if not setObjectDynamicPhysics(object, true) then
        destroyElement(object)
        message(player, "setObjectDynamicPhysics failed", 255, 100, 100)
        return false
    end

    setElementVelocity(object, 0, 0, 0)
    setElementAngularVelocity(object, 0, 0, 0)

    playerBall[player] = object
    balls[object] = true
    addEventHandler("onElementDestroy", object, function()
        forgetBall(source)
    end)

    message(player, "Ball spawned. It should fall, bounce and settle using native object physics.")
    return object
end

local function getOrCreateBall(player)
    local object = playerBall[player]
    if isElement(object) then
        return object
    end
    return createHarnessBall(player)
end

addCommandHandler("dophys", function(player)
    if isElement(player) then
        createHarnessBall(player)
    end
end)

addCommandHandler("dothrow", function(player, _, speedArgument, upArgument)
    if not isElement(player) then
        return
    end

    local object = getOrCreateBall(player)
    if not isElement(object) then
        return
    end

    local speed = clamp(tonumber(speedArgument) or 0.18, 0.02, 0.5)
    local up = clamp(tonumber(upArgument) or 0.12, -0.2, 0.4)
    local x, y, z = getElementPosition(player)
    local forwardX, forwardY = getForward(player)

    setElementInterior(object, getElementInterior(player))
    setElementDimension(object, getElementDimension(player))
    setElementPosition(object, x + forwardX * 1.25, y + forwardY * 1.25, z + 1.15)
    setElementFrozen(object, false)
    setElementCollisionsEnabled(object, true)
    setObjectDynamicPhysics(object, true)
    setElementVelocity(object, forwardX * speed, forwardY * speed, up)
    setElementAngularVelocity(object, -forwardY * 0.12, forwardX * 0.12, 0.05)

    message(player, ("Throw speed=%.3f up=%.3f"):format(speed, up))
end)

addCommandHandler("dofreeze", function(player)
    local object = isElement(player) and playerBall[player] or nil
    if not isElement(object) then
        return
    end

    local frozen = not isElementFrozen(object)
    setElementFrozen(object, frozen)
    message(player, "Frozen=" .. tostring(frozen))
end)

addCommandHandler("dostatus", function(player)
    local object = isElement(player) and playerBall[player] or nil
    if not isElement(object) then
        message(player, "No harness ball; use /dophys first.", 255, 180, 120)
        return
    end

    local x, y, z = getElementPosition(object)
    local vx, vy, vz = getElementVelocity(object)
    local avx, avy, avz = getElementAngularVelocity(object)
    message(player, ("dynamic=%s frozen=%s pos=(%.2f %.2f %.2f)"):format(tostring(isObjectDynamicPhysics(object)), tostring(isElementFrozen(object)), x, y, z))
    message(player, ("vel=(%.4f %.4f %.4f) angular=(%.4f %.4f %.4f)"):format(vx, vy, vz, avx, avy, avz))
end)

addCommandHandler("doreset", function(player)
    if isElement(player) then
        createHarnessBall(player)
    end
end)

addCommandHandler("doclear", function(player)
    local count = 0
    for object in pairs(balls) do
        if isElement(object) then
            destroyElement(object)
            count = count + 1
        end
    end
    balls = {}
    playerBall = {}
    if isElement(player) then
        message(player, "Cleared " .. count .. " harness objects.")
    end
end)

addEventHandler("onPlayerQuit", root, function()
    destroyBall(playerBall[source])
    playerBall[source] = nil
end)

addEventHandler("onResourceStop", resourceRoot, function()
    for object in pairs(balls) do
        if isElement(object) then
            destroyElement(object)
        end
    end
end)