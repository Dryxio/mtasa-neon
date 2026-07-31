local GLITCH_NAME = "fastweaponstrafe"

setGlitchEnabled(GLITCH_NAME, true)
outputServerLog("[samp-movement] fastweaponstrafe=" .. tostring(isGlitchEnabled(GLITCH_NAME)))

local function replyToExecutor(executor, message, red, green, blue)
    if isElement(executor) then
        outputChatBox(message, executor, red or 120, green or 220, blue or 255)
    else
        outputServerLog(message)
    end
end

addCommandHandler("faststrafe", function(executor)
    local enabled = not isGlitchEnabled(GLITCH_NAME)
    setGlitchEnabled(GLITCH_NAME, enabled)

    local message = "[faststrafe] " .. (enabled and "enabled" or "disabled")
    replyToExecutor(executor, message, enabled and 110 or 255, enabled and 230 or 190, enabled and 140 or 80)
    outputServerLog(message .. (isElement(executor) and (" by " .. getPlayerName(executor)) or " from server console"))
end)

local function reportNativeWalkingStyle(player, prefix)
    local enabled = isPedUsingNativeWalkingStyle(player)
    local style = getPedWalkingStyle(player)
    local message = ("%s native=%s walkingStyle=%s model=%s"):format(prefix, tostring(enabled), tostring(style), tostring(getElementModel(player)))

    outputChatBox(message, player, 255, 220, 80)
    outputServerLog("[samp-movement] " .. getPlayerName(player) .. " " .. message)
end

local function setNativeWalkingStyle(player, enabled, prefix)
    local succeeded = setPedUseNativeWalkingStyle(player, enabled)
    if not succeeded then
        outputChatBox("setPedUseNativeWalkingStyle failed", player, 255, 80, 80)
        outputServerLog("[samp-movement] setPedUseNativeWalkingStyle failed for " .. getPlayerName(player))
        return
    end

    reportNativeWalkingStyle(player, prefix)
end

addEventHandler("onPlayerJoin", root, function()
    setNativeWalkingStyle(source, true, "Model-native walking enabled:")
end)

addEventHandler("onResourceStart", resourceRoot, function()
    for _, player in ipairs(getElementsByType("player")) do
        setNativeWalkingStyle(player, true, "Model-native walking enabled:")
    end
end)

addCommandHandler("nativewalk", function(player, _, state)
    if not isElement(player) or getElementType(player) ~= "player" then
        return
    end

    local enabled
    if state == "on" then
        enabled = true
    elseif state == "off" then
        enabled = false
    else
        enabled = not isPedUsingNativeWalkingStyle(player)
    end

    setNativeWalkingStyle(player, enabled, "Model-native walking updated:")
end)

addCommandHandler("nativewalkstatus", function(player)
    if isElement(player) and getElementType(player) == "player" then
        reportNativeWalkingStyle(player, "Server state:")
    end
end)

addCommandHandler("sampwalk", function(player, _, state)
    if not isElement(player) or getElementType(player) ~= "player" then
        return
    end

    local useManStyle
    if state == "on" then
        useManStyle = true
    elseif state == "off" then
        useManStyle = false
    else
        useManStyle = getPedWalkingStyle(player) ~= 118
    end

    setPedWalkingStyle(player, useManStyle and 118 or 0)
    reportNativeWalkingStyle(player, useManStyle and "Explicit MAN (118):" or "MTA default:")
end)
