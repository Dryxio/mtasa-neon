local cargo, fixtures = {}, {}
local function publish(object, row, target)
    triggerClientEvent(target or root, 'nativeCargo:state', resourceRoot,
        object, row.revision, row.phase, row.ped or false, row.executor or false)
end
local function validPed(ped, executor)
    if not isElement(ped) or not isElement(executor) or isPedDead(ped) or isPedInVehicle(ped) then return false end
    return ped == executor or (getElementType(ped) == 'ped' and getElementSyncer(ped) == executor)
end
local function release(object, row, reason)
    if not CargoState.release(row) then return end
    -- Use the last server-observed safe position, never a client's claimed
    -- delivery/drop transform. Unsafe interruption recovers a visible crate.
    if isElement(object) and row.safe then
        setElementPosition(object, row.safe[1], row.safe[2], row.safe[3])
    end
    publish(object, row)
    outputDebugString('[native-cargo] released: ' .. reason)
end
local function reserve(player, ped, object)
    local row = cargo[object]
    if not row or not validPed(ped, player) or getElementDimension(ped) ~= getElementDimension(object)
        or getElementInterior(ped) ~= getElementInterior(object) then return false end
    for _, other in pairs(cargo) do
        if other.phase ~= 'available' and other.ped == ped then return false end
    end
    local x,y,z = getElementPosition(ped)
    local ox,oy,oz = getElementPosition(object)
    local near = getDistanceBetweenPoints3D(x,y,z,ox,oy,oz) <= 3
    if not CargoState.reserve(row, ped, player, getTickCount(), near) then return false end
    row.safe = {ox,oy,oz}
    publish(object, row)
    return true
end
addEvent('nativeCargo:ready', true)
addEventHandler('nativeCargo:ready', resourceRoot, function()
    if not client or source ~= resourceRoot then return end
    for object, row in pairs(cargo) do publish(object, row, client) end
end)
addEvent('nativeCargo:report', true)
addEventHandler('nativeCargo:report', resourceRoot, function(object, revision, state)
    if not client or source ~= resourceRoot then return end
    local row = cargo[object]
    if not row or row.executor ~= client or row.revision ~= revision then return end
    if state == 'released' then release(object, row, 'native report'); return end
    if not validPed(row.ped, client) then release(object, row, 'executor unavailable'); return end
    if CargoState.report(row, client, revision, state, getTickCount()) then publish(object, row) end
end)
local function createFixture(player, npc)
    local previous = fixtures[player]
    if previous then
        if isElement(previous.object) then destroyElement(previous.object) end
        if isElement(previous.ped) and previous.ped ~= player then destroyElement(previous.ped) end
    end
    local x,y,z = getElementPosition(player)
    local interior, dimension = getElementInterior(player), getElementDimension(player)
    local ped = npc and createPed(7, x + 1, y, z) or player
    if npc then
        setElementInterior(ped, interior); setElementDimension(ped, dimension)
        setElementSyncer(ped, player)
    end
    local object = createObject(1271, x + 1, y + 1, z - 0.6)
    setElementInterior(object, interior); setElementDimension(object, dimension)
    setElementFrozen(object, true)
    cargo[object] = CargoState.new()
    fixtures[player] = {object=object, ped=ped}
    publish(object, cargo[object])
    outputChatBox('Cargo : /cargocarry puis /cargodrop. /cargocancel interrompt. PNJ : /cargonpc.', player)
end
addCommandHandler('cargotest', function(player) if isElement(player) then createFixture(player, false) end end)
addCommandHandler('cargonpc', function(player) if isElement(player) then createFixture(player, true) end end)
addCommandHandler('cargocarry', function(player)
    local fixture = fixtures[player]
    if not fixture or not reserve(player, fixture.ped, fixture.object) then
        outputChatBox('Cargo indisponible : rapproche-toi de la caisse et réessaie.', player)
    end
end)
setTimer(function()
    for object, row in pairs(cargo) do
        if row.phase ~= 'available' then
            if not validPed(row.ped, row.executor) or getElementDimension(row.ped) ~= getElementDimension(object)
                or getElementInterior(row.ped) ~= getElementInterior(object) then
                release(object, row, 'ownership/lifecycle')
            elseif CargoState.expired(row, getTickCount()) then
                release(object, row, 'timeout')
            else
                local x,y,z = getElementPosition(row.ped)
                if not isElementInWater(row.ped) and isPedOnGround(row.ped) then row.safe = {x,y,z-0.6} end
            end
        end
    end
end, 250, 0)
addEventHandler('onElementDestroy', root, function()
    if cargo[source] then cargo[source] = nil end
    for object,row in pairs(cargo) do
        if row.ped == source or row.executor == source then release(object,row,'element destroyed') end
    end
end)
addEventHandler('onPlayerQuit', root, function()
    for object,row in pairs(cargo) do
        if row.executor == source then release(object,row,'disconnect') end
    end
    local fixture = fixtures[source]
    fixtures[source] = nil
    if fixture then
        if isElement(fixture.object) then destroyElement(fixture.object) end
        if fixture.ped ~= source and isElement(fixture.ped) then destroyElement(fixture.ped) end
    end
end)

-- Explicit opt-in spawn for an otherwise empty smoke-test server.
addCommandHandler('cargospawn', function(player)
    if not isElement(player) or getElementType(player) ~= 'player' then return end
    spawnPlayer(player, 2492, -1685, 13.5, 0, 0, 0, 0)
    fadeCamera(player, true)
    setCameraTarget(player, player)
end)
