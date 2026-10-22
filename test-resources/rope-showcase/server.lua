local SHOW_DIMENSION = 64988
local RUNWAY_START_X, RUNWAY_START_Y, RUNWAY_Z = 100.74450, 2501.11401, 16.48438
local RUNWAY_END_X, RUNWAY_END_Y = 424.00473, 2503.03442
local CENTER_X = (RUNWAY_START_X + RUNWAY_END_X) * 0.5
local CENTER_Y = (RUNWAY_START_Y + RUNWAY_END_Y) * 0.5

local state = {
    player = nil,
    saved = nil,
    startTimer = nil,
    running = false,
}

local function clearStartTimer()
    if isTimer(state.startTimer) then
        killTimer(state.startTimer)
    end
    state.startTimer = nil
end

local function restore()
    if not state.running then
        return
    end

    local player, saved = state.player, state.saved
    state.running = false
    clearStartTimer()

    if isElement(player) then
        fadeCamera(player, false, 0.2)
        setTimer(function()
            if not isElement(player) then
                return
            end

            triggerClientEvent(player, "ropeShowcase:stop", resourceRoot)
            if saved then
                setElementInterior(player, saved.interior)
                setElementDimension(player, saved.dimension)
                setElementPosition(player, saved.x, saved.y, saved.z)
                setElementRotation(player, saved.rx, saved.ry, saved.rz)
                setElementAlpha(player, saved.alpha)
                setElementFrozen(player, saved.frozen)
            end
            fadeCamera(player, true, 0.45)
        end, 220, 1)
    end

    state.player = nil
    state.saved = nil
end

local function start(player, mode)
    if state.running then
        outputChatBox("[ROPE SHOWCASE] Another showcase is already running.", player, 255, 90, 90)
        return
    end

    local x, y, z = getElementPosition(player)
    local rx, ry, rz = getElementRotation(player)

    state.player = player
    state.saved = {
        x = x,
        y = y,
        z = z,
        rx = rx,
        ry = ry,
        rz = rz,
        interior = getElementInterior(player),
        dimension = getElementDimension(player),
        alpha = getElementAlpha(player),
        frozen = isElementFrozen(player),
    }
    state.running = true

    fadeCamera(player, false, 0.25)
    setElementInterior(player, 0)
    setElementDimension(player, SHOW_DIMENSION)
    setElementPosition(player, CENTER_X, CENTER_Y - 55.0, RUNWAY_Z + 1.0)
    setElementRotation(player, 0, 0, 0)
    setElementAlpha(player, 0)
    setElementFrozen(player, true)

    state.startTimer = setTimer(function()
        if state.running and state.player == player and isElement(player) then
            state.startTimer = nil
            triggerClientEvent(player, "ropeShowcase:start", resourceRoot, CENTER_X, CENTER_Y, RUNWAY_Z, SHOW_DIMENSION, mode)
        end
    end, 350, 1)
end

addCommandHandler("ropeshow", function(player, _, action)
    if not isElement(player) then
        return
    end

    action = (action or "start"):lower()
    if action == "stop" or action == "reset" then
        if state.player == player then
            restore()
        end
        return
    end

    if action ~= "start" and action ~= "interactive" then
        outputChatBox("[ROPE SHOWCASE] Usage: /ropeshow [start|stop|interactive]", player, 255, 200, 80)
        return
    end

    start(player, action == "interactive" and "interactive" or "auto")
end)

addEvent("ropeShowcase:ready", true)
addEventHandler("ropeShowcase:ready", resourceRoot, function()
    if client and client == state.player and state.running then
        fadeCamera(client, true, 0.8)
    end
end)

addEvent("ropeShowcase:finished", true)
addEventHandler("ropeShowcase:finished", resourceRoot, function()
    if client and client == state.player then
        restore()
    end
end)

addEventHandler("onPlayerQuit", root, function()
    if source == state.player then
        clearStartTimer()
        state.running = false
        state.player = nil
        state.saved = nil
    end
end)

addEventHandler("onResourceStop", resourceRoot, restore)
