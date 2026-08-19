local BALL_MODEL = 2114
local MAX_BALLS_PER_PLAYER = 150

local SHOT_SPEED_MIN = 0.10
local SHOT_SPEED_MAX = 0.26
local SHOT_LIFT_MIN = 0.025
local SHOT_LIFT_MAX = 0.085
local SHOT_SPIN_MIN = 0.08
local SHOT_SPIN_MAX = 0.18

local readyPlayers = {}
local playerBalls = {}

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

local function getBallList(player)
    local list = playerBalls[player]
    if not list then
        list = {}
        playerBalls[player] = list
    end
    return list
end

local function pruneBallList(player)
    local list = getBallList(player)
    local compact = {}
    for _, object in ipairs(list) do
        if isElement(object) then
            compact[#compact + 1] = object
        end
    end
    playerBalls[player] = compact
    return compact
end

local function clearPlayerBalls(player)
    local list = playerBalls[player]
    if not list then
        return 0
    end

    local count = 0
    for _, object in ipairs(list) do
        if isElement(object) then
            destroyElement(object)
            count = count + 1
        end
    end
    playerBalls[player] = {}
    return count
end

local function createThrownBall(player, dirX, dirY, dirZ, power)
    if not readyPlayers[player] then
        return false
    end

    dirX, dirY, dirZ, power = finiteNumber(dirX), finiteNumber(dirY), finiteNumber(dirZ), finiteNumber(power)
    if not dirX or not dirY or not dirZ or not power then
        return false
    end

    local length = math.sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ)
    if length < 0.5 or length > 1.5 then
        return false
    end

    dirX, dirY, dirZ = dirX / length, dirY / length, dirZ / length
    power = clamp(power, 0, 1)

    local horizontalLength = math.sqrt(dirX * dirX + dirY * dirY)
    if horizontalLength < 0.08 then
        return false
    end

    local horizontalX, horizontalY = dirX / horizontalLength, dirY / horizontalLength
    local playerX, playerY, playerZ = getElementPosition(player)
    local releaseX = playerX + horizontalX * 0.72
    local releaseY = playerY + horizontalY * 0.72
    local releaseZ = playerZ + 1.25

    local object = createObject(BALL_MODEL, releaseX, releaseY, releaseZ)
    if not object then
        return false
    end

    setElementInterior(object, getElementInterior(player))
    setElementDimension(object, getElementDimension(player))
    setElementData(object, "dynamicPhysicsShowcase", true)
    setElementData(object, "dynamicPhysicsShowcase:owner", player)
    setElementCollisionsEnabled(object, true)
    setElementFrozen(object, false)

    if not setObjectDynamicPhysics(object, true) then
        destroyElement(object)
        return false
    end

    local speed = SHOT_SPEED_MIN + (SHOT_SPEED_MAX - SHOT_SPEED_MIN) * power
    local lift = SHOT_LIFT_MIN + (SHOT_LIFT_MAX - SHOT_LIFT_MIN) * power
    local spin = SHOT_SPIN_MIN + (SHOT_SPIN_MAX - SHOT_SPIN_MIN) * power

    setElementVelocity(object, dirX * speed, dirY * speed, dirZ * speed + lift)
    setElementAngularVelocity(object, -horizontalY * spin, horizontalX * spin, 0)

    local list = pruneBallList(player)
    list[#list + 1] = object
    while #list > MAX_BALLS_PER_PLAYER do
        local oldest = table.remove(list, 1)
        if isElement(oldest) then
            destroyElement(oldest)
        end
    end

    return true
end

addEvent("dynamicPhysicsShowcase:clientReady", true)
addEventHandler("dynamicPhysicsShowcase:clientReady", resourceRoot, function()
    if client and isElement(client) then
        readyPlayers[client] = true
    end
end)

addEvent("dynamicPhysicsShowcase:throw", true)
addEventHandler("dynamicPhysicsShowcase:throw", resourceRoot, function(dirX, dirY, dirZ, power)
    if client and isElement(client) then
        createThrownBall(client, dirX, dirY, dirZ, power)
    end
end)

addEvent("dynamicPhysicsShowcase:clear", true)
addEventHandler("dynamicPhysicsShowcase:clear", resourceRoot, function()
    if client and isElement(client) then
        local count = clearPlayerBalls(client)
        outputChatBox("[physics-showcase] cleared " .. count .. " balls", client, 170, 220, 255)
    end
end)

addCommandHandler("showcaseclearall", function(player)
    local count = 0
    for owner in pairs(playerBalls) do
        count = count + clearPlayerBalls(owner)
    end
    outputChatBox("[physics-showcase] cleared " .. count .. " balls", player, 170, 220, 255)
end)

addEventHandler("onPlayerQuit", root, function()
    clearPlayerBalls(source)
    playerBalls[source] = nil
    readyPlayers[source] = nil
end)

addEventHandler("onResourceStop", resourceRoot, function()
    for player in pairs(playerBalls) do
        clearPlayerBalls(player)
    end
    playerBalls = {}
    readyPlayers = {}
end)
