local state

local function releaseLeases()
    if not state then return end
    for _, token in ipairs(state.leases or {}) do
        if token and type(releaseElementStreamingLease) == "function" then
            releaseElementStreamingLease(token)
        end
    end
    state.leases = nil
end

local function report(kind, ...)
    if state then
        triggerServerEvent("nativeEscortDriveBy:evidence", resourceRoot, state.id, state.epoch, state.nonce, kind, ...)
    end
end

local function clearCurrent(killTasks)
    if not state then return end
    if isTimer(state.retry) then killTimer(state.retry) end
    if isTimer(state.monitor) then killTimer(state.monitor) end
    if killTasks then
        if state.driverTask and isElement(state.driver) then killPedTask(state.driver, "primary", 3, false) end
        if state.passengerTask and isElement(state.passenger) then killPedTask(state.passenger, "primary", 3, false) end
    end
    releaseLeases()
    setCameraTarget(localPlayer)
    state = nil
end

local function streamed(s)
    for _, element in ipairs({s.voodoo, s.driver, s.passenger, s.targetVehicle, s.targetPlayer}) do
        if not isElement(element) or not isElementStreamedIn(element) then return false end
    end
    return true
end

local function sample(observer)
    if not state or not streamed(state) then return end
    local vx, vy, vz = getElementPosition(state.voodoo)
    local tx, ty, tz = getElementPosition(state.targetVehicle)
    local distance = getDistanceBetweenPoints3D(vx, vy, vz, tx, ty, tz)
    report(observer and "observer" or "sample", distance, isPedDoingGangDriveby(state.passenger),
           getPedSimplestTask(state.driver) or "", getPedSimplestTask(state.passenger) or "",
           getElementSyncer(state.voodoo) == localPlayer,
           getElementSyncer(state.driver) == localPlayer,
           getElementSyncer(state.passenger) == localPlayer)
end

local function beginOwner()
    if not state then return end
    if type(setPedDriveMission) ~= "function" or type(setPedDriveBy) ~= "function" or type(acquireElementStreamingLease) ~= "function" then
        return report("failure", "required Neon APIs unavailable")
    end
    if not state.leases then
        state.leases = {}
        for _, element in ipairs({state.voodoo, state.driver, state.passenger, state.targetVehicle, state.targetPlayer}) do
            local token = acquireElementStreamingLease(element)
            if not token then return report("failure", "streaming lease rejected") end
            table.insert(state.leases, token)
        end
    end
    if not streamed(state) or getElementSyncer(state.voodoo) ~= localPlayer or getElementSyncer(state.driver) ~= localPlayer or
       getElementSyncer(state.passenger) ~= localPlayer then
        if getTickCount() - state.assignedAt < 10000 then
            state.retry = setTimer(beginOwner, 200, 1)
            return
        end
        return report("failure", "authority cohort did not converge")
    end
    if getPedOccupiedVehicle(state.driver) ~= state.voodoo or getPedOccupiedVehicleSeat(state.driver) ~= 0 or
       getPedOccupiedVehicle(state.passenger) ~= state.voodoo then
        return report("failure", "cohort seats invalid")
    end
    if type(setPedMissionActor) == "function" then
        setPedMissionActor(state.driver, true)
        setPedMissionActor(state.passenger, true)
    end
    state.driverTask = setPedDriveMission(state.driver, state.voodoo, state.targetVehicle, VOODOO_TEST.mission, VOODOO_TEST.speed, VOODOO_TEST.drivingStyle)
    state.passengerTask = setPedDriveBy(state.passenger, state.targetPlayer, VOODOO_TEST.driveByRange, VOODOO_TEST.driveByStyle,
                                        VOODOO_TEST.driveByRhs, VOODOO_TEST.driveByFrequency)
    report("acceptance", state.driverTask, state.passengerTask)
    if not state.driverTask or not state.passengerTask then return end
    setCameraTarget(state.targetPlayer)
    state.monitor = setTimer(function()
        if not state then return end
        if streamed(state) and not isPedDoingGangDriveby(state.passenger) then
            local accepted = setPedDriveBy(state.passenger, state.targetPlayer, VOODOO_TEST.driveByRange, VOODOO_TEST.driveByStyle,
                                           VOODOO_TEST.driveByRhs, VOODOO_TEST.driveByFrequency)
            report("reissue_passenger", accepted)
        end
        sample(false)
    end, 500, 0)
end

local function beginObserver()
    if not state then return end
    setCameraTarget(state.targetPlayer)
    state.monitor = setTimer(function() sample(true) end, 500, 0)
end

addEvent("nativeEscortDriveBy:assign", true)
addEventHandler("nativeEscortDriveBy:assign", resourceRoot, function(id, epoch, nonce, voodoo, driver, passenger, targetVehicle, targetPlayer, owner)
    clearCurrent(true)
    state = {id=id, epoch=epoch, nonce=nonce, voodoo=voodoo, driver=driver, passenger=passenger,
             targetVehicle=targetVehicle, targetPlayer=targetPlayer, owner=owner == true, assignedAt=getTickCount()}
    if state.owner then beginOwner() else beginObserver() end
end)

addEvent("nativeEscortDriveBy:revoke", true)
addEventHandler("nativeEscortDriveBy:revoke", resourceRoot, function(id, epoch, nonce)
    if state and state.id == id and state.epoch == epoch and state.nonce == nonce then
        clearCurrent(true)
        triggerServerEvent("nativeEscortDriveBy:revoked", resourceRoot, id, epoch, nonce)
    end
end)

addEvent("nativeEscortDriveBy:stop", true)
addEventHandler("nativeEscortDriveBy:stop", resourceRoot, function(id)
    if state and state.id == id then clearCurrent(true) end
end)

addEventHandler("onClientResourceStop", resourceRoot, function() clearCurrent(true) end)
