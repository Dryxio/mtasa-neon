local revision
local barrier
local collisionReady = false
local prepareCollision
local function report()
    local loaded, reason = engineGetPedPathPatchRevision()
    outputDebugString('[pathpatch] revision=' .. tostring(loaded) .. ' reason=' .. tostring(reason))
    triggerServerEvent('pathpatch:ready', resourceRoot, collisionReady and loaded, engineGetPedPathGraphRevision())
end
addEventHandler('onClientResourceStart', resourceRoot, function()
    if not engineLoadPedPathPatch then
        outputChatBox('This client does not contain the pedestrian patch API.', 255, 80, 80)
        return
    end
    local ok, result = engineLoadPedPathPatch('detour.npp')
    revision = ok and result or nil
    outputChatBox('Path patch: ' .. tostring(ok) .. ' / ' .. tostring(result))
    if not ok then report(); return end
    local a,b = fixture.start,fixture.finish
    -- FollowNodeRoute's short-route shortcut tests buildings, not CObjects.
    -- Admit the fixture only after collision proves the direct route blocked
    -- and the westward detour clear; a visual model alone is not evidence.
    barrier = createBuilding(3095, Vector3((a[1]+b[1])/2, (a[2]+b[2])/2, a[3]+4), Vector3(90,0,0))
    prepareCollision = function()
    local attempts = 0
    local function clear(x1,y1,z1,x2,y2,z2)
        return isLineOfSightClear(x1,y1,z1,x2,y2,z2,true,false,false,false,false,false,false)
    end
    local function checkCollision()
        attempts = attempts + 1
        local z = a[3]+1
        local direct = clear(a[1],a[2],z,b[1],b[2],z)
        local detour = clear(a[1],a[2],z,1948.5,a[2],z) and clear(1948.5,a[2],z,1948.5,b[2],z) and clear(1948.5,b[2],z,b[1],b[2],z)
        collisionReady = isElement(barrier) and not direct and detour
        if collisionReady or attempts>=100 then
            outputChatBox('Path patch building collision ready: ' .. tostring(collisionReady))
            report()
        else
            setTimer(checkCollision,100,1)
        end
    end
    checkCollision()
    end
    prepareCollision()
end)
addEvent('pathpatch:prepare', true)
addEventHandler('pathpatch:prepare', resourceRoot, function() if prepareCollision then prepareCollision() end end)
addEvent('pathpatch:probe', true)
addEventHandler('pathpatch:probe', resourceRoot, function(id)
    triggerServerEvent('pathpatch:probeResult', resourceRoot, id, collisionReady and engineGetPedPathPatchRevision(), engineGetPedPathGraphRevision())
end)
addEvent('pathpatch:task', true)
addEventHandler('pathpatch:task', resourceRoot, function(ped, mode, id, expectedGraph)
    local tries = 0
    local function done(ok)
        local graph = engineGetPedPathGraphRevision()
        triggerServerEvent('pathpatch:taskResult', resourceRoot, id, ok, graph)
        outputDebugString('[pathpatch] task=' .. mode .. ' accepted=' .. tostring(ok))
        if not ok then report() end
    end
    local function dispatch()
        tries = tries + 1
        if not collisionReady or not isElement(ped) or engineGetPedPathPatchRevision() ~= fixture.revision or engineGetPedPathGraphRevision() ~= expectedGraph then
            done(false)
            return
        end
        local ok
        if mode == 'wander' then
            ok = setPedWander(ped, 'walk')
        else
            local p = fixture.finish
            ok = setPedNavigateTo(ped, Vector3(p[1], p[2], p[3]), 'walk')
        end
        if ok or tries >= 20 then done(ok) else setTimer(dispatch, 100, 1) end
    end
    dispatch()
end)
addCommandHandler('pathpatchstatus', report)
addCommandHandler('pathpatchunload', function()
    outputChatBox('Unloaded: ' .. tostring(engineUnloadPedPathPatch()))
    report()
end)
addCommandHandler('pathpatchload', function()
    local ok, result = engineLoadPedPathPatch('detour.npp')
    outputChatBox('Load: ' .. tostring(ok) .. ' / ' .. tostring(result))
    report()
end)
