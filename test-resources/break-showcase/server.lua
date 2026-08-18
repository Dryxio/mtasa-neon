local DIMENSION = 64992
local RUNWAY_START = {100.74450, 2501.11401, 16.48438, 272.30032}
local RUNWAY_END = {424.00473, 2503.03442, 16.48438, 267.65860}
local STAGING = {
    (RUNWAY_START[1] + RUNWAY_END[1]) * 0.5,
    (RUNWAY_START[2] + RUNWAY_END[2]) * 0.5,
    (RUNWAY_START[3] + RUNWAY_END[3]) * 0.5 + 1.0,
}
local SETUP_START = {
    RUNWAY_START[1] - 4.0,
    RUNWAY_START[2],
    RUNWAY_START[3] + 1.0,
}
local saved = {}

local function saveState(player)
    if saved[player] then return saved[player] end

    local x, y, z = getElementPosition(player)
    local _, _, rz = getElementRotation(player)
    saved[player] = {
        x = x, y = y, z = z, rz = rz,
        dimension = getElementDimension(player),
        interior = getElementInterior(player),
        alpha = getElementAlpha(player),
        frozen = isElementFrozen(player),
        mode = nil,
    }
    return saved[player]
end

local function restore(player)
    local state = saved[player]
    if not state or not isElement(player) then
        saved[player] = nil
        return
    end

    setElementPosition(player, state.x, state.y, state.z)
    setElementRotation(player, 0, 0, state.rz)
    setElementDimension(player, state.dimension)
    setElementInterior(player, state.interior)
    setElementAlpha(player, state.alpha)
    setElementFrozen(player, state.frozen)
    saved[player] = nil
end

local function stop(player)
    if not saved[player] then return end
    triggerClientEvent(player, "breakShowcase:stop", resourceRoot)
    restore(player)
end

local function startShow(player)
    local state = saveState(player)
    state.mode = "show"

    setElementInterior(player, 0)
    setElementDimension(player, DIMENSION)
    setElementPosition(player, STAGING[1], STAGING[2], STAGING[3])
    setElementRotation(player, 0, 0, RUNWAY_START[4])
    setElementAlpha(player, 0)
    setElementFrozen(player, true)

    -- Keep the hidden player near the middle of the runway so the complete
    -- client-side gallery is comfortably inside the normal streaming radius.
    setTimer(function()
        local current = saved[player]
        if isElement(player) and current and current.mode == "show" then
            triggerClientEvent(player, "breakShowcase:start", resourceRoot, DIMENSION)
        end
    end, 650, 1)
end

local function startSetup(player)
    local state = saveState(player)
    state.mode = "setup"

    setElementInterior(player, 0)
    setElementDimension(player, DIMENSION)
    setElementPosition(player, SETUP_START[1], SETUP_START[2], SETUP_START[3])
    setElementRotation(player, 0, 0, RUNWAY_START[4])
    setElementAlpha(player, 255)
    setElementFrozen(player, false)

    setTimer(function()
        local current = saved[player]
        if isElement(player) and current and current.mode == "setup" then
            triggerClientEvent(player, "breakShowcase:calibrate", resourceRoot, DIMENSION)
        end
    end, 650, 1)
end

addCommandHandler("breakshow", function(player, _, action)
    if action and action:lower() == "stop" then
        stop(player)
        return
    end

    if saved[player] and saved[player].mode == "show" then return end
    startShow(player)
end)

addCommandHandler("breaksetup", function(player, _, action)
    if action and action:lower() == "stop" then
        stop(player)
        return
    end

    if saved[player] and saved[player].mode == "show" then return end
    startSetup(player)
end)

addEvent("breakShowcase:finished", true)
addEventHandler("breakShowcase:finished", resourceRoot, function()
    local player = client
    if player and saved[player] then restore(player) end
end)

addEventHandler("onPlayerQuit", root, function() saved[source] = nil end)
addEventHandler("onResourceStop", resourceRoot, function()
    for player in pairs(saved) do
        if isElement(player) then restore(player) end
    end
end)
