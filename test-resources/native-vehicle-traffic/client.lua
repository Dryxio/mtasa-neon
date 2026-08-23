local units = {}
local TASK_NAME = "TASK_COMPLEX_CAR_DRIVE_WANDER"

local function clearTimer(unit, name)
    if isTimer(unit[name]) then killTimer(unit[name]) end
    unit[name] = nil
end

local function report(unit, evidence, data)
    triggerServerEvent("carTraffic:evidence", resourceRoot, unit.id, unit.epoch, evidence, data or {})
end

local function releaseUnit(unit, killTask)
    clearTimer(unit, "retryTimer")
    clearTimer(unit, "monitorTimer")
    clearTimer(unit, "observerTimer")
    if killTask and (unit.accepted or unit.taskQueued) and isElement(unit.ped) then
        killPedTask(unit.ped, "primary", 3, false)
    end
    unit.accepted = false
    unit.taskQueued = false
    if unit.pedLease then
        releaseElementStreamingLease(unit.pedLease)
        unit.pedLease = nil
    end
    if unit.vehicleLease then
        releaseElementStreamingLease(unit.vehicleLease)
        unit.vehicleLease = nil
    end
    if unit.missionActorApplied and isElement(unit.ped) and type(setPedMissionActor) == "function" then
        setPedMissionActor(unit.ped, unit.wasMissionActor == true)
        unit.missionActorApplied = false
    end
end

local function fail(unit, reason)
    report(unit, "failure", {reason = reason})
    releaseUnit(unit, true)
    units[unit.id] = nil
end

local function acceptOwner(unit)
    if units[unit.id] ~= unit or not isElement(unit.ped) or not isElement(unit.vehicle) then return end
    if not isElementSyncer(unit.ped) or not isElementSyncer(unit.vehicle) or getPedOccupiedVehicle(unit.ped) ~= unit.vehicle or
        getPedOccupiedVehicleSeat(unit.ped) ~= 0 then
        return fail(unit, "drive-wander-state-lost")
    end
    if type(isPedDoingTask) ~= "function" then return fail(unit, "task-oracle-missing") end
    if not isPedDoingTask(unit.ped, TASK_NAME) then
        if getTickCount() - (unit.taskStartedAt or unit.requestedAt) < 2500 then
            unit.retryTimer = setTimer(function() acceptOwner(unit) end, 50, 1)
            return
        end
        return fail(unit, "drive-wander-not-installed")
    end
    unit.accepted = true
    unit.acceptedAt = getTickCount()
    report(unit, "accepted", {task = true, seat = getPedOccupiedVehicleSeat(unit.ped)})
    unit.monitorTimer = setTimer(function()
        if units[unit.id] ~= unit or not isElement(unit.ped) or not isElement(unit.vehicle) then return end
        local x, y, z = getElementPosition(unit.vehicle)
        local vx, vy, vz = getElementVelocity(unit.vehicle)
        local px, py, pz = getElementPosition(unit.ped)
        report(unit, "owner-sample", {
            task = type(isPedDoingTask) == "function" and isPedDoingTask(unit.ped, TASK_NAME) == true,
            seat = getPedOccupiedVehicleSeat(unit.ped),
            pedSyncer = isElementSyncer(unit.ped),
            vehicleSyncer = isElementSyncer(unit.vehicle),
            x = x, y = y, z = z, vx = vx, vy = vy, vz = vz,
            pedVehicleDelta = getDistanceBetweenPoints3D(px, py, pz, x, y, z),
        })
    end, 500, 0)
end

local function beginOwner(unit)
    if units[unit.id] ~= unit or not isElement(unit.ped) or not isElement(unit.vehicle) then return end
    if type(acquireElementStreamingLease) ~= "function" or type(setPedDriveWander) ~= "function" then
        return fail(unit, "native-api-missing")
    end
    unit.pedLease = unit.pedLease or acquireElementStreamingLease(unit.ped)
    unit.vehicleLease = unit.vehicleLease or acquireElementStreamingLease(unit.vehicle)
    if not unit.pedLease or not unit.vehicleLease then return fail(unit, "streaming-lease-refused") end

    if not unit.readinessReported then
        unit.readinessReported = true
        triggerServerEvent("carTraffic:clientDiagnostic", resourceRoot, unit.id, unit.epoch, "owner-readiness", {
            pedStreamed = isElementStreamedIn(unit.ped), vehicleStreamed = isElementStreamedIn(unit.vehicle),
            pedSyncer = isElementSyncer(unit.ped), vehicleSyncer = isElementSyncer(unit.vehicle),
            occupied = getPedOccupiedVehicle(unit.ped) == unit.vehicle, seat = getPedOccupiedVehicleSeat(unit.ped),
            pedLease = unit.pedLease ~= nil, vehicleLease = unit.vehicleLease ~= nil,
        })
    end

    -- A seated driver does not use the on-foot native-event profile. That
    -- profile intentionally waits for ground/collision residency, which can
    -- never become ready while the ped is attached to a vehicle seat.
    local ready = isElementStreamedIn(unit.ped) and isElementStreamedIn(unit.vehicle) and isElementSyncer(unit.ped) and
        isElementSyncer(unit.vehicle) and getPedOccupiedVehicle(unit.ped) == unit.vehicle and getPedOccupiedVehicleSeat(unit.ped) == 0
    if not ready then
        if getTickCount() - unit.requestedAt < 10000 then
            unit.retryTimer = setTimer(function() beginOwner(unit) end, 100, 1)
            return
        end
        return fail(unit, "owner-readiness-timeout")
    end

    if not unit.missionActorApplied then
        if type(setPedMissionActor) ~= "function" or type(isPedMissionActor) ~= "function" then
            return fail(unit, "mission-actor-api-missing")
        end
        unit.wasMissionActor = isPedMissionActor(unit.ped) == true
        if not setPedMissionActor(unit.ped, true) then return fail(unit, "mission-actor-refused") end
        unit.missionActorApplied = true
    end

    -- Warping a real driver can leave GTA's generic in-car primary in the
    -- script-command slot. Clear it before installing the authoritative
    -- indefinite Wander task so the event response never chains both roots.
    local cleared = killPedTask(unit.ped, "primary", 3, false)
    local started = setPedDriveWander(unit.ped, unit.vehicle, unit.cruiseSpeed, "stop_for_cars")
    triggerServerEvent("carTraffic:clientDiagnostic", resourceRoot, unit.id, unit.epoch, "drive-wander-returned", {
        started = started == true, cleared = cleared == true,
    })
    if not started then
        return fail(unit, "drive-wander-refused")
    end
    unit.taskStartedAt = getTickCount()
    unit.taskQueued = true
    -- Script-command dispatch installs a clone through GTA's event queue.
    -- Let one task-processing slice pass before acknowledging authority; the
    -- server then proves execution independently through sustained movement.
    unit.retryTimer = setTimer(function() acceptOwner(unit) end, 50, 1)
end

local function beginObserver(unit)
    if units[unit.id] ~= unit or not isElement(unit.ped) or not isElement(unit.vehicle) then return end
    if type(acquireElementStreamingLease) ~= "function" then return fail(unit, "streaming-api-missing") end
    unit.pedLease = unit.pedLease or acquireElementStreamingLease(unit.ped)
    unit.vehicleLease = unit.vehicleLease or acquireElementStreamingLease(unit.vehicle)
    if not unit.pedLease or not unit.vehicleLease then return fail(unit, "observer-lease-refused") end
    if not isElementStreamedIn(unit.ped) or not isElementStreamedIn(unit.vehicle) then
        if getTickCount() - unit.requestedAt < 10000 then
            unit.retryTimer = setTimer(function() beginObserver(unit) end, 100, 1)
            return
        end
        return fail(unit, "observer-stream-timeout")
    end
    report(unit, "observer-ready", {pedSyncer = isElementSyncer(unit.ped), vehicleSyncer = isElementSyncer(unit.vehicle)})
    unit.observerTimer = setTimer(function()
        if units[unit.id] ~= unit or not isElement(unit.ped) or not isElement(unit.vehicle) then return end
        local x, y, z = getElementPosition(unit.vehicle)
        report(unit, "observer-sample", {
            seat = getPedOccupiedVehicleSeat(unit.ped),
            pedSyncer = isElementSyncer(unit.ped),
            vehicleSyncer = isElementSyncer(unit.vehicle),
            x = x, y = y, z = z,
        })
    end, 500, 0)
end

addEvent("carTraffic:observe", true)
addEventHandler("carTraffic:observe", resourceRoot, function(id, epoch, ped, vehicle)
    triggerServerEvent("carTraffic:clientDiagnostic", resourceRoot, id, epoch, "observe-received", {
        ped = isElement(ped), vehicle = isElement(vehicle), pedStreamed = isElement(ped) and isElementStreamedIn(ped) or false,
        vehicleStreamed = isElement(vehicle) and isElementStreamedIn(vehicle) or false,
    })
    local old = units[id]
    if old then releaseUnit(old, old.owner == true) end
    local unit = {id = id, epoch = epoch, ped = ped, vehicle = vehicle, owner = false, requestedAt = getTickCount()}
    units[id] = unit
    beginObserver(unit)
end)

addEvent("carTraffic:assign", true)
addEventHandler("carTraffic:assign", resourceRoot, function(id, epoch, ped, vehicle, cruiseSpeed)
    triggerServerEvent("carTraffic:clientDiagnostic", resourceRoot, id, epoch, "assign-received", {
        ped = isElement(ped), vehicle = isElement(vehicle), pedStreamed = isElement(ped) and isElementStreamedIn(ped) or false,
        vehicleStreamed = isElement(vehicle) and isElementStreamedIn(vehicle) or false,
    })
    local old = units[id]
    if old then releaseUnit(old, old.owner == true) end
    local unit = {
        id = id, epoch = epoch, ped = ped, vehicle = vehicle, owner = true,
        cruiseSpeed = tonumber(cruiseSpeed) or 16.0, requestedAt = getTickCount(),
    }
    units[id] = unit
    beginOwner(unit)
end)

addEvent("carTraffic:revoke", true)
addEventHandler("carTraffic:revoke", resourceRoot, function(id, epoch)
    local unit = units[id]
    if not unit or unit.epoch ~= epoch or not unit.owner then return end
    releaseUnit(unit, true)
    units[id] = nil
    report(unit, "released", {})
end)

addEvent("carTraffic:stop", true)
addEventHandler("carTraffic:stop", resourceRoot, function(id, epoch)
    local unit = units[id]
    if unit and unit.epoch <= epoch then
        releaseUnit(unit, unit.owner == true)
        units[id] = nil
    end
    triggerServerEvent("carTraffic:cleanupAck", resourceRoot, id, epoch)
end)

addEvent("carTraffic:requestCandidate", true)
addEventHandler("carTraffic:requestCandidate", resourceRoot, function(session, model, x, y, z)
    if type(getAmbientVehicleSpawnCandidate) ~= "function" then
        return triggerServerEvent("carTraffic:candidate", resourceRoot, session, false, "api-missing")
    end
    -- Refresh GTA's zone state at the same origin before asking its road oracle.
    -- MTA disables the stock ambient population loop that would normally do it.
    if type(updateAmbientPedPopulationModels) == "function" then
        updateAmbientPedPopulationModels(x, y, z)
    end
    -- GenerateCarCreationCoors2 only chooses road geometry. Hold the requested
    -- stock model while the C++ oracle reads its collision centre-to-base
    -- offset, then release the temporary reference after the scalar result is
    -- captured.
    local modelHeld = false
    local modelReference = false
    if type(engineStreamingRequestModel) == "function" then
        local called = pcall(engineStreamingRequestModel, model, true, true)
        -- The API returns false for an already-held model even though it adds
        -- this resource reference, so successful invocation plus load state is
        -- the reliable contract.
        modelReference = called
        modelHeld = called
    end
    if modelHeld and type(engineStreamingGetModelLoadState) == "function" then
        modelHeld = engineStreamingGetModelLoadState(model) == "loaded"
    end
    if not modelHeld then
        if modelReference and type(engineStreamingReleaseModel) == "function" then
            pcall(engineStreamingReleaseModel, model, true)
        end
        return triggerServerEvent("carTraffic:candidate", resourceRoot, session, false, "model-load-refused")
    end
    local candidate, reason = getAmbientVehicleSpawnCandidate(x, y, z, model)
    if type(engineStreamingReleaseModel) == "function" then
        pcall(engineStreamingReleaseModel, model, true)
    end
    triggerServerEvent("carTraffic:candidate", resourceRoot, session, candidate or false, reason)
end)

addEvent("carTraffic:visibilityProbe", true)
addEventHandler("carTraffic:visibilityProbe", resourceRoot, function(session, x, y, z)
    local visible = true
    if type(isAmbientPedSphereVisible) == "function" then
        visible = isAmbientPedSphereVisible(x, y, z, 5.0) == true
    end
    local px, py, pz = getElementPosition(localPlayer)
    local distance = getDistanceBetweenPoints3D(px, py, pz, x, y, z)
    -- The verified retail oracle has a deliberate 38 m inner ring. A 45 m
    -- veto rejects every valid inner-ring candidate, so retain only the true
    -- near-camera pop-in guard here.
    local tooClose = visible == true and distance < 30.0
    triggerServerEvent("carTraffic:visibility", resourceRoot, session, tooClose, visible == true, distance)
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    for _, unit in pairs(units) do releaseUnit(unit, unit.owner == true) end
    units = {}
end)
