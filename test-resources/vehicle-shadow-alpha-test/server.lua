-- Keep the last tested vehicle so the same shadow can be compared after exiting.
local lastVehicles = {}
local originalAlpha = {}

local function message(player, text)
    outputChatBox("[Shadow test] " .. text, player, 130, 220, 255)
end

local function changeAlpha(player, element, value, label)
    local alpha = tonumber(value)
    if not alpha or alpha < 0 or alpha > 255 or alpha ~= math.floor(alpha) then
        message(player, "Alpha attendu : un entier de 0 a 255.")
        return
    end
    if originalAlpha[element] == nil then
        originalAlpha[element] = getElementAlpha(element)
    end
    setElementAlpha(element, alpha)
    message(player, label .. " : alpha " .. alpha)
end

addCommandHandler("vshadow", function(player, _, value)
    if not isElement(player) then return end
    local vehicle = getPedOccupiedVehicle(player)
    if vehicle then
        lastVehicles[player] = vehicle
    else
        vehicle = lastVehicles[player]
    end
    if not isElement(vehicle) then
        message(player, "Monte dans un vehicule puis utilise /vshadow 0 ou /vshadow 255.")
        return
    end
    changeAlpha(player, vehicle, value, "Vehicule (reste selectionne apres ta sortie)")
end)

addCommandHandler("pshadow", function(player, _, value)
    if not isElement(player) then return end
    changeAlpha(player, player, value, "Joueur")
end)

local function restore(element)
    if isElement(element) and originalAlpha[element] ~= nil then
        setElementAlpha(element, originalAlpha[element])
    end
    originalAlpha[element] = nil
end

addCommandHandler("shadowreset", function(player)
    if not isElement(player) then return end
    restore(player)
    local vehicle = lastVehicles[player]
    if vehicle then restore(vehicle) end
    lastVehicles[player] = nil
    message(player, "Alpha initial restaure pour toi et ton dernier vehicule teste.")
end)

-- Restore the pre-test state when stopping, so an invisible test vehicle is not left behind.
addEventHandler("onResourceStop", resourceRoot, function()
    for element, alpha in pairs(originalAlpha) do
        if isElement(element) then setElementAlpha(element, alpha) end
    end
end)

addEventHandler("onElementDestroy", root, function()
    originalAlpha[source] = nil
    for player, vehicle in pairs(lastVehicles) do
        if vehicle == source then lastVehicles[player] = nil end
    end
end)

addEventHandler("onPlayerQuit", root, function()
    originalAlpha[source] = nil
    lastVehicles[source] = nil
end)

addEventHandler("onResourceStart", resourceRoot, function()
    outputChatBox("[Shadow test] /vshadow 0|255 : vehicule, /pshadow 0|255 : joueur, /shadowreset : restaurer", root, 130, 220, 255)
end)
