local function message(text, r, g, b)
    outputChatBox("[ped-shadow] " .. text, r or 190, g or 225, b or 255)
end

addCommandHandler("pedshadowmode", function(_, mode)
    mode = mode and mode:lower() or ""

    local success
    if mode == "blob" then
        success = setDynamicPedShadowsEnabled(false)
    elseif mode == "dynamic" then
        success = setDynamicPedShadowsEnabled(true)
    elseif mode == "reset" then
        success = resetDynamicPedShadows()
    else
        message("Usage: /pedshadowmode [blob|dynamic|reset]", 255, 180, 120)
        return
    end

    if success then
        message(("Shadow mode set to %s."):format(mode))
    else
        message(("Failed to set shadow mode to %s."):format(mode), 255, 100, 100)
    end
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    message("Use /pedshadow to spawn the synced ped and /pedshadowmode blob|dynamic.")
end)
