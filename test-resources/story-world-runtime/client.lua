local teardowns = {}
local occupancies = {}
local vehiclePlacements = {}
local fileCutscenes = {}
local vehicleRelocations = {}

local function stopVehicleRelocation(id)
    local record = vehicleRelocations[id]
    if not record then return end
    if isTimer(record.timer) then killTimer(record.timer) end
    for _, lease in ipairs(record.leases) do
        if type(releaseElementStreamingLease) == "function" then releaseElementStreamingLease(lease) end
    end
    vehicleRelocations[id] = nil
end

local function acquireRelocationLease(record, element)
    if record.leased[element] then return true end
    if type(acquireElementStreamingLease) ~= "function" or
        type(releaseElementStreamingLease) ~= "function" then return false end
    local lease = isElement(element) and acquireElementStreamingLease(element)
    if not lease then return false end
    record.leased[element] = true
    record.leases[#record.leases + 1] = lease
    return true
end

local function relocationSeatsReady(entry, vehicle, interior, dimension)
    local expectedSeats = type(entry.seatPeds) == "table" and entry.seatPeds or {}
    for seat, ped in pairs(getVehicleOccupants(vehicle) or {}) do
        if expectedSeats[tonumber(seat)] ~= ped then
            return false, ("unexpected occupant in seat %s"):format(tostring(seat))
        end
    end
    for seat, ped in pairs(expectedSeats) do
        if not isElement(ped) then return false, ("seat %s ped missing"):format(tostring(seat)) end
        if ped ~= localPlayer and not isElementStreamedIn(ped) then
            return false, ("seat %s ped not streamed"):format(tostring(seat))
        end
        if getPedOccupiedVehicle(ped) ~= vehicle then
            return false, ("seat %s ped not in target vehicle"):format(tostring(seat))
        end
        if getPedOccupiedVehicleSeat(ped) ~= tonumber(seat) then
            return false, ("seat %s actual seat=%s"):format(tostring(seat),
                                                             tostring(getPedOccupiedVehicleSeat(ped)))
        end
        if interior ~= nil and getElementInterior(ped) ~= interior then
            return false, ("seat %s interior=%s expected=%s"):format(
                              tostring(seat), tostring(getElementInterior(ped)), tostring(interior))
        end
        if dimension ~= nil and getElementDimension(ped) ~= dimension then
            return false, ("seat %s dimension=%s expected=%s"):format(
                              tostring(seat), tostring(getElementDimension(ped)), tostring(dimension))
        end
    end
    return true
end

addEvent("storyWorldRuntime:prepareVehicleRelocation", true)
addEventHandler("storyWorldRuntime:prepareVehicleRelocation", resourceRoot, function(id, entries)
    id = tonumber(id)
    if source ~= resourceRoot or vehicleRelocations[id] or type(entries) ~= "table" or #entries == 0 then return end
    local record = {id = id, entries = entries, leases = {}, leased = {}, seatRepairAt = {}}
    vehicleRelocations[id] = record
    for _, entry in ipairs(entries) do
        if not acquireRelocationLease(record, entry.vehicle) then
            triggerServerEvent("storyWorldRuntime:vehicleRelocationPrepared", resourceRoot, id, false, {},
                               "vehicle relocation streaming lease refused")
            return
        end
        for _, occupant in ipairs(type(entry.occupants) == "table" and entry.occupants or {}) do
            if occupant.ped ~= localPlayer and not acquireRelocationLease(record, occupant.ped) then
                triggerServerEvent("storyWorldRuntime:vehicleRelocationPrepared", resourceRoot, id, false, {},
                                   "occupant relocation streaming lease refused")
                return
            end
        end
        if entry.requireGround == true then
            if type(enginePreloadWorldArea) ~= "function" then
                triggerServerEvent("storyWorldRuntime:vehicleRelocationPrepared", resourceRoot, id, false, {},
                                   "vehicle relocation collision preload API unavailable")
                return
            end
            local preloadZ = tonumber(entry.scriptZ) or tonumber(entry.centerZ)
            local called = preloadZ and pcall(enginePreloadWorldArea,
                                               Vector3(entry.x, entry.y, preloadZ), "collisions")
            if not called then
                triggerServerEvent("storyWorldRuntime:vehicleRelocationPrepared", resourceRoot, id, false, {},
                                   ("vehicle %d collision preload refused"):format(entry.index))
                return
            end
        end
    end
    record.startedAt = getTickCount()
    record.timer = setTimer(function()
        if vehicleRelocations[id] ~= record then return end
        local ready, offsets, reason = true, {}, nil
        for _, entry in ipairs(entries) do
            if not isElement(entry.vehicle) or not isElementStreamedIn(entry.vehicle) then
                ready, reason = false, ("vehicle %d not streamed for preparation"):format(entry.index)
                break
            end
            if entry.verifierIsSyncer and not isElementSyncer(entry.vehicle) then
                ready, reason = false, ("vehicle %d syncer not authoritative during preparation"):format(entry.index)
                break
            end
            local seatsReady, seatReason = relocationSeatsReady(entry, entry.vehicle)
            if not seatsReady then
                ready, reason = false, ("vehicle %d exact seat map not streamed for preparation: %s"):format(
                                           entry.index, tostring(seatReason))
            end
            if not ready then break end
            if entry.scriptZ ~= nil then
                local offset = tonumber(getElementDistanceFromCentreOfMassToBaseOfModel(entry.vehicle))
                if not offset or offset <= 0 then
                    ready, reason = false, ("vehicle %d base offset unavailable"):format(entry.index)
                    break
                end
                offsets[tonumber(entry.index)] = offset
            end
            if entry.requireGround == true then
                local offset = offsets[tonumber(entry.index)] or
                                   tonumber(getElementDistanceFromCentreOfMassToBaseOfModel(entry.vehicle))
                local expectedBottom = tonumber(entry.scriptZ) or
                                           (tonumber(entry.centerZ) and offset and entry.centerZ - offset)
                if expectedBottom then
                    pcall(enginePreloadWorldArea, Vector3(entry.x, entry.y, expectedBottom), "collisions")
                end
                local groundHit = expectedBottom and processLineOfSight(
                                      entry.x, entry.y, expectedBottom + 3, entry.x, entry.y, expectedBottom - 5,
                                      true, false, false, true, true, false, false, false, entry.vehicle)
                if groundHit ~= true then
                    ready, reason = false, ("vehicle %d target collision not loaded"):format(entry.index)
                    break
                end
            end
        end
        if ready then
            killTimer(record.timer)
            record.timer = nil
            return triggerServerEvent("storyWorldRuntime:vehicleRelocationPrepared", resourceRoot, id, true,
                                      offsets)
        end
        if getTickCount() - record.startedAt >= 10000 then
            killTimer(record.timer)
            record.timer = nil
            triggerServerEvent("storyWorldRuntime:vehicleRelocationPrepared", resourceRoot, id, false, {}, reason)
        end
    end, 50, 0)
end)

addEvent("storyWorldRuntime:verifyVehicleRelocation", true)
addEventHandler("storyWorldRuntime:verifyVehicleRelocation", resourceRoot,
                function(id, entries, positionTolerance, groundTolerance, requiredSamples)
    id, positionTolerance, groundTolerance, requiredSamples = tonumber(id), tonumber(positionTolerance) or 0.08,
                                                                 tonumber(groundTolerance) or 0.2,
                                                                 tonumber(requiredSamples) or 3
    local record = vehicleRelocations[id]
    if source ~= resourceRoot or not record or type(entries) ~= "table" or #entries == 0 then return end
    record.entries, record.stableSamples, record.startedAt = entries, 0, getTickCount()
    record.timer = setTimer(function()
        if vehicleRelocations[id] ~= record then return end
        local stable, reason, observed = true, nil, {}
        for _, entry in ipairs(entries) do
            local vehicle = entry.vehicle
            if not isElement(vehicle) or not isElementStreamedIn(vehicle) then
                stable, reason = false, ("vehicle %d not streamed"):format(entry.index)
                break
            end
            local x, y, z = getElementPosition(vehicle)
            local rx, ry, rz = getElementRotation(vehicle)
            local vx, vy, vz = getElementVelocity(vehicle)
            local avx, avy, avz = getElementAngularVelocity(vehicle)
            local baseOffset = tonumber(entry.baseOffset) or
                                   tonumber(getElementDistanceFromCentreOfMassToBaseOfModel(vehicle))
            local expectedBottom = baseOffset and entry.centerZ - baseOffset or entry.centerZ
            if entry.requireGround == true and type(enginePreloadWorldArea) == "function" then
                pcall(enginePreloadWorldArea, Vector3(entry.x, entry.y, expectedBottom), "collisions")
            end
            local groundHit, _, _, groundZ = processLineOfSight(
                entry.x, entry.y, expectedBottom + 3, entry.x, entry.y, expectedBottom - 5,
                true, false, false, true, true, false, false, false, vehicle)
            local positionError = math.sqrt((x - entry.x) ^ 2 + (y - entry.y) ^ 2 + (z - entry.centerZ) ^ 2)
            local bottomClearance = baseOffset and groundHit and z - baseOffset - groundZ or nil
            local seatsReady, seatReason = relocationSeatsReady(entry, vehicle, entry.interior, entry.dimension)
            if not seatsReady and getTickCount() - (record.seatRepairAt[entry.index] or 0) >= 500 then
                record.seatRepairAt[entry.index] = getTickCount()
                triggerServerEvent("storyWorldRuntime:vehicleRelocationSeatRepairNeeded", resourceRoot, id,
                                   entry.index)
            end
            local zeroMotion = math.max(math.abs(vx), math.abs(vy), math.abs(vz), math.abs(avx), math.abs(avy),
                                        math.abs(avz)) <= 0.01
            local grounded = entry.requireGround ~= true or
                                 (bottomClearance and math.abs(bottomClearance) <= groundTolerance)
            local syncerReady = entry.verifierIsSyncer ~= true or isElementSyncer(vehicle)
            local physicsReady = isElementFrozen(vehicle) == entry.frozen and
                                     getElementCollisionsEnabled(vehicle) == entry.collisions
            local function angleError(actual, expected)
                return math.abs((actual - expected + 180) % 360 - 180)
            end
            local transformReady = getElementInterior(vehicle) == entry.interior and
                                       getElementDimension(vehicle) == entry.dimension and
                                       angleError(rx, entry.rx) <= 0.5 and angleError(ry, entry.ry) <= 0.5 and
                                       angleError(rz, entry.heading) <= 0.5
            observed[entry.index] = {x = x, y = y, z = z, groundZ = groundZ, baseOffset = baseOffset,
                                     bottomClearance = bottomClearance, positionError = positionError,
                                     groundHit = groundHit, onGround = isVehicleOnGround(vehicle), seats = seatsReady,
                                     zeroMotion = zeroMotion,
                                     syncer = isElementSyncer(vehicle), frozen = isElementFrozen(vehicle),
                                     collisions = getElementCollisionsEnabled(vehicle), rx = rx, ry = ry, rz = rz,
                                     interior = getElementInterior(vehicle), dimension = getElementDimension(vehicle),
                                     transform = transformReady, sample = record.stableSamples + 1,
                                     timestamp = getTickCount()}
            if positionError > positionTolerance or not zeroMotion or not grounded or not seatsReady or
                not syncerReady or not physicsReady or not transformReady then
                stable = false
                reason = ("vehicle %d unstable pos=%.4f ground=%s clearance=%s seats=%s(%s) motion=%s syncer=%s"):format(
                             entry.index, positionError, tostring(grounded), tostring(bottomClearance),
                             tostring(seatsReady), tostring(seatReason), tostring(zeroMotion), tostring(syncerReady))
                break
            end
        end
        record.stableSamples = stable and record.stableSamples + 1 or 0
        record.lastObserved, record.lastReason = observed, reason
        if record.stableSamples >= requiredSamples then
            killTimer(record.timer)
            record.timer = nil
            return triggerServerEvent("storyWorldRuntime:vehicleRelocationVerified", resourceRoot, id, true,
                                      observed)
        end
        if getTickCount() - record.startedAt >= 15000 then
            killTimer(record.timer)
            record.timer = nil
            triggerServerEvent("storyWorldRuntime:vehicleRelocationVerified", resourceRoot, id, false,
                               record.lastObserved, record.lastReason or "vehicle relocation did not stabilize")
        end
    end, 50, 0)
end)

addEvent("storyWorldRuntime:cancelVehicleRelocation", true)
addEventHandler("storyWorldRuntime:cancelVehicleRelocation", resourceRoot, function(id)
    if source == resourceRoot then stopVehicleRelocation(tonumber(id)) end
end)

addEvent("storyWorldRuntime:verifyVehicleRelocationRollback", true)
addEventHandler("storyWorldRuntime:verifyVehicleRelocationRollback", resourceRoot,
                function(id, entries, positionTolerance, requiredSamples)
    id, positionTolerance, requiredSamples = tonumber(id), tonumber(positionTolerance) or 0.08,
                                               tonumber(requiredSamples) or 3
    local record = vehicleRelocations[id]
    if source ~= resourceRoot or not record or type(entries) ~= "table" then return end
    if isTimer(record.timer) then killTimer(record.timer) end
    record.stableSamples, record.startedAt = 0, getTickCount()
    record.timer = setTimer(function()
        if vehicleRelocations[id] ~= record then return end
        local stable, reason, observed = true, nil, {}
        for _, entry in ipairs(entries) do
            local vehicle = entry.vehicle
            if not isElement(vehicle) or not isElementStreamedIn(vehicle) then
                observed[entry.index] = {vehicle = vehicle, exists = isElement(vehicle), streamed = false,
                                         timestamp = getTickCount()}
                stable, reason = false, ("rollback vehicle %d not streamed"):format(entry.index)
                break
            end
            local x, y, z = getElementPosition(vehicle)
            local rx, ry, rz = getElementRotation(vehicle)
            local vx, vy, vz = getElementVelocity(vehicle)
            local avx, avy, avz = getElementAngularVelocity(vehicle)
            local function angleError(actual, expected)
                return math.abs((actual - expected + 180) % 360 - 180)
            end
            local seatsReady, seatReason = relocationSeatsReady(entry, vehicle, entry.interior, entry.dimension)
            local positionError = math.sqrt((x - entry.x) ^ 2 + (y - entry.y) ^ 2 +
                                                (z - entry.centerZ) ^ 2)
            local rotationError = math.max(angleError(rx, entry.rx), angleError(ry, entry.ry),
                                           angleError(rz, entry.heading))
            local worldReady = getElementInterior(vehicle) == entry.interior and
                                   getElementDimension(vehicle) == entry.dimension
            local physicsReady = isElementFrozen(vehicle) == entry.frozen and
                                     getElementCollisionsEnabled(vehicle) == entry.collisions
            local syncerReady = entry.verifierIsSyncer ~= true or isElementSyncer(vehicle)
            local zeroMotion = math.max(math.abs(vx), math.abs(vy), math.abs(vz), math.abs(avx), math.abs(avy),
                                        math.abs(avz)) <= 0.01
            observed[entry.index] = {vehicle = vehicle, exists = true, streamed = true,
                                     expectedX = entry.x, expectedY = entry.y, expectedZ = entry.centerZ,
                                     expectedRx = entry.rx, expectedRy = entry.ry, expectedRz = entry.heading,
                                     x = x, y = y, z = z, rx = rx, ry = ry, rz = rz,
                                     positionError = positionError, rotationError = rotationError,
                                     interior = getElementInterior(vehicle), dimension = getElementDimension(vehicle),
                                     frozen = isElementFrozen(vehicle),
                                     collisions = getElementCollisionsEnabled(vehicle), seats = seatsReady,
                                     seatReason = seatReason, syncer = isElementSyncer(vehicle),
                                     worldReady = worldReady, physicsReady = physicsReady,
                                     syncerReady = syncerReady, zeroMotion = zeroMotion,
                                     vx = vx, vy = vy, vz = vz, avx = avx, avy = avy, avz = avz,
                                     sample = record.stableSamples + 1,
                                     timestamp = getTickCount()}
            if positionError > positionTolerance or rotationError > 0.5 or not worldReady or
                not physicsReady or not seatsReady or not syncerReady or not zeroMotion then
                stable, reason = false,
                    ("rollback vehicle %d diverged pos=%.4f rot=%.4f world=%s physics=%s seats=%s(%s) " ..
                        "syncer=%s motion=%s"):format(entry.index, positionError, rotationError,
                                                      tostring(worldReady), tostring(physicsReady),
                                                      tostring(seatsReady), tostring(seatReason),
                                                      tostring(syncerReady), tostring(zeroMotion))
                break
            end
        end
        record.stableSamples = stable and record.stableSamples + 1 or 0
        record.lastObserved, record.lastReason = observed, reason
        if record.stableSamples >= requiredSamples then
            killTimer(record.timer)
            record.timer = nil
            return triggerServerEvent("storyWorldRuntime:vehicleRelocationRollbackVerified", resourceRoot, id, true,
                                      observed)
        end
        if getTickCount() - record.startedAt >= 4500 then
            killTimer(record.timer)
            record.timer = nil
            triggerServerEvent("storyWorldRuntime:vehicleRelocationRollbackVerified", resourceRoot, id, false,
                               record.lastObserved, record.lastReason or "rollback did not stabilize")
        end
    end, 50, 0)
end)

local function clearFileCutsceneTimers(record)
    for _, name in ipairs({"loadTimer", "playTimer", "fadeTimer"}) do
        if isTimer(record[name]) then killTimer(record[name]) end
        record[name] = nil
    end
end

local function hasFileCutsceneLease(record)
    if not record or not record.token or type(isFileCutsceneLeaseActive) ~= "function" then return false end
    local ok, active = pcall(isFileCutsceneLeaseActive, record.token)
    return ok and active == true
end

local function stopFileCutscene(id, preserveFade, keepOnFailure)
    local record = fileCutscenes[id]
    if not record then return true end
    clearFileCutsceneTimers(record)
    local released = true
    if record.token and hasFileCutsceneLease(record) then
        if type(releaseFileCutscene) ~= "function" then
            released = false
        else
            local ok, result = pcall(releaseFileCutscene, record.token, preserveFade == true)
            released = ok and result == true
        end
    end
    if released or keepOnFailure ~= true then fileCutscenes[id] = nil end
    if preserveFade ~= true and not released then fadeCamera(true, 0) end
    return released
end

local function ensureFileCutsceneReleaseRetry(record)
    if isTimer(record.releaseRetryTimer) then return end
    record.releaseRetryTimer = setTimer(function()
        if fileCutscenes[record.id] ~= record then
            if isTimer(record.releaseRetryTimer) then killTimer(record.releaseRetryTimer) end
            record.releaseRetryTimer = nil
            return
        end
        if stopFileCutscene(record.id, false, true) then
            triggerServerEvent("storyWorldRuntime:fileCutsceneReleased", resourceRoot, record.id, true)
        end
    end, 250, 0)
end

local function reportFileCutsceneLoaded(record, ok, reason)
    if record.loadedReported then return end
    record.loadedReported = true
    triggerServerEvent("storyWorldRuntime:fileCutsceneLoaded", resourceRoot, record.id, ok == true, reason)
end

local function reportFileCutsceneStarted(record, ok, reason)
    if record.startedReported then return end
    record.startedReported = true
    triggerServerEvent("storyWorldRuntime:fileCutsceneStarted", resourceRoot, record.id, ok == true, reason)
end

local function reportFileCutsceneFinished(record, ok, reason)
    if record.finishedReported then return end
    record.finishedReported = true
    local skipped = false
    if record.token and type(wasFileCutsceneSkipped) == "function" then
        local queried, result = pcall(wasFileCutsceneSkipped, record.token)
        skipped = queried and result == true
    end
    triggerServerEvent("storyWorldRuntime:fileCutsceneFinished", resourceRoot, record.id, ok == true, skipped,
                       record.startedAt and getTickCount() - record.startedAt or nil, reason)
end

local function applyFileCutsceneSkip(record)
    if not record.startedAt then
        record.skipPending = true
        return true
    end
    if record.skipApplied then return true end
    local ok, skipped = pcall(skipFileCutscene, record.token)
    if not ok or skipped ~= true then
        reportFileCutsceneFinished(record, false, "native file cutscene skip refused")
        return false
    end
    record.skipApplied = true
    return true
end

addEvent("storyWorldRuntime:prepareFileCutscene", true)
addEventHandler("storyWorldRuntime:prepareFileCutscene", resourceRoot,
                function(id, name, visibleArea, leader, allowLeaderSkip, fadeIn)
    if source ~= resourceRoot or fileCutscenes[tonumber(id)] then return end
    id = tonumber(id)
    local record = {
        id = id,
        leader = leader,
        allowLeaderSkip = allowLeaderSkip == true,
        fadeIn = math.max(0, math.min(3, tonumber(fadeIn) or 1)),
        requestedAt = getTickCount(),
    }
    fileCutscenes[id] = record
    for otherId in pairs(fileCutscenes) do
        if otherId ~= id then return reportFileCutsceneLoaded(record, false, "another grouped file cutscene is active") end
    end
    local required = {"requestFileCutscene", "releaseFileCutscene", "isFileCutsceneLeaseActive",
                      "isFileCutsceneLoaded", "startFileCutscene", "fadeFileCutscene",
                      "isFileCutsceneFading", "isFileCutsceneFinished", "isFileCutsceneSkipInputPressed",
                      "wasFileCutsceneSkipped", "skipFileCutscene"}
    for _, nameOfFunction in ipairs(required) do
        if type(_G[nameOfFunction]) ~= "function" then
            return reportFileCutsceneLoaded(record, false, "file cutscene API unavailable: " .. nameOfFunction)
        end
    end
    local requested, token
    if visibleArea == nil then
        requested, token = pcall(requestFileCutscene, name)
    else
        requested, token = pcall(requestFileCutscene, name, visibleArea)
    end
    if not requested or not token then
        return reportFileCutsceneLoaded(record, false, "native file cutscene request refused")
    end
    record.token = token
    record.loadTimer = setTimer(function()
        if fileCutscenes[id] ~= record or record.loadedReported then return end
        if not hasFileCutsceneLease(record) then
            return reportFileCutsceneLoaded(record, false, "native file cutscene lease lost during load")
        end
        local queried, loaded = pcall(isFileCutsceneLoaded, record.token)
        if not queried then return reportFileCutsceneLoaded(record, false, "native file cutscene load query failed") end
        if loaded == true then
            killTimer(record.loadTimer)
            record.loadTimer = nil
            reportFileCutsceneLoaded(record, true)
        end
    end, 50, 0)
end)

addEvent("storyWorldRuntime:startFileCutscene", true)
addEventHandler("storyWorldRuntime:startFileCutscene", resourceRoot, function(id)
    local record = fileCutscenes[tonumber(id)]
    if source ~= resourceRoot or not record or record.startedAt then return end
    if not hasFileCutsceneLease(record) then
        return reportFileCutsceneStarted(record, false, "native file cutscene lease lost before start")
    end
    local startedOk, started = pcall(startFileCutscene, record.token)
    if not startedOk or started ~= true then
        return reportFileCutsceneStarted(record, false, "native file cutscene start refused")
    end
    record.startedAt = getTickCount()
    local fadedOk, faded = pcall(fadeFileCutscene, record.token, true, record.fadeIn, 0, 0, 0)
    if not fadedOk or faded ~= true then
        return reportFileCutsceneStarted(record, false, "native file cutscene fade-in refused")
    end
    reportFileCutsceneStarted(record, true)
    if record.skipPending and not applyFileCutsceneSkip(record) then return end
    record.playTimer = setTimer(function()
        if fileCutscenes[record.id] ~= record or record.finishedReported then return end
        if not hasFileCutsceneLease(record) then
            return reportFileCutsceneFinished(record, false, "native file cutscene lease lost during playback")
        end
        if record.allowLeaderSkip and localPlayer == record.leader and not record.skipReported then
            local queried, pressed = pcall(isFileCutsceneSkipInputPressed, record.token)
            if not queried then
                return reportFileCutsceneFinished(record, false, "native file cutscene skip query failed")
            end
            if pressed == true then
                record.skipReported = true
                triggerServerEvent("storyWorldRuntime:fileCutsceneSkipRequested", resourceRoot, record.id)
            end
        end
        local queried, finished = pcall(isFileCutsceneFinished, record.token)
        if not queried then
            return reportFileCutsceneFinished(record, false, "native file cutscene finish query failed")
        end
        if finished ~= true then return end
        killTimer(record.playTimer)
        record.playTimer = nil
        local fadedOk, faded = pcall(fadeFileCutscene, record.token, false, 0, 0, 0, 0)
        if not fadedOk or faded ~= true then
            return reportFileCutsceneFinished(record, false, "native file cutscene fade-out refused")
        end
        record.fadeTimer = setTimer(function()
            if fileCutscenes[record.id] ~= record or record.finishedReported then return end
            local fadeQueried, fading = pcall(isFileCutsceneFading, record.token)
            if not fadeQueried then
                return reportFileCutsceneFinished(record, false, "native file cutscene fade query failed")
            end
            if fading ~= true then
                killTimer(record.fadeTimer)
                record.fadeTimer = nil
                reportFileCutsceneFinished(record, true)
            end
        end, 50, 0)
    end, 50, 0)
end)

addEvent("storyWorldRuntime:skipFileCutscene", true)
addEventHandler("storyWorldRuntime:skipFileCutscene", resourceRoot, function(id)
    local record = fileCutscenes[tonumber(id)]
    if source == resourceRoot and record and hasFileCutsceneLease(record) then applyFileCutsceneSkip(record) end
end)

addEvent("storyWorldRuntime:releaseFileCutscene", true)
addEventHandler("storyWorldRuntime:releaseFileCutscene", resourceRoot, function(id)
    id = tonumber(id)
    if source ~= resourceRoot or not fileCutscenes[id] then return end
    local released = stopFileCutscene(id, false, true)
    local reason
    if not released then reason = "native file cutscene release refused" end
    triggerServerEvent("storyWorldRuntime:fileCutsceneReleased", resourceRoot, id, released, reason)
    if not released then ensureFileCutsceneReleaseRetry(fileCutscenes[id]) end
end)

addEvent("storyWorldRuntime:abortFileCutscene", true)
addEventHandler("storyWorldRuntime:abortFileCutscene", resourceRoot, function(id)
    id = tonumber(id)
    local record = fileCutscenes[id]
    if source ~= resourceRoot or not record then return end
    if stopFileCutscene(id, false, true) then
        triggerServerEvent("storyWorldRuntime:fileCutsceneReleased", resourceRoot, id, true)
    else
        ensureFileCutsceneReleaseRetry(record)
    end
end)

local function stopTeardown(id, restoreFade)
    local teardown = teardowns[id]
    if teardown and isTimer(teardown.timer) then killTimer(teardown.timer) end
    teardowns[id] = nil
    if restoreFade == true then fadeCamera(true, 0) end
end

local function stopVehiclePlacement(id)
    local record = vehiclePlacements[id]
    if not record then return end
    if isTimer(record.timer) then killTimer(record.timer) end
    if record.lease and type(releaseElementStreamingLease) == "function" then
        releaseElementStreamingLease(record.lease)
    end
    vehiclePlacements[id] = nil
end

local function prepareVehiclePlacement(id, vehicle)
    local record = vehiclePlacements[id]
    if record and record.vehicle == vehicle then
        if isTimer(record.timer) then killTimer(record.timer) end
        record.timer = nil
        return record
    end
    stopVehiclePlacement(id)
    if type(acquireElementStreamingLease) ~= "function" or
        type(releaseElementStreamingLease) ~= "function" then
        return false, "element streaming lease API unavailable"
    end
    local lease = isElement(vehicle) and acquireElementStreamingLease(vehicle)
    if not lease then return false, "vehicle streaming lease refused" end
    record = {vehicle = vehicle, lease = lease}
    vehiclePlacements[id] = record
    return record
end

local function stopOccupancy(id)
    local record = occupancies[id]
    if not record then return end
    if isTimer(record.timer) then killTimer(record.timer) end
    for _, lease in ipairs(record.leases or {}) do
        if type(releaseElementStreamingLease) == "function" then releaseElementStreamingLease(lease) end
    end
    occupancies[id] = nil
end

local function prepareOccupancy(id, assignments)
    local record = occupancies[id]
    if record then
        if isTimer(record.timer) then killTimer(record.timer) end
        record.timer = nil
        return record
    end
    if type(acquireElementStreamingLease) ~= "function" or
        type(releaseElementStreamingLease) ~= "function" then
        return false, "element streaming lease API unavailable"
    end
    record = {startedAt = getTickCount(), stableSamples = 0, leases = {}}
    local leased = {}
    for _, assignment in ipairs(assignments) do
        for _, element in ipairs({assignment.vehicle, assignment.ped}) do
            if element ~= localPlayer and not leased[element] then
                local lease = isElement(element) and acquireElementStreamingLease(element)
                if not lease then
                    stopOccupancy(id)
                    for _, acquired in ipairs(record.leases) do releaseElementStreamingLease(acquired) end
                    return false, "vehicle occupancy streaming lease refused"
                end
                leased[element] = true
                record.leases[#record.leases + 1] = lease
            end
        end
    end
    occupancies[id] = record
    return record
end

local function occupancyReady(assignments, requireSeats)
    for index, assignment in ipairs(assignments) do
        local ped, vehicle = assignment.ped, assignment.vehicle
        if not isElement(ped) then return false, ("assignment %d ped missing"):format(index) end
        if not isElement(vehicle) then return false, ("assignment %d vehicle missing"):format(index) end
        if not isElementStreamedIn(vehicle) then
            return false, ("assignment %d vehicle not streamed"):format(index)
        end
        if ped ~= localPlayer and not isElementStreamedIn(ped) then
            return false, ("assignment %d ped not streamed"):format(index)
        end
        if requireSeats and
            (getPedOccupiedVehicle(ped) ~= vehicle or getPedOccupiedVehicleSeat(ped) ~= assignment.seat) then
            return false, ("assignment %d seat %d not converged"):format(index, assignment.seat)
        end
    end
    return true
end

local function pollOccupancy(id, assignments, requireSeats, eventName)
    local record, preparationReason = prepareOccupancy(id, assignments)
    if not record then
        return triggerServerEvent(eventName, resourceRoot, id, false, preparationReason)
    end
    record.startedAt, record.stableSamples = getTickCount(), 0
    record.timer = setTimer(function()
        if occupancies[id] ~= record then return end
        local ready, reason = occupancyReady(assignments, requireSeats)
        if ready then
            record.stableSamples = record.stableSamples + 1
            if record.stableSamples >= 3 then
                if isTimer(record.timer) then killTimer(record.timer) end
                record.timer = nil
                if requireSeats then stopOccupancy(id) end
                return triggerServerEvent(eventName, resourceRoot, id, true)
            end
        else
            record.stableSamples = 0
            record.lastReason = reason
        end
        if getTickCount() - record.startedAt > 10000 then
            stopOccupancy(id)
            triggerServerEvent(eventName, resourceRoot, id, false, record.lastReason or
                                   (requireSeats and "vehicle seats did not converge" or
                                       "vehicle occupancy elements did not stream on the coordinator"))
        end
    end, 50, 0)
end

addEvent("storyWorldRuntime:prepareOccupancy", true)
addEventHandler("storyWorldRuntime:prepareOccupancy", resourceRoot, function(id, assignments)
    if type(assignments) ~= "table" or #assignments == 0 then
        return triggerServerEvent("storyWorldRuntime:occupancyPrepared", resourceRoot, id, false,
                                  "invalid vehicle occupancy assignments")
    end
    pollOccupancy(id, assignments, false, "storyWorldRuntime:occupancyPrepared")
end)

addEvent("storyWorldRuntime:verifyOccupancy", true)
addEventHandler("storyWorldRuntime:verifyOccupancy", resourceRoot, function(id, assignments)
    if type(assignments) ~= "table" or #assignments == 0 then
        return triggerServerEvent("storyWorldRuntime:occupancyVerified", resourceRoot, id, false,
                                  "invalid vehicle occupancy assignments")
    end
    pollOccupancy(id, assignments, true, "storyWorldRuntime:occupancyVerified")
end)

addEvent("storyWorldRuntime:cancelOccupancy", true)
addEventHandler("storyWorldRuntime:cancelOccupancy", resourceRoot, function(id)
    stopOccupancy(id)
end)

addEvent("storyWorldRuntime:measureVehicle", true)
addEventHandler("storyWorldRuntime:measureVehicle", resourceRoot, function(id, vehicle, model)
    local record, reason = prepareVehiclePlacement(id, vehicle)
    if not record then
        return triggerServerEvent("storyWorldRuntime:vehicleMeasured", resourceRoot, id, vehicle, false, reason)
    end
    local startedAt = getTickCount()
    record.timer = setTimer(function()
        if not isElement(vehicle) then
            stopVehiclePlacement(id)
            return triggerServerEvent("storyWorldRuntime:vehicleMeasured", resourceRoot, id, vehicle, false)
        end
        if isElementStreamedIn(vehicle) and isElementSyncer(vehicle) and getElementModel(vehicle) == tonumber(model) then
            local baseOffset = tonumber(getElementDistanceFromCentreOfMassToBaseOfModel(vehicle))
            if baseOffset and baseOffset > 0 then
                killTimer(record.timer)
                record.timer = nil
                return triggerServerEvent("storyWorldRuntime:vehicleMeasured", resourceRoot, id, vehicle, baseOffset)
            end
        elseif getTickCount() - startedAt > 10000 then
            stopVehiclePlacement(id)
            triggerServerEvent("storyWorldRuntime:vehicleMeasured", resourceRoot, id, vehicle, false)
        end
    end, 50, 0)
end)

addEvent("storyWorldRuntime:verifyVehicle", true)
addEventHandler("storyWorldRuntime:verifyVehicle", resourceRoot, function(id, vehicle, expectedZ, tolerance)
    expectedZ, tolerance = tonumber(expectedZ), tonumber(tolerance) or 0.03
    local record, reason = prepareVehiclePlacement(id, vehicle)
    if not record then
        return triggerServerEvent("storyWorldRuntime:vehicleVerified", resourceRoot, id, vehicle, false, reason)
    end
    local startedAt, stableSamples, lastReason = getTickCount(), 0, "verification not sampled"
    record.timer = setTimer(function()
        if not isElement(vehicle) then
            stopVehiclePlacement(id)
            return triggerServerEvent("storyWorldRuntime:vehicleVerified", resourceRoot, id, vehicle, false,
                                      "vehicle disappeared")
        end
        local _, _, z = getElementPosition(vehicle)
        local streamed, syncing = isElementStreamedIn(vehicle), isElementSyncer(vehicle)
        local delta = expectedZ and math.abs(z - expectedZ)
        if streamed and syncing and delta and delta <= tolerance then
            stableSamples = stableSamples + 1
            if stableSamples >= 3 then
                stopVehiclePlacement(id)
                return triggerServerEvent("storyWorldRuntime:vehicleVerified", resourceRoot, id, vehicle, true, nil,
                                          z)
            end
        else
            stableSamples = 0
            if not streamed then
                lastReason = "vehicle not streamed under placement lease"
            elseif not syncing then
                lastReason = "placement owner is not the local syncer"
            elseif not delta then
                lastReason = "invalid expected centre Z"
            else
                lastReason = ("centre Z delta %.5f exceeds %.5f"):format(delta, tolerance)
            end
        end
        if getTickCount() - startedAt > 10000 then
            stopVehiclePlacement(id)
            triggerServerEvent("storyWorldRuntime:vehicleVerified", resourceRoot, id, vehicle, false,
                               "vehicle did not stabilize at converted SCM Z: " .. lastReason, z)
        end
    end, 50, 0)
end)

addEvent("storyWorldRuntime:cancelVehiclePlacement", true)
addEventHandler("storyWorldRuntime:cancelVehiclePlacement", resourceRoot, function(id)
    stopVehiclePlacement(id)
end)

addEvent("storyWorldRuntime:prepareTeardown", true)
addEventHandler("storyWorldRuntime:prepareTeardown", resourceRoot, function(id, elements, fadeOut)
    if type(elements) ~= "table" then return end
    fadeOut = math.max(0, math.min(3, tonumber(fadeOut) or 0))
    stopTeardown(id, false)
    local teardown = {elements = elements, armedAt = getTickCount(), fadeOwned = fadeOut > 0}
    teardowns[id] = teardown
    if fadeOut > 0 then fadeCamera(false, fadeOut) end
    teardown.timer = setTimer(function()
        teardown.timer = nil
        if teardowns[id] == teardown then triggerServerEvent("storyWorldRuntime:teardownArmed", resourceRoot, id) end
    end, math.max(50, math.ceil(fadeOut * 1000)), 1)
end)

addEvent("storyWorldRuntime:commitTeardown", true)
addEventHandler("storyWorldRuntime:commitTeardown", resourceRoot, function(id)
    local teardown = teardowns[id]
    if not teardown then return end
    teardown.timer = setTimer(function()
        for _, element in ipairs(teardown.elements) do
            if isElement(element) then
                if getTickCount() - teardown.armedAt <= 10000 then return end
                stopTeardown(id, false)
                return triggerServerEvent("storyWorldRuntime:teardownGone", resourceRoot, id, false,
                                          "mission element remained client-visible")
            end
        end
        stopTeardown(id, false)
        triggerServerEvent("storyWorldRuntime:teardownGone", resourceRoot, id, true)
    end, 50, 0)
end)

addEvent("storyWorldRuntime:cancelTeardown", true)
addEventHandler("storyWorldRuntime:cancelTeardown", resourceRoot, function(id, restoreFade)
    stopTeardown(id, restoreFade == true)
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    local relocationIds = {}
    for id in pairs(vehicleRelocations) do relocationIds[#relocationIds + 1] = id end
    for _, id in ipairs(relocationIds) do stopVehicleRelocation(id) end
    local cutsceneIds = {}
    for id in pairs(fileCutscenes) do cutsceneIds[#cutsceneIds + 1] = id end
    for _, id in ipairs(cutsceneIds) do stopFileCutscene(id, false) end
    local teardownIds = {}
    for id in pairs(teardowns) do teardownIds[#teardownIds + 1] = id end
    for _, id in ipairs(teardownIds) do stopTeardown(id, true) end
    -- A committed teardown deliberately forgets its record while the caller
    -- owns the following black-frame transition. If this runtime itself stops
    -- in that gap, never leave GTA's global fade stranded.
    fadeCamera(true, 0)
    local ids = {}
    for id in pairs(occupancies) do ids[#ids + 1] = id end
    for _, id in ipairs(ids) do stopOccupancy(id) end
    ids = {}
    for id in pairs(vehiclePlacements) do ids[#ids + 1] = id end
    for _, id in ipairs(ids) do stopVehiclePlacement(id) end
end)
