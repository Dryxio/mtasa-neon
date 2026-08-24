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

addCommandHandler("navpet", function(player)
    destroySession(player)

    local x, y, z = getElementPosition(player)
    local _, _, rotation = getElementRotation(player)
    local spawnX, spawnY = forwardOffset(x, y, rotation, 2.5)
    local ped = createPed(270, spawnX, spawnY, z, rotation)
    if not ped then
        return outputChatBox("[native navigate] Impossible de creer le ped.", player, 255, 80, 80)
    end

    setElementInterior(ped, getElementInterior(player))
    setElementDimension(ped, getElementDimension(player))
    setElementSyncer(ped, player)
    sessions[player] = {ped = ped}

    outputChatBox("[native navigate] Ped cree. Eloigne-toi puis utilise /navpetgo [walk|run|sprint].", player, 100, 220, 130)
end)

addCommandHandler("navpetgo", function(player, _, requestedMovement)
    local session = sessions[player]
    if not session or not isElement(session.ped) then
        return outputChatBox("[native navigate] Utilise d'abord /navpet.", player, 255, 170, 80)
    end

    local movement = string.lower(requestedMovement or "walk")
    if movement ~= "walk" and movement ~= "run" and movement ~= "sprint" then
        return outputChatBox("Usage: /navpetgo [walk|run|sprint]", player, 255, 170, 80)
    end

    local x, y, z = getElementPosition(player)
    triggerClientEvent(player, "nativePedNavigateVisual:start", resourceRoot, session.ped, x, y, z, movement)
    outputChatBox(("[native navigate] Cible enregistree ici, mode %s."):format(movement), player, 120, 210, 255)
end)

addCommandHandler("navpetstop", function(player)
    local session = sessions[player]
    if session and isElement(session.ped) then
        triggerClientEvent(player, "nativePedNavigateVisual:stop", resourceRoot, session.ped)
    end
end)

addCommandHandler("navpetcleanup", function(player)
    destroySession(player)
    outputChatBox("[native navigate] Nettoye.", player, 180, 220, 255)
end)

addEvent("nativePedNavigateVisual:result", true)
addEventHandler("nativePedNavigateVisual:result", resourceRoot, function(ped, result, details)
    local player = client
    local session = sessions[player]
    if source ~= resourceRoot or not session or session.ped ~= ped then
        return
    end

    local ok = result == "arrived" or result == "accepted"
    outputChatBox(("[native navigate] %s: %s"):format(result, details or ""), player, ok and 100 or 255, ok and 230 or 150, ok and 130 or 80)
end)

addEventHandler("onPlayerQuit", root, function()
    destroySession(source)
end)

addEventHandler("onResourceStop", resourceRoot, function()
    for player in pairs(sessions) do
        destroySession(player)
    end
end)
