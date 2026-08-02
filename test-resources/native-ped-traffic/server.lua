local config = {
    globalCap = 24,
    pedPoolSoftLimit = 90,
    targetNearPlayer = 12,
    nearRadius = 120,
    despawnRadius = 180,
    cellSize = 64,
    maxPerCell = 4,
    minSeparation = 10,
    requestInterval = 500,
    requestTimeout = 2500,
    handoffMargin = 20,
    handoffHold = 3000,
    handoffTimeout = 2000,
    corpseLifetime = 8000,
}

local enabled = false
local debugEnabled = false
local nextRequestId = 0
local nextPedId = 0
local nextAirTestId = 0
local nextClimbTestId = 0
local requestCursor = 0
local pendingRequests = {}
local trafficPeds = {}
local testVehicles = {}
local stats = {
    requests = 0,
    candidateMisses = 0,
    rejected = 0,
    spawned = 0,
    handoffs = 0,
    despawned = 0,
    missReasons = {},
    rejectionReasons = {},
}

local function log(message, force)
    if debugEnabled or force then
        outputDebugString("[ped-traffic][server] " .. message)
    end
end

local function countReason(bucket, reason)
    reason = tostring(reason or "unknown")
    bucket[reason] = (bucket[reason] or 0) + 1
end

local function formatReasons(bucket)
    local values = {}
    for reason, count in pairs(bucket) do
        values[#values + 1] = reason .. ":" .. tostring(count)
    end
    table.sort(values)
    return #values > 0 and table.concat(values, ",") or "none"
end

local function isFiniteNumber(value)
    return type(value) == "number" and value == value and value > -1000000 and value < 1000000
end

local function isEligiblePlayer(player)
    return isElement(player) and getElementType(player) == "player" and not isPedDead(player) and
        getElementDimension(player) == 0 and getElementInterior(player) == 0
end

local function squaredDistance(x1, y1, z1, x2, y2, z2)
    local dx, dy, dz = x1 - x2, y1 - y2, z1 - z2
    return dx * dx + dy * dy + dz * dz
end

local function getEligiblePlayers()
    local players = {}
    for _, player in ipairs(getElementsByType("player")) do
        if isEligiblePlayer(player) then
            players[#players + 1] = player
        end
    end
    return players
end

local function findClosestPlayer(x, y, z, maxDistance, excludedPlayer)
    local closest, closestDistanceSquared
    local limitSquared = maxDistance * maxDistance
    for _, player in ipairs(getEligiblePlayers()) do
        if player ~= excludedPlayer then
            local px, py, pz = getElementPosition(player)
            local distanceSquared = squaredDistance(x, y, z, px, py, pz)
            if distanceSquared <= limitSquared and (not closestDistanceSquared or distanceSquared < closestDistanceSquared) then
                closest = player
                closestDistanceSquared = distanceSquared
            end
        end
    end
    return closest, closestDistanceSquared
end

local function cellForPosition(x, y)
    return math.floor(x / config.cellSize), math.floor(y / config.cellSize)
end

local function countPedsInCell(cellX, cellY)
    local count = 0
    for ped in pairs(trafficPeds) do
        if isElement(ped) then
            local x, y = getElementPosition(ped)
            local pedCellX, pedCellY = cellForPosition(x, y)
            if pedCellX == cellX and pedCellY == cellY then
                count = count + 1
            end
        end
    end
    return count
end

local function countPedsNear(x, y, z, radius)
    local count = 0
    local radiusSquared = radius * radius
    for ped in pairs(trafficPeds) do
        if isElement(ped) then
            local px, py, pz = getElementPosition(ped)
            if squaredDistance(x, y, z, px, py, pz) <= radiusSquared then
                count = count + 1
            end
        end
    end
    return count
end

local function getTrafficPedCount()
    local count = 0
    for ped in pairs(trafficPeds) do
        if isElement(ped) then count = count + 1 end
    end
    return count
end

local function hasNearbyTrafficPed(x, y, z)
    local minimumSquared = config.minSeparation * config.minSeparation
    for ped in pairs(trafficPeds) do
        if isElement(ped) then
            local px, py, pz = getElementPosition(ped)
            if squaredDistance(x, y, z, px, py, pz) < minimumSquared then
                return true
            end
        end
    end
    return false
end

local function hasOtherPlayerTooClose(x, y, z, proposingPlayer)
    local minimumSquared = 25 * 25
    for _, player in ipairs(getEligiblePlayers()) do
        if player ~= proposingPlayer then
            local px, py, pz = getElementPosition(player)
            if squaredDistance(x, y, z, px, py, pz) < minimumSquared then
                return true
            end
        end
    end
    return false
end

local function stopClimbTest(record, reason)
    local test = record and record.climbTest
    if not test then
        return
    end
    if isTimer(test.prepareTimer) then
        killTimer(test.prepareTimer)
    end
    if isElement(record.ped) then
        setElementFrozen(record.ped, false)
    end
    triggerClientEvent(root, "pedTraffic:climbTestStop", resourceRoot, record.ped, record.epoch, test.nonce, reason)
    if isElement(test.obstacle) then
        destroyElement(test.obstacle)
    end
    record.climbTest = nil
end

local function removeRecord(record, reason)
    if not record or record.removing then
        return
    end
    record.removing = true
    if record.airTest then
        triggerClientEvent(root, "pedTraffic:airTestStop", resourceRoot, record.ped, record.epoch, record.airTest.nonce, reason)
        record.airTest = nil
    end
    stopClimbTest(record, reason)
    trafficPeds[record.ped] = nil
    if isElement(record.ped) then
        if isElement(record.owner) then
            triggerClientEvent(record.owner, "pedTraffic:stop", resourceRoot, record.ped, record.epoch, reason)
        end
        destroyElement(record.ped)
    end
    stats.despawned = stats.despawned + 1
    log(("despawn id=%d reason=%s active=%d"):format(record.id, tostring(reason), getTrafficPedCount()))
end

local function sendAssignment(record, reason)
    if not record or record.removing or record.state ~= "assigning" or not isElement(record.owner) then
        return false
    end
    record.assignmentLastSent = getTickCount()
    local resumePhysical = (record.airTest and record.airTest.handoffTriggered == true) or
        (record.climbTest and record.climbTest.handoffTriggered == true)
    return triggerClientEvent(record.owner, "pedTraffic:assign", resourceRoot, record.ped, record.epoch, record.direction, reason,
                              resumePhysical)
end

local function hasValidGunAimContext(player, ped, requireSyncedControl)
    if not isElement(player) or not isElement(ped) or getPedTarget(player) ~= ped then
        return false
    end

    local weapon = getPedWeapon(player)
    if weapon < 22 or weapon > 39 or (requireSyncedControl and not getControlState(player, "aim_weapon")) then
        return false
    end

    local px, py, pz = getElementPosition(ped)
    local ax, ay, az = getElementPosition(player)
    return getElementDimension(player) == getElementDimension(ped) and getElementInterior(player) == getElementInterior(ped) and
        squaredDistance(px, py, pz, ax, ay, az) <= 250 * 250
end

local function bridgeGunAim(record, aimingPlayer)
    if not record or record.removing or record.state ~= "active" or not isElement(record.owner) or not isElement(aimingPlayer) or
        record.owner == aimingPlayer then
        return false
    end

    log(("gun-aim-bridge id=%d shooter=%s owner=%s"):format(record.id, getPlayerName(aimingPlayer), getPlayerName(record.owner)))
    return triggerClientEvent(record.owner, "pedTraffic:gunAimedAt", resourceRoot, record.ped, aimingPlayer)
end

local function bridgeDamageResponse(record, attackingPlayer, weapon, bodypart)
    if not record or record.removing or record.state ~= "active" or not isElement(record.owner) or not isElement(attackingPlayer) or
        record.owner == attackingPlayer then
        return false
    end

    log(("damage-bridge id=%d attacker=%s owner=%s weapon=%d bodypart=%d"):format(
            record.id, getPlayerName(attackingPlayer), getPlayerName(record.owner), weapon, bodypart))
    return triggerClientEvent(record.owner, "pedTraffic:damageResponse", resourceRoot, record.ped, attackingPlayer, weapon, bodypart)
end

local function assignOwner(record, owner, reason)
    if not record or record.removing or not isElement(record.ped) or not isEligiblePlayer(owner) then
        return false
    end

    local isHandoff = record.epoch > 0
    record.owner = owner
    record.pendingOwner = nil
    record.state = "assigning"
    record.epoch = record.epoch + 1
    record.handoffCandidate = nil
    record.handoffCandidateSince = nil
    record.handoffDeadline = nil
    record.assignmentStartedAt = getTickCount()
    record.assignmentLastSent = 0

    if not setElementSyncer(record.ped, owner, true) then
        removeRecord(record, "syncer-refused")
        return false
    end

    -- Count successful ownership epochs here so disconnect handoffs, which skip
    -- the revoke phase, are measured consistently with ordinary handoffs.
    if isHandoff then
        stats.handoffs = stats.handoffs + 1
    end

    if record.airTest then
        record.airTest.epoch = record.epoch
        triggerClientEvent(root, "pedTraffic:airTestWatch", resourceRoot, record.ped, record.epoch, record.airTest.nonce,
                           record.airTest.forceHandoff)
    end
    if record.climbTest then
        record.climbTest.epoch = record.epoch
        triggerClientEvent(root, "pedTraffic:climbTestWatch", resourceRoot, record.ped, record.epoch, record.climbTest.nonce,
                           record.climbTest.forceHandoff)
    end
    sendAssignment(record, reason)
    log(("assign id=%d epoch=%d owner=%s reason=%s"):format(record.id, record.epoch, getPlayerName(owner), tostring(reason)))
    return true
end

local function findClosestActiveTrafficPed(player, maxDistance)
    if not isEligiblePlayer(player) then
        return false
    end

    local x, y, z = getElementPosition(player)
    local closestRecord, closestDistanceSquared
    local maximumDistanceSquared = maxDistance * maxDistance
    for _, record in pairs(trafficPeds) do
        if not record.removing and record.state == "active" and isElement(record.ped) and not isPedDead(record.ped) and
            getElementDimension(record.ped) == getElementDimension(player) and getElementInterior(record.ped) == getElementInterior(player) then
            local px, py, pz = getElementPosition(record.ped)
            local distanceSquared = squaredDistance(x, y, z, px, py, pz)
            if distanceSquared <= maximumDistanceSquared and (not closestDistanceSquared or distanceSquared < closestDistanceSquared) then
                closestRecord = record
                closestDistanceSquared = distanceSquared
            end
        end
    end
    return closestRecord
end

local function startAirTest(player, forceHandoff)
    if not enabled or not isEligiblePlayer(player) then
        outputChatBox("Run /pedtraffic on before starting the airborne test", player, 255, 160, 80)
        return false
    end

    local record = findClosestActiveTrafficPed(player, 30)
    if not record then
        outputChatBox("No active traffic ped within 30 metres", player, 255, 160, 80)
        return false
    end
    if record.airTest then
        outputChatBox("That traffic ped already has an airborne test in progress", player, 255, 160, 80)
        return false
    end
    if record.climbTest then
        outputChatBox("That traffic ped already has a climb test in progress", player, 255, 160, 80)
        return false
    end
    if forceHandoff and #getEligiblePlayers() < 2 then
        outputChatBox("The airborne handoff test requires two connected players", player, 255, 160, 80)
        return false
    end

    nextAirTestId = nextAirTestId + 1
    record.airTest = {
        nonce = nextAirTestId,
        epoch = record.epoch,
        requester = player,
        forceHandoff = forceHandoff == true,
        startedAt = getTickCount(),
    }
    triggerClientEvent(root, "pedTraffic:airTestWatch", resourceRoot, record.ped, record.epoch, record.airTest.nonce,
                       record.airTest.forceHandoff)
    triggerClientEvent(record.owner, "pedTraffic:airTest", resourceRoot, record.ped, record.epoch, record.airTest.nonce)
    log(("airtest-start id=%d epoch=%d nonce=%d owner=%s handoff=%s"):format(
            record.id, record.epoch, record.airTest.nonce, getPlayerName(record.owner), tostring(record.airTest.forceHandoff)), true)
    outputChatBox(("Native airborne test started on traffic ped %d%s"):format(
                      record.id, record.airTest.forceHandoff and " with forced handoff" or ""), player, 120, 220, 255)
    return true
end

local function startClimbTest(player, forceHandoff)
    if not enabled or not isEligiblePlayer(player) then
        outputChatBox("Run /pedtraffic on before starting the climb test", player, 255, 160, 80)
        return false
    end

    local record = findClosestActiveTrafficPed(player, 30)
    if not record then
        outputChatBox("No active traffic ped within 30 metres", player, 255, 160, 80)
        return false
    end
    if record.airTest or record.climbTest then
        outputChatBox("That traffic ped already has a physical test in progress", player, 255, 160, 80)
        return false
    end
    if forceHandoff and #getEligiblePlayers() < 2 then
        outputChatBox("The climb handoff test requires two connected players", player, 255, 160, 80)
        return false
    end

    local playerX, playerY, playerZ = getElementPosition(player)
    local _, _, heading = getElementRotation(player)
    local radians = math.rad(heading)
    local forwardX, forwardY = -math.sin(radians), math.cos(radians)
    local pedX, pedY = playerX + forwardX * 4.0, playerY + forwardY * 4.0
    local obstacleX, obstacleY = pedX + forwardX * 1.05, pedY + forwardY * 1.05
    local obstacle = createObject(1422, obstacleX, obstacleY, playerZ - 0.05, 0, 0, heading)
    if not obstacle then
        outputChatBox("Could not create the climb-test obstacle", player, 255, 80, 80)
        return false
    end

    setElementDimension(obstacle, getElementDimension(player))
    setElementInterior(obstacle, getElementInterior(player))
    setElementFrozen(obstacle, true)
    if type(setObjectBreakable) == "function" then
        setObjectBreakable(obstacle, false)
    end
    setElementData(obstacle, "neon:pedTrafficClimbTestObstacle", true, false)

    nextClimbTestId = nextClimbTestId + 1
    local test = {
        nonce = nextClimbTestId,
        epoch = record.epoch,
        requester = player,
        forceHandoff = forceHandoff == true,
        startedAt = getTickCount(),
        obstacle = obstacle,
    }
    record.climbTest = test

    -- Freeze only during the short placement/streaming window. The owner then
    -- starts GTA's real jump task against the shared collision object.
    local placed = setElementFrozen(record.ped, true) and setElementPosition(record.ped, pedX, pedY, playerZ) and
        setElementRotation(record.ped, 0, 0, heading) and setElementVelocity(record.ped, 0, 0, 0)
    if not placed then
        stopClimbTest(record, "placement-refused")
        outputChatBox("Could not prepare the climb-test ped", player, 255, 80, 80)
        return false
    end
    triggerClientEvent(root, "pedTraffic:climbTestWatch", resourceRoot, record.ped, record.epoch, test.nonce, test.forceHandoff)

    test.prepareTimer = setTimer(function()
        if record.removing or record.climbTest ~= test or not isElement(record.ped) or not isElement(test.obstacle) or
            not isElement(record.owner) then
            stopClimbTest(record, "preparation-invalidated")
            return
        end
        setElementFrozen(record.ped, false)
        triggerClientEvent(record.owner, "pedTraffic:climbTest", resourceRoot, record.ped, record.epoch, test.nonce)
    end, 500, 1)

    log(("climbtest-start id=%d epoch=%d nonce=%d owner=%s obstacle=%d pos=(%.2f,%.2f,%.2f) heading=%.1f handoff=%s"):format(
            record.id, record.epoch, test.nonce, getPlayerName(record.owner), getElementModel(obstacle), obstacleX, obstacleY,
            playerZ - 0.05, heading, tostring(test.forceHandoff)), true)
    outputChatBox(("Native climb test started on traffic ped %d%s"):format(
                      record.id, test.forceHandoff and " with forced handoff" or ""), player, 120, 220, 255)
    return true
end

local function finishHandoff(record, reason)
    if not record or record.removing then
        return
    end
    local owner = record.pendingOwner
    if not isEligiblePlayer(owner) then
        local x, y, z = getElementPosition(record.ped)
        owner = findClosestPlayer(x, y, z, config.despawnRadius)
    end
    if not owner then
        removeRecord(record, "handoff-no-owner")
        return
    end
    assignOwner(record, owner, reason)
end

local function beginHandoff(record, newOwner, reason)
    if not record or record.removing or record.state == "revoking" or newOwner == record.owner then
        return
    end

    record.pendingOwner = newOwner
    record.state = "revoking"
    record.handoffDeadline = getTickCount() + config.handoffTimeout

    if isElement(record.owner) then
        triggerClientEvent(record.owner, "pedTraffic:revoke", resourceRoot, record.ped, record.epoch, reason)
        log(("revoke id=%d epoch=%d old=%s new=%s reason=%s"):format(record.id, record.epoch, getPlayerName(record.owner),
                                                                    getPlayerName(newOwner), tostring(reason)))
    else
        finishHandoff(record, "owner-departed")
    end
end

local function validateCandidate(player, candidate)
    if type(candidate) ~= "table" or not isFiniteNumber(candidate.x) or not isFiniteNumber(candidate.y) or
        not isFiniteNumber(candidate.z) or not isFiniteNumber(candidate.model) or not isFiniteNumber(candidate.pedType) or
        not isFiniteNumber(candidate.direction) then
        return false, "shape"
    end

    local model = math.floor(candidate.model)
    local pedType = math.floor(candidate.pedType)
    local direction = math.floor(candidate.direction)
    if model < 7 or model > 288 or (pedType ~= 4 and pedType ~= 5) or direction < 0 or direction > 7 then
        return false, "civilian-contract"
    end

    local playerX, playerY, playerZ = getElementPosition(player)
    local distanceSquared = squaredDistance(candidate.x, candidate.y, candidate.z, playerX, playerY, playerZ)
    if distanceSquared < 10 * 10 or distanceSquared > 95 * 95 or math.abs(candidate.z - playerZ) > 35 then
        return false, "distance"
    end

    local cellX, cellY = cellForPosition(candidate.x, candidate.y)
    if countPedsInCell(cellX, cellY) >= config.maxPerCell then
        return false, "cell-full"
    end
    if hasNearbyTrafficPed(candidate.x, candidate.y, candidate.z) then
        return false, "separation"
    end
    if hasOtherPlayerTooClose(candidate.x, candidate.y, candidate.z, player) then
        return false, "other-player-too-close"
    end
    return true, model, direction
end

local function spawnCandidate(player, candidate)
    if not enabled or not isEligiblePlayer(player) or getTrafficPedCount() >= config.globalCap or
        #getElementsByType("ped") >= config.pedPoolSoftLimit then
        return false, "runtime-unavailable"
    end

    local valid, modelOrReason, direction = validateCandidate(player, candidate)
    if not valid then
        return false, modelOrReason
    end

    local owner = findClosestPlayer(candidate.x, candidate.y, candidate.z, config.despawnRadius)
    if not owner then
        return false, "no-owner"
    end

    local ped = createPed(modelOrReason, candidate.x, candidate.y, candidate.z, direction * 45)
    if not ped then
        return false, "create-ped"
    end

    nextPedId = nextPedId + 1
    local record = {
        id = nextPedId,
        ped = ped,
        owner = nil,
        epoch = 0,
        direction = direction,
        state = "created",
        createdAt = getTickCount(),
    }
    trafficPeds[ped] = record
    setElementDimension(ped, 0)
    setElementInterior(ped, 0)
    -- These two small immutable values let every observer correlate bounded
    -- telemetry for the same network ped without synchronizing AI state.
    setElementData(ped, "neon:ambientPedTraffic", true)
    setElementData(ped, "neon:ambientPedTrafficId", record.id)

    -- Persist this through the server custom-data lane so observers use the
    -- same native walk style as the machine running WanderStandard.
    if not setPedUseNativeWalkingStyle(ped, true) then
        removeRecord(record, "native-walking-style-refused")
        return false, "native-walking-style"
    end

    if not assignOwner(record, owner, "spawn") then
        return false, "assign-owner"
    end
    stats.spawned = stats.spawned + 1
    log(("spawn id=%d model=%d pos=%.1f,%.1f,%.1f owner=%s"):format(record.id, modelOrReason, candidate.x, candidate.y,
                                                                    candidate.z, getPlayerName(owner)))
    return true
end

local function clearTraffic(reason)
    local records = {}
    for _, record in pairs(trafficPeds) do
        records[#records + 1] = record
    end
    for _, record in ipairs(records) do
        removeRecord(record, reason)
    end
    pendingRequests = {}
end

local function removeTestVehicle(player)
    local vehicle = testVehicles[player]
    testVehicles[player] = nil
    if isElement(vehicle) then
        destroyElement(vehicle)
    end
end

local function clearTestVehicles()
    local players = {}
    for player in pairs(testVehicles) do
        players[#players + 1] = player
    end
    for _, player in ipairs(players) do
        removeTestVehicle(player)
    end
end

local function createTestVehicle(player, requestedModel)
    if not enabled or not isEligiblePlayer(player) then
        if isElement(player) then
            outputChatBox("Run /pedtraffic on before creating the test vehicle", player, 255, 160, 80)
        end
        return false
    end

    local model = math.floor(tonumber(requestedModel) or 560)
    if model < 400 or model > 611 then
        outputChatBox("Usage: /pedtraffic vehicle [model 400..611]", player, 255, 160, 80)
        return false
    end

    removeTestVehicle(player)
    local matrix = getElementMatrix(player)
    local x = matrix[4][1] + matrix[1][1] * 4
    local y = matrix[4][2] + matrix[1][2] * 4
    local z = matrix[4][3] + 0.5
    local _, _, rotation = getElementRotation(player)
    local vehicle = createVehicle(model, x, y, z, 0, 0, rotation)
    if not vehicle then
        outputChatBox("Could not create the ped-traffic test vehicle", player, 255, 80, 80)
        return false
    end

    testVehicles[player] = vehicle
    setElementDimension(vehicle, getElementDimension(player))
    setElementInterior(vehicle, getElementInterior(player))
    setElementData(vehicle, "neon:pedTrafficTestVehicle", true, false)
    warpPedIntoVehicle(player, vehicle)
    outputChatBox(("Ped traffic collision test vehicle: model %d"):format(model), player, 120, 220, 255)
    return true
end

local function setEnabled(value, actor)
    value = value == true
    if enabled == value then
        if not value then
            -- Keep `off` idempotent so an interrupted test cannot leave a
            -- resource-owned vehicle behind even if traffic was already off.
            clearTraffic("disabled")
            clearTestVehicles()
        end
        return
    end
    enabled = value
    if not enabled then
        clearTraffic("disabled")
        clearTestVehicles()
    end
    triggerClientEvent(root, "pedTraffic:setEnabled", resourceRoot, enabled, debugEnabled)
    log(("enabled=%s actor=%s"):format(tostring(enabled), isElement(actor) and getPlayerName(actor) or "console"), true)
end

addEvent("pedTraffic:candidate", true)
addEventHandler("pedTraffic:candidate", resourceRoot, function(requestId, candidate, elapsedMs, missReason)
    local player = client
    local request = pendingRequests[player]
    pendingRequests[player] = nil
    if not request or request.id ~= requestId or getTickCount() - request.issuedAt > config.requestTimeout or not isEligiblePlayer(player) then
        stats.rejected = stats.rejected + 1
        return
    end

    if candidate == false then
        stats.candidateMisses = stats.candidateMisses + 1
        countReason(stats.missReasons, missReason)
        return
    end
    if getTrafficPedCount() >= config.globalCap then
        stats.rejected = stats.rejected + 1
        return
    end

    local created, reason = spawnCandidate(player, candidate)
    if not created then
        stats.rejected = stats.rejected + 1
        countReason(stats.rejectionReasons, reason)
    end
end)

addEvent("pedTraffic:ready", true)
addEventHandler("pedTraffic:ready", resourceRoot, function()
    triggerClientEvent(client, "pedTraffic:setEnabled", resourceRoot, enabled, debugEnabled)
end)

addEvent("pedTraffic:evidence", true)
addEventHandler("pedTraffic:evidence", resourceRoot, function(ped, epoch, evidence, data)
    local record = trafficPeds[ped]
    if not record or record.removing or record.epoch ~= epoch or client ~= record.owner then
        return
    end

    if evidence == "accepted" and record.state == "assigning" then
        record.state = "active"
        record.acceptedAt = getTickCount()
        log(("accepted id=%d epoch=%d owner=%s"):format(record.id, epoch, getPlayerName(client)))
        for _, player in ipairs(getEligiblePlayers()) do
            -- Reconstruct a still-active threat after an owner handoff, but do
            -- not confuse MTA's permanent shot raycast with actual aiming.
            if hasValidGunAimContext(player, record.ped, true) then
                bridgeGunAim(record, player)
            end
        end
    elseif evidence == "released" and record.state == "revoking" then
        finishHandoff(record, "release-ack")
    elseif evidence == "airtest-phase" and record.airTest and type(data) == "table" and
        tonumber(data.nonce) == record.airTest.nonce then
        local phase = tostring(data.phase or "unknown")
        local detail = type(data.reason) == "string" and (" reason=" .. data.reason) or ""
        log(("airtest-phase id=%d epoch=%d nonce=%d owner=%s phase=%s%s"):format(
                record.id, epoch, record.airTest.nonce, getPlayerName(client), phase, detail), true)
        if record.state == "revoking" and record.airTest.handoffTriggered then
            log(("airtest-phase-ignored id=%d epoch=%d nonce=%d phase=%s reason=handoff-in-progress"):format(
                    record.id, epoch, record.airTest.nonce, phase))
        elseif record.airTest.forceHandoff and not record.airTest.handoffTriggered and phase == "in_air" then
            local x, y, z = getElementPosition(record.ped)
            local newOwner = findClosestPlayer(x, y, z, config.despawnRadius, record.owner)
            if newOwner then
                record.airTest.handoffTriggered = true
                beginHandoff(record, newOwner, "airtest-in-air")
            else
                triggerClientEvent(root, "pedTraffic:airTestStop", resourceRoot, record.ped, record.epoch, record.airTest.nonce, "handoff-no-owner")
                record.airTest = nil
            end
        elseif phase == "complete" or phase == "failed" then
            triggerClientEvent(root, "pedTraffic:airTestStop", resourceRoot, record.ped, record.epoch, record.airTest.nonce, phase)
            record.airTest = nil
        end
    elseif evidence == "climbtest-phase" and record.climbTest and type(data) == "table" and
        tonumber(data.nonce) == record.climbTest.nonce then
        local phase = tostring(data.phase or "unknown")
        local detail = type(data.reason) == "string" and (" reason=" .. data.reason) or ""
        log(("climbtest-phase id=%d epoch=%d nonce=%d owner=%s phase=%s%s"):format(
                record.id, epoch, record.climbTest.nonce, getPlayerName(client), phase, detail), true)
        if record.state == "revoking" and record.climbTest.handoffTriggered then
            log(("climbtest-phase-ignored id=%d epoch=%d nonce=%d phase=%s reason=handoff-in-progress"):format(
                    record.id, epoch, record.climbTest.nonce, phase))
        elseif record.climbTest.forceHandoff and not record.climbTest.handoffTriggered and phase == "climb" then
            local x, y, z = getElementPosition(record.ped)
            local newOwner = findClosestPlayer(x, y, z, config.despawnRadius, record.owner)
            if newOwner then
                record.climbTest.handoffTriggered = true
                beginHandoff(record, newOwner, "climbtest-climb")
            else
                stopClimbTest(record, "handoff-no-owner")
            end
        elseif phase == "complete" or phase == "failed" then
            stopClimbTest(record, phase)
        end
    elseif evidence == "failure" then
        log(("client-failure id=%d epoch=%d owner=%s reason=%s"):format(record.id, epoch, getPlayerName(client),
                                                                        type(data) == "table" and tostring(data.reason) or "unknown"), true)
        removeRecord(record, "client-failure")
    end
end)

addEvent("pedTraffic:gunAimObserved", true)
addEventHandler("pedTraffic:gunAimObserved", resourceRoot, function(ped)
    local record = trafficPeds[ped]
    -- The client owns its input transition. The server still requires the
    -- synchronized target ray, firearm, world context and bounded distance.
    if not record or not hasValidGunAimContext(client, ped, false) then
        return
    end
    bridgeGunAim(record, client)
end)

addEvent("pedTraffic:damageObserved", true)
addEventHandler("pedTraffic:damageObserved", resourceRoot, function(ped, weapon, bodypart)
    local record = trafficPeds[ped]
    weapon = tonumber(weapon)
    bodypart = tonumber(bodypart)
    if not record or not isElement(client) or not weapon or not bodypart or weapon < 0 or weapon > 54 or
        (bodypart ~= 0 and (bodypart < 3 or bodypart > 9)) then
        return
    end

    local px, py, pz = getElementPosition(ped)
    local ax, ay, az = getElementPosition(client)
    if getElementDimension(client) ~= getElementDimension(ped) or getElementInterior(client) ~= getElementInterior(ped) or
        squaredDistance(px, py, pz, ax, ay, az) > 250 * 250 then
        return
    end

    bridgeDamageResponse(record, client, math.floor(weapon), math.floor(bodypart))
end)

addEventHandler("onPedWasted", root, function()
    local record = trafficPeds[source]
    if not record then
        return
    end
    local expectedPed = source
    setTimer(function()
        local current = trafficPeds[expectedPed]
        if current then
            removeRecord(current, "corpse-timeout")
        end
    end, config.corpseLifetime, 1)
end)

addEventHandler("onElementDestroy", root, function()
    local record = trafficPeds[source]
    if record and not record.removing then
        if record.airTest then
            triggerClientEvent(root, "pedTraffic:airTestStop", resourceRoot, record.ped, record.epoch, record.airTest.nonce,
                               "ped-destroyed")
            record.airTest = nil
        end
        stopClimbTest(record, "ped-destroyed")
    end
    trafficPeds[source] = nil
end)

addEventHandler("onPlayerQuit", root, function()
    pendingRequests[source] = nil
    removeTestVehicle(source)
    for _, record in pairs(trafficPeds) do
        if record.owner == source and not record.removing then
            local x, y, z = getElementPosition(record.ped)
            local newOwner = findClosestPlayer(x, y, z, config.despawnRadius, source)
            if newOwner then
                record.owner = nil
                record.pendingOwner = newOwner
                finishHandoff(record, "owner-quit")
            else
                removeRecord(record, "owner-quit-no-fallback")
            end
        end
    end
end)

setTimer(function()
    if not enabled then
        return
    end

    local players = getEligiblePlayers()
    if #players == 0 then
        clearTraffic("no-eligible-player")
        return
    end

    requestCursor = requestCursor % #players + 1
    for offset = 0, #players - 1 do
        local player = players[(requestCursor + offset - 1) % #players + 1]
        local x, y, z = getElementPosition(player)
        local request = pendingRequests[player]
        if request and getTickCount() - request.issuedAt > config.requestTimeout then
            pendingRequests[player] = nil
            request = nil
        end
        if not request and countPedsNear(x, y, z, config.nearRadius) < config.targetNearPlayer and
            getTrafficPedCount() < config.globalCap and #getElementsByType("ped") < config.pedPoolSoftLimit then
            nextRequestId = nextRequestId + 1
            pendingRequests[player] = {id = nextRequestId, issuedAt = getTickCount()}
            stats.requests = stats.requests + 1
            triggerClientEvent(player, "pedTraffic:candidateRequest", resourceRoot, nextRequestId)
            break
        end
    end
end, config.requestInterval, 0)

setTimer(function()
    if not enabled then
        return
    end

    local now = getTickCount()
    local records = {}
    for _, record in pairs(trafficPeds) do
        records[#records + 1] = record
    end
    for _, record in ipairs(records) do
        if not record.removing and isElement(record.ped) then
            if record.state == "revoking" then
                if now >= (record.handoffDeadline or 0) then
                    finishHandoff(record, "release-timeout")
                end
            elseif record.state == "assigning" then
                if now - (record.assignmentStartedAt or now) >= 10000 then
                    removeRecord(record, "assignment-timeout")
                elseif now - (record.assignmentLastSent or 0) >= 1000 then
                    sendAssignment(record, "assignment-retry")
                end
            elseif record.airTest and now - record.airTest.startedAt >= 8000 then
                log(("airtest-timeout id=%d epoch=%d nonce=%d"):format(record.id, record.epoch, record.airTest.nonce), true)
                triggerClientEvent(root, "pedTraffic:airTestStop", resourceRoot, record.ped, record.epoch, record.airTest.nonce, "timeout")
                record.airTest = nil
            elseif record.climbTest and not isElement(record.climbTest.obstacle) then
                log(("climbtest-failed id=%d epoch=%d nonce=%d reason=obstacle-lost"):format(
                        record.id, record.epoch, record.climbTest.nonce), true)
                stopClimbTest(record, "obstacle-lost")
            elseif record.climbTest and now - record.climbTest.startedAt >= 12000 then
                log(("climbtest-timeout id=%d epoch=%d nonce=%d"):format(record.id, record.epoch, record.climbTest.nonce), true)
                stopClimbTest(record, "timeout")
            elseif not isPedDead(record.ped) then
                local x, y, z = getElementPosition(record.ped)
                local closest, closestDistanceSquared = findClosestPlayer(x, y, z, config.despawnRadius)
                if not closest then
                    removeRecord(record, "outside-residency")
                elseif not isEligiblePlayer(record.owner) then
                    beginHandoff(record, closest, "owner-ineligible")
                elseif record.airTest or record.climbTest then
                    -- Keep the deterministic baseline on one owner. The
                    -- handoff variants transfer only when their owner reports
                    -- the requested physical phase above.
                    record.handoffCandidate = nil
                    record.handoffCandidateSince = nil
                elseif closest ~= record.owner then
                    local ownerX, ownerY, ownerZ = getElementPosition(record.owner)
                    local ownerDistance = math.sqrt(squaredDistance(x, y, z, ownerX, ownerY, ownerZ))
                    local closestDistance = math.sqrt(closestDistanceSquared)
                    if closestDistance + config.handoffMargin < ownerDistance then
                        if record.handoffCandidate ~= closest then
                            record.handoffCandidate = closest
                            record.handoffCandidateSince = now
                        elseif now - record.handoffCandidateSince >= config.handoffHold then
                            beginHandoff(record, closest, "closer-owner")
                        end
                    else
                        record.handoffCandidate = nil
                        record.handoffCandidateSince = nil
                    end
                else
                    record.handoffCandidate = nil
                    record.handoffCandidateSince = nil
                end
            end
        end
    end
end, 1000, 0)

setTimer(function()
    if debugEnabled then
        log(("telemetry active=%d requests=%d misses=%d rejected=%d spawned=%d despawned=%d handoffs=%d missReasons=%s rejectionReasons=%s"):format(
                getTrafficPedCount(), stats.requests, stats.candidateMisses, stats.rejected, stats.spawned, stats.despawned, stats.handoffs,
                formatReasons(stats.missReasons), formatReasons(stats.rejectionReasons)))
    end
end, 10000, 0)

addCommandHandler("pedtraffic", function(player, _, action, value)
    action = tostring(action or "status"):lower()
    if action == "on" then
        setEnabled(true, player)
    elseif action == "off" then
        setEnabled(false, player)
    elseif action == "debug" then
        debugEnabled = tostring(value or "on"):lower() ~= "off"
        triggerClientEvent(root, "pedTraffic:setDebug", resourceRoot, debugEnabled)
        log("debug=" .. tostring(debugEnabled), true)
    elseif action == "cap" then
        local cap = math.floor(tonumber(value) or 0)
        if cap >= 1 and cap <= 110 then
            config.globalCap = cap
            outputChatBox("Ped traffic cap = " .. tostring(cap), player, 120, 220, 255)
        else
            outputChatBox("Usage: /pedtraffic cap 1..110", player, 255, 160, 80)
        end
    elseif action == "weapon" and isElement(player) then
        giveWeapon(player, 22, 200, true)
        outputChatBox("Ped traffic threat test: pistol + 200 rounds", player, 120, 220, 255)
    elseif action == "vehicle" and isElement(player) then
        createTestVehicle(player, value)
    elseif action == "airtest" and isElement(player) then
        startAirTest(player, tostring(value or ""):lower() == "handoff")
    elseif action == "climbtest" and isElement(player) then
        startClimbTest(player, tostring(value or ""):lower() == "handoff")
    else
        local activeCount = 0
        for ped in pairs(trafficPeds) do
            if isElement(ped) then activeCount = activeCount + 1 end
        end
        outputChatBox(("Ped traffic: enabled=%s active=%d cap=%d requests=%d misses=%d rejected=%d handoffs=%d"):format(
                          tostring(enabled), activeCount, config.globalCap, stats.requests, stats.candidateMisses, stats.rejected, stats.handoffs),
                      player, 120, 220, 255)
    end
end)

addEventHandler("onResourceStart", resourceRoot, function()
    outputServerLog("[ped-traffic] V1 loaded disabled; use /pedtraffic on, /pedtraffic debug on")
end)

addEventHandler("onResourceStop", resourceRoot, function()
    triggerClientEvent(root, "pedTraffic:setEnabled", resourceRoot, false, false)
    clearTraffic("resource-stop")
    clearTestVehicles()
end)
