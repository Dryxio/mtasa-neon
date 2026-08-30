local placements = {}
local placementsById = {}
local teardowns = {}
local teardownsById = {}
local occupancies = {}
local occupanciesById = {}
local playerModelLeases = {}
local playerModelLeaseByPlayer = {}
local modelBaseOffsets = {}
local nextPlacementId = 0
local nextTeardownId = 0
local nextOccupancyId = 0
local nextPlayerModelLeaseId = 0

addEvent("onStoryScmVehicleStateChange", false)
addEvent("onStoryWorldTeardownStateChange", false)
addEvent("onStoryVehicleOccupancyStateChange", false)
addEvent("onStoryPlayerModelLeaseStateChange", false)
addEvent("onStoryWorldRuntimeStopping", false)

local function finite(value)
    return type(value) == "number" and value == value and value > -math.huge and value < math.huge
end

local function validDimension(value)
    return type(value) == "number" and value >= 0 and value <= 65535 and value % 1 == 0
end

local function callerRoot()
    return sourceResourceRoot or resourceRoot
end

local function clearTimer(record)
    if isTimer(record.timeout) then killTimer(record.timeout) end
    record.timeout = nil
end

local function clearOccupancyTimers(record)
    clearTimer(record)
    if isTimer(record.stepTimer) then killTimer(record.stepTimer) end
    if isTimer(record.readyTimer) then killTimer(record.readyTimer) end
    record.stepTimer, record.readyTimer = nil, nil
end

local function placementSnapshot(record, extra)
    local data = {
        id = record.id,
        state = record.state,
        vehicle = record.vehicle,
        model = record.model,
        scriptZ = record.scriptZ,
        centerZ = record.centerZ,
        baseOffset = record.baseOffset,
    }
    for key, value in pairs(type(extra) == "table" and extra or {}) do data[key] = value end
    return data
end

local function emitPlacement(record, state, extra)
    record.state = state
    triggerEvent("onStoryScmVehicleStateChange", record.handle, state, placementSnapshot(record, extra))
end

local function removePlacement(record, destroyHandle, destroyVehicle)
    if not record then return end
    clearTimer(record)
    if isElement(record.syncer) then
        triggerClientEvent(record.syncer, "storyWorldRuntime:cancelVehiclePlacement", resourceRoot, record.id)
    end
    placements[record.handle] = nil
    placementsById[record.id] = nil
    if destroyVehicle and isElement(record.vehicle) then destroyElement(record.vehicle) end
    if destroyHandle and isElement(record.handle) then destroyElement(record.handle) end
end

local function failPlacement(record, reason)
    if not record or record.state == "failed" or record.state == "ready" then return end
    clearTimer(record)
    emitPlacement(record, "failed", {reason = reason})
end

function createStoryScmVehicle(model, x, y, scriptZ, heading, dimension, syncer, options)
    model, x, y, scriptZ, heading, dimension = tonumber(model), tonumber(x), tonumber(y), tonumber(scriptZ),
                                                     tonumber(heading), tonumber(dimension)
    if not model or model % 1 ~= 0 or model < 400 or model > 611 or not finite(x) or not finite(y) or
        not finite(scriptZ) or not finite(heading) or not validDimension(dimension) then
        return false, false, "invalid SCM vehicle placement"
    end
    if not isElement(syncer) or getElementType(syncer) ~= "player" then
        return false, false, "invalid placement syncer"
    end

    options = type(options) == "table" and options or {}
    local vehicle = createVehicle(model, x, y, scriptZ, 0, 0, heading)
    if not vehicle then return false, false, "vehicle creation failed" end
    setElementDimension(vehicle, dimension)
    setElementFrozen(vehicle, true)
    setElementCollisionsEnabled(vehicle, false)
    if not setElementSyncer(vehicle, syncer, true, true) then
        destroyElement(vehicle)
        return false, false, "vehicle syncer assignment failed"
    end

    nextPlacementId = nextPlacementId + 1
    local handle = createElement("story-scm-vehicle", ("story-scm-vehicle-%d"):format(nextPlacementId))
    if not handle then
        destroyElement(vehicle)
        return false, false, "placement handle creation failed"
    end
    setElementParent(handle, callerRoot())
    local record = {
        id = nextPlacementId,
        handle = handle,
        caller = callerRoot(),
        vehicle = vehicle,
        model = model,
        x = x,
        y = y,
        scriptZ = scriptZ,
        heading = heading,
        dimension = dimension,
        syncer = syncer,
        state = "measuring",
        tolerance = math.max(0.005, math.min(0.1, tonumber(options.tolerance) or 0.03)),
    }
    placements[handle] = record
    placementsById[record.id] = record
    record.timeout = setTimer(function()
        if placements[handle] == record and record.state ~= "ready" then
            failPlacement(record, "SCM vehicle placement timeout")
        end
    end, math.max(2000, math.min(30000, tonumber(options.timeout) or 10000)), 1)

    local syncerOffsets = modelBaseOffsets[syncer] or {}
    modelBaseOffsets[syncer] = syncerOffsets
    local cachedOffset = syncerOffsets[model]
    if cachedOffset then
        record.baseOffset = cachedOffset
        record.centerZ = scriptZ + cachedOffset
        setElementPosition(vehicle, x, y, record.centerZ)
        record.state = "verifying"
        triggerClientEvent(syncer, "storyWorldRuntime:verifyVehicle", resourceRoot, record.id, vehicle,
                           record.centerZ, record.tolerance)
    else
        triggerClientEvent(syncer, "storyWorldRuntime:measureVehicle", resourceRoot, record.id, vehicle, model)
    end
    return handle, vehicle
end

function releaseStoryScmVehicle(handle)
    local record = placements[handle]
    if not record or record.caller ~= callerRoot() then return false end
    removePlacement(record, true)
    return true
end

function getStoryScmVehicleState(handle)
    local record = placements[handle]
    if not record or record.caller ~= callerRoot() then return false end
    return record.state, placementSnapshot(record)
end

function createStoryScmPed(model, x, y, scriptZ, heading, dimension)
    model, x, y, scriptZ, heading, dimension = tonumber(model), tonumber(x), tonumber(y), tonumber(scriptZ),
                                                     tonumber(heading), tonumber(dimension)
    if not model or model % 1 ~= 0 or model < 0 or model > 312 or not finite(x) or not finite(y) or
        not finite(scriptZ) or not finite(heading) or not validDimension(dimension) then
        return false, "invalid SCM ped placement"
    end
    -- COMMAND_CREATE_CHAR adds the target executable's 1.0 constant to the
    -- script-space Z. Ordinary MTA createPed deliberately does not.
    local ped = createPed(model, x, y, scriptZ + 1.0, heading)
    if not ped then return false, "ped creation failed" end
    setElementDimension(ped, dimension)
    return ped
end

local function playerModelLeaseSnapshot(record)
    return {id = record.id, state = record.state, model = record.model, players = #record.players}
end

local function restorePlayerModelLease(record)
    if not record or record.state == "released" then return end
    record.state = "released"
    for _, entry in ipairs(record.players) do
        if playerModelLeaseByPlayer[entry.player] == record then playerModelLeaseByPlayer[entry.player] = nil end
        if isElement(entry.player) and getElementModel(entry.player) ~= entry.model then
            setElementModel(entry.player, entry.model)
        end
    end
    playerModelLeases[record.handle] = nil
    triggerEvent("onStoryPlayerModelLeaseStateChange", record.handle, "released", playerModelLeaseSnapshot(record))
end

function createStoryPlayerModelLease(players, model)
    model = tonumber(model)
    if type(players) ~= "table" or not model or model % 1 ~= 0 or model < 0 or model > 312 then
        return false, "invalid player model lease"
    end
    local immutable, seen = {}, {}
    for index, player in ipairs(players) do
        if not isElement(player) or getElementType(player) ~= "player" then
            return false, ("invalid player model lease member %d"):format(index)
        end
        if seen[player] or playerModelLeaseByPlayer[player] then
            return false, ("player model already leased at member %d"):format(index)
        end
        seen[player] = true
        immutable[#immutable + 1] = {player = player, model = getElementModel(player)}
    end
    if #immutable == 0 then return false, "player model lease requires a player" end

    nextPlayerModelLeaseId = nextPlayerModelLeaseId + 1
    local handle = createElement("story-player-model-lease",
                                 ("story-player-model-lease-%d"):format(nextPlayerModelLeaseId))
    if not handle then return false, "player model lease handle creation failed" end
    setElementParent(handle, callerRoot())
    local record = {id = nextPlayerModelLeaseId, handle = handle, caller = callerRoot(), players = immutable,
                    model = model, state = "applying"}
    playerModelLeases[handle] = record
    for _, entry in ipairs(immutable) do playerModelLeaseByPlayer[entry.player] = record end

    for _, entry in ipairs(immutable) do
        if getElementModel(entry.player) ~= model and not setElementModel(entry.player, model) then
            restorePlayerModelLease(record)
            destroyElement(handle)
            return false, "player model transition refused"
        end
    end
    record.state = "active"
    triggerEvent("onStoryPlayerModelLeaseStateChange", handle, "active", playerModelLeaseSnapshot(record))
    return handle
end

function releaseStoryPlayerModelLease(handle)
    local record = playerModelLeases[handle]
    if not record or record.caller ~= callerRoot() then return false end
    restorePlayerModelLease(record)
    if isElement(handle) then destroyElement(handle) end
    return true
end

function getStoryPlayerModelLeaseState(handle)
    local record = playerModelLeases[handle]
    if not record or record.caller ~= callerRoot() then return false end
    return record.state, playerModelLeaseSnapshot(record)
end

local function occupancySnapshot(record, extra)
    local data = {
        id = record.id,
        state = record.state,
        syncer = record.syncer,
        assignments = #record.assignments,
    }
    for key, value in pairs(type(extra) == "table" and extra or {}) do data[key] = value end
    return data
end

local function emitOccupancy(record, state, extra)
    record.state = state
    triggerEvent("onStoryVehicleOccupancyStateChange", record.handle, state, occupancySnapshot(record, extra))
end

local function removeOccupancy(record, destroyHandle)
    if not record then return end
    clearOccupancyTimers(record)
    if isElement(record.syncer) then
        triggerClientEvent(record.syncer, "storyWorldRuntime:cancelOccupancy", resourceRoot, record.id)
    end
    occupancies[record.handle] = nil
    occupanciesById[record.id] = nil
    if destroyHandle and isElement(record.handle) then destroyElement(record.handle) end
end

local function failOccupancy(record, reason)
    if not record or record.state == "failed" or record.state == "ready" then return end
    clearOccupancyTimers(record)
    if isElement(record.syncer) then
        triggerClientEvent(record.syncer, "storyWorldRuntime:cancelOccupancy", resourceRoot, record.id)
    end
    emitOccupancy(record, "failed", {reason = reason})
end

local function advanceOccupancy(record)
    if not occupancies[record.handle] or record.state ~= "warping" then return end
    record.index = record.index + 1
    local assignment = record.assignments[record.index]
    if not assignment then
        record.state = "verifying"
        return triggerClientEvent(record.syncer, "storyWorldRuntime:verifyOccupancy", resourceRoot, record.id,
                                  record.assignments)
    end
    if not isElement(assignment.ped) or not isElement(assignment.vehicle) or
        not warpPedIntoVehicle(assignment.ped, assignment.vehicle, assignment.seat) then
        return failOccupancy(record, ("vehicle warp %d failed"):format(record.index))
    end
    record.stepTimer = setTimer(function() advanceOccupancy(record) end, record.stepDelay, 1)
end

function createStoryVehicleOccupancy(syncer, assignments, options)
    if not isElement(syncer) or getElementType(syncer) ~= "player" or type(assignments) ~= "table" or
        #assignments == 0 then
        return false, "invalid vehicle occupancy request"
    end
    options = type(options) == "table" and options or {}
    local immutable, peds, seats = {}, {}, {}
    for index, assignment in ipairs(assignments) do
        local ped = type(assignment) == "table" and assignment.ped
        local vehicle = type(assignment) == "table" and assignment.vehicle
        local seat = type(assignment) == "table" and tonumber(assignment.seat)
        local pedType = isElement(ped) and getElementType(ped)
        if (pedType ~= "ped" and pedType ~= "player") or not isElement(vehicle) or
            getElementType(vehicle) ~= "vehicle" or not seat or seat % 1 ~= 0 or seat < 0 or
            seat > getVehicleMaxPassengers(vehicle) then
            return false, ("invalid vehicle occupancy assignment %d"):format(index)
        end
        seats[vehicle] = seats[vehicle] or {}
        if peds[ped] or seats[vehicle][seat] then
            return false, ("duplicate vehicle occupancy assignment %d"):format(index)
        end
        peds[ped], seats[vehicle][seat] = true, true
        immutable[index] = {ped = ped, vehicle = vehicle, seat = seat}
    end

    nextOccupancyId = nextOccupancyId + 1
    local handle = createElement("story-vehicle-occupancy", ("story-vehicle-occupancy-%d"):format(nextOccupancyId))
    if not handle then return false, "vehicle occupancy handle creation failed" end
    setElementParent(handle, callerRoot())
    local record = {
        id = nextOccupancyId,
        handle = handle,
        caller = callerRoot(),
        syncer = syncer,
        assignments = immutable,
        state = "preparing",
        index = 0,
        stepDelay = math.max(50, math.min(500, tonumber(options.stepDelay) or 100)),
    }
    occupancies[handle] = record
    occupanciesById[record.id] = record
    record.timeout = setTimer(function()
        if occupancies[handle] == record and record.state ~= "ready" then
            failOccupancy(record, "client-confirmed vehicle occupancy timeout")
        end
    end, math.max(2000, math.min(30000, tonumber(options.timeout) or 15000)), 1)
    if options.stageActors == true then
        for index, assignment in ipairs(immutable) do
            if getElementType(assignment.ped) == "ped" then
                local x, y, z = getElementPosition(assignment.vehicle)
                local _, _, heading = getElementRotation(assignment.vehicle)
                local distance = 2 + index * 0.35
                local radians = math.rad(heading + 90)
                setElementInterior(assignment.ped, getElementInterior(assignment.vehicle))
                setElementDimension(assignment.ped, getElementDimension(assignment.vehicle))
                setElementPosition(assignment.ped, x + math.cos(radians) * distance,
                                   y + math.sin(radians) * distance, z + 0.5)
            end
        end
    end
    triggerClientEvent(syncer, "storyWorldRuntime:prepareOccupancy", resourceRoot, record.id, immutable)
    return handle
end

function releaseStoryVehicleOccupancy(handle)
    local record = occupancies[handle]
    if not record or record.caller ~= callerRoot() then return false end
    removeOccupancy(record, true)
    return true
end

function getStoryVehicleOccupancyState(handle)
    local record = occupancies[handle]
    if not record or record.caller ~= callerRoot() then return false end
    return record.state, occupancySnapshot(record)
end

addEvent("storyWorldRuntime:occupancyPrepared", true)
addEventHandler("storyWorldRuntime:occupancyPrepared", resourceRoot, function(id, ok, reason)
    local record = occupanciesById[tonumber(id)]
    if source ~= resourceRoot or not record or record.state ~= "preparing" or client ~= record.syncer then return end
    if ok ~= true then return failOccupancy(record, tostring(reason or "vehicle occupancy preflight failed")) end
    record.state = "warping"
    record.stepTimer = setTimer(function() advanceOccupancy(record) end, record.stepDelay, 1)
end)

addEvent("storyWorldRuntime:occupancyVerified", true)
addEventHandler("storyWorldRuntime:occupancyVerified", resourceRoot, function(id, ok, reason)
    local record = occupanciesById[tonumber(id)]
    if source ~= resourceRoot or not record or record.state ~= "verifying" or client ~= record.syncer then return end
    if ok ~= true then return failOccupancy(record, tostring(reason or "vehicle occupancy verification failed")) end
    clearTimer(record)
    -- Consumers commonly assign native authority immediately after occupancy.
    -- Emit from a fresh server tick instead of re-entering replication from the
    -- client RPC that acknowledged GTA's seats.
    record.readyTimer = setTimer(function()
        if occupancies[record.handle] == record and record.state == "verifying" then
            emitOccupancy(record, "ready")
        end
    end, 50, 1)
end)

addEvent("storyWorldRuntime:vehicleMeasured", true)
addEventHandler("storyWorldRuntime:vehicleMeasured", resourceRoot, function(id, vehicle, baseOffset, failureReason)
    local record = placementsById[tonumber(id)]
    baseOffset = tonumber(baseOffset)
    if source ~= resourceRoot or not record or record.state ~= "measuring" or client ~= record.syncer or
        vehicle ~= record.vehicle then
        return
    end
    if not finite(baseOffset) or baseOffset <= 0 or baseOffset > 10 then
        return failPlacement(record, tostring(failureReason or "client could not measure vehicle base offset"))
    end
    record.baseOffset = baseOffset
    record.centerZ = record.scriptZ + baseOffset
    modelBaseOffsets[record.syncer] = modelBaseOffsets[record.syncer] or {}
    modelBaseOffsets[record.syncer][record.model] = baseOffset
    setElementPosition(record.vehicle, record.x, record.y, record.centerZ)
    setElementRotation(record.vehicle, 0, 0, record.heading)
    record.state = "verifying"
    triggerClientEvent(record.syncer, "storyWorldRuntime:verifyVehicle", resourceRoot, record.id, record.vehicle,
                       record.centerZ, record.tolerance)
end)

addEvent("storyWorldRuntime:vehicleVerified", true)
addEventHandler("storyWorldRuntime:vehicleVerified", resourceRoot, function(id, vehicle, ok, reason, actualZ)
    local record = placementsById[tonumber(id)]
    if source ~= resourceRoot or not record or record.state ~= "verifying" or client ~= record.syncer or
        vehicle ~= record.vehicle then
        return
    end
    if ok ~= true then return failPlacement(record, tostring(reason or "client verification failed")) end
    actualZ = tonumber(actualZ)
    if not finite(actualZ) or math.abs(actualZ - record.centerZ) > record.tolerance then
        return failPlacement(record, "client observed an invalid vehicle centre Z")
    end
    clearTimer(record)
    emitPlacement(record, "ready", {actualZ = actualZ})
end)

local function teardownSnapshot(record, extra)
    local data = {id = record.id, state = record.state, clients = #record.players, elements = #record.elements}
    for key, value in pairs(type(extra) == "table" and extra or {}) do data[key] = value end
    return data
end

local function emitTeardown(record, state, extra)
    record.state = state
    triggerEvent("onStoryWorldTeardownStateChange", record.handle, state, teardownSnapshot(record, extra))
end

local function removeTeardown(record, destroyHandle, restoreFade)
    if not record then return end
    clearTimer(record)
    if restoreFade == true then
        triggerClientEvent(record.players, "storyWorldRuntime:cancelTeardown", resourceRoot, record.id, true)
    end
    teardowns[record.handle] = nil
    teardownsById[record.id] = nil
    if destroyHandle and isElement(record.handle) then destroyElement(record.handle) end
end

local function failTeardown(record, reason)
    if not record or record.state == "failed" or record.state == "ready" then return end
    clearTimer(record)
    triggerClientEvent(record.players, "storyWorldRuntime:cancelTeardown", resourceRoot, record.id, true)
    emitTeardown(record, "failed", {reason = reason})
end

local function commitTeardown(record)
    if record.state ~= "arming" then return end
    record.state = "destroying"
    for _, element in ipairs(record.elements) do
        if isElement(element) then destroyElement(element) end
    end
    triggerClientEvent(record.players, "storyWorldRuntime:commitTeardown", resourceRoot, record.id)
end

function destroyStoryWorldElements(players, elements, options)
    if type(players) ~= "table" or type(elements) ~= "table" then return false, "invalid teardown lists" end
    options = type(options) == "table" and options or {}
    local validPlayers, validElements = {}, {}
    for _, player in ipairs(players) do
        if not isElement(player) or getElementType(player) ~= "player" then return false, "invalid teardown player" end
        validPlayers[#validPlayers + 1] = player
    end
    for _, element in ipairs(elements) do
        if isElement(element) then validElements[#validElements + 1] = element end
    end
    if #validPlayers == 0 then return false, "teardown requires a client" end

    nextTeardownId = nextTeardownId + 1
    local handle = createElement("story-world-teardown", ("story-world-teardown-%d"):format(nextTeardownId))
    if not handle then return false, "teardown handle creation failed" end
    setElementParent(handle, callerRoot())
    local record = {
        id = nextTeardownId,
        handle = handle,
        caller = callerRoot(),
        players = validPlayers,
        elements = validElements,
        armed = {},
        gone = {},
        state = "arming",
        fadeOut = math.max(0, math.min(3, tonumber(options.fadeOut) or 0)),
    }
    teardowns[handle] = record
    teardownsById[record.id] = record
    record.timeout = setTimer(function()
        if teardowns[handle] == record and record.state ~= "ready" then
            failTeardown(record, "client-confirmed world teardown timeout")
        end
    end, math.max(2000, math.min(30000, tonumber(options.timeout) or 10000)), 1)
    triggerClientEvent(validPlayers, "storyWorldRuntime:prepareTeardown", resourceRoot, record.id, validElements,
                       record.fadeOut)
    return handle
end

function releaseStoryWorldTeardown(handle)
    local record = teardowns[handle]
    if not record or record.caller ~= callerRoot() then return false end
    removeTeardown(record, true, record.state ~= "ready")
    return true
end

addEvent("storyWorldRuntime:teardownArmed", true)
addEventHandler("storyWorldRuntime:teardownArmed", resourceRoot, function(id)
    local record = teardownsById[tonumber(id)]
    if source ~= resourceRoot or not record or record.state ~= "arming" or record.armed[client] ~= nil then return end
    local expected = false
    for _, player in ipairs(record.players) do if player == client then expected = true break end end
    if not expected then return end
    record.armed[client] = true
    for _, player in ipairs(record.players) do if not record.armed[player] then return end end
    commitTeardown(record)
end)

addEvent("storyWorldRuntime:teardownGone", true)
addEventHandler("storyWorldRuntime:teardownGone", resourceRoot, function(id, ok, reason)
    local record = teardownsById[tonumber(id)]
    if source ~= resourceRoot or not record or record.state ~= "destroying" or record.gone[client] ~= nil then return end
    local expected = false
    for _, player in ipairs(record.players) do if player == client then expected = true break end end
    if not expected then return end
    if ok ~= true then return failTeardown(record, tostring(reason or "client teardown failed")) end
    record.gone[client] = true
    for _, player in ipairs(record.players) do if not record.gone[player] then return end end
    clearTimer(record)
    emitTeardown(record, "ready")
end)

addEventHandler("onPlayerQuit", root, function()
    modelBaseOffsets[source] = nil
    local modelLease = playerModelLeaseByPlayer[source]
    if modelLease then playerModelLeaseByPlayer[source] = nil end
    for _, record in pairs(teardowns) do
        if record.state ~= "ready" and record.state ~= "failed" then
            for _, player in ipairs(record.players) do
                if player == source then failTeardown(record, "teardown client disconnected") break end
            end
        end
    end
    for _, record in pairs(placements) do
        if record.syncer == source and record.state ~= "ready" and record.state ~= "failed" then
            failPlacement(record, "placement syncer disconnected")
        end
    end
    for _, record in pairs(occupancies) do
        if record.syncer == source and record.state ~= "ready" and record.state ~= "failed" then
            failOccupancy(record, "vehicle occupancy syncer disconnected")
        end
    end
end)

addEventHandler("onElementDestroy", root, function()
    local placement = placements[source]
    if placement then return removePlacement(placement, false, true) end
    local teardown = teardowns[source]
    if teardown then return removeTeardown(teardown, false, teardown.state ~= "ready") end
    local occupancy = occupancies[source]
    if occupancy then return removeOccupancy(occupancy, false) end
    local playerModelLease = playerModelLeases[source]
    if playerModelLease then
        restorePlayerModelLease(playerModelLease)
        return
    end
    for _, record in pairs(placements) do
        if record.vehicle == source and record.state ~= "ready" and record.state ~= "failed" then
            failPlacement(record, "vehicle destroyed during SCM placement")
            return
        end
    end
    for _, record in pairs(occupancies) do
        if record.state ~= "ready" and record.state ~= "failed" then
            for _, assignment in ipairs(record.assignments) do
                if source == assignment.ped or source == assignment.vehicle then
                    failOccupancy(record, "vehicle occupancy element destroyed")
                    return
                end
            end
        end
    end
end)

addEventHandler("onResourceStop", root, function(stoppedResource)
    local stoppedRoot = getResourceRootElement(stoppedResource)
    local runtimeStopping = stoppedRoot == resourceRoot
    if runtimeStopping then
        -- Mission elements created through exports are owned by this runtime
        -- and will disappear after Lua stop handlers finish. Notify consumers
        -- while their own VMs and cleanup paths are still alive.
        triggerEvent("onStoryWorldRuntimeStopping", resourceRoot)
    end
    local ownedPlacements, ownedTeardowns, ownedOccupancies, ownedPlayerModelLeases = {}, {}, {}, {}
    for _, record in pairs(placements) do
        if stoppedRoot == resourceRoot or record.caller == stoppedRoot then
            ownedPlacements[#ownedPlacements + 1] = record
        end
    end
    for _, record in pairs(teardowns) do
        if stoppedRoot == resourceRoot or record.caller == stoppedRoot then
            ownedTeardowns[#ownedTeardowns + 1] = record
        end
    end
    for _, record in pairs(occupancies) do
        if stoppedRoot == resourceRoot or record.caller == stoppedRoot then
            ownedOccupancies[#ownedOccupancies + 1] = record
        end
    end
    for _, record in pairs(playerModelLeases) do
        if stoppedRoot == resourceRoot or record.caller == stoppedRoot then
            ownedPlayerModelLeases[#ownedPlayerModelLeases + 1] = record
        end
    end
    for _, record in ipairs(ownedPlacements) do
        if runtimeStopping and record.caller ~= resourceRoot and record.state ~= "ready" and record.state ~= "failed" then
            failPlacement(record, "story world runtime stopped during SCM placement")
        end
        removePlacement(record, true, true)
    end
    for _, record in ipairs(ownedTeardowns) do
        if runtimeStopping and record.caller ~= resourceRoot and record.state ~= "ready" and record.state ~= "failed" then
            failTeardown(record, "story world runtime stopped during teardown")
        end
        removeTeardown(record, true, true)
    end
    for _, record in ipairs(ownedOccupancies) do
        if runtimeStopping and record.caller ~= resourceRoot and record.state ~= "ready" and record.state ~= "failed" then
            failOccupancy(record, "story world runtime stopped during vehicle occupancy")
        end
        removeOccupancy(record, true)
    end
    for _, record in ipairs(ownedPlayerModelLeases) do
        restorePlayerModelLease(record)
        if isElement(record.handle) then destroyElement(record.handle) end
    end
end)

outputServerLog("[story world runtime] Ready: SCM placement, player-model leases, occupancy and client-confirmed teardown available.")
