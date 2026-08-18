local DIMENSION = 64992
local CENTER = {404.8, 2532.5, 18.0}
local saved = {}

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

addCommandHandler("breakshow", function(player, _, action)
    if action and action:lower() == "stop" then
        stop(player)
        return
    end
    if saved[player] then return end

    local x, y, z = getElementPosition(player)
    local _, _, rz = getElementRotation(player)
    saved[player] = {
        x = x, y = y, z = z, rz = rz,
        dimension = getElementDimension(player),
        interior = getElementInterior(player),
        alpha = getElementAlpha(player),
        frozen = isElementFrozen(player),
    }

    setElementInterior(player, 0)
    setElementDimension(player, DIMENSION)
    setElementPosition(player, CENTER[1], CENTER[2] - 22, CENTER[3] + 1)
    setElementAlpha(player, 0)
    setElementFrozen(player, true)

    setTimer(function()
        if isElement(player) and saved[player] then
            triggerClientEvent(player, "breakShowcase:start", resourceRoot, CENTER[1], CENTER[2], CENTER[3], DIMENSION)
        end
    end, 650, 1)
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
