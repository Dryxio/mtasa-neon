local SHOW_DIMENSION = 64988
local CENTER_X, CENTER_Y, CENTER_Z = 405.0, 2535.0, 22.0
local CARGO_MODEL = 2912

local state = {
    player = nil,
    saved = nil,
    cargo = nil,
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

    local player, saved, cargo = state.player, state.saved, state.cargo
    state.running = false
    clearStartTimer()

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

    if isElement(cargo) then
        destroyElement(cargo)
    end

    state.player = nil
    state.saved = nil
    state.cargo = nil
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

    local cargo = createObject(CARGO_MODEL, CENTER_X, CENTER_Y, CENTER_Z + 0.9, 0, 0, 18)
    if not isElement(cargo) then
        outputChatBox("[ROPE SHOWCASE] Failed to create cargo object.", player, 255, 90, 90)
        restore()
        return
    end

    state.cargo = cargo
    setElementInterior(cargo, 0)
    setElementDimension(cargo, SHOW_DIMENSION)
    setElementCollisionsEnabled(cargo, true)
    setElementFrozen(cargo, true)
    setObjectDynamicPhysics(cargo, true)

    local attempts = 0
    local function beginWhenAuthoritative()
        if not state.running or state.player ~= player or state.cargo ~= cargo or not isElement(player) or not isElement(cargo) then
            return
        end

        attempts = attempts + 1
        if getElementSyncer(cargo) == player then
            state.startTimer = nil
            setElementFrozen(cargo, false)
            triggerClientEvent(player, "ropeShowcase:start", resourceRoot, CENTER_X, CENTER_Y, CENTER_Z, SHOW_DIMENSION, cargo)
            return
        end

        if attempts >= 24 then
            outputChatBox("[ROPE SHOWCASE] Cargo sync ownership was not assigned; showcase aborted.", player, 255, 90, 90)
            restore()
            return
        end

        state.startTimer = setTimer(beginWhenAuthoritative, 250, 1)
    end

    state.startTimer = setTimer(beginWhenAuthoritative, 750, 1)
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
        clearStartTimer()
        if isElement(state.cargo) then
            destroyElement(state.cargo)
        end
        state.running = false
        state.player = nil
        state.saved = nil
        state.cargo = nil
    end
end)

addEventHandler("onResourceStop", resourceRoot, restore)
