local leases = {}
local leasesById = {}
local leasesByPlayer = {}
local activeTransitionByPlayer = {}
local nextLeaseId = 0

local function finite(value)
    return type(value) == "number" and value == value and value > -math.huge and value < math.huge
end

local function copyDuration(value, defaultValue, maximum)
    value = tonumber(value)
    if not finite(value) then
        return defaultValue
    end
    return math.max(0.0, math.min(maximum, value))
end

local function copyOptions(options)
    options = type(options) == "table" and options or {}
    return {
        fadeOut = copyDuration(options.fadeOut, 1.0, 3.0),
        blackHold = copyDuration(options.blackHold, 0.25, 3.0),
        fadeIn = copyDuration(options.fadeIn, 1.0, 3.0),
    }
end

local function snapshot(lease, extra)
    local data = {
        id = lease.id,
        name = lease.definition.name,
        site = lease.definition.site,
        state = lease.state,
        player = lease.player,
        dimension = lease.dimension,
        epoch = lease.epoch,
    }
    if type(extra) == "table" then
        for key, value in pairs(extra) do
            data[key] = value
        end
    end
    return data
end

local function emit(lease, state, extra)
    lease.state = state
    triggerEvent("onStoryEntryExitStateChange", lease.handle, state, snapshot(lease, extra))
end

local function clearTimer(lease)
    if isTimer(lease.timeout) then
        killTimer(lease.timeout)
    end
    lease.timeout = nil
end

local function restorePlayer(lease, rollback)
    if activeTransitionByPlayer[lease.player] == lease then
        activeTransitionByPlayer[lease.player] = nil
    end
    local saved = lease.transitionSnapshot
    if rollback and isElement(lease.player) and saved then
        setElementInterior(lease.player, saved.interior)
        setCameraInterior(lease.player, saved.cameraInterior)
        setElementDimension(lease.player, saved.dimension)
        setElementPosition(lease.player, saved.x, saved.y, saved.z)
        setElementRotation(lease.player, saved.rx, saved.ry, saved.rz)
        setElementVelocity(lease.player, saved.vx, saved.vy, saved.vz)
    end
    if isElement(lease.player) and lease.wasFrozen ~= nil then
        setElementFrozen(lease.player, lease.wasFrozen)
    end
    lease.transitionSnapshot = nil
    lease.wasFrozen = nil
    lease.direction = nil
    lease.blackReadyAt = nil
end

local function removePlayerLease(lease)
    local playerLeases = leasesByPlayer[lease.player]
    if not playerLeases then
        return
    end
    playerLeases[lease] = nil
    if not next(playerLeases) then
        leasesByPlayer[lease.player] = nil
    end
end

local function removeLease(lease, destroyHandle, notifyClient, alreadyRemoving)
    if not lease or lease.removing and not alreadyRemoving then
        return
    end
    lease.removing = true
    clearTimer(lease)
    restorePlayer(lease, true)
    if notifyClient and isElement(lease.player) then
        triggerClientEvent(lease.player, "storyEntryExitRuntime:remove", resourceRoot, lease.id)
    end
    leases[lease.handle] = nil
    leasesById[lease.id] = nil
    removePlayerLease(lease)
    if destroyHandle and isElement(lease.handle) then
        destroyElement(lease.handle)
    end
end

local function failLease(lease, reason)
    if not lease or lease.removing then
        return
    end
    clearTimer(lease)
    restorePlayer(lease, true)
    if isElement(lease.player) then
        triggerClientEvent(lease.player, "storyEntryExitRuntime:cancel", resourceRoot, lease.id, lease.epoch)
    end
    emit(lease, "failed", {reason = reason})
end

local function rotatedOffset(x, y, trigger)
    local radians = math.rad(-(trigger.rotation or 0.0))
    local dx, dy = x - trigger.x, y - trigger.y
    local cosine, sine = math.cos(radians), math.sin(radians)
    return dx * cosine - dy * sine, dx * sine + dy * cosine
end

local function isWithinTrigger(player, endpoint, tolerance)
    if getElementInterior(player) ~= endpoint.interior then
        return false
    end
    local x, y, z = getElementPosition(player)
    local dx, dy = rotatedOffset(x, y, endpoint.trigger)
    tolerance = tolerance or 0.0
    return math.abs(dx) <= endpoint.trigger.radiusX + tolerance and
               math.abs(dy) <= endpoint.trigger.radiusY + tolerance and
               math.abs(z - endpoint.trigger.z) <= endpoint.trigger.zTolerance + tolerance
end

local function transitionEndpoints(lease, direction)
    if direction == "enter" then
        return lease.definition.outside, lease.definition.inside
    end
    if direction == "exit" then
        return lease.definition.inside, lease.definition.outside
    end
    return nil, nil
end

local function validRequest(lease, player, direction)
    if not lease or lease.removing or lease.state ~= "active" or lease.player ~= player or
        activeTransitionByPlayer[player] then
        return false
    end
    if getElementDimension(player) ~= lease.dimension or getPedOccupiedVehicle(player) then
        return false
    end
    local sourceEndpoint = transitionEndpoints(lease, direction)
    return sourceEndpoint and isWithinTrigger(player, sourceEndpoint, 0.35)
end

local function armTimeout(lease)
    clearTimer(lease)
    local expectedEpoch = lease.epoch
    local timeoutMilliseconds = math.ceil((lease.options.fadeOut + lease.options.blackHold + lease.options.fadeIn) * 1000) + 5000
    lease.timeout = setTimer(function()
        if not lease.removing and (lease.state == "fading_out" or lease.state == "committed") and
            lease.epoch == expectedEpoch then
            failLease(lease, "transition client timeout")
        end
    end, timeoutMilliseconds, 1)
end

local function armActivationTimeout(lease)
    clearTimer(lease)
    lease.timeout = setTimer(function()
        if not lease.removing and lease.state == "activating" then
            failLease(lease, "client activation timeout")
        end
    end, 5000, 1)
end

function acquireStoryEntryExit(player, site, dimension, options)
    if not isElement(player) or getElementType(player) ~= "player" then
        return false, "invalid player"
    end
    site = type(site) == "string" and site:lower() or ""
    local definition = STORY_ENTRY_EXIT_DEFINITIONS[site]
    if not definition then
        return false, "unknown entry-exit site"
    end
    dimension = tonumber(dimension)
    if not dimension or dimension < 0 or dimension > 65535 or dimension % 1 ~= 0 then
        return false, "invalid dimension"
    end
    for existing in pairs(leasesByPlayer[player] or {}) do
        if not existing.removing and existing.definition.site == site then
            return false, "entry-exit site already leased for player"
        end
    end

    nextLeaseId = nextLeaseId + 1
    local handle = createElement("story-entry-exit", ("story-entry-exit-%d"):format(nextLeaseId))
    if not handle then
        return false, "handle creation failed"
    end

    local caller = sourceResourceRoot or resourceRoot
    local lease = {
        id = nextLeaseId,
        handle = handle,
        caller = caller,
        player = player,
        definition = definition,
        dimension = dimension,
        options = copyOptions(options),
        epoch = 0,
        state = "activating",
    }
    leases[handle] = lease
    leasesById[lease.id] = lease
    leasesByPlayer[player] = leasesByPlayer[player] or {}
    leasesByPlayer[player][lease] = true
    setElementParent(handle, caller)

    if not triggerClientEvent(player, "storyEntryExitRuntime:add", resourceRoot, lease.id, definition, dimension, lease.options) then
        removeLease(lease, true, false)
        return false, "client dispatch failed"
    end
    armActivationTimeout(lease)
    return handle
end

function releaseStoryEntryExit(handle)
    local lease = leases[handle]
    if not lease or lease.removing or lease.caller ~= (sourceResourceRoot or resourceRoot) then
        return false
    end
    lease.removing = true
    emit(lease, "released")
    removeLease(lease, true, true, true)
    return true
end

function getStoryEntryExitState(handle)
    local lease = leases[handle]
    if not lease or lease.caller ~= (sourceResourceRoot or resourceRoot) then
        return false
    end
    return snapshot(lease, {direction = lease.direction})
end

addEvent("onStoryEntryExitStateChange", false)

addEvent("storyEntryExitRuntime:ack", true)
addEventHandler("storyEntryExitRuntime:ack", resourceRoot, function(id)
    local lease = leasesById[tonumber(id)]
    if source ~= resourceRoot or not client or not lease or lease.player ~= client or lease.state ~= "activating" then
        return
    end
    clearTimer(lease)
    emit(lease, "active", {acquired = true})
end)

addEvent("storyEntryExitRuntime:request", true)
addEventHandler("storyEntryExitRuntime:request", resourceRoot, function(id, direction)
    local lease = leasesById[tonumber(id)]
    if source ~= resourceRoot or not client or not lease or lease.player ~= client then
        return
    end
    if not validRequest(lease, client, direction) then
        triggerClientEvent(client, "storyEntryExitRuntime:rejected", resourceRoot, lease.id)
        return
    end

    lease.epoch = lease.epoch + 1
    lease.direction = direction
    lease.wasFrozen = isElementFrozen(client)
    local x, y, z = getElementPosition(client)
    local rx, ry, rz = getElementRotation(client)
    local vx, vy, vz = getElementVelocity(client)
    lease.transitionSnapshot = {
        x = x,
        y = y,
        z = z,
        rx = rx,
        ry = ry,
        rz = rz,
        vx = vx,
        vy = vy,
        vz = vz,
        interior = getElementInterior(client),
        cameraInterior = getCameraInterior(client),
        dimension = getElementDimension(client),
    }
    activeTransitionByPlayer[client] = lease
    setElementFrozen(client, true)
    lease.blackReadyAt = getTickCount() + math.ceil((lease.options.fadeOut + lease.options.blackHold) * 1000) - 50
    emit(lease, "fading_out", {direction = direction})
    armTimeout(lease)
    triggerClientEvent(client, "storyEntryExitRuntime:fadeOut", resourceRoot, lease.id, lease.epoch, lease.options.fadeOut)
end)

addEvent("storyEntryExitRuntime:black", true)
addEventHandler("storyEntryExitRuntime:black", resourceRoot, function(id, epoch)
    local lease = leasesById[tonumber(id)]
    if source ~= resourceRoot or not client or not lease or lease.player ~= client or lease.state ~= "fading_out" or
        lease.epoch ~= tonumber(epoch) or getTickCount() < lease.blackReadyAt then
        return
    end

    local _, destinationEndpoint = transitionEndpoints(lease, lease.direction)
    if not destinationEndpoint then
        return failLease(lease, "invalid transition direction")
    end
    local destination = destinationEndpoint.destination
    setElementInterior(client, destinationEndpoint.interior)
    setCameraInterior(client, destinationEndpoint.interior)
    setElementDimension(client, lease.dimension)
    setElementPosition(client, destination.x, destination.y, destination.z)
    setElementRotation(client, 0.0, 0.0, destination.rotation)
    setElementVelocity(client, 0.0, 0.0, 0.0)
    emit(lease, "committed", {
        direction = lease.direction,
        interior = destinationEndpoint.interior,
    })
    if lease.removing then
        return
    end
    triggerClientEvent(client, "storyEntryExitRuntime:committed", resourceRoot, lease.id, lease.epoch,
                       destinationEndpoint.interior, lease.options.fadeIn)
end)

addEvent("storyEntryExitRuntime:finished", true)
addEventHandler("storyEntryExitRuntime:finished", resourceRoot, function(id, epoch)
    local lease = leasesById[tonumber(id)]
    if source ~= resourceRoot or not client or not lease or lease.player ~= client or lease.state ~= "committed" or
        lease.epoch ~= tonumber(epoch) then
        return
    end
    local direction = lease.direction
    local _, destinationEndpoint = transitionEndpoints(lease, direction)
    local destination = destinationEndpoint and destinationEndpoint.destination
    local x, y, z = getElementPosition(client)
    if not destination or getElementInterior(client) ~= destinationEndpoint.interior or
        getElementDimension(client) ~= lease.dimension or
        getDistanceBetweenPoints3D(x, y, z, destination.x, destination.y, destination.z) > 3.0 then
        return failLease(lease, "committed destination lost before fade completion")
    end
    clearTimer(lease)
    restorePlayer(lease, false)
    emit(lease, direction == "enter" and "entered" or "exited", {
        direction = direction,
        interior = destinationEndpoint.interior,
    })
    if lease.removing then
        return
    end
    lease.state = "active"
    triggerClientEvent(client, "storyEntryExitRuntime:ready", resourceRoot, lease.id, lease.epoch)
end)

addEventHandler("onElementDestroy", root, function()
    local lease = leases[source]
    if lease and not lease.removing then
        removeLease(lease, false, true)
    end
end)

addEventHandler("onPlayerQuit", root, function()
    local owned = {}
    for lease in pairs(leasesByPlayer[source] or {}) do
        owned[#owned + 1] = lease
    end
    for _, lease in ipairs(owned) do
        removeLease(lease, true, false)
    end
end)

addEventHandler("onResourceStop", root, function(stoppedResource)
    local stoppedRoot = getResourceRootElement(stoppedResource)
    local runtimeStopping = stoppedRoot == resourceRoot
    local owned = {}
    for _, lease in pairs(leases) do
        if stoppedRoot == resourceRoot or lease.caller == stoppedRoot then
            owned[#owned + 1] = lease
        end
    end
    for _, lease in ipairs(owned) do
        if runtimeStopping and lease.caller ~= resourceRoot and lease.state ~= "failed" then
            failLease(lease, "entry-exit runtime stopped")
        end
        removeLease(lease, true, true)
    end
end)

outputServerLog("[story entry-exit runtime] Ready: safe resource-owned IPL transitions available.")
