local teardowns = {}
local occupancies = {}
local vehiclePlacements = {}

local function stopTeardown(id, restoreFade)
    local teardown = teardowns[id]
    if teardown and isTimer(teardown.timer) then killTimer(teardown.timer) end
    teardowns[id] = nil
    if restoreFade == true then fadeCamera(true, 0) end
end

local function stopVehiclePlacement(id)
    local record = vehiclePlacements[id]
    if not record then return end
    if isTimer(record.timer) then killTimer(record.timer) end
    if record.lease and type(releaseElementStreamingLease) == "function" then
        releaseElementStreamingLease(record.lease)
    end
    vehiclePlacements[id] = nil
end

local function prepareVehiclePlacement(id, vehicle)
    local record = vehiclePlacements[id]
    if record and record.vehicle == vehicle then
        if isTimer(record.timer) then killTimer(record.timer) end
        record.timer = nil
        return record
    end
    stopVehiclePlacement(id)
    if type(acquireElementStreamingLease) ~= "function" or
        type(releaseElementStreamingLease) ~= "function" then
        return false, "element streaming lease API unavailable"
    end
    local lease = isElement(vehicle) and acquireElementStreamingLease(vehicle)
    if not lease then return false, "vehicle streaming lease refused" end
    record = {vehicle = vehicle, lease = lease}
    vehiclePlacements[id] = record
    return record
end

local function stopOccupancy(id)
    local record = occupancies[id]
    if not record then return end
    if isTimer(record.timer) then killTimer(record.timer) end
    for _, lease in ipairs(record.leases or {}) do
        if type(releaseElementStreamingLease) == "function" then releaseElementStreamingLease(lease) end
    end
    occupancies[id] = nil
end

local function prepareOccupancy(id, assignments)
    local record = occupancies[id]
    if record then
        if isTimer(record.timer) then killTimer(record.timer) end
        record.timer = nil
        return record
    end
    if type(acquireElementStreamingLease) ~= "function" or
        type(releaseElementStreamingLease) ~= "function" then
        return false, "element streaming lease API unavailable"
    end
    record = {startedAt = getTickCount(), stableSamples = 0, leases = {}}
    local leased = {}
    for _, assignment in ipairs(assignments) do
        for _, element in ipairs({assignment.vehicle, assignment.ped}) do
            if element ~= localPlayer and not leased[element] then
                local lease = isElement(element) and acquireElementStreamingLease(element)
                if not lease then
                    stopOccupancy(id)
                    for _, acquired in ipairs(record.leases) do releaseElementStreamingLease(acquired) end
                    return false, "vehicle occupancy streaming lease refused"
                end
                leased[element] = true
                record.leases[#record.leases + 1] = lease
            end
        end
    end
    occupancies[id] = record
    return record
end

local function occupancyReady(assignments, requireSeats)
    for index, assignment in ipairs(assignments) do
        local ped, vehicle = assignment.ped, assignment.vehicle
        if not isElement(ped) then return false, ("assignment %d ped missing"):format(index) end
        if not isElement(vehicle) then return false, ("assignment %d vehicle missing"):format(index) end
        if not isElementStreamedIn(vehicle) then
            return false, ("assignment %d vehicle not streamed"):format(index)
        end
        if ped ~= localPlayer and not isElementStreamedIn(ped) then
            return false, ("assignment %d ped not streamed"):format(index)
        end
        if requireSeats and
            (getPedOccupiedVehicle(ped) ~= vehicle or getPedOccupiedVehicleSeat(ped) ~= assignment.seat) then
            return false, ("assignment %d seat %d not converged"):format(index, assignment.seat)
        end
    end
    return true
end

local function pollOccupancy(id, assignments, requireSeats, eventName)
    local record, preparationReason = prepareOccupancy(id, assignments)
    if not record then
        return triggerServerEvent(eventName, resourceRoot, id, false, preparationReason)
    end
    record.startedAt, record.stableSamples = getTickCount(), 0
    record.timer = setTimer(function()
        if occupancies[id] ~= record then return end
        local ready, reason = occupancyReady(assignments, requireSeats)
        if ready then
            record.stableSamples = record.stableSamples + 1
            if record.stableSamples >= 3 then
                if isTimer(record.timer) then killTimer(record.timer) end
                record.timer = nil
                if requireSeats then stopOccupancy(id) end
                return triggerServerEvent(eventName, resourceRoot, id, true)
            end
        else
            record.stableSamples = 0
            record.lastReason = reason
        end
        if getTickCount() - record.startedAt > 10000 then
            stopOccupancy(id)
            triggerServerEvent(eventName, resourceRoot, id, false, record.lastReason or
                                   (requireSeats and "vehicle seats did not converge" or
                                       "vehicle occupancy elements did not stream on the coordinator"))
        end
    end, 50, 0)
end

addEvent("storyWorldRuntime:prepareOccupancy", true)
addEventHandler("storyWorldRuntime:prepareOccupancy", resourceRoot, function(id, assignments)
    if type(assignments) ~= "table" or #assignments == 0 then
        return triggerServerEvent("storyWorldRuntime:occupancyPrepared", resourceRoot, id, false,
                                  "invalid vehicle occupancy assignments")
    end
    pollOccupancy(id, assignments, false, "storyWorldRuntime:occupancyPrepared")
end)

addEvent("storyWorldRuntime:verifyOccupancy", true)
addEventHandler("storyWorldRuntime:verifyOccupancy", resourceRoot, function(id, assignments)
    if type(assignments) ~= "table" or #assignments == 0 then
        return triggerServerEvent("storyWorldRuntime:occupancyVerified", resourceRoot, id, false,
                                  "invalid vehicle occupancy assignments")
    end
    pollOccupancy(id, assignments, true, "storyWorldRuntime:occupancyVerified")
end)

addEvent("storyWorldRuntime:cancelOccupancy", true)
addEventHandler("storyWorldRuntime:cancelOccupancy", resourceRoot, function(id)
    stopOccupancy(id)
end)

addEvent("storyWorldRuntime:measureVehicle", true)
addEventHandler("storyWorldRuntime:measureVehicle", resourceRoot, function(id, vehicle, model)
    local record, reason = prepareVehiclePlacement(id, vehicle)
    if not record then
        return triggerServerEvent("storyWorldRuntime:vehicleMeasured", resourceRoot, id, vehicle, false, reason)
    end
    local startedAt = getTickCount()
    record.timer = setTimer(function()
        if not isElement(vehicle) then
            stopVehiclePlacement(id)
            return triggerServerEvent("storyWorldRuntime:vehicleMeasured", resourceRoot, id, vehicle, false)
        end
        if isElementStreamedIn(vehicle) and isElementSyncer(vehicle) and getElementModel(vehicle) == tonumber(model) then
            local baseOffset = tonumber(getElementDistanceFromCentreOfMassToBaseOfModel(vehicle))
            if baseOffset and baseOffset > 0 then
                killTimer(record.timer)
                record.timer = nil
                return triggerServerEvent("storyWorldRuntime:vehicleMeasured", resourceRoot, id, vehicle, baseOffset)
            end
        elseif getTickCount() - startedAt > 10000 then
            stopVehiclePlacement(id)
            triggerServerEvent("storyWorldRuntime:vehicleMeasured", resourceRoot, id, vehicle, false)
        end
    end, 50, 0)
end)

addEvent("storyWorldRuntime:verifyVehicle", true)
addEventHandler("storyWorldRuntime:verifyVehicle", resourceRoot, function(id, vehicle, expectedZ, tolerance)
    expectedZ, tolerance = tonumber(expectedZ), tonumber(tolerance) or 0.03
    local record, reason = prepareVehiclePlacement(id, vehicle)
    if not record then
        return triggerServerEvent("storyWorldRuntime:vehicleVerified", resourceRoot, id, vehicle, false, reason)
    end
    local startedAt, stableSamples, lastReason = getTickCount(), 0, "verification not sampled"
    record.timer = setTimer(function()
        if not isElement(vehicle) then
            stopVehiclePlacement(id)
            return triggerServerEvent("storyWorldRuntime:vehicleVerified", resourceRoot, id, vehicle, false,
                                      "vehicle disappeared")
        end
        local _, _, z = getElementPosition(vehicle)
        local streamed, syncing = isElementStreamedIn(vehicle), isElementSyncer(vehicle)
        local delta = expectedZ and math.abs(z - expectedZ)
        if streamed and syncing and delta and delta <= tolerance then
            stableSamples = stableSamples + 1
            if stableSamples >= 3 then
                stopVehiclePlacement(id)
                return triggerServerEvent("storyWorldRuntime:vehicleVerified", resourceRoot, id, vehicle, true, nil,
                                          z)
            end
        else
            stableSamples = 0
            if not streamed then
                lastReason = "vehicle not streamed under placement lease"
            elseif not syncing then
                lastReason = "placement owner is not the local syncer"
            elseif not delta then
                lastReason = "invalid expected centre Z"
            else
                lastReason = ("centre Z delta %.5f exceeds %.5f"):format(delta, tolerance)
            end
        end
        if getTickCount() - startedAt > 10000 then
            stopVehiclePlacement(id)
            triggerServerEvent("storyWorldRuntime:vehicleVerified", resourceRoot, id, vehicle, false,
                               "vehicle did not stabilize at converted SCM Z: " .. lastReason, z)
        end
    end, 50, 0)
end)

addEvent("storyWorldRuntime:cancelVehiclePlacement", true)
addEventHandler("storyWorldRuntime:cancelVehiclePlacement", resourceRoot, function(id)
    stopVehiclePlacement(id)
end)

addEvent("storyWorldRuntime:prepareTeardown", true)
addEventHandler("storyWorldRuntime:prepareTeardown", resourceRoot, function(id, elements, fadeOut)
    if type(elements) ~= "table" then return end
    fadeOut = math.max(0, math.min(3, tonumber(fadeOut) or 0))
    stopTeardown(id, false)
    local teardown = {elements = elements, armedAt = getTickCount(), fadeOwned = fadeOut > 0}
    teardowns[id] = teardown
    if fadeOut > 0 then fadeCamera(false, fadeOut) end
    teardown.timer = setTimer(function()
        teardown.timer = nil
        if teardowns[id] == teardown then triggerServerEvent("storyWorldRuntime:teardownArmed", resourceRoot, id) end
    end, math.max(50, math.ceil(fadeOut * 1000)), 1)
end)

addEvent("storyWorldRuntime:commitTeardown", true)
addEventHandler("storyWorldRuntime:commitTeardown", resourceRoot, function(id)
    local teardown = teardowns[id]
    if not teardown then return end
    teardown.timer = setTimer(function()
        for _, element in ipairs(teardown.elements) do
            if isElement(element) then
                if getTickCount() - teardown.armedAt <= 10000 then return end
                stopTeardown(id, false)
                return triggerServerEvent("storyWorldRuntime:teardownGone", resourceRoot, id, false,
                                          "mission element remained client-visible")
            end
        end
        stopTeardown(id, false)
        triggerServerEvent("storyWorldRuntime:teardownGone", resourceRoot, id, true)
    end, 50, 0)
end)

addEvent("storyWorldRuntime:cancelTeardown", true)
addEventHandler("storyWorldRuntime:cancelTeardown", resourceRoot, function(id, restoreFade)
    stopTeardown(id, restoreFade == true)
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    local teardownIds = {}
    for id in pairs(teardowns) do teardownIds[#teardownIds + 1] = id end
    for _, id in ipairs(teardownIds) do stopTeardown(id, true) end
    -- A committed teardown deliberately forgets its record while the caller
    -- owns the following black-frame transition. If this runtime itself stops
    -- in that gap, never leave GTA's global fade stranded.
    fadeCamera(true, 0)
    local ids = {}
    for id in pairs(occupancies) do ids[#ids + 1] = id end
    for _, id in ipairs(ids) do stopOccupancy(id) end
    ids = {}
    for id in pairs(vehiclePlacements) do ids[#ids + 1] = id end
    for _, id in ipairs(ids) do stopVehiclePlacement(id) end
end)
