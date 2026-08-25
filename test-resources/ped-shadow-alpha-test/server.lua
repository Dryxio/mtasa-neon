local PED_MODEL = 7
local testPeds = {}

local function message(player, text, r, g, b)
    outputChatBox("[ped-shadow] " .. text, player, r or 190, g or 225, b or 255)
end

local function destroyTestPed(player)
    local ped = testPeds[player]
    if isElement(ped) then
        destroyElement(ped)
    end
    testPeds[player] = nil
end

local function positionInFront(player)
    local x, y, z = getElementPosition(player)
    local _, _, heading = getElementRotation(player)
    local radians = math.rad(heading)
    return x - math.sin(radians) * 3, y + math.cos(radians) * 3, z + 0.15, heading + 180
end

local function getOrCreateTestPed(player)
    local ped = testPeds[player]
    local x, y, z, heading = positionInFront(player)

    if not isElement(ped) then
        ped = createPed(PED_MODEL, x, y, z, heading)
        if not isElement(ped) then
            message(player, "Failed to create the test ped.", 255, 100, 100)
            return nil
        end

        testPeds[player] = ped
        setElementFrozen(ped, true)
        setElementCollisionsEnabled(ped, false)
    else
        setElementPosition(ped, x, y, z)
        setElementRotation(ped, 0, 0, heading)
    end

    setElementInterior(ped, getElementInterior(player))
    setElementDimension(ped, getElementDimension(player))
    return ped
end

addCommandHandler("pedshadow", function(player, _, action)
    if not isElement(player) then
        return
    end

    action = action and action:lower() or "spawn"
    if action == "destroy" then
        destroyTestPed(player)
        message(player, "Test ped destroyed.")
        return
    end

    local ped = getOrCreateTestPed(player)
    if not ped then
        return
    end

    if action == "spawn" or action == "move" then
        message(player, ("Ped ready; alpha=%d. Use /pedshadow 0, 1, 255 or toggle."):format(getElementAlpha(ped)))
        return
    end

    local alpha = tonumber(action)
    if action == "toggle" then
        alpha = getElementAlpha(ped) == 0 and 255 or 0
    end

    if not alpha or alpha < 0 or alpha > 255 then
        message(player, "Usage: /pedshadow [0-255|toggle|move|destroy]", 255, 180, 120)
        return
    end

    alpha = math.floor(alpha)
    setElementAlpha(ped, alpha)
    message(player, ("Alpha set to %d."):format(alpha))
end)

addEventHandler("onPlayerQuit", root, function()
    destroyTestPed(source)
end)

addEventHandler("onResourceStop", resourceRoot, function()
    for player in pairs(testPeds) do
        destroyTestPed(player)
    end
end)
