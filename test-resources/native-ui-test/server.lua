-- Demonstration of server-owned remaining time. No client result awards money
-- or completes a mission; stale sequence updates are ignored by the viewer.
local sessions = {}
addEvent('native-ui-test:startClock', true)
addEventHandler('native-ui-test:startClock', resourceRoot, function()
    if not client or source ~= resourceRoot then return end
    sessions[client] = {started=getTickCount(), sequence=0}
end)
addEvent('native-ui-test:stopClock', true)
addEventHandler('native-ui-test:stopClock', resourceRoot, function()
    if client and source == resourceRoot then sessions[client]=nil end
end)
addEventHandler('onPlayerQuit', root, function() sessions[source]=nil end)
setTimer(function()
    for player, session in pairs(sessions) do
        session.sequence=session.sequence+1
        local elapsed=(getTickCount()-session.started)%4294967296
        local remaining=math.max(0,60000-elapsed)
        triggerClientEvent(player,'native-ui-test:clock',resourceRoot,session.sequence,remaining)
        if remaining==0 then sessions[player]=nil end
    end
end,5000,0)
