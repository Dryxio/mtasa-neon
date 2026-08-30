local activeRun
local serial = 0

local TRACE_RECORD_LIMIT = 256
local HEALTH_EPSILON = 0.1
local SAMPLE_RETRY_MS = 1200
local GLOBAL_TIMEOUT_MS = 60000

local function compact(value, limit)
    local text = tostring(value == nil and "nil" or value):gsub("[\r\n]", " ")
    return text:sub(1, limit or 300)
end

local function encode(value)
    local encoded = toJSON(value, true)
    return encoded and encoded:gsub("[\r\n]", "") or "{}"
end

local function playerLabel(player)
    return isElement(player) and getPlayerName(player) or "console"
end

local function trace(event, fields, forceFile)
    local run = activeRun
    local record = {
        event = event,
        run = run and run.id or 0,
        generation = run and run.generation or 0,
        phase = run and run.phase or "idle",
        tick = getTickCount(),
    }
    for key, value in pairs(type(fields) == "table" and fields or {}) do
        if type(value) ~= "userdata" then
            record[key] = type(value) == "string" and compact(value, 500) or value
        end
    end
    local line = encode(record) .. "\n"
    outputServerLog("[vehicle-health-sync-jsonl] " .. line:gsub("\n$", ""))
    if run and run.traceFile and (run.traceRecords < TRACE_RECORD_LIMIT or forceFile) then
        fileWrite(run.traceFile, line)
        fileFlush(run.traceFile)
        run.traceRecords = run.traceRecords + 1
    end
end

local function snapshotPlayer(player)
    local x, y, z = getElementPosition(player)
    local rx, ry, rz = getElementRotation(player)
    return {
        x = x,
        y = y,
        z = z,
        rx = rx,
        ry = ry,
        rz = rz,
        interior = getElementInterior(player),
        dimension = getElementDimension(player),
        frozen = isElementFrozen(player),
        alpha = getElementAlpha(player),
        vehicle = getPedOccupiedVehicle(player),
        seat = getPedOccupiedVehicleSeat(player),
    }
end

local function restorePlayer(player, state)
    if not isElement(player) or type(state) ~= "table" then
        return false, "player_missing"
    end
    if isPedInVehicle(player) then
        removePedFromVehicle(player)
    end
    setElementInterior(player, state.interior)
    setElementDimension(player, state.dimension)
    setElementPosition(player, state.x, state.y, state.z)
    setElementRotation(player, state.rx, state.ry, state.rz)
    setElementFrozen(player, state.frozen)
    setElementAlpha(player, state.alpha)
    if isElement(state.vehicle) then
        if not warpPedIntoVehicle(player, state.vehicle, state.seat) then
            return false, "original_seat_restore_refused"
        end
    end
    if getElementInterior(player) ~= state.interior or getElementDimension(player) ~= state.dimension then
        return false, "original_world_not_restored"
    end
    return true
end

local function stopTimers(run)
    if isTimer(run.monitorTimer) then
        killTimer(run.monitorTimer)
    end
    if isTimer(run.globalTimer) then
        killTimer(run.globalTimer)
    end
end

local function cleanupRun(run, restorePlayers)
    stopTimers(run)
    local problems = {}
    for player in pairs(run.players) do
        if isElement(player) then
            triggerClientEvent(player, "vehicleHealthHarness:stop", resourceRoot, run.id)
        end
    end
    if restorePlayers then
        -- Restore seats only after every selected player has left the harness
        -- vehicle, so two occupants from the same original car remain valid.
        for player in pairs(run.players) do
            if isElement(player) and isPedInVehicle(player) then
                removePedFromVehicle(player)
            end
        end
        for player, state in pairs(run.players) do
            local restored, reason = restorePlayer(player, state)
            if not restored then
                problems[#problems + 1] = playerLabel(player) .. ":" .. tostring(reason)
            end
        end
    end
    if isElement(run.vehicle) then
        destroyElement(run.vehicle)
    end
    if isElement(run.vehicle) then
        problems[#problems + 1] = "test_vehicle_survived"
    end
    return #problems == 0, table.concat(problems, ",")
end

local function finish(verdict, reason, fields)
    local run = activeRun
    if not run or run.finished then
        return
    end
    run.finished = true
    local cleanupPassed, cleanupReason = cleanupRun(run, true)
    if not cleanupPassed and verdict == "PASS" then
        verdict = "FAIL"
        reason = "cleanup_failed:" .. cleanupReason
    end
    local terminal = {
        verdict = verdict,
        reason = compact(reason, 500),
        expected = fields and fields.expected or "n/a",
        actual = fields and fields.actual or "n/a",
        player = fields and fields.player or "n/a",
        cleanup = cleanupPassed,
        cleanupReason = cleanupReason,
    }
    trace(verdict, terminal, true)
    outputServerLog(("[vehicle-health-sync-test] %s phase=%s expected=%s actual=%s player=%s reason=%s cleanup=%s"):format(
                        verdict, tostring(run.phase), tostring(terminal.expected), tostring(terminal.actual),
                        tostring(terminal.player), tostring(terminal.reason), tostring(cleanupPassed)))
    if run.traceFile then
        fileClose(run.traceFile)
        run.traceFile = nil
    end
    activeRun = nil
end

local function fail(expected, actual, player, reason)
    finish("FAIL", reason, {
        expected = compact(expected),
        actual = compact(actual),
        player = playerLabel(player),
    })
end

local function healthMatches(actual, expected)
    return type(actual) == "number" and type(expected) == "number" and
        math.abs(actual - expected) <= HEALTH_EPSILON
end

local issueSampleRound
local beginVerification

local function completeVerification()
    local run = activeRun
    if not run or run.finished then
        return
    end
    local verification = run.verification
    run.lastHealth = {server = getElementHealth(run.vehicle), clients = {}}
    for player, ack in pairs(verification.lastAcks) do
        run.lastHealth.clients[player] = ack.health
    end
    trace("phase_pass", {
        expected = verification.expected,
        samples = verification.stableSamples,
        elapsed = getTickCount() - verification.startedAt,
        server = run.lastHealth.server,
    })
    run.awaiting = nil
    verification.onPass()
end

local function evaluateSampleRound()
    local run = activeRun
    if not run or run.finished or not run.awaiting or run.awaiting.kind ~= "sample" then
        return
    end
    local verification = run.verification
    local serverActual = isElement(run.vehicle) and getElementHealth(run.vehicle) or nil
    local serverSyncer = isElement(run.vehicle) and getElementSyncer(run.vehicle) or nil
    local serverOccupied = isElement(run.vehicle) and getVehicleOccupant(run.vehicle, 0) == run.owner or false
    local mismatch

    if not healthMatches(serverActual, verification.expected) then
        mismatch = {expected = verification.expected, actual = serverActual, player = false,
                    reason = "server_health_mismatch"}
    elseif serverSyncer ~= run.owner then
        mismatch = {expected = playerLabel(run.owner), actual = playerLabel(serverSyncer), player = false,
                    reason = "server_syncer_mismatch"}
    elseif serverOccupied ~= verification.occupied then
        mismatch = {expected = verification.occupied, actual = serverOccupied, player = false,
                    reason = "server_occupancy_mismatch"}
    end

    for player, ack in pairs(run.awaiting.acks) do
        local expectedSyncer = player == run.owner
        if not mismatch and ack.streamed ~= true then
            mismatch = {expected = "streamed", actual = ack.streamed, player = player,
                        reason = "client_not_streamed"}
        elseif not mismatch and not healthMatches(ack.health, verification.expected) then
            mismatch = {expected = verification.expected, actual = ack.health, player = player,
                        reason = "client_health_mismatch"}
        elseif not mismatch and ack.syncer ~= expectedSyncer then
            mismatch = {expected = expectedSyncer, actual = ack.syncer, player = player,
                        reason = "client_syncer_role_mismatch"}
        elseif not mismatch and ack.occupied ~= verification.occupied then
            mismatch = {expected = verification.occupied, actual = ack.occupied, player = player,
                        reason = "client_occupancy_mismatch"}
        elseif not mismatch and (ack.dimension ~= run.dimension or ack.interior ~= 0) then
            mismatch = {expected = "world=0/" .. run.dimension,
                        actual = "world=" .. tostring(ack.interior) .. "/" .. tostring(ack.dimension),
                        player = player, reason = "client_world_mismatch"}
        end

        local previous = run.lastHealth and run.lastHealth.clients[player]
        if not mismatch and verification.delta and
            (type(previous) ~= "number" or math.abs((ack.health - previous) - verification.delta) > HEALTH_EPSILON) then
            mismatch = {expected = verification.delta, actual = type(previous) == "number" and ack.health - previous or nil,
                        player = player, reason = "client_delta_mismatch"}
        end
    end
    local previousServer = run.lastHealth and run.lastHealth.server
    if not mismatch and verification.delta and
        (type(previousServer) ~= "number" or math.abs((serverActual - previousServer) - verification.delta) > HEALTH_EPSILON) then
        mismatch = {expected = verification.delta,
                    actual = type(previousServer) == "number" and serverActual - previousServer or nil,
                    player = false, reason = "server_delta_mismatch"}
    end

    verification.lastAcks = run.awaiting.acks
    trace(mismatch and "sample_mismatch" or "sample_ok", {
        request = run.awaiting.requestId,
        expected = verification.expected,
        actual = mismatch and mismatch.actual or serverActual,
        player = mismatch and playerLabel(mismatch.player) or "all",
        reason = mismatch and mismatch.reason or "stable",
        stable = mismatch and 0 or verification.stableSamples + 1,
    })
    run.awaiting = nil
    if mismatch then
        verification.stableSamples = 0
        verification.lastMismatch = mismatch
    else
        verification.stableSamples = verification.stableSamples + 1
    end

    local elapsed = getTickCount() - verification.startedAt
    if verification.stableSamples >= verification.requiredSamples and elapsed >= verification.minimumDuration then
        return completeVerification()
    end
    run.nextSampleAt = getTickCount() + verification.interval
end

issueSampleRound = function()
    local run = activeRun
    if not run or run.finished or run.awaiting then
        return
    end
    run.requestSerial = run.requestSerial + 1
    run.awaiting = {kind = "sample", requestId = run.requestSerial, sentAt = getTickCount(), acks = {}}
    for player in pairs(run.players) do
        triggerClientEvent(player, "vehicleHealthHarness:sample", resourceRoot, run.id, run.generation,
                           run.requestSerial, run.phase, run.vehicle, run.owner)
    end
end

beginVerification = function(phase, expected, options, onPass)
    local run = activeRun
    run.phase = phase
    run.generation = run.generation + 1
    run.awaiting = nil
    run.verification = {
        expected = expected,
        occupied = options.occupied,
        delta = options.delta,
        startedAt = getTickCount(),
        deadline = getTickCount() + options.timeout,
        minimumDuration = options.minimumDuration,
        requiredSamples = options.samples,
        interval = options.interval or 400,
        stableSamples = 0,
        lastAcks = {},
        onPass = onPass,
    }
    run.nextSampleAt = getTickCount()
    trace("phase_start", {expected = expected, occupied = options.occupied, delta = options.delta,
                          samples = options.samples, minimumDuration = options.minimumDuration})
end

local function beginMutation(phase, expected, onAccepted)
    local run = activeRun
    run.phase = phase
    run.generation = run.generation + 1
    run.mutationSerial = run.mutationSerial + 1
    run.awaiting = {
        kind = "mutation",
        mutationId = run.mutationSerial,
        expected = expected,
        sentAt = 0,
        deadline = getTickCount() + 5000,
        onAccepted = onAccepted,
    }
    trace("mutation_requested", {expected = expected, player = playerLabel(run.owner)})
end

local function beginRemoteServerSet()
    local run = activeRun
    run.phase = "remote_server_set_2400"
    -- Clients may only lower authoritative vehicle health. Raising 1800 to
    -- 2400 from a client would correctly hit the server's anti-heal guard and
    -- would test the wrong contract. Publish the repair by RPC, then leave the
    -- unoccupied remote syncer active long enough to catch codec reclamping.
    if not setElementHealth(run.vehicle, 2400) then
        return fail(2400, "setElementHealth_refused", false, "remote_server_health_set_refused")
    end
    trace("server_mutation", {expected = 2400, actual = getElementHealth(run.vehicle), delta = 600})
    beginVerification("unoccupied_remote_sync_2400", 2400,
                      {occupied = false, delta = 600, timeout = 12000, minimumDuration = 4000, samples = 8,
                       interval = 500}, function()
        finish("PASS", "occupied and unoccupied puresync preserved full vehicle health and exact deltas", {
            expected = 2400,
            actual = 2400,
            player = "server+syncer+observer",
        })
    end)
end

local function beginUnoccupiedPhase()
    local run = activeRun
    run.phase = "unoccupied_transition"
    if isPedInVehicle(run.owner) then
        removePedFromVehicle(run.owner)
    end
    setElementPosition(run.owner, 2496.0, -1668.0, 13.4)
    setElementFrozen(run.owner, true)
    if not setElementSyncer(run.vehicle, run.owner, true) then
        return fail(playerLabel(run.owner), "refused", run.owner, "remote_syncer_assignment_refused")
    end
    beginVerification("unoccupied_remote_ready", 1800,
                      {occupied = false, delta = 0, timeout = 8000, minimumDuration = 1500, samples = 4},
                      beginRemoteServerSet)
end

local function beginBelowThresholdMutation()
    beginMutation("syncer_set_1800", 1800, function()
        beginVerification("occupied_puresync_1800", 1800,
                          {occupied = true, delta = -900, timeout = 10000, minimumDuration = 2200, samples = 5},
                          beginUnoccupiedPhase)
    end)
end

local function beginHighClientMutation()
    beginMutation("syncer_set_2700", 2700, function()
        beginVerification("occupied_puresync_2700", 2700,
                          {occupied = true, delta = -400, timeout = 12000, minimumDuration = 3500, samples = 7},
                          beginBelowThresholdMutation)
    end)
end

local function beginServerHighHealth()
    local run = activeRun
    run.phase = "server_set_3100"
    if not setElementHealth(run.vehicle, 3100) then
        return fail(3100, "setElementHealth_refused", false, "server_high_health_set_refused")
    end
    trace("server_mutation", {expected = 3100, actual = getElementHealth(run.vehicle)})
    beginVerification("occupied_server_3100", 3100,
                      {occupied = true, delta = 2100, timeout = 12000, minimumDuration = 4500, samples = 8,
                       interval = 500}, beginHighClientMutation)
end

local function startHarness(requester)
    if activeRun then
        return false, "run_already_active"
    end
    local players = getElementsByType("player")
    table.sort(players, function(a, b)
        return tostring(getPlayerSerial(a)) .. tostring(getPlayerName(a)) <
            tostring(getPlayerSerial(b)) .. tostring(getPlayerName(b))
    end)
    if #players < 2 then
        return false, "two_connected_clients_required"
    end
    local owner = isElement(requester) and getElementType(requester) == "player" and requester or players[1]
    local observer
    for _, player in ipairs(players) do
        if player ~= owner then
            observer = player
            break
        end
    end
    if not observer then
        return false, "independent_observer_missing"
    end

    serial = serial + 1
    local tracePath = "@vehicle-health-sync-" .. tostring(serial) .. ".jsonl"
    if fileExists(tracePath) then
        fileDelete(tracePath)
    end
    activeRun = {
        id = serial,
        generation = 0,
        phase = "setup",
        owner = owner,
        observer = observer,
        players = {[owner] = snapshotPlayer(owner), [observer] = snapshotPlayer(observer)},
        dimension = 63700 + serial % 200,
        requestSerial = 0,
        mutationSerial = 0,
        traceRecords = 0,
        traceFile = fileCreate(tracePath),
    }
    local run = activeRun
    trace("START", {owner = playerLabel(owner), observer = playerLabel(observer), trace = tracePath})

    run.vehicle = createVehicle(492, 2493.0, -1668.0, 13.1, 0, 0, 90)
    if not isElement(run.vehicle) then
        fail("vehicle", "creation_refused", false, "setup_vehicle_creation_failed")
        return false, "vehicle_creation_failed"
    end
    setElementInterior(run.vehicle, 0)
    setElementDimension(run.vehicle, run.dimension)
    setElementHealth(run.vehicle, 1000)
    setVehicleEngineState(run.vehicle, false)
    setVehicleLocked(run.vehicle, true)
    setElementVelocity(run.vehicle, 0, 0, 0)

    for player in pairs(run.players) do
        if isPedInVehicle(player) then
            removePedFromVehicle(player)
        end
        setElementInterior(player, 0)
        setElementDimension(player, run.dimension)
        setElementPosition(player, 2495.0, -1668.0, 13.4)
        setElementFrozen(player, true)
    end
    if not warpPedIntoVehicle(owner, run.vehicle, 0) then
        fail("driver_seat", "warp_refused", owner, "setup_occupied_vehicle_failed")
        return false, "owner_warp_failed"
    end
    setElementFrozen(run.vehicle, false)
    setElementVelocity(run.vehicle, 0, 0, 0)
    if not setElementSyncer(run.vehicle, owner, true) then
        fail(playerLabel(owner), "syncer_refused", owner, "setup_syncer_assignment_failed")
        return false, "syncer_assignment_failed"
    end
    for player in pairs(run.players) do
        triggerClientEvent(player, "vehicleHealthHarness:begin", resourceRoot, run.id, run.generation)
    end

    run.monitorTimer = setTimer(function()
        local current = activeRun
        if not current or current.finished then
            return
        end
        local now = getTickCount()
        if current.verification and now >= current.verification.deadline then
            local mismatch = current.verification.lastMismatch or {}
            return fail(mismatch.expected or current.verification.expected, mismatch.actual or "timeout",
                        mismatch.player, mismatch.reason or "verification_timeout")
        end
        if current.awaiting and current.awaiting.kind == "sample" and
            now - current.awaiting.sentAt >= SAMPLE_RETRY_MS then
            local received = (current.awaiting.acks[current.owner] and 1 or 0) +
                                 (current.awaiting.acks[current.observer] and 1 or 0)
            trace("sample_ack_timeout", {request = current.awaiting.requestId, received = received})
            current.awaiting = nil
            current.nextSampleAt = now
        elseif current.awaiting and current.awaiting.kind == "mutation" then
            if now >= current.awaiting.deadline then
                return fail(current.awaiting.expected, "mutation_ack_timeout", current.owner,
                            "authoritative_mutation_timeout")
            elseif now - current.awaiting.sentAt >= SAMPLE_RETRY_MS then
                current.awaiting.sentAt = now
                triggerClientEvent(current.owner, "vehicleHealthHarness:mutate", resourceRoot, current.id,
                                   current.generation, current.awaiting.mutationId, current.phase, current.vehicle,
                                   current.awaiting.expected)
            end
        elseif current.verification and not current.awaiting and now >= current.nextSampleAt then
            issueSampleRound()
        end
    end, 100, 0)
    run.globalTimer = setTimer(function()
        fail("terminal_PASS", "global_timeout", false, "global_timeout_after_60s")
    end, GLOBAL_TIMEOUT_MS, 1)

    beginVerification("occupied_stream_ready", 1000,
                      {occupied = true, delta = nil, timeout = 8000, minimumDuration = 1500, samples = 4},
                      beginServerHighHealth)
    return true
end

addEvent("vehicleHealthHarness:sampleAck", true)
addEventHandler("vehicleHealthHarness:sampleAck", resourceRoot,
                function(runId, generation, requestId, phase, data)
    local run = activeRun
    local player = client
    if source ~= resourceRoot or not run or run.finished or not run.players[player] or
        run.id ~= tonumber(runId) or run.generation ~= tonumber(generation) or run.phase ~= phase or
        not run.awaiting or run.awaiting.kind ~= "sample" or run.awaiting.requestId ~= tonumber(requestId) or
        type(data) ~= "table" or run.awaiting.acks[player] then
        return
    end
    run.awaiting.acks[player] = data
    if run.awaiting.acks[run.owner] and run.awaiting.acks[run.observer] then
        evaluateSampleRound()
    end
end)

addEvent("vehicleHealthHarness:mutationAck", true)
addEventHandler("vehicleHealthHarness:mutationAck", resourceRoot,
                function(runId, generation, mutationId, phase, data)
    local run = activeRun
    local player = client
    if source ~= resourceRoot or not run or run.finished or player ~= run.owner or run.id ~= tonumber(runId) or
        run.generation ~= tonumber(generation) or run.phase ~= phase or not run.awaiting or
        run.awaiting.kind ~= "mutation" or run.awaiting.mutationId ~= tonumber(mutationId) or type(data) ~= "table" then
        return
    end
    local expectedBefore = run.lastHealth and run.lastHealth.clients[player]
    if data.streamed ~= true or data.syncer ~= true or data.accepted ~= true or
        not healthMatches(data.before, expectedBefore) or not healthMatches(data.after, run.awaiting.expected) then
        return fail("before=" .. tostring(expectedBefore) .. ",after=" .. tostring(run.awaiting.expected),
                    "before=" .. tostring(data.before) .. ",after=" .. tostring(data.after), player,
                    "authoritative_client_mutation_rejected")
    end
    local onAccepted = run.awaiting.onAccepted
    trace("mutation_ack", {expected = run.awaiting.expected, before = data.before, actual = data.after,
                           player = playerLabel(player)})
    run.awaiting = nil
    onAccepted()
end)

addCommandHandler("vehiclehealthsynctest", function(requester)
    local started, reason = startHarness(requester)
    if not started and not activeRun then
        trace("FAIL", {expected = "two_clients_and_vehicle", actual = reason,
                       player = playerLabel(requester), reason = reason})
        outputServerLog(("[vehicle-health-sync-test] FAIL phase=setup expected=two_clients_and_vehicle actual=%s " ..
                            "player=%s reason=%s cleanup=true"):format(tostring(reason), playerLabel(requester),
                                                                       tostring(reason)))
    elseif not started then
        outputServerLog(("[vehicle-health-sync-test] FAIL phase=%s expected=idle actual=active player=%s " ..
                            "reason=%s cleanup=false"):format(activeRun.phase, playerLabel(requester),
                                                               tostring(reason)))
    end
end)

addCommandHandler("vehiclehealthsynctestabort", function(requester)
    if not activeRun then
        return outputServerLog(("[vehicle-health-sync-test] FAIL phase=abort expected=active_run actual=idle " ..
                                   "player=%s reason=no_active_run cleanup=true"):format(playerLabel(requester)))
    end
    finish("FAIL", "manual_abort", {expected = "terminal_PASS", actual = "manual_abort",
                                      player = playerLabel(requester)})
end)

addEventHandler("onPlayerQuit", root, function()
    if activeRun and activeRun.players[source] then
        fail("connected", "quit", source, "required_client_disconnected")
    end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    local run = activeRun
    if not run then
        return
    end
    run.finished = true
    local cleanupPassed, cleanupReason = cleanupRun(run, true)
    trace("FAIL", {verdict = "FAIL", expected = "terminal_PASS", actual = "resource_stop",
                   player = "all", reason = "resource_stop_abort", cleanup = cleanupPassed,
                   cleanupReason = cleanupReason}, true)
    outputServerLog(("[vehicle-health-sync-test] FAIL phase=%s expected=terminal_PASS actual=resource_stop player=all " ..
                        "reason=resource_stop_abort cleanup=%s cleanupReason=%s"):format(
                        run.phase, tostring(cleanupPassed), tostring(cleanupReason)))
    if run.traceFile then
        fileClose(run.traceFile)
    end
    activeRun = nil
end)
