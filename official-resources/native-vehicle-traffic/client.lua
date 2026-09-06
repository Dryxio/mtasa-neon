local units = {}
local releasedProofs = {}
local TASK_NAME = "TASK_COMPLEX_CAR_DRIVE_WANDER"
-- A native road candidate has yaw but no slope orientation. The server drops
-- new hidden vehicles slightly above the road; keep staging them until GTA's
-- suspension reports a stable ground contact before adding forward velocity.
local INITIAL_PLACEMENT_STABLE_SAMPLES = 3
local INITIAL_PLACEMENT_TIMEOUT = 2500
local INITIAL_PLACEMENT_VERTICAL_SPEED = 0.025
local REVEAL_VISIBLE_MIN_DISTANCE = 70.0

local function clearTimer(unit, name)
    if isTimer(unit[name]) then killTimer(unit[name]) end
    unit[name] = nil
end

local function report(unit, evidence, data)
    if evidence == "owner-sample" or evidence == "observer-sample" then
        VehicleTrafficTransport.enqueue(unit, evidence, data or {})
    else
        triggerServerEvent("carTraffic:evidence", resourceRoot, unit.id, unit.epoch, evidence, data or {})
    end
end

local function sendDiagnostic(id, epoch, stage, data)
    if VehicleTrafficTransport.diagnosticsEnabled then
        triggerServerEvent("carTraffic:clientDiagnostic", resourceRoot, id, epoch, stage, data)
    end
end

addEvent("carTraffic:diagnostics", true)
addEventHandler("carTraffic:diagnostics", resourceRoot, function(enabled)
    VehicleTrafficTransport.diagnosticsEnabled = enabled == true
end)

setTimer(function()
    VehicleTrafficTransport.flush(function(unit)
        return units[unit.id] == unit and isElement(unit.ped) and isElement(unit.vehicle)
    end, function(batch)
        triggerServerEvent("carTraffic:evidenceBatch", resourceRoot, batch)
    end)
end, 500, 0)

local function nativeAutoPilotDiagnostic(vehicle)
    if type(getVehicleNativeAutoPilotDiagnostic) ~= "function" or not isElement(vehicle) then return false end
    local ok, diagnostic = pcall(getVehicleNativeAutoPilotDiagnostic, vehicle)
    return ok and type(diagnostic) == "table" and diagnostic or false
end

local function normalizePassengers(passengers, driver)
    local result = {}
    local seen = {}
    local seenSeats = {}
    if isElement(driver) then seen[driver] = true end
    if passengers == nil then return result, true end
    if type(passengers) ~= "table" then return result, false end
    for index, descriptor in ipairs(passengers) do
        local ped = isElement(descriptor) and descriptor or type(descriptor) == "table" and (descriptor.ped or descriptor[1]) or nil
        local seat = type(descriptor) == "table" and tonumber(descriptor.seat or descriptor[2]) or index
        seat = seat and math.floor(seat) or nil
        if not isElement(ped) or getElementType(ped) ~= "ped" or seen[ped] or not seat or seat < 1 or seenSeats[seat] then return result, false end
        seen[ped] = true
        seenSeats[seat] = true
        result[#result + 1] = {ped = ped, seat = seat}
    end
    return result, true
end

local function passengerSamples(unit)
    local samples = {}
    local vehicleValid = isElement(unit.vehicle)
    local vx, vy, vz
    if vehicleValid then vx, vy, vz = getElementPosition(unit.vehicle) end
    for index, passenger in ipairs(unit.passengers) do
        local ped = passenger.ped
        local px, py, pz
        if isElement(ped) then px, py, pz = getElementPosition(ped) end
        samples[index] = {
            ped = ped,
            expectedSeat = passenger.seat,
            seat = isElement(ped) and getPedOccupiedVehicleSeat(ped) or -1,
            occupied = vehicleValid and isElement(ped) and getPedOccupiedVehicle(ped) == unit.vehicle or false,
            syncer = isElement(ped) and isElementSyncer(ped) or false,
            streamed = isElement(ped) and isElementStreamedIn(ped) or false,
            vehicleDelta = vehicleValid and px and py and pz and getDistanceBetweenPoints3D(px, py, pz, vx, vy, vz) or false,
        }
    end
    return samples
end

local function passengersReady(unit, requireSyncer)
    for _, passenger in ipairs(unit.passengers) do
        local ped = passenger.ped
        if not isElement(ped) or not isElementStreamedIn(ped) or getPedOccupiedVehicle(ped) ~= unit.vehicle or
            getPedOccupiedVehicleSeat(ped) ~= passenger.seat or requireSyncer and not isElementSyncer(ped) then
            return false
        end
    end
    return true
end

local function applyPassengerMissionActors(unit)
    if #unit.passengers == 0 then return true end
    if type(setPedMissionActor) ~= "function" or type(isPedMissionActor) ~= "function" then return false end
    unit.passengerMissionActors = unit.passengerMissionActors or {}
    for _, passenger in ipairs(unit.passengers) do
        local ped = passenger.ped
        local state = unit.passengerMissionActors[ped]
        if not state then
            state = {wasMissionActor = isPedMissionActor(ped) == true, applied = false}
            unit.passengerMissionActors[ped] = state
            if not setPedMissionActor(ped, true) then return false end
            state.applied = true
        end
    end
    return true
end

local function normalizeResumeKinematics(value)
    if value == nil then return nil, false end
    if type(value) ~= "table" then return nil, false end
    local names = {"vx", "vy", "vz", "avx", "avy", "avz"}
    local result = {}
    for _, name in ipairs(names) do
        local number = tonumber(value[name])
        if not number or number ~= number then return nil, true end
        local bound = name:sub(1, 1) == "a" and 1.0 or 3.0
        if math.abs(number) > bound then return nil, true end
        result[name] = number
    end
    if result.vx * result.vx + result.vy * result.vy + result.vz * result.vz > 9.0 or
        result.avx * result.avx + result.avy * result.avy + result.avz * result.avz > 1.0 then
        return nil, true
    end
    return result, true
end

local function releaseUnit(unit, killTask)
    local proof = {
        epoch = unit.epoch,
        hadUnit = true,
        taskWasQueued = unit.accepted == true or unit.taskQueued == true,
        taskKillAccepted = true,
        taskStopped = true,
        hadPedLease = unit.pedLease ~= nil,
        hadVehicleLease = unit.vehicleLease ~= nil,
        pedLeaseReleased = true,
        vehicleLeaseReleased = true,
        passengerLeaseTokens = 0,
        passengerLeasesReleasedCount = 0,
        passengerLeasesReleased = true,
        passengerMissionActorPolicies = 0,
        passengerMissionActorPoliciesRestored = 0,
        passengerMissionActorsRestored = true,
        missionActorRestored = true,
        releasedAt = getTickCount(),
    }
    clearTimer(unit, "retryTimer")
    clearTimer(unit, "monitorTimer")
    clearTimer(unit, "observerTimer")
    if killTask and (unit.accepted or unit.taskQueued) and isElement(unit.ped) then
        proof.taskKillAccepted = killPedTask(unit.ped, "primary", 3, false) == true
    end
    proof.taskStopped = not isElement(unit.ped) or type(isPedDoingTask) ~= "function" or not isPedDoingTask(unit.ped, TASK_NAME)
    unit.accepted = false
    unit.taskQueued = false
    if unit.pedLease then
        proof.pedLeaseReleased = releaseElementStreamingLease(unit.pedLease) == true
        unit.pedLease = nil
    end
    if unit.vehicleLease then
        proof.vehicleLeaseReleased = releaseElementStreamingLease(unit.vehicleLease) == true
        unit.vehicleLease = nil
    end
    for ped, lease in pairs(unit.passengerLeases or {}) do
        if lease then
            proof.passengerLeaseTokens = proof.passengerLeaseTokens + 1
            if releaseElementStreamingLease(lease) == true then
                proof.passengerLeasesReleasedCount = proof.passengerLeasesReleasedCount + 1
            else
                proof.passengerLeasesReleased = false
            end
        end
        unit.passengerLeases[ped] = nil
    end
    for ped, state in pairs(unit.passengerMissionActors or {}) do
        if state.applied then
            proof.passengerMissionActorPolicies = proof.passengerMissionActorPolicies + 1
            if not isElement(ped) or setPedMissionActor(ped, state.wasMissionActor == true) == true then
                proof.passengerMissionActorPoliciesRestored = proof.passengerMissionActorPoliciesRestored + 1
            else
                proof.passengerMissionActorsRestored = false
            end
        end
        unit.passengerMissionActors[ped] = nil
    end
    if unit.missionActorApplied and isElement(unit.ped) and type(setPedMissionActor) == "function" then
        proof.missionActorRestored = setPedMissionActor(unit.ped, unit.wasMissionActor == true) == true
        unit.missionActorApplied = false
    end
    proof.passengerLeaseRegistryEmpty = not next(unit.passengerLeases or {})
    proof.passengerMissionActorRegistryEmpty = not next(unit.passengerMissionActors or {})
    proof.missionActorRestored = proof.missionActorRestored and proof.passengerMissionActorsRestored and proof.passengerMissionActorRegistryEmpty
    proof.leasesReleased = proof.pedLeaseReleased and proof.vehicleLeaseReleased and proof.passengerLeasesReleased
    return proof
end

local function fail(unit, reason)
    report(unit, "failure", {reason = reason})
    local proof = releaseUnit(unit, true)
    units[unit.id] = nil
    proof.registryEmpty = units[unit.id] == nil
    releasedProofs[unit.id] = releasedProofs[unit.id] or {}
    releasedProofs[unit.id][unit.epoch] = proof
end

local function acceptOwner(unit)
    if units[unit.id] ~= unit or not isElement(unit.ped) or not isElement(unit.vehicle) then return end
    if not isElementSyncer(unit.ped) or not isElementSyncer(unit.vehicle) or getPedOccupiedVehicle(unit.ped) ~= unit.vehicle or
        getPedOccupiedVehicleSeat(unit.ped) ~= 0 or not passengersReady(unit, true) then
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
    local initialPlacementGrounded = false
    local initialPlacementSettleMs = 0
    local initialPlacementStableSamples = 0
    if unit.initialVelocityRequested then
        if type(isVehicleOnGround) ~= "function" then return fail(unit, "vehicle-ground-oracle-missing") end
        unit.initialPlacementStartedAt = unit.initialPlacementStartedAt or getTickCount()
        initialPlacementSettleMs = getTickCount() - unit.initialPlacementStartedAt
        local _, _, verticalSpeed = getElementVelocity(unit.vehicle)
        local groundedNow = isVehicleOnGround(unit.vehicle) == true and
            math.abs(tonumber(verticalSpeed) or math.huge) <= INITIAL_PLACEMENT_VERTICAL_SPEED
        unit.initialPlacementStableSamples = groundedNow and (unit.initialPlacementStableSamples or 0) + 1 or 0
        initialPlacementStableSamples = unit.initialPlacementStableSamples
        initialPlacementGrounded = initialPlacementStableSamples >= INITIAL_PLACEMENT_STABLE_SAMPLES
        if not initialPlacementGrounded then
            if initialPlacementSettleMs < INITIAL_PLACEMENT_TIMEOUT then
                unit.retryTimer = setTimer(function() acceptOwner(unit) end, 50, 1)
                return
            end
            return fail(unit, "initial-placement-timeout")
        end
    end
    local initialVelocityApplied = false
    local initialSpeed = 0
    if unit.initialVelocityRequested and not unit.initialVelocityAttempted then
        unit.initialVelocityAttempted = true
        local matrix = getElementMatrix(unit.vehicle)
        local forward = type(matrix) == "table" and matrix[2]
        local fx = type(forward) == "table" and tonumber(forward[1])
        local fy = type(forward) == "table" and tonumber(forward[2])
        local fz = type(forward) == "table" and tonumber(forward[3])
        local length = fx and fy and fz and math.sqrt(fx * fx + fy * fy + fz * fz) or 0
        if length > 0.001 then
            -- GTA cruise speed is expressed in world units per second, while
            -- MTA vehicle velocity is world units per 50 Hz physics step.
            initialSpeed = math.max(0, math.min(tonumber(unit.cruiseSpeed) or 0, 30)) / 50
            initialVelocityApplied = setElementVelocity(unit.vehicle, fx / length * initialSpeed, fy / length * initialSpeed,
                fz / length * initialSpeed) == true
        end
        unit.initialVelocityApplied = initialVelocityApplied
        unit.initialSpeed = initialSpeed
    elseif unit.initialVelocityRequested then
        initialVelocityApplied = unit.initialVelocityApplied == true
        initialSpeed = unit.initialSpeed or 0
    end
    local resumeApplied = false
    if not unit.resumeAttempted then
        unit.resumeAttempted = true
        if unit.resumeKinematics then
            local velocityApplied = setElementVelocity(unit.vehicle, unit.resumeKinematics.vx, unit.resumeKinematics.vy, unit.resumeKinematics.vz) == true
            local angularVelocityApplied =
                setElementAngularVelocity(unit.vehicle, unit.resumeKinematics.avx, unit.resumeKinematics.avy, unit.resumeKinematics.avz) == true
            resumeApplied = velocityApplied and angularVelocityApplied
        end
        unit.resumeApplied = resumeApplied
        unit.resumeAppliedAt = getTickCount()
    else
        resumeApplied = unit.resumeApplied == true
    end
    unit.accepted = true
    unit.acceptedAt = getTickCount()
    local x, y, z = getElementPosition(unit.vehicle)
    local rx, ry, rz = getElementRotation(unit.vehicle)
    local vx, vy, vz = getElementVelocity(unit.vehicle)
    local avx, avy, avz = getElementAngularVelocity(unit.vehicle)
    local resumeDelay = (unit.resumeAppliedAt or unit.acceptedAt) - unit.requestedAt
    report(unit, "accepted", {
        task = true, seat = getPedOccupiedVehicleSeat(unit.ped), passengers = passengerSamples(unit),
        resumeProvided = unit.resumeProvided, resumeValid = unit.resumeKinematics ~= nil, resumeApplied = resumeApplied,
        initialVelocityRequested = unit.initialVelocityRequested, initialVelocityApplied = initialVelocityApplied,
        initialSpeed = initialSpeed, initialPlacementGrounded = initialPlacementGrounded,
        initialPlacementSettleMs = initialPlacementSettleMs, initialPlacementStableSamples = initialPlacementStableSamples,
        resumeDelay = resumeDelay, resumeDelayMs = resumeDelay,
        x = x, y = y, z = z, rx = rx, ry = ry, rz = rz, vx = vx, vy = vy, vz = vz, avx = avx, avy = avy, avz = avz,
        oracle = unit.oracleDiagnostic, nativeAutoPilot = nativeAutoPilotDiagnostic(unit.vehicle),
    })
    unit.monitorTimer = setTimer(function()
        if units[unit.id] ~= unit or not isElement(unit.ped) or not isElement(unit.vehicle) then return end
        local x, y, z = getElementPosition(unit.vehicle)
        local vx, vy, vz = getElementVelocity(unit.vehicle)
        local rx, ry, rz = getElementRotation(unit.vehicle)
        local avx, avy, avz = getElementAngularVelocity(unit.vehicle)
        local px, py, pz = getElementPosition(unit.ped)
        local nativeAutoPilot = VehicleTrafficTransport.diagnosticsEnabled and nativeAutoPilotDiagnostic(unit.vehicle)
        unit.ownerSeq = unit.ownerSeq + 1
        if type(nativeAutoPilot) == "table" then
            local signature = table.concat({
                tostring(nativeAutoPilot.currentAddressArea), tostring(nativeAutoPilot.currentAddressNode),
                tostring(nativeAutoPilot.startingAddressArea), tostring(nativeAutoPilot.startingAddressNode),
                tostring(nativeAutoPilot.currentLane), tostring(nativeAutoPilot.nextLane),
                tostring(nativeAutoPilot.laneChangeCounter), tostring(nativeAutoPilot.roadJoinSequence),
            }, ":")
            if signature ~= unit.lastNativeAutoPilotSignature then
                unit.lastNativeAutoPilotSignature = signature
                sendDiagnostic(unit.id, unit.epoch, "native-autopilot-change", {
                    seq = unit.ownerSeq, x = x, y = y, z = z, oracle = unit.oracleDiagnostic, nativeAutoPilot = nativeAutoPilot,
                })
            end
        end
        report(unit, "owner-sample", {
            seq = unit.ownerSeq, capturedAt = getTickCount(),
            task = type(isPedDoingTask) == "function" and isPedDoingTask(unit.ped, TASK_NAME) == true,
            seat = getPedOccupiedVehicleSeat(unit.ped),
            pedSyncer = isElementSyncer(unit.ped),
            vehicleSyncer = isElementSyncer(unit.vehicle),
            x = x, y = y, z = z, rx = rx, ry = ry, rz = rz, vx = vx, vy = vy, vz = vz, avx = avx, avy = avy, avz = avz,
            pedVehicleDelta = getDistanceBetweenPoints3D(px, py, pz, x, y, z),
            passengers = passengerSamples(unit),
        })
    end, 500, 0)
end

local function beginOwner(unit)
    if units[unit.id] ~= unit or not isElement(unit.ped) or not isElement(unit.vehicle) then return end
    if not unit.passengerContractValid then return fail(unit, "passenger-contract-invalid") end
    if type(acquireElementStreamingLease) ~= "function" or type(setPedDriveWander) ~= "function" or
        type(setVehicleLoadCollisionFlag) ~= "function" then
        return fail(unit, "native-api-missing")
    end
    unit.pedLease = unit.pedLease or acquireElementStreamingLease(unit.ped)
    unit.vehicleLease = unit.vehicleLease or acquireElementStreamingLease(unit.vehicle)
    if not unit.pedLease or not unit.vehicleLease then return fail(unit, "streaming-lease-refused") end
    unit.passengerLeases = unit.passengerLeases or {}
    for _, passenger in ipairs(unit.passengers) do
        unit.passengerLeases[passenger.ped] = unit.passengerLeases[passenger.ped] or acquireElementStreamingLease(passenger.ped)
        if not unit.passengerLeases[passenger.ped] then return fail(unit, "passenger-streaming-lease-refused") end
    end

    if not unit.readinessReported then
        unit.readinessReported = true
        sendDiagnostic(unit.id, unit.epoch, "owner-readiness", {
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
        isElementSyncer(unit.vehicle) and getPedOccupiedVehicle(unit.ped) == unit.vehicle and getPedOccupiedVehicleSeat(unit.ped) == 0 and
        passengersReady(unit, true)
    if not ready then
        if getTickCount() - unit.requestedAt < 10000 then
            unit.retryTimer = setTimer(function() beginOwner(unit) end, 100, 1)
            return
        end
        return fail(unit, "owner-readiness-timeout")
    end
    if not applyPassengerMissionActors(unit) then return fail(unit, "passenger-mission-actor-refused") end

    if not unit.missionActorApplied then
        if type(setPedMissionActor) ~= "function" or type(isPedMissionActor) ~= "function" then
            return fail(unit, "mission-actor-api-missing")
        end
        unit.wasMissionActor = isPedMissionActor(unit.ped) == true
        if not setPedMissionActor(unit.ped, true) then return fail(unit, "mission-actor-refused") end
        unit.missionActorApplied = true
    end

    -- Traffic remains alive outside the player's world-collision radius. Use
    -- GTA's mission-car policy so an authoritative vehicle becomes a ghost
    -- instead of falling when its road collision unloads, then returns to
    -- physics and is placed back on the road when collision becomes resident.
    if not setVehicleLoadCollisionFlag(unit.vehicle, false) then
        return fail(unit, "vehicle-collision-policy-refused")
    end

    -- Warping a real driver can leave GTA's generic in-car primary in the
    -- script-command slot. Clear it before installing the authoritative
    -- indefinite Wander task so the event response never chains both roots.
    local cleared = killPedTask(unit.ped, "primary", 3, false)
    local drivingStyleName = unit.drivingStyle == 6 and "avoid_cars_stop_for_peds_obey_lights" or "stop_for_cars"
    sendDiagnostic(unit.id, unit.epoch, "drive-wander-before", {
        oracle = unit.oracleDiagnostic, nativeAutoPilot = nativeAutoPilotDiagnostic(unit.vehicle),
    })
    local started = setPedDriveWander(unit.ped, unit.vehicle, unit.cruiseSpeed, drivingStyleName)
    sendDiagnostic(unit.id, unit.epoch, "drive-wander-returned", {
        started = started == true, cleared = cleared == true, drivingStyle = unit.drivingStyle, drivingStyleName = drivingStyleName,
        remoteCollisionGhostPolicy = true, oracle = unit.oracleDiagnostic,
        nativeAutoPilot = nativeAutoPilotDiagnostic(unit.vehicle),
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
    if not unit.passengerContractValid then return fail(unit, "passenger-contract-invalid") end
    if type(acquireElementStreamingLease) ~= "function" then return fail(unit, "streaming-api-missing") end
    unit.pedLease = unit.pedLease or acquireElementStreamingLease(unit.ped)
    unit.vehicleLease = unit.vehicleLease or acquireElementStreamingLease(unit.vehicle)
    if not unit.pedLease or not unit.vehicleLease then return fail(unit, "observer-lease-refused") end
    unit.passengerLeases = unit.passengerLeases or {}
    for _, passenger in ipairs(unit.passengers) do
        unit.passengerLeases[passenger.ped] = unit.passengerLeases[passenger.ped] or acquireElementStreamingLease(passenger.ped)
        if not unit.passengerLeases[passenger.ped] then return fail(unit, "passenger-observer-lease-refused") end
    end
    if not isElementStreamedIn(unit.ped) or not isElementStreamedIn(unit.vehicle) or not passengersReady(unit, false) then
        if getTickCount() - unit.requestedAt < 10000 then
            unit.retryTimer = setTimer(function() beginObserver(unit) end, 100, 1)
            return
        end
        return fail(unit, "observer-stream-timeout")
    end
    if not applyPassengerMissionActors(unit) then return fail(unit, "passenger-mission-actor-refused") end
    report(unit, "observer-ready", {
        pedSyncer = isElementSyncer(unit.ped), vehicleSyncer = isElementSyncer(unit.vehicle), passengers = passengerSamples(unit),
    })
    unit.observerTimer = setTimer(function()
        if units[unit.id] ~= unit or not isElement(unit.ped) or not isElement(unit.vehicle) then return end
        local x, y, z = getElementPosition(unit.vehicle)
        local rx, ry, rz = getElementRotation(unit.vehicle)
        local vx, vy, vz = getElementVelocity(unit.vehicle)
        unit.observerSeq = unit.observerSeq + 1
        report(unit, "observer-sample", {
            seq = unit.observerSeq, capturedAt = getTickCount(),
            seat = getPedOccupiedVehicleSeat(unit.ped),
            pedSyncer = isElementSyncer(unit.ped),
            vehicleSyncer = isElementSyncer(unit.vehicle),
            x = x, y = y, z = z, rx = rx, ry = ry, rz = rz, vx = vx, vy = vy, vz = vz,
            passengers = passengerSamples(unit),
        })
    end, 500, 0)
end

addEvent("carTraffic:observe", true)
addEventHandler("carTraffic:observe", resourceRoot, function(id, epoch, ped, vehicle, passengers)
    sendDiagnostic(id, epoch, "observe-received", {
        ped = isElement(ped), vehicle = isElement(vehicle), pedStreamed = isElement(ped) and isElementStreamedIn(ped) or false,
        vehicleStreamed = isElement(vehicle) and isElementStreamedIn(vehicle) or false,
    })
    local old = units[id]
    if old then releaseUnit(old, old.owner == true) end
    local normalizedPassengers, passengerContractValid = normalizePassengers(passengers, ped)
    local unit = {
        id = id, epoch = epoch, ped = ped, vehicle = vehicle, owner = false, requestedAt = getTickCount(),
        passengers = normalizedPassengers, passengerContractValid = passengerContractValid, observerSeq = 0, ownerSeq = 0,
    }
    releasedProofs[id] = nil
    units[id] = unit
    beginObserver(unit)
end)

addEvent("carTraffic:assign", true)
addEventHandler("carTraffic:assign", resourceRoot, function(id, epoch, ped, vehicle, cruiseSpeed, passengers, drivingStyle, resumeKinematics,
                                                             initialVelocityRequested, oracleDiagnostic)
    sendDiagnostic(id, epoch, "assign-received", {
        ped = isElement(ped), vehicle = isElement(vehicle), pedStreamed = isElement(ped) and isElementStreamedIn(ped) or false,
        vehicleStreamed = isElement(vehicle) and isElementStreamedIn(vehicle) or false,
    })
    local old = units[id]
    if old then releaseUnit(old, old.owner == true) end
    local normalizedPassengers, passengerContractValid = normalizePassengers(passengers, ped)
    local normalizedResume, resumeProvided = normalizeResumeKinematics(resumeKinematics)
    local unit = {
        id = id, epoch = epoch, ped = ped, vehicle = vehicle, owner = true,
        cruiseSpeed = tonumber(cruiseSpeed) or 16.0, requestedAt = getTickCount(),
        drivingStyle = tonumber(drivingStyle) == 6 and 6 or 0,
        resumeKinematics = normalizedResume, resumeProvided = resumeProvided,
        initialVelocityRequested = initialVelocityRequested == true,
        oracleDiagnostic = type(oracleDiagnostic) == "table" and oracleDiagnostic or false,
        passengers = normalizedPassengers, passengerContractValid = passengerContractValid, observerSeq = 0, ownerSeq = 0,
    }
    releasedProofs[id] = nil
    units[id] = unit
    beginOwner(unit)
end)

addEvent("carTraffic:testResumeDrive", true)
addEventHandler("carTraffic:testResumeDrive", resourceRoot, function(id, epoch)
    local unit = units[id]
    if not unit or unit.epoch ~= epoch or not unit.owner or not isElement(unit.ped) or not isElement(unit.vehicle) then return end
    if type(setVehicleLoadCollisionFlag) ~= "function" or not setVehicleLoadCollisionFlag(unit.vehicle, false) then
        return fail(unit, "vehicle-collision-policy-refused")
    end
    local cleared = killPedTask(unit.ped, "primary", 3, false)
    local drivingStyleName = unit.drivingStyle == 6 and "avoid_cars_stop_for_peds_obey_lights" or "stop_for_cars"
    local started = setPedDriveWander(unit.ped, unit.vehicle, unit.cruiseSpeed, drivingStyleName)
    unit.taskStartedAt = getTickCount()
    unit.taskQueued = started == true
    sendDiagnostic(unit.id, unit.epoch, "test-drive-resume", {
        started = started == true, cleared = cleared == true, drivingStyle = unit.drivingStyle,
        remoteCollisionGhostPolicy = true,
    })
end)

addEvent("carTraffic:revoke", true)
addEventHandler("carTraffic:revoke", resourceRoot, function(id, epoch)
    local unit = units[id]
    if not unit or unit.epoch ~= epoch or not unit.owner then return end
    local snapshot = {
        capturedAt = getTickCount(), seq = unit.ownerSeq,
        task = type(isPedDoingTask) == "function" and isPedDoingTask(unit.ped, TASK_NAME) == true,
        seat = isElement(unit.ped) and getPedOccupiedVehicleSeat(unit.ped) or -1,
        pedSyncer = isElement(unit.ped) and isElementSyncer(unit.ped) or false,
        vehicleSyncer = isElement(unit.vehicle) and isElementSyncer(unit.vehicle) or false,
    }
    if isElement(unit.vehicle) then
        snapshot.x, snapshot.y, snapshot.z = getElementPosition(unit.vehicle)
        snapshot.rx, snapshot.ry, snapshot.rz = getElementRotation(unit.vehicle)
        snapshot.vx, snapshot.vy, snapshot.vz = getElementVelocity(unit.vehicle)
        snapshot.avx, snapshot.avy, snapshot.avz = getElementAngularVelocity(unit.vehicle)
    end
    snapshot.passengers = passengerSamples(unit)
    local proof = releaseUnit(unit, true)
    units[id] = nil
    proof.registryEmpty = units[id] == nil
    releasedProofs[id] = releasedProofs[id] or {}
    releasedProofs[id][epoch] = proof
    snapshot.release = proof
    report(unit, "released", snapshot)
end)

addEvent("carTraffic:stop", true)
addEventHandler("carTraffic:stop", resourceRoot, function(id, epoch)
    local unit = units[id]
    local proof
    if unit and unit.epoch <= epoch then
        proof = releaseUnit(unit, unit.owner == true)
        units[id] = nil
    end
    if not proof and releasedProofs[id] then
        local proofEpoch = -1
        for releasedEpoch, releasedProof in pairs(releasedProofs[id]) do
            if releasedEpoch <= epoch and releasedEpoch > proofEpoch then
                proofEpoch = releasedEpoch
                proof = releasedProof
            end
        end
    end
    proof = proof or {
        epoch = epoch, hadUnit = false, missingReleaseProof = true, taskWasQueued = false, taskKillAccepted = false, taskStopped = false,
        hadPedLease = false, hadVehicleLease = false, pedLeaseReleased = false, vehicleLeaseReleased = false,
        passengerLeaseTokens = 0, passengerLeasesReleasedCount = 0, passengerLeasesReleased = false,
        passengerLeaseRegistryEmpty = false, passengerMissionActorPolicies = 0, passengerMissionActorPoliciesRestored = 0,
        passengerMissionActorsRestored = false, passengerMissionActorRegistryEmpty = false,
        leasesReleased = false, missionActorRestored = false, releasedAt = getTickCount(),
    }
    proof.registryEmpty = units[id] == nil
    proof.proofEpoch = proof.epoch
    proof.ackEpoch = epoch
    proof.acknowledgedAt = getTickCount()
    if releasedProofs[id] then
        for releasedEpoch in pairs(releasedProofs[id]) do
            if releasedEpoch <= epoch then releasedProofs[id][releasedEpoch] = nil end
        end
        if not next(releasedProofs[id]) then releasedProofs[id] = nil end
    end
    triggerServerEvent("carTraffic:cleanupAck", resourceRoot, id, epoch, proof)
end)

addEvent("carTraffic:detach", true)
addEventHandler("carTraffic:detach", resourceRoot, function(id, epoch)
    local unit = units[id]
    if not unit or unit.epoch ~= epoch or unit.owner then return end
    local proof = releaseUnit(unit, false)
    units[id] = nil
    proof.registryEmpty = units[id] == nil
    proof.detached = true
    releasedProofs[id] = releasedProofs[id] or {}
    releasedProofs[id][epoch] = proof
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
    local modelSelection
    if not tonumber(model) then
        if type(getAmbientVehicleModelCandidate) ~= "function" then
            return triggerServerEvent("carTraffic:candidate", resourceRoot, session, false, "model-api-missing")
        end
        local selection, selectionReason = getAmbientVehicleModelCandidate()
        if type(selection) ~= "table" or not tonumber(selection.model) then
            return triggerServerEvent("carTraffic:candidate", resourceRoot, session, false, selectionReason or "model-unavailable")
        end
        modelSelection = selection
        model = selection.model
    end
    -- GenerateCarCreationCoors2 only chooses road geometry. Hold the requested
    -- stock model while the C++ oracle reads its collision centre-to-base
    -- offset, then release the temporary reference after the scalar result is
    -- captured.
    local modelHeld = false
    local modelReference = false
    if type(engineStreamingRequestModel) == "function" and type(engineStreamingGetModelLoadState) == "function" and
        type(engineStreamingReleaseModel) == "function" then
        local called = pcall(engineStreamingRequestModel, model, true, true)
        -- The API returns false for an already-held model even though it adds
        -- this resource reference, so successful invocation plus load state is
        -- the reliable contract.
        modelReference = called
        modelHeld = called
    end
    if modelHeld then
        local queried, state = pcall(engineStreamingGetModelLoadState, model)
        modelHeld = queried and state == "loaded"
    end
    if not modelHeld then
        if modelReference then pcall(engineStreamingReleaseModel, model, true) end
        return triggerServerEvent("carTraffic:candidate", resourceRoot, session, false, "model-load-refused")
    end
    local candidate, reason = getAmbientVehicleSpawnCandidate(x, y, z, model)
    local occupantModels
    local occupantModelReferences = {}
    if type(candidate) == "table" and type(getAmbientVehicleOccupantModelCandidate) == "function" then
        occupantModels = getAmbientVehicleOccupantModelCandidate(model, 4)
        if type(occupantModels) ~= "table" or #occupantModels < 1 then
            candidate = false
            reason = "occupant-model-unavailable"
        elseif type(engineStreamingRequestModel) ~= "function" or type(engineStreamingGetModelLoadState) ~= "function" or
            type(engineStreamingReleaseModel) ~= "function" then
            candidate = false
            reason = "occupant-model-load-refused"
        else
            local distinctModels = {}
            for _, occupantModel in ipairs(occupantModels) do
                occupantModel = tonumber(occupantModel)
                if not occupantModel then
                    candidate = false
                    reason = "occupant-model-load-refused"
                    break
                end
                if not distinctModels[occupantModel] then
                    distinctModels[occupantModel] = true
                    local called = pcall(engineStreamingRequestModel, occupantModel, true, true)
                    if called then occupantModelReferences[#occupantModelReferences + 1] = occupantModel end
                    local queried, state = pcall(engineStreamingGetModelLoadState, occupantModel)
                    if not called or not queried or state ~= "loaded" then
                        candidate = false
                        reason = "occupant-model-load-refused"
                        break
                    end
                end
            end
        end
    elseif type(candidate) == "table" then
        candidate = false
        reason = "occupant-model-api-missing"
    end
    if type(candidate) == "table" then
        -- Keep this proposal scalar on the wire. Nested array fields inside a
        -- candidate table are not preserved by every supported net module.
        candidate.occupantCount = #occupantModels
        for index, occupantModel in ipairs(occupantModels) do
            candidate["occupantModel" .. index] = occupantModel
        end
        if modelSelection then candidate.carGroup = modelSelection.carGroup end
    end
    for _, occupantModel in ipairs(occupantModelReferences) do
        pcall(engineStreamingReleaseModel, occupantModel, true)
    end
    pcall(engineStreamingReleaseModel, model, true)
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

addEvent("carTraffic:revealProbe", true)
addEventHandler("carTraffic:revealProbe", resourceRoot, function(id, epoch, vehicle)
    if not isElement(vehicle) then
        return triggerServerEvent("carTraffic:revealVisibility", resourceRoot, id, epoch, true, false, false)
    end
    local x, y, z = getElementPosition(vehicle)
    local px, py, pz = getElementPosition(localPlayer)
    local distance = getDistanceBetweenPoints3D(px, py, pz, x, y, z)
    local visible = type(isAmbientPedSphereVisible) ~= "function" or isAmbientPedSphereVisible(x, y, z, 5.0) == true
    -- Staging happens while the player keeps moving. Recheck the live pose at
    -- reveal time so a formerly valid distant candidate can never pop into an
    -- already visible near field.
    local veto = visible and distance < REVEAL_VISIBLE_MIN_DISTANCE
    triggerServerEvent("carTraffic:revealVisibility", resourceRoot, id, epoch, veto, visible, distance)
end)

local interactionProbe
local interactionExitProbe

local function reportInteractionDiagnostic(stage, data)
    sendDiagnostic(0, 0, stage, data or {})
end

local function releaseInteractionProbe()
    if not interactionProbe then return end
    if isTimer(interactionProbe.timer) then killTimer(interactionProbe.timer) end
    if interactionProbe.lease then releaseElementStreamingLease(interactionProbe.lease) end
    interactionProbe = nil
end

local function positionAtInteractionDoor(vehicle, seat)
    local matrix = getElementMatrix(vehicle)
    if type(matrix) ~= "table" or type(matrix[1]) ~= "table" or type(matrix[2]) ~= "table" or type(matrix[4]) ~= "table" then return false end
    local side = seat % 2 == 0 and -1.75 or 1.75
    local forward = seat >= 2 and -0.75 or 0.6
    local x = matrix[4][1] + matrix[1][1] * side + matrix[2][1] * forward
    local y = matrix[4][2] + matrix[1][2] * side + matrix[2][2] * forward
    local z = matrix[4][3] + matrix[1][3] * side + matrix[2][3] * forward + 0.25
    if not x or not y or not z then return false end
    setElementPosition(localPlayer, x, y, z)
    local _, _, rotation = getElementRotation(vehicle)
    setElementRotation(localPlayer, 0, 0, rotation)
    return true
end

local function attemptInteractionEntry()
    local probe = interactionProbe
    if not probe then return end
    if getTickCount() - probe.startedAt >= 15000 then
        reportInteractionDiagnostic("interaction-entry-timeout",
            {seat = probe.seat, attempts = probe.attempts, accepted = probe.accepted == true, harness = probe.harness})
        return releaseInteractionProbe()
    end
    if not isElement(probe.vehicle) then
        reportInteractionDiagnostic("interaction-entry-vehicle-lost", {seat = probe.seat, attempts = probe.attempts, harness = probe.harness})
        return releaseInteractionProbe()
    end
    local entering = type(isPedDoingTask) == "function" and
        (isPedDoingTask(localPlayer, "TASK_COMPLEX_ENTER_CAR_AS_DRIVER") or isPedDoingTask(localPlayer, "TASK_COMPLEX_ENTER_CAR_AS_PASSENGER"))
    if isElementStreamedIn(probe.vehicle) and not isPedInVehicle(localPlayer) and not entering then
        if probe.harness then positionAtInteractionDoor(probe.vehicle, probe.seat) end
        probe.attempts = probe.attempts + 1
        probe.accepted = setPedEnterVehicle(localPlayer, probe.vehicle, probe.seat) == true or probe.accepted
    end
end

local function requestInteractionEntry(vehicle, seat, harness)
    if not isElement(vehicle) or type(setPedEnterVehicle) ~= "function" or type(acquireElementStreamingLease) ~= "function" then return end
    seat = math.max(0, math.floor(tonumber(seat) or 0))
    if not interactionProbe or interactionProbe.vehicle ~= vehicle or interactionProbe.seat ~= seat then
        releaseInteractionProbe()
        interactionProbe = {
            vehicle = vehicle, seat = seat, lease = acquireElementStreamingLease(vehicle), startedAt = getTickCount(), attempts = 0,
            harness = harness == true,
        }
    elseif harness == true then
        interactionProbe.harness = true
    end
    if not interactionProbe.lease then return end
    if not interactionProbe.timer then
        interactionProbe.timer = setTimer(attemptInteractionEntry, 500, 0)
    end
    attemptInteractionEntry()
end

addEvent("carTraffic:testEnterVehicle", true)
addEventHandler("carTraffic:testEnterVehicle", resourceRoot, function(vehicle, seat) requestInteractionEntry(vehicle, seat, true) end)

addEvent("carTraffic:takeoverReady", true)
addEventHandler("carTraffic:takeoverReady", resourceRoot, function(vehicle, seat) requestInteractionEntry(vehicle, seat, false) end)

local function releaseInteractionExitProbe()
    if not interactionExitProbe then return end
    if isTimer(interactionExitProbe.timer) then killTimer(interactionExitProbe.timer) end
    if interactionExitProbe.lease then releaseElementStreamingLease(interactionExitProbe.lease) end
    interactionExitProbe = nil
end

local function attemptInteractionExit()
    local probe = interactionExitProbe
    if not probe then return end
    if not isElement(probe.vehicle) or getPedOccupiedVehicle(localPlayer) ~= probe.vehicle then return releaseInteractionExitProbe() end
    if getTickCount() - probe.startedAt >= 15000 then
        reportInteractionDiagnostic("interaction-exit-timeout", {attempts = probe.attempts, accepted = probe.accepted})
        return releaseInteractionExitProbe()
    end
    probe.attempts = probe.attempts + 1
    probe.accepted = setPedExitVehicle(localPlayer) == true or probe.accepted
end

addEvent("carTraffic:testExitVehicle", true)
addEventHandler("carTraffic:testExitVehicle", resourceRoot, function(vehicle)
    releaseInteractionExitProbe()
    if not isElement(vehicle) or getPedOccupiedVehicle(localPlayer) ~= vehicle or type(setPedExitVehicle) ~= "function" or
        type(acquireElementStreamingLease) ~= "function" then
        return reportInteractionDiagnostic("interaction-exit-refused", {vehicle = isElement(vehicle), occupied = getPedOccupiedVehicle(localPlayer) == vehicle})
    end
    interactionExitProbe = {vehicle = vehicle, lease = acquireElementStreamingLease(vehicle), startedAt = getTickCount(), attempts = 0, accepted = false}
    if not interactionExitProbe.lease then
        releaseInteractionExitProbe()
        return reportInteractionDiagnostic("interaction-exit-lease-refused")
    end
    interactionExitProbe.timer = setTimer(attemptInteractionExit, 500, 0)
    attemptInteractionExit()
end)

addEventHandler("onClientVehicleEnter", root, function(player)
    if player == localPlayer and interactionProbe and source == interactionProbe.vehicle then releaseInteractionProbe() end
end)

addEventHandler("onClientVehicleExit", root, function(player)
    if player == localPlayer and interactionExitProbe and source == interactionExitProbe.vehicle then releaseInteractionExitProbe() end
end)

local populationProfileSeq = 0
local function publishPopulationProfile()
    if getElementHealth(localPlayer) <= 0 or getElementDimension(localPlayer) ~= 0 or getElementInterior(localPlayer) ~= 0 or
        type(getAmbientPedPopulationProfile) ~= "function" then
        triggerServerEvent("carTraffic:populationProfile", resourceRoot, false)
        return
    end

    local x, y, z = getElementPosition(localPlayer)
    -- GTA's disabled ambient loop normally refreshes this popcycle snapshot.
    -- Keep producing it from a timer because minimized test clients may not run
    -- render callbacks frequently enough for a deterministic density harness.
    if type(updateAmbientPedPopulationModels) == "function" then updateAmbientPedPopulationModels(x, y, z) end
    local profile = getAmbientPedPopulationProfile()
    if type(profile) ~= "table" then
        triggerServerEvent("carTraffic:populationProfile", resourceRoot, false)
        return
    end
    populationProfileSeq = populationProfileSeq + 1
    profile.seq = populationProfileSeq
    profile.capturedAt = getTickCount()
    profile.x, profile.y, profile.z = x, y, z
    triggerServerEvent("carTraffic:populationProfile", resourceRoot, profile)
end

addEventHandler("onClientResourceStart", resourceRoot, function()
    -- Admit this player only after the complete client script, including every
    -- remotely triggered event handler above, has finished loading.
    triggerServerEvent("carTraffic:clientReady", resourceRoot)
    publishPopulationProfile()
end)

setTimer(publishPopulationProfile, 1000, 0)
setTimer(publishPopulationProfile, 250, 1)

addEventHandler("onClientResourceStop", resourceRoot, function()
    releaseInteractionProbe()
    releaseInteractionExitProbe()
    for _, unit in pairs(units) do releaseUnit(unit, unit.owner == true) end
    units = {}
    releasedProofs = {}
end)
