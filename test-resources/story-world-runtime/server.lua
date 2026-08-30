local placements = {}
local placementsById = {}
local teardowns = {}
local teardownsById = {}
local occupancies = {}
local occupanciesById = {}
local playerModelLeases = {}
local playerModelLeaseByPlayer = {}
local fileCutscenes = {}
local fileCutscenesById = {}
local fileCutsceneByPlayer = {}
local vehicleRelocations = {}
local vehicleRelocationsById = {}
local vehicleRelocationReservations = {}
local storyWorldElementReservations = {}
local modelBaseOffsets = {}
local nextPlacementId = 0
local nextTeardownId = 0
local nextOccupancyId = 0
local nextPlayerModelLeaseId = 0
local nextFileCutsceneId = 0
local nextVehicleRelocationId = 0

addEvent("onStoryScmVehicleStateChange", false)
addEvent("onStoryWorldTeardownStateChange", false)
addEvent("onStoryVehicleOccupancyStateChange", false)
addEvent("onStoryPlayerModelLeaseStateChange", false)
addEvent("onStoryFileCutsceneStateChange", false)
addEvent("onStoryVehicleRelocationStateChange", false)
addEvent("onStoryWorldRuntimeStopping", false)

local function finite(value)
    return type(value) == "number" and value == value and value > -math.huge and value < math.huge
end

local function validDimension(value)
    return type(value) == "number" and value >= 0 and value <= 65535 and value % 1 == 0
end

local function boundedOption(value, defaultValue, minimum, maximum)
    value = tonumber(value)
    if not finite(value) then value = defaultValue end
    return math.max(minimum, math.min(maximum, value))
end

local function callerRoot()
    return sourceResourceRoot or resourceRoot
end

local function clearTimer(record)
    if isTimer(record.timeout) then killTimer(record.timeout) end
    record.timeout = nil
end

local function reserveStoryWorldElements(record, elements)
    record.reservedWorldElements = elements
    for _, element in ipairs(elements) do storyWorldElementReservations[element] = record end
end

local function releaseStoryWorldElements(record)
    for _, element in ipairs(record.reservedWorldElements or {}) do
        if storyWorldElementReservations[element] == record then storyWorldElementReservations[element] = nil end
    end
end

local function clearOccupancyTimers(record)
    clearTimer(record)
    if isTimer(record.stepTimer) then killTimer(record.stepTimer) end
    if isTimer(record.readyTimer) then killTimer(record.readyTimer) end
    record.stepTimer, record.readyTimer = nil, nil
end

local function placementSnapshot(record, extra)
    local data = {
        id = record.id,
        state = record.state,
        vehicle = record.vehicle,
        model = record.model,
        scriptZ = record.scriptZ,
        centerZ = record.centerZ,
        baseOffset = record.baseOffset,
    }
    for key, value in pairs(type(extra) == "table" and extra or {}) do data[key] = value end
    return data
end

local function emitPlacement(record, state, extra)
    record.state = state
    triggerEvent("onStoryScmVehicleStateChange", record.handle, state, placementSnapshot(record, extra))
end

local function removePlacement(record, destroyHandle, destroyVehicle)
    if not record then return end
    clearTimer(record)
    if isElement(record.syncer) then
        triggerClientEvent(record.syncer, "storyWorldRuntime:cancelVehiclePlacement", resourceRoot, record.id)
    end
    placements[record.handle] = nil
    placementsById[record.id] = nil
    releaseStoryWorldElements(record)
    if destroyVehicle and isElement(record.vehicle) then destroyElement(record.vehicle) end
    if destroyHandle and isElement(record.handle) then destroyElement(record.handle) end
end

local function failPlacement(record, reason)
    if not record or record.state == "failed" or record.state == "ready" then return end
    clearTimer(record)
    if isElement(record.syncer) then
        triggerClientEvent(record.syncer, "storyWorldRuntime:cancelVehiclePlacement", resourceRoot, record.id)
    end
    releaseStoryWorldElements(record)
    emitPlacement(record, "failed", {reason = reason})
end

function createStoryScmVehicle(model, x, y, scriptZ, heading, dimension, syncer, options)
    model, x, y, scriptZ, heading, dimension = tonumber(model), tonumber(x), tonumber(y), tonumber(scriptZ),
                                                     tonumber(heading), tonumber(dimension)
    if not model or model % 1 ~= 0 or model < 400 or model > 611 or not finite(x) or not finite(y) or
        not finite(scriptZ) or not finite(heading) or not validDimension(dimension) then
        return false, false, "invalid SCM vehicle placement"
    end
    if not isElement(syncer) or getElementType(syncer) ~= "player" then
        return false, false, "invalid placement syncer"
    end

    options = type(options) == "table" and options or {}
    local vehicle = createVehicle(model, x, y, scriptZ, 0, 0, heading)
    if not vehicle then return false, false, "vehicle creation failed" end
    setElementDimension(vehicle, dimension)
    setElementFrozen(vehicle, true)
    setElementCollisionsEnabled(vehicle, false)
    if not setElementSyncer(vehicle, syncer, true, true) then
        destroyElement(vehicle)
        return false, false, "vehicle syncer assignment failed"
    end

    nextPlacementId = nextPlacementId + 1
    local handle = createElement("story-scm-vehicle", ("story-scm-vehicle-%d"):format(nextPlacementId))
    if not handle then
        destroyElement(vehicle)
        return false, false, "placement handle creation failed"
    end
    setElementParent(handle, callerRoot())
    local record = {
        id = nextPlacementId,
        handle = handle,
        caller = callerRoot(),
        vehicle = vehicle,
        model = model,
        x = x,
        y = y,
        scriptZ = scriptZ,
        heading = heading,
        dimension = dimension,
        syncer = syncer,
        state = "measuring",
        tolerance = math.max(0.005, math.min(0.1, tonumber(options.tolerance) or 0.03)),
    }
    placements[handle] = record
    placementsById[record.id] = record
    reserveStoryWorldElements(record, {vehicle})
    record.timeout = setTimer(function()
        if placements[handle] == record and record.state ~= "ready" then
            failPlacement(record, "SCM vehicle placement timeout")
        end
    end, math.max(2000, math.min(30000, tonumber(options.timeout) or 10000)), 1)

    local syncerOffsets = modelBaseOffsets[syncer] or {}
    modelBaseOffsets[syncer] = syncerOffsets
    local cachedOffset = syncerOffsets[model]
    if cachedOffset then
        record.baseOffset = cachedOffset
        record.centerZ = scriptZ + cachedOffset
        setElementPosition(vehicle, x, y, record.centerZ)
        record.state = "verifying"
        triggerClientEvent(syncer, "storyWorldRuntime:verifyVehicle", resourceRoot, record.id, vehicle,
                           record.centerZ, record.tolerance)
    else
        triggerClientEvent(syncer, "storyWorldRuntime:measureVehicle", resourceRoot, record.id, vehicle, model)
    end
    return handle, vehicle
end

function releaseStoryScmVehicle(handle)
    local record = placements[handle]
    if not record or record.caller ~= callerRoot() then return false end
    removePlacement(record, true)
    return true
end

function getStoryScmVehicleState(handle)
    local record = placements[handle]
    if not record or record.caller ~= callerRoot() then return false end
    return record.state, placementSnapshot(record)
end

function createStoryScmPed(model, x, y, scriptZ, heading, dimension)
    model, x, y, scriptZ, heading, dimension = tonumber(model), tonumber(x), tonumber(y), tonumber(scriptZ),
                                                     tonumber(heading), tonumber(dimension)
    if not model or model % 1 ~= 0 or model < 0 or model > 312 or not finite(x) or not finite(y) or
        not finite(scriptZ) or not finite(heading) or not validDimension(dimension) then
        return false, "invalid SCM ped placement"
    end
    -- COMMAND_CREATE_CHAR adds the target executable's 1.0 constant to the
    -- script-space Z. Ordinary MTA createPed deliberately does not.
    local ped = createPed(model, x, y, scriptZ + 1.0, heading)
    if not ped then return false, "ped creation failed" end
    setElementDimension(ped, dimension)
    return ped
end

local function playerModelLeaseSnapshot(record)
    return {id = record.id, state = record.state, model = record.model, players = #record.players}
end

local function restorePlayerModelLease(record)
    if not record or record.state == "released" then return end
    record.state = "released"
    for _, entry in ipairs(record.players) do
        if playerModelLeaseByPlayer[entry.player] == record then playerModelLeaseByPlayer[entry.player] = nil end
        if isElement(entry.player) and getElementModel(entry.player) ~= entry.model then
            setElementModel(entry.player, entry.model)
        end
    end
    playerModelLeases[record.handle] = nil
    triggerEvent("onStoryPlayerModelLeaseStateChange", record.handle, "released", playerModelLeaseSnapshot(record))
end

function createStoryPlayerModelLease(players, model)
    model = tonumber(model)
    if type(players) ~= "table" or not model or model % 1 ~= 0 or model < 0 or model > 312 then
        return false, "invalid player model lease"
    end
    local immutable, seen = {}, {}
    for index, player in ipairs(players) do
        if not isElement(player) or getElementType(player) ~= "player" then
            return false, ("invalid player model lease member %d"):format(index)
        end
        if seen[player] or playerModelLeaseByPlayer[player] then
            return false, ("player model already leased at member %d"):format(index)
        end
        seen[player] = true
        immutable[#immutable + 1] = {player = player, model = getElementModel(player)}
    end
    if #immutable == 0 then return false, "player model lease requires a player" end

    nextPlayerModelLeaseId = nextPlayerModelLeaseId + 1
    local handle = createElement("story-player-model-lease",
                                 ("story-player-model-lease-%d"):format(nextPlayerModelLeaseId))
    if not handle then return false, "player model lease handle creation failed" end
    setElementParent(handle, callerRoot())
    local record = {id = nextPlayerModelLeaseId, handle = handle, caller = callerRoot(), players = immutable,
                    model = model, state = "applying"}
    playerModelLeases[handle] = record
    for _, entry in ipairs(immutable) do playerModelLeaseByPlayer[entry.player] = record end

    for _, entry in ipairs(immutable) do
        if getElementModel(entry.player) ~= model and not setElementModel(entry.player, model) then
            restorePlayerModelLease(record)
            destroyElement(handle)
            return false, "player model transition refused"
        end
    end
    record.state = "active"
    triggerEvent("onStoryPlayerModelLeaseStateChange", handle, "active", playerModelLeaseSnapshot(record))
    return handle
end

function releaseStoryPlayerModelLease(handle)
    local record = playerModelLeases[handle]
    if not record or record.caller ~= callerRoot() then return false end
    restorePlayerModelLease(record)
    if isElement(handle) then destroyElement(handle) end
    return true
end

function getStoryPlayerModelLeaseState(handle)
    local record = playerModelLeases[handle]
    if not record or record.caller ~= callerRoot() then return false end
    return record.state, playerModelLeaseSnapshot(record)
end

local function fileCutsceneCount(record, field)
    local count = 0
    for _, player in ipairs(record.players) do
        if record[field][player] then count = count + 1 end
    end
    return count
end

local function fileCutsceneSnapshot(record, extra)
    local data = {
        id = record.id,
        state = record.state,
        name = record.name,
        leader = record.leader,
        participants = #record.players,
        loaded = fileCutsceneCount(record, "loaded"),
        started = fileCutsceneCount(record, "started"),
        finished = fileCutsceneCount(record, "finished"),
        released = fileCutsceneCount(record, "released"),
        skipped = record.skipped == true,
        skipSource = record.skipSource,
    }
    for key, value in pairs(type(extra) == "table" and extra or {}) do data[key] = value end
    return data
end

local function emitFileCutscene(record, state, extra)
    record.state = state
    triggerEvent("onStoryFileCutsceneStateChange", record.handle, state, fileCutsceneSnapshot(record, extra))
end

local function clearFileCutsceneTimer(record)
    if isTimer(record.timer) then killTimer(record.timer) end
    record.timer = nil
end

local function fileCutscenePlayerExpected(record, player)
    for _, participant in ipairs(record.players) do
        if participant == player then return true end
    end
    return false
end

local function allFileCutscenePlayers(record, field)
    for _, player in ipairs(record.players) do
        if not record.unavailable[player] and not record[field][player] then return false end
    end
    return true
end

local function deactivateFileCutscene(record)
    for _, player in ipairs(record.players) do
        if fileCutsceneByPlayer[player] == record then fileCutsceneByPlayer[player] = nil end
    end
end

local function removeFileCutscene(record, destroyHandle, notifyClients)
    if not record then return end
    clearFileCutsceneTimer(record)
    if notifyClients then
        for _, player in ipairs(record.players) do
            if isElement(player) and not record.unavailable[player] then
                triggerClientEvent(player, "storyWorldRuntime:abortFileCutscene", resourceRoot, record.id)
            end
        end
    end
    deactivateFileCutscene(record)
    fileCutscenes[record.handle] = nil
    fileCutscenesById[record.id] = nil
    if destroyHandle and isElement(record.handle) then destroyElement(record.handle) end
end

local function finishFileCutsceneRelease(record)
    if fileCutscenes[record.handle] ~= record or record.phase ~= "releasing" then return end
    clearFileCutsceneTimer(record)
    deactivateFileCutscene(record)
    record.phase = "terminal"
    if record.releaseTerminal == "failed" then
        emitFileCutscene(record, "failed", {reason = record.releaseReason})
    else
        emitFileCutscene(record, "released", {reason = record.releaseReason})
    end
end

local function beginFileCutsceneRelease(record, terminal, reason)
    if not record or record.phase == "terminal" or record.phase == "releasing" then return end
    clearFileCutsceneTimer(record)
    record.phase = "releasing"
    record.releaseTerminal = terminal == "failed" and "failed" or "released"
    record.releaseReason = tostring(reason or (terminal == "failed" and "file cutscene failed" or
                                                   "file cutscene completed"))
    record.released = {}
    for _, player in ipairs(record.players) do
        if isElement(player) and not record.unavailable[player] then
            triggerClientEvent(player, "storyWorldRuntime:releaseFileCutscene", resourceRoot, record.id)
        end
    end
    if allFileCutscenePlayers(record, "released") then return finishFileCutsceneRelease(record) end
    record.timer = setTimer(function()
        if fileCutscenes[record.handle] ~= record or record.phase ~= "releasing" then return end
        -- ReleaseFileCutscene(false) and resource teardown both own an
        -- unconditional fade-in. Repeat the safety cleanup before publishing
        -- a terminal failure so a lost acknowledgement cannot strand a peer.
        for _, player in ipairs(record.players) do
            if isElement(player) and not record.unavailable[player] and not record.released[player] then
                triggerClientEvent(player, "storyWorldRuntime:abortFileCutscene", resourceRoot, record.id)
            end
        end
        record.releaseTerminal = "failed"
        record.releaseReason = record.releaseReason .. "; client release barrier timed out"
        finishFileCutsceneRelease(record)
    end, record.releaseTimeout, 1)
end

local function failFileCutscene(record, reason)
    if not record or record.phase == "terminal" then return end
    beginFileCutsceneRelease(record, "failed", reason)
end

local function armFileCutsceneTimer(record, timeout, reason)
    clearFileCutsceneTimer(record)
    record.timer = setTimer(function()
        if fileCutscenes[record.handle] == record and record.phase ~= "terminal" then
            failFileCutscene(record, reason)
        end
    end, timeout, 1)
end

function createStoryFileCutscene(players, leader, name, visibleArea, options)
    if type(players) ~= "table" or type(name) ~= "string" or #name < 1 or #name > 7 or name:find("[^%w_]") then
        return false, "invalid file cutscene request"
    end
    if visibleArea ~= nil then
        visibleArea = tonumber(visibleArea)
        if not visibleArea or visibleArea % 1 ~= 0 or visibleArea < 0 or visibleArea > 255 then
            return false, "invalid file cutscene visible area"
        end
    end
    options = type(options) == "table" and options or {}
    local immutable, seen, hasLeader = {}, {}, false
    for index, player in ipairs(players) do
        if not isElement(player) or getElementType(player) ~= "player" then
            return false, ("invalid file cutscene participant %d"):format(index)
        end
        if seen[player] then return false, ("duplicate file cutscene participant %d"):format(index) end
        if fileCutsceneByPlayer[player] then
            return false, ("file cutscene participant %d is already active"):format(index)
        end
        seen[player] = true
        immutable[#immutable + 1] = player
        if player == leader then hasLeader = true end
    end
    if #immutable == 0 or not hasLeader then return false, "file cutscene requires its leader in the party" end

    nextFileCutsceneId = nextFileCutsceneId + 1
    local handle = createElement("story-file-cutscene", ("story-file-cutscene-%d"):format(nextFileCutsceneId))
    if not handle then return false, "file cutscene handle creation failed" end
    setElementParent(handle, callerRoot())
    local record = {
        id = nextFileCutsceneId,
        handle = handle,
        caller = callerRoot(),
        players = immutable,
        leader = leader,
        name = name,
        visibleArea = visibleArea,
        state = "loading",
        phase = "loading",
        loaded = {},
        started = {},
        finished = {},
        released = {},
        unavailable = {},
        allowLeaderSkip = options.allowLeaderSkip ~= false,
        loadTimeout = boundedOption(options.loadTimeout, 15000, 2000, 60000),
        playTimeout = boundedOption(options.playTimeout, 180000, 5000, 600000),
        releaseTimeout = boundedOption(options.releaseTimeout, 10000, 2000, 30000),
        fadeIn = boundedOption(options.fadeIn, 1, 0, 3),
    }
    fileCutscenes[handle] = record
    fileCutscenesById[record.id] = record
    for _, player in ipairs(immutable) do fileCutsceneByPlayer[player] = record end
    armFileCutsceneTimer(record, record.loadTimeout, "file cutscene load barrier timed out")
    triggerClientEvent(immutable, "storyWorldRuntime:prepareFileCutscene", resourceRoot, record.id, name,
                       visibleArea, leader, record.allowLeaderSkip, record.fadeIn)
    return handle
end

function skipStoryFileCutscene(handle)
    local record = fileCutscenes[handle]
    if not record or record.caller ~= callerRoot() or
        (record.phase ~= "starting" and record.phase ~= "playing") then
        return false
    end
    if record.skipped then return true end
    record.skipped, record.skipSource = true, "server"
    triggerClientEvent(record.players, "storyWorldRuntime:skipFileCutscene", resourceRoot, record.id)
    return true
end

function releaseStoryFileCutscene(handle)
    local record = fileCutscenes[handle]
    if not record or record.caller ~= callerRoot() then return false end
    if record.phase == "terminal" then
        removeFileCutscene(record, true, false)
    else
        beginFileCutsceneRelease(record, "released", "released by caller")
    end
    return true
end

function getStoryFileCutsceneState(handle)
    local record = fileCutscenes[handle]
    if not record or record.caller ~= callerRoot() then return false end
    return record.state, fileCutsceneSnapshot(record, record.state == "failed" and {reason = record.releaseReason} or nil)
end

addEvent("storyWorldRuntime:fileCutsceneLoaded", true)
addEventHandler("storyWorldRuntime:fileCutsceneLoaded", resourceRoot, function(id, ok, reason)
    local record = fileCutscenesById[tonumber(id)]
    if source ~= resourceRoot or not record or record.phase ~= "loading" or
        not fileCutscenePlayerExpected(record, client) or record.loaded[client] then
        return
    end
    if ok ~= true then return failFileCutscene(record, tostring(reason or "client refused file cutscene load")) end
    record.loaded[client] = true
    if not allFileCutscenePlayers(record, "loaded") then return end
    clearFileCutsceneTimer(record)
    emitFileCutscene(record, "loaded")
    if fileCutscenes[record.handle] ~= record or record.phase ~= "loading" then return end
    record.phase = "starting"
    armFileCutsceneTimer(record, math.min(record.loadTimeout, 15000), "file cutscene start barrier timed out")
    triggerClientEvent(record.players, "storyWorldRuntime:startFileCutscene", resourceRoot, record.id)
end)

addEvent("storyWorldRuntime:fileCutsceneStarted", true)
addEventHandler("storyWorldRuntime:fileCutsceneStarted", resourceRoot, function(id, ok, reason)
    local record = fileCutscenesById[tonumber(id)]
    if source ~= resourceRoot or not record or record.phase ~= "starting" or
        not fileCutscenePlayerExpected(record, client) or record.started[client] then
        return
    end
    if ok ~= true then return failFileCutscene(record, tostring(reason or "client refused file cutscene start")) end
    record.started[client] = true
    if not allFileCutscenePlayers(record, "started") then return end
    clearFileCutsceneTimer(record)
    record.phase = "playing"
    emitFileCutscene(record, "started")
    if fileCutscenes[record.handle] ~= record or record.phase ~= "playing" then return end
    armFileCutsceneTimer(record, record.playTimeout, "file cutscene playback barrier timed out")
end)

addEvent("storyWorldRuntime:fileCutsceneSkipRequested", true)
addEventHandler("storyWorldRuntime:fileCutsceneSkipRequested", resourceRoot, function(id)
    local record = fileCutscenesById[tonumber(id)]
    if source ~= resourceRoot or not record or not record.allowLeaderSkip or client ~= record.leader or
        (record.phase ~= "starting" and record.phase ~= "playing") or record.skipped then
        return
    end
    record.skipped, record.skipSource = true, record.leader
    triggerClientEvent(record.players, "storyWorldRuntime:skipFileCutscene", resourceRoot, record.id)
end)

addEvent("storyWorldRuntime:fileCutsceneFinished", true)
addEventHandler("storyWorldRuntime:fileCutsceneFinished", resourceRoot, function(id, ok, skipped, elapsed, reason)
    local record = fileCutscenesById[tonumber(id)]
    if source ~= resourceRoot or not record or
        (record.phase ~= "starting" and record.phase ~= "playing") or
        not fileCutscenePlayerExpected(record, client) or record.finished[client] then
        return
    end
    if ok ~= true then return failFileCutscene(record, tostring(reason or "client file cutscene playback failed")) end
    record.finished[client] = {skipped = skipped == true, elapsed = tonumber(elapsed)}
    if skipped == true then record.skipped = true end
    if not allFileCutscenePlayers(record, "finished") then return end
    clearFileCutsceneTimer(record)
    record.phase = "finished"
    emitFileCutscene(record, "finished")
    if fileCutscenes[record.handle] ~= record or record.phase ~= "finished" then return end
    beginFileCutsceneRelease(record, "released", "file cutscene completed")
end)

addEvent("storyWorldRuntime:fileCutsceneReleased", true)
addEventHandler("storyWorldRuntime:fileCutsceneReleased", resourceRoot, function(id, ok, reason)
    local record = fileCutscenesById[tonumber(id)]
    if source ~= resourceRoot or not record or record.phase ~= "releasing" or
        not fileCutscenePlayerExpected(record, client) or record.released[client] then
        return
    end
    if ok ~= true then
        record.releaseTerminal = "failed"
        record.releaseReason = tostring(reason or "client file cutscene release failed")
        triggerClientEvent(client, "storyWorldRuntime:abortFileCutscene", resourceRoot, record.id)
        return
    end
    record.released[client] = true
    if allFileCutscenePlayers(record, "released") then finishFileCutsceneRelease(record) end
end)

local function relocationSnapshot(record, extra)
    local expected = {}
    for index, entry in ipairs(record.entries) do
        expected[index] = {vehicle = entry.vehicle, x = entry.x, y = entry.y, centerZ = entry.centerZ,
                           scriptZ = entry.scriptZ, rx = entry.rx, ry = entry.ry, heading = entry.heading,
                           interior = entry.interior, dimension = entry.dimension,
                           requireGround = entry.requireGround, seats = #entry.occupants}
    end
    local data = {id = record.id, generation = record.generation, state = record.state, phase = record.phase,
                  participants = #record.players, vehicles = #record.entries, expected = expected,
                  observed = record.observed, createdAt = record.createdAt, stateChangedAt = record.stateChangedAt,
                  movedAt = record.movedAt, verifiedAt = record.verifiedAt}
    for key, value in pairs(type(extra) == "table" and extra or {}) do data[key] = value end
    return data
end

local function emitRelocation(record, state, extra)
    record.state, record.phase, record.stateChangedAt = state, state, getTickCount()
    triggerEvent("onStoryVehicleRelocationStateChange", record.handle, state, relocationSnapshot(record, extra))
end

local function clearRelocationTimers(record)
    for _, name in ipairs({"timeout", "moveTimer"}) do
        if isTimer(record[name]) then killTimer(record[name]) end
        record[name] = nil
    end
end

local function setRelocationInterior(element, interior)
    -- Server-side setElementInterior returns false when the element is already
    -- in the requested interior. Relocation setters are transactional and
    -- therefore need idempotent success semantics for both move and rollback.
    return getElementInterior(element) == interior or setElementInterior(element, interior)
end

local function setRelocationDimension(element, dimension)
    -- Avoid touching an occupied ped for an idempotent world change: even a
    -- same-value dimension setter can invalidate its local vehicle task.
    return getElementDimension(element) == dimension or setElementDimension(element, dimension)
end

local function restoreRelocationPhysics(record, rollback, holdFrozen)
    if record.physicsRestored then return true end
    if not record.mutationStarted then
        record.physicsRestored = true
        return true
    end
    local ok, reason = true, nil
    for _, entry in ipairs(record.entries) do
        if isElement(entry.vehicle) then
            ok = setElementFrozen(entry.vehicle, true) and ok
            ok = setElementCollisionsEnabled(entry.vehicle, false) and ok
            ok = setElementVelocity(entry.vehicle, 0, 0, 0) and ok
            ok = setElementAngularVelocity(entry.vehicle, 0, 0, 0) and ok
            for _, occupant in ipairs(entry.occupants) do
                if rollback and isElement(occupant.ped) and isPedInVehicle(occupant.ped) then
                    removePedFromVehicle(occupant.ped)
                end
            end
            if rollback then
                ok = setRelocationInterior(entry.vehicle, entry.originalInterior) and ok
                ok = setRelocationDimension(entry.vehicle, entry.originalDimension) and ok
                ok = setElementPosition(entry.vehicle, entry.originalX, entry.originalY, entry.originalZ) and ok
                ok = setElementRotation(entry.vehicle, entry.originalRx, entry.originalRy, entry.originalRz) and ok
                -- Automatic syncer selection can migrate while a remote target
                -- is being streamed. Rollback owns the complete pre-mutation
                -- state, including authority; otherwise the transform may be
                -- restored while a different client immediately rewrites it.
                if not record.unavailable[entry.syncer] and getElementSyncer(entry.vehicle) ~= entry.syncer then
                    if not setElementSyncer(entry.vehicle, entry.syncer or false, true, true) or
                        getElementSyncer(entry.vehicle) ~= entry.syncer then
                        ok, reason = false, ("vehicle %d syncer rollback refused"):format(entry.index)
                    end
                end
            end
            for occupantIndex, occupant in ipairs(entry.occupants) do
                if rollback and isElement(occupant.ped) then
                    ok = setRelocationInterior(occupant.ped, entry.originalInterior) and ok
                    ok = setRelocationDimension(occupant.ped, entry.originalDimension) and ok
                end
                if isElement(occupant.ped) and
                    (getPedOccupiedVehicle(occupant.ped) ~= entry.vehicle or
                        getPedOccupiedVehicleSeat(occupant.ped) ~= occupant.seat) then
                    if rollback then
                        local radians = math.rad(entry.originalRz + 90)
                        ok = setElementPosition(occupant.ped,
                                               entry.originalX + math.cos(radians) * (2 + occupantIndex * 0.2),
                                               entry.originalY + math.sin(radians) * (2 + occupantIndex * 0.2),
                                               entry.originalZ + 0.5) and ok
                    end
                    if not warpPedIntoVehicle(occupant.ped, entry.vehicle, occupant.seat) then
                        ok, reason = false, ("vehicle %d seat %d restore refused"):format(entry.index,
                                                                                         occupant.seat)
                    end
                end
            end
            ok = setElementVelocity(entry.vehicle, 0, 0, 0) and ok
            ok = setElementAngularVelocity(entry.vehicle, 0, 0, 0) and ok
            ok = setElementCollisionsEnabled(entry.vehicle, entry.collisions) and ok
            ok = setElementFrozen(entry.vehicle, holdFrozen and true or entry.frozen) and ok
        end
    end
    if ok and not holdFrozen then record.physicsRestored = true end
    return ok, reason or "vehicle relocation physics restore refused"
end

local function finalizeRelocationRollbackPhysics(record)
    if record.physicsRestored then return true end
    local ok = true
    for _, entry in ipairs(record.entries) do
        if isElement(entry.vehicle) then
            ok = setElementVelocity(entry.vehicle, 0, 0, 0) and ok
            ok = setElementAngularVelocity(entry.vehicle, 0, 0, 0) and ok
            ok = setElementCollisionsEnabled(entry.vehicle, entry.collisions) and ok
            ok = setElementFrozen(entry.vehicle, entry.frozen) and ok
        end
    end
    if ok then record.physicsRestored = true end
    return ok, "vehicle relocation final rollback flags restore refused"
end

local function cancelRelocationClients(record)
    for verifier in pairs(record.verifiers) do
        if isElement(verifier) then
            triggerClientEvent(verifier, "storyWorldRuntime:cancelVehicleRelocation", resourceRoot, record.id)
        end
    end
end

local function releaseRelocationReservations(record)
    for _, element in ipairs(record.reservedElements or {}) do
        if vehicleRelocationReservations[element] == record then vehicleRelocationReservations[element] = nil end
    end
    releaseStoryWorldElements(record)
end

local function removeRelocation(record, destroyHandle)
    if not record then return end
    clearRelocationTimers(record)
    restoreRelocationPhysics(record, record.state ~= "ready")
    cancelRelocationClients(record)
    releaseRelocationReservations(record)
    vehicleRelocations[record.handle] = nil
    vehicleRelocationsById[record.id] = nil
    if destroyHandle and isElement(record.handle) then destroyElement(record.handle) end
end

local function relocationRollbackExpected(record)
    local expected = {}
    for _, entry in ipairs(record.entries) do
        expected[entry.index] = {vehicle = entry.vehicle, x = entry.originalX, y = entry.originalY,
                                 z = entry.originalZ, rx = entry.originalRx, ry = entry.originalRy,
                                 rz = entry.originalRz, interior = entry.originalInterior,
                                 dimension = entry.originalDimension, frozenDuringProof = true,
                                 finalFrozen = entry.frozen, collisions = entry.collisions,
                                 syncer = entry.syncer, seats = entry.seatPeds}
    end
    return expected
end

local function inspectRelocationRollbackServer(record)
    local observed, ok, reason = {}, true, nil
    local function angleError(actual, expected)
        return math.abs((actual - expected + 180) % 360 - 180)
    end
    for _, entry in ipairs(record.entries) do
        if not isElement(entry.vehicle) then
            observed[entry.index] = {vehicle = entry.vehicle, exists = false, timestamp = getTickCount()}
            ok, reason = false, ("rollback vehicle %d disappeared before server commit"):format(entry.index)
            break
        end
        local x, y, z = getElementPosition(entry.vehicle)
        local rx, ry, rz = getElementRotation(entry.vehicle)
        local vx, vy, vz = getElementVelocity(entry.vehicle)
        local avx, avy, avz = getElementAngularVelocity(entry.vehicle)
        local positionError = math.sqrt((x - entry.originalX) ^ 2 + (y - entry.originalY) ^ 2 +
                                            (z - entry.originalZ) ^ 2)
        local rotationError = math.max(angleError(rx, entry.originalRx), angleError(ry, entry.originalRy),
                                       angleError(rz, entry.originalRz))
        local seatsReady = true
        for seat, actualOccupant in pairs(getVehicleOccupants(entry.vehicle) or {}) do
            if entry.seatPeds[tonumber(seat)] ~= actualOccupant then seatsReady = false break end
        end
        if seatsReady then
            for seat, ped in pairs(entry.seatPeds) do
                if not isElement(ped) or getPedOccupiedVehicle(ped) ~= entry.vehicle or
                    getPedOccupiedVehicleSeat(ped) ~= tonumber(seat) or
                    getElementInterior(ped) ~= entry.originalInterior or
                    getElementDimension(ped) ~= entry.originalDimension then
                    seatsReady = false
                    break
                end
            end
        end
        local worldReady = getElementInterior(entry.vehicle) == entry.originalInterior and
                               getElementDimension(entry.vehicle) == entry.originalDimension
        local physicsReady = isElementFrozen(entry.vehicle) == true and
                                 getElementCollisionsEnabled(entry.vehicle) == entry.collisions
        local syncerReady = record.unavailable[entry.syncer] or getElementSyncer(entry.vehicle) == entry.syncer
        local zeroMotion = math.max(math.abs(vx), math.abs(vy), math.abs(vz), math.abs(avx), math.abs(avy),
                                    math.abs(avz)) <= 0.01
        observed[entry.index] = {vehicle = entry.vehicle, exists = true, x = x, y = y, z = z,
                                 rx = rx, ry = ry, rz = rz, positionError = positionError,
                                 rotationError = rotationError, interior = getElementInterior(entry.vehicle),
                                 dimension = getElementDimension(entry.vehicle),
                                 frozen = isElementFrozen(entry.vehicle),
                                 collisions = getElementCollisionsEnabled(entry.vehicle), seats = seatsReady,
                                 syncer = getElementSyncer(entry.vehicle), zeroMotion = zeroMotion,
                                 vx = vx, vy = vy, vz = vz, avx = avx, avy = avy, avz = avz,
                                 timestamp = getTickCount()}
        if positionError > record.positionTolerance or rotationError > 0.5 or not worldReady or
            not physicsReady or not seatsReady or not syncerReady or not zeroMotion then
            ok, reason = false, ("rollback vehicle %d server commit diverged pos=%.4f rot=%.4f world=%s " ..
                                    "physics=%s seats=%s syncer=%s motion=%s"):format(
                                     entry.index, positionError, rotationError, tostring(worldReady),
                                     tostring(physicsReady), tostring(seatsReady), tostring(syncerReady),
                                     tostring(zeroMotion))
            break
        end
    end
    return ok, observed, reason
end

local allRelocationVerifiers

local function failRelocation(record, reason)
    if not record or record.state == "failed" or record.state == "ready" or record.failing then return end
    clearRelocationTimers(record)
    record.failing, record.phase = true, "restoring"
    record.failureReason, record.restoreStartedAt = tostring(reason), getTickCount()
    record.rollbackObserved = {}
    local function finish(restored, restoreReason)
        clearRelocationTimers(record)
        local finalRestored, finalRestoreReason = finalizeRelocationRollbackPhysics(record)
        if not finalRestored then
            restored = false
            restoreReason = tostring(restoreReason or "rollback proof failed") .. "; " ..
                                tostring(finalRestoreReason)
        end
        cancelRelocationClients(record)
        releaseRelocationReservations(record)
        if not restored then record.failureReason = record.failureReason .. "; " .. tostring(restoreReason) end
        record.failedAt = getTickCount()
        if not record.handleDestroyed then
            emitRelocation(record, "failed", {reason = record.failureReason, cleanupRestored = restored,
                                               rollbackExpected = relocationRollbackExpected(record),
                                               rollbackObserved = record.rollbackObserved})
        end
        if record.removeAfterFailure then removeRelocation(record, not record.handleDestroyed) end
    end
    local attempt
    attempt = function()
        if vehicleRelocations[record.handle] ~= record then return end
        local restored, restoreReason = restoreRelocationPhysics(record, true, true)
        if not restored and getTickCount() - record.restoreStartedAt < 3000 then
            record.moveTimer = setTimer(attempt, 50, 1)
            return
        end
        if not restored then return finish(false, restoreReason) end
        if not record.mutationStarted then return finish(true) end
        record.phase, record.rollbackVerified = "rollback_verifying", {}
        local perVerifier = {}
        for _, entry in ipairs(record.entries) do
            perVerifier[entry.verifier] = perVerifier[entry.verifier] or {}
            perVerifier[entry.verifier][#perVerifier[entry.verifier] + 1] = {
                index = entry.index, vehicle = entry.vehicle, x = entry.originalX, y = entry.originalY,
                centerZ = entry.originalZ, rx = entry.originalRx, ry = entry.originalRy,
                heading = entry.originalRz, interior = entry.originalInterior, dimension = entry.originalDimension,
                frozen = true, collisions = entry.collisions, occupants = entry.occupants,
                seatPeds = entry.seatPeds, verifierIsSyncer = entry.syncer == entry.verifier}
        end
        record.commitRollbackProof = function()
            if vehicleRelocations[record.handle] ~= record or not record.failing or
                record.phase ~= "rollback_verifying" then return end
            local serverReady, serverObserved, serverReason = inspectRelocationRollbackServer(record)
            record.rollbackObserved.server = serverObserved
            finish(serverReady, serverReason)
        end
        record.finishFailure = finish
        for verifier, verifierEntries in pairs(perVerifier) do
            if isElement(verifier) and not record.unavailable[verifier] then
                triggerClientEvent(verifier, "storyWorldRuntime:verifyVehicleRelocationRollback", resourceRoot,
                                   record.id, verifierEntries, record.positionTolerance, record.stableSamples)
            end
        end
        if allRelocationVerifiers(record, "rollbackVerified") then return record.commitRollbackProof() end
        record.moveTimer = setTimer(function()
            if vehicleRelocations[record.handle] == record and record.failing then
                finish(false, "client rollback verification timed out")
            end
        end, 5000, 1)
    end
    attempt()
end

allRelocationVerifiers = function(record, field)
    for verifier in pairs(record.verifiers) do
        if not record.unavailable[verifier] and not record[field][verifier] then return false end
    end
    return true
end

local function beginRelocationMove(record)
    if vehicleRelocations[record.handle] ~= record or record.state ~= "preparing" or record.failing then return end
    for _, entry in ipairs(record.entries) do
        if entry.scriptZ ~= nil then
            local offset = record.prepared[entry.verifier] and record.prepared[entry.verifier][entry.index]
            if not finite(offset) or offset <= 0 or offset > 10 then
                return failRelocation(record, ("vehicle %d base offset missing"):format(entry.index))
            end
            entry.baseOffset, entry.centerZ = offset, entry.scriptZ + offset
        end
    end
    for _, entry in ipairs(record.entries) do
        if getElementSyncer(entry.vehicle) ~= entry.syncer then
            -- Automatic syncer selection can migrate an NPC-owned vehicle
            -- while remote collision is loading. Reassert the transaction's
            -- captured participant before the first mutation so one client
            -- remains authoritative for the whole relocation.
            if not setElementSyncer(entry.vehicle, entry.syncer, true, true) or
                getElementSyncer(entry.vehicle) ~= entry.syncer then
                return failRelocation(record, ("vehicle %d syncer changed during preparation"):format(entry.index))
            end
        end
        entry.originalX, entry.originalY, entry.originalZ = getElementPosition(entry.vehicle)
        entry.originalRx, entry.originalRy, entry.originalRz = getElementRotation(entry.vehicle)
        entry.originalInterior, entry.originalDimension = getElementInterior(entry.vehicle),
                                                    getElementDimension(entry.vehicle)
        entry.frozen, entry.collisions = isElementFrozen(entry.vehicle),
                                           getElementCollisionsEnabled(entry.vehicle)
    end
    for _, entry in ipairs(record.entries) do
        if not isElement(entry.vehicle) then return failRelocation(record, "vehicle destroyed before relocation") end
        for seat, actualOccupant in pairs(getVehicleOccupants(entry.vehicle) or {}) do
            if entry.seatPeds[tonumber(seat)] ~= actualOccupant then
                return failRelocation(record, ("vehicle %d seat map changed during preparation"):format(entry.index))
            end
        end
        for _, occupant in ipairs(entry.occupants) do
            if not isElement(occupant.ped) or getPedOccupiedVehicle(occupant.ped) ~= entry.vehicle or
                getPedOccupiedVehicleSeat(occupant.ped) ~= occupant.seat then
                return failRelocation(record, ("vehicle %d declared seat changed before move"):format(entry.index))
            end
        end
    end
    emitRelocation(record, "moving")
    if vehicleRelocations[record.handle] ~= record or record.state ~= "moving" then return end
    record.mutationStarted = true
    for _, entry in ipairs(record.entries) do
        if not isElement(entry.vehicle) then return failRelocation(record, "vehicle destroyed before relocation") end
        if not setElementFrozen(entry.vehicle, true) then
            return failRelocation(record, ("vehicle %d freeze staging refused"):format(entry.index))
        end
        -- Preserve the caller's collision state. Disabling collisions on an
        -- occupied vehicle can make the local GTA task drop its seat; the
        -- frozen zero-motion staging already prevents physical impulses.
        if not setElementVelocity(entry.vehicle, 0, 0, 0) then
            return failRelocation(record, ("vehicle %d linear velocity staging refused"):format(entry.index))
        end
        if not setElementAngularVelocity(entry.vehicle, 0, 0, 0) then
            return failRelocation(record, ("vehicle %d angular velocity staging refused"):format(entry.index))
        end
        for _, occupant in ipairs(entry.occupants) do
            if not isElement(occupant.ped) or getPedOccupiedVehicle(occupant.ped) ~= entry.vehicle or
                getPedOccupiedVehicleSeat(occupant.ped) ~= occupant.seat then
                return failRelocation(record, ("vehicle %d declared seat changed before move"):format(entry.index))
            end
            if not setRelocationInterior(occupant.ped, entry.interior) or
                not setRelocationDimension(occupant.ped, entry.dimension) then
                return failRelocation(record, ("vehicle %d occupant world change refused"):format(entry.index))
            end
        end
        if not setRelocationInterior(entry.vehicle, entry.interior) then
            return failRelocation(record, ("vehicle %d target interior refused"):format(entry.index))
        end
        if not setRelocationDimension(entry.vehicle, entry.dimension) then
            return failRelocation(record, ("vehicle %d target dimension refused"):format(entry.index))
        end
        if not setElementPosition(entry.vehicle, entry.x, entry.y, entry.centerZ) then
            return failRelocation(record, ("vehicle %d target position refused"):format(entry.index))
        end
        if not setElementRotation(entry.vehicle, entry.rx, entry.ry, entry.heading) then
            return failRelocation(record, ("vehicle %d target rotation refused"):format(entry.index))
        end
        if not setElementVelocity(entry.vehicle, 0, 0, 0) then
            return failRelocation(record, ("vehicle %d target linear velocity refused"):format(entry.index))
        end
        if not setElementAngularVelocity(entry.vehicle, 0, 0, 0) then
            return failRelocation(record, ("vehicle %d target angular velocity refused"):format(entry.index))
        end
        for _, occupant in ipairs(entry.occupants) do
            if getPedOccupiedVehicle(occupant.ped) ~= entry.vehicle or
                getPedOccupiedVehicleSeat(occupant.ped) ~= occupant.seat then
                return failRelocation(record, ("vehicle %d seat %d was lost during target move"):format(
                                          entry.index, occupant.seat))
            end
        end
        -- Ground/contact verification needs the caller's collision and freeze
        -- policy. Keep the vehicle frozen until every verifier has proved that
        -- target collision is resident; unfreezing before that barrier lets a
        -- long-distance vehicle fall through an unloaded road.
        setElementCollisionsEnabled(entry.vehicle, entry.collisions)
    end
    record.movedAt = getTickCount()
    emitRelocation(record, "verifying")
    if vehicleRelocations[record.handle] ~= record or record.state ~= "verifying" or record.failing or
        record.phase ~= "verifying" then return end
    record.moveTimer = setTimer(function()
        if vehicleRelocations[record.handle] ~= record or record.state ~= "verifying" or record.failing then return end
        -- A long-distance bike move can leave the server seat intact while
        -- the owning GTA task drops it locally. Re-seat only after the target
        -- collision preload and frozen move have settled, then ask clients to
        -- prove the resulting complete seat map.
        for _, entry in ipairs(record.entries) do
            for occupantIndex, occupant in ipairs(entry.occupants) do
                if isPedInVehicle(occupant.ped) then removePedFromVehicle(occupant.ped) end
                local radians = math.rad(entry.heading + 90)
                if not setElementPosition(occupant.ped,
                                          entry.x + math.cos(radians) * (2 + occupantIndex * 0.2),
                                          entry.y + math.sin(radians) * (2 + occupantIndex * 0.2),
                                          entry.centerZ + 0.5) or
                    not setElementVelocity(occupant.ped, 0, 0, 0) or
                    not warpPedIntoVehicle(occupant.ped, entry.vehicle, occupant.seat) then
                    return failRelocation(record, ("vehicle %d seat %d settled re-warp failed"):format(
                                              entry.index, occupant.seat))
                end
            end
        end
        record.moveTimer = setTimer(function()
            if vehicleRelocations[record.handle] ~= record or record.state ~= "verifying" or record.failing then
                return
            end
        local perVerifier = {}
        for _, entry in ipairs(record.entries) do
            perVerifier[entry.verifier] = perVerifier[entry.verifier] or {}
            perVerifier[entry.verifier][#perVerifier[entry.verifier] + 1] = {
                index = entry.index, vehicle = entry.vehicle, x = entry.x, y = entry.y, centerZ = entry.centerZ,
                baseOffset = entry.baseOffset, occupants = entry.occupants, requireGround = entry.requireGround,
                seatPeds = entry.seatPeds,
                verifierIsSyncer = entry.syncer == entry.verifier, frozen = true,
                collisions = entry.collisions, rx = entry.rx, ry = entry.ry, heading = entry.heading,
                interior = entry.interior, dimension = entry.dimension,
            }
        end
        for verifier, entries in pairs(perVerifier) do
            triggerClientEvent(verifier, "storyWorldRuntime:verifyVehicleRelocation", resourceRoot, record.id,
                               entries, record.positionTolerance, record.groundTolerance, record.stableSamples)
        end
        end, 150, 1)
    end, 150, 1)
end

function createStoryVehicleRelocation(players, coordinator, entries, options)
    if type(players) ~= "table" or type(entries) ~= "table" or #entries == 0 or not isElement(coordinator) or
        getElementType(coordinator) ~= "player" then
        return false, "invalid vehicle relocation request"
    end
    options = type(options) == "table" and options or {}
    local immutablePlayers, playerSet = {}, {}
    for index, player in ipairs(players) do
        if not isElement(player) or getElementType(player) ~= "player" or playerSet[player] then
            return false, ("invalid vehicle relocation participant %d"):format(index)
        end
        playerSet[player] = true
        immutablePlayers[#immutablePlayers + 1] = player
    end
    if #immutablePlayers == 0 or not playerSet[coordinator] then
        return false, "vehicle relocation coordinator must be a participant"
    end
    local immutableEntries, vehicles, peds, verifiers, reservedElements = {}, {}, {}, {}, {}
    for index, requested in ipairs(entries) do
        local vehicle = type(requested) == "table" and requested.vehicle
        local x, y = tonumber(requested and requested.x), tonumber(requested and requested.y)
        local scriptZ, centerZ = tonumber(requested and requested.scriptZ), tonumber(requested and requested.centerZ)
        if not isElement(vehicle) or getElementType(vehicle) ~= "vehicle" then
            return false, ("vehicle relocation entry %d vehicle is invalid"):format(index)
        end
        if vehicles[vehicle] then return false, ("vehicle relocation entry %d duplicates a vehicle"):format(index) end
        if vehicleRelocationReservations[vehicle] then
            return false, ("vehicle relocation entry %d overlaps an active relocation"):format(index)
        end
        if storyWorldElementReservations[vehicle] then
            return false, ("vehicle relocation entry %d overlaps an active story-world operation"):format(index)
        end
        if not finite(x) or not finite(y) then
            return false, ("vehicle relocation entry %d has invalid XY"):format(index)
        end
        if finite(scriptZ) == finite(centerZ) then
            return false, ("vehicle relocation entry %d must provide exactly one Z mode"):format(index)
        end
        if (requested.heading ~= nil and not finite(tonumber(requested.heading))) or
            (requested.rx ~= nil and not finite(tonumber(requested.rx))) or
            (requested.ry ~= nil and not finite(tonumber(requested.ry))) or
            (requested.interior ~= nil and
                (not validDimension(tonumber(requested.interior)) or tonumber(requested.interior) > 255)) or
            (requested.dimension ~= nil and not validDimension(tonumber(requested.dimension))) then
            return false, ("invalid vehicle relocation transform %d"):format(index)
        end
        local syncer = getElementSyncer(vehicle)
        local verifier = isElement(syncer) and getElementType(syncer) == "player" and syncer or coordinator
        if not playerSet[verifier] then
            return false, ("vehicle relocation syncer for entry %d must be a participant"):format(index)
        end
        local occupants, seats, seatPeds = {}, {}, {}
        for occupantIndex, requestedOccupant in ipairs(type(requested.occupants) == "table" and
                                                            requested.occupants or {}) do
            local ped = type(requestedOccupant) == "table" and requestedOccupant.ped
            local seat = tonumber(type(requestedOccupant) == "table" and requestedOccupant.seat)
            local pedType = isElement(ped) and getElementType(ped)
            if (pedType ~= "ped" and pedType ~= "player") or peds[ped] or vehicleRelocationReservations[ped] or
                storyWorldElementReservations[ped] or
                not seat or seat % 1 ~= 0 or seat < 0 or
                seat > getVehicleMaxPassengers(vehicle) or seats[seat] or getPedOccupiedVehicle(ped) ~= vehicle or
                getPedOccupiedVehicleSeat(ped) ~= seat or (pedType == "player" and not playerSet[ped]) then
                return false, ("invalid vehicle relocation occupant %d:%d"):format(index, occupantIndex)
            end
            peds[ped], seats[seat] = true, true
            seatPeds[seat] = ped
            occupants[#occupants + 1] = {ped = ped, seat = seat}
        end
        for seat, actualOccupant in pairs(getVehicleOccupants(vehicle) or {}) do
            if isElement(actualOccupant) and (not seats[tonumber(seat)] or peds[actualOccupant] ~= true) then
                return false, ("vehicle relocation entry %d omits occupied seat %s"):format(index, tostring(seat))
            end
        end
        local requireGround = requested.requireGround ~= false
        if requireGround and not getElementCollisionsEnabled(vehicle) then
            return false, ("vehicle relocation entry %d requires collisions enabled for ground proof"):format(index)
        end
        vehicles[vehicle] = true
        reservedElements[#reservedElements + 1] = vehicle
        for _, occupant in ipairs(occupants) do reservedElements[#reservedElements + 1] = occupant.ped end
        verifiers[verifier] = true
        local rx, ry, rz = getElementRotation(vehicle)
        local originalX, originalY, originalZ = getElementPosition(vehicle)
        immutableEntries[index] = {index = index, vehicle = vehicle, x = x, y = y, scriptZ = scriptZ,
                                   centerZ = centerZ, heading = finite(tonumber(requested.heading)) and
                                       tonumber(requested.heading) or rz,
                                   rx = finite(tonumber(requested.rx)) and tonumber(requested.rx) or 0,
                                   ry = finite(tonumber(requested.ry)) and tonumber(requested.ry) or 0,
                                   interior = requested.interior ~= nil and tonumber(requested.interior) or
                                       getElementInterior(vehicle),
                                   dimension = requested.dimension ~= nil and tonumber(requested.dimension) or
                                       getElementDimension(vehicle),
                                   occupants = occupants, seatPeds = seatPeds, syncer = syncer, verifier = verifier,
                                   frozen = isElementFrozen(vehicle), collisions = getElementCollisionsEnabled(vehicle),
                                   originalX = originalX, originalY = originalY, originalZ = originalZ,
                                   originalRx = rx, originalRy = ry, originalRz = rz,
                                   originalInterior = getElementInterior(vehicle),
                                   originalDimension = getElementDimension(vehicle),
                                   requireGround = requireGround}
    end
    nextVehicleRelocationId = nextVehicleRelocationId + 1
    local handle = createElement("story-vehicle-relocation",
                                 ("story-vehicle-relocation-%d"):format(nextVehicleRelocationId))
    if not handle then return false, "vehicle relocation handle creation failed" end
    setElementParent(handle, callerRoot())
    local now = getTickCount()
    local record = {id = nextVehicleRelocationId, generation = nextVehicleRelocationId, handle = handle,
                    caller = callerRoot(), players = immutablePlayers, entries = immutableEntries,
                    verifiers = verifiers, prepared = {}, verified = {}, observed = {}, unavailable = {},
                    seatRepairs = {},
                    state = "preparing",
                    phase = "preparing", createdAt = now, stateChangedAt = now, physicsRestored = false,
                    reservedElements = reservedElements,
                    positionTolerance = boundedOption(options.positionTolerance, 0.08, 0.01, 0.5),
                    groundTolerance = boundedOption(options.groundTolerance, 0.2, 0.02, 1),
                    stableSamples = math.floor(boundedOption(options.stableSamples, 3, 3, 10))}
    vehicleRelocations[handle], vehicleRelocationsById[record.id] = record, record
    for _, element in ipairs(reservedElements) do vehicleRelocationReservations[element] = record end
    reserveStoryWorldElements(record, reservedElements)
    record.timeout = setTimer(function()
        if vehicleRelocations[handle] == record and record.state ~= "ready" then
            failRelocation(record, ("vehicle relocation timeout during %s"):format(record.phase))
        end
    end, boundedOption(options.timeout, 20000, 3000, 60000), 1)
    local perVerifier = {}
    for _, entry in ipairs(immutableEntries) do
        perVerifier[entry.verifier] = perVerifier[entry.verifier] or {}
        perVerifier[entry.verifier][#perVerifier[entry.verifier] + 1] = {
            index = entry.index, vehicle = entry.vehicle, occupants = entry.occupants, scriptZ = entry.scriptZ,
            centerZ = entry.centerZ, x = entry.x, y = entry.y, requireGround = entry.requireGround,
            seatPeds = entry.seatPeds, verifierIsSyncer = entry.syncer == entry.verifier}
    end
    for verifier, verifierEntries in pairs(perVerifier) do
        triggerClientEvent(verifier, "storyWorldRuntime:prepareVehicleRelocation", resourceRoot, record.id,
                           verifierEntries)
    end
    return handle
end

function releaseStoryVehicleRelocation(handle)
    local record = vehicleRelocations[handle]
    if not record or record.caller ~= callerRoot() then return false end
    if record.state ~= "ready" and record.state ~= "failed" then
        failRelocation(record, "vehicle relocation released by caller")
    else
        removeRelocation(record, true)
    end
    return true
end

function getStoryVehicleRelocationState(handle)
    local record = vehicleRelocations[handle]
    if not record or record.caller ~= callerRoot() then return false end
    return record.state, relocationSnapshot(record, record.state == "failed" and {reason = record.failureReason} or nil)
end

addEvent("storyWorldRuntime:vehicleRelocationPrepared", true)
addEventHandler("storyWorldRuntime:vehicleRelocationPrepared", resourceRoot, function(id, ok, offsets, reason)
    local record = vehicleRelocationsById[tonumber(id)]
    if source ~= resourceRoot or not record or record.state ~= "preparing" or record.failing or
        not record.verifiers[client] or
        record.prepared[client] then return end
    if ok ~= true or type(offsets) ~= "table" then
        record.failureReason = tostring(reason or "vehicle relocation preparation failed")
        return failRelocation(record, record.failureReason)
    end
    record.prepared[client] = offsets
    if allRelocationVerifiers(record, "prepared") then beginRelocationMove(record) end
end)

addEvent("storyWorldRuntime:vehicleRelocationVerified", true)
addEventHandler("storyWorldRuntime:vehicleRelocationVerified", resourceRoot, function(id, ok, observed, reason)
    local record = vehicleRelocationsById[tonumber(id)]
    if source ~= resourceRoot or not record or record.state ~= "verifying" or record.failing or
        not record.verifiers[client] or
        record.verified[client] then return end
    if ok ~= true or type(observed) ~= "table" then
        record.failureReason = tostring(reason or "vehicle relocation verification failed")
        return failRelocation(record, record.failureReason)
    end
    record.verified[client], record.observed[client] = true, observed
    if not allRelocationVerifiers(record, "verified") then return end
    for _, entry in ipairs(record.entries) do
        local x, y, z = getElementPosition(entry.vehicle)
        local rx, ry, rz = getElementRotation(entry.vehicle)
        local function angleError(actual, expected)
            return math.abs((actual - expected + 180) % 360 - 180)
        end
        if getElementSyncer(entry.vehicle) ~= entry.syncer and
            (not setElementSyncer(entry.vehicle, entry.syncer, true, true) or
                getElementSyncer(entry.vehicle) ~= entry.syncer) then
            return failRelocation(record, ("vehicle %d syncer diverged after client proof"):format(entry.index))
        end
        local positionError = math.sqrt((x - entry.x) ^ 2 + (y - entry.y) ^ 2 + (z - entry.centerZ) ^ 2)
        if positionError > record.positionTolerance then
            return failRelocation(record, ("vehicle %d position diverged after client proof: %.4f"):format(
                                      entry.index, positionError))
        end
        if angleError(rx, entry.rx) > 0.5 or angleError(ry, entry.ry) > 0.5 or
            angleError(rz, entry.heading) > 0.5 then
            return failRelocation(record, ("vehicle %d rotation diverged after client proof"):format(entry.index))
        end
        if getElementInterior(entry.vehicle) ~= entry.interior or
            getElementDimension(entry.vehicle) ~= entry.dimension then
            return failRelocation(record, ("vehicle %d world diverged after client proof"):format(entry.index))
        end
        if isElementFrozen(entry.vehicle) ~= true or
            getElementCollisionsEnabled(entry.vehicle) ~= entry.collisions then
            return failRelocation(record, ("vehicle %d physics diverged after client proof"):format(entry.index))
        end
        for seat, actualOccupant in pairs(getVehicleOccupants(entry.vehicle) or {}) do
            if entry.seatPeds[tonumber(seat)] ~= actualOccupant then
                return failRelocation(record, ("vehicle %d gained an undeclared occupant after client proof"):format(
                                          entry.index))
            end
        end
        for _, occupant in ipairs(entry.occupants) do
            if getPedOccupiedVehicle(occupant.ped) ~= entry.vehicle or
                getPedOccupiedVehicleSeat(occupant.ped) ~= occupant.seat or
                getElementInterior(occupant.ped) ~= entry.interior or
                getElementDimension(occupant.ped) ~= entry.dimension then
                return failRelocation(record, ("vehicle %d seat diverged after client proof"):format(entry.index))
            end
        end
        if not setElementVelocity(entry.vehicle, 0, 0, 0) or
            not setElementAngularVelocity(entry.vehicle, 0, 0, 0) or
            not setElementFrozen(entry.vehicle, entry.frozen) or
            isElementFrozen(entry.vehicle) ~= entry.frozen then
            return failRelocation(record, ("vehicle %d final physics restore refused"):format(entry.index))
        end
    end
    clearRelocationTimers(record)
    record.physicsRestored = true
    releaseRelocationReservations(record)
    cancelRelocationClients(record)
    record.verifiedAt = getTickCount()
    emitRelocation(record, "ready")
end)

addEvent("storyWorldRuntime:vehicleRelocationSeatRepairNeeded", true)
addEventHandler("storyWorldRuntime:vehicleRelocationSeatRepairNeeded", resourceRoot, function(id, entryIndex)
    local record = vehicleRelocationsById[tonumber(id)]
    entryIndex = tonumber(entryIndex)
    local entry = record and record.entries[entryIndex]
    if source ~= resourceRoot or not record or record.state ~= "verifying" or record.failing or
        not entry or entry.verifier ~= client then return end
    local repairs = tonumber(record.seatRepairs[entryIndex]) or 0
    if repairs >= 5 then return end
    record.seatRepairs[entryIndex] = repairs + 1
    setElementFrozen(entry.vehicle, true)
    setElementVelocity(entry.vehicle, 0, 0, 0)
    setElementAngularVelocity(entry.vehicle, 0, 0, 0)
    for occupantIndex, occupant in ipairs(entry.occupants) do
        if isPedInVehicle(occupant.ped) then removePedFromVehicle(occupant.ped) end
        local radians = math.rad(entry.heading + 90)
        setElementPosition(occupant.ped, entry.x + math.cos(radians) * (2 + occupantIndex * 0.2),
                           entry.y + math.sin(radians) * (2 + occupantIndex * 0.2), entry.centerZ + 0.5)
        setElementVelocity(occupant.ped, 0, 0, 0)
        if not warpPedIntoVehicle(occupant.ped, entry.vehicle, occupant.seat) then
            return failRelocation(record, ("vehicle %d seat %d repair refused"):format(entry.index,
                                                                                       occupant.seat))
        end
    end
end)

addEvent("storyWorldRuntime:vehicleRelocationRollbackVerified", true)
addEventHandler("storyWorldRuntime:vehicleRelocationRollbackVerified", resourceRoot, function(id, ok, observed, reason)
    local record = vehicleRelocationsById[tonumber(id)]
    if source ~= resourceRoot or not record or not record.failing or record.phase ~= "rollback_verifying" or
        not record.verifiers[client] or record.rollbackVerified[client] then return end
    if type(observed) ~= "table" then
        return record.finishFailure(false, "client rollback verification omitted observations")
    end
    record.rollbackObserved[client] = observed
    if ok ~= true then return record.finishFailure(false, reason or "client rollback verification failed") end
    record.rollbackVerified[client] = true
    if allRelocationVerifiers(record, "rollbackVerified") then record.commitRollbackProof() end
end)

local function occupancySnapshot(record, extra)
    local data = {
        id = record.id,
        state = record.state,
        syncer = record.syncer,
        assignments = #record.assignments,
    }
    for key, value in pairs(type(extra) == "table" and extra or {}) do data[key] = value end
    return data
end

local function emitOccupancy(record, state, extra)
    record.state = state
    triggerEvent("onStoryVehicleOccupancyStateChange", record.handle, state, occupancySnapshot(record, extra))
end

local function removeOccupancy(record, destroyHandle)
    if not record then return end
    clearOccupancyTimers(record)
    if isElement(record.syncer) then
        triggerClientEvent(record.syncer, "storyWorldRuntime:cancelOccupancy", resourceRoot, record.id)
    end
    occupancies[record.handle] = nil
    occupanciesById[record.id] = nil
    releaseStoryWorldElements(record)
    if destroyHandle and isElement(record.handle) then destroyElement(record.handle) end
end

local function failOccupancy(record, reason)
    if not record or record.state == "failed" or record.state == "ready" then return end
    clearOccupancyTimers(record)
    if isElement(record.syncer) then
        triggerClientEvent(record.syncer, "storyWorldRuntime:cancelOccupancy", resourceRoot, record.id)
    end
    releaseStoryWorldElements(record)
    emitOccupancy(record, "failed", {reason = reason})
end

local function advanceOccupancy(record)
    if not occupancies[record.handle] or record.state ~= "warping" then return end
    record.index = record.index + 1
    local assignment = record.assignments[record.index]
    if not assignment then
        record.state = "verifying"
        return triggerClientEvent(record.syncer, "storyWorldRuntime:verifyOccupancy", resourceRoot, record.id,
                                  record.assignments)
    end
    if not isElement(assignment.ped) or not isElement(assignment.vehicle) or
        not warpPedIntoVehicle(assignment.ped, assignment.vehicle, assignment.seat) then
        return failOccupancy(record, ("vehicle warp %d failed"):format(record.index))
    end
    record.stepTimer = setTimer(function() advanceOccupancy(record) end, record.stepDelay, 1)
end

function createStoryVehicleOccupancy(syncer, assignments, options)
    if not isElement(syncer) or getElementType(syncer) ~= "player" or type(assignments) ~= "table" or
        #assignments == 0 then
        return false, "invalid vehicle occupancy request"
    end
    options = type(options) == "table" and options or {}
    local immutable, peds, seats, reservedElements = {}, {}, {}, {}
    for index, assignment in ipairs(assignments) do
        local ped = type(assignment) == "table" and assignment.ped
        local vehicle = type(assignment) == "table" and assignment.vehicle
        local seat = type(assignment) == "table" and tonumber(assignment.seat)
        local pedType = isElement(ped) and getElementType(ped)
        if (pedType ~= "ped" and pedType ~= "player") or not isElement(vehicle) or
            getElementType(vehicle) ~= "vehicle" or not seat or seat % 1 ~= 0 or seat < 0 or
            seat > getVehicleMaxPassengers(vehicle) then
            return false, ("invalid vehicle occupancy assignment %d"):format(index)
        end
        if storyWorldElementReservations[ped] or storyWorldElementReservations[vehicle] then
            return false, ("vehicle occupancy assignment %d overlaps another story world operation"):format(index)
        end
        seats[vehicle] = seats[vehicle] or {}
        if peds[ped] or seats[vehicle][seat] then
            return false, ("duplicate vehicle occupancy assignment %d"):format(index)
        end
        peds[ped], seats[vehicle][seat] = true, true
        immutable[index] = {ped = ped, vehicle = vehicle, seat = seat}
        reservedElements[#reservedElements + 1] = ped
        if not reservedElements[vehicle] then
            reservedElements[#reservedElements + 1] = vehicle
            reservedElements[vehicle] = true
        end
    end

    nextOccupancyId = nextOccupancyId + 1
    local handle = createElement("story-vehicle-occupancy", ("story-vehicle-occupancy-%d"):format(nextOccupancyId))
    if not handle then return false, "vehicle occupancy handle creation failed" end
    setElementParent(handle, callerRoot())
    local record = {
        id = nextOccupancyId,
        handle = handle,
        caller = callerRoot(),
        syncer = syncer,
        assignments = immutable,
        state = "preparing",
        index = 0,
        stepDelay = math.max(50, math.min(500, tonumber(options.stepDelay) or 100)),
    }
    occupancies[handle] = record
    occupanciesById[record.id] = record
    reserveStoryWorldElements(record, reservedElements)
    record.timeout = setTimer(function()
        if occupancies[handle] == record and record.state ~= "ready" then
            failOccupancy(record, "client-confirmed vehicle occupancy timeout")
        end
    end, math.max(2000, math.min(30000, tonumber(options.timeout) or 15000)), 1)
    if options.stageActors == true then
        for index, assignment in ipairs(immutable) do
            if getElementType(assignment.ped) == "ped" then
                local x, y, z = getElementPosition(assignment.vehicle)
                local _, _, heading = getElementRotation(assignment.vehicle)
                local distance = 2 + index * 0.35
                local radians = math.rad(heading + 90)
                setElementInterior(assignment.ped, getElementInterior(assignment.vehicle))
                setElementDimension(assignment.ped, getElementDimension(assignment.vehicle))
                setElementPosition(assignment.ped, x + math.cos(radians) * distance,
                                   y + math.sin(radians) * distance, z + 0.5)
            end
        end
    end
    triggerClientEvent(syncer, "storyWorldRuntime:prepareOccupancy", resourceRoot, record.id, immutable)
    return handle
end

function releaseStoryVehicleOccupancy(handle)
    local record = occupancies[handle]
    if not record or record.caller ~= callerRoot() then return false end
    removeOccupancy(record, true)
    return true
end

function getStoryVehicleOccupancyState(handle)
    local record = occupancies[handle]
    if not record or record.caller ~= callerRoot() then return false end
    return record.state, occupancySnapshot(record)
end

addEvent("storyWorldRuntime:occupancyPrepared", true)
addEventHandler("storyWorldRuntime:occupancyPrepared", resourceRoot, function(id, ok, reason)
    local record = occupanciesById[tonumber(id)]
    if source ~= resourceRoot or not record or record.state ~= "preparing" or client ~= record.syncer then return end
    if ok ~= true then return failOccupancy(record, tostring(reason or "vehicle occupancy preflight failed")) end
    record.state = "warping"
    record.stepTimer = setTimer(function() advanceOccupancy(record) end, record.stepDelay, 1)
end)

addEvent("storyWorldRuntime:occupancyVerified", true)
addEventHandler("storyWorldRuntime:occupancyVerified", resourceRoot, function(id, ok, reason)
    local record = occupanciesById[tonumber(id)]
    if source ~= resourceRoot or not record or record.state ~= "verifying" or client ~= record.syncer then return end
    if ok ~= true then return failOccupancy(record, tostring(reason or "vehicle occupancy verification failed")) end
    clearTimer(record)
    -- Consumers commonly assign native authority immediately after occupancy.
    -- Emit from a fresh server tick instead of re-entering replication from the
    -- client RPC that acknowledged GTA's seats.
    record.readyTimer = setTimer(function()
        if occupancies[record.handle] == record and record.state == "verifying" then
            releaseStoryWorldElements(record)
            emitOccupancy(record, "ready")
        end
    end, 50, 1)
end)

addEvent("storyWorldRuntime:vehicleMeasured", true)
addEventHandler("storyWorldRuntime:vehicleMeasured", resourceRoot, function(id, vehicle, baseOffset, failureReason)
    local record = placementsById[tonumber(id)]
    baseOffset = tonumber(baseOffset)
    if source ~= resourceRoot or not record or record.state ~= "measuring" or client ~= record.syncer or
        vehicle ~= record.vehicle then
        return
    end
    if not finite(baseOffset) or baseOffset <= 0 or baseOffset > 10 then
        return failPlacement(record, tostring(failureReason or "client could not measure vehicle base offset"))
    end
    record.baseOffset = baseOffset
    record.centerZ = record.scriptZ + baseOffset
    modelBaseOffsets[record.syncer] = modelBaseOffsets[record.syncer] or {}
    modelBaseOffsets[record.syncer][record.model] = baseOffset
    setElementPosition(record.vehicle, record.x, record.y, record.centerZ)
    setElementRotation(record.vehicle, 0, 0, record.heading)
    record.state = "verifying"
    triggerClientEvent(record.syncer, "storyWorldRuntime:verifyVehicle", resourceRoot, record.id, record.vehicle,
                       record.centerZ, record.tolerance)
end)

addEvent("storyWorldRuntime:vehicleVerified", true)
addEventHandler("storyWorldRuntime:vehicleVerified", resourceRoot, function(id, vehicle, ok, reason, actualZ)
    local record = placementsById[tonumber(id)]
    if source ~= resourceRoot or not record or record.state ~= "verifying" or client ~= record.syncer or
        vehicle ~= record.vehicle then
        return
    end
    if ok ~= true then return failPlacement(record, tostring(reason or "client verification failed")) end
    actualZ = tonumber(actualZ)
    if not finite(actualZ) or math.abs(actualZ - record.centerZ) > record.tolerance then
        return failPlacement(record, "client observed an invalid vehicle centre Z")
    end
    clearTimer(record)
    releaseStoryWorldElements(record)
    emitPlacement(record, "ready", {actualZ = actualZ})
end)

local function teardownSnapshot(record, extra)
    local data = {id = record.id, state = record.state, clients = #record.players, elements = #record.elements}
    for key, value in pairs(type(extra) == "table" and extra or {}) do data[key] = value end
    return data
end

local function emitTeardown(record, state, extra)
    record.state = state
    triggerEvent("onStoryWorldTeardownStateChange", record.handle, state, teardownSnapshot(record, extra))
end

local function removeTeardown(record, destroyHandle, restoreFade)
    if not record then return end
    clearTimer(record)
    if restoreFade == true then
        triggerClientEvent(record.players, "storyWorldRuntime:cancelTeardown", resourceRoot, record.id, true)
    end
    teardowns[record.handle] = nil
    teardownsById[record.id] = nil
    if destroyHandle and isElement(record.handle) then destroyElement(record.handle) end
end

local function failTeardown(record, reason)
    if not record or record.state == "failed" or record.state == "ready" then return end
    clearTimer(record)
    triggerClientEvent(record.players, "storyWorldRuntime:cancelTeardown", resourceRoot, record.id, true)
    emitTeardown(record, "failed", {reason = reason})
end

local function commitTeardown(record)
    if record.state ~= "arming" then return end
    record.state = "destroying"
    for _, element in ipairs(record.elements) do
        if isElement(element) then destroyElement(element) end
    end
    triggerClientEvent(record.players, "storyWorldRuntime:commitTeardown", resourceRoot, record.id)
end

function destroyStoryWorldElements(players, elements, options)
    if type(players) ~= "table" or type(elements) ~= "table" then return false, "invalid teardown lists" end
    options = type(options) == "table" and options or {}
    local validPlayers, validElements = {}, {}
    for _, player in ipairs(players) do
        if not isElement(player) or getElementType(player) ~= "player" then return false, "invalid teardown player" end
        validPlayers[#validPlayers + 1] = player
    end
    for _, element in ipairs(elements) do
        if isElement(element) then validElements[#validElements + 1] = element end
    end
    if #validPlayers == 0 then return false, "teardown requires a client" end

    nextTeardownId = nextTeardownId + 1
    local handle = createElement("story-world-teardown", ("story-world-teardown-%d"):format(nextTeardownId))
    if not handle then return false, "teardown handle creation failed" end
    setElementParent(handle, callerRoot())
    local record = {
        id = nextTeardownId,
        handle = handle,
        caller = callerRoot(),
        players = validPlayers,
        elements = validElements,
        armed = {},
        gone = {},
        state = "arming",
        fadeOut = math.max(0, math.min(3, tonumber(options.fadeOut) or 0)),
    }
    teardowns[handle] = record
    teardownsById[record.id] = record
    record.timeout = setTimer(function()
        if teardowns[handle] == record and record.state ~= "ready" then
            failTeardown(record, "client-confirmed world teardown timeout")
        end
    end, math.max(2000, math.min(30000, tonumber(options.timeout) or 10000)), 1)
    triggerClientEvent(validPlayers, "storyWorldRuntime:prepareTeardown", resourceRoot, record.id, validElements,
                       record.fadeOut)
    return handle
end

function releaseStoryWorldTeardown(handle)
    local record = teardowns[handle]
    if not record or record.caller ~= callerRoot() then return false end
    removeTeardown(record, true, record.state ~= "ready")
    return true
end

addEvent("storyWorldRuntime:teardownArmed", true)
addEventHandler("storyWorldRuntime:teardownArmed", resourceRoot, function(id)
    local record = teardownsById[tonumber(id)]
    if source ~= resourceRoot or not record or record.state ~= "arming" or record.armed[client] ~= nil then return end
    local expected = false
    for _, player in ipairs(record.players) do if player == client then expected = true break end end
    if not expected then return end
    record.armed[client] = true
    for _, player in ipairs(record.players) do if not record.armed[player] then return end end
    commitTeardown(record)
end)

addEvent("storyWorldRuntime:teardownGone", true)
addEventHandler("storyWorldRuntime:teardownGone", resourceRoot, function(id, ok, reason)
    local record = teardownsById[tonumber(id)]
    if source ~= resourceRoot or not record or record.state ~= "destroying" or record.gone[client] ~= nil then return end
    local expected = false
    for _, player in ipairs(record.players) do if player == client then expected = true break end end
    if not expected then return end
    if ok ~= true then return failTeardown(record, tostring(reason or "client teardown failed")) end
    record.gone[client] = true
    for _, player in ipairs(record.players) do if not record.gone[player] then return end end
    clearTimer(record)
    emitTeardown(record, "ready")
end)

addEventHandler("onPlayerQuit", root, function()
    modelBaseOffsets[source] = nil
    local modelLease = playerModelLeaseByPlayer[source]
    if modelLease then playerModelLeaseByPlayer[source] = nil end
    local fileCutscene = fileCutsceneByPlayer[source]
    if fileCutscene then
        fileCutscene.unavailable[source] = true
        fileCutsceneByPlayer[source] = nil
        if fileCutscene.phase == "releasing" then
            fileCutscene.releaseTerminal = "failed"
            fileCutscene.releaseReason = tostring(fileCutscene.releaseReason or "file cutscene release") ..
                                             "; participant disconnected during release"
            if allFileCutscenePlayers(fileCutscene, "released") then finishFileCutsceneRelease(fileCutscene) end
        elseif fileCutscene.phase ~= "terminal" then
            failFileCutscene(fileCutscene, "file cutscene participant disconnected")
        end
    end
    for _, record in pairs(vehicleRelocations) do
        if record.state ~= "ready" and record.state ~= "failed" then
            if record.verifiers[source] then
                record.unavailable[source] = true
                if record.failing and record.phase == "rollback_verifying" and
                    allRelocationVerifiers(record, "rollbackVerified") then
                    record.commitRollbackProof()
                else
                    failRelocation(record, "vehicle relocation verifier disconnected")
                end
            else
                for _, player in ipairs(record.players) do
                    if player == source then
                        failRelocation(record, "vehicle relocation participant disconnected")
                        break
                    end
                end
            end
        end
    end
    for _, record in pairs(teardowns) do
        if record.state ~= "ready" and record.state ~= "failed" then
            for _, player in ipairs(record.players) do
                if player == source then failTeardown(record, "teardown client disconnected") break end
            end
        end
    end
    for _, record in pairs(placements) do
        if record.syncer == source and record.state ~= "ready" and record.state ~= "failed" then
            failPlacement(record, "placement syncer disconnected")
        end
    end
    for _, record in pairs(occupancies) do
        if record.syncer == source and record.state ~= "ready" and record.state ~= "failed" then
            failOccupancy(record, "vehicle occupancy syncer disconnected")
        end
    end
end)

addEventHandler("onElementDestroy", root, function()
    local relocation = vehicleRelocations[source]
    if relocation then
        if relocation.state ~= "ready" and relocation.state ~= "failed" then
            relocation.handleDestroyed, relocation.removeAfterFailure = true, true
            return failRelocation(relocation, "vehicle relocation handle destroyed")
        end
        return removeRelocation(relocation, false)
    end
    local fileCutscene = fileCutscenes[source]
    if fileCutscene then return removeFileCutscene(fileCutscene, false, true) end
    local placement = placements[source]
    if placement then return removePlacement(placement, false, true) end
    local teardown = teardowns[source]
    if teardown then return removeTeardown(teardown, false, teardown.state ~= "ready") end
    local occupancy = occupancies[source]
    if occupancy then return removeOccupancy(occupancy, false) end
    local playerModelLease = playerModelLeases[source]
    if playerModelLease then
        restorePlayerModelLease(playerModelLease)
        return
    end
    for _, record in pairs(placements) do
        if record.vehicle == source and record.state ~= "ready" and record.state ~= "failed" then
            failPlacement(record, "vehicle destroyed during SCM placement")
            return
        end
    end
    for _, record in pairs(occupancies) do
        if record.state ~= "ready" and record.state ~= "failed" then
            for _, assignment in ipairs(record.assignments) do
                if source == assignment.ped or source == assignment.vehicle then
                    failOccupancy(record, "vehicle occupancy element destroyed")
                    return
                end
            end
        end
    end
    for _, record in pairs(vehicleRelocations) do
        if record.state ~= "ready" and record.state ~= "failed" then
            for _, entry in ipairs(record.entries) do
                if source == entry.vehicle then
                    failRelocation(record, "vehicle destroyed during relocation")
                    return
                end
                for _, occupant in ipairs(entry.occupants) do
                    if source == occupant.ped then
                        failRelocation(record, "occupant destroyed during vehicle relocation")
                        return
                    end
                end
            end
        end
    end
end)

addEventHandler("onResourceStop", root, function(stoppedResource)
    local stoppedRoot = getResourceRootElement(stoppedResource)
    local runtimeStopping = stoppedRoot == resourceRoot
    if runtimeStopping then
        -- Mission elements created through exports are owned by this runtime
        -- and will disappear after Lua stop handlers finish. Notify consumers
        -- while their own VMs and cleanup paths are still alive.
        triggerEvent("onStoryWorldRuntimeStopping", resourceRoot)
    end
    local ownedPlacements, ownedTeardowns, ownedOccupancies, ownedPlayerModelLeases, ownedFileCutscenes,
        ownedRelocations = {}, {}, {}, {}, {}, {}
    for _, record in pairs(placements) do
        if stoppedRoot == resourceRoot or record.caller == stoppedRoot then
            ownedPlacements[#ownedPlacements + 1] = record
        end
    end
    for _, record in pairs(teardowns) do
        if stoppedRoot == resourceRoot or record.caller == stoppedRoot then
            ownedTeardowns[#ownedTeardowns + 1] = record
        end
    end
    for _, record in pairs(occupancies) do
        if stoppedRoot == resourceRoot or record.caller == stoppedRoot then
            ownedOccupancies[#ownedOccupancies + 1] = record
        end
    end
    for _, record in pairs(playerModelLeases) do
        if stoppedRoot == resourceRoot or record.caller == stoppedRoot then
            ownedPlayerModelLeases[#ownedPlayerModelLeases + 1] = record
        end
    end
    for _, record in pairs(fileCutscenes) do
        if stoppedRoot == resourceRoot or record.caller == stoppedRoot then
            ownedFileCutscenes[#ownedFileCutscenes + 1] = record
        end
    end
    for _, record in pairs(vehicleRelocations) do
        if stoppedRoot == resourceRoot or record.caller == stoppedRoot then
            ownedRelocations[#ownedRelocations + 1] = record
        end
    end
    for _, record in ipairs(ownedPlacements) do
        if runtimeStopping and record.caller ~= resourceRoot and record.state ~= "ready" and record.state ~= "failed" then
            failPlacement(record, "story world runtime stopped during SCM placement")
        end
        removePlacement(record, true, true)
    end
    for _, record in ipairs(ownedTeardowns) do
        if runtimeStopping and record.caller ~= resourceRoot and record.state ~= "ready" and record.state ~= "failed" then
            failTeardown(record, "story world runtime stopped during teardown")
        end
        removeTeardown(record, true, true)
    end
    for _, record in ipairs(ownedOccupancies) do
        if runtimeStopping and record.caller ~= resourceRoot and record.state ~= "ready" and record.state ~= "failed" then
            failOccupancy(record, "story world runtime stopped during vehicle occupancy")
        end
        removeOccupancy(record, true)
    end
    for _, record in ipairs(ownedFileCutscenes) do
        if runtimeStopping and record.caller ~= resourceRoot and record.phase ~= "terminal" then
            emitFileCutscene(record, "failed", {reason = "story world runtime stopped during file cutscene"})
        end
        removeFileCutscene(record, true, true)
    end
    for _, record in ipairs(ownedRelocations) do
        if record.state == "ready" or record.state == "failed" then
            removeRelocation(record, true)
        elseif runtimeStopping then
            local restored, restoreReason = restoreRelocationPhysics(record, true)
            if record.caller ~= resourceRoot and isElement(record.handle) then
                emitRelocation(record, "failed", {reason = "story world runtime stopped during vehicle relocation",
                                                  cleanupRestored = restored, cleanupReason = restoreReason})
            end
            removeRelocation(record, true)
        else
            if isElement(record.handle) then setElementParent(record.handle, resourceRoot) end
            record.removeAfterFailure = true
            failRelocation(record, "vehicle relocation caller stopped")
        end
    end
    for _, record in ipairs(ownedPlayerModelLeases) do
        restorePlayerModelLease(record)
        if isElement(record.handle) then destroyElement(record.handle) end
    end
end)

outputServerLog("[story world runtime] Ready: SCM placement, player-model leases, grouped file cutscenes, safe vehicle relocation, occupancy and client-confirmed teardown available.")
