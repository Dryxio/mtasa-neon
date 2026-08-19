local SHOW_DIMENSION = 64988
local CENTER_X, CENTER_Y, CENTER_Z = 405.0, 2535.0, 22.0

local state = {
    player = nil,
    saved = nil,
    running = false,
}

local function restore()
    if not state.running then
        return
    end

    local player, saved = state.player, state.saved
    state.running = false

    if isElement(player) then
        triggerClientEvent(player, "ropeShowcase:stop", resourceRoot)
        if saved then
            setElementInterior(player, saved.interior)
            setElementDimension(player, saved.dimension)
            setElementPosition(player, saved.x, saved.y, saved.z)
            setElementRotation(player, saved.rx, saved.ry, saved.rz)
            setElementAlpha(player, saved.alpha)
            setElementFrozen(player, saved.frozen)
        end
    end

    state.player = nil
    state.saved = nil
end

local function start(player)
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

    setElementInterior(player, 0)
    setElementDimension(player, SHOW_DIMENSION)
    setElementPosition(player, CENTER_X, CENTER_Y - 40.0, 16.8)
    setElementAlpha(player, 0)
    setElementFrozen(player, true)

    setTimer(function()
        if state.running and state.player == player and isElement(player) then
            triggerClientEvent(player, "ropeShowcase:start", resourceRoot, CENTER_X, CENTER_Y, CENTER_Z, SHOW_DIMENSION)
        end
    end, 600, 1)
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

    if action ~= "start" then
        outputChatBox("[ROPE SHOWCASE] Usage: /ropeshow [start|stop]", player, 255, 200, 80)
        return
    end

    start(player)
end)

addEvent("ropeShowcase:finished", true)
addEventHandler("ropeShowcase:finished", resourceRoot, function()
    if client and client == state.player then
        restore()
    end
end)

addEventHandler("onPlayerQuit", root, function()
    if source == state.player then
        state.running = false
        state.player = nil
        state.saved = nil
    end
end)

addEventHandler("onResourceStop", resourceRoot, restore)
