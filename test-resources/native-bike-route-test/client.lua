local active

local function releaseLease(token)
    if token and type(releaseElementStreamingLease) == "function" then
        releaseElementStreamingLease(token)
    end
end

local function stopNative(killTask)
    local state = active
    if not state then return end
    if isTimer(state.retry) then killTimer(state.retry) end
    if isTimer(state.monitor) then killTimer(state.monitor) end
    if killTask and state.accepted and isElement(state.ped) then
        killPedTask(state.ped, "primary", 3, false)
    end
    releaseLease(state.pedLease)
    releaseLease(state.vehicleLease)
    if state.cameraOwned then setCameraTarget(localPlayer) end
    active = nil
end

local function report(kind, ...)
    local state = active
    if state then
        triggerServerEvent("nativeBikeRoute:evidence", resourceRoot, state.sessionId, state.epoch, kind, ...)
    end
end

local function nearestPoint(x, y, z)
    local bestIndex, bestDistance = -1, math.huge
    for index, point in ipairs(NATIVE_BIKE_ROUTE) do
        local distance = getDistanceBetweenPoints3D(x, y, z, point[1], point[2], point[3])
        if distance < bestDistance then
            bestIndex, bestDistance = index - 1, distance
        end
    end
    return bestIndex, bestDistance
end

local function samplePresentation(state, observer)
    if not isElement(state.ped) or not isElement(state.vehicle) or not isElementStreamedIn(state.ped) or not isElementStreamedIn(state.vehicle) then
        return false
    end
    local x, y, z = getElementPosition(state.vehicle)
    local nearestIndex, distance = nearestPoint(x, y, z)
    local moveState = getPedMoveState(state.ped) or ""
    local block, anim = getPedAnimation(state.ped)
    if observer then
        report("observer_sample", x, y, z, nearestIndex, distance, moveState, block or "", anim or "",
               getPedOccupiedVehicle(state.ped) == state.vehicle and getPedOccupiedVehicleSeat(state.ped) == 0)
    else
        report("sample", x, y, z, nearestIndex, distance, moveState, block or "", anim or "",
               getElementSyncer(state.ped) == localPlayer, getElementSyncer(state.vehicle) == localPlayer)
    end
    return true
end

local function beginObserver(state)
    if active ~= state then return end
    if isElement(state.ped) and isElementStreamedIn(state.ped) then
        setCameraTarget(state.ped)
        state.cameraOwned = true
    end
    state.monitor = setTimer(function()
        local current = active
        if not current then return end
        if isElement(current.ped) and isElementStreamedIn(current.ped) and not current.cameraOwned then
            setCameraTarget(current.ped)
            current.cameraOwned = true
        end
        samplePresentation(current, true)
    end, 500, 0)
end

local function beginOwner(state)
    if active ~= state then return end
    if not isElement(state.ped) or not isElement(state.vehicle) then
        return report("failure", "elements missing")
    end
    if type(setPedTaskSequence) ~= "function" or type(getPedTaskSequenceProgress) ~= "function" or
       type(acquireElementStreamingLease) ~= "function" then
        return report("failure", "required Neon task/lease API missing")
    end

    if not state.pedLease then state.pedLease = acquireElementStreamingLease(state.ped) end
    if not state.vehicleLease then state.vehicleLease = acquireElementStreamingLease(state.vehicle) end
    if not state.pedLease or not state.vehicleLease then
        return report("failure", "streaming lease rejected")
    end

    if not isElementStreamedIn(state.ped) or not isElementStreamedIn(state.vehicle) or
       not isElementSyncer(state.ped) or not isElementSyncer(state.vehicle) then
        if getTickCount() - state.assignedAt < 10000 then
            state.retry = setTimer(beginOwner, 200, 1, state)
            return
        end
        return report("failure", "ped/vehicle ownership did not converge")
    end

    if getPedOccupiedVehicle(state.ped) ~= state.vehicle or getPedOccupiedVehicleSeat(state.ped) ~= 0 then
        return report("failure", "rider is not BMX driver")
    end
    if type(setPedMissionActor) == "function" and not setPedMissionActor(state.ped, true) then
        return report("failure", "mission actor policy rejected")
    end
    setPedCanBeKnockedOffBike(state.ped, false)

    local sequence = {}
    for routeIndex = state.resumeIndex + 1, #NATIVE_BIKE_ROUTE do
        local point = NATIVE_BIKE_ROUTE[routeIndex]
        sequence[#sequence + 1] = {
            task = "drive_to",
            x = point[1], y = point[2], z = point[3], speed = point[4],
            mode = "normal", vehicleModel = NATIVE_BIKE_ROUTE_MODEL, drivingStyle = "avoid_cars",
        }
    end

    state.accepted = #sequence > 0 and setPedTaskSequence(state.ped, sequence, false) or false
    state.startedAt = getTickCount()
    state.localBaseIndex = state.resumeIndex
    report("acceptance", state.accepted, state.resumeIndex)
    if not state.accepted then return end

    setCameraTarget(state.ped)
    state.cameraOwned = true
    state.monitor = setTimer(function()
        local current = active
        if not current then return end
        local localIndex = getPedTaskSequenceProgress(current.ped)
        if localIndex and localIndex >= 0 then
            local logicalIndex = current.localBaseIndex + localIndex
            if logicalIndex ~= current.lastIndex then
                current.lastIndex = logicalIndex
                report("index", logicalIndex, getTickCount() - current.startedAt)
            end
        end
        samplePresentation(current, false)
    end, 500, 0)
end

addEvent("nativeBikeRoute:assign", true)
addEventHandler("nativeBikeRoute:assign", resourceRoot, function(sessionId, epoch, ped, vehicle, owner, resumeIndex)
    stopNative(true)
    local state = {
        sessionId = sessionId, epoch = epoch, ped = ped, vehicle = vehicle,
        owner = owner == true, resumeIndex = math.max(0, tonumber(resumeIndex) or 0),
        assignedAt = getTickCount(), accepted = false,
    }
    active = state
    if state.owner then beginOwner(state) else beginObserver(state) end
end)

addEvent("nativeBikeRoute:observe", true)
addEventHandler("nativeBikeRoute:observe", resourceRoot, function(sessionId, epoch, ped, vehicle)
    stopNative(false)
    local state = {sessionId = sessionId, epoch = epoch, ped = ped, vehicle = vehicle, owner = false, assignedAt = getTickCount()}
    active = state
    beginObserver(state)
end)

addEvent("nativeBikeRoute:revoke", true)
addEventHandler("nativeBikeRoute:revoke", resourceRoot, function(sessionId, epoch)
    if active and active.sessionId == sessionId and active.epoch == epoch then
        stopNative(true)
        triggerServerEvent("nativeBikeRoute:revoked", resourceRoot, sessionId, epoch)
    end
end)

addEvent("nativeBikeRoute:stop", true)
addEventHandler("nativeBikeRoute:stop", resourceRoot, function(sessionId)
    if active and active.sessionId == sessionId then stopNative(true) end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    stopNative(true)
end)
