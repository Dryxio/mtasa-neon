local UPDATE_INTERVAL_MS = 50
local lastUpdate = setmetatable({}, {__mode = "k"})
local pointingPlayers = setmetatable({}, {__mode = "k"})

local function isFiniteNumber(value)
    return type(value) == "number" and value == value and value > -math.huge and value < math.huge
end

local function stopPointing(player)
    if not pointingPlayers[player] then
        return
    end

    pointingPlayers[player] = nil
    triggerClientEvent(root, "cameraPointing:setState", resourceRoot, player, false)
end

addEvent("cameraPointing:update", true)
addEventHandler("cameraPointing:update", resourceRoot, function(active, directionX, directionY, directionZ)
    if source ~= resourceRoot or not isElement(client) or getElementType(client) ~= "player" then
        return
    end

    if active ~= true then
        stopPointing(client)
        return
    end

    if not isFiniteNumber(directionX) or not isFiniteNumber(directionY) or not isFiniteNumber(directionZ) then
        return
    end

    local length = math.sqrt(directionX * directionX + directionY * directionY + directionZ * directionZ)
    if length < 0.5 or length > 1.5 then
        return
    end

    local now = getTickCount()
    if lastUpdate[client] and now - lastUpdate[client] < UPDATE_INTERVAL_MS then
        return
    end

    lastUpdate[client] = now
    pointingPlayers[client] = {directionX / length, directionY / length, directionZ / length}
    triggerClientEvent(root, "cameraPointing:setState", resourceRoot, client, true, directionX / length, directionY / length, directionZ / length)
end)

addEvent("cameraPointing:requestState", true)
addEventHandler("cameraPointing:requestState", resourceRoot, function()
    if source ~= resourceRoot or not isElement(client) then
        return
    end

    for player, direction in pairs(pointingPlayers) do
        if isElement(player) then
            triggerClientEvent(client, "cameraPointing:setState", resourceRoot, player, true, direction[1], direction[2], direction[3])
        end
    end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    for player in pairs(pointingPlayers) do
        if isElement(player) then
            triggerClientEvent(root, "cameraPointing:setState", resourceRoot, player, false)
        end
    end
end)
