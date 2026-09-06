local rows = {}
local supported = type(setPedCarryObject) == 'function' and type(getPedCarriedObject) == 'function'
    and type(putDownPedObject) == 'function' and type(cancelPedCarryObject) == 'function'
local function report(object, row, state)
    if row.executor ~= localPlayer or row.suppress or row.finished then return end
    if state == 'released' then row.finished = true end
    triggerServerEvent('nativeCargo:report', resourceRoot, object, row.revision, state)
end
local function cleanup(object, row)
    row.suppress = true
    if supported and isElement(row.ped) and row.started and getPedCarriedObject(row.ped) == row.proxy then
        cancelPedCarryObject(row.ped)
    end
    if isElement(row.proxy) then destroyElement(row.proxy) end
    row.proxy = nil
    if isElement(object) and row.alpha then setElementAlpha(object, row.alpha) end
    row.suppress = false
end
addEvent('nativeCargo:state', true)
addEventHandler('nativeCargo:state', resourceRoot, function(object, revision, phase, ped, executor)
    if not isElement(object) then return end
    local old = rows[object]
    if old and revision < old.revision then return end
    if old and revision == old.revision then old.phase = phase; return end
    if old then cleanup(object, old) end
    rows[object] = {revision=revision, phase=phase, ped=ped, executor=executor,
        created=getTickCount(), alpha=getElementAlpha(object)}
end)
if supported then
    addEventHandler('onClientPedCarryStateChange', root, function(proxy, state, reason)
        for object,row in pairs(rows) do
            if row.ped == source and row.proxy == proxy then
                report(object,row,state)
                outputDebugString('[native-cargo] ' .. state .. ': ' .. reason)
                break
            end
        end
    end)
end
addEventHandler('onClientPreRender', root, function()
    for object,row in pairs(rows) do
        if row.phase ~= 'available' and not row.finished then
            if not isElement(row.ped) or not isElement(object) then
                report(object,row,'released')
            elseif not isElementStreamedIn(row.ped) or not isElementStreamedIn(object) then
                if row.started then report(object,row,'released') end
                cleanup(object,row)
            else
                if not isElement(row.proxy) then
                    local x,y,z = getElementPosition(object)
                    row.proxy = createObject(1271,x,y,z)
                    setElementInterior(row.proxy,getElementInterior(row.ped))
                    setElementDimension(row.proxy,getElementDimension(row.ped))
                    if row.executor ~= localPlayer then
                        setElementCollisionsEnabled(row.proxy,false)
                        setElementFrozen(row.proxy,true)
                    end
                end
                if row.executor == localPlayer then
                    if not supported then
                        report(object,row,'released')
                        outputChatBox('Ce client ne contient pas les API cargo natives.')
                    elseif not row.started then
                        row.started = setPedCarryObject(row.ped,row.proxy,'box')
                        if not row.started and getTickCount()-row.created > 5000 then report(object,row,'released') end
                    elseif getPedCarriedObject(row.ped) ~= row.proxy then
                        -- Covers teardown paths where native model destruction
                        -- deliberately cannot invoke a reentrant Lua callback.
                        report(object,row,'released')
                    end
                else
                    -- Observers reconstruct accepted state without submitting
                    -- tasks to a remote ped or moving the authoritative object.
                    -- This hand-position preview is explicitly NOT a claim of
                    -- replicated native pickup/putdown animation fidelity.
                    local x,y,z = getPedBonePosition(row.ped,25)
                    if x then setElementPosition(row.proxy,x,y,z) end
                    local rx,ry,rz = getElementRotation(row.ped)
                    setElementRotation(row.proxy,rx,ry,rz)
                end
                if row.started or row.executor ~= localPlayer then setElementAlpha(object,0) end
            end
        end
    end
end)
local function forOwned(action)
    for _,row in pairs(rows) do
        if row.executor == localPlayer and row.started and not row.finished then action(row.ped) end
    end
end
addCommandHandler('cargodrop',function() if supported then forOwned(putDownPedObject) end end)
addCommandHandler('cargocancel',function() if supported then forOwned(cancelPedCarryObject) end end)
addEventHandler('onClientElementDestroy',root,function()
    if rows[source] then cleanup(source,rows[source]); rows[source]=nil end
end)
addEventHandler('onClientResourceStop',resourceRoot,function()
    for object,row in pairs(rows) do cleanup(object,row) end
end)
addEventHandler('onClientResourceStart',resourceRoot,function()
    triggerServerEvent('nativeCargo:ready',resourceRoot)
end)

addCommandHandler('cargorejections', function()
    if not supported then outputChatBox('API cargo absentes de ce client.'); return end
    local x,y,z = getElementPosition(localPlayer)
    local wrongModel = createObject(1337,x,y,z)
    local farBox = createObject(1271,x+100,y,z)
    local nearbyBox = createObject(1271,x,y,z)
    assert(not setPedCarryObject(false, nearbyBox, 'box'), 'invalid holder accepted')
    assert(not setPedCarryObject(localPlayer, false, 'box'), 'invalid object accepted')
    assert(not setPedCarryObject(localPlayer, nearbyBox, 'unknown'), 'unknown preset accepted')
    assert(not setPedCarryObject(localPlayer, wrongModel, 'box'), 'unsupported model accepted')
    assert(not setPedCarryObject(localPlayer, farBox, 'box'), 'distant object accepted')
    setObjectScale(nearbyBox,2)
    assert(not setPedCarryObject(localPlayer, nearbyBox, 'box'), 'scaled preset accepted')
    for _,player in ipairs(getElementsByType('player')) do
        if player ~= localPlayer then
            assert(not setPedCarryObject(player,nearbyBox,'box'), 'remote player accepted')
        end
    end
    destroyElement(wrongModel); destroyElement(farBox); destroyElement(nearbyBox)
    outputChatBox('Cargo : assertions de rejet réussies (à répéter avec les modèles streamés).')
end)
