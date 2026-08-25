local placements = {}
local placementsById = {}
local teardowns = {}
local teardownsById = {}
local modelBaseOffsets = {}
local nextPlacementId = 0
local nextTeardownId = 0

addEvent("onStoryScmVehicleStateChange", false)
addEvent("onStoryWorldTeardownStateChange", false)

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

addEvent("storyWorldRuntime:vehicleMeasured", true)
addEventHandler("storyWorldRuntime:vehicleMeasured", resourceRoot, function(id, vehicle, baseOffset)
    local record = placementsById[tonumber(id)]
    baseOffset = tonumber(baseOffset)
    if source ~= resourceRoot or not record or record.state ~= "measuring" or client ~= record.syncer or
        vehicle ~= record.vehicle or not finite(baseOffset) or baseOffset <= 0 or baseOffset > 10 then
        return
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

local function removeTeardown(record, destroyHandle)
    if not record then return end
    clearTimer(record)
    teardowns[record.handle] = nil
    teardownsById[record.id] = nil
    if destroyHandle and isElement(record.handle) then destroyElement(record.handle) end
end

local function failTeardown(record, reason)
    if not record or record.state == "failed" or record.state == "ready" then return end
    clearTimer(record)
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
    removeTeardown(record, true)
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
end)

addEventHandler("onElementDestroy", root, function()
    local placement = placements[source]
    if placement then return removePlacement(placement, false, true) end
    local teardown = teardowns[source]
    if teardown then return removeTeardown(teardown, false) end
    for _, record in pairs(placements) do
        if record.vehicle == source and record.state ~= "ready" and record.state ~= "failed" then
            failPlacement(record, "vehicle destroyed during SCM placement")
            return
        end
    end
end)

addEventHandler("onResourceStop", root, function(stoppedResource)
    local stoppedRoot = getResourceRootElement(stoppedResource)
    local ownedPlacements, ownedTeardowns = {}, {}
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
    for _, record in ipairs(ownedPlacements) do removePlacement(record, true, true) end
    for _, record in ipairs(ownedTeardowns) do removeTeardown(record, true) end
end)

outputServerLog("[story world runtime] Ready: SCM placement and client-confirmed teardown available.")
