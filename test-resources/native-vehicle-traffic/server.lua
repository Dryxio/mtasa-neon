local units = {}
local pendingCandidates = {}
local candidateReservations = {}
local populationProfiles = {}
local readyClients = {}
local takeoverVehicles = {}
local nextUnitId = 0
local nextSession = 0
local trafficGeneration = 0
local activeTest = false
-- Nearby players see the same road population, so production budgets sixteen
-- units per spatial bubble rather than sixteen duplicate units per player.
-- Keep the existing global circuit breaker: ten isolated areas can receive
-- the complete local target without raising the combined traffic-ped fence.
local PRODUCTION_TARGET_PER_BUBBLE = 16
local PRODUCTION_GLOBAL_CAP = 160
-- The pool fence is global because both traffic resources create server-side
-- ped elements, while GTA's actual pool pressure is local after streaming.
-- This leaves room for 240 walkers, 160 drivers and a small number of native
-- optional passengers without forcing distant populations into one client.
local PRODUCTION_PED_POOL_SOFT_LIMIT = 416
local population = {
    enabled = false,
    targetPerPlayer = PRODUCTION_TARGET_PER_BUBBLE,
    cap = PRODUCTION_GLOBAL_CAP,
    demo = false,
}
local demoPopulationSnapshot = false

local MONITOR_INTERVAL = 500
-- Population admission is idle once the spatial target is full. A short tick
-- only matters while a fast player has just shed several distant cars, where
-- the former one-second cadence left the visible bubble half empty.
local PRODUCTION_REFILL_INTERVAL = 250
-- Eight seconds is shorter than an ordinary red-light wait. Keep deliberate
-- lifecycle stalls immediate by backdating lastMovingAt in that scenario,
-- while normal fixtures get a realistic traffic timeout.
local TEST_STUCK_TIMEOUT = 20000
local PRODUCTION_STUCK_TIMEOUT = 30000
local RESIDENCY_DISTANCE = 220
local OWNER_DISTANCE = 220
-- Players this close can reliably observe traffic created around the same
-- anchor while still respecting the wider residency boundary above.
local POPULATION_BUBBLE_DISTANCE = 180
local MAX_CANDIDATE_ATTEMPTS = 25
local PRODUCTION_TELEMETRY_INTERVAL = 15000
-- Element creation and the owner event use separate network packets. Give the
-- entity stream a short head start, while keeping the staging window well
-- below the former visibly frozen 1.2-second pause.
local INITIAL_ASSIGNMENT_DELAY = 350
local telemetryWindow = VehicleTrafficTelemetry.newCounterWindow()

-- These pools are the road-safe subset of GTA's cargrp.dat categories. MTA
-- disables the retail ambient vehicle streamer, so reproducing that ecology
-- explicitly is more reliable than calling ChooseModel against empty native
-- loaded-car groups.
local VEHICLE_GROUPS = {
    business = {401, 405, 409, 420, 421, 426, 428, 433, 445, 507, 526, 533, 551, 579, 580, 602},
    rich = {402, 405, 409, 411, 415, 426, 429, 451, 477, 480, 506, 507, 533, 541, 555, 558, 559, 560, 562, 579, 580, 587, 602, 603},
    average = {400, 401, 405, 410, 421, 422, 426, 436, 445, 458, 466, 467, 491, 496, 516, 526, 527, 540, 546, 547, 550, 551, 554, 561, 566, 580, 585, 600},
    poor = {401, 404, 410, 412, 418, 419, 422, 436, 439, 466, 467, 474, 475, 478, 479, 491, 492, 517, 518, 529, 542, 543, 545, 546, 547, 549, 566, 567, 575, 576, 582, 600},
    workers = {403, 408, 413, 414, 431, 437, 440, 443, 455, 456, 459, 482, 498, 499, 514, 515, 524, 525, 552, 578, 582, 588},
    rural = {403, 463, 468, 478, 483, 489, 508, 514, 515, 531, 543, 554, 581, 586, 600},
    beach = {400, 401, 404, 424, 436, 462, 466, 467, 480, 481, 489, 491, 496, 500, 509, 510, 521, 550, 554, 581, 600},
    gang = {410, 412, 466, 467, 474, 475, 517, 518, 536, 542, 549, 566, 567, 575, 576},
    entertainment = {402, 409, 411, 415, 420, 426, 429, 437, 451, 477, 506, 541, 559, 560, 562, 565, 580, 587, 589, 602},
    airport = {401, 405, 421, 426, 428, 431, 437, 443, 455, 456, 458, 482, 498, 499, 507, 578, 580, 582},
}

local ZONE_VEHICLE_GROUPS = {
    [0] = {"business", "average"}, [1] = {"rural", "workers"}, [2] = {"entertainment", "rich"},
    [3] = {"rural", "poor"}, [4] = {"rich"}, [5] = {"average"}, [6] = {"poor"}, [7] = {"gang", "poor"},
    [8] = {"beach", "average"}, [9] = {"average", "business"}, [10] = {"beach", "average"},
    [11] = {"workers"}, [12] = {"entertainment", "rich"}, [13] = {"average", "rich"}, [14] = {"rich"},
    [15] = {"rich", "business"}, [16] = {"airport", "business"}, [17] = {"rich"}, [18] = {"workers"}, [19] = {"airport"},
}

local MODEL_CLASS = {
    [471] = 2,
    [448] = 9, [461] = 9, [462] = 9, [463] = 9, [468] = 9, [521] = 9, [522] = 9, [581] = 9, [586] = 9,
    [481] = 10, [509] = 10, [510] = 10,
}
local CLASS_TEST_MODELS = {401, 413, 403, 461, 481, 471}
local SOAK_SCENARIOS = {"smooth", "lifecycle", "passengers", "fixture"}
-- Exact legal lanes from the public-server incident in the LS Airport
-- tunnel. The south carriageway is eastbound and the north carriageway is
-- westbound, matching GTA's retail lane-bit selection and right-hand traffic.
-- The regression bypasses the probabilistic spawn oracle so every run
-- exercises both one-way carriageways that exposed the inverted lane halves.
local HIGHWAY_TEST_CASES = {
    {name = "eastbound-premier", model = 426, x = 1850.5289, y = -2682.175, z = 5.6871872, rotation = 270,
        directionX = 1, directionY = 0},
    {name = "westbound-bobcat", model = 422, x = 1923.2065, y = -2667.3, z = 6.0233274, rotation = 90,
        directionX = -1, directionY = 0},
}
local ALLOWED_MODELS = {}
local ALLOWED_MODEL_COUNT = 0
for _, group in pairs(VEHICLE_GROUPS) do
    for _, model in ipairs(group) do
        if not ALLOWED_MODELS[model] then
            ALLOWED_MODELS[model] = true
            ALLOWED_MODEL_COUNT = ALLOWED_MODEL_COUNT + 1
        end
    end
end
for _, model in ipairs(CLASS_TEST_MODELS) do ALLOWED_MODELS[model] = true end
local function selectVehicleModel(owner, session, mode)
    if mode == "passengers" or mode == "interaction" or mode == "smooth" or mode == "soak" then return 401 end
    if mode == "classes" and activeTest and activeTest.mode == "classes" then
        return CLASS_TEST_MODELS[activeTest.classIndex]
    end
    local profile = populationProfiles[owner]
    local zoneType = profile and profile.zoneType or 5
    local names = ZONE_VEHICLE_GROUPS[zoneType] or ZONE_VEHICLE_GROUPS[5]
    local groupName = names[((session * 1103515245 + zoneType * 12345) % #names) + 1]
    local group = VEHICLE_GROUPS[groupName]
    return group[((session * 214013 + zoneType * 2531011) % #group) + 1]
end

local function trace(event, fields)
    fields = fields or {}
    fields.event = event
    fields.tick = getTickCount()
    if event ~= "population-snapshot" then
        VehicleTrafficTelemetry.record(telemetryWindow, event, fields.reason)
    end
    outputServerLog("[car-traffic] " .. toJSON(fields, true))
end

local function finiteNumber(value, limit)
    value = tonumber(value)
    return value and value == value and math.abs(value) <= (limit or 100000) and value or nil
end

local function angleDelta(a, b)
    local delta = math.abs((a - b) % 360)
    return delta > 180 and 360 - delta or delta
end

local function players()
    local result = {}
    for _, player in ipairs(getElementsByType("player")) do
        if readyClients[player] and getElementHealth(player) > 0 and getElementDimension(player) == 0 and getElementInterior(player) == 0 then
            result[#result + 1] = player
        end
    end
    table.sort(result, function(a, b) return getPlayerSerial(a) < getPlayerSerial(b) end)
    return result
end

local function populationBubbles(ps)
    local bubbles = {}
    for _, player in ipairs(ps) do
        local px, py, pz = getElementPosition(player)
        local selected, selectedDistance
        for index, bubble in ipairs(bubbles) do
            local x, y, z = getElementPosition(bubble.anchor)
            local distance = getDistanceBetweenPoints3D(px, py, pz, x, y, z)
            if distance <= POPULATION_BUBBLE_DISTANCE and (not selectedDistance or distance < selectedDistance) then
                selected, selectedDistance = index, distance
            end
        end
        if selected then
            bubbles[selected].members[#bubbles[selected].members + 1] = player
        else
            bubbles[#bubbles + 1] = {anchor = player, members = {player}}
        end
    end
    return bubbles
end

local function populationDesired(ps, mode)
    local demandUnits = mode == "production" and not population.demo and #populationBubbles(ps) or #ps
    return math.min(population.cap, demandUnits * population.targetPerPlayer)
end

local function clearTimer(unit, name)
    if isTimer(unit[name]) then killTimer(unit[name]) end
    unit[name] = nil
end

local function forEachOccupant(unit, callback)
    callback(unit.ped, 0)
    for _, passenger in ipairs(unit.passengers or {}) do callback(passenger.ped, passenger.seat) end
end

local function passengerPayload(unit)
    local result = {}
    for _, passenger in ipairs(unit.passengers or {}) do
        result[#result + 1] = {ped = passenger.ped, seat = passenger.seat}
    end
    return result
end

local function setUnitStagingVisible(unit, visible)
    local alpha = visible and 255 or 0
    if isElement(unit.vehicle) then setElementAlpha(unit.vehicle, alpha) end
    forEachOccupant(unit, function(ped)
        if isElement(ped) then setElementAlpha(ped, alpha) end
    end)
    unit.stagedHidden = not visible
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

local function bubbleIndexForPlayer(bubbles, player)
    for index, bubble in ipairs(bubbles) do
        for _, member in ipairs(bubble.members) do
            if member == player then return index end
        end
    end
    return false
end

local function nearestBubbleIndex(bubbles, unit)
    local selected, selectedDistance
    for index, bubble in ipairs(bubbles) do
        local distance = distanceToUnit(bubble.anchor, unit)
        if not selectedDistance or distance < selectedDistance then
            selected, selectedDistance = index, distance
        end
    end
    return selected
end

local function ownerHasPendingCandidate(owner)
    if candidateReservations[owner] == trafficGeneration then return true end
    for _, pending in pairs(pendingCandidates) do
        if pending.owner == owner then return true end
    end
    return false
end

local function productionBubbleLoads(ps, includePending)
    local bubbles = populationBubbles(ps)
    local loads = {}
    local bubbleUnits = {}
    for index = 1, #bubbles do
        loads[index] = 0
        bubbleUnits[index] = {}
    end
    for _, unit in pairs(units) do
        if unit.mode == "production" then
            local index = nearestBubbleIndex(bubbles, unit)
            if index then
                loads[index] = loads[index] + 1
                bubbleUnits[index][#bubbleUnits[index] + 1] = unit
            end
        end
    end
    if includePending then
        for _, pending in pairs(pendingCandidates) do
            if pending.mode == "production" then
                local index = bubbleIndexForPlayer(bubbles, pending.owner)
                if index then loads[index] = loads[index] + 1 end
            end
        end
    end
    return bubbles, loads, bubbleUnits
end

local function emitProductionTelemetry()
    if not population.enabled then return end
    local ps = players()
    local desired = populationDesired(ps, "production")
    local bubbles, loads, bubbleUnits = productionBubbleLoads(ps, false)
    local bubbleSnapshots = {}
    local base = #bubbles > 0 and math.floor(desired / #bubbles) or 0
    local remainder = #bubbles > 0 and desired % #bubbles or 0
    for index, bubble in ipairs(bubbles) do
        local nearest, furthest
        local states = {created = 0, assigning = 0, active = 0, revoking = 0}
        for _, unit in ipairs(bubbleUnits[index]) do
            states[unit.state] = (states[unit.state] or 0) + 1
            local distance = distanceToUnit(bubble.anchor, unit)
            nearest = not nearest and distance or math.min(nearest, distance)
            furthest = not furthest and distance or math.max(furthest, distance)
        end
        local pending = 0
        for _, candidate in pairs(pendingCandidates) do
            if candidate.mode == "production" and bubbleIndexForPlayer(bubbles, candidate.owner) == index then pending = pending + 1 end
        end
        bubbleSnapshots[#bubbleSnapshots + 1] = {
            index = index,
            members = #bubble.members,
            allocation = base + (index <= remainder and 1 or 0),
            units = loads[index],
            pending = pending,
            states = states,
            nearest = nearest or false,
            furthest = furthest or false,
        }
    end

    local states = {created = 0, assigning = 0, active = 0, revoking = 0}
    local motion = {forward = 0, reverse = 0, stationary = 0, lateral = 0, unsampled = 0}
    local productionUnits = 0
    for _, unit in pairs(units) do
        if unit.mode == "production" then
            productionUnits = productionUnits + 1
            states[unit.state] = (states[unit.state] or 0) + 1
            local motionState = unit.motionTelemetry and unit.motionTelemetry.lastState or "unsampled"
            motion[motionState] = (motion[motionState] or 0) + 1
        end
    end

    trace("population-snapshot", {
        version = 1,
        players = #ps,
        bubbles = #bubbles,
        desired = desired,
        cap = population.cap,
        targetPerBubble = population.targetPerPlayer,
        units = productionUnits,
        pending = pendingCount(),
        totalPeds = #getElementsByType("ped"),
        pedPoolSoftLimit = PRODUCTION_PED_POOL_SOFT_LIMIT,
        states = states,
        motion = motion,
        bubbleDetails = bubbleSnapshots,
        window = VehicleTrafficTelemetry.drain(telemetryWindow),
    })
end

local function chooseProductionOwner(ps)
    local bubbles, loads = productionBubbleLoads(ps, true)
    local selected = false
    for index, bubble in ipairs(bubbles) do
        if not ownerHasPendingCandidate(bubble.anchor) and (not selected or loads[index] < loads[selected]) then
            selected = index
        end
    end
    return selected and bubbles[selected].anchor or false
end

local function productionRebalanceUnit(ps, desired)
    local bubbles, loads, bubbleUnits = productionBubbleLoads(ps, false)
    if #bubbles == 0 then return false end
    local base = math.floor(desired / #bubbles)
    local remainder = desired % #bubbles
    for index, bubble in ipairs(bubbles) do
        local allocation = base + (index <= remainder and 1 or 0)
        if loads[index] > allocation then
            table.sort(bubbleUnits[index], function(a, b)
                return distanceToUnit(bubble.anchor, a) > distanceToUnit(bubble.anchor, b)
            end)
            return bubbleUnits[index][1]
        end
    end
    return false
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
    if activeTest.cleanupExpected[unit.id] then return end
    local participants = {}
    local count = 0
    for player in pairs(unit.participants or {}) do
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
    forEachOccupant(unit, function(ped)
        if isElement(ped) then destroyElement(ped) end
    end)
    local x, y, z
    if isElement(unit.vehicle) then x, y, z = getElementPosition(unit.vehicle) end
    local motion = unit.motionTelemetry or {}
    if isElement(unit.vehicle) then destroyElement(unit.vehicle) end
    units[unit.id] = nil
    trace("despawn", {
        id = unit.id,
        epoch = unit.epoch,
        reason = reason,
        model = unit.model,
        lifetimeMs = getTickCount() - (unit.createdAt or getTickCount()),
        x = x,
        y = y,
        z = z,
        distance = motion.distance or 0,
        motionSamples = motion.samples or 0,
        motionCounts = motion.counts or {},
        maximumReverseSpeed = motion.maximumReverseSpeed or 0,
    })
end

local function destroyUnitsWhere(reason, predicate)
    local snapshot = {}
    for _, unit in pairs(units) do
        if not predicate or predicate(unit) then snapshot[#snapshot + 1] = unit end
    end
    for _, unit in ipairs(snapshot) do destroyUnit(unit, reason) end
end

local function clearTakeovers(test, reason)
    local snapshot = {}
    for vehicle, takeover in pairs(takeoverVehicles) do
        if not test or takeover.test == test then snapshot[#snapshot + 1] = {vehicle = vehicle, takeover = takeover} end
    end
    for _, entry in ipairs(snapshot) do
        if isTimer(entry.takeover.timer) then killTimer(entry.takeover.timer) end
        takeoverVehicles[entry.vehicle] = nil
        if isElement(entry.vehicle) then destroyElement(entry.vehicle) end
        trace("takeover-cleanup", {reason = reason, player = isElement(entry.takeover.player) and getPlayerName(entry.takeover.player) or false})
    end
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
    if isTimer(failed.interactionBrakeTimer) then killTimer(failed.interactionBrakeTimer) end
    if isTimer(failed.ownerQuitFollowTimer) then killTimer(failed.ownerQuitFollowTimer) end
    population.enabled = false
    population.testDensity = false
    if isTimer(population.timer) then killTimer(population.timer) end
    population.timer = nil
    trafficGeneration = trafficGeneration + 1
    pendingCandidates = {}
    candidateReservations = {}
    trace("FAIL", {id = unit and unit.id, epoch = unit and unit.epoch, mode = failed.mode, reason = reason})
    clearTakeovers(failed, "test-failed:" .. tostring(reason))
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
local startHighwayCase

local function finishTestCleanupIfReady()
    if not activeTest or not activeTest.cleanupExpected then return end
    if activeTest.cleanupFinalizing ~= true then return end
    if activeTest.waitForTakeoverEnter then return end
    local total = 0
    for _, expected in pairs(activeTest.cleanupExpected) do
        if expected.complete ~= true then return end
        total = total + expected.count
    end
    trace("PASS-cleanup", {mode = activeTest.mode, acknowledgements = total})
    if activeTest.mode == "classes" and activeTest.classIndex < #CLASS_TEST_MODELS then
        activeTest.classIndex = activeTest.classIndex + 1
        activeTest.cleanupExpected = nil
        activeTest.cleanupFinalizing = false
        activeTest.unit = nil
        activeTest.routeRetries = 0
        return setTimer(function()
            local owner = chooseOwner()
            if owner and activeTest and activeTest.mode == "classes" then requestCandidate(owner, "classes", 1) end
        end, 500, 1)
    end
    if activeTest.mode == "classes" then trace("PASS-classes", {models = #CLASS_TEST_MODELS}) end
    if activeTest.mode == "highway" and activeTest.highwayCase < #HIGHWAY_TEST_CASES then
        activeTest.highwayCase = activeTest.highwayCase + 1
        activeTest.cleanupExpected = nil
        activeTest.cleanupFinalizing = false
        activeTest.unit = nil
        return setTimer(function()
            if activeTest and activeTest.mode == "highway" then startHighwayCase() end
        end, 500, 1)
    end
    if activeTest.mode == "highway" then trace("PASS-highway", {cases = #HIGHWAY_TEST_CASES}) end
    if activeTest.mode == "soak" and activeTest.cycle < activeTest.cycles then
        activeTest.cycle = activeTest.cycle + 1
        activeTest.scenario = SOAK_SCENARIOS[((activeTest.cycle - 1) % #SOAK_SCENARIOS) + 1]
        activeTest.cleanupExpected = nil
        activeTest.cleanupFinalizing = false
        activeTest.unit = nil
        activeTest.routeRetries = 0
        activeTest.smoothHandoffs = 0
        return setTimer(function()
            local owner = chooseOwner()
            if owner and activeTest and activeTest.mode == "soak" then requestCandidate(owner, "soak", 1) end
        end, 500, 1)
    end
    if activeTest.mode == "soak" then
        -- This soak deliberately churns the core unit lifecycle. Classes,
        -- spatial caps, player interaction and a real process quit keep their
        -- own terminal tests and must also pass before a V2 freeze.
        trace("PASS-soak-core", {cycles = activeTest.cycles})
    end
    if isTimer(activeTest.watchdogTimer) then killTimer(activeTest.watchdogTimer) end
    activeTest = false
end

local function finalizePlayerTakeover(unit, player)
    if not unit or unit.removing or not isElement(player) or not isElement(unit.vehicle) then return fail(unit, "takeover-invalid") end
    if activeTest and activeTest.unit == unit and activeTest.mode == "interaction" then
        activeTest.cleanupFinalizing = true
        registerTestCleanup(unit)
        activeTest.waitForTakeoverEnter = true
    end
    unit.removing = true
    clearTimer(unit, "monitorTimer")
    clearTimer(unit, "handoffTimer")
    clearTimer(unit, "dispatchTimer")
    clearTimer(unit, "assignmentTimer")
    triggerClientEvent(root, "carTraffic:stop", resourceRoot, unit.id, unit.epoch)
    units[unit.id] = nil
    forEachOccupant(unit, function(ped)
        if isElement(ped) then destroyElement(ped) end
    end)
    setElementSyncer(unit.vehicle, false)
    removeElementData(unit.vehicle, "neon:ambientVehicleTraffic")
    removeElementData(unit.vehicle, "neon:ambientVehicleTrafficId")
    removeElementData(unit.vehicle, "neon:ambientVehicleTrafficEpoch")
    local takeover = {player = player, test = activeTest and activeTest.mode == "interaction" and activeTest or false}
    takeoverVehicles[unit.vehicle] = takeover
    takeover.timer = setTimer(function(vehicle, expected)
        if takeoverVehicles[vehicle] ~= expected then return end
        takeoverVehicles[vehicle] = nil
        if isElement(vehicle) then destroyElement(vehicle) end
        if expected.test and activeTest == expected.test then
            terminalTestFailure("takeover-enter-timeout")
        else
            trace("takeover-timeout", {player = isElement(expected.player) and getPlayerName(expected.player) or false})
        end
    end, 15000, 1, unit.vehicle, takeover)
    trace("takeover-ready", {id = unit.id, epoch = unit.epoch, player = getPlayerName(player)})
    triggerClientEvent(player, takeover.test and "carTraffic:testEnterVehicle" or "carTraffic:takeoverReady", resourceRoot, unit.vehicle, 0)
end

local function assign(unit, owner, reason)
    if not isElement(owner) or not isElement(unit.ped) or not isElement(unit.vehicle) then return fail(unit, "assign-invalid") end
    local occupantInvalid = false
    forEachOccupant(unit, function(ped)
        if not isElement(ped) then occupantInvalid = true end
    end)
    if occupantInvalid then return fail(unit, "occupant-invalid") end
    clearTimer(unit, "dispatchTimer")
    clearTimer(unit, "assignmentTimer")
    unit.owner = owner
    unit.epoch = unit.epoch + 1
    unit.state = "assigning"
    unit.acceptedAt = nil
    unit.ownerTaskSamples = 0
    unit.movingSamples = 0
    unit.observerSamples = 0
    unit.ownerLastSeq = 0
    unit.observerEvidence = {}
    unit.currentEpochStable = false
    unit.lastMovingAt = getTickCount()
    unit.history = {}
    local initialAssignment = not unit.dispatchedOnce
    unit.assignmentStartedAt = getTickCount()
    unit.requiresResume = not initialAssignment
    unit.requiresInitialVelocity = initialAssignment
    unit.maxEpochStep = 0
    unit.maxResumeSpeed = 0
    unit.epochStartX, unit.epochStartY, unit.epochStartZ = getElementPosition(unit.vehicle)
    unit.lastX, unit.lastY, unit.lastZ = unit.epochStartX, unit.epochStartY, unit.epochStartZ
    local freezeForDispatch = initialAssignment or unit.forceTransferFreeze == true
    unit.forceTransferFreeze = nil
    if freezeForDispatch then
        forEachOccupant(unit, function(ped) setElementFrozen(ped, true) end)
        setElementFrozen(unit.vehicle, true)
    end
    local syncersAccepted = setElementSyncer(unit.vehicle, owner, true, true)
    forEachOccupant(unit, function(ped)
        syncersAccepted = setElementSyncer(ped, owner, true, true) and syncersAccepted
    end)
    if not syncersAccepted then
        return fail(unit, "double-syncer-refused")
    end
    setElementData(unit.vehicle, "neon:ambientVehicleTrafficEpoch", unit.epoch)
    forEachOccupant(unit, function(ped) setElementData(ped, "neon:ambientVehicleTrafficEpoch", unit.epoch) end)
    local dispatchDelay = unit.dispatchedOnce and 150 or INITIAL_ASSIGNMENT_DELAY
    local expectedEpoch = unit.epoch
    unit.dispatchTimer = setTimer(function()
        unit.dispatchTimer = nil
        if units[unit.id] == unit and unit.state == "assigning" and unit.epoch == expectedEpoch and unit.owner == owner and
            expectedEpoch == getElementData(unit.vehicle, "neon:ambientVehicleTrafficEpoch") then
            local passengers = passengerPayload(unit)
            for _, participant in ipairs(players()) do
                if participant == owner or distanceToUnit(participant, unit) <= RESIDENCY_DISTANCE then
                    unit.participants[participant] = true
                    unit.attached[participant] = true
                    triggerClientEvent(participant, "carTraffic:observe", resourceRoot, unit.id, unit.epoch, unit.ped, unit.vehicle, passengers)
                end
            end
            -- Commit the synchronized pose before GTA consumes the script
            -- command. A frozen vehicle cannot advance its native autopilot.
            if freezeForDispatch then
                forEachOccupant(unit, function(ped) setElementFrozen(ped, false) end)
                setElementFrozen(unit.vehicle, false)
            end
            triggerClientEvent(owner, "carTraffic:assign", resourceRoot, unit.id, unit.epoch, unit.ped, unit.vehicle, unit.cruiseSpeed, passengers,
                unit.drivingStyle, unit.resumeKinematics, initialAssignment)
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
    local bestSample
    local now = getTickCount()
    for index = #unit.history, 1, -1 do
        local sample = unit.history[index]
        if now - sample.tick > 2000 then break end
        local distance = getDistanceBetweenPoints3D(x, y, z, sample.x, sample.y, sample.z)
        if distance < best then
            best = distance
            bestSample = sample
        end
    end
    return best, bestSample
end

local function qualifiedObserverCount(unit)
    local count = 0
    for player, evidence in pairs(unit.observerEvidence or {}) do
        if isElement(player) and player ~= unit.owner and evidence.uniqueSamples >= 4 and (evidence.distance or 0) >= 5 then count = count + 1 end
    end
    return count
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
            if activeTest and activeTest.unit == unit and (activeTest.mode == "lifecycle" or
                (activeTest.mode == "soak" and activeTest.scenario == "lifecycle")) and unit.expectDestruction then
                trace(activeTest.mode == "soak" and "PASS-soak-lifecycle" or "PASS-lifecycle",
                    {id = unit.id, epoch = unit.epoch, recovery = true, destruction = true, cycle = activeTest.cycle})
                activeTest.cleanupFinalizing = true
                registerTestCleanup(unit)
                return destroyUnit(unit, "expected-lifecycle-destruction")
            end
            if activeTest and activeTest.unit == unit then return fail(unit, "unit-destroyed") end
            return destroyUnit(unit, "unit-destroyed")
        end
        if getElementSyncer(unit.ped) ~= unit.owner or getElementSyncer(unit.vehicle) ~= unit.owner then return fail(unit, "split-ownership") end
        for _, passenger in ipairs(unit.passengers or {}) do
            if not isElement(passenger.ped) or isPedDead(passenger.ped) then return fail(unit, "passenger-lost") end
            if getElementSyncer(passenger.ped) ~= unit.owner then return fail(unit, "passenger-ownership-split") end
            if getPedOccupiedVehicle(passenger.ped) ~= unit.vehicle or getPedOccupiedVehicleSeat(passenger.ped) ~= passenger.seat then
                return fail(unit, "passenger-seat-lost")
            end
        end
        if getPedOccupiedVehicle(unit.ped) ~= unit.vehicle or getPedOccupiedVehicleSeat(unit.ped) ~= 0 then return fail(unit, "driver-seat-lost") end
        local x, y, z = getElementPosition(unit.vehicle)
        local vx, vy, vz = getElementVelocity(unit.vehicle)
        local speed = math.sqrt(vx * vx + vy * vy + vz * vz)
        if unit.mode == "highway" then
            local deltaX, deltaY = x - unit.startX, y - unit.startY
            unit.highwayProgress = deltaX * unit.highwayDirectionX + deltaY * unit.highwayDirectionY
            unit.highwayLateral = math.abs(deltaX * -unit.highwayDirectionY + deltaY * unit.highwayDirectionX)
            if unit.highwayProgress < -6 then return fail(unit, "highway-wrong-direction") end
            if unit.highwayLateral > 9 then return fail(unit, "highway-left-carriageway") end

            local _, _, rotation = getElementRotation(unit.vehicle)
            local wrongHeading = speed >= 0.02 and angleDelta(rotation, unit.spawnRotation) >= 100
            unit.highwayWrongHeadingMs = wrongHeading and (unit.highwayWrongHeadingMs or 0) + MONITOR_INTERVAL or 0
            if unit.highwayWrongHeadingMs >= 1500 then return fail(unit, "highway-sustained-u-turn") end
        end
        if unit.acceptedAt and getTickCount() - unit.acceptedAt <= 1500 then unit.maxResumeSpeed = math.max(unit.maxResumeSpeed or 0, speed) end
        if allUnitsOutsideResidency(unit) then
            if activeTest and activeTest.unit == unit then return fail(unit, "test-left-residency") end
            if activeTest and activeTest.mode == "spatial" and activeTest.units and activeTest.units[unit.id] then registerTestCleanup(unit) end
            return destroyUnit(unit, "outside-residency")
        end
        for _, participant in ipairs(players()) do
            if distanceToUnit(participant, unit) <= RESIDENCY_DISTANCE and not unit.attached[participant] then
                unit.attached[participant] = true
                unit.participants[participant] = true
                triggerClientEvent(participant, "carTraffic:observe", resourceRoot, unit.id, unit.epoch, unit.ped, unit.vehicle, passengerPayload(unit))
            end
        end
        for participant in pairs(unit.attached) do
            if participant ~= unit.owner and (not isElement(participant) or distanceToUnit(participant, unit) > RESIDENCY_DISTANCE + 40) then
                if isElement(participant) then triggerClientEvent(participant, "carTraffic:detach", resourceRoot, unit.id, unit.epoch) end
                unit.attached[participant] = nil
            end
        end
        local distance = getDistanceBetweenPoints2D(unit.startX, unit.startY, x, y)
        if unit.lastX then
            local step = getDistanceBetweenPoints2D(unit.lastX, unit.lastY, x, y)
            unit.maxEpochStep = math.max(unit.maxEpochStep or 0, step)
            if activeTest and activeTest.unit == unit and step > 18 then return fail(unit, "teleport-step") end
            if step >= 0.25 then
                unit.movingSamples = unit.movingSamples + 1
                unit.lastMovingAt = getTickCount()
            end
        end
        unit.lastX, unit.lastY, unit.lastZ = x, y, z
        unit.serverHistorySeq = (unit.serverHistorySeq or 0) + 1
        unit.history[#unit.history + 1] = {seq = unit.serverHistorySeq, tick = getTickCount(), x = x, y = y, z = z}
        while #unit.history > 8 do table.remove(unit.history, 1) end

        local stuckTimeout = unit.mode == "production" and PRODUCTION_STUCK_TIMEOUT or TEST_STUCK_TIMEOUT
        local awaitingOwnerQuit = activeTest and activeTest.unit == unit and activeTest.mode == "ownerquit" and
            activeTest.phase == "await-owner-quit"
        if getTickCount() - unit.lastMovingAt >= stuckTimeout then
            if awaitingOwnerQuit then
                unit.lastMovingAt = getTickCount()
                return
            end
            if unit.mode == "highway" then return terminalTestFailure("highway-stuck", unit) end
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
            forEachOccupant(unit, function(ped) setElementFrozen(ped, false) end)
            setElementFrozen(unit.vehicle, false)
            unit.recoveryX, unit.recoveryY, unit.recoveryZ = x, y, z
            unit.recoveryPending = true
            return beginRevoke(unit, nextOwner, "stuck-restart")
        end

        if not awaitingOwnerQuit and distanceToUnit(unit.owner, unit) > OWNER_DISTANCE then
            local nextOwner = chooseOwner(unit, unit.owner)
            if nextOwner then return beginRevoke(unit, nextOwner, "owner-residency") end
        end

        -- The owner-quit epoch necessarily has no independent observer once
        -- the two-client fixture loses its original owner. The initial epoch
        -- already proved observer correlation; recovery is instead gated by
        -- the new owner's real task samples and sustained server movement.
        local ownerQuitRecovery = activeTest and activeTest.unit == unit and activeTest.mode == "ownerquit" and
            activeTest.phase == "recovering-owner-quit" and unit.ownerQuitEpoch == unit.epoch
        local observerQualified = qualifiedObserverCount(unit) >= 1 or unit.mode == "spatial" or ownerQuitRecovery
        local routeProgressReady = unit.mode ~= "highway" or (unit.highwayProgress or 0) >= 25
        if unit.ownerTaskSamples >= 4 and observerQualified and unit.movingSamples >= 6 and distance >= 20 and routeProgressReady and
            not unit.currentEpochStable then
            local firstStableEpoch = not unit.fixturePassed
            unit.fixturePassed = true
            unit.currentEpochStable = true
            unit.stableEpoch = unit.epoch
            trace(unit.mode == "production" and "unit-stable" or (firstStableEpoch and "PASS-fixture" or "epoch-stable"),
                {id = unit.id, epoch = unit.epoch, distance = distance, ownerSamples = unit.ownerTaskSamples})
            if unit.ownerQuitEpoch == unit.epoch then
                trace("PASS-owner-quit", {id = unit.id, epoch = unit.epoch, owner = getPlayerName(unit.owner), resumed = true})
                unit.ownerQuitEpoch = nil
                if activeTest and activeTest.unit == unit and activeTest.mode == "ownerquit" and activeTest.phase == "recovering-owner-quit" then
                    activeTest.cleanupFinalizing = true
                    registerTestCleanup(unit)
                    destroyUnit(unit, "owner-quit-test-complete")
                    return
                end
            end
            if firstStableEpoch and activeTest and activeTest.unit == unit and
                (activeTest.mode == "all" or activeTest.mode == "passengers" or activeTest.mode == "smooth" or
                    (activeTest.mode == "soak" and (activeTest.scenario == "smooth" or activeTest.scenario == "passengers"))) then
                local ps = players()
                if #ps >= 2 then
                    activeTest.initialOwner = unit.owner
                    beginRevoke(unit, ps[1] == unit.owner and ps[2] or ps[1], "test-handoff")
                end
            elseif firstStableEpoch and activeTest and activeTest.unit == unit and activeTest.mode == "highway" then
                trace("PASS-highway-case", {id = unit.id, case = unit.highwayCaseName, progress = unit.highwayProgress,
                    lateral = unit.highwayLateral, rotation = select(3, getElementRotation(unit.vehicle))})
                activeTest.cleanupFinalizing = true
                registerTestCleanup(unit)
                destroyUnit(unit, "highway-case-complete")
            elseif firstStableEpoch and activeTest and activeTest.unit == unit and activeTest.mode == "fixture" then
                activeTest.cleanupFinalizing = true
                registerTestCleanup(unit)
                destroyUnit(unit, "fixture-test-complete")
            elseif firstStableEpoch and activeTest and activeTest.unit == unit and
                (activeTest.mode == "lifecycle" or (activeTest.mode == "soak" and activeTest.scenario == "lifecycle")) then
                activeTest.phase = "forced-stuck"
                setElementFrozen(unit.vehicle, true)
                unit.lastMovingAt = getTickCount() - TEST_STUCK_TIMEOUT
                trace("lifecycle-forced-stuck", {id = unit.id, epoch = unit.epoch})
            elseif firstStableEpoch and activeTest and activeTest.unit == unit and activeTest.mode == "soak" and activeTest.scenario == "fixture" then
                trace("PASS-soak-fixture", {id = unit.id, epoch = unit.epoch, cycle = activeTest.cycle})
                activeTest.cleanupFinalizing = true
                registerTestCleanup(unit)
                destroyUnit(unit, "soak-cycle-complete")
            elseif firstStableEpoch and activeTest and activeTest.unit == unit and activeTest.mode == "classes" then
                trace("PASS-class", {id = unit.id, model = unit.model, vehicleClass = unit.vehicleClass, classIndex = activeTest.classIndex})
                activeTest.cleanupFinalizing = true
                registerTestCleanup(unit)
                destroyUnit(unit, "class-test-complete")
            elseif firstStableEpoch and activeTest and activeTest.unit == unit and activeTest.mode == "interaction" then
                local target
                for _, player in ipairs(players()) do if player ~= unit.owner then target = player break end end
                if not target then return fail(unit, "interaction-player-missing") end
                activeTest.phase = "passenger-request"
                activeTest.interactionPlayer = target
                -- Hold the vehicle at the door while the remote player runs
                -- GTA's real enter task. DriveWander remains installed and is
                -- resumed immediately after the network entry is confirmed.
                setElementFrozen(unit.vehicle, true)
                setElementVelocity(unit.vehicle, 0, 0, 0)
                setElementPosition(target, x + 1.5, y, z + 0.5)
                setCameraTarget(target, target)
                triggerClientEvent(target, "carTraffic:testEnterVehicle", resourceRoot, unit.vehicle, 1)
                trace("interaction-request", {id = unit.id, epoch = unit.epoch, player = getPlayerName(target), seat = 1})
            elseif firstStableEpoch and activeTest and activeTest.unit == unit and activeTest.mode == "ownerquit" then
                local follower = chooseOwner(unit, unit.owner)
                if not follower then return fail(unit, "owner-quit-follower-missing") end
                activeTest.phase = "await-owner-quit"
                activeTest.expectedQuit = unit.owner
                activeTest.ownerQuitFollower = follower
                local ownerQuitTest = activeTest
                activeTest.ownerQuitFollowTimer = setTimer(function(expected, vehicle, player)
                    if activeTest ~= expected or expected.phase ~= "await-owner-quit" or not isElement(vehicle) or not isElement(player) then return end
                    local followX, followY, followZ = getElementPosition(vehicle)
                    setElementPosition(player, followX + 5, followY, followZ + 1)
                    setCameraTarget(player, player)
                end, 500, 0, ownerQuitTest, unit.vehicle, follower)
                trace("owner-quit-ready", {id = unit.id, epoch = unit.epoch, owner = getPlayerName(unit.owner)})
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
                    activeTest.cleanupFinalizing = true
                    for _, testUnit in pairs(activeTest.units) do
                        if units[testUnit.id] == testUnit then registerTestCleanup(testUnit) end
                    end
                    for _, testUnit in pairs(activeTest.units) do
                        if units[testUnit.id] == testUnit then destroyUnit(testUnit, "density-test-complete") end
                    end
                end
            end
        end
        if activeTest and activeTest.unit == unit and activeTest.mode == "interaction" and activeTest.phase == "passenger-ride" and
            activeTest.passengerRideX and getDistanceBetweenPoints2D(activeTest.passengerRideX, activeTest.passengerRideY, x, y) >= 15 then
            if activeTest.interactionEpoch ~= unit.epoch then return fail(unit, "passenger-ride-epoch-changed") end
            activeTest.phase = "passenger-exit-request"
            setElementFrozen(unit.vehicle, true)
            setElementVelocity(unit.vehicle, 0, 0, 0)
            triggerClientEvent(activeTest.interactionPlayer, "carTraffic:testExitVehicle", resourceRoot, unit.vehicle)
        end
        if unit.handoffEpoch == unit.epoch and unit.ownerTaskSamples >= 4 and qualifiedObserverCount(unit) >= 1 and unit.movingSamples >= 6 and
            unit.handoffX and getDistanceBetweenPoints2D(unit.handoffX, unit.handoffY, x, y) >= 20 and not unit.handoffPassed then
            if unit.handoffMetrics and unit.handoffMetrics.preSpeed >= 0.02 and (unit.maxResumeSpeed or 0) < unit.handoffMetrics.preSpeed * 0.5 then
                return fail(unit, "handoff-speed-not-recovered")
            end
            unit.handoffPassed = true
            trace("PASS-handoff", {id = unit.id, epoch = unit.epoch, owner = getPlayerName(unit.owner), maxStep = unit.maxEpochStep,
                acceptDelay = unit.handoffMetrics and unit.handoffMetrics.acceptDelay, speedRatio = unit.handoffMetrics and unit.handoffMetrics.preSpeed > 0 and
                    (unit.maxResumeSpeed or 0) / unit.handoffMetrics.preSpeed or false})
            if activeTest and activeTest.unit == unit and activeTest.mode == "smooth" then
                activeTest.smoothHandoffs = (activeTest.smoothHandoffs or 0) + 1
                if activeTest.smoothHandoffs < 2 then
                    local nextOwner = activeTest.initialOwner
                    if not isElement(nextOwner) or nextOwner == unit.owner then return fail(unit, "smooth-return-owner-missing") end
                    return beginRevoke(unit, nextOwner, "test-handoff")
                end
                trace("PASS-smooth", {id = unit.id, epoch = unit.epoch, handoffs = activeTest.smoothHandoffs})
                activeTest.cleanupFinalizing = true
                registerTestCleanup(unit)
                destroyUnit(unit, "smooth-test-complete")
            elseif activeTest and activeTest.unit == unit and activeTest.mode == "soak" and
                (activeTest.scenario == "smooth" or activeTest.scenario == "passengers") then
                trace("PASS-soak-handoff", {id = unit.id, epoch = unit.epoch, cycle = activeTest.cycle, scenario = activeTest.scenario,
                    passengers = #(unit.passengers or {})})
                activeTest.cleanupFinalizing = true
                registerTestCleanup(unit)
                destroyUnit(unit, "soak-cycle-complete")
            elseif activeTest and activeTest.unit == unit and (activeTest.mode == "all" or activeTest.mode == "passengers") then
                if activeTest.mode == "passengers" then
                    trace("PASS-passengers", {id = unit.id, epoch = unit.epoch, passengers = #(unit.passengers or {})})
                end
                activeTest.cleanupFinalizing = true
                registerTestCleanup(unit)
                destroyUnit(unit, "test-complete")
            end
        end
        if unit.recoveryEpoch == unit.epoch and unit.recoveryX and unit.ownerTaskSamples >= 4 and qualifiedObserverCount(unit) >= 1 and unit.movingSamples >= 6 and
            getDistanceBetweenPoints2D(unit.recoveryX, unit.recoveryY, x, y) >= 15 and not unit.lifecyclePassed then
            unit.lifecyclePassed = true
            trace("PASS-recovery", {id = unit.id, epoch = unit.epoch, owner = getPlayerName(unit.owner), restarts = unit.stuckRestarts})
            if activeTest and activeTest.unit == unit and
                (activeTest.mode == "lifecycle" or (activeTest.mode == "soak" and activeTest.scenario == "lifecycle")) then
                unit.expectDestruction = true
                blowVehicle(unit.vehicle)
            end
        end
    end, MONITOR_INTERVAL, 0)
end

local function createUnit(candidate, owner, observers, mode)
    if mode == "production" and #getElementsByType("ped") >= PRODUCTION_PED_POOL_SOFT_LIMIT then
        return false, "ped-pool-reserve"
    end

    local vehicle = createVehicle(candidate.model, candidate.x, candidate.y, candidate.z, 0, 0, candidate.rotation)
    local driverModel = tonumber(candidate.occupantModels and candidate.occupantModels[1])
    local ped = vehicle and createPed(driverModel, candidate.x, candidate.y, candidate.z + 1.0, candidate.rotation) or nil
    if not isElement(vehicle) or not isElement(ped) then
        if isElement(vehicle) then destroyElement(vehicle) end
        if isElement(ped) then destroyElement(ped) end
        return false, "atomic-create-refused"
    end
    warpPedIntoVehicle(ped, vehicle, 0)
    if getPedOccupiedVehicle(ped) ~= vehicle or getPedOccupiedVehicleSeat(ped) ~= 0 then
        destroyElement(ped)
        destroyElement(vehicle)
        return false, "driver-warp-refused"
    end

    local passengers = {}
    local maximumPassengers = math.min(3, tonumber(getVehicleMaxPassengers(vehicle)) or 0)
    local requestedSeats = {}
    if tonumber(candidate.vehicleClass) == 0 and maximumPassengers > 0 then
        if mode == "passengers" or (mode == "soak" and activeTest and activeTest.scenario == "passengers") then
            requestedSeats[1] = 1
        elseif (mode == "production" or mode == "density") and not population.demo then
            -- Retail performs one independent 1/8 trial for every available
            -- passenger seat. This deterministic hash preserves that density
            -- without coupling server correctness to Lua's global RNG.
            local passengerCount = 0
            for trial = 1, maximumPassengers do
                if ((nextUnitId + 1) * 1103515245 + trial * 12345) % 8 == 0 then passengerCount = passengerCount + 1 end
            end
            for seat = 1, passengerCount do requestedSeats[#requestedSeats + 1] = seat end
        end
    end
    if mode == "production" then
        -- Reserve one ped slot for every driver still required by the current
        -- spatial demand. Optional passengers may consume only true surplus.
        local remainingDrivers = math.max(0, populationDesired(players(), "production") -
            unitCount(function(unit) return unit.mode == "production" end) - 1)
        local availablePassengerSlots = math.max(0,
            PRODUCTION_PED_POOL_SOFT_LIMIT - #getElementsByType("ped") - remainingDrivers)
        while #requestedSeats > availablePassengerSlots do table.remove(requestedSeats) end
    end
    for _, seat in ipairs(requestedSeats) do
        local passengerModel = tonumber(candidate.occupantModels[math.min(#candidate.occupantModels, #passengers + 2)])
        local passengerPed = createPed(passengerModel, candidate.x, candidate.y, candidate.z + 1.0, candidate.rotation)
        if isElement(passengerPed) then warpPedIntoVehicle(passengerPed, vehicle, seat) end
        if not isElement(passengerPed) or getPedOccupiedVehicle(passengerPed) ~= vehicle or getPedOccupiedVehicleSeat(passengerPed) ~= seat then
            if isElement(passengerPed) then destroyElement(passengerPed) end
            for _, passenger in ipairs(passengers) do if isElement(passenger.ped) then destroyElement(passenger.ped) end end
            destroyElement(ped)
            destroyElement(vehicle)
            return false, "passenger-warp-refused"
        end
        passengers[#passengers + 1] = {ped = passengerPed, seat = seat}
    end
    setVehicleEngineState(vehicle, true)
    setElementFrozen(vehicle, true)
    setElementFrozen(ped, true)
    for _, passenger in ipairs(passengers) do setElementFrozen(passenger.ped, true) end
    nextUnitId = nextUnitId + 1
    local unit = {
        id = nextUnitId, epoch = 0, ped = ped, vehicle = vehicle, owner = owner, observers = observers, participants = {}, attached = {},
        state = "created", startX = candidate.x, startY = candidate.y, startZ = candidate.z,
        cruiseSpeed = candidate.cruiseSpeed or 16.0, drivingStyle = tonumber(candidate.drivingStyle) or 0,
        model = candidate.model, vehicleClass = tonumber(candidate.vehicleClass) or 0, mode = mode, passengers = passengers, anchorPlayer = owner,
        createdAt = getTickCount(), spawnRotation = tonumber(candidate.rotation), carGroup = candidate.carGroup,
        highwayCaseName = candidate.highwayCaseName, highwayDirectionX = candidate.highwayDirectionX,
        highwayDirectionY = candidate.highwayDirectionY,
    }
    units[unit.id] = unit
    -- Server-created elements can stream before their owner has received the
    -- native driving task. Keep the complete atomic unit invisible until the
    -- owner proves that DriveWander and the initial road velocity are active.
    setUnitStagingVisible(unit, false)
    -- Vehicle occupants are governed by the car task and seat attachment, not
    -- by the on-foot native-ped collision-residency fence. Marking the driver
    -- as ambientPedTraffic would make that fence compare its seated root to
    -- the ground and freeze it forever.
    forEachOccupant(unit, function(occupant)
        setElementData(occupant, "neon:ambientPedPopulationClass", "civilian")
        setElementData(occupant, "neon:ambientVehicleTrafficId", unit.id)
    end)
    setElementData(vehicle, "neon:ambientVehicleTraffic", true)
    setElementData(vehicle, "neon:ambientVehicleTrafficId", unit.id)
    trace("spawn", {id = unit.id, model = unit.model, vehicleClass = unit.vehicleClass, passengers = #passengers,
        x = candidate.x, y = candidate.y, z = candidate.z, rotation = candidate.rotation, cruiseSpeed = unit.cruiseSpeed,
        drivingStyle = unit.drivingStyle, carGroup = candidate.carGroup})
    assign(unit, owner, "spawn")
    return unit
end

startHighwayCase = function()
    if not activeTest or activeTest.mode ~= "highway" then return end
    local testCase = HIGHWAY_TEST_CASES[activeTest.highwayCase]
    local owner = chooseOwner()
    local participants = players()
    if not testCase or not owner or #participants < 2 then return terminalTestFailure("highway-participants-missing") end

    local candidate = {
        model = testCase.model, x = testCase.x, y = testCase.y, z = testCase.z, rotation = testCase.rotation,
        cruiseSpeed = 18, drivingStyle = 0, vehicleClass = 0, occupantModels = {7},
        highwayCaseName = testCase.name, highwayDirectionX = testCase.directionX, highwayDirectionY = testCase.directionY,
    }
    local unit, reason = createUnit(candidate, owner, participants, "highway")
    if not unit then return terminalTestFailure(reason or "highway-create-refused") end
    activeTest.unit = unit
    trace("highway-case-start", {id = unit.id, case = testCase.name, model = testCase.model, x = testCase.x, y = testCase.y,
        rotation = testCase.rotation, directionX = testCase.directionX, directionY = testCase.directionY})
end

requestCandidate = function(owner, mode, attempt, generation)
    generation = generation or trafficGeneration
    if generation ~= trafficGeneration then return end
    local ps = players()
    if not isElement(owner) then
        if owner ~= nil then candidateReservations[owner] = nil end
        return trace("candidate-aborted", {reason = "owner-missing", mode = mode})
    end
    if mode ~= "production" and mode ~= "density" and mode ~= "spatial" and #ps < 2 then return terminalTestFailure("two-clients-required") end
    if #ps < 1 then return trace("candidate-aborted", {reason = "no-clients", mode = mode}) end
    attempt = tonumber(attempt) or 1
    if attempt == 1 then
        if candidateReservations[owner] == generation then return end
        candidateReservations[owner] = generation
    end
    if attempt > MAX_CANDIDATE_ATTEMPTS then
        candidateReservations[owner] = nil
        if mode ~= "production" and mode ~= "density" and mode ~= "spatial" then return terminalTestFailure("candidate-attempt-limit") end
        return trace("candidate-aborted", {reason = "candidate-attempt-limit", attempts = attempt - 1, mode = mode})
    end
    nextSession = nextSession + 1
    local x, y, z = getElementPosition(owner)
    local model = (mode == "production" or mode == "density" or mode == "spatial") and false or selectVehicleModel(owner, nextSession, mode)
    pendingCandidates[nextSession] = {
        session = nextSession, owner = owner, players = ps, mode = mode, model = model or nil,
        attempt = attempt, requestedAt = getTickCount(), generation = generation,
    }
    triggerClientEvent(owner, "carTraffic:requestCandidate", resourceRoot, nextSession, model, x, y, z)
    trace("candidate-request", {session = nextSession, owner = getPlayerName(owner), model = model or "native", attempt = attempt, x = x, y = y, z = z})
end

addEvent("carTraffic:candidate", true)
addEventHandler("carTraffic:candidate", resourceRoot, function(session, candidate, reason)
    local pending = pendingCandidates[tonumber(session)]
    if not pending or pending.generation ~= trafficGeneration or client ~= pending.owner then return end
    local proposedModel = type(candidate) == "table" and tonumber(candidate.model) or nil
    if type(candidate) ~= "table" or not proposedModel or (pending.model and proposedModel ~= pending.model) or not ALLOWED_MODELS[proposedModel] then
        pendingCandidates[session] = nil
        trace("candidate-retry", {session = session, reason = tostring(reason)})
        return setTimer(requestCandidate, 250, 1, pending.owner, pending.mode, pending.attempt + 1, pending.generation)
    end
    pending.model = proposedModel
    local x, y, z = tonumber(candidate.x), tonumber(candidate.y), tonumber(candidate.z)
    local vehicleClass = tonumber(candidate.vehicleClass)
    local drivingStyle = tonumber(candidate.drivingStyle)
    local occupantModels = candidate.occupantModels
    if type(occupantModels) ~= "table" then
        occupantModels = {}
        local occupantCount = math.floor(tonumber(candidate.occupantCount) or 0)
        for index = 1, math.min(4, occupantCount) do
            occupantModels[index] = candidate["occupantModel" .. index]
        end
        candidate.occupantModels = occupantModels
    end
    local occupantsValid = type(occupantModels) == "table" and #occupantModels >= 1 and #occupantModels <= 4
    if occupantsValid then
        for _, occupantModel in ipairs(occupantModels) do
            occupantModel = tonumber(occupantModel)
            if not occupantModel or occupantModel % 1 ~= 0 or occupantModel < 7 or occupantModel > 288 then occupantsValid = false break end
        end
    end
    local ox, oy, oz = getElementPosition(pending.owner)
    local finite = x and y and z and x == x and y == y and z == z and math.abs(x) < 10000 and math.abs(y) < 10000 and math.abs(z) < 2000
    if not finite or not occupantsValid or vehicleClass ~= (MODEL_CLASS[pending.model] or 0) or (drivingStyle ~= 0 and drivingStyle ~= 6) or
        getDistanceBetweenPoints3D(ox, oy, oz, x, y, z) > RESIDENCY_DISTANCE then
        pendingCandidates[session] = nil
        trace("candidate-retry", {session = session, reason = "server-validation", finite = finite == true,
            occupantsValid = occupantsValid, occupantCount = type(occupantModels) == "table" and #occupantModels or -1,
            vehicleClass = vehicleClass, expectedVehicleClass = MODEL_CLASS[pending.model] or 0, drivingStyle = drivingStyle,
            distance = x and y and z and getDistanceBetweenPoints3D(ox, oy, oz, x, y, z) or false})
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

addEvent("carTraffic:populationProfile", true)
addEventHandler("carTraffic:populationProfile", resourceRoot, function(profile)
    if type(profile) ~= "table" then
        populationProfiles[client] = nil
        return
    end
    local zoneType = tonumber(profile.zoneType)
    local timeIndex = tonumber(profile.timeIndex)
    if not zoneType or zoneType < 0 or zoneType > 19 or zoneType % 1 ~= 0 or not timeIndex or timeIndex < 0 or timeIndex > 11 or timeIndex % 1 ~= 0 then
        return
    end
    populationProfiles[client] = {
        zoneType = zoneType,
        timeIndex = timeIndex,
        weekend = profile.weekend == true,
        zoneLabel = tostring(profile.zoneLabel or ""),
        capturedAt = getTickCount(),
    }
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
    if pending.mode == "production" or pending.mode == "density" or pending.mode == "spatial" then
        local desired = populationDesired(players(), pending.mode)
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
    if pending.mode == "density" or pending.mode == "spatial" then
        if activeTest and activeTest.mode == pending.mode then activeTest.units[unit.id] = unit end
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
    local lifecycleTest = activeTest and activeTest.unit == unit and
        (activeTest.mode == "lifecycle" or (activeTest.mode == "soak" and activeTest.scenario == "lifecycle"))
    if unit.expectDestruction and lifecycleTest and
        (evidence == "failure" or evidence == "owner-sample") then
        return
    end
    if lifecycleTest and activeTest.phase == "forced-stuck" and evidence == "owner-sample" then
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
        if unit.requiresInitialVelocity and data.initialVelocityApplied ~= true then
            return fail(unit, "initial-velocity-not-applied")
        end
        if unit.requiresResume and data.resumeApplied ~= true then
            if activeTest and activeTest.unit == unit then return fail(unit, "handoff-resume-not-applied") end
            trace("handoff-resume-unavailable", {id = unit.id, epoch = unit.epoch})
        end
        clearTimer(unit, "assignmentTimer")
        unit.state = "active"
        unit.acceptedAt = getTickCount()
        if unit.stagedHidden then setUnitStagingVisible(unit, true) end
        if unit.handoffMetrics then
            local x, y, z = getElementPosition(unit.vehicle)
            local _, _, rz = getElementRotation(unit.vehicle)
            unit.handoffMetrics.acceptDelay = unit.acceptedAt - unit.handoffMetrics.startedAt
            unit.handoffMetrics.acceptJump = getDistanceBetweenPoints3D(x, y, z, unit.handoffMetrics.preX, unit.handoffMetrics.preY, unit.handoffMetrics.preZ)
            unit.handoffMetrics.acceptHeadingDelta = angleDelta(rz, unit.handoffMetrics.preHeading)
            if activeTest and activeTest.unit == unit and (unit.handoffMetrics.acceptDelay > 3000 or unit.handoffMetrics.acceptJump > 8 or
                unit.handoffMetrics.acceptHeadingDelta > 35) then
                return fail(unit, "handoff-continuity-invalid")
            end
        end
        unit.observerSamples = 0
        startMonitor(unit)
        if unit.assignmentReason == "passenger-seat-release" and unit.pendingPassengerEntry then
            local entry = unit.pendingPassengerEntry
            unit.pendingPassengerEntry = nil
            if isElement(entry.player) then triggerClientEvent(entry.player, "carTraffic:takeoverReady", resourceRoot, unit.vehicle, entry.seat) end
        end
        return trace("active", {id = unit.id, epoch = unit.epoch, owner = getPlayerName(unit.owner),
            stagingMs = unit.acceptedAt - (unit.createdAt or unit.acceptedAt), initialVelocityApplied = data.initialVelocityApplied == true,
            initialSpeed = tonumber(data.initialSpeed) or 0})
    end
    if evidence == "owner-sample" and client == unit.owner and unit.state == "active" then
        local pedVehicleDelta = tonumber(data.pedVehicleDelta)
        local seq = tonumber(data.seq)
        if not seq or seq % 1 ~= 0 or seq <= unit.ownerLastSeq then return fail(unit, "owner-sequence-invalid") end
        if data.task ~= true or data.pedSyncer ~= true or data.vehicleSyncer ~= true or tonumber(data.seat) ~= 0 or not pedVehicleDelta or pedVehicleDelta > 5 then
            return fail(unit, "owner-native-state-lost")
        end
        if type(data.passengers) ~= "table" or #data.passengers ~= #(unit.passengers or {}) then return fail(unit, "owner-passenger-evidence-invalid") end
        for index, passenger in ipairs(unit.passengers or {}) do
            local sample = data.passengers[index]
            if type(sample) ~= "table" or sample.ped ~= passenger.ped or tonumber(sample.expectedSeat) ~= passenger.seat or
                tonumber(sample.seat) ~= passenger.seat or sample.occupied ~= true or sample.syncer ~= true then
                return fail(unit, "owner-passenger-state-lost")
            end
        end
        unit.ownerLastSeq = seq
        unit.ownerTaskSamples = unit.ownerTaskSamples + 1
        local sample = {
            x = finiteNumber(data.x, 10000),
            y = finiteNumber(data.y, 10000),
            rz = finiteNumber(data.rz, 3600),
            vx = finiteNumber(data.vx, 3),
            vy = finiteNumber(data.vy, 3),
        }
        local signal
        unit.motionTelemetry, signal = VehicleTrafficTelemetry.updateMotion(unit.motionTelemetry, sample, getTickCount())
        if signal then
            trace(signal.kind == "reverse-sustained" and "motion-anomaly" or "motion-recovered", {
                id = unit.id,
                epoch = unit.epoch,
                model = unit.model,
                reason = signal.kind,
                owner = getPlayerName(unit.owner),
                x = sample.x,
                y = sample.y,
                heading = sample.rz,
                speed = signal.motion.speed,
                alignment = signal.motion.alignment,
                signedForwardSpeed = signal.motion.signedForwardSpeed,
                reverseDurationMs = signal.duration,
                spawnRotation = unit.spawnRotation,
                distance = unit.motionTelemetry.distance or 0,
                samples = unit.motionTelemetry.samples or 0,
            })
        end
        return
    end
    if evidence == "observer-sample" and client ~= unit.owner and unit.state == "active" then
        if data.pedSyncer == true or data.vehicleSyncer == true or tonumber(data.seat) ~= 0 then return fail(unit, "observer-authority-invalid") end
        if not unit.participants[client] then return end
        if type(data.passengers) ~= "table" or #data.passengers ~= #(unit.passengers or {}) then return fail(unit, "observer-passenger-evidence-invalid") end
        for index, passenger in ipairs(unit.passengers or {}) do
            local sample = data.passengers[index]
            if type(sample) ~= "table" or sample.ped ~= passenger.ped or tonumber(sample.expectedSeat) ~= passenger.seat or
                tonumber(sample.seat) ~= passenger.seat or sample.occupied ~= true or sample.syncer == true then
                return fail(unit, "observer-passenger-state-lost")
            end
        end
        local x, y, z = tonumber(data.x), tonumber(data.y), tonumber(data.z)
        local seq = tonumber(data.seq)
        local observer = unit.observerEvidence[client] or {lastSeq = 0, uniqueSamples = 0, distance = 0}
        if not seq or seq % 1 ~= 0 or seq <= observer.lastSeq then return fail(unit, "observer-sequence-invalid") end
        observer.lastSeq = seq
        unit.observerEvidence[client] = observer
        if not x or not y or not z then return end
        local errorDistance, history = nearestServerHistory(unit, x, y, z)
        if not history or errorDistance > 15 or observer.lastHistorySeq == history.seq then return end
        observer.lastHistorySeq = history.seq
        observer.uniqueSamples = observer.uniqueSamples + 1
        observer.maximumError = math.max(observer.maximumError or 0, errorDistance)
        if observer.lastX then observer.distance = observer.distance + getDistanceBetweenPoints2D(observer.lastX, observer.lastY, x, y) end
        observer.lastX, observer.lastY, observer.lastZ = x, y, z
        unit.observerSamples = (unit.observerSamples or 0) + 1
        return
    end
    if evidence == "released" and client == unit.owner and unit.state == "revoking" then
        clearTimer(unit, "handoffTimer")
        local nextOwner = unit.pendingOwner
        local x, y, z = finiteNumber(data.x, 10000), finiteNumber(data.y, 10000), finiteNumber(data.z, 2000)
        local rx, ry, rz = finiteNumber(data.rx, 3600), finiteNumber(data.ry, 3600), finiteNumber(data.rz, 3600)
        local vx, vy, vz = finiteNumber(data.vx, 3), finiteNumber(data.vy, 3), finiteNumber(data.vz, 3)
        local avx, avy, avz = finiteNumber(data.avx, 1), finiteNumber(data.avy, 1), finiteNumber(data.avz, 1)
        local serverX, serverY, serverZ = getElementPosition(unit.vehicle)
        if not x or not y or not z or not rx or not ry or not rz or not vx or not vy or not vz or not avx or not avy or not avz or
            getDistanceBetweenPoints3D(x, y, z, serverX, serverY, serverZ) > 15 or math.sqrt(vx * vx + vy * vy + vz * vz) > 3 or
            math.sqrt(avx * avx + avy * avy + avz * avz) > 1 then
            return fail(unit, "release-snapshot-invalid")
        end
        unit.resumeKinematics = {
            x = x, y = y, z = z, rx = rx, ry = ry, rz = rz,
            vx = vx, vy = vy, vz = vz, avx = avx, avy = avy, avz = avz,
            capturedAt = getTickCount(),
        }
        unit.handoffMetrics = {
            startedAt = getTickCount(), preX = x, preY = y, preZ = z, preHeading = rz,
            preSpeed = math.sqrt(vx * vx + vy * vy + vz * vz), release = data,
        }
        forEachOccupant(unit, function(ped)
            if isElement(ped) then setElementSyncer(ped, false) end
        end)
        setElementSyncer(unit.vehicle, false)
        unit.owner = nil
        unit.pendingOwner = nil
        local reason = unit.pendingHandoffReason or "handoff"
        unit.pendingHandoffReason = nil
        if unit.pendingTakeover then
            local player = unit.pendingTakeover
            unit.pendingTakeover = nil
            return finalizePlayerTakeover(unit, player)
        end
        assign(unit, nextOwner, reason)
        if reason == "test-handoff" then
            unit.handoffEpoch = unit.epoch
            unit.handoffX, unit.handoffY, unit.handoffZ = getElementPosition(unit.vehicle)
            unit.handoffPassed = false
        elseif reason == "stuck-restart" then
            unit.recoveryEpoch = unit.epoch
            unit.recoveryPending = nil
            if activeTest and activeTest.unit == unit and
                (activeTest.mode == "lifecycle" or (activeTest.mode == "soak" and activeTest.scenario == "lifecycle")) then
                activeTest.phase = "recovering"
            end
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
    if activeTest and activeTest.mode == "interaction" and client == activeTest.interactionPlayer and
        tostring(stage):find("^interaction%-.*%-timeout$") then
        terminalTestFailure(tostring(stage), activeTest.unit)
    end
end)

addEvent("carTraffic:cleanupAck", true)
addEventHandler("carTraffic:cleanupAck", resourceRoot, function(id, epoch, proof)
    if not activeTest or not activeTest.cleanupExpected then return end
    local expected = activeTest.cleanupExpected[tonumber(id)]
    if not expected or tonumber(epoch) ~= expected.epoch then return end
    if not expected.participants[client] then return end
    if type(proof) ~= "table" or proof.registryEmpty ~= true or proof.leasesReleased ~= true or proof.taskStopped ~= true or
        proof.missionActorRestored ~= true or proof.passengerLeaseRegistryEmpty ~= true or
        (proof.hadUnit ~= true and proof.hadTombstone ~= true) then
        return terminalTestFailure("cleanup-proof-invalid")
    end
    expected.proofs = expected.proofs or {}
    expected.proofs[client] = proof
    expected.acknowledgements[client] = true
    local count = 0
    for player in pairs(expected.acknowledgements) do if isElement(player) then count = count + 1 end end
    if count >= expected.count then expected.complete = true end
    finishTestCleanupIfReady()
end)

local function populationTick()
    if not population.enabled then return end
    local ps = players()
    if #ps == 0 then return end
    if #getElementsByType("ped") >= PRODUCTION_PED_POOL_SOFT_LIMIT and not population.testDensity then return end

    for session, pending in pairs(pendingCandidates) do
        if getTickCount() - pending.requestedAt > 15000 then
            pendingCandidates[session] = nil
            candidateReservations[pending.owner] = nil
            trace("candidate-timeout", {session = session, mode = pending.mode})
        end
    end

    local mode = type(population.testDensity) == "string" and population.testDensity or "production"
    local desired = populationDesired(ps, mode)
    local relevant = unitCount(function(unit) return unit.mode == mode end)
    if relevant > desired then
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
        for index = 1, relevant - desired do
            local excessUnit = excess[index].unit
            if activeTest and activeTest.mode == "spatial" and activeTest.units and activeTest.units[excessUnit.id] then
                registerTestCleanup(excessUnit)
            end
            destroyUnit(excessUnit, "density-reconcile")
        end
        relevant = desired
    end

    if mode == "production" and not population.demo and relevant >= desired then
        local excessUnit = productionRebalanceUnit(ps, desired)
        if excessUnit then
            destroyUnit(excessUnit, "bubble-rebalance")
            relevant = relevant - 1
        end
    end

    if activeTest and activeTest.mode == "spatial" and mode == "spatial" then
        local stable = {}
        for _, unit in pairs(activeTest.units) do
            if units[unit.id] == unit and unit.state == "active" and unit.stableEpoch == unit.epoch then stable[#stable + 1] = unit end
        end
        if activeTest.phase == "bubbles" and #stable >= 2 then
            local distinctAnchors = stable[1].anchorPlayer ~= stable[2].anchorPlayer
            local separation = distanceToUnit(stable[1].anchorPlayer, stable[2])
            if not distinctAnchors or separation < 500 then return terminalTestFailure("spatial-bubbles-invalid") end
            trace("PASS-spatial-bubbles", {units = #stable, separation = separation, owners = 2})
            local participants = players()
            for index, participant in ipairs(participants) do setElementPosition(participant, 1540 + index * 2, -1675, 13.55) end
            population.cap = 1
            activeTest.phase = "cap-down"
        elseif activeTest.phase == "cap-down" and relevant <= 1 then
            trace("PASS-spatial-cap-down", {units = relevant, cap = 1})
            population.cap = 2
            activeTest.phase = "cap-up"
        elseif activeTest.phase == "cap-up" and #stable >= 2 then
            local observable = 0
            for _, unit in ipairs(stable) do if qualifiedObserverCount(unit) >= 1 then observable = observable + 1 end end
            if observable >= 2 then
                population.enabled = false
                if isTimer(population.timer) then killTimer(population.timer) end
                population.timer = nil
                trace("PASS-spatial", {units = #stable, cap = population.cap, observable = observable})
                activeTest.cleanupFinalizing = true
                for _, unit in ipairs(stable) do registerTestCleanup(unit) end
                for _, unit in ipairs(stable) do destroyUnit(unit, "spatial-test-complete") end
                return
            end
        end
    end
    if relevant + pendingCount() >= desired then return end

    local useProductionBubbles = mode == "production" and not population.demo
    local owner = useProductionBubbles and chooseProductionOwner(ps) or chooseOwner()
    if not useProductionBubbles and (not owner or ownerHasPendingCandidate(owner)) then
        for _, candidateOwner in ipairs(ps) do
            if not ownerHasPendingCandidate(candidateOwner) then owner = candidateOwner break end
        end
    end
    if owner and not ownerHasPendingCandidate(owner) then requestCandidate(owner, mode, 1) end
end

local function startPopulation(targetPerPlayer, cap, testDensity, targetPerPlayerLimit, demoMode)
    trafficGeneration = trafficGeneration + 1
    pendingCandidates = {}
    candidateReservations = {}
    population.targetPerPlayer = math.max(
        1,
        math.min(tonumber(targetPerPlayerLimit) or PRODUCTION_TARGET_PER_BUBBLE,
                 tonumber(targetPerPlayer) or PRODUCTION_TARGET_PER_BUBBLE))
    population.cap = math.max(1, math.min(PRODUCTION_GLOBAL_CAP, tonumber(cap) or PRODUCTION_GLOBAL_CAP))
    population.testDensity = type(testDensity) == "string" and testDensity or false
    population.demo = demoMode == true
    population.enabled = true
    if isTimer(population.timer) then killTimer(population.timer) end
    population.timer = setTimer(populationTick, PRODUCTION_REFILL_INTERVAL, 0)
    populationTick()
    trace("population-start", {
        targetPerPlayer = population.targetPerPlayer, cap = population.cap, testDensity = population.testDensity, demo = population.demo,
    })
end

local function stopPopulation(reason, destroyOwned)
    trafficGeneration = trafficGeneration + 1
    pendingCandidates = {}
    candidateReservations = {}
    population.enabled = false
    population.testDensity = false
    population.demo = false
    if isTimer(population.timer) then killTimer(population.timer) end
    population.timer = nil
    if destroyOwned then
        destroyUnitsWhere(reason or "population-stop", function(unit) return unit.mode == "production" or unit.mode == "density" end)
    end
    trace("population-stop", {reason = reason, remaining = unitCount()})
end

local function setVehicleTrafficDemo(requested)
    requested = requested == true
    if requested then
        if activeTest then
            trace("demo-refused", {reason = "test-active"})
            return false, "test-active"
        end
        if #players() ~= 2 then
            trace("demo-refused", {reason = "exactly-two-clients-required", players = #players()})
            return false, "exactly-two-clients-required"
        end
        if not demoPopulationSnapshot then
            demoPopulationSnapshot = {
                enabled = population.enabled,
                targetPerPlayer = population.targetPerPlayer,
                cap = population.cap,
            }
        end
        -- Existing production units may contain passengers. Recreate the
        -- visual preset from zero so its 16 vehicles reserve exactly 16 ped
        -- slots for drivers and leave the promised 50 slots for foot traffic.
        destroyUnitsWhere("demo-reset", function(unit) return unit.mode == "production" or unit.mode == "density" end)
        startPopulation(8, 16, false, 8, true)
        trace("demo-start", {targetPerPlayer = 8, cap = 16, players = 2})
        return true
    end

    if not demoPopulationSnapshot then
        trace("demo-stop", {restored = false, alreadyStopped = true})
        return true
    end
    local snapshot = demoPopulationSnapshot
    demoPopulationSnapshot = false
    stopPopulation("demo-stop", true)
    if snapshot and snapshot.enabled then startPopulation(snapshot.targetPerPlayer, snapshot.cap, false) end
    trace("demo-stop", {restored = snapshot and snapshot.enabled == true or false})
    return true
end

local function handleTrafficCommand(player, action, mode, value)
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
            if mode == "spatial" and index == 2 then
                setElementPosition(participant, -1985, 138, 27.7)
            elseif mode == "highway" then
                -- Keep both headless participants close enough to stream the
                -- tunnel while leaving the tested carriageways unobstructed.
                setElementPosition(participant, 1840 + index * 2, -2625, 14)
            else
                setElementPosition(participant, 1540 + index * 2, -1675, 13.55)
            end
            setElementRotation(participant, 0, 0, 90)
            setCameraTarget(participant, participant)
        end
        if mode == "density" then
            local expected = math.min(4, #ps * 2)
            activeTest = {mode = "density", units = {}, expectedUnits = expected, startedAt = getTickCount()}
            armTestWatchdog(240000)
            return setTimer(function() startPopulation(2, expected, "density") end, 1500, 1)
        end
        if mode == "spatial" then
            if #ps < 2 then return trace("test-refused", {reason = "two-clients-required", mode = mode}) end
            activeTest = {mode = "spatial", units = {}, expectedUnits = 2, phase = "bubbles", startedAt = getTickCount()}
            armTestWatchdog(300000)
            return setTimer(function() startPopulation(1, 2, "spatial") end, 1500, 1)
        end
        if mode == "highway" then
            if #ps < 2 then return trace("test-refused", {reason = "two-clients-required", mode = mode}) end
            activeTest = {mode = "highway", highwayCase = 1, startedAt = getTickCount()}
            armTestWatchdog(180000)
            return setTimer(startHighwayCase, 1500, 1)
        end
        if mode ~= "fixture" and mode ~= "all" and mode ~= "lifecycle" and mode ~= "soak" and mode ~= "passengers" and mode ~= "classes" and
            mode ~= "interaction" and mode ~= "smooth" and mode ~= "spatial" and mode ~= "ownerquit" and mode ~= "highway" then
            return trace("test-refused", {reason = "unknown-test-mode", mode = mode})
        end
        activeTest = {
            mode = mode, startedAt = getTickCount(), cycle = 1,
            cycles = mode == "soak" and math.max(4, math.min(10, tonumber(value) or 6)) or 1,
            scenario = mode == "soak" and SOAK_SCENARIOS[1] or nil,
            classIndex = mode == "classes" and 1 or nil,
        }
        armTestWatchdog(mode == "soak" and 600000 or 180000)
        setTimer(requestCandidate, 1500, 1, owner, mode, 1)
    elseif action == "start" then
        if activeTest then return trace("population-refused", {reason = "test-active"}) end
        if demoPopulationSnapshot then return trace("population-refused", {reason = "demo-active"}) end
        startPopulation(mode, value, false)
    elseif action == "demo" then
        local requested = tostring(mode or "on"):lower()
        if requested == "on" or requested == "off" then
            setVehicleTrafficDemo(requested == "on")
        else
            trace("usage", {command = "cartraffic demo on|off"})
        end
    elseif action == "stop" then
        demoPopulationSnapshot = false
        stopPopulation("command-stop", true)
    elseif action == "status" then
        local ps = players()
        trace("status", {enabled = population.enabled, units = unitCount(), pending = pendingCount(), cap = population.cap,
            targetPerBubble = population.targetPerPlayer, bubbles = #populationBubbles(ps), desired = populationDesired(ps, "production"),
            demo = population.demo, test = activeTest and activeTest.mode or false})
    elseif action == "cleanup" then
        demoPopulationSnapshot = false
        stopPopulation("command-cleanup", false)
        if activeTest and isTimer(activeTest.watchdogTimer) then killTimer(activeTest.watchdogTimer) end
        activeTest = false
        destroyUnitsWhere("command-cleanup")
        clearTakeovers(false, "command-cleanup")
    else
        trace("usage", {command = "cartraffic test fixture|all|smooth|ownerquit|lifecycle|density|spatial|passengers|classes|interaction|highway|soak [cycles] | cartraffic start [perBubble] [cap] | demo on|off | stop | status | cleanup"})
    end
end

addCommandHandler("cartraffic", function(player, _, action, mode, value)
    if isElement(player) and not hasObjectPermissionTo(player, "function.kickPlayer", false) then
        outputChatBox("Vehicle traffic controls are restricted to server staff", player, 255, 100, 80)
        return
    end
    handleTrafficCommand(player, action, mode, value)
end)

addCommandHandler("trafficdemo", function(player, _, action)
    if isElement(player) and not hasObjectPermissionTo(player, "function.kickPlayer", false) then
        outputChatBox("Traffic demo controls are restricted to server staff", player, 255, 100, 80)
        return
    end
    action = tostring(action or "on"):lower()
    if action == "on" or action == "off" then
        local requested = action == "on"
        if requested then
            local pedCallOk, pedAccepted, pedReason = pcall(function()
                return exports["native-ped-traffic"]:pedTrafficSetDemo(true, player, true)
            end)
            if not pedCallOk or pedAccepted ~= true then
                return trace("demo-refused", {reason = pedReason or "ped-resource-unavailable"})
            end
            local vehicleAccepted, vehicleReason = setVehicleTrafficDemo(true)
            if not vehicleAccepted then
                pcall(function() exports["native-ped-traffic"]:pedTrafficSetDemo(false, player, true) end)
                return trace("demo-refused", {reason = vehicleReason or "vehicle-preflight"})
            end
        else
            setVehicleTrafficDemo(false)
            pcall(function() exports["native-ped-traffic"]:pedTrafficSetDemo(false, player, true) end)
        end
    else
        trace("usage", {command = "trafficdemo on|off"})
    end
end)

setTimer(function()
    if not demoPopulationSnapshot then return end
    local pedResource = getResourceFromName("native-ped-traffic")
    if not pedResource or getResourceState(pedResource) ~= "running" then
        trace("demo-stop", {reason = "ped-resource-unavailable"})
        setVehicleTrafficDemo(false)
    end
end, 1000, 0)

setTimer(emitProductionTelemetry, PRODUCTION_TELEMETRY_INTERVAL, 0)

-- A file-backed queue keeps the integration harness entirely headless. The
-- deployed test resource consumes one command atomically, while ordinary
-- gameplay continues to use /cartraffic through the same implementation.
setTimer(function()
    local path = "automation-command.txt"
    if not fileExists(path) then return end
    local file = fileOpen(path, true)
    if not file then return end
    local contents = fileRead(file, fileGetSize(file)) or ""
    fileClose(file)
    fileDelete(path)
    local action, mode, value = contents:match("^%s*(%S+)%s*(%S*)%s*(%S*)")
    if not action then return end
    handleTrafficCommand(false, action, mode ~= "" and mode or nil, value ~= "" and value or nil)
end, 250, 0)

addEventHandler("onPlayerQuit", root, function()
    if demoPopulationSnapshot then
        setVehicleTrafficDemo(false)
        pcall(function() exports["native-ped-traffic"]:pedTrafficSetDemo(false, false, true) end)
    end
    readyClients[source] = nil
    populationProfiles[source] = nil
    for vehicle, takeover in pairs(takeoverVehicles) do
        if takeover.player == source then
            if isTimer(takeover.timer) then killTimer(takeover.timer) end
            takeoverVehicles[vehicle] = nil
            if isElement(vehicle) then destroyElement(vehicle) end
            trace("takeover-cleanup", {reason = "player-quit", player = getPlayerName(source)})
        end
    end
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
    elseif activeTest and activeTest.mode == "ownerquit" and activeTest.phase == "await-owner-quit" and activeTest.expectedQuit == source then
        if isTimer(activeTest.ownerQuitFollowTimer) then killTimer(activeTest.ownerQuitFollowTimer) end
        activeTest.ownerQuitFollowTimer = nil
        activeTest.phase = "recovering-owner-quit"
        activeTest.expectedQuit = nil
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
            local nextOwner = chooseOwner(unit, source)
            if nextOwner then
                clearTimer(unit, "handoffTimer")
                local vx, vy, vz = getElementVelocity(unit.vehicle)
                local avx, avy, avz = getElementAngularVelocity(unit.vehicle)
                unit.resumeKinematics = {vx = vx, vy = vy, vz = vz, avx = avx, avy = avy, avz = avz, capturedAt = getTickCount()}
                -- A timed-out owner cannot provide the normal revoke snapshot.
                -- Fence the last authoritative server pose until the new
                -- owner has received it, then let the assignment restore the
                -- validated linear and angular velocity.
                unit.forceTransferFreeze = true
                forEachOccupant(unit, function(ped)
                    if isElement(ped) then setElementSyncer(ped, false) end
                end)
                setElementSyncer(unit.vehicle, false)
                assign(unit, nextOwner, "owner-quit")
                unit.ownerQuitEpoch = unit.epoch
            else
                destroyUnit(unit, "owner-quit-no-fallback")
            end
        end
    end
end)

-- A connected player is not eligible for ownership until its client script has
-- registered every traffic event. This explicit handshake avoids guessing how
-- long the initial resource download and client startup will take.
addEvent("carTraffic:clientReady", true)
addEventHandler("carTraffic:clientReady", resourceRoot, function()
    local player = client
    if not isElement(player) or getElementType(player) ~= "player" then return end
    readyClients[player] = true
    trace("client-ready", {client = getPlayerName(player)})
    if getElementDimension(player) ~= 0 or getElementInterior(player) ~= 0 then return end
    for _, unit in pairs(units) do
        if not unit.removing and isElement(unit.ped) and isElement(unit.vehicle) and distanceToUnit(player, unit) <= RESIDENCY_DISTANCE then
            unit.participants[player] = true
            unit.attached[player] = true
            triggerClientEvent(player, "carTraffic:observe", resourceRoot, unit.id, unit.epoch, unit.ped, unit.vehicle, passengerPayload(unit))
        end
    end
end)

addEventHandler("onVehicleStartEnter", root, function(player, seat)
    for _, unit in pairs(units) do
        if source == unit.vehicle then
            if seat ~= 0 then
                for index, passenger in ipairs(unit.passengers or {}) do
                    if passenger.seat == seat then
                        cancelEvent()
                        table.remove(unit.passengers, index)
                        if isElement(passenger.ped) then destroyElement(passenger.ped) end
                        unit.pendingPassengerEntry = {player = player, seat = seat}
                        if not beginRevoke(unit, unit.owner, "passenger-seat-release") then return fail(unit, "passenger-release-refused") end
                        trace("passenger-seat-release", {id = unit.id, epoch = unit.epoch, seat = seat, player = getPlayerName(player)})
                        return
                    end
                end
                return
            end
            cancelEvent()
            if unit.state ~= "active" or not beginRevoke(unit, player, "player-takeover") then return fail(unit, "takeover-revoke-refused") end
            unit.pendingTakeover = player
            return
        end
    end
end)

addEventHandler("onVehicleEnter", root, function(player, seat)
    for _, unit in pairs(units) do
        if source == unit.vehicle and activeTest and activeTest.unit == unit and activeTest.mode == "interaction" and
            activeTest.phase == "passenger-request" and player == activeTest.interactionPlayer and seat == 1 then
            activeTest.phase = "passenger-ride"
            activeTest.interactionEpoch = unit.epoch
            setElementFrozen(unit.vehicle, false)
            triggerClientEvent(unit.owner, "carTraffic:testResumeDrive", resourceRoot, unit.id, unit.epoch)
            unit.lastMovingAt = getTickCount()
            activeTest.passengerRideX, activeTest.passengerRideY, activeTest.passengerRideZ = getElementPosition(unit.vehicle)
            trace("PASS-player-passenger-enter", {id = unit.id, epoch = unit.epoch, player = getPlayerName(player), seat = seat})
            return
        end
    end
    local takeover = takeoverVehicles[source]
    if not takeover or takeover.player ~= player or seat ~= 0 then return end
    if getVehicleOccupant(source, 0) ~= player or getElementData(source, "neon:ambientVehicleTraffic") ~= false or
        getElementData(source, "neon:ambientVehicleTrafficId") ~= false or getElementData(source, "neon:ambientVehicleTrafficEpoch") ~= false then
        if takeover.test and activeTest == takeover.test then return terminalTestFailure("takeover-postcondition-invalid") end
        return
    end
    if isTimer(takeover.timer) then killTimer(takeover.timer) end
    takeoverVehicles[source] = nil
    trace("PASS-takeover", {player = getPlayerName(player), vehiclePreserved = true, occupant = true})
    if takeover.test and activeTest == takeover.test then
        activeTest.waitForTakeoverEnter = false
        if isElement(source) then destroyElement(source) end
        finishTestCleanupIfReady()
    end
end)

addEventHandler("onVehicleExit", root, function(player, seat)
    if not activeTest or activeTest.mode ~= "interaction" or activeTest.phase ~= "passenger-exit-request" or
        player ~= activeTest.interactionPlayer or seat ~= 1 then
        return
    end
    local unit = activeTest.unit
    if not unit or source ~= unit.vehicle or units[unit.id] ~= unit then return terminalTestFailure("passenger-exit-unit-lost", unit) end
    if isTimer(activeTest.interactionBrakeTimer) then killTimer(activeTest.interactionBrakeTimer) end
    activeTest.interactionBrakeTimer = nil
    trace("PASS-player-passenger-exit", {id = unit.id, epoch = unit.epoch, player = getPlayerName(player), distance = 15})
    activeTest.phase = "takeover-request"
    local x, y, z = getElementPosition(unit.vehicle)
    setElementPosition(player, x + 3, y, z + 1)
    triggerClientEvent(player, "carTraffic:testEnterVehicle", resourceRoot, unit.vehicle, 0)
end)

addEventHandler("onElementDestroy", root, function()
    for _, unit in pairs(units) do
        local member = source == unit.ped or source == unit.vehicle
        for _, passenger in ipairs(unit.passengers or {}) do
            if source == passenger.ped then member = true break end
        end
        if not unit.removing and member then
            return fail(unit, "member-destroyed")
        end
    end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    if demoPopulationSnapshot then
        pcall(function() exports["native-ped-traffic"]:pedTrafficSetDemo(false, false, true) end)
    end
    trafficGeneration = trafficGeneration + 1
    pendingCandidates = {}
    candidateReservations = {}
    destroyUnitsWhere("resource-stop")
    clearTakeovers(false, "resource-stop")
end)

addEventHandler("onResourceStart", resourceRoot, function()
    startPopulation(PRODUCTION_TARGET_PER_BUBBLE, PRODUCTION_GLOBAL_CAP, false)
    trace("ready", {
        models = ALLOWED_MODEL_COUNT,
        contextualGroups = 20,
        autoStart = true,
        targetPerBubble = PRODUCTION_TARGET_PER_BUBBLE,
        cap = PRODUCTION_GLOBAL_CAP,
        pedPoolSoftLimit = PRODUCTION_PED_POOL_SOFT_LIMIT,
    })
end)
