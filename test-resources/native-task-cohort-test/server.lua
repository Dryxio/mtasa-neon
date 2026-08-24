local run
local serial = 0
local autoStarted = false

local function encode(value)
    local encoded = toJSON(value, true)
    return encoded and encoded:gsub("[\r\n]", "") or "{}"
end

local function trace(event, fields)
    if not run then
        return
    end
    local record = {
        event = event,
        run = run.id,
        phase = run.phase,
        tick = getTickCount(),
    }
    for key, value in pairs(type(fields) == "table" and fields or {}) do
        if type(value) ~= "userdata" then
            record[key] = value
        end
    end
    local line = encode(record) .. "\n"
    outputServerLog("[cohort-harness-jsonl] " .. line:gsub("\n$", ""))
    if run.traceFile then
        fileWrite(run.traceFile, line)
        fileFlush(run.traceFile)
    end
end

local function snapshot(player)
    local x, y, z = getElementPosition(player)
    return {
        x = x,
        y = y,
        z = z,
        interior = getElementInterior(player),
        dimension = getElementDimension(player),
        alpha = getElementAlpha(player),
        frozen = isElementFrozen(player),
    }
end

local function restore(player, state)
    if not isElement(player) then
        return
    end
    if isPedInVehicle(player) then
        removePedFromVehicle(player)
    end
    setElementInterior(player, state.interior)
    setElementDimension(player, state.dimension)
    setElementPosition(player, state.x, state.y, state.z)
    setElementAlpha(player, state.alpha)
    setElementFrozen(player, state.frozen)
end

local function cleanup(restorePlayers)
    if not run then
        return
    end
    local old = run
    if isTimer(old.monitorTimer) then
        killTimer(old.monitorTimer)
    end
    if isTimer(old.timeoutTimer) then
        killTimer(old.timeoutTimer)
    end
    if isElement(old.handle) then
        exports["native-task-runtime"]:cancelNativeTaskCohort(old.handle)
    end
    for player, state in pairs(old.players) do
        if isElement(player) then
            triggerClientEvent(player, "cohortHarness:stop", resourceRoot, old.id)
            if restorePlayers then
                restore(player, state)
            end
        end
    end
    for _, element in ipairs(old.elements) do
        if isElement(element) then
            destroyElement(element)
        end
    end
    if old.traceFile then
        fileClose(old.traceFile)
    end
    run = nil
end

local function verdict(passed, reason)
    if not run or run.finished then
        return
    end
    run.finished = true
    trace(passed and "PASS" or "FAIL", {reason = reason})
    outputServerLog(("[native-task-cohort-test] %s: %s"):format(passed and "PASS" or "FAIL", reason))
    setTimer(function()
        cleanup(true)
    end, 1000, 1)
end

local function publishObserverEpoch()
    for player in pairs(run.players) do
        if isElement(player) then
            triggerClientEvent(player, "cohortHarness:observe", resourceRoot, run.id, run.epoch, run.owner,
                               run.driver, run.passenger, run.voodoo)
        end
    end
end

local function ownedBy(expected)
    return getElementSyncer(run.driver) == expected and getElementSyncer(run.passenger) == expected and
        getElementSyncer(run.voodoo) == expected
end

local function advanceFromActive(epoch)
    run.epoch = epoch
    local x, y, z = getElementPosition(run.voodoo)
    run.phaseStart = {x = x, y = y, z = z, tick = getTickCount()}
    run.observerSeen = {}
    publishObserverEpoch()
    trace("active", {epoch = epoch, owner = getPlayerName(run.owner)})
end

local function startHarness(requester)
    if run then
        return false, "un test est deja actif"
    end
    local players = getElementsByType("player")
    if #players < 2 then
        return false, "deux clients sont requis"
    end
    local owner = isElement(requester) and requester or players[1]
    local observer = players[1] == owner and players[2] or players[1]
    serial = serial + 1
    local tracePath = "cohort-test-" .. tostring(serial) .. ".jsonl"
    if fileExists(tracePath) then
        fileDelete(tracePath)
    end
    run = {
        id = serial,
        phase = "setup",
        owner = owner,
        nextOwner = observer,
        players = {},
        elements = {},
        observerSeen = {},
        traceFile = fileCreate(tracePath),
    }
    for _, player in ipairs(players) do
        run.players[player] = snapshot(player)
    end
    trace("start", {owner = getPlayerName(owner), nextOwner = getPlayerName(observer), trace = tracePath})

    local dimension = 64650 + serial % 100
    run.target = createVehicle(492, 1079, -1018, 25, 0, 0, 0)
    run.voodoo = createVehicle(412, 1079, -1084, 25, 0, 0, 0)
    run.driver = createPed(102, 1079, -1084, 26, 0)
    run.passenger = createPed(103, 1079, -1084, 26, 0)
    for _, element in ipairs({run.target, run.voodoo, run.driver, run.passenger}) do
        if not isElement(element) then
            return verdict(false, "creation d'un element refusee")
        end
        run.elements[#run.elements + 1] = element
        setElementDimension(element, dimension)
    end
    setElementFrozen(run.target, true)
    setElementHealth(run.target, 4000)
    setElementHealth(run.voodoo, 2500)
    warpPedIntoVehicle(run.driver, run.voodoo, 0)
    warpPedIntoVehicle(run.passenger, run.voodoo, 1)
    giveWeapon(run.passenger, 28, 9999, true)

    for player in pairs(run.players) do
        setElementInterior(player, 0)
        setElementDimension(player, dimension)
        setElementFrozen(player, true)
        setElementPosition(player, 1082, -1018, 26)
        setElementAlpha(player, player == owner and 255 or 0)
    end
    setElementFrozen(owner, false)
    warpPedIntoVehicle(owner, run.target, 0)
    setElementFrozen(run.target, true)

    local descriptor = {
        members = {
            {
                ped = run.driver,
                vehicle = run.voodoo,
                seat = 0,
                missionActor = true,
                proofs = {bullet = true, fire = true, explosion = true, collision = true, melee = true},
                task = {
                    type = "drive_mission",
                    targetVehicle = run.target,
                    mission = "escort_left",
                    speed = 30,
                    drivingStyle = "avoid_cars",
                },
            },
            {
                ped = run.passenger,
                vehicle = run.voodoo,
                seat = 1,
                missionActor = true,
                proofs = {bullet = true, fire = true, explosion = true, collision = true, melee = true},
                task = {
                    type = "drive_by",
                    target = owner,
                    radius = 5000,
                    style = "ai_all_directions",
                    rightHandSide = true,
                    frequency = 40,
                    reissue = true,
                },
            },
        },
        vehicles = {{vehicle = run.voodoo, straightLineDistance = 10}},
        dependencies = {run.target, owner},
    }
    run.phase = "first_assignment"
    local handle, reason = exports["native-task-runtime"]:createNativeTaskCohort(owner, descriptor,
                                                                                 {fallbackOwners = {observer}})
    if not handle then
        return verdict(false, "creation de cohorte refusee: " .. tostring(reason))
    end
    run.handle = handle
    run.timeoutTimer = setTimer(function()
        verdict(false, "timeout terminal apres 45 s")
    end, 45000, 1)
    run.monitorTimer = setTimer(function()
        if not run or run.finished or not run.phaseStart then
            return
        end
        local x, y, z = getElementPosition(run.voodoo)
        local moved = getDistanceBetweenPoints3D(x, y, z, run.phaseStart.x, run.phaseStart.y, run.phaseStart.z)
        local observerSeen = run.observerSeen[run.phase] == true
        trace("sample", {epoch = run.epoch, moved = moved, observerSeen = observerSeen, authority = ownedBy(run.owner)})
        if run.phase == "first_active" and moved >= 5 and observerSeen and ownedBy(run.owner) then
            run.phase = "handoff"
            trace("handoff_requested", {from = getPlayerName(run.owner), to = getPlayerName(run.nextOwner)})
            local targetX, targetY, targetZ = getElementPosition(run.target)
            setElementPosition(run.target, targetX, targetY + 60, targetZ)
            local accepted, handoffReason = exports["native-task-runtime"]:handoffNativeTaskCohort(run.handle,
                                                                                                    run.nextOwner,
                                                                                                    false)
            if not accepted then
                return verdict(false, "handoff refuse: " .. tostring(handoffReason))
            end
        elseif run.phase == "second_active" and moved >= 5 and observerSeen and ownedBy(run.owner) then
            verdict(true, "deux epochs actifs, handoff atomique et observation passive valides")
        end
    end, 500, 0)
    return true
end

addEventHandler("onNativeTaskCohortStateChange", root, function(state, data)
    if not run or source ~= run.handle then
        return
    end
    trace("runtime_state", {state = state, epoch = data.epoch, reason = data.reason})
    if state == "failed" then
        return verdict(false, "runtime: " .. tostring(data.reason))
    end
    if state == "active" and data.epoch ~= run.epoch then
        if run.phase == "first_assignment" then
            run.phase = "first_active"
        elseif run.phase == "handoff" then
            run.owner, run.nextOwner = run.nextOwner, run.owner
            run.phase = "second_active"
        end
        advanceFromActive(data.epoch)
    end
end)

addEvent("cohortHarness:observerEvidence", true)
addEventHandler("cohortHarness:observerEvidence", resourceRoot, function(id, epoch, isObserver, ownerIsLocal, moving)
    if source ~= resourceRoot or not run or id ~= run.id or epoch ~= run.epoch or not run.players[client] then
        return
    end
    if client ~= run.owner and isObserver == true and ownerIsLocal == false then
        run.observerSeen[run.phase] = true
        trace("observer", {player = getPlayerName(client), moving = moving == true})
    end
end)

addCommandHandler("cohorttest", function(player)
    local ok, reason = startHarness(player)
    if ok == false then
        outputServerLog("[native-task-cohort-test] NOT STARTED: " .. tostring(reason))
    end
end)

addCommandHandler("cohortteststop", function()
    cleanup(true)
end)

addEventHandler("onPlayerQuit", root, function()
    if run and run.players[source] then
        verdict(false, "participant deconnecte")
    end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    cleanup(true)
end)

setTimer(function()
    if not autoStarted and not run and #getElementsByType("player") >= 2 then
        autoStarted = true
        local ok, reason = startHarness(nil)
        if ok == false then
            outputServerLog("[native-task-cohort-test] AUTO NOT STARTED: " .. tostring(reason))
        end
    end
end, 1000, 0)

outputServerLog("[native-task-cohort-test] Ready; auto-start waits for two clients, or use cohorttest.")
