local assignments = {}

local function report(task, evidence, data)
    triggerServerEvent("nativeTaskRuntime:evidence", resourceRoot, task.handle, task.epoch, evidence, data or {})
end

local function clearTimer(task, name)
    if isTimer(task[name]) then
        killTimer(task[name])
    end
    task[name] = nil
end

local function releaseLeases(task)
    if task.pedLease then
        releaseElementStreamingLease(task.pedLease)
        task.pedLease = nil
    end
    if task.vehicleLease then
        releaseElementStreamingLease(task.vehicleLease)
        task.vehicleLease = nil
    end
end

local function stopNativeTask(task, keepForStreamOut)
    clearTimer(task, "retryTimer")
    clearTimer(task, "monitorTimer")
    if task.accepted and isElement(task.ped) then
        killPedTask(task.ped, "primary", 3, false)
    end
    task.accepted = false
    if task.missionActorApplied and isElement(task.ped) and type(setPedMissionActor) == "function" then
        setPedMissionActor(task.ped, task.wasMissionActor)
    end
    task.missionActorApplied = false
    releaseLeases(task)
    if not keepForStreamOut then
        assignments[task.handle] = nil
    end
end

local function fail(task, reason)
    report(task, "failure", {reason = reason})
    stopNativeTask(task, false)
end

local function buildSequence(task)
    local sequence = {}
    for logicalIndex = task.resumeIndex + 1, #task.route do
        local point = task.route[logicalIndex]
        sequence[#sequence + 1] = {
            task = "drive_to",
            x = point.x,
            y = point.y,
            z = point.z,
            speed = point.speed,
            mode = point.mode,
            vehicleModel = point.vehicleModel or getElementModel(task.vehicle),
            drivingStyle = point.drivingStyle,
        }
    end
    return sequence
end

local function beginAssignment(task)
    if assignments[task.handle] ~= task then
        return
    end
    if not isElement(task.ped) or not isElement(task.vehicle) then
        return fail(task, "ped ou vehicule absent au client")
    end
    if type(acquireElementStreamingLease) ~= "function" or type(releaseElementStreamingLease) ~= "function" or
        type(setPedTaskSequence) ~= "function" or type(getPedTaskSequenceProgress) ~= "function" then
        return fail(task, "API native task ou streaming lease absente")
    end

    if not task.pedLease then task.pedLease = acquireElementStreamingLease(task.ped) end
    if not task.vehicleLease then task.vehicleLease = acquireElementStreamingLease(task.vehicle) end
    if not task.pedLease or not task.vehicleLease then
        return fail(task, "acquisition du double lease refusee")
    end

    if not isElementStreamedIn(task.ped) or not isElementStreamedIn(task.vehicle) or not isElementSyncer(task.ped) or
        not isElementSyncer(task.vehicle) or getPedOccupiedVehicle(task.ped) ~= task.vehicle or getPedOccupiedVehicleSeat(task.ped) ~= 0 then
        if getTickCount() - task.requestedAt < 10000 then
            clearTimer(task, "retryTimer")
            task.retryTimer = setTimer(function()
                beginAssignment(task)
            end, 200, 1)
            return
        end
        return fail(task, "double stream, ownership ou siege conducteur absent apres 10 s")
    end

    if type(setPedMissionActor) ~= "function" or type(isPedMissionActor) ~= "function" then
        return fail(task, "API mission actor absente")
    end
    task.wasMissionActor = isPedMissionActor(task.ped)
    if not setPedMissionActor(task.ped, true) then
        return fail(task, "PED_MISSION refuse")
    end
    task.missionActorApplied = true

    if task.options.loadCollision ~= nil then
        if type(setVehicleLoadCollisionFlag) ~= "function" or
            not setVehicleLoadCollisionFlag(task.vehicle, task.options.loadCollision == true) then
            return fail(task, "SET_LOAD_COLLISION_FOR_CAR_FLAG refuse")
        end
    end

    local sequence = buildSequence(task)
    if #sequence == 0 then
        report(task, "completed")
        return stopNativeTask(task, false)
    end

    task.startedAt = getTickCount()
    task.accepted = setPedTaskSequence(task.ped, sequence, false)
    report(task, "accepted", {accepted = task.accepted == true, routeIndex = task.resumeIndex})
    if not task.accepted then
        return stopNativeTask(task, false)
    end

    task.wasActive = false
    task.lastLogicalIndex = task.resumeIndex
    task.monitorTimer = setTimer(function()
        if assignments[task.handle] ~= task or not isElement(task.ped) or not isElement(task.vehicle) then
            return fail(task, "elements absents pendant la sequence")
        end
        local localIndex = getPedTaskSequenceProgress(task.ped)
        if localIndex >= 0 then
            task.wasActive = true
            task.lastLogicalIndex = math.max(task.lastLogicalIndex, task.resumeIndex + localIndex)
            report(task, "sample", {routeIndex = task.lastLogicalIndex, active = true})
        elseif task.wasActive then
            report(task, "completed", {routeIndex = task.lastLogicalIndex})
            stopNativeTask(task, false)
        end
    end, 500, 0)
end

addEvent("nativeTaskRuntime:assign", true)
addEventHandler("nativeTaskRuntime:assign", resourceRoot, function(handle, epoch, ped, vehicle, route, resumeIndex, options)
    if not isElement(handle) or not isElement(ped) or not isElement(vehicle) then
        outputDebugString(("[native task runtime] unresolved assignment epoch=%s handle=%s ped=%s vehicle=%s"):format(
                              tostring(epoch), tostring(isElement(handle)), tostring(isElement(ped)),
                              tostring(isElement(vehicle))), 2)
        return
    end
    local old = assignments[handle]
    if old then
        if old.epoch == epoch then
            outputDebugString(("[native task runtime] duplicate assignment ignored epoch=%d"):format(epoch))
            return
        end
        stopNativeTask(old, false)
    end
    local task = {
        handle = handle,
        epoch = epoch,
        ped = ped,
        vehicle = vehicle,
        route = route,
        resumeIndex = resumeIndex,
        options = type(options) == "table" and options or {},
        requestedAt = getTickCount(),
        streamedOutPed = false,
        streamedOutVehicle = false,
    }
    assignments[handle] = task
    outputDebugString(("[native task runtime] assignment received epoch=%d resume=%d route=%d"):format(
                          epoch, resumeIndex, #route))
    beginAssignment(task)
end)

addEvent("nativeTaskRuntime:revoke", true)
addEventHandler("nativeTaskRuntime:revoke", resourceRoot, function(handle, epoch, requireStreamOut)
    local task = assignments[handle]
    if not task or task.epoch ~= epoch then
        return
    end
    task.awaitingStreamOut = requireStreamOut == true
    stopNativeTask(task, task.awaitingStreamOut)
    report(task, "released", {awaitingStreamOut = task.awaitingStreamOut})
    if not task.awaitingStreamOut then
        assignments[handle] = nil
    end
end)

addEvent("nativeTaskRuntime:stop", true)
addEventHandler("nativeTaskRuntime:stop", resourceRoot, function(handle, epoch)
    local task = assignments[handle]
    if task and task.epoch == epoch then
        stopNativeTask(task, false)
    end
end)

addEventHandler("onClientElementStreamOut", root, function()
    for _, task in pairs(assignments) do
        if task.awaitingStreamOut then
            local kind
            if source == task.ped and not task.streamedOutPed then
                task.streamedOutPed = true
                kind = "ped"
            elseif source == task.vehicle and not task.streamedOutVehicle then
                task.streamedOutVehicle = true
                kind = "vehicle"
            end
            if kind then
                report(task, "streamout", {element = kind})
            end
        end
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    local current = {}
    for _, task in pairs(assignments) do
        current[#current + 1] = task
    end
    for _, task in ipairs(current) do
        stopNativeTask(task, false)
    end
end)
