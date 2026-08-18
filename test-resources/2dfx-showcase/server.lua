local SHOW_DIMENSION = 64991
local CENTER_X, CENTER_Y, CENTER_Z = 405.0, 2535.0, 16.4

local active = {}

local function chat(player, message, r, g, b)
    if isElement(player) then
        outputChatBox("[2DFX SHOWCASE] " .. message, player, r or 100, g or 220, b or 255)
    end
end

local function rememberPlayer(player)
    local x, y, z = getElementPosition(player)
    local rx, ry, rz = getElementRotation(player)
    return {
        x = x, y = y, z = z,
        rx = rx, ry = ry, rz = rz,
        dimension = getElementDimension(player),
        interior = getElementInterior(player),
        alpha = getElementAlpha(player),
        frozen = isElementFrozen(player),
    }
end

local function restorePlayer(player, saved)
    if not isElement(player) or not saved then
        return
    end
    setElementInterior(player, saved.interior)
    setElementDimension(player, saved.dimension)
    setElementPosition(player, saved.x, saved.y, saved.z)
    setElementRotation(player, saved.rx, saved.ry, saved.rz)
    setElementAlpha(player, saved.alpha)
    setElementFrozen(player, saved.frozen)
end

local function finishShow(player, instant)
    local entry = active[player]
    if not entry then
        return
    end

    active[player] = nil
    entry.serial = entry.serial + 1

    if not isElement(player) then
        return
    end

    local function restoreNow()
        if isElement(player) then
            triggerClientEvent(player, "2dfxShowcase:stop", resourceRoot)
            restorePlayer(player, entry.saved)
            fadeCamera(player, true, 0.45)
        end
    end

    if instant then
        restoreNow()
    else
        fadeCamera(player, false, 0.25)
        setTimer(restoreNow, 300, 1)
    end
end

local function startShow(player, mode, shot)
    if active[player] then
        finishShow(player, true)
    end

    local serial = getTickCount()
    active[player] = {
        saved = rememberPlayer(player),
        serial = serial,
        mode = mode,
        shot = shot,
    }

    fadeCamera(player, false, 0.25)
    setElementInterior(player, 0)
    setElementDimension(player, SHOW_DIMENSION)
    setElementPosition(player, CENTER_X, CENTER_Y - 34.0, CENTER_Z + 1.0)
    setElementRotation(player, 0, 0, 0)
    setElementAlpha(player, 0)
    setElementFrozen(player, true)

    setTimer(function()
        local entry = active[player]
        if not entry or entry.serial ~= serial or not isElement(player) then
            return
        end
        triggerClientEvent(
            player,
            "2dfxShowcase:prepare",
            resourceRoot,
            CENTER_X, CENTER_Y, CENTER_Z,
            SHOW_DIMENSION, mode, shot
        )
    end, 350, 1)
end

addCommandHandler("2dfxshow", function(player, _, first, second)
    local mode = first or "full"
    local shot = nil

    if mode == "stop" then
        finishShow(player, true)
        return
    end

    if mode == "shot" then
        shot = math.floor(tonumber(second) or 0)
        if shot < 1 or shot > 7 then
            chat(player, "Usage: /2dfxshow shot 1-7", 255, 120, 120)
            return
        end
    elseif mode ~= "full" and mode ~= "setup" and mode ~= "final" then
        chat(player, "Usage: /2dfxshow [full|setup|final|shot 1-7|stop]", 255, 120, 120)
        return
    end

    startShow(player, mode, shot)
end)

addEvent("2dfxShowcase:ready", true)
addEventHandler("2dfxShowcase:ready", resourceRoot, function()
    local player = client
    local entry = active[player]
    if not entry then
        return
    end

    fadeCamera(player, true, 0.8)
    setTimer(function()
        local current = active[player]
        if current ~= entry or not isElement(player) then
            return
        end
        triggerClientEvent(player, "2dfxShowcase:begin", resourceRoot, entry.mode, entry.shot)
    end, 250, 1)
end)

addEvent("2dfxShowcase:failed", true)
addEventHandler("2dfxShowcase:failed", resourceRoot, function(reason)
    local player = client
    chat(player, "Setup failed: " .. tostring(reason), 255, 100, 100)
    finishShow(player, true)
end)

addEvent("2dfxShowcase:finished", true)
addEventHandler("2dfxShowcase:finished", resourceRoot, function()
    finishShow(client, false)
end)

addEventHandler("onPlayerQuit", root, function()
    active[source] = nil
end)

addEventHandler("onResourceStop", resourceRoot, function()
    for player, entry in pairs(active) do
        if isElement(player) then
            restorePlayer(player, entry.saved)
            fadeCamera(player, true, 0)
        end
    end
    active = {}
end)
