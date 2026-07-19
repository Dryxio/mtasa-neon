local activeTest

local function stopTest(killTask)
    local test = activeTest
    if not test then
        return
    end
    if isTimer(test.retryTimer) then
        killTimer(test.retryTimer)
    end
    if isTimer(test.monitorTimer) then
        killTimer(test.monitorTimer)
    end
    if killTask and test.accepted and isElement(test.ped) then
        killPedTask(test.ped, "primary", 3, false)
    end
    if test.missionActorApplied and isElement(test.ped) and type(setPedMissionActor) == "function" then
        setPedMissionActor(test.ped, test.wasMissionActor)
    end
    activeTest = nil
end

local function report(evidence, a, b, c, d, e)
    local test = activeTest
    if not test or not isElement(test.ped) or not isElement(test.vehicle) then
        return false
    end
    triggerServerEvent("nativeDriveRoute:evidence", resourceRoot, test.id, test.ped, test.vehicle, evidence, a, b, c, d, e)
    return true
end

local function nearestRoutePoint(x, y, z)
    local nearestIndex, nearestDistance
    for index, point in ipairs(NATIVE_DRIVE_ROUTE) do
        local distance = getDistanceBetweenPoints3D(x, y, z, point[1], point[2], point[3])
        if not nearestDistance or distance < nearestDistance then
            nearestIndex, nearestDistance = index - 1, distance
        end
    end
    return nearestIndex, nearestDistance
end

local function beginRoute()
    local test = activeTest
    if not test then
        return
    end
    if not isElement(test.ped) or not isElement(test.vehicle) then
        report("failure", "ped ou vehicule detruit avant dispatch")
        return stopTest(false)
    end
    if not isElementStreamedIn(test.ped) or not isElementStreamedIn(test.vehicle) or not isElementSyncer(test.ped) or not isElementSyncer(test.vehicle) then
        if getTickCount() - test.requestedAt < 8000 then
            test.retryTimer = setTimer(beginRoute, 250, 1)
            return
        end
        report("failure", "double stream/ownership absent apres 8 s")
        return stopTest(false)
    end
    if getPedOccupiedVehicle(test.ped) ~= test.vehicle or getPedOccupiedVehicleSeat(test.ped) ~= 0 then
        report("failure", "Ballas n'est pas conducteur du Voodoo")
        return stopTest(false)
    end
    if type(setPedTaskSequence) ~= "function" or type(getPedTaskSequenceProgress) ~= "function" then
        report("failure", "API de sequence absente du client Neon")
        return stopTest(false)
    end
    if type(setPedMissionActor) ~= "function" or type(isPedMissionActor) ~= "function" then
        report("failure", "API mission-actor absente du client Neon")
        return stopTest(false)
    end

    test.wasMissionActor = isPedMissionActor(test.ped)
    if not setPedMissionActor(test.ped, true) then
        report("failure", "PED_MISSION refuse")
        return stopTest(false)
    end
    test.missionActorApplied = true

    local sequence = {}
    for index, point in ipairs(NATIVE_DRIVE_ROUTE) do
        sequence[index] = {
            task = "drive_to",
            x = point[1],
            y = point[2],
            z = point[3],
            speed = point[4],
            mode = "normal",
            vehicleModel = 412,
            drivingStyle = "avoid_cars",
        }
    end

    test.startedAt = getTickCount()
    test.accepted = setPedTaskSequence(test.ped, sequence, false)
    report("acceptance", test.accepted)
    if not test.accepted then
        return stopTest(false)
    end

    test.lastIndex = nil
    test.samples = 0
    test.monitorTimer = setTimer(function()
        local current = activeTest
        if not current or not isElement(current.ped) or not isElement(current.vehicle) then
            report("failure", "elements detruits pendant la route")
            return stopTest(false)
        end

        local elapsed = getTickCount() - current.startedAt
        local index = getPedTaskSequenceProgress(current.ped)
        if index ~= current.lastIndex then
            current.lastIndex = index
            report("index", index, elapsed)
        end

        current.samples = current.samples + 1
        if current.samples % 5 == 0 then
            local x, y, z = getElementPosition(current.vehicle)
            local nearestIndex, nearestDistance = nearestRoutePoint(x, y, z)
            report("position", x, y, z, nearestIndex, nearestDistance)
        end
    end, 1000, 0)
end

addEvent("nativeDriveRoute:start", true)
addEventHandler("nativeDriveRoute:start", resourceRoot, function(sessionId, ped, vehicle)
    stopTest(true)
    activeTest = {id = sessionId, ped = ped, vehicle = vehicle, requestedAt = getTickCount(), accepted = false}
    beginRoute()
end)

addEvent("nativeDriveRoute:stop", true)
addEventHandler("nativeDriveRoute:stop", resourceRoot, function(sessionId, ped, vehicle)
    local test = activeTest
    if test and test.id == sessionId and test.ped == ped and test.vehicle == vehicle then
        stopTest(false)
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    stopTest(true)
end)
