local revision
local function report()
    local loaded, reason = engineGetPedPathPatchRevision()
    outputDebugString('[pathpatch] revision=' .. tostring(loaded) .. ' reason=' .. tostring(reason))
    triggerServerEvent('pathpatch:ready', resourceRoot, loaded, engineGetPedPathGraphRevision())
end
addEventHandler('onClientResourceStart', resourceRoot, function()
    if not engineLoadPedPathPatch then
        outputChatBox('This client does not contain the pedestrian patch API.', 255, 80, 80)
        return
    end
    local ok, result = engineLoadPedPathPatch('detour.npp')
    revision = ok and result or nil
    outputChatBox('Path patch: ' .. tostring(ok) .. ' / ' .. tostring(result))
    report()
end)
addEvent('pathpatch:probe', true)
addEventHandler('pathpatch:probe', resourceRoot, function(id)
    triggerServerEvent('pathpatch:probeResult', resourceRoot, id, engineGetPedPathPatchRevision(), engineGetPedPathGraphRevision())
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
        if not isElement(ped) or engineGetPedPathPatchRevision() ~= fixture.revision or engineGetPedPathGraphRevision() ~= expectedGraph then
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
