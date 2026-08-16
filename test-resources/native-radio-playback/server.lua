addEvent("native-radio-playback:relay", true)
addEventHandler("native-radio-playback:relay", resourceRoot, function(state)
    -- Relay only to the other connected clients so the sender can compare
    -- their native playback independently and no client can spoof the sender.
    if not client or type(state) ~= "table" then
        return
    end

    for _, player in ipairs(getElementsByType("player")) do
        if player ~= client then
            triggerClientEvent(player, "native-radio-playback:receive", resourceRoot, state, client)
        end
    end
end)
