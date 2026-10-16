local ready, ped, owner, mode = {}, nil, nil, 'navigate'
local requests, serial = {}, 0
local expectedGraph = hash('sha256', 'NPEDGRAPH1' .. fixture.revision)

addEvent('pathpatch:ready', true)
addEventHandler('pathpatch:ready', resourceRoot, function(revision, graph)
    if not client or source ~= resourceRoot then return end
    ready[client] = revision == fixture.revision and graph == expectedGraph
    outputDebugString('[pathpatch] ' .. getPlayerName(client) .. ' matching graph=' .. tostring(ready[client]))
end)
local function assign(player)
    if not ready[player] or not isElement(ped) then return false end
    local x,y,z = getElementPosition(player)
    local px,py,pz = getElementPosition(ped)
    if getElementDimension(player) ~= 0 or getElementInterior(player) ~= 0 or getDistanceBetweenPoints3D(x,y,z,px,py,pz)>150 then return false end
    serial = serial + 1
    requests[player] = {id=serial, ped=ped, time=getTickCount(), stage='probe'}
    -- The initial ready report is only candidate discovery. Authority changes
    -- after a fresh response, and success is reported after task acceptance.
    triggerClientEvent(player, 'pathpatch:probe', resourceRoot, serial)
    return true
end
addEvent('pathpatch:probeResult', true)
addEventHandler('pathpatch:probeResult', resourceRoot, function(id, revision, graph)
    local request = client and requests[client]
    if source ~= resourceRoot or not request or request.id~=id or request.stage~='probe' then return end
    if getTickCount()-request.time>3000 or request.ped~=ped or not isElement(ped) or revision~=fixture.revision or graph~=expectedGraph then
        ready[client] = false
        requests[client] = nil
        outputDebugString('[pathpatch] refused stale/incompatible handoff', 2)
        return
    end
    if not setElementSyncer(ped, client, true) then requests[client]=nil; return end
    owner = client
    request.stage = 'task'
    triggerClientEvent(client, 'pathpatch:task', resourceRoot, ped, mode, id, expectedGraph)
end)
addEvent('pathpatch:taskResult', true)
addEventHandler('pathpatch:taskResult', resourceRoot, function(id, accepted, graph)
    local request = client and requests[client]
    if source ~= resourceRoot or not request or request.id~=id or request.stage~='task' or owner~=client then return end
    requests[client] = nil
    local ok = accepted==true and graph==expectedGraph and request.ped==ped and getTickCount()-request.time<6000
    outputChatBox('Path patch task accepted: ' .. tostring(ok) .. ' / ' .. getPlayerName(client), root)
    if not ok then ready[client]=false end
end)
addCommandHandler('pathpatch', function(player, _, action)
    if not isElement(player) or not ready[player] then return end
    if action == 'handoff' then
        for _, other in ipairs(getElementsByType('player')) do
            if other ~= owner and ready[other] and assign(other) then return end
        end
        outputChatBox('A second nearby client with the same graph is required.', player)
        return
    end
    if isElement(ped) then destroyElement(ped) end
    requests = {}
    mode = action == 'wander' and 'wander' or 'navigate'
    local p = fixture.start
    setElementDimension(player, 0)
    setElementInterior(player, 0)
    setElementPosition(player, p[1]+3, p[2], p[3]+2)
    ped = createPed(7, p[1], p[2], p[3]+1)
    if not ped or not assign(player) then outputChatBox('Could not create/assign test pedestrian.', player) end
end)
setTimer(function()
    for player, request in pairs(requests) do
        if getTickCount()-request.time>6000 then
            requests[player]=nil
            outputDebugString('[pathpatch] ownership/task handshake timed out', 2)
        end
    end
end, 1000, 0)
addEventHandler('onPlayerQuit', root, function() ready[source]=nil; requests[source]=nil end)
