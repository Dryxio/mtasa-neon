local sessions = {}

local function destroySession(player)
    local session = sessions[player]
    if not session then
        return
    end

    if isElement(session.ped) then
        destroyElement(session.ped)
    end
    sessions[player] = nil
end

local function forwardOffset(x, y, rotation, distance)
    local radians = math.rad(rotation)
    return x - math.sin(radians) * distance, y + math.cos(radians) * distance
end

addCommandHandler("nativegoto", function(player, _, requestedMovement)
    if not isElement(player) then
        return
    end

    local movement = string.lower(requestedMovement or "walk")
    if movement ~= "walk" and movement ~= "run" and movement ~= "sprint" then
        outputChatBox("Usage: /nativegoto [walk|run|sprint]", player, 255, 170, 80)
        return
    end

    destroySession(player)

    local playerX, playerY, playerZ = getElementPosition(player)
    local _, _, playerRotation = getElementRotation(player)
    local startX, startY = forwardOffset(playerX, playerY, playerRotation, 2.5)
    local targetX, targetY = forwardOffset(startX, startY, playerRotation, 10.0)
    local ped = createPed(270, startX, startY, playerZ, playerRotation)
    if not ped then
        outputChatBox("[native go-to] Impossible de creer Sweet.", player, 255, 80, 80)
        return
    end

    setElementInterior(ped, getElementInterior(player))
    setElementDimension(ped, getElementDimension(player))
    setElementSyncer(ped, player)

    sessions[player] = {
        ped = ped,
        target = {targetX, targetY, playerZ},
        movement = movement,
    }

    outputChatBox(("[native go-to] Sweet doit parcourir 10 m en mode %s."):format(movement), player, 100, 220, 130)
    outputChatBox("Commandes: /nativegotocancel et /nativegotocleanup", player, 210, 210, 210)

    -- Give streaming and ped-sync ownership one frame to settle before asking the
    -- selected syncer to construct the native GTA task.
    setTimer(function(targetPlayer, targetPed, x, y, z, moveState)
        local session = sessions[targetPlayer]
        if not session or session.ped ~= targetPed or not isElement(targetPed) then
            return
        end
        triggerClientEvent(targetPlayer, "nativePedGoTo:start", resourceRoot, targetPed, x, y, z, moveState, 20000)
    end, 500, 1, player, ped, targetX, targetY, playerZ, movement)
end)

addCommandHandler("nativegotocancel", function(player)
    local session = sessions[player]
    if session and isElement(session.ped) then
        triggerClientEvent(player, "nativePedGoTo:cancel", resourceRoot, session.ped)
    else
        outputChatBox("[native go-to] Aucun test actif.", player, 255, 170, 80)
    end
end)

addCommandHandler("nativegotocleanup", function(player)
    destroySession(player)
    outputChatBox("[native go-to] Test nettoye.", player, 180, 220, 255)
end)

addEvent("nativePedGoTo:result", true)
addEventHandler("nativePedGoTo:result", resourceRoot, function(ped, result, details)
    local player = client
    local session = sessions[player]
    if not session or source ~= resourceRoot or session.ped ~= ped then
        return
    end

    local colors = {
        arrived = {100, 230, 130},
        timeout_relocated = {255, 190, 80},
        cancelled = {255, 190, 80},
        refused = {255, 80, 80},
        ended_outside_radius = {255, 190, 80},
        destroyed = {255, 80, 80},
    }
    local color = colors[result] or {220, 220, 220}
    outputChatBox(("[native go-to] %s: %s"):format(result, details or ""), player, unpack(color))
end)

addEventHandler("onPlayerQuit", root, function()
    destroySession(source)
end)

addEventHandler("onResourceStop", resourceRoot, function()
    for player in pairs(sessions) do
        destroySession(player)
    end
end)
