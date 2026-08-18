local BALL_MODEL = 2114
local GRAVITY_PER_FRAME = 0.008

-- Existing world hoop: model 947 (bskballhub_lax01).
-- The supplied DFF places the rim center at this local offset from the model origin.
local COURT_OBJECT_X, COURT_OBJECT_Y, COURT_OBJECT_Z = 2533.2582, -1668.019607, 16.3
local COURT_OBJECT_HEADING = 354.0
local RIM_LOCAL_X, RIM_LOCAL_Y, RIM_LOCAL_Z = 0.07342529296875, 0.5001220703125, 0.9108123779296875

local court = false
local ball = false
local holder = false
local lastShooter = false
local lastScoreTick = 0
local courtRevision = 0

local function message(target, text, r, g, b)
    outputChatBox("[basket-test] " .. text, target or root, r or 235, g or 235, b or 235)
end

local function clamp(value, minimum, maximum)
    return math.max(minimum, math.min(maximum, value))
end

local function finiteNumber(value)
    value = tonumber(value)
    if not value or value ~= value or value == math.huge or value == -math.huge then
        return false
    end
    return value
end

local function rotateLocal(x, y, heading)
    local radians = math.rad(heading)
    local cosHeading, sinHeading = math.cos(radians), math.sin(radians)
    return x * cosHeading - y * sinHeading, x * sinHeading + y * cosHeading
end

local function getForward(player)
    local _, _, heading = getElementRotation(player)
    local radians = math.rad(heading)
    return -math.sin(radians), math.cos(radians), heading
end

local function publishCourt()
    setElementData(resourceRoot, "basketPhysics:active", court ~= false)
    if not court then
        return
    end

    setElementData(resourceRoot, "basketPhysics:ringX", court.ringX)
    setElementData(resourceRoot, "basketPhysics:ringY", court.ringY)
    setElementData(resourceRoot, "basketPhysics:ringZ", court.ringZ)
    setElementData(resourceRoot, "basketPhysics:heading", court.heading)
    courtRevision = courtRevision + 1
    setElementData(resourceRoot, "basketPhysics:revision", courtRevision)
end

local function destroyBall()
    if isElement(ball) then
        destroyElement(ball)
    end
    ball = false
    holder = false
    lastShooter = false
end

local function setHolder(player)
    if not isElement(ball) or not isElement(player) then
        return false
    end

    if isElementAttached(ball) then
        detachElements(ball)
    end

    setObjectDynamicPhysics(ball, false)
    setElementVelocity(ball, 0, 0, 0)
    setElementAngularVelocity(ball, 0, 0, 0)
    setElementFrozen(ball, true)
    setElementCollisionsEnabled(ball, false)
    setElementInterior(ball, getElementInterior(player))
    setElementDimension(ball, getElementDimension(player))
    attachElements(ball, player, 0.26, 0.42, 0.72)

    holder = player
    setElementData(ball, "basketPhysics:holder", player)
    setElementData(ball, "basketPhysics:free", false)
    return true
end

local function createBallFor(player)
    destroyBall()

    local x, y, z = getElementPosition(player)
    ball = createObject(BALL_MODEL, x, y, z + 1.0)
    if not ball then
        message(player, "createObject failed for basketball model", 255, 100, 100)
        return false
    end

    setElementData(ball, "basketPhysicsTest", true)
    setElementData(ball, "basketPhysics:free", false)
    setElementInterior(ball, getElementInterior(player))
    setElementDimension(ball, getElementDimension(player))
    setHolder(player)
    return ball
end

local function resetCourt(player)
    if not isElement(player) then
        return
    end

    local rimOffsetX, rimOffsetY = rotateLocal(RIM_LOCAL_X, RIM_LOCAL_Y, COURT_OBJECT_HEADING)
    court = {
        owner = player,
        ringX = COURT_OBJECT_X + rimOffsetX,
        ringY = COURT_OBJECT_Y + rimOffsetY,
        ringZ = COURT_OBJECT_Z + RIM_LOCAL_Z,
        heading = COURT_OBJECT_HEADING,
        interior = getElementInterior(player),
        dimension = getElementDimension(player)
    }

    publishCourt()
    if createBallFor(player) then
        message(root, ("Court aligned to model 947. A aim, hold/release E to shoot, F pickup. Rim %.3f %.3f %.3f")
            :format(court.ringX, court.ringY, court.ringZ), 170, 230, 255)
    end
end

local function launchBall(player, power)
    if not court or not isElement(ball) or holder ~= player then
        return false
    end

    power = clamp(tonumber(power) or 0, 0, 1)

    if isElementAttached(ball) then
        detachElements(ball)
    end

    local px, py, pz = getElementPosition(player)
    local forwardX, forwardY = getForward(player)
    local releaseX = px + forwardX * 0.62
    local releaseY = py + forwardY * 0.62
    local releaseZ = pz + 1.25

    local dx = court.ringX - releaseX
    local dy = court.ringY - releaseY
    local dz = court.ringZ - releaseZ
    local horizontalDistance = math.sqrt(dx * dx + dy * dy)
    if horizontalDistance < 0.01 then
        return false
    end

    local horizontalSpeed = 0.105 + power * 0.055
    local frames = math.max(12, horizontalDistance / horizontalSpeed)
    local velocityX = dx / frames
    local velocityY = dy / frames
    local velocityZ = (dz + 0.5 * GRAVITY_PER_FRAME * frames * frames) / frames

    local dirX = dx / horizontalDistance
    local dirY = dy / horizontalDistance
    local spin = 0.10 + power * 0.08

    setElementPosition(ball, releaseX, releaseY, releaseZ)
    setElementInterior(ball, getElementInterior(player))
    setElementDimension(ball, getElementDimension(player))
    setElementFrozen(ball, false)
    setElementCollisionsEnabled(ball, true)
    setObjectDynamicPhysics(ball, true)
    setElementVelocity(ball, velocityX, velocityY, velocityZ)
    setElementAngularVelocity(ball, -dirY * spin, dirX * spin, 0)

    holder = false
    lastShooter = player
    setElementData(ball, "basketPhysics:holder", false)
    setElementData(ball, "basketPhysics:free", true)
    setElementData(ball, "basketPhysics:lastPower", power)

    message(player, ("Shot power %.0f%%, velocity=(%.3f %.3f %.3f)"):format(power * 100, velocityX, velocityY, velocityZ), 180, 255, 190)
    return true
end

addCommandHandler("baskettest", function(player)
    resetCourt(player)
end)

addCommandHandler("basketreset", function(player)
    if not court then
        resetCourt(player)
        return
    end

    if not isElement(ball) then
        createBallFor(player)
    else
        setHolder(player)
    end
    message(player, "Ball returned to your hands.")
end)

addEvent("basketPhysics:shoot", true)
addEventHandler("basketPhysics:shoot", resourceRoot, function(power)
    if client and isElement(client) then
        launchBall(client, power)
    end
end)

addEvent("basketPhysics:pickup", true)
addEventHandler("basketPhysics:pickup", resourceRoot, function()
    if not client or not isElement(client) or not isElement(ball) or holder then
        return
    end

    if getElementInterior(client) ~= getElementInterior(ball) or getElementDimension(client) ~= getElementDimension(ball) then
        return
    end

    local px, py, pz = getElementPosition(client)
    local bx, by, bz = getElementPosition(ball)
    local dx, dy, dz = bx - px, by - py, bz - pz
    if dx * dx + dy * dy + dz * dz > 3.24 then
        return
    end

    setHolder(client)
    message(client, "Ball picked up.", 180, 255, 190)
end)

addEvent("basketPhysics:calibrate", true)
addEventHandler("basketPhysics:calibrate", resourceRoot, function(dx, dy, dz, dHeading)
    if not client or not court or court.owner ~= client then
        return
    end

    dx, dy, dz, dHeading = finiteNumber(dx), finiteNumber(dy), finiteNumber(dz), finiteNumber(dHeading)
    if not dx or not dy or not dz or not dHeading then
        return
    end

    -- Calibration input is intentionally tiny; clamp remote requests so this
    -- debug event cannot teleport the shared target arbitrary distances.
    dx = clamp(dx, -0.10, 0.10)
    dy = clamp(dy, -0.10, 0.10)
    dz = clamp(dz, -0.10, 0.10)
    dHeading = clamp(dHeading, -5.0, 5.0)

    court.ringX = court.ringX + dx
    court.ringY = court.ringY + dy
    court.ringZ = court.ringZ + dz
    court.heading = (court.heading + dHeading) % 360.0
    publishCourt()
end)

addEvent("basketPhysics:score", true)
addEventHandler("basketPhysics:score", resourceRoot, function(scoredBall)
    if not client or not court or scoredBall ~= ball or not isElement(ball) or holder then
        return
    end

    local now = getTickCount()
    if now - lastScoreTick < 1200 then
        return
    end

    local x, y, z = getElementPosition(ball)
    local dx, dy = x - court.ringX, y - court.ringY
    if dx * dx + dy * dy > 1.0 or math.abs(z - court.ringZ) > 1.2 then
        return
    end

    lastScoreTick = now
    local scorer = isElement(lastShooter) and lastShooter or client
    local score = (getElementData(scorer, "basketPhysics:score") or 0) + 1
    setElementData(scorer, "basketPhysics:score", score)
    message(root, ("%s scored (%d)."):format(getPlayerName(scorer), score), 120, 255, 150)
end)

addEventHandler("onPlayerQuit", root, function()
    if holder == source then
        holder = false
    end
    if lastShooter == source then
        lastShooter = false
    end
    if court and court.owner == source then
        court.owner = false
    end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    destroyBall()
    court = false
end)
