local units = {}
local pendingCandidates = {}
local candidateReservations = {}
local nextUnitId = 0
local nextSession = 0
local trafficGeneration = 0
local activeTest = false
local population = {enabled = false, targetPerPlayer = 2, cap = 12}

local MONITOR_INTERVAL = 500
local TEST_STUCK_TIMEOUT = 8000
local PRODUCTION_STUCK_TIMEOUT = 30000
local RESIDENCY_DISTANCE = 280
local OWNER_DISTANCE = 220
local MAX_CANDIDATE_ATTEMPTS = 25

local VEHICLE_MODELS = {401, 404, 405, 410, 418, 419, 421, 426, 436, 439, 445, 466, 467, 474, 475, 479, 491, 492, 496, 507, 516, 517, 518, 526, 527, 529, 540, 542, 546, 547, 549, 550, 551, 555, 560, 561, 562, 566, 580, 585, 589, 600, 602}
local DRIVER_MODELS = {7, 14, 15, 17, 18, 20, 21, 22, 23, 24, 25, 26, 28, 29, 30, 32, 33, 34, 35, 36, 37, 38, 41, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60}

local function trace(event, fields)
    fields = fields or {}
    fields.event = event
    fields.tick = getTickCount()
    outputServerLog("[car-traffic] " .. toJSON(fields, true))
end

local function players()
    local result = {}
    for _, player in ipairs(getElementsByType("player")) do
        if getElementHealth(player) > 0 and getElementDimension(player) == 0 and getElementInterior(player) == 0 then
            result[#result + 1] = player
        end
    end
    table.sort(result, function(a, b) return getPlayerSerial(a) < getPlayerSerial(b) end)
    return result
end

local function clearTimer(unit, name)
    if isTimer(unit[name]) then killTimer(unit[name]) end
    unit[name] = nil
end

local function unitCount(predicate)
    local count = 0
    for _, unit in pairs(units) do
        if not predicate or predicate(unit) then count = count + 1 end
    end
    return count
end

local function pendingCount()
    local count = 0
    for _, generation in pairs(candidateReservations) do
        if generation == trafficGeneration then count = count + 1 end
    end
    return count
end

local function distanceToUnit(player, unit)
    if not isElement(player) or not isElement(unit.vehicle) then return math.huge end
    local px, py, pz = getElementPosition(player)
    local x, y, z = getElementPosition(unit.vehicle)
    return getDistanceBetweenPoints3D(px, py, pz, x, y, z)
end

local function ownerLoad(player)
    local count = 0
    for _, unit in pairs(units) do
        if unit.owner == player or unit.pendingOwner == player then count = count + 1 end
    end
    return count
end

local function chooseOwner(unit, excluded)
    local best, bestScore
    for _, player in ipairs(players()) do
        if player ~= excluded then
            local distance = unit and distanceToUnit(player, unit) or 0
            if not unit or distance <= OWNER_DISTANCE then
                local score = ownerLoad(player) * 1000 + distance
                if not bestScore or score < bestScore then
                    best, bestScore = player, score
                end
            end
        end
    end
    return best
end

local function registerTestCleanup(unit)
    if not activeTest then return end
    activeTest.cleanupExpected = activeTest.cleanupExpected or {}
    local participants = {}
    local count = 0
    for _, player in ipairs(unit.observers or players()) do
        if isElement(player) and not participants[player] then
            participants[player] = true
            count = count + 1
        end
    end
    activeTest.cleanupExpected[unit.id] = {
        epoch = unit.epoch,
        count = count,
        participants = participants,
        acknowledgements = {},
        complete = count == 0,
    }
end

local function destroyUnit(unit, reason)
    if not unit or unit.removing then return end
    unit.removing = true
    clearTimer(unit, "monitorTimer")
    clearTimer(unit, "handoffTimer")
    clearTimer(unit, "dispatchTimer")
    clearTimer(unit, "assignmentTimer")
    triggerClientEvent(root, "carTraffic:stop", resourceRoot, unit.id, unit.epoch)
    if isElement(unit.ped) then destroyElement(unit.ped) end
    if isElement(unit.vehicle) then destroyElement(unit.vehicle) end
    units[unit.id] = nil
    trace("despawn", {id = unit.id, epoch = unit.epoch, reason = reason})
end

local function destroyUnitsWhere(reason, predicate)
    local snapshot = {}
    for _, unit in pairs(units) do
        if not predicate or predicate(unit) then snapshot[#snapshot + 1] = unit end
    end
    for _, unit in ipairs(snapshot) do destroyUnit(unit, reason) end
end

local function terminalTestFailure(reason, unit)
    if not activeTest then
        trace("unit-failure", {id = unit and unit.id, epoch = unit and unit.epoch, reason = reason})
        if unit then destroyUnit(unit, reason) end
        return
    end

    local failed = activeTest
    activeTest = false
    if isTimer(failed.watchdogTimer) then killTimer(failed.watchdogTimer) end
    population.enabled = false
    population.testDensity = false
    if isTimer(population.timer) then killTimer(population.timer) end
    population.timer = nil
    trafficGeneration = trafficGeneration + 1
    pendingCandidates = {}
    candidateReservations = {}
    trace("FAIL", {id = unit and unit.id, epoch = unit and unit.epoch, mode = failed.mode, reason = reason})
    destroyUnitsWhere("test-failed:" .. tostring(reason), function(candidate)
        return candidate == unit or candidate.mode == failed.mode or (failed.units and failed.units[candidate.id] ~= nil)
    end)
end

local function armTestWatchdog(timeout)
    if not activeTest then return end
    local expected = activeTest
    expected.watchdogTimer = setTimer(function()
        if activeTest == expected then terminalTestFailure("test-timeout", expected.unit) end
    end, timeout, 1)
end

local function fail(unit, reason)
    if activeTest and (not unit or activeTest.unit == unit or unit.mode == activeTest.mode or (activeTest.units and activeTest.units[unit.id])) then
        return terminalTestFailure(reason, unit)
    end
    trace("unit-failure", {id = unit and unit.id, epoch = unit and unit.epoch, reason = reason})
    if unit then destroyUnit(unit, reason) end
end

local requestCandidate

local function finishTestCleanupIfReady()
    if not activeTest or not activeTest.cleanupExpected then return end
    local total = 0
    for _, expected in pairs(activeTest.cleanupExpected) do
        if expected.complete ~= true then return end
        total = total + expected.count
    end
    trace("PASS-cleanup", {mode = activeTest.mode, acknowledgements = total})
    if activeTest.mode == "soak" and activeTest.cycle < activeTest.cycles then
        activeTest.cycle = activeTest.cycle + 1
        activeTest.cleanupExpected = nil
        activeTest.unit = nil
        activeTest.routeRetries = 0
        return setTimer(function()
            local owner = chooseOwner()
            if owner and activeTest and activeTest.mode == "soak" then requestCandidate(owner, "soak", 1) end
        end, 500, 1)
    end
    if activeTest.mode == "soak" then
        trace("PASS-soak", {cycles = activeTest.cycles})
    end
    if isTimer(activeTest.watchdogTimer) then killTimer(activeTest.watchdogTimer) end
    activeTest = false
end

local function assign(unit, owner, reason)
    if not isElement(owner) or not isElement(unit.ped) or not isElement(unit.vehicle) then return fail(unit, "assign-invalid") end
    clearTimer(unit, "dispatchTimer")
    clearTimer(unit, "assignmentTimer")
    unit.owner = owner
    unit.epoch = unit.epoch + 1
    unit.state = "assigning"
    unit.acceptedAt = nil
    unit.ownerTaskSamples = 0
    unit.movingSamples = 0
    unit.observerSamples = 0
    unit.currentEpochStable = false
    unit.lastMovingAt = getTickCount()
    unit.history = {}
    setElementFrozen(unit.ped, true)
    setElementFrozen(unit.vehicle, true)
    if not setElementSyncer(unit.ped, owner, true, true) or not setElementSyncer(unit.vehicle, owner, true, true) then
        return fail(unit, "double-syncer-refused")
    end
    setElementData(unit.vehicle, "neon:ambientVehicleTrafficEpoch", unit.epoch)
    setElementData(unit.ped, "neon:ambientVehicleTrafficEpoch", unit.epoch)
    local dispatchDelay = unit.dispatchedOnce and 150 or 1200
    local expectedEpoch = unit.epoch
    unit.dispatchTimer = setTimer(function()
        unit.dispatchTimer = nil
        if units[unit.id] == unit and unit.state == "assigning" and unit.epoch == expectedEpoch and unit.owner == owner and
            expectedEpoch == getElementData(unit.vehicle, "neon:ambientVehicleTrafficEpoch") then
            triggerClientEvent(root, "carTraffic:observe", resourceRoot, unit.id, unit.epoch, unit.ped, unit.vehicle)
            -- Commit the synchronized pose before GTA consumes the script
            -- command. A frozen vehicle cannot advance its native autopilot.
            setElementFrozen(unit.ped, false)
            setElementFrozen(unit.vehicle, false)
            triggerClientEvent(owner, "carTraffic:assign", resourceRoot, unit.id, unit.epoch, unit.ped, unit.vehicle, unit.cruiseSpeed)
            unit.dispatchedOnce = true
            trace("assignment-dispatched", {id = unit.id, epoch = unit.epoch, owner = getPlayerName(owner), delay = dispatchDelay})
        end
    end, dispatchDelay, 1)
    unit.assignmentTimer = setTimer(function()
        unit.assignmentTimer = nil
        if units[unit.id] == unit and unit.state == "assigning" and unit.epoch == expectedEpoch and unit.owner == owner then
            fail(unit, "assignment-timeout")
        end
    end, dispatchDelay + 12000, 1)
    unit.assignmentReason = reason
    trace("assign", {id = unit.id, epoch = unit.epoch, owner = getPlayerName(owner), reason = reason})
end

local function beginRevoke(unit, nextOwner, reason)
    if not unit or unit.state ~= "active" or not isElement(nextOwner) then return false end
    unit.pendingOwner = nextOwner
    unit.pendingHandoffReason = reason
    unit.state = "revoking"
    triggerClientEvent(unit.owner, "carTraffic:revoke", resourceRoot, unit.id, unit.epoch)
    clearTimer(unit, "handoffTimer")
    unit.handoffTimer = setTimer(function()
        if units[unit.id] == unit and unit.state == "revoking" then fail(unit, "revoke-timeout:" .. tostring(reason)) end
    end, 10000, 1)
    trace("revoke", {id = unit.id, epoch = unit.epoch, owner = getPlayerName(unit.owner), nextOwner = getPlayerName(nextOwner), reason = reason})
    return true
end

local function nearestServerHistory(unit, x, y, z)
    local best = math.huge
    local now = getTickCount()
    for index = #unit.history, 1, -1 do
        local sample = unit.history[index]
        if now - sample.tick > 2000 then break end
        local distance = getDistanceBetweenPoints3D(x, y, z, sample.x, sample.y, sample.z)
        if distance < best then best = distance end
    end
    return best
end

local function allUnitsOutsideResidency(unit)
    local found = false
    for _, player in ipairs(players()) do
        found = true
        if distanceToUnit(player, unit) <= RESIDENCY_DISTANCE then return false end
    end
    return found
end

local function startMonitor(unit)
    clearTimer(unit, "monitorTimer")
    unit.monitorTimer = setTimer(function()
        if units[unit.id] ~= unit or unit.state ~= "active" then return end
        if not isElement(unit.ped) or not isElement(unit.vehicle) then return fail(unit, "element-missing") end
        if isVehicleBlown(unit.vehicle) or isPedDead(unit.ped) then
            if activeTest and activeTest.unit == unit and activeTest.mode == "lifecycle" and unit.expectDestruction then
                trace("PASS-lifecycle", {id = unit.id, epoch = unit.epoch, recovery = true, destruction = true})
                registerTestCleanup(unit)
                return destroyUnit(unit, "expected-lifecycle-destruction")
            end
            if activeTest and activeTest.unit == unit then return fail(unit, "unit-destroyed") end
            return destroyUnit(unit, "unit-destroyed")
        end
        if getElementSyncer(unit.ped) ~= unit.owner or getElementSyncer(unit.vehicle) ~= unit.owner then return fail(unit, "split-ownership") end
        if getPedOccupiedVehicle(unit.ped) ~= unit.vehicle or getPedOccupiedVehicleSeat(unit.ped) ~= 0 then return fail(unit, "driver-seat-lost") end
        local x, y, z = getElementPosition(unit.vehicle)
        if allUnitsOutsideResidency(unit) then
            if activeTest and activeTest.unit == unit then return fail(unit, "test-left-residency") end
            return destroyUnit(unit, "outside-residency")
        end
        local distance = getDistanceBetweenPoints2D(unit.startX, unit.startY, x, y)
        if unit.lastX then
            local step = getDistanceBetweenPoints2D(unit.lastX, unit.lastY, x, y)
            if step >= 0.25 then
                unit.movingSamples = unit.movingSamples + 1
                unit.lastMovingAt = getTickCount()
            end
        end
        unit.lastX, unit.lastY, unit.lastZ = x, y, z
        unit.history[#unit.history + 1] = {tick = getTickCount(), x = x, y = y, z = z}
        while #unit.history > 8 do table.remove(unit.history, 1) end

        local stuckTimeout = unit.mode == "production" and PRODUCTION_STUCK_TIMEOUT or TEST_STUCK_TIMEOUT
        if getTickCount() - unit.lastMovingAt >= stuckTimeout then
            if activeTest and activeTest.unit == unit and not unit.fixturePassed then
                activeTest.routeRetries = (activeTest.routeRetries or 0) + 1
                local mode = activeTest.mode
                activeTest.unit = nil
                destroyUnit(unit, "initial-route-rejected")
                if activeTest.routeRetries <= 4 then
                    trace("test-route-retry", {mode = mode, attempt = activeTest.routeRetries})
                    return setTimer(function()
                        local owner = chooseOwner()
                        if owner and activeTest and activeTest.mode == mode then requestCandidate(owner, mode, 1) end
                    end, 500, 1)
                end
                return terminalTestFailure("route-retry-limit")
            end
            if (unit.stuckRestarts or 0) >= 1 then
                if activeTest and activeTest.unit == unit then return fail(unit, "stuck-after-restart") end
                return destroyUnit(unit, "stuck-after-restart")
            end
            local nextOwner = chooseOwner(unit, unit.owner) or unit.owner
            if not isElement(nextOwner) then return destroyUnit(unit, "stuck-no-owner") end
            unit.stuckRestarts = (unit.stuckRestarts or 0) + 1
            setElementFrozen(unit.ped, false)
            setElementFrozen(unit.vehicle, false)
            unit.recoveryX, unit.recoveryY, unit.recoveryZ = x, y, z
            unit.recoveryPending = true
            return beginRevoke(unit, nextOwner, "stuck-restart")
        end

        if distanceToUnit(unit.owner, unit) > OWNER_DISTANCE then
            local nextOwner = chooseOwner(unit, unit.owner)
            if nextOwner then return beginRevoke(unit, nextOwner, "owner-residency") end
        end

        if unit.ownerTaskSamples >= 4 and (unit.observerSamples or 0) >= 4 and unit.movingSamples >= 6 and distance >= 20 and not unit.currentEpochStable then
            local firstStableEpoch = not unit.fixturePassed
            unit.fixturePassed = true
            unit.currentEpochStable = true
            unit.stableEpoch = unit.epoch
            trace(unit.mode == "production" and "unit-stable" or (firstStableEpoch and "PASS-fixture" or "epoch-stable"),
                {id = unit.id, epoch = unit.epoch, distance = distance, ownerSamples = unit.ownerTaskSamples})
            if firstStableEpoch and activeTest and activeTest.unit == unit and activeTest.mode == "all" then
                local ps = players()
                if #ps >= 2 then
                    beginRevoke(unit, ps[1] == unit.owner and ps[2] or ps[1], "test-handoff")
                end
            elseif firstStableEpoch and activeTest and activeTest.unit == unit and activeTest.mode == "fixture" then
                registerTestCleanup(unit)
                destroyUnit(unit, "fixture-test-complete")
            elseif firstStableEpoch and activeTest and activeTest.unit == unit and activeTest.mode == "lifecycle" then
                activeTest.phase = "forced-stuck"
                setElementFrozen(unit.vehicle, true)
                unit.lastMovingAt = getTickCount() - TEST_STUCK_TIMEOUT
                trace("lifecycle-forced-stuck", {id = unit.id, epoch = unit.epoch})
            elseif firstStableEpoch and activeTest and activeTest.unit == unit and activeTest.mode == "soak" then
                registerTestCleanup(unit)
                destroyUnit(unit, "soak-cycle-complete")
            end
            if activeTest and activeTest.mode == "density" then
                local ready = 0
                for _, testUnit in pairs(activeTest.units) do
                    if units[testUnit.id] == testUnit and testUnit.state == "active" and testUnit.stableEpoch == testUnit.epoch then ready = ready + 1 end
                end
                if ready >= activeTest.expectedUnits and not activeTest.densityPassed then
                    activeTest.densityPassed = true
                    population.enabled = false
                    if isTimer(population.timer) then killTimer(population.timer) end
                    population.timer = nil
                    trace("PASS-density", {units = ready, cap = population.cap, targetPerPlayer = population.targetPerPlayer})
                    for _, testUnit in pairs(activeTest.units) do
                        if units[testUnit.id] == testUnit then registerTestCleanup(testUnit) end
                    end
                    for _, testUnit in pairs(activeTest.units) do
                        if units[testUnit.id] == testUnit then destroyUnit(testUnit, "density-test-complete") end
                    end
                end
            end
        end
        if unit.handoffEpoch == unit.epoch and unit.ownerTaskSamples >= 4 and (unit.observerSamples or 0) >= 4 and unit.movingSamples >= 6 and
            unit.handoffX and getDistanceBetweenPoints2D(unit.handoffX, unit.handoffY, x, y) >= 20 and not unit.handoffPassed then
            unit.handoffPassed = true
            trace("PASS-handoff", {id = unit.id, epoch = unit.epoch, owner = getPlayerName(unit.owner)})
            if activeTest and activeTest.unit == unit and activeTest.mode == "all" then
                registerTestCleanup(unit)
                destroyUnit(unit, "test-complete")
            end
        end
        if unit.recoveryEpoch == unit.epoch and unit.recoveryX and unit.ownerTaskSamples >= 4 and (unit.observerSamples or 0) >= 4 and unit.movingSamples >= 6 and
            getDistanceBetweenPoints2D(unit.recoveryX, unit.recoveryY, x, y) >= 15 and not unit.lifecyclePassed then
            unit.lifecyclePassed = true
            trace("PASS-recovery", {id = unit.id, epoch = unit.epoch, owner = getPlayerName(unit.owner), restarts = unit.stuckRestarts})
            if activeTest and activeTest.unit == unit and activeTest.mode == "lifecycle" then
                unit.expectDestruction = true
                blowVehicle(unit.vehicle)
            end
        end
    end, MONITOR_INTERVAL, 0)
end

local function createUnit(candidate, owner, observers, mode)
    local vehicle = createVehicle(candidate.model, candidate.x, candidate.y, candidate.z, 0, 0, candidate.rotation)
    local driverModel = DRIVER_MODELS[(nextUnitId % #DRIVER_MODELS) + 1]
    local ped = vehicle and createPed(driverModel, candidate.x, candidate.y, candidate.z + 1.0, candidate.rotation) or nil
    if not isElement(vehicle) or not isElement(ped) then
        if isElement(vehicle) then destroyElement(vehicle) end
        if isElement(ped) then destroyElement(ped) end
        return false, "atomic-create-refused"
    end
    warpPedIntoVehicle(ped, vehicle, 0)
    setVehicleEngineState(vehicle, true)
    setElementFrozen(vehicle, true)
    setElementFrozen(ped, true)
    nextUnitId = nextUnitId + 1
    local unit = {
        id = nextUnitId, epoch = 0, ped = ped, vehicle = vehicle, owner = owner, observers = observers,
        state = "created", startX = candidate.x, startY = candidate.y, startZ = candidate.z,
        cruiseSpeed = candidate.cruiseSpeed or 16.0, model = candidate.model, mode = mode,
    }
    units[unit.id] = unit
    -- Vehicle occupants are governed by the car task and seat attachment, not
    -- by the on-foot native-ped collision-residency fence. Marking the driver
    -- as ambientPedTraffic would make that fence compare its seated root to
    -- the ground and freeze it forever.
    setElementData(ped, "neon:ambientPedPopulationClass", "civilian")
    setElementData(ped, "neon:ambientVehicleTrafficId", unit.id)
    setElementData(vehicle, "neon:ambientVehicleTraffic", true)
    setElementData(vehicle, "neon:ambientVehicleTrafficId", unit.id)
    trace("spawn", {id = unit.id, model = unit.model, x = candidate.x, y = candidate.y, z = candidate.z})
    assign(unit, owner, "spawn")
    return unit
end

requestCandidate = function(owner, mode, attempt, generation)
    generation = generation or trafficGeneration
    if generation ~= trafficGeneration then return end
    local ps = players()
    if not isElement(owner) then
        if owner ~= nil then candidateReservations[owner] = nil end
        return trace("candidate-aborted", {reason = "owner-missing", mode = mode})
    end
    if mode ~= "production" and mode ~= "density" and #ps < 2 then return terminalTestFailure("two-clients-required") end
    if #ps < 1 then return trace("candidate-aborted", {reason = "no-clients", mode = mode}) end
    attempt = tonumber(attempt) or 1
    if attempt == 1 then
        if candidateReservations[owner] == generation then return end
        candidateReservations[owner] = generation
    end
    if attempt > MAX_CANDIDATE_ATTEMPTS then
        candidateReservations[owner] = nil
        if mode ~= "production" and mode ~= "density" then return terminalTestFailure("candidate-attempt-limit") end
        return trace("candidate-aborted", {reason = "candidate-attempt-limit", attempts = attempt - 1, mode = mode})
    end
    nextSession = nextSession + 1
    local x, y, z = getElementPosition(owner)
    local model = VEHICLE_MODELS[(nextSession % #VEHICLE_MODELS) + 1]
    pendingCandidates[nextSession] = {
        session = nextSession, owner = owner, players = ps, mode = mode, model = model,
        attempt = attempt, requestedAt = getTickCount(), generation = generation,
    }
    triggerClientEvent(owner, "carTraffic:requestCandidate", resourceRoot, nextSession, model, x, y, z)
    trace("candidate-request", {session = nextSession, owner = getPlayerName(owner), model = model, attempt = attempt, x = x, y = y, z = z})
end

addEvent("carTraffic:candidate", true)
addEventHandler("carTraffic:candidate", resourceRoot, function(session, candidate, reason)
    local pending = pendingCandidates[tonumber(session)]
    if not pending or pending.generation ~= trafficGeneration or client ~= pending.owner then return end
    if type(candidate) ~= "table" or tonumber(candidate.model) ~= pending.model then
        pendingCandidates[session] = nil
        trace("candidate-retry", {session = session, reason = tostring(reason)})
        return setTimer(requestCandidate, 250, 1, pending.owner, pending.mode, pending.attempt + 1, pending.generation)
    end
    local x, y, z = tonumber(candidate.x), tonumber(candidate.y), tonumber(candidate.z)
    local ox, oy, oz = getElementPosition(pending.owner)
    local finite = x and y and z and x == x and y == y and z == z and math.abs(x) < 10000 and math.abs(y) < 10000 and math.abs(z) < 2000
    if not finite or getDistanceBetweenPoints3D(ox, oy, oz, x, y, z) > 240 then
        pendingCandidates[session] = nil
        trace("candidate-retry", {session = session, reason = "server-validation"})
        return setTimer(requestCandidate, 250, 1, pending.owner, pending.mode, pending.attempt + 1, pending.generation)
    end
    for _, unit in pairs(units) do
        if isElement(unit.vehicle) then
            local ux, uy, uz = getElementPosition(unit.vehicle)
            if getDistanceBetweenPoints3D(ux, uy, uz, x, y, z) < 18 then
                pendingCandidates[session] = nil
                trace("candidate-retry", {session = session, reason = "deduplicated"})
                return setTimer(requestCandidate, 250, 1, pending.owner, pending.mode, pending.attempt + 1, pending.generation)
            end
        end
    end
    pending.candidate = candidate
    pending.visibility = {}
    trace("candidate", {session = session, x = candidate.x, y = candidate.y, z = candidate.z, model = candidate.model})
    for _, player in ipairs(pending.players) do
        triggerClientEvent(player, "carTraffic:visibilityProbe", resourceRoot, session, candidate.x, candidate.y, candidate.z)
    end
end)

addEvent("carTraffic:visibility", true)
addEventHandler("carTraffic:visibility", resourceRoot, function(session, visible, rawVisible, distance)
    local pending = pendingCandidates[tonumber(session)]
    if not pending or not pending.candidate then return end
    local expected = false
    for _, player in ipairs(pending.players) do if player == client then expected = true break end end
    if not expected then return end
    pending.visibility[client] = {veto = visible == true, visible = rawVisible == true, distance = tonumber(distance)}
    for _, player in ipairs(pending.players) do if pending.visibility[player] == nil then return end end
    for _, player in ipairs(pending.players) do
        local probe = pending.visibility[player]
        if probe.veto then
            pendingCandidates[session] = nil
            trace("candidate-veto", {session = session, player = getPlayerName(player), visible = probe.visible, distance = probe.distance})
            return setTimer(requestCandidate, 250, 1, pending.owner, pending.mode, pending.attempt + 1, pending.generation)
        end
    end
    pendingCandidates[session] = nil
    candidateReservations[pending.owner] = nil
    if pending.mode == "production" or pending.mode == "density" then
        local desired = math.min(population.cap, #players() * population.targetPerPlayer)
        local relevant = unitCount(function(unit) return unit.mode == pending.mode end)
        if relevant >= desired then
            return trace("candidate-aborted", {session = session, mode = pending.mode, reason = "cap-commit-fence", units = relevant, desired = desired})
        end
    end
    local unit, reason = createUnit(pending.candidate, pending.owner, pending.players, pending.mode)
    if not unit then
        if pending.mode ~= "production" then return terminalTestFailure(reason) end
        return trace("unit-failure", {reason = reason, mode = pending.mode})
    end
    if pending.mode == "production" then return end
    if pending.mode == "density" then
        if activeTest and activeTest.mode == "density" then activeTest.units[unit.id] = unit end
        return
    end
    if activeTest and activeTest.mode == pending.mode then
        activeTest.unit = unit
    else
        activeTest = {mode = pending.mode, unit = unit, startedAt = getTickCount()}
    end
end)

addEvent("carTraffic:evidence", true)
addEventHandler("carTraffic:evidence", resourceRoot, function(id, epoch, evidence, data)
    local unit = units[tonumber(id)]
    if not unit or tonumber(epoch) ~= unit.epoch or type(evidence) ~= "string" then return end
    data = type(data) == "table" and data or {}
    if unit.expectDestruction and activeTest and activeTest.unit == unit and activeTest.mode == "lifecycle" and
        (evidence == "failure" or evidence == "owner-sample") then
        return
    end
    if activeTest and activeTest.unit == unit and activeTest.mode == "lifecycle" and activeTest.phase == "forced-stuck" and evidence == "owner-sample" then
        return
    end
    if evidence == "failure" then
        if client ~= unit.owner and not (activeTest and activeTest.unit == unit) then
            return trace("observer-unavailable", {id = unit.id, epoch = unit.epoch, client = getPlayerName(client), reason = tostring(data.reason)})
        end
        return fail(unit, (client == unit.owner and "owner:" or "observer:") .. tostring(data.reason))
    end
    if evidence == "accepted" then
        if client ~= unit.owner or unit.state ~= "assigning" or data.task ~= true or tonumber(data.seat) ~= 0 then return end
        clearTimer(unit, "assignmentTimer")
        unit.state = "active"
        unit.acceptedAt = getTickCount()
        unit.observerSamples = 0
        startMonitor(unit)
        if unit.assignmentReason == "owner-quit" then
            trace("PASS-owner-quit", {id = unit.id, epoch = unit.epoch, owner = getPlayerName(unit.owner)})
        end
        return trace("active", {id = unit.id, epoch = unit.epoch, owner = getPlayerName(unit.owner)})
    end
    if evidence == "owner-sample" and client == unit.owner and unit.state == "active" then
        local pedVehicleDelta = tonumber(data.pedVehicleDelta)
        if data.task ~= true or data.pedSyncer ~= true or data.vehicleSyncer ~= true or tonumber(data.seat) ~= 0 or not pedVehicleDelta or pedVehicleDelta > 5 then
            return fail(unit, "owner-native-state-lost")
        end
        unit.ownerTaskSamples = unit.ownerTaskSamples + 1
        return
    end
    if evidence == "observer-sample" and client ~= unit.owner and unit.state == "active" then
        if data.pedSyncer == true or data.vehicleSyncer == true or tonumber(data.seat) ~= 0 then return fail(unit, "observer-authority-invalid") end
        local x, y, z = tonumber(data.x), tonumber(data.y), tonumber(data.z)
        if not x or not y or not z or nearestServerHistory(unit, x, y, z) > 15 then return end
        unit.observerSamples = (unit.observerSamples or 0) + 1
        return
    end
    if evidence == "released" and client == unit.owner and unit.state == "revoking" then
        clearTimer(unit, "handoffTimer")
        local nextOwner = unit.pendingOwner
        setElementSyncer(unit.ped, false)
        setElementSyncer(unit.vehicle, false)
        unit.owner = nil
        unit.pendingOwner = nil
        local reason = unit.pendingHandoffReason or "handoff"
        unit.pendingHandoffReason = nil
        assign(unit, nextOwner, reason)
        if reason == "test-handoff" then
            unit.handoffEpoch = unit.epoch
            unit.handoffX, unit.handoffY, unit.handoffZ = getElementPosition(unit.vehicle)
        elseif reason == "stuck-restart" then
            unit.recoveryEpoch = unit.epoch
            unit.recoveryPending = nil
            if activeTest and activeTest.unit == unit and activeTest.mode == "lifecycle" then activeTest.phase = "recovering" end
        end
        return
    end
end)

addEvent("carTraffic:clientDiagnostic", true)
addEventHandler("carTraffic:clientDiagnostic", resourceRoot, function(id, epoch, stage, data)
    trace("client-diagnostic", {
        id = tonumber(id), epoch = tonumber(epoch), client = isElement(client) and getPlayerName(client) or "invalid",
        stage = tostring(stage), data = type(data) == "table" and data or {},
    })
end)

addEvent("carTraffic:cleanupAck", true)
addEventHandler("carTraffic:cleanupAck", resourceRoot, function(id, epoch)
    if not activeTest or not activeTest.cleanupExpected then return end
    local expected = activeTest.cleanupExpected[tonumber(id)]
    if not expected or tonumber(epoch) ~= expected.epoch then return end
    if not expected.participants[client] then return end
    expected.acknowledgements[client] = true
    local count = 0
    for player in pairs(expected.acknowledgements) do if isElement(player) then count = count + 1 end end
    if count >= expected.count then expected.complete = true end
    finishTestCleanupIfReady()
end)

local function ownerHasPendingCandidate(owner)
    if candidateReservations[owner] == trafficGeneration then return true end
    for _, pending in pairs(pendingCandidates) do
        if pending.owner == owner then return true end
    end
    return false
end

local function populationTick()
    if not population.enabled then return end
    local ps = players()
    if #ps == 0 then return end

    for session, pending in pairs(pendingCandidates) do
        if getTickCount() - pending.requestedAt > 15000 then
            pendingCandidates[session] = nil
            candidateReservations[pending.owner] = nil
            trace("candidate-timeout", {session = session, mode = pending.mode})
        end
    end

    local mode = population.testDensity and "density" or "production"
    local desired = math.min(population.cap, #ps * population.targetPerPlayer)
    local relevant = unitCount(function(unit) return unit.mode == mode end)
    if mode == "production" and relevant > desired then
        local excess = {}
        for _, unit in pairs(units) do
            if unit.mode == mode then
                local nearest = math.huge
                for _, player in ipairs(ps) do nearest = math.min(nearest, distanceToUnit(player, unit)) end
                excess[#excess + 1] = {unit = unit, distance = nearest}
            end
        end
        table.sort(excess, function(a, b)
            if a.distance == b.distance then return a.unit.id > b.unit.id end
            return a.distance > b.distance
        end)
        for index = 1, relevant - desired do destroyUnit(excess[index].unit, "density-reconcile") end
        relevant = desired
    end
    if relevant + pendingCount() >= desired then return end

    local owner = chooseOwner()
    if not owner or ownerHasPendingCandidate(owner) then
        for _, candidateOwner in ipairs(ps) do
            if not ownerHasPendingCandidate(candidateOwner) then owner = candidateOwner break end
        end
    end
    if owner and not ownerHasPendingCandidate(owner) then requestCandidate(owner, mode, 1) end
end

local function startPopulation(targetPerPlayer, cap, testDensity)
    trafficGeneration = trafficGeneration + 1
    pendingCandidates = {}
    candidateReservations = {}
    population.targetPerPlayer = math.max(1, math.min(4, tonumber(targetPerPlayer) or 2))
    population.cap = math.max(1, math.min(24, tonumber(cap) or 12))
    population.testDensity = testDensity == true
    population.enabled = true
    if isTimer(population.timer) then killTimer(population.timer) end
    population.timer = setTimer(populationTick, 1000, 0)
    populationTick()
    trace("population-start", {targetPerPlayer = population.targetPerPlayer, cap = population.cap, testDensity = population.testDensity})
end

local function stopPopulation(reason, destroyOwned)
    trafficGeneration = trafficGeneration + 1
    pendingCandidates = {}
    candidateReservations = {}
    population.enabled = false
    population.testDensity = false
    if isTimer(population.timer) then killTimer(population.timer) end
    population.timer = nil
    if destroyOwned then
        destroyUnitsWhere(reason or "population-stop", function(unit) return unit.mode == "production" or unit.mode == "density" end)
    end
    trace("population-stop", {reason = reason, remaining = unitCount()})
end

addCommandHandler("cartraffic", function(player, _, action, mode, value)
    if action == "test" then
        mode = mode or "fixture"
        if activeTest then return trace("test-refused", {reason = "test-already-active", mode = activeTest.mode}) end
        if population.enabled or unitCount() > 0 or pendingCount() > 0 then return trace("test-refused", {reason = "traffic-not-empty"}) end
        trafficGeneration = trafficGeneration + 1
        pendingCandidates = {}
        candidateReservations = {}
        local ps = players()
        local owner = isElement(player) and getElementType(player) == "player" and player or ps[1]
        if not owner then return trace("test-refused", {reason = "no-owner"}) end
        -- Keep the fixture on a broad, connected central-LS road. The stock
        -- creation oracle walks only 230 m of non-repeating path links, so a
        -- cul-de-sac is a bad harness origin even though it is valid gameplay.
        for index, participant in ipairs(ps) do
            setElementInterior(participant, 0)
            setElementDimension(participant, 0)
            setElementPosition(participant, 1540 + index * 2, -1675, 13.55)
            setElementRotation(participant, 0, 0, 90)
            setCameraTarget(participant, participant)
        end
        if mode == "density" then
            local expected = math.min(4, #ps * 2)
            activeTest = {mode = "density", units = {}, expectedUnits = expected, startedAt = getTickCount()}
            armTestWatchdog(240000)
            return setTimer(function() startPopulation(2, expected, true) end, 1500, 1)
        end
        if mode ~= "fixture" and mode ~= "all" and mode ~= "lifecycle" and mode ~= "soak" then
            return trace("test-refused", {reason = "unknown-test-mode", mode = mode})
        end
        activeTest = {mode = mode, startedAt = getTickCount(), cycle = 1, cycles = mode == "soak" and math.max(2, math.min(10, tonumber(value) or 3)) or 1}
        armTestWatchdog(mode == "soak" and 300000 or 180000)
        setTimer(requestCandidate, 1500, 1, owner, mode, 1)
    elseif action == "start" then
        if activeTest then return trace("population-refused", {reason = "test-active"}) end
        startPopulation(mode, value, false)
    elseif action == "stop" then
        stopPopulation("command-stop", true)
    elseif action == "status" then
        trace("status", {enabled = population.enabled, units = unitCount(), pending = pendingCount(), cap = population.cap,
            targetPerPlayer = population.targetPerPlayer, test = activeTest and activeTest.mode or false})
    elseif action == "cleanup" then
        stopPopulation("command-cleanup", false)
        if activeTest and isTimer(activeTest.watchdogTimer) then killTimer(activeTest.watchdogTimer) end
        activeTest = false
        destroyUnitsWhere("command-cleanup")
    else
        trace("usage", {command = "cartraffic test fixture|all|lifecycle|density|soak [cycles] | cartraffic start [perPlayer] [cap] | stop | status | cleanup"})
    end
end)

addEventHandler("onPlayerQuit", root, function()
    if activeTest and activeTest.cleanupExpected then
        for _, expected in pairs(activeTest.cleanupExpected) do
            if expected.participants[source] then
                expected.participants[source] = nil
                expected.acknowledgements[source] = nil
                expected.count = math.max(0, expected.count - 1)
                local count = 0
                for player in pairs(expected.acknowledgements) do if expected.participants[player] then count = count + 1 end end
                if count >= expected.count then expected.complete = true end
            end
        end
        finishTestCleanupIfReady()
    elseif activeTest then
        return terminalTestFailure("client-left")
    end

    for session, pending in pairs(pendingCandidates) do
        local affected = pending.owner == source
        for _, participant in ipairs(pending.players) do if participant == source then affected = true break end end
        if affected then
            pendingCandidates[session] = nil
            candidateReservations[pending.owner] = nil
            trace("candidate-aborted", {session = session, mode = pending.mode, reason = "client-left"})
        end
    end

    for _, unit in pairs(units) do
        if unit.pendingOwner == source then
            unit.pendingOwner = chooseOwner(unit, source)
            if not unit.pendingOwner then
                fail(unit, "pending-owner-left")
            end
        end
        if not unit.removing and unit.owner == source then
            local nextOwner
            for _, player in ipairs(players()) do if player ~= source then nextOwner = player break end end
            if nextOwner then
                clearTimer(unit, "handoffTimer")
                setElementSyncer(unit.ped, false)
                setElementSyncer(unit.vehicle, false)
                assign(unit, nextOwner, "owner-quit")
            else
                destroyUnit(unit, "owner-quit-no-fallback")
            end
        end
    end
end)

addEventHandler("onVehicleStartEnter", root, function(player, seat)
    if seat ~= 0 then return end
    for _, unit in pairs(units) do
        if source == unit.vehicle then
            cancelEvent()
            if activeTest and (activeTest.unit == unit or (activeTest.units and activeTest.units[unit.id])) then
                fail(unit, "player-driver-intervention")
            else
                destroyUnit(unit, "player-driver-intervention")
            end
            return
        end
    end
end)

addEventHandler("onElementDestroy", root, function()
    for _, unit in pairs(units) do
        if not unit.removing and (source == unit.ped or source == unit.vehicle) then
            return fail(unit, "member-destroyed")
        end
    end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    trafficGeneration = trafficGeneration + 1
    pendingCandidates = {}
    candidateReservations = {}
    destroyUnitsWhere("resource-stop")
end)

trace("ready", {models = #VEHICLE_MODELS})
