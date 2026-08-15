local config = {
    -- Preserve twenty logical ped slots for players, missions and unrelated
    -- resources while allowing two disjoint native population bubbles to fill.
    globalCap = 90,
    pedPoolSoftLimit = 90,
    despawnGrace = 4000,
    cellSize = 64,
    maxPerCell = 12,
    minSeparation = 2,
    requestInterval = 100,
    requestTimeout = 3500,
    visibilityTimeout = 1000,
    handoffMargin = 20,
    handoffHold = 3000,
    handoffTimeout = 2000,
    corpseLifetime = 30000,
    nativeMeleeDamageRadius = 5,
    nativeMeleeDamageInterval = 250,
    minimumGangGroupSize = 2,
    maximumGangGroupSize = 4,
    maximumGangGroupSpan = 8,
    -- Each client GTA process has eight group slots and normally reserves one
    -- for the player. Leave two more per owner for unrelated resource logic.
    maximumNativeGangGroups = 5,
}

-- MTA represents scripted peds with CPlayerPed, whose constructor selects
-- STYLE_GRAB_KICK (15). Stock ambient CPed selects STYLE_STANDARD (4), so every
-- traffic spawn below explicitly restores style 4 before native AI runs.
-- CTaskSimpleFight then uses UNARMED_1. Gang pedstats have attackStrength=1.0,
-- while the stock civilian profiles span 0.3..1.5. These are the exact integer factors
-- reachable after GetStrikeDamage applies that multiplier and truncates. Keep
-- this canonical allowlist narrower than the engine primitive: accepting the
-- full melee.dat 1..200 range would let a compromised syncer claim the
-- UNARMED_4 instant-kill strike for an ordinary traffic ped.
local stockUnarmedGangDamageFactors = {[5] = true, [6] = true, [9] = true, [15] = true, [25] = true}
local stockUnarmedCivilianDamageFactors = {
    [1] = true, [2] = true, [3] = true, [4] = true, [5] = true, [6] = true, [7] = true, [8] = true,
    [9] = true, [10] = true, [12] = true, [13] = true, [15] = true, [16] = true, [17] = true,
    [18] = true, [20] = true, [22] = true, [25] = true, [27] = true, [30] = true, [37] = true,
}

local function isCanonicalTrafficMeleeDamage(record, weapon, damageFactor)
    if weapon ~= 0 then
        return false
    end
    if record.populationClass == "gang" then
        return stockUnarmedGangDamageFactors[damageFactor] == true
    end
    return record.populationClass == "civilian" and stockUnarmedCivilianDamageFactors[damageFactor] == true
end

-- Stock peds.ide/pedgrp.dat mapping. The server cannot inspect a model's native
-- CPedModelInfo type, so this allowlist authenticates the gang identity claimed
-- by an untrusted client candidate before creating shared element metadata.
local gangByModel = {
    [102] = 0, [103] = 0, [104] = 0,
    [105] = 1, [106] = 1, [107] = 1,
    [108] = 2, [109] = 2, [110] = 2,
    [173] = 3, [174] = 3, [175] = 3,
    [121] = 4, [122] = 4, [123] = 4,
    [124] = 5, [125] = 5, [126] = 5, [127] = 5,
    [117] = 6, [118] = 6, [120] = 6,
    [114] = 7, [115] = 7, [116] = 7,
}

-- Models reachable from the stock civilian popcycle groups, intersected with
-- peds.ide CIVMALE/CIVFEMALE. This excludes cops, dealers, gangs and specials
-- even when a modified client forges the accompanying pedType field.
local civilianPedTypeByModel = {}
for _, model in ipairs({
    14, 15, 16, 17, 18, 19, 20, 22, 24, 25, 26, 27, 32, 33, 34, 35, 36, 37, 43, 44, 45, 46, 48, 50, 51, 52, 57, 58,
    59, 60, 61, 71, 72, 73, 77, 78, 79, 82, 83, 84, 94, 95, 96, 98, 99, 101, 128, 132, 133, 134, 135, 136, 137, 142,
    147, 153, 154, 158, 159, 160, 161, 162, 164, 170, 182, 185, 186, 187, 188, 189, 200, 202, 206, 212, 213, 221, 222,
    227, 228, 229, 230, 234, 235, 236, 239, 240, 241, 242,
}) do
    civilianPedTypeByModel[model] = 4
end
for _, model in ipairs({
    9, 10, 12, 13, 31, 38, 39, 40, 41, 53, 54, 55, 56, 69, 76, 88, 89, 90, 91, 92, 93, 129, 130, 131, 138, 139, 140,
    141, 148, 150, 151, 157, 169, 196, 197, 198, 199, 201, 215, 216, 218, 219, 224, 225, 226, 231, 232, 233, 251,
}) do
    civilianPedTypeByModel[model] = 5
end

local enabled = false
local debugEnabled = false
local populationTracePath = "@population-server.jsonl"
local populationTraceRows = 0
local populationTraceLimit = 20000
local populationClientIds = {}
local nextPopulationClientId = 0
local nextVisibilityCheckId = 0
local nextRequestId = 0
local nextPedId = 0
local nextGroupId = 0
local nextAirTestId = 0
local nextClimbTestId = 0
local requestCursor = 0
local pendingRequests = {}
local pendingVisibilityChecks = {}
local populationProfiles = {}
local populationWorld = PedTrafficPopulationWorld.create("post_intro")
local populationWorldRevisions = {}
local trafficPeds = {}
local trafficGroups = {}
local testVehicles = {}
local residencyTest = false
local nextResidencyTestId = 0
local stopResidencyTest
local stats = {
    requests = 0,
    candidateMisses = 0,
    rejected = 0,
    spawned = 0,
    handoffs = 0,
    despawned = 0,
    missReasons = {},
    rejectionReasons = {},
    populationSelections = {civilian = 0, gang = 0},
    gangSelections = {[0] = 0, [1] = 0, [2] = 0, [3] = 0, [4] = 0, [5] = 0, [6] = 0, [7] = 0, [8] = 0, [9] = 0},
    spawnedModels = {},
    groupSpawns = 0,
    groupHandoffs = 0,
    groupRemovals = 0,
    groupPromotions = 0,
}

local function log(message, force)
    if debugEnabled or force then
        outputDebugString("[ped-traffic][server] " .. message)
    end
end

local function utcTimestamp()
    local localTime = getRealTime()
    local utc = getRealTime(localTime.timestamp, false)
    return ("%04d-%02d-%02dT%02d:%02d:%02dZ"):format(
        utc.year + 1900, utc.month + 1, utc.monthday, utc.hour, utc.minute, utc.second)
end

local function getPopulationClientId(player)
    local id = populationClientIds[player]
    if not id then
        nextPopulationClientId = nextPopulationClientId + 1
        id = nextPopulationClientId
        populationClientIds[player] = id
    end
    return id
end

local function resetPopulationTrace()
    populationTraceRows = 0
    if fileExists(populationTracePath) then
        fileDelete(populationTracePath)
    end
end

local function writePopulationTrace(event, fields)
    if not debugEnabled or populationTraceRows >= populationTraceLimit then
        return false
    end

    local row = {
        schema = "neon.ped_traffic.population",
        schema_version = 1,
        wall_utc = utcTimestamp(),
        monotonic_ms = getTickCount(),
        event_sequence = populationTraceRows + 1,
        event = event,
        world_revision = populationWorld.revision,
        preset = populationWorld.preset,
    }
    for key, value in pairs(fields or {}) do
        row[key] = value
    end

    local encoded = toJSON(row, true)
    -- MTA wraps one keyed Lua table in a JSON array. JSONL requires the object
    -- itself so ordinary line-oriented tooling can parse every record.
    if type(encoded) == "string" and encoded:sub(1, 2) == "[{" and encoded:sub(-2) == "}]" then
        encoded = encoded:sub(2, -2)
    end
    if type(encoded) ~= "string" or encoded:sub(1, 1) ~= "{" or encoded:sub(-1) ~= "}" then
        return false
    end

    local file = fileExists(populationTracePath) and fileOpen(populationTracePath) or fileCreate(populationTracePath)
    if not file then
        return false
    end
    fileSetPos(file, fileGetSize(file))
    fileWrite(file, encoded .. "\n")
    fileFlush(file)
    fileClose(file)
    populationTraceRows = populationTraceRows + 1
    return true
end

local function sendPopulationWorldState(target)
    triggerClientEvent(target or root, "pedTraffic:populationWorldState", resourceRoot, populationWorld:getClientProjection())
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

local function formatNumericMap(bucket)
    local values = {}
    for key, count in pairs(bucket) do
        if count > 0 then
            values[#values + 1] = tostring(key) .. ":" .. tostring(count)
        end
    end
    table.sort(values)
    return #values > 0 and table.concat(values, ",") or "none"
end

local function isFiniteNumber(value)
    return type(value) == "number" and value == value and value > -1000000 and value < 1000000
end

local function isIntegerInRange(value, minimum, maximum)
    return isFiniteNumber(value) and value == math.floor(value) and value >= minimum and value <= maximum
end

local function validatePopulationProfile(profile)
    if type(profile) ~= "table" or profile.worldRevision ~= populationWorld.revision or
        not isFiniteNumber(profile.target) or not isFiniteNumber(profile.supportedTarget) or not isFiniteNumber(profile.civilianTarget) or
        not isFiniteNumber(profile.rawCopTarget) or not isFiniteNumber(profile.copTarget) or not isFiniteNumber(profile.gangTarget) or
        not isFiniteNumber(profile.dealerTarget) or not isFiniteNumber(profile.pedDensityMultiplier) or
        not isFiniteNumber(profile.fewerPedsMultiplier) or not isFiniteNumber(profile.creationDistanceMultiplier) or
        not isFiniteNumber(profile.generationDistanceMultiplier) or not isIntegerInRange(profile.maximumPedsInUse, 0, 110) or
        not isIntegerInRange(profile.zoneType, 0, 19) or
        not isIntegerInRange(profile.timeIndex, 0, 11) or type(profile.weekend) ~= "boolean" or
        not isIntegerInRange(profile.dealerStrength, 0, 255) or not isIntegerInRange(profile.raceFlags, 0, 15) or
        type(profile.noCops) ~= "boolean" or type(profile.gangWeights) ~= "table" then
        return false
    end

    if profile.target < 0 or profile.target > 110 or profile.supportedTarget < 0 or profile.supportedTarget > 110 or
        profile.civilianTarget < 0 or profile.civilianTarget > 110 or profile.rawCopTarget < 0 or profile.rawCopTarget > 110 or
        profile.copTarget < 0 or profile.copTarget > 110 or
        profile.gangTarget < 0 or profile.gangTarget > 110 or profile.dealerTarget < 0 or profile.dealerTarget > 110 or
        math.abs(profile.supportedTarget - profile.civilianTarget - profile.gangTarget) > 0.05 or
        math.abs(profile.target - profile.supportedTarget - profile.copTarget - profile.dealerTarget) > 0.05 or
        (profile.noCops and profile.copTarget > 0.05) or (not profile.noCops and math.abs(profile.copTarget - profile.rawCopTarget) > 0.05) or
        profile.pedDensityMultiplier < 0 or profile.pedDensityMultiplier > 10 or profile.fewerPedsMultiplier < 0 or
        profile.fewerPedsMultiplier > 10 or profile.creationDistanceMultiplier < 0.999 or profile.creationDistanceMultiplier > 1.501 or
        profile.generationDistanceMultiplier <= 0 or profile.generationDistanceMultiplier > 10 then
        return false
    end

    local gangWeights = {}
    local totalGangWeight = 0
    for index = 1, 10 do
        local weight = profile.gangWeights[index]
        if not isIntegerInRange(weight, 0, 255) then
            return false
        end
        gangWeights[index] = weight
        totalGangWeight = totalGangWeight + weight
    end
    if profile.gangTarget > 0.05 and totalGangWeight == 0 then
        return false
    end

    -- Enabling territory wars does not mean one is currently being fought.
    -- Vanilla suppresses ambient cops only during an active fight (or through
    -- the independent no-cops flags already reflected by profile.copTarget).
    local effectiveCopTarget = profile.copTarget
    return {
        target = profile.target,
        effectiveTarget = profile.target - profile.copTarget + effectiveCopTarget,
        supportedTarget = profile.supportedTarget,
        civilianTarget = profile.civilianTarget,
        rawCopTarget = profile.rawCopTarget,
        copTarget = profile.copTarget,
        effectiveCopTarget = effectiveCopTarget,
        gangTarget = profile.gangTarget,
        dealerTarget = profile.dealerTarget,
        zoneType = profile.zoneType,
        timeIndex = profile.timeIndex,
        weekend = profile.weekend,
        dealerStrength = profile.dealerStrength,
        raceFlags = profile.raceFlags,
        noCops = profile.noCops,
        gangWeights = gangWeights,
        totalGangWeight = totalGangWeight,
        pedDensityMultiplier = profile.pedDensityMultiplier,
        fewerPedsMultiplier = profile.fewerPedsMultiplier,
        maximumPedsInUse = profile.maximumPedsInUse,
        creationDistanceMultiplier = profile.creationDistanceMultiplier,
        generationDistanceMultiplier = profile.generationDistanceMultiplier,
        worldRevision = profile.worldRevision,
        receivedAt = getTickCount(),
    }
end

local function calculateNativeTargets(profile)
    -- AddToPopulation gates the complete ped population before FindNewPedType
    -- chooses a family. Project that exact gate onto the civilian/gang subset
    -- currently implemented instead of independently rounding both classes.
    local civilianNativeTarget = profile.civilianTarget * populationWorld.densityMultiplier
    local gangNativeTarget = populationWorld.randomGangMembers and profile.gangTarget * populationWorld.densityMultiplier or 0
    local supportedTarget = civilianNativeTarget + gangNativeTarget
    local supportedGangWeight = 0
    for index = 1, 8 do supportedGangWeight = supportedGangWeight + profile.gangWeights[index] end
    if profile.totalGangWeight > 0 and supportedGangWeight < profile.totalGangWeight then
        gangNativeTarget = gangNativeTarget * supportedGangWeight / profile.totalGangWeight
        supportedTarget = civilianNativeTarget + gangNativeTarget
    end
    if supportedTarget <= 0 or profile.target <= 0 then
        return 0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
    end

    local fullPopulationGate = profile.pedDensityMultiplier * profile.fewerPedsMultiplier *
        math.min(profile.maximumPedsInUse, profile.target)
    local projectedSupportedTarget = math.max(0, fullPopulationGate * supportedTarget / profile.target)
    local civilian = projectedSupportedTarget * civilianNativeTarget / supportedTarget
    local gang = projectedSupportedTarget * gangNativeTarget / supportedTarget
    local total = math.max(0, math.ceil(projectedSupportedTarget - 0.0001))
    local gangTargets = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
    if gang > 0 and supportedGangWeight > 0 then
        for index = 1, 8 do
            gangTargets[index] = gang * profile.gangWeights[index] / supportedGangWeight
        end
    end
    return total, civilian, gang, gangTargets
end

local function getPopulationRadii(profile)
    if not profile then
        return false
    end
    local scaledDistance = profile.creationDistanceMultiplier * profile.generationDistanceMultiplier
    local civilian = scaledDistance * 54.5
    return {
        civilian = civilian,
        gang = civilian + 30,
        maximum = civilian + 30,
    }
end

local function getNativeTargetsNearPlayer(player)
    local profile = populationProfiles[player]
    if not profile or getTickCount() - profile.receivedAt > 2500 then
        return false
    end
    return calculateNativeTargets(profile)
end

local function isEligiblePlayer(player)
    return isElement(player) and getElementType(player) == "player" and not isPedDead(player) and
        getElementDimension(player) == 0 and getElementInterior(player) == 0
end

local function isPopulationWorldReady(player)
    return populationWorldRevisions[player] == populationWorld.revision
end

local function squaredDistance(x1, y1, z1, x2, y2, z2)
    local dx, dy, dz = x1 - x2, y1 - y2, z1 - z2
    return dx * dx + dy * dy + dz * dz
end

local function squaredDistance2D(x1, y1, x2, y2)
    local dx, dy = x1 - x2, y1 - y2
    return dx * dx + dy * dy
end

local function getEligiblePlayers()
    local players = {}
    for _, player in ipairs(getElementsByType("player")) do
        if isEligiblePlayer(player) and isPopulationWorldReady(player) then
            players[#players + 1] = player
        end
    end
    return players
end

local function getPopulationRadiusForClass(profile, populationClass)
    local radii = getPopulationRadii(profile)
    return radii and (populationClass == "gang" and radii.gang or radii.civilian) or false
end

local function findClosestPopulationResident(x, y, z, populationClass, excludedPlayer)
    local closest, closestDistanceSquared
    for _, player in ipairs(getEligiblePlayers()) do
        if player ~= excludedPlayer then
            local radius = getPopulationRadiusForClass(populationProfiles[player], populationClass)
            if radius then
                local px, py, pz = getElementPosition(player)
                local distanceSquared = squaredDistance2D(x, y, px, py)
                if distanceSquared <= radius * radius and
                    (not closestDistanceSquared or distanceSquared < closestDistanceSquared) then
                    closest = player
                    closestDistanceSquared = distanceSquared
                end
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

local function getPopulationCountsNearPlayer(player)
    local profile = populationProfiles[player]
    local radii = getPopulationRadii(profile)
    if not radii then
        return 0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
    end

    local x, y, z = getElementPosition(player)
    local total = 0
    local civilians = 0
    local gangs = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
    for ped, record in pairs(trafficPeds) do
        if isElement(ped) then
            local px, py, pz = getElementPosition(ped)
            local radius = record.populationClass == "gang" and radii.gang or radii.civilian
            if squaredDistance2D(x, y, px, py) <= radius * radius then
                total = total + 1
                if record.populationClass == "gang" and type(record.gang) == "number" then
                    gangs[record.gang + 1] = gangs[record.gang + 1] + 1
                else
                    civilians = civilians + 1
                end
            end
        end
    end
    return total, civilians, gangs
end

local function chooseGangFromDeficit(profile, gangTarget, gangCounts)
    if gangTarget <= 0 or profile.totalGangWeight <= 0 then
        return false
    end

    local selectedGang = false
    local selectedScore = 0
    local scores = {}
    -- GTA's stock slots 8/9 reuse GANG8 model identity and cannot currently
    -- be represented by Neon's explicit ped-type contract. Keep them outside
    -- this supported checkpoint instead of issuing an impossible client hint.
    local supportedWeight = 0
    for index = 1, 8 do supportedWeight = supportedWeight + profile.gangWeights[index] end
    if supportedWeight <= 0 then
        return false
    end
    for index = 1, 8 do
        local score = profile.gangWeights[index] / supportedWeight - gangCounts[index] / gangTarget
        scores[index] = score
        -- Vanilla's strict argmax starts at gang zero, so the lower gang ID
        -- wins an exact tie. A positive aggregate gang deficit guarantees at
        -- least one positive family score.
        if score > selectedScore then
            selectedGang = index - 1
            selectedScore = score
        end
    end
    return selectedGang, scores
end

local function selectPopulationForPlayer(player)
    local profile = populationProfiles[player]
    local totalTarget, civilianTarget, gangTarget, gangTargets = getNativeTargetsNearPlayer(player)
    if totalTarget == false or not profile then
        return false
    end

    local totalCount, civilianCount, gangCounts = getPopulationCountsNearPlayer(player)
    if totalCount >= totalTarget then
        return false
    end

    local totalGangCount = 0
    for index = 1, 10 do
        totalGangCount = totalGangCount + gangCounts[index]
    end
    local civilianDeficit = civilianTarget - civilianCount
    local gangDeficit = gangTarget - totalGangCount
    local civilianChance = civilianDeficit
    local gangChance = gangDeficit
    -- This small independent randomization is part of FindNewPedType itself;
    -- it prevents low remaining deficits from producing a rigid cadence.
    if civilianChance < 2 then civilianChance = civilianChance * math.random() end
    if gangChance < 2 then gangChance = gangChance * math.random() end

    if math.max(civilianChance, gangChance) <= 0 then
        return false
    end

    local selection = {
        profileSignature = profile.signature,
        totalTarget = totalTarget,
        civilianTarget = civilianTarget,
        gangTarget = gangTarget,
        gangTargets = gangTargets,
        totalCount = totalCount,
        civilianCount = civilianCount,
        gangCounts = gangCounts,
        totalGangCount = totalGangCount,
        civilianDeficit = civilianDeficit,
        gangDeficit = gangDeficit,
        civilianChance = civilianChance,
        gangChance = gangChance,
    }
    -- FindNewPedType's complete tie order is dealer, gang, cop, civilian. In
    -- this civilian/gang checkpoint that leaves gang before civilian.
    if gangChance >= civilianChance then
        local gang, scores = chooseGangFromDeficit(profile, gangTarget, gangCounts)
        if gang == false then
            return false
        end
        selection.populationClass = "gang"
        selection.gang = gang
        selection.gangScores = scores
        selection.gangScore = scores[gang + 1]
    else
        selection.populationClass = "civilian"
        selection.gang = false
    end
    return selection
end

local function isSelectionStillNeeded(player, selection)
    local profile = populationProfiles[player]
    local totalTarget, civilianTarget, gangTarget = getNativeTargetsNearPlayer(player)
    if totalTarget == false or not profile or profile.signature ~= selection.profileSignature then
        return false, "stale-population-profile"
    end

    local totalCount, civilianCount, gangCounts = getPopulationCountsNearPlayer(player)
    if totalCount ~= selection.totalCount or civilianCount ~= selection.civilianCount then
        return false, "population-selection-stale"
    end
    for index = 1, 10 do
        if gangCounts[index] ~= selection.gangCounts[index] then
            return false, "population-selection-stale"
        end
    end
    if totalCount >= totalTarget then
        return false, "population-total-target"
    end
    if selection.populationClass == "civilian" then
        if civilianTarget - civilianCount <= 0 then
            return false, "population-selection-stale"
        end
        return true
    end

    local totalGangCount = 0
    for index = 1, 10 do totalGangCount = totalGangCount + gangCounts[index] end
    if gangTarget - totalGangCount <= 0 then
        return false, "population-selection-stale"
    end
    local gang = chooseGangFromDeficit(profile, gangTarget, gangCounts)
    if gang ~= selection.gang then
        return false, "population-selection-stale"
    end
    return true
end

local function isPopulationPedSurplusForAllResidents(record)
    if not record or not isElement(record.ped) then
        return false
    end

    local pedX, pedY, pedZ = getElementPosition(record.ped)
    local residentCount = 0
    for _, player in ipairs(getEligiblePlayers()) do
        local playerX, playerY, playerZ = getElementPosition(player)
        local radius = getPopulationRadiusForClass(populationProfiles[player], record.populationClass)
        if radius and squaredDistance2D(pedX, pedY, playerX, playerY) <= radius * radius then
            residentCount = residentCount + 1
            local nativeTarget, civilianTarget, _, gangTargets = getNativeTargetsNearPlayer(player)
            if nativeTarget == false then
                return false
            end
            local _, civilianCount, gangCounts = getPopulationCountsNearPlayer(player)
            if record.populationClass == "gang" then
                if gangCounts[record.gang + 1] - gangTargets[record.gang + 1] < 1 then
                    return false
                end
            elseif civilianCount - civilianTarget < 1 then
                return false
            end
        end
    end
    return residentCount > 0
end

local function findFurthestPopulationPed(x, y, z, radius, predicate)
    local furthestRecord = false
    local furthestDistanceSquared = -1
    local radiusSquared = radius * radius
    for ped, record in pairs(trafficPeds) do
        if isElement(ped) and not record.group and not record.removing and record.state == "active" and not isPedDead(ped) and not record.airTest and
            not record.climbTest and getTickCount() - (record.lastInteractionAt or 0) >= 10000 and predicate(record) and
            isPopulationPedSurplusForAllResidents(record) then
            local px, py, pz = getElementPosition(ped)
            local distanceSquared = squaredDistance2D(x, y, px, py)
            if distanceSquared <= radiusSquared and distanceSquared > furthestDistanceSquared then
                furthestRecord = record
                furthestDistanceSquared = distanceSquared
            end
        end
    end
    return furthestRecord
end

local function getGroupDistanceSquaredToPlayer(group, player)
    local playerX, playerY = getElementPosition(player)
    local closestDistanceSquared
    for _, record in ipairs(group.members) do
        if isElement(record.ped) then
            local pedX, pedY = getElementPosition(record.ped)
            local distanceSquared = squaredDistance2D(pedX, pedY, playerX, playerY)
            if not closestDistanceSquared or distanceSquared < closestDistanceSquared then
                closestDistanceSquared = distanceSquared
            end
        end
    end
    return closestDistanceSquared
end

local function findFurthestSurplusPopulationGroup(x, y, z, radius, gang, requireTotalSurplus)
    local furthestGroup = false
    local furthestDistanceSquared = -1
    local radiusSquared = radius * radius
    for _, group in pairs(trafficGroups) do
        if not group.removing and not group.residencyTestId and group.state == "active" and (gang == nil or group.gang == gang) then
            local centreX, centreY, centreZ, memberCount = 0, 0, 0, 0
            local eligible = true
            for _, record in ipairs(group.members) do
                if not isElement(record.ped) or isPedDead(record.ped) or getTickCount() - (record.lastInteractionAt or 0) < 10000 then
                    eligible = false
                    break
                end
                local px, py, pz = getElementPosition(record.ped)
                centreX, centreY, centreZ = centreX + px, centreY + py, centreZ + pz
                memberCount = memberCount + 1
            end
            if eligible and memberCount > 0 then
                centreX, centreY, centreZ = centreX / memberCount, centreY / memberCount, centreZ / memberCount
                local distanceSquared = squaredDistance2D(x, y, centreX, centreY)
                local surplusForEveryResident = true
                local residentCount = 0
                for _, player in ipairs(getEligiblePlayers()) do
                    local residentDistanceSquared = getGroupDistanceSquaredToPlayer(group, player)
                    local radii = getPopulationRadii(populationProfiles[player])
                    if radii and residentDistanceSquared and residentDistanceSquared <= radii.gang * radii.gang then
                        residentCount = residentCount + 1
                        local nativeTarget, _, _, gangTargets = getNativeTargetsNearPlayer(player)
                        if nativeTarget == false then
                            surplusForEveryResident = false
                            break
                        end
                        local totalCount, _, gangCounts = getPopulationCountsNearPlayer(player)
                        if (requireTotalSurplus and totalCount - nativeTarget < memberCount) or
                            gangCounts[group.gang + 1] - gangTargets[group.gang + 1] < memberCount then
                            surplusForEveryResident = false
                            break
                        end
                    end
                end
                if residentCount == 0 then
                    surplusForEveryResident = false
                end
                if surplusForEveryResident and distanceSquared <= radiusSquared and distanceSquared > furthestDistanceSquared then
                    furthestGroup = group
                    furthestDistanceSquared = distanceSquared
                end
            end
        end
    end
    return furthestGroup
end

local function getTrafficPedCount()
    local count = 0
    for ped in pairs(trafficPeds) do
        if isElement(ped) then count = count + 1 end
    end
    return count
end

local function getTrafficGroupCount()
    local count = 0
    for _, group in pairs(trafficGroups) do
        if not group.removing then count = count + 1 end
    end
    return count
end

local function beginSpawnFade(peds)
    local fading = {}
    for _, ped in ipairs(peds) do
        if isElement(ped) then
            setElementAlpha(ped, 0)
            fading[#fading + 1] = ped
        end
    end
    if #fading == 0 then
        return
    end
    triggerClientEvent(root, "pedTraffic:spawnFadeIn", resourceRoot, fading, 267)
    setTimer(function(elements)
        for _, ped in ipairs(elements) do
            if isElement(ped) then
                setElementAlpha(ped, 255)
            end
        end
    end, 320, 1, fading)
end

local function getActivePopulationSummary()
    local civilians = 0
    local gangs = {[0] = 0, [1] = 0, [2] = 0, [3] = 0, [4] = 0, [5] = 0, [6] = 0, [7] = 0, [8] = 0, [9] = 0}
    for ped, record in pairs(trafficPeds) do
        if isElement(ped) then
            if record.populationClass == "gang" and type(record.gang) == "number" then
                gangs[record.gang] = gangs[record.gang] + 1
            else
                civilians = civilians + 1
            end
        end
    end
    return civilians, gangs
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

local removeGroup

local function removeRecord(record, reason)
    if not record or record.removing then
        return
    end
    if record.group then
        return removeGroup(record.group, reason)
    end
    record.removing = true
    if record.visibilityCheckId then
        pendingVisibilityChecks[record.visibilityCheckId] = nil
        record.visibilityCheckId = nil
    end
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
    writePopulationTrace("despawn", {
        traffic_id = record.id,
        population_class = record.populationClass,
        gang = record.gang,
        reason = tostring(reason),
        active = getTrafficPedCount(),
    })
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
    record.outsideResidencySince = nil
    record.assignmentStartedAt = getTickCount()
    record.assignmentLastSent = 0

    -- `assigning` is a transaction, not an authority grant. Keep the shared
    -- element inert until the client proves that its collision residency is
    -- loaded and ground-valid by accepting the native profile.
    local resumePhysical = (record.airTest and record.airTest.handoffTriggered == true) or
        (record.climbTest and record.climbTest.handoffTriggered == true)
    if not resumePhysical then
        setElementFrozen(record.ped, true)
    end

    if not setElementSyncer(record.ped, owner, true) then
        removeRecord(record, "syncer-refused")
        return false
    end
    -- Victim-side damage replay can outlive the owner packet that produced it.
    -- Replicate the sparse authority generation so every observer, including a
    -- third client that is neither old nor new owner, can reject a stale hit.
    setElementData(record.ped, "neon:ambientPedTrafficEpoch", record.epoch)

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
        if not record.group and not record.removing and record.state == "active" and isElement(record.ped) and not isPedDead(record.ped) and
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

local finishSuspension

local function finishHandoff(record, reason)
    if not record or record.removing then
        return
    end
    local owner = record.pendingOwner
    if not isEligiblePlayer(owner) then
        local x, y, z = getElementPosition(record.ped)
        owner = findClosestPopulationResident(x, y, z, record.populationClass)
    end
    if not owner then
        finishSuspension(record, reason)
        return
    end
    assignOwner(record, owner, reason)
end

finishSuspension = function(record, reason)
    if not record or record.removing or not isElement(record.ped) then
        return
    end
    record.pendingOwner = nil
    record.owner = nil
    record.state = "suspended"
    record.handoffDeadline = nil
    record.suspendedAt = record.suspendedAt or getTickCount()
    setElementSyncer(record.ped, false)
    setElementVelocity(record.ped, 0, 0, 0)
    setElementFrozen(record.ped, true)
    writePopulationTrace("collision_residency_suspended", {
        traffic_id = record.id,
        epoch = record.epoch,
        reason = tostring(reason),
    })
    log(("suspended id=%d epoch=%d reason=%s"):format(record.id, record.epoch, tostring(reason)), true)
end

local function beginSuspension(record, reason)
    if not record or record.removing or record.state == "suspending" or record.state == "suspended" or not isElement(record.ped) then
        return
    end
    record.pendingOwner = nil
    record.state = "suspending"
    record.handoffDeadline = getTickCount() + config.handoffTimeout
    record.outsideResidencySince = record.outsideResidencySince or getTickCount()
    setElementFrozen(record.ped, true)
    writePopulationTrace("collision_residency_suspension_started", {
        traffic_id = record.id,
        epoch = record.epoch,
        owner_id = isElement(record.owner) and getPopulationClientId(record.owner) or false,
        reason = tostring(reason),
    })
    if isElement(record.owner) then
        triggerClientEvent(record.owner, "pedTraffic:revoke", resourceRoot, record.ped, record.epoch, reason)
    else
        finishSuspension(record, "owner-departed")
    end
end

local function beginHandoff(record, newOwner, reason)
    if not record or record.removing or record.state ~= "active" or newOwner == record.owner then
        return
    end

    record.pendingOwner = newOwner
    record.state = "revoking"
    record.handoffDeadline = getTickCount() + config.handoffTimeout

    -- Preserve explicit airborne/climb transfers, whose physical task is the
    -- handoff payload. Ordinary ambient handoffs are frozen until the new
    -- owner's collision-ready acknowledgement.
    if not (record.airTest and record.airTest.handoffTriggered == true) and
        not (record.climbTest and record.climbTest.handoffTriggered == true) then
        setElementFrozen(record.ped, true)
    end

    if isElement(record.owner) then
        triggerClientEvent(record.owner, "pedTraffic:revoke", resourceRoot, record.ped, record.epoch, reason)
        log(("revoke id=%d epoch=%d old=%s new=%s reason=%s"):format(record.id, record.epoch, getPlayerName(record.owner),
                                                                    getPlayerName(newOwner), tostring(reason)))
    else
        finishHandoff(record, "owner-departed")
    end
end

local function getGroupCentre(group)
    local x, y, z, count = 0, 0, 0, 0
    for _, record in ipairs(group.members) do
        if isElement(record.ped) then
            local px, py, pz = getElementPosition(record.ped)
            x, y, z, count = x + px, y + py, z + pz, count + 1
        end
    end
    if count == 0 then
        return false
    end
    return x / count, y / count, z / count
end

local function countNativeGroupsForOwner(owner, excludedGroup)
    local count = 0
    for _, group in pairs(trafficGroups) do
        if group ~= excludedGroup and not group.removing and (group.owner == owner or group.pendingOwner == owner) then
            count = count + 1
        end
    end
    return count
end

local function findClosestGroupResident(group, excludedPlayer, requireCapacity)
    local closest, closestDistanceSquared
    for _, player in ipairs(getEligiblePlayers()) do
        if player ~= excludedPlayer and
            (not requireCapacity or countNativeGroupsForOwner(player, group) < config.maximumNativeGangGroups) then
            local radii = getPopulationRadii(populationProfiles[player])
            local distanceSquared = radii and getGroupDistanceSquaredToPlayer(group, player)
            if distanceSquared and distanceSquared <= radii.gang * radii.gang and
                (not closestDistanceSquared or distanceSquared < closestDistanceSquared) then
                closest = player
                closestDistanceSquared = distanceSquared
            end
        end
    end
    return closest, closestDistanceSquared
end

local function setGroupState(group, state)
    group.state = state
    for _, record in ipairs(group.members) do
        record.owner = group.owner
        record.pendingOwner = group.pendingOwner
        record.state = state
        record.epoch = group.epoch
    end
end

local function sendGroupAssignment(group, reason)
    if not group or group.removing or group.state ~= "assigning" or not isElement(group.owner) then
        return false
    end
    local peds = {}
    for _, record in ipairs(group.members) do
        if not isElement(record.ped) then
            return false
        end
        peds[#peds + 1] = record.ped
    end
    group.assignmentLastSent = getTickCount()
    return triggerClientEvent(group.owner, "pedTraffic:assignGroup", resourceRoot, group.id, group.epoch, peds, reason)
end

removeGroup = function(group, reason)
    if not group or group.removing then
        return
    end
    group.removing = true
    if group.visibilityCheckId then
        pendingVisibilityChecks[group.visibilityCheckId] = nil
        group.visibilityCheckId = nil
    end
    trafficGroups[group.id] = nil
    if isElement(group.owner) then
        triggerClientEvent(group.owner, "pedTraffic:stopGroup", resourceRoot, group.id, group.epoch, reason)
    end

    local removed = 0
    for _, record in ipairs(group.members) do
        if not record.removing then
            record.removing = true
            trafficPeds[record.ped] = nil
            if isElement(record.ped) then
                destroyElement(record.ped)
            end
            removed = removed + 1
        end
    end
    if group.counted then
        stats.despawned = stats.despawned + removed
        stats.groupRemovals = stats.groupRemovals + 1
    end
    log(("group-despawn group=%d gang=%d members=%d reason=%s active=%d"):format(
            group.id, group.gang, removed, tostring(reason), getTrafficPedCount()))
    writePopulationTrace("group_despawn", {
        group_id = group.id,
        gang = group.gang,
        members = removed,
        reason = tostring(reason),
        active = getTrafficPedCount(),
    })
end

local function assignGroupOwner(group, owner, reason)
    if not group or group.removing or not isEligiblePlayer(owner) or
        countNativeGroupsForOwner(owner, group) >= config.maximumNativeGangGroups then
        return false
    end
    for _, record in ipairs(group.members) do
        if not isElement(record.ped) then
            removeGroup(group, "member-missing-before-assign")
            return false
        end
    end

    local isHandoff = group.epoch > 0
    local previousOwner = group.owner
    group.owner = owner
    group.pendingOwner = nil
    group.epoch = group.epoch + 1
    group.handoffCandidate = nil
    group.handoffCandidateSince = nil
    group.handoffDeadline = nil
    group.outsideResidencySince = nil
    group.assignmentStartedAt = getTickCount()
    group.assignmentLastSent = 0
    setGroupState(group, "assigning")

    for _, record in ipairs(group.members) do
        setElementFrozen(record.ped, true)
    end

    -- A native group may only be driven by one machine. Commit every MTA
    -- syncer before asking that owner to acquire the local GTA group slot.
    for _, record in ipairs(group.members) do
        if not setElementSyncer(record.ped, owner, true) then
            removeGroup(group, "group-syncer-refused")
            return false
        end
    end
    for _, record in ipairs(group.members) do
        setElementData(record.ped, "neon:ambientPedTrafficEpoch", group.epoch)
    end
    if isHandoff then
        stats.handoffs = stats.handoffs + 1
        stats.groupHandoffs = stats.groupHandoffs + 1
    end
    if not sendGroupAssignment(group, reason) then
        removeGroup(group, "group-assignment-send-refused")
        return false
    end
    log(("group-assign group=%d epoch=%d gang=%d members=%d owner=%s reason=%s"):format(
            group.id, group.epoch, group.gang, #group.members, getPlayerName(owner), tostring(reason)))
    if isHandoff then
        local memberIds = {}
        for _, record in ipairs(group.members) do memberIds[#memberIds + 1] = record.id end
        writePopulationTrace("group_handoff_assigned", {
            group_id = group.id,
            epoch = group.epoch,
            member_ids = memberIds,
            previous_owner_id = isElement(previousOwner) and getPopulationClientId(previousOwner) or false,
            owner_id = getPopulationClientId(owner),
            reason = tostring(reason),
        })
    end
    return true
end

local finishGroupSuspension

local function finishGroupHandoff(group, reason)
    if not group or group.removing then
        return
    end
    local owner = group.pendingOwner
    if not isEligiblePlayer(owner) then
        owner = findClosestGroupResident(group, nil, true)
    end
    if not owner then
        finishGroupSuspension(group, reason)
        return
    end
    if not assignGroupOwner(group, owner, reason) then
        removeGroup(group, "group-handoff-owner-cap")
    end
end


finishGroupSuspension = function(group, reason)
    if not group or group.removing then
        return
    end
    group.owner = nil
    group.pendingOwner = nil
    group.handoffDeadline = nil
    group.suspendedAt = group.suspendedAt or getTickCount()
    setGroupState(group, "suspended")
    for _, record in ipairs(group.members) do
        if isElement(record.ped) then
            setElementSyncer(record.ped, false)
            setElementVelocity(record.ped, 0, 0, 0)
            setElementFrozen(record.ped, true)
        end
    end
    writePopulationTrace("group_collision_residency_suspended", {
        group_id = group.id,
        epoch = group.epoch,
        reason = tostring(reason),
    })
    log(("group-suspended group=%d epoch=%d reason=%s"):format(group.id, group.epoch, tostring(reason)), true)
end

local function beginGroupSuspension(group, reason)
    if not group or group.removing or group.state == "suspending" or group.state == "suspended" then
        return
    end
    group.pendingOwner = nil
    group.handoffDeadline = getTickCount() + config.handoffTimeout
    group.outsideResidencySince = group.outsideResidencySince or getTickCount()
    setGroupState(group, "suspending")
    for _, record in ipairs(group.members) do
        if isElement(record.ped) then
            setElementFrozen(record.ped, true)
        end
    end
    writePopulationTrace("group_collision_residency_suspension_started", {
        group_id = group.id,
        epoch = group.epoch,
        owner_id = isElement(group.owner) and getPopulationClientId(group.owner) or false,
        reason = tostring(reason),
    })
    if isElement(group.owner) then
        triggerClientEvent(group.owner, "pedTraffic:revokeGroup", resourceRoot, group.id, group.epoch, reason)
    else
        finishGroupSuspension(group, "group-owner-departed")
    end
end

local function beginGroupHandoff(group, newOwner, reason)
    if not group or group.removing or group.state ~= "active" or newOwner == group.owner then
        return
    end
    local oldOwner = group.owner
    local oldOwnerDistanceSquared = isElement(oldOwner) and getGroupDistanceSquaredToPlayer(group, oldOwner) or false
    local newOwnerDistanceSquared = isElement(newOwner) and getGroupDistanceSquaredToPlayer(group, newOwner) or false
    local memberIds = {}
    for _, record in ipairs(group.members) do memberIds[#memberIds + 1] = record.id end

    group.pendingOwner = newOwner
    group.handoffDeadline = getTickCount() + config.handoffTimeout
    setGroupState(group, "revoking")
    for _, record in ipairs(group.members) do
        if isElement(record.ped) then
            setElementFrozen(record.ped, true)
        end
    end
    if isElement(group.owner) then
        triggerClientEvent(group.owner, "pedTraffic:revokeGroup", resourceRoot, group.id, group.epoch, reason)
        log(("group-revoke group=%d epoch=%d old=%s new=%s reason=%s"):format(
                group.id, group.epoch, getPlayerName(group.owner), getPlayerName(newOwner), tostring(reason)))
    else
        finishGroupHandoff(group, "group-owner-departed")
    end
    writePopulationTrace("group_handoff_started", {
        group_id = group.id,
        epoch = group.epoch,
        member_ids = memberIds,
        old_owner_id = isElement(oldOwner) and getPopulationClientId(oldOwner) or false,
        new_owner_id = isElement(newOwner) and getPopulationClientId(newOwner) or false,
        old_owner_distance = oldOwnerDistanceSquared and math.sqrt(oldOwnerDistanceSquared) or false,
        new_owner_distance = newOwnerDistanceSquared and math.sqrt(newOwnerDistanceSquared) or false,
        reason = tostring(reason),
    })
end

-- The ordinary three-second hysteresis prevents ownership ping-pong while two
-- players remain inside the same population bubble. It is not safe once the
-- current owner has left that bubble: GTA can unload the old collision sector
-- before the delayed handoff, allowing an authoritative ped to fall and export
-- that invalid transform. Check this narrow safety condition frequently and
-- transfer immediately to an already-resident peer, or suspend without a
-- syncer when none exists. The slower lifecycle loop below owns resumption,
-- ordinary closer-owner arbitration and despawn decisions.
setTimer(function()
    if not enabled then
        return
    end

    local groups = {}
    for _, group in pairs(trafficGroups) do groups[#groups + 1] = group end
    for _, group in ipairs(groups) do
        if not group.removing and group.state == "active" then
            local closest = findClosestGroupResident(group, nil, true)
            if not closest then
                beginGroupSuspension(group, "group-no-collision-resident")
            elseif closest ~= group.owner then
                local ownerRadii = isEligiblePlayer(group.owner) and getPopulationRadii(populationProfiles[group.owner]) or false
                local ownerDistanceSquared = ownerRadii and getGroupDistanceSquaredToPlayer(group, group.owner) or false
                if not ownerDistanceSquared or ownerDistanceSquared > ownerRadii.gang * ownerRadii.gang then
                    beginGroupHandoff(group, closest, "group-owner-left-residency")
                end
            end
        end
    end

    local records = {}
    for _, record in pairs(trafficPeds) do records[#records + 1] = record end
    for _, record in ipairs(records) do
        if not record.group and not record.removing and record.state == "active" and isElement(record.ped) then
            local x, y, z = getElementPosition(record.ped)
            local closest = findClosestPopulationResident(x, y, z, record.populationClass)
            if not closest then
                beginSuspension(record, "no-collision-resident")
            elseif closest ~= record.owner then
                local ownerRadius = isEligiblePlayer(record.owner) and
                                        getPopulationRadiusForClass(populationProfiles[record.owner], record.populationClass) or false
                local ownerX, ownerY
                if isElement(record.owner) then
                    ownerX, ownerY = getElementPosition(record.owner)
                end
                local ownerDistanceSquared = ownerRadius and ownerX and squaredDistance2D(x, y, ownerX, ownerY) or false
                if not ownerDistanceSquared or ownerDistanceSquared > ownerRadius * ownerRadius then
                    beginHandoff(record, closest, "owner-left-residency")
                end
            end
        end
    end
end, 100, 0)

local function validateCandidate(player, candidate)
    if type(candidate) ~= "table" or not isFiniteNumber(candidate.x) or not isFiniteNumber(candidate.y) or
        not isFiniteNumber(candidate.z) or not isFiniteNumber(candidate.model) or not isFiniteNumber(candidate.pedType) or
        not isFiniteNumber(candidate.direction) or (candidate.populationClass == "gang" and not isFiniteNumber(candidate.heading)) then
        return false, "shape"
    end

    local model = candidate.model
    local pedType = candidate.pedType
    local direction = candidate.direction
    local populationClass = candidate.populationClass
    local gang = candidate.gang
    if not isIntegerInRange(model, 7, 288) or not isIntegerInRange(pedType, 4, 16) or
        not isIntegerInRange(direction, 0, 7) or (populationClass == "gang" and (candidate.heading < -360 or candidate.heading > 360)) then
        return false, "candidate-contract"
    end
    if populationClass == "civilian" then
        if civilianPedTypeByModel[model] ~= pedType or (gang ~= false and gang ~= -1) then
            return false, "civilian-contract"
        end
        gang = false
    elseif populationClass == "gang" then
        if pedType < 7 or pedType > 16 or not isIntegerInRange(gang, 0, 9) or gang ~= pedType - 7 or gangByModel[model] ~= gang then
            return false, "gang-contract"
        end
    else
        return false, "population-class"
    end

    local profile = populationProfiles[player]
    if not profile then
        return false, "population-profile-missing"
    end
    local playerX, playerY, playerZ = getElementPosition(player)
    local distanceSquared = squaredDistance2D(candidate.x, candidate.y, playerX, playerY)
    local visibleMaximum = profile.creationDistanceMultiplier * profile.generationDistanceMultiplier * 50.5 +
        (populationClass == "gang" and 30 or 0)
    local hiddenMinimum = math.max(0, profile.creationDistanceMultiplier * 25 - 10)
    local hiddenMaximum = profile.creationDistanceMultiplier * 25
    local maximum = math.max(visibleMaximum, hiddenMaximum) + config.maximumGangGroupSpan
    if distanceSquared < math.max(0, hiddenMinimum - config.maximumGangGroupSpan) ^ 2 or distanceSquared > maximum * maximum or
        math.abs(candidate.z - playerZ) > 35 then
        return false, "distance"
    end

    local cellX, cellY = cellForPosition(candidate.x, candidate.y)
    if countPedsInCell(cellX, cellY) >= config.maxPerCell then
        return false, "cell-full"
    end
    if hasNearbyTrafficPed(candidate.x, candidate.y, candidate.z) then
        return false, "separation"
    end
    return true, model, direction, populationClass, gang
end

local function validateGroupCandidate(player, candidate, selection)
    local members = type(candidate) == "table" and (candidate.members or candidate) or false
    local maximum = type(selection) == "table" and tonumber(selection.maximumGroupMembers) or 0
    if type(members) ~= "table" or #members < config.minimumGangGroupSize or
        #members > config.maximumGangGroupSize or #members > maximum then
        return false, "group-size"
    end

    local validated = {}
    local plannedCells = {}
    local anchorX, anchorY, anchorZ
    local maximumSpanSquared = config.maximumGangGroupSpan * config.maximumGangGroupSpan
    for index, candidateMember in ipairs(members) do
        local valid, modelOrReason, direction, populationClass, gang = validateCandidate(player, candidateMember)
        if not valid then
            return false, "group-member-" .. tostring(index) .. ":" .. tostring(modelOrReason)
        end
        if populationClass ~= "gang" or gang ~= selection.gang then
            return false, "group-population-hint-mismatch"
        end
        if index == 1 then
            anchorX, anchorY, anchorZ = candidateMember.x, candidateMember.y, candidateMember.z
        elseif squaredDistance(candidateMember.x, candidateMember.y, candidateMember.z, anchorX, anchorY, anchorZ) > maximumSpanSquared then
            return false, "group-span"
        end
        for previousIndex = 1, index - 1 do
            local previous = members[previousIndex]
            if squaredDistance(candidateMember.x, candidateMember.y, candidateMember.z, previous.x, previous.y, previous.z) < 0.5 * 0.5 then
                return false, "group-member-overlap"
            end
        end

        local cellX, cellY = cellForPosition(candidateMember.x, candidateMember.y)
        local cellKey = tostring(cellX) .. ":" .. tostring(cellY)
        plannedCells[cellKey] = (plannedCells[cellKey] or 0) + 1
        if countPedsInCell(cellX, cellY) + plannedCells[cellKey] > config.maxPerCell then
            return false, "group-cell-full"
        end
        validated[index] = {
            candidate = candidateMember,
            model = modelOrReason,
            direction = direction,
        }
    end
    return true, validated
end

local function spawnGangGroup(player, candidate, selection)
    if not enabled or not isEligiblePlayer(player) then
        return false, "runtime-unavailable"
    end
    if countNativeGroupsForOwner(player) >= config.maximumNativeGangGroups then
        return false, "native-group-cap"
    end
    local valid, validatedOrReason = validateGroupCandidate(player, candidate, selection)
    if not valid then
        return false, validatedOrReason
    end
    local validated = validatedOrReason
    local memberCount = #validated
    if getTrafficPedCount() + memberCount > config.globalCap or
        #getElementsByType("ped") + memberCount > config.pedPoolSoftLimit then
        return false, "group-capacity-changed"
    end
    local stillNeeded, staleReason = isSelectionStillNeeded(player, selection)
    if not stillNeeded then
        return false, staleReason
    end

    nextGroupId = nextGroupId + 1
    local group = {
        id = nextGroupId,
        gang = selection.gang,
        members = {},
        owner = nil,
        epoch = 0,
        state = "created",
        createdAt = getTickCount(),
    }
    trafficGroups[group.id] = group

    for index, member in ipairs(validated) do
        local candidateMember = member.candidate
        local ped = createPed(member.model, candidateMember.x, candidateMember.y, candidateMember.z, candidateMember.heading)
        if not ped then
            removeGroup(group, "group-create-ped-refused")
            return false, "group-create-ped"
        end
        nextPedId = nextPedId + 1
        local record = {
            id = nextPedId,
            ped = ped,
            owner = nil,
            epoch = 0,
            direction = member.direction,
            populationClass = "gang",
            gang = selection.gang,
            state = "created",
            createdAt = group.createdAt,
            group = group,
            groupIndex = index,
        }
        group.members[index] = record
        trafficPeds[ped] = record
        setElementDimension(ped, 0)
        setElementInterior(ped, 0)
        setElementData(ped, "neon:ambientPedTraffic", true)
        setElementData(ped, "neon:ambientPedTrafficId", record.id)
        setElementData(ped, "neon:ambientPedPopulationClass", "gang")
        setElementData(ped, "neon:ambientPedGang", selection.gang)
        setElementData(ped, "neon:ambientPedGroupId", group.id)
        setElementData(ped, "neon:ambientPedGroupIndex", index)
        setElementData(ped, "neon:ambientPedGroupRole", index == 1 and "leader" or "member")
        if not setPedFightingStyle(ped, 4) then
            removeGroup(group, "group-vanilla-fighting-style-refused")
            return false, "group-vanilla-fighting-style"
        end
        if not setPedUseNativeWalkingStyle(ped, true) then
            removeGroup(group, "group-native-walking-style-refused")
            return false, "group-native-walking-style"
        end
    end
    group.leader = group.members[1]

    local fadingPeds = {}
    for _, record in ipairs(group.members) do fadingPeds[#fadingPeds + 1] = record.ped end
    beginSpawnFade(fadingPeds)

    local centreX, centreY, centreZ = getGroupCentre(group)
    local owner = findClosestGroupResident(group, nil, true)
    if not owner then
        removeGroup(group, "group-no-owner")
        return false, "group-no-owner"
    end
    if not assignGroupOwner(group, owner, "group-spawn") then
        removeGroup(group, "group-owner-capacity-changed")
        return false, "group-assign-owner"
    end

    group.counted = true
    stats.groupSpawns = stats.groupSpawns + 1
    stats.spawned = stats.spawned + memberCount
    local models = {}
    local modelIds = {}
    local memberIds = {}
    local spawnTransforms = {}
    for _, member in ipairs(validated) do
        stats.spawnedModels[member.model] = (stats.spawnedModels[member.model] or 0) + 1
        models[#models + 1] = tostring(member.model)
        modelIds[#modelIds + 1] = member.model
    end
    for _, record in ipairs(group.members) do
        memberIds[#memberIds + 1] = record.id
        local x, y, z = getElementPosition(record.ped)
        local _, _, heading = getElementRotation(record.ped)
        spawnTransforms[#spawnTransforms + 1] = ("id=%d:(%.2f,%.2f,%.2f)@%.1f"):format(record.id, x, y, z, heading)
    end
    log(("group-spawn group=%d gang=%d members=%d models=%s owner=%s epoch=%d target=%.2f live=%d pos=%.1f,%.1f,%.1f"):format(
            group.id, group.gang, memberCount, table.concat(models, "/"), getPlayerName(owner), group.epoch,
            selection.gangTarget, selection.totalGangCount, centreX, centreY, centreZ), true)
    log(("group-spawn-transform group=%d epoch=%d members=[%s]"):format(
            group.id, group.epoch, table.concat(spawnTransforms, ";")), true)
    writePopulationTrace("group_spawn", {
        request_id = selection.requestId,
        player_id = getPopulationClientId(player),
        group_id = group.id,
        gang = group.gang,
        member_count = memberCount,
        member_ids = memberIds,
        models = modelIds,
        target = selection.gangTarget,
        live_before = selection.totalGangCount,
        position = {x = centreX, y = centreY, z = centreZ},
    })
    return true
end

local function spawnCandidate(player, candidate, selection)
    if not enabled or not isEligiblePlayer(player) or getTrafficPedCount() >= config.globalCap or
        #getElementsByType("ped") >= config.pedPoolSoftLimit then
        return false, "runtime-unavailable"
    end

    local valid, modelOrReason, direction, populationClass, gang = validateCandidate(player, candidate)
    if not valid then
        return false, modelOrReason
    end
    if type(selection) ~= "table" or populationClass ~= selection.populationClass or gang ~= selection.gang then
        return false, "population-hint-mismatch"
    end
    local stillNeeded, staleReason = isSelectionStillNeeded(player, selection)
    if not stillNeeded then
        return false, staleReason
    end

    local owner = findClosestPopulationResident(candidate.x, candidate.y, candidate.z, populationClass)
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
        populationClass = populationClass,
        gang = gang,
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
    setElementData(ped, "neon:ambientPedPopulationClass", populationClass)
    setElementData(ped, "neon:ambientPedGang", gang)
    beginSpawnFade({ped})

    if not setPedFightingStyle(ped, 4) then
        removeRecord(record, "vanilla-fighting-style-refused")
        return false, "vanilla-fighting-style"
    end

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
    stats.spawnedModels[modelOrReason] = (stats.spawnedModels[modelOrReason] or 0) + 1
    log(("spawn id=%d model=%d class=%s gang=%s owner=%s target=%.2f/%.2f live=%d/%d deficit=%.2f/%.2f roll=%.2f/%.2f gangScore=%.3f pos=%.1f,%.1f,%.1f"):format(
            record.id, modelOrReason, populationClass, tostring(gang), getPlayerName(owner), selection.civilianTarget,
            selection.gangTarget, selection.civilianCount, selection.totalGangCount, selection.civilianDeficit, selection.gangDeficit,
            selection.civilianChance, selection.gangChance, selection.gangScore or 0, candidate.x, candidate.y, candidate.z))
    writePopulationTrace("spawn", {
        request_id = selection.requestId,
        player_id = getPopulationClientId(player),
        traffic_id = record.id,
        model = modelOrReason,
        population_class = populationClass,
        gang = gang,
        targets = {civilian = selection.civilianTarget, gang = selection.gangTarget},
        live_before = {civilian = selection.civilianCount, gang = selection.totalGangCount},
        deficits = {civilian = selection.civilianDeficit, gang = selection.gangDeficit},
        position = {x = candidate.x, y = candidate.y, z = candidate.z},
    })
    return true
end

local function getCandidateVisibilityProbes(candidate, populationClass)
    local source = populationClass == "gang" and type(candidate) == "table" and (candidate.members or candidate) or {candidate}
    local probes = {}
    for _, member in ipairs(source) do
        if type(member) == "table" and isFiniteNumber(member.x) and isFiniteNumber(member.y) and isFiniteNumber(member.z) then
            probes[#probes + 1] = {x = member.x, y = member.y, z = member.z, radius = 2}
        end
    end
    return probes
end

local function finishCandidateVisibilityCheck(check, reason)
    if not check or pendingVisibilityChecks[check.id] ~= check then
        return
    end
    pendingVisibilityChecks[check.id] = nil

    -- Every started camera check needs one explicit terminal row. The trace
    -- analyzer can then distinguish a completed veto from a probe which was
    -- lost between owner, server and observer without inferring it from the
    -- later spawn/rejection event.
    writePopulationTrace("visibility_check_result", {
        visibility_check_id = check.id,
        request_id = check.request.id,
        kind = check.kind,
        player_id = getPopulationClientId(check.player),
        allowed = reason == nil,
        reason = reason or "all-clear",
        votes = check.voteCount or 0,
        expected_votes = check.voterCount or 0,
    })

    if reason then
        stats.rejected = stats.rejected + 1
        countReason(stats.rejectionReasons, reason)
        writePopulationTrace("candidate_rejected", {
            visibility_check_id = check.id,
            request_id = check.request.id,
            player_id = getPopulationClientId(check.player),
            population_class = check.request.selection.populationClass,
            gang = check.request.selection.gang,
            reason = reason,
        })
        return
    end

    local created, spawnReason
    if check.request.selection.populationClass == "gang" then
        created, spawnReason = spawnGangGroup(check.player, check.candidate, check.request.selection)
    else
        created, spawnReason = spawnCandidate(check.player, check.candidate, check.request.selection)
    end
    if not created then
        stats.rejected = stats.rejected + 1
        countReason(stats.rejectionReasons, spawnReason)
        writePopulationTrace("candidate_rejected", {
            visibility_check_id = check.id,
            request_id = check.request.id,
            player_id = getPopulationClientId(check.player),
            population_class = check.request.selection.populationClass,
            gang = check.request.selection.gang,
            reason = tostring(spawnReason),
        })
    end
end

local function beginCandidateVisibilityCheck(player, candidate, request)
    local valid, reason
    if request.selection.populationClass == "gang" then
        valid, reason = validateGroupCandidate(player, candidate, request.selection)
    else
        valid, reason = validateCandidate(player, candidate)
    end
    if not valid then
        return false, reason
    end

    local probes = getCandidateVisibilityProbes(candidate, request.selection.populationClass)
    if #probes == 0 then
        return false, "visibility-no-probes"
    end
    local voters = {}
    for _, voter in ipairs(getEligiblePlayers()) do
        -- PedCreationDistMultiplier is stock-clamped to 1.0..1.5. Use the
        -- maximum only to bound network voters; each client applies its exact
        -- current multiplier before querying GTA's camera.
        local threshold = 1.5 * 42.5 + (request.selection.populationClass == "gang" and 30 or 0)
        local playerX, playerY = getElementPosition(voter)
        for _, probe in ipairs(probes) do
            if squaredDistance2D(playerX, playerY, probe.x, probe.y) < threshold * threshold then
                voters[#voters + 1] = voter
                break
            end
        end
    end

    nextVisibilityCheckId = nextVisibilityCheckId + 1
    local check = {
        id = nextVisibilityCheckId,
        kind = "candidate",
        player = player,
        candidate = candidate,
        request = request,
        awaiting = {},
        voterCount = #voters,
        voteCount = 0,
        deadline = getTickCount() + config.visibilityTimeout,
    }
    pendingVisibilityChecks[check.id] = check
    writePopulationTrace("visibility_check_started", {
        visibility_check_id = check.id,
        request_id = request.id,
        kind = check.kind,
        player_id = getPopulationClientId(player),
        population_class = request.selection.populationClass,
        voters = #voters,
        probes = #probes,
    })
    if #voters == 0 then
        writePopulationTrace("visibility_check_skipped", {
            visibility_check_id = check.id,
            request_id = request.id,
            kind = check.kind,
            player_id = getPopulationClientId(player),
            reason = "no-nearby-camera",
        })
        finishCandidateVisibilityCheck(check)
        return true
    end
    for _, voter in ipairs(voters) do
        check.awaiting[voter] = true
        triggerClientEvent(voter, "pedTraffic:visibilityProbe", resourceRoot, check.id, "candidate", probes,
                           request.selection.populationClass)
    end
    return true
end

local function finishRemovalVisibilityCheck(check, visible, reason)
    if not check or pendingVisibilityChecks[check.id] ~= check then
        return
    end
    pendingVisibilityChecks[check.id] = nil
    local target = check.target
    if target and target.visibilityCheckId == check.id then
        target.visibilityCheckId = nil
    end
    if check.kind == "group-removal" then
        if not target or target.removing or (target.state ~= "active" and target.state ~= "suspended") then
            writePopulationTrace("removal_visibility_result", {
                visibility_check_id = check.id,
                kind = check.kind,
                group_id = target and target.id or false,
                visible = true,
                reason = reason or "target-state-changed",
            })
            return
        end
        if visible then
            target.visibilityRetryAt = getTickCount() + 1000
        else
            removeGroup(target, check.reason)
        end
    else
        if not target or target.removing or (target.state ~= "active" and target.state ~= "suspended") or not isElement(target.ped) then
            writePopulationTrace("removal_visibility_result", {
                visibility_check_id = check.id,
                kind = check.kind,
                traffic_id = target and target.id or false,
                visible = true,
                reason = reason or "target-state-changed",
            })
            return
        end
        if visible then
            target.visibilityRetryAt = getTickCount() + 1000
        else
            removeRecord(target, check.reason)
        end
    end
    writePopulationTrace("removal_visibility_result", {
        visibility_check_id = check.id,
        kind = check.kind,
        traffic_id = check.kind == "ped-removal" and target.id or false,
        group_id = check.kind == "group-removal" and target.id or false,
        visible = visible == true,
        reason = reason or check.reason,
    })
end

local function beginRemovalVisibilityCheck(target, reason)
    if not target or target.removing or target.visibilityCheckId or next(pendingVisibilityChecks) or
        getTickCount() < (target.visibilityRetryAt or 0) then
        return false
    end

    local elements = {}
    local kind
    if target.members then
        kind = "group-removal"
        for _, record in ipairs(target.members) do
            if isElement(record.ped) then
                elements[#elements + 1] = record.ped
            end
        end
    elseif isElement(target.ped) then
        kind = "ped-removal"
        elements[1] = target.ped
    end
    if not kind or #elements == 0 then
        return false
    end

    local voters = getEligiblePlayers()
    if #voters == 0 then
        if kind == "group-removal" then
            removeGroup(target, reason)
        else
            removeRecord(target, reason)
        end
        return true
    end

    nextVisibilityCheckId = nextVisibilityCheckId + 1
    local check = {
        id = nextVisibilityCheckId,
        kind = kind,
        target = target,
        reason = reason,
        awaiting = {},
        deadline = getTickCount() + config.visibilityTimeout,
    }
    target.visibilityCheckId = check.id
    pendingVisibilityChecks[check.id] = check
    for _, voter in ipairs(voters) do
        check.awaiting[voter] = true
        triggerClientEvent(voter, "pedTraffic:visibilityProbe", resourceRoot, check.id, kind, elements, false)
    end
    writePopulationTrace("visibility_check_started", {
        visibility_check_id = check.id,
        kind = kind,
        traffic_id = kind == "ped-removal" and target.id or false,
        group_id = kind == "group-removal" and target.id or false,
        voters = #voters,
        probes = #elements,
        reason = reason,
    })
    return true
end

local function clearTraffic(reason)
    -- Close every asynchronous lane before destroying its elements. Besides
    -- preventing stale target check IDs, this gives the causal JSONL trace a
    -- terminal outcome when traffic is disabled, reset or stopped mid-probe.
    local visibilityChecks = {}
    for _, check in pairs(pendingVisibilityChecks) do visibilityChecks[#visibilityChecks + 1] = check end
    for _, check in ipairs(visibilityChecks) do
        if check.kind == "candidate" then
            finishCandidateVisibilityCheck(check, tostring(reason) .. "-visibility-cancelled")
        else
            finishRemovalVisibilityCheck(check, true, tostring(reason) .. "-visibility-cancelled")
        end
    end
    for player, request in pairs(pendingRequests) do
        stats.rejected = stats.rejected + 1
        countReason(stats.rejectionReasons, tostring(reason) .. "-request-cancelled")
        writePopulationTrace("candidate_rejected", {
            request_id = request.id,
            player_id = isElement(player) and getPopulationClientId(player) or false,
            population_class = request.selection and request.selection.populationClass or false,
            gang = request.selection and request.selection.gang or false,
            reason = tostring(reason) .. "-request-cancelled",
        })
    end

    local records = {}
    for _, record in pairs(trafficPeds) do
        records[#records + 1] = record
    end
    for _, record in ipairs(records) do
        removeRecord(record, reason)
    end
    pendingRequests = {}
    pendingVisibilityChecks = {}
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
            populationProfiles = {}
            populationWorldRevisions = {}
        end
        return
    end
    enabled = value
    if enabled then
        sendPopulationWorldState(root)
    end
    if not enabled then
        if stopResidencyTest then
            stopResidencyTest("CANCEL", "traffic-disabled")
        end
        clearTraffic("disabled")
        clearTestVehicles()
        populationProfiles = {}
        populationWorldRevisions = {}
    end
    triggerClientEvent(root, "pedTraffic:setEnabled", resourceRoot, enabled, debugEnabled)
    log(("enabled=%s actor=%s"):format(tostring(enabled), isElement(actor) and getPlayerName(actor) or "console"), true)
end

local RESIDENCY_TEST_DROP_TOLERANCE = 0.5
local RESIDENCY_TEST_SAMPLE_MAX_AGE = 500
local RESIDENCY_TEST_PHASE_TIMEOUT = 7000

local function getResidencyTestGroup()
    local selected = false
    for _, group in pairs(trafficGroups) do
        local valid = not group.removing and group.state == "active" and isElement(group.owner) and not group.visibilityCheckId and
            #group.members >= config.minimumGangGroupSize
        if valid then
            for _, record in ipairs(group.members) do
                if not isElement(record.ped) or isPedDead(record.ped) or record.airTest or record.climbTest then
                    valid = false
                    break
                end
            end
        end
        if valid and (not selected or group.id < selected.id) then
            selected = group
        end
    end
    return selected
end

local function getResidencyTestSample(test, player)
    local sample = test.samples[player]
    return sample and getTickCount() - sample.receivedAt <= RESIDENCY_TEST_SAMPLE_MAX_AGE and sample or false
end

local function isResidencyOwnerReady(test, player)
    local sample = getResidencyTestSample(test, player)
    if not sample or sample.epoch ~= test.group.epoch then
        return false
    end
    for _, record in ipairs(test.group.members) do
        local row = sample.byPed[record.ped]
        if not row or not row.syncer or not row.ready then
            return false
        end
    end
    return true
end

local function isResidencyTestPlayerResident(test, player)
    local distanceSquared = isElement(player) and getGroupDistanceSquaredToPlayer(test.group, player) or false
    local radius = test.residencyRadii[player]
    return distanceSquared and radius and distanceSquared <= radius * radius or false
end

local function moveResidencyTestPlayer(test, player, offset)
    local saved = test.savedPlayers[player]
    if not saved or not isElement(player) then
        return false
    end
    setElementFrozen(player, true)
    return setElementPosition(player, test.centre.x + offset, test.centre.y, test.centre.z + 1)
end

local function traceResidencyTestAction(test, action)
    test.action = action
    test.actionStartedAt = getTickCount()
    writePopulationTrace("residency_test_action", {
        scenario_id = test.id,
        action = action,
        scenario_tick = test.actionStartedAt - test.startedAt,
        group_id = test.group.id,
        owner_id = isElement(test.group.owner) and getPopulationClientId(test.group.owner) or false,
        epoch = test.group.epoch,
    })
    log(("residency action=%s scenario=%d group=%d owner=%s epoch=%d"):format(
            action, test.id, test.group.id,
            isElement(test.group.owner) and getPlayerName(test.group.owner) or "none", test.group.epoch), true)
end

stopResidencyTest = function(outcome, reason)
    local test = residencyTest
    residencyTest = false
    if not test then
        return
    end
    if isTimer(test.timer) then
        killTimer(test.timer)
    end
    if test.group and test.group.residencyTestId == test.id then
        test.group.residencyTestId = nil
    end
    for _, player in ipairs(test.players) do
        if isElement(player) then
            triggerClientEvent(player, "pedTraffic:residencyStop", resourceRoot, test.id)
            local saved = test.savedPlayers[player]
            if saved then
                setElementDimension(player, saved.dimension)
                setElementInterior(player, saved.interior)
                setElementPosition(player, saved.x, saved.y, saved.z)
                setElementRotation(player, saved.rx, saved.ry, saved.rz)
                setElementFrozen(player, saved.frozen)
            end
        end
    end
    writePopulationTrace("residency_test_result", {
        scenario_id = test.id,
        result = outcome,
        reason = tostring(reason),
        action = test.action,
        scenario_tick = getTickCount() - test.startedAt,
        group_id = test.group and test.group.id or false,
        owner_id = test.group and isElement(test.group.owner) and getPopulationClientId(test.group.owner) or false,
        epoch = test.group and test.group.epoch or false,
    })
    local message = ("Residency test %s: %s (scenario %d)"):format(outcome, tostring(reason), test.id)
    outputChatBox(message, root, outcome == "PASS" and 80 or 255, outcome == "PASS" and 220 or 80, 120)
    log(message, true)
end

local function failResidencyTest(reason)
    stopResidencyTest("FAIL", reason)
end

local function traceResidencyServerSample(test)
    local peds = {}
    for _, record in ipairs(test.group.members) do
        if not isElement(record.ped) then
            return false
        end
        local _, _, z = getElementPosition(record.ped)
        local _, _, vz = getElementVelocity(record.ped)
        peds[#peds + 1] = {
            traffic_id = record.id,
            z = z,
            velocity_z = vz,
            frozen = isElementFrozen(record.ped),
        }
        local baseline = test.baselineServerZ[record.ped]
        if baseline and z < baseline - RESIDENCY_TEST_DROP_TOLERANCE then
            failResidencyTest(("server-z-drop:id=%d baseline=%.3f z=%.3f"):format(record.id, baseline, z))
            return false
        end
    end
    local clients = {}
    for _, player in ipairs(test.players) do
        local distanceSquared = isElement(player) and getGroupDistanceSquaredToPlayer(test.group, player) or false
        local distance = distanceSquared and math.sqrt(distanceSquared) or false
        clients[#clients + 1] = {
            client_id = isElement(player) and getPopulationClientId(player) or false,
            distance = distance,
            residency_radius = test.residencyRadii[player],
            resident = distance and distance <= test.residencyRadii[player] or false,
            frozen = isElement(player) and isElementFrozen(player) or false,
        }
    end
    writePopulationTrace("residency_test_sample", {
        scenario_id = test.id,
        source = "server",
        action = test.action,
        scenario_tick = getTickCount() - test.startedAt,
        group_id = test.group.id,
        group_state = test.group.state,
        owner_id = isElement(test.group.owner) and getPopulationClientId(test.group.owner) or false,
        epoch = test.group.epoch,
        clients = clients,
        peds = peds,
    })
    return true
end

local function pulseResidencyTest(test)
    if residencyTest ~= test then
        return
    end
    if test.group.removing or trafficGroups[test.group.id] ~= test.group then
        return failResidencyTest("target-group-removed")
    end
    for _, player in ipairs(test.players) do
        if not isElement(player) then
            return failResidencyTest("client-left")
        end
    end
    if not traceResidencyServerSample(test) or residencyTest ~= test then
        return
    end

    local now = getTickCount()
    if now - test.actionStartedAt > RESIDENCY_TEST_PHASE_TIMEOUT then
        return failResidencyTest("phase-timeout:" .. test.action)
    end

    local residentA = isResidencyTestPlayerResident(test, test.ownerA)
    local residentB = isResidencyTestPlayerResident(test, test.ownerB)
    local expectedA = test.action == "prepare" or test.action == "handoff-b-to-a" or test.action == "resume"
    local expectedB = test.action == "prepare" or test.action == "handoff-a-to-b"
    if residentA ~= expectedA or residentB ~= expectedB then
        return failResidencyTest(("resident-layout:%s:a=%s:b=%s"):format(test.action, tostring(residentA), tostring(residentB)))
    end

    if test.action == "prepare" then
        if test.group.state ~= "active" or test.group.owner ~= test.ownerA or not isResidencyOwnerReady(test, test.ownerA) then
            return
        end
        local ownerSample = getResidencyTestSample(test, test.ownerA)
        local observerSample = getResidencyTestSample(test, test.ownerB)
        if not observerSample then
            return
        end
        for _, record in ipairs(test.group.members) do
            local row = ownerSample.byPed[record.ped]
            if not row.grounded or row.phase ~= "none" then
                return failResidencyTest("initial-ped-not-grounded:id=" .. tostring(record.id))
            end
            if observerSample.byPed[record.ped].phase ~= "none" then
                return failResidencyTest("initial-observer-physical-task:id=" .. tostring(record.id))
            end
            local _, _, z = getElementPosition(record.ped)
            test.baselineServerZ[record.ped] = z
            test.baselineClientZ[test.ownerA][record.ped] = row.z
            test.baselineClientZ[test.ownerB][record.ped] = observerSample.byPed[record.ped].z
        end
        test.firstEpoch = test.group.epoch
        moveResidencyTestPlayer(test, test.ownerA, test.farDistance)
        traceResidencyTestAction(test, "handoff-a-to-b")
    elseif test.action == "handoff-a-to-b" then
        if test.group.state == "active" and test.group.owner == test.ownerB and test.group.epoch > test.firstEpoch and
            isResidencyOwnerReady(test, test.ownerB) then
            test.secondEpoch = test.group.epoch
            moveResidencyTestPlayer(test, test.ownerA, test.nearA)
            moveResidencyTestPlayer(test, test.ownerB, -test.farDistance)
            traceResidencyTestAction(test, "handoff-b-to-a")
        end
    elseif test.action == "handoff-b-to-a" then
        if test.group.state == "active" and test.group.owner == test.ownerA and test.group.epoch > test.secondEpoch and
            isResidencyOwnerReady(test, test.ownerA) then
            test.thirdEpoch = test.group.epoch
            moveResidencyTestPlayer(test, test.ownerA, test.farDistance)
            moveResidencyTestPlayer(test, test.ownerB, -test.farDistance)
            traceResidencyTestAction(test, "no-resident")
        end
    elseif test.action == "no-resident" then
        if test.group.state == "suspended" then
            for _, record in ipairs(test.group.members) do
                if not isElementFrozen(record.ped) then
                    return failResidencyTest("suspended-ped-not-frozen:id=" .. tostring(record.id))
                end
            end
            moveResidencyTestPlayer(test, test.ownerA, test.nearA)
            traceResidencyTestAction(test, "resume")
        end
    elseif test.action == "resume" and test.group.state == "active" and test.group.owner == test.ownerA and
        test.group.epoch > test.thirdEpoch and isResidencyOwnerReady(test, test.ownerA) then
        stopResidencyTest("PASS", "handoff-return-suspend-resume")
    end
end

local function startResidencyTest(player)
    if residencyTest then
        outputChatBox("A residency test is already running", player, 255, 160, 80)
        return false
    end
    if not enabled then
        outputChatBox("Run /pedtraffic on and wait for an active grounded group first", player, 255, 160, 80)
        return false
    end
    local players = getEligiblePlayers()
    if #players ~= 2 then
        outputChatBox("The residency test requires exactly two ready clients in dimension/interior 0", player, 255, 160, 80)
        return false
    end
    table.sort(players, function(left, right) return getPopulationClientId(left) < getPopulationClientId(right) end)
    for _, candidate in ipairs(players) do
        if isPedInVehicle(candidate) or not getPopulationRadii(populationProfiles[candidate]) then
            outputChatBox("Both residency-test clients must be on foot with a current population profile", player, 255, 160, 80)
            return false
        end
    end
    local group = getResidencyTestGroup()
    if not group then
        outputChatBox("No stable active native group is available yet; wait for a grounded group and retry", player, 255, 160, 80)
        return false
    end
    local ownerA = group.owner
    local ownerB = players[1] == ownerA and players[2] or players[1]
    if ownerA ~= players[1] and ownerA ~= players[2] then
        outputChatBox("The selected group is not owned by either residency-test client", player, 255, 160, 80)
        return false
    end

    if not debugEnabled then
        debugEnabled = true
        triggerClientEvent(root, "pedTraffic:setDebug", resourceRoot, true)
    end
    resetPopulationTrace()
    nextResidencyTestId = nextResidencyTestId + 1
    local centreX, centreY, centreZ = getGroupCentre(group)
    local radiiA = getPopulationRadii(populationProfiles[ownerA])
    local radiiB = getPopulationRadii(populationProfiles[ownerB])
    local test = {
        id = nextResidencyTestId,
        group = group,
        players = players,
        ownerA = ownerA,
        ownerB = ownerB,
        centre = {x = centreX, y = centreY, z = centreZ},
        nearA = 10,
        farDistance = math.max(radiiA.maximum, radiiB.maximum) + 50,
        startedAt = getTickCount(),
        actionStartedAt = getTickCount(),
        action = "prepare",
        savedPlayers = {},
        samples = {},
        baselineServerZ = {},
        baselineClientZ = {[ownerA] = {}, [ownerB] = {}},
        residencyRadii = {[ownerA] = radiiA.gang, [ownerB] = radiiB.gang},
    }
    residencyTest = test
    -- Keep population rebalancing from deleting the oracle target while the
    -- harness deliberately moves both residents outside its normal bubble.
    group.residencyTestId = test.id
    for _, candidate in ipairs(players) do
        local x, y, z = getElementPosition(candidate)
        local rx, ry, rz = getElementRotation(candidate)
        test.savedPlayers[candidate] = {
            x = x, y = y, z = z, rx = rx, ry = ry, rz = rz,
            dimension = getElementDimension(candidate), interior = getElementInterior(candidate), frozen = isElementFrozen(candidate),
        }
    end
    local peds = {}
    for _, record in ipairs(group.members) do peds[#peds + 1] = record.ped end
    moveResidencyTestPlayer(test, ownerA, test.nearA)
    moveResidencyTestPlayer(test, ownerB, 20)
    for _, candidate in ipairs(players) do
        triggerClientEvent(candidate, "pedTraffic:residencyObserve", resourceRoot, test.id, peds)
    end
    writePopulationTrace("residency_test_started", {
        scenario_id = test.id,
        action = test.action,
        scenario_tick = 0,
        group_id = group.id,
        member_ids = (function()
            local ids = {}
            for _, record in ipairs(group.members) do ids[#ids + 1] = record.id end
            return ids
        end)(),
        owner_id = getPopulationClientId(ownerA),
        second_resident_id = getPopulationClientId(ownerB),
        epoch = group.epoch,
    })
    test.timer = setTimer(pulseResidencyTest, 100, 0, test)
    outputChatBox(("Residency test started: scenario %d, group %d"):format(test.id, group.id), root, 120, 220, 255)
    return true
end

addEvent("pedTraffic:candidate", true)
addEventHandler("pedTraffic:candidate", resourceRoot, function(requestId, worldRevision, candidate, elapsedMs, missReason)
    local player = client
    local request = pendingRequests[player]
    pendingRequests[player] = nil
    if not request or request.id ~= requestId or request.worldRevision ~= worldRevision or worldRevision ~= populationWorld.revision or
        not isPopulationWorldReady(player) or getTickCount() - request.issuedAt > config.requestTimeout or not isEligiblePlayer(player) then
        stats.rejected = stats.rejected + 1
        writePopulationTrace("candidate_rejected", {
            request_id = requestId,
            player_id = isElement(player) and getPopulationClientId(player) or false,
            reason = "stale-or-unauthorized",
        })
        return
    end

    if candidate == false then
        stats.candidateMisses = stats.candidateMisses + 1
        countReason(stats.missReasons, missReason)
        writePopulationTrace("candidate_miss", {
            request_id = requestId,
            player_id = getPopulationClientId(player),
            population_class = request.selection.populationClass,
            gang = request.selection.gang,
            elapsed_ms = elapsedMs,
            reason = tostring(missReason),
        })
        return
    end
    if getTrafficPedCount() >= config.globalCap then
        stats.rejected = stats.rejected + 1
        countReason(stats.rejectionReasons, "global-cap")
        writePopulationTrace("candidate_rejected", {
            request_id = requestId,
            player_id = getPopulationClientId(player),
            reason = "global-cap",
        })
        return
    end

    local started, reason = beginCandidateVisibilityCheck(player, candidate, request)
    if not started then
        stats.rejected = stats.rejected + 1
        countReason(stats.rejectionReasons, reason)
        writePopulationTrace("candidate_rejected", {
            request_id = requestId,
            player_id = getPopulationClientId(player),
            population_class = request.selection.populationClass,
            gang = request.selection.gang,
            elapsed_ms = elapsedMs,
            reason = tostring(reason),
        })
    end
end)

addEvent("pedTraffic:visibilityProbeResult", true)
addEventHandler("pedTraffic:visibilityProbeResult", resourceRoot, function(checkId, visible, detail)
    if not isIntegerInRange(checkId, 1, 2147483647) or type(visible) ~= "boolean" or type(detail) ~= "table" then
        return
    end
    local check = pendingVisibilityChecks[checkId]
    if not check or check.awaiting[client] ~= true or not isEligiblePlayer(client) or not isPopulationWorldReady(client) then
        return
    end
    check.awaiting[client] = nil
    check.voteCount = (check.voteCount or 0) + 1
    writePopulationTrace("visibility_vote", {
        visibility_check_id = check.id,
        request_id = check.request and check.request.id or false,
        kind = check.kind,
        player_id = getPopulationClientId(client),
        visible = visible,
        probe_index = tonumber(detail.probeIndex) or false,
        too_close = detail.tooClose == true,
    })

    if visible then
        if check.kind == "candidate" then
            finishCandidateVisibilityCheck(check, "visible-to-resident")
        else
            finishRemovalVisibilityCheck(check, true, "visible-to-resident")
        end
        return
    end
    if not next(check.awaiting) then
        if check.kind == "candidate" then
            finishCandidateVisibilityCheck(check)
        else
            finishRemovalVisibilityCheck(check, false)
        end
    end
end)

addEvent("pedTraffic:populationProfile", true)
addEventHandler("pedTraffic:populationProfile", resourceRoot, function(profile)
    if not enabled or not isEligiblePlayer(client) or not isPopulationWorldReady(client) then
        populationProfiles[client] = nil
        return
    end

    local validated = validatePopulationProfile(profile)
    if not validated then
        populationProfiles[client] = nil
        return
    end

    local totalTarget, civilianTarget, gangTarget, gangTargets = calculateNativeTargets(validated)
    local targetSignature = {}
    for index = 1, 10 do targetSignature[index] = ("%.3f"):format(gangTargets[index]) end
    validated.signature = ("%d:%.3f:%.3f:%s:%d:%d:%s:%d:%.3f:%.3f:%s:%.3f:%.3f:%d:%.3f:%.3f"):format(
        totalTarget, civilianTarget, gangTarget, table.concat(targetSignature, ","), validated.zoneType, validated.timeIndex,
        tostring(validated.weekend), validated.worldRevision, validated.effectiveCopTarget, validated.dealerTarget, tostring(validated.noCops),
        validated.pedDensityMultiplier, validated.fewerPedsMultiplier, validated.maximumPedsInUse,
        validated.creationDistanceMultiplier, validated.generationDistanceMultiplier)
    local previous = populationProfiles[client]
    if previous and previous.signature == validated.signature then
        validated.stableSince = previous.stableSince
        validated.nextRebalanceAt = previous.nextRebalanceAt
    else
        validated.stableSince = validated.receivedAt
        validated.nextRebalanceAt = validated.receivedAt + 3000
    end
    populationProfiles[client] = validated
    if not previous or previous.signature ~= validated.signature then
        log(("population-profile player=%s revision=%d target=%.1f effective=%.1f supported=%.1f civilian=%.1f gang=%.1f cops=%.1f rawCops=%.1f dealers=%.1f weights=%s zone=%d time=%d weekend=%s noCops=%s"):format(
                getPlayerName(client), validated.worldRevision, validated.target, validated.effectiveTarget, validated.supportedTarget, validated.civilianTarget,
                validated.gangTarget, validated.effectiveCopTarget, validated.rawCopTarget, validated.dealerTarget, table.concat(validated.gangWeights, "/"),
                validated.zoneType, validated.timeIndex, tostring(validated.weekend), tostring(validated.noCops)))
        writePopulationTrace("population_profile", {
            player_id = getPopulationClientId(client),
            target = validated.target,
            supported_target = validated.supportedTarget,
            civilian_target = validated.civilianTarget,
            gang_target = validated.gangTarget,
            cop_target = validated.effectiveCopTarget,
            dealer_target = validated.dealerTarget,
            gang_weights = validated.gangWeights,
            zone_type = validated.zoneType,
            time_index = validated.timeIndex,
            weekend = validated.weekend,
            no_cops = validated.noCops,
            ped_density_multiplier = validated.pedDensityMultiplier,
            fewer_peds_multiplier = validated.fewerPedsMultiplier,
            maximum_peds_in_use = validated.maximumPedsInUse,
            creation_distance_multiplier = validated.creationDistanceMultiplier,
            generation_distance_multiplier = validated.generationDistanceMultiplier,
        })
    end
end)

addEvent("pedTraffic:populationWorldApplied", true)
addEventHandler("pedTraffic:populationWorldApplied", resourceRoot, function(revision, capabilities, success, reason)
    if not enabled or not isEligiblePlayer(client) or revision ~= populationWorld.revision then
        return
    end

    if success == true and type(capabilities) == "table" and capabilities.zones == true then
        populationWorldRevisions[client] = revision
        log(("population-world-ready player=%s revision=%d preset=%s"):format(getPlayerName(client), revision, populationWorld.preset), true)
    else
        populationWorldRevisions[client] = nil
        log(("population-world-failed player=%s revision=%s reason=%s"):format(getPlayerName(client), tostring(revision), tostring(reason)), true)
    end
end)

addEvent("pedTraffic:ready", true)
addEventHandler("pedTraffic:ready", resourceRoot, function()
    populationWorldRevisions[client] = nil
    if enabled then
        sendPopulationWorldState(client)
    end
    triggerClientEvent(client, "pedTraffic:setEnabled", resourceRoot, enabled, debugEnabled)
end)

addEvent("pedTraffic:residencySample", true)
addEventHandler("pedTraffic:residencySample", resourceRoot, function(id, data)
    local test = residencyTest
    if not test or id ~= test.id or (client ~= test.ownerA and client ~= test.ownerB) then
        return
    end
    if type(data) ~= "table" or type(data.error) == "string" then
        return failResidencyTest("client-sample:" .. tostring(type(data) == "table" and data.error or "invalid-shape"))
    end
    if not isIntegerInRange(data.clientTick, 0, 4294967295) or not isIntegerInRange(data.sequence, 1, 2147483647) or
        type(data.peds) ~= "table" or #data.peds ~= #test.group.members then
        return failResidencyTest("client-sample:invalid-header")
    end

    local expected = {}
    for _, record in ipairs(test.group.members) do expected[record.ped] = record end
    local normalized = {receivedAt = getTickCount(), clientTick = data.clientTick, sequence = data.sequence, byPed = {}, epoch = false}
    local traceRows = {}
    for _, row in ipairs(data.peds) do
        local record = type(row) == "table" and expected[row.ped] or false
        if not record or normalized.byPed[row.ped] or not isFiniteNumber(row.z) or not isFiniteNumber(row.velocityZ) or
            not isIntegerInRange(row.epoch, 1, 2147483647) or type(row.phase) ~= "string" or #row.phase > 32 then
            return failResidencyTest("client-sample:invalid-ped")
        end
        local compact = {
            trafficId = record.id,
            epoch = row.epoch,
            z = row.z,
            velocityZ = row.velocityZ,
            frozen = row.frozen == true,
            grounded = row.grounded == true,
            syncer = row.syncer == true,
            ready = row.ready == true,
            rootTask = type(row.rootTask) == "string" and row.rootTask or false,
            leafTask = type(row.leafTask) == "string" and row.leafTask or false,
            phase = row.phase,
        }
        normalized.byPed[row.ped] = compact
        normalized.epoch = normalized.epoch or compact.epoch
        if normalized.epoch ~= compact.epoch then
            return failResidencyTest("client-sample:mixed-epoch")
        end
        traceRows[#traceRows + 1] = {
            traffic_id = compact.trafficId,
            z = compact.z,
            velocity_z = compact.velocityZ,
            frozen = compact.frozen,
            grounded = compact.grounded,
            syncer = compact.syncer,
            ready = compact.ready,
            root_task = compact.rootTask,
            leaf_task = compact.leafTask,
            phase = compact.phase,
        }

        local inAir = compact.phase == "in_air" or compact.phase == "fall" or
            compact.rootTask and compact.rootTask:find("IN_AIR", 1, true) or
            compact.leafTask and compact.leafTask:find("IN_AIR", 1, true)
        if inAir then
            return failResidencyTest(("unexpected-in-air:client=%d ped=%d phase=%s root=%s leaf=%s"):format(
                    getPopulationClientId(client), record.id, compact.phase, tostring(compact.rootTask), tostring(compact.leafTask)))
        end
        local baseline = test.baselineClientZ[client][row.ped]
        if baseline and compact.z < baseline - RESIDENCY_TEST_DROP_TOLERANCE then
            return failResidencyTest(("client-z-drop:client=%d ped=%d baseline=%.3f z=%.3f"):format(
                    getPopulationClientId(client), record.id, baseline, compact.z))
        end
    end
    test.samples[client] = normalized
    writePopulationTrace("residency_test_sample", {
        scenario_id = test.id,
        source = "client",
        client_id = getPopulationClientId(client),
        client_tick = normalized.clientTick,
        sample_sequence = normalized.sequence,
        action = test.action,
        scenario_tick = getTickCount() - test.startedAt,
        group_id = test.group.id,
        group_state = test.group.state,
        owner_id = isElement(test.group.owner) and getPopulationClientId(test.group.owner) or false,
        epoch = test.group.epoch,
        peds = traceRows,
    })

    if test.group.state == "active" and client == test.group.owner and normalized.epoch == test.group.epoch then
        for _, row in pairs(normalized.byPed) do
            if not row.syncer or not row.ready then
                return failResidencyTest(("active-owner-not-ready:client=%d ped=%d epoch=%d"):format(
                        getPopulationClientId(client), row.trafficId, test.group.epoch))
            end
        end
    end
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
        record.suspendedAt = nil
        setElementFrozen(record.ped, false)
        writePopulationTrace("collision_residency_authority_granted", {
            traffic_id = record.id,
            epoch = record.epoch,
            owner_id = getPopulationClientId(client),
        })
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
    elseif evidence == "released" and record.state == "suspending" then
        finishSuspension(record, "suspension-release-ack")
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
            local newOwner = findClosestPopulationResident(x, y, z, record.populationClass, record.owner)
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
            local newOwner = findClosestPopulationResident(x, y, z, record.populationClass, record.owner)
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

addEvent("pedTraffic:groupEvidence", true)
addEventHandler("pedTraffic:groupEvidence", resourceRoot, function(groupId, epoch, evidence, data)
    local group = trafficGroups[tonumber(groupId)]
    if not group or group.removing or group.epoch ~= epoch or client ~= group.owner then
        return
    end
    if evidence == "accepted" and group.state == "assigning" then
        if residencyTest and residencyTest.group == group then
            writePopulationTrace("residency_test_accept", {
                scenario_id = residencyTest.id,
                action = residencyTest.action,
                scenario_tick = getTickCount() - residencyTest.startedAt,
                group_id = group.id,
                owner_id = getPopulationClientId(client),
                epoch = epoch,
                collision_ready = type(data) == "table" and data.collisionReady == true,
            })
            if type(data) ~= "table" or data.collisionReady ~= true then
                failResidencyTest(("accepted-without-collision-ready:client=%d epoch=%d"):format(getPopulationClientId(client), epoch))
            end
        end
        setGroupState(group, "active")
        group.acceptedAt = getTickCount()
        group.suspendedAt = nil
        for _, record in ipairs(group.members) do
            if isElement(record.ped) then
                setElementFrozen(record.ped, false)
            end
        end
        writePopulationTrace("group_collision_residency_authority_granted", {
            group_id = group.id,
            epoch = group.epoch,
            owner_id = getPopulationClientId(client),
        })
        log(("group-accepted group=%d epoch=%d gang=%d members=%d owner=%s"):format(
                group.id, group.epoch, group.gang, #group.members, getPlayerName(client)), true)
        for _, record in ipairs(group.members) do
            for _, player in ipairs(getEligiblePlayers()) do
                if hasValidGunAimContext(player, record.ped, true) then
                    bridgeGunAim(record, player)
                end
            end
        end
    elseif evidence == "released" and group.state == "revoking" then
        finishGroupHandoff(group, "group-release-ack")
    elseif evidence == "released" and group.state == "suspending" then
        finishGroupSuspension(group, "group-suspension-release-ack")
    elseif evidence == "failure" then
        local diagnostic = type(data) == "table" and type(data.nativeDiagnostic) == "table" and data.nativeDiagnostic or false
        log(("group-client-failure group=%d epoch=%d owner=%s reason=%s nativeReason=%s nativeGroup=%s slotActive=%s tracked=%s"):format(
                group.id, group.epoch, getPlayerName(client), type(data) == "table" and tostring(data.reason) or "unknown",
                diagnostic and tostring(diagnostic.reason) or "unavailable", diagnostic and tostring(diagnostic.nativeGroupId) or "unavailable",
                diagnostic and tostring(diagnostic.slotActive) or "unavailable",
                diagnostic and tostring(diagnostic.hasTrackedMember) or "unavailable"), true)
        removeGroup(group, "group-client-failure")
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
    record.lastInteractionAt = getTickCount()
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

    record.lastInteractionAt = getTickCount()
    bridgeDamageResponse(record, client, math.floor(weapon), math.floor(bodypart))
end)

addEvent("pedTraffic:nativePlayerDamageObserved", true)
addEventHandler("pedTraffic:nativePlayerDamageObserved", resourceRoot,
                function(attackingPed, victim, epoch, nonce, weapon, bodypart, damageFactor, direction)
    local record = trafficPeds[attackingPed]
    epoch = tonumber(epoch)
    nonce = tonumber(nonce)
    weapon = tonumber(weapon)
    bodypart = tonumber(bodypart)
    damageFactor = tonumber(damageFactor)
    direction = tonumber(direction)
    if not record or record.removing or record.state ~= "active" or client ~= record.owner or
        getElementSyncer(attackingPed) ~= client or not isElement(victim) or getElementType(victim) ~= "player" or victim == client or
        getElementHealth(victim) <= 0 or not isIntegerInRange(epoch, 1, 2147483647) or epoch ~= record.epoch or
        not isIntegerInRange(nonce, 1, 2147483647) or not isIntegerInRange(weapon, 0, 15) or
        (bodypart ~= 0 and not isIntegerInRange(bodypart, 3, 9)) or not isIntegerInRange(damageFactor, 1, 200) or
        not isIntegerInRange(direction, 0, 3) then
        return
    end

    if record.group and (record.group.removing or record.group.state ~= "active" or record.group.owner ~= client or
        record.group.epoch ~= epoch) then
        return
    end

    -- The raw factor comes from GTA's synchronous GenerateDamageEvent hook,
    -- but remains untrusted network input. Authenticate the exclusive owner,
    -- epoch, canonical melee weapon, collision envelope and cadence, then
    -- require one of the factors reachable by this traffic ped's stock class.
    local canonicalWeapon = getPedWeapon(attackingPed)
    if canonicalWeapon ~= math.floor(weapon) or
        not isCanonicalTrafficMeleeDamage(record, canonicalWeapon, math.floor(damageFactor)) then
        return
    end

    local px, py, pz = getElementPosition(attackingPed)
    local vx, vy, vz = getElementPosition(victim)
    if getElementDimension(attackingPed) ~= getElementDimension(victim) or
        getElementInterior(attackingPed) ~= getElementInterior(victim) or
        squaredDistance(px, py, pz, vx, vy, vz) > config.nativeMeleeDamageRadius * config.nativeMeleeDamageRadius then
        return
    end

    local now = getTickCount()
    if record.nativePlayerDamageOwner == client and record.nativePlayerDamageEpoch == epoch then
        if nonce <= (record.nativePlayerDamageNonce or 0) or now - (record.nativePlayerDamageAt or 0) < config.nativeMeleeDamageInterval then
            return
        end
    end

    record.nativePlayerDamageOwner = client
    record.nativePlayerDamageEpoch = epoch
    record.nativePlayerDamageNonce = nonce
    record.nativePlayerDamageAt = now
    record.lastInteractionAt = now
    log(("native-player-damage id=%d epoch=%d nonce=%d owner=%s victim=%s weapon=%d bodypart=%d factor=%d direction=%d"):format(
            record.id, epoch, nonce, getPlayerName(client), getPlayerName(victim), canonicalWeapon, math.floor(bodypart),
            math.floor(damageFactor), math.floor(direction)))
    triggerClientEvent(victim, "pedTraffic:nativePlayerDamage", resourceRoot, attackingPed, epoch, nonce,
                       canonicalWeapon, math.floor(bodypart), math.floor(damageFactor), math.floor(direction))
end)

addEventHandler("onPedWasted", root, function()
    local record = trafficPeds[source]
    if not record then
        return
    end
    if record.group and record.group.leader == record then
        local replacement = false
        for _, member in ipairs(record.group.members) do
            if member ~= record and isElement(member.ped) and not isPedDead(member.ped) then
                replacement = member
                break
            end
        end
        if replacement then
            record.group.leader = replacement
            stats.groupPromotions = stats.groupPromotions + 1
            for _, member in ipairs(record.group.members) do
                if isElement(member.ped) then
                    setElementData(member.ped, "neon:ambientPedGroupRole", member == replacement and "leader" or "member")
                end
            end
            log(("group-promote group=%d epoch=%d old=%d new=%d reason=leader-wasted"):format(
                    record.group.id, record.group.epoch, record.id, replacement.id), true)
        else
            log(("group-death group=%d epoch=%d member=%d role=last-alive"):format(
                    record.group.id, record.group.epoch, record.id), true)
        end
    elseif record.group then
        log(("group-death group=%d epoch=%d member=%d role=member"):format(
                record.group.id, record.group.epoch, record.id), true)
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
    if record and record.group and not record.group.removing then
        removeGroup(record.group, "group-member-destroyed")
        return
    end
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
    if residencyTest and (source == residencyTest.ownerA or source == residencyTest.ownerB) then
        failResidencyTest("client-left")
    end
    local visibilityChecks = {}
    for _, check in pairs(pendingVisibilityChecks) do visibilityChecks[#visibilityChecks + 1] = check end
    for _, check in ipairs(visibilityChecks) do
        if check.awaiting[source] or check.player == source then
            if check.kind == "candidate" then
                finishCandidateVisibilityCheck(check, "visibility-voter-departed")
            else
                finishRemovalVisibilityCheck(check, true, "visibility-voter-departed")
            end
        end
    end
    pendingRequests[source] = nil
    populationProfiles[source] = nil
    populationWorldRevisions[source] = nil
    populationClientIds[source] = nil
    removeTestVehicle(source)
    local groups = {}
    for _, group in pairs(trafficGroups) do groups[#groups + 1] = group end
    for _, group in ipairs(groups) do
        if group.owner == source and not group.removing then
            local newOwner = findClosestGroupResident(group, source, true)
            if newOwner then
                group.owner = nil
                group.pendingOwner = newOwner
                setGroupState(group, "revoking")
                finishGroupHandoff(group, "group-owner-quit")
            else
                group.owner = nil
                finishGroupSuspension(group, "group-owner-quit-no-resident")
            end
        end
    end
    for _, record in pairs(trafficPeds) do
        if not record.group and record.owner == source and not record.removing then
            local x, y, z = getElementPosition(record.ped)
            local newOwner = findClosestPopulationResident(x, y, z, record.populationClass, source)
            if newOwner then
                record.owner = nil
                record.pendingOwner = newOwner
                finishHandoff(record, "owner-quit")
            else
                record.owner = nil
                finishSuspension(record, "owner-quit-no-resident")
            end
        end
    end
end)

setTimer(function()
    local now = getTickCount()
    local expired = {}
    for _, check in pairs(pendingVisibilityChecks) do
        if now >= check.deadline then
            expired[#expired + 1] = check
        end
    end
    for _, check in ipairs(expired) do
        if check.kind == "candidate" then
            finishCandidateVisibilityCheck(check, "visibility-timeout")
        else
            -- A missing camera result must never turn into an on-screen pop.
            finishRemovalVisibilityCheck(check, true, "visibility-timeout")
        end
    end
end, 100, 0)

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
        local radii = getPopulationRadii(populationProfiles[player])
        local nativeTarget, civilianTarget, gangTarget, gangTargets = getNativeTargetsNearPlayer(player)
        local request = pendingRequests[player]
        if request and getTickCount() - request.issuedAt > config.requestTimeout then
            pendingRequests[player] = nil
            request = nil
        end

        local totalCount, civilianCount, gangCounts = getPopulationCountsNearPlayer(player)
        local totalGangCount = 0
        for index = 1, 10 do totalGangCount = totalGangCount + gangCounts[index] end
        local classDeficit = nativeTarget and civilianTarget - civilianCount >= 1
        local familyDeficit = false
        if nativeTarget then
            for index = 1, 10 do
                if gangTargets[index] - gangCounts[index] >= 1 then
                    familyDeficit = true
                    break
                end
            end
            classDeficit = classDeficit or gangTarget - totalGangCount >= 1 or familyDeficit
        end

        -- A stable zone/time transition may keep the same total while changing
        -- its families. Retire at most one furthest surplus ped every two
        -- seconds, then let the normal native candidate lane refill it.
        local profile = populationProfiles[player]
        local now = getTickCount()
        if nativeTarget and profile and now >= (profile.nextRebalanceAt or 0) and
            (totalCount > nativeTarget or (totalCount >= nativeTarget and classDeficit)) then
            local surplus = false
            local surplusGroup = false
            if civilianCount - civilianTarget >= 1 then
                surplus = findFurthestPopulationPed(x, y, z, radii.civilian, function(record)
                    return record.populationClass == "civilian"
                end)
            end
            if not surplus then
                for gangIndex = 1, 10 do
                    if gangCounts[gangIndex] - gangTargets[gangIndex] >= 1 then
                        local gang = gangIndex - 1
                        surplus = findFurthestPopulationPed(x, y, z, radii.gang, function(record)
                            return record.populationClass == "gang" and record.gang == gang
                        end)
                        if not surplus then
                            surplusGroup = findFurthestSurplusPopulationGroup(x, y, z, radii.gang, gang, false)
                        end
                        if surplus or surplusGroup then break end
                    end
                end
            end
            if not surplus and not surplusGroup and totalCount > nativeTarget then
                surplus = findFurthestPopulationPed(x, y, z, radii.maximum, function()
                    return true
                end)
                if not surplus then
                    surplusGroup = findFurthestSurplusPopulationGroup(x, y, z, radii.gang, nil, true)
                end
            end
            if surplus or surplusGroup then
                profile.nextRebalanceAt = now + 2000
                if surplusGroup then
                    beginRemovalVisibilityCheck(surplusGroup, "group-population-rebalance")
                else
                    beginRemovalVisibilityCheck(surplus, "population-rebalance")
                end
                break
            end
            profile.nextRebalanceAt = now + 1000
        end

        local selection = nativeTarget and selectPopulationForPlayer(player)
        if selection and selection.populationClass == "gang" and
            countNativeGroupsForOwner(player) >= config.maximumNativeGangGroups then
            selection = false
        end
        if selection and selection.populationClass == "gang" then
            selection.maximumGroupMembers = math.min(
                config.maximumGangGroupSize,
                config.globalCap - getTrafficPedCount(),
                config.pedPoolSoftLimit - #getElementsByType("ped"))
            if selection.maximumGroupMembers < config.minimumGangGroupSize then
                selection = false
            end
        end
        if selection and not request and not next(pendingRequests) and not next(pendingVisibilityChecks) and
            getTrafficPedCount() < config.globalCap and
            #getElementsByType("ped") < config.pedPoolSoftLimit then
            nextRequestId = nextRequestId + 1
            selection.requestId = nextRequestId
            pendingRequests[player] = {
                id = nextRequestId,
                issuedAt = getTickCount(),
                worldRevision = populationWorld.revision,
                selection = selection,
            }
            stats.requests = stats.requests + 1
            stats.populationSelections[selection.populationClass] = stats.populationSelections[selection.populationClass] + 1
            if selection.populationClass == "gang" then
                stats.gangSelections[selection.gang] = stats.gangSelections[selection.gang] + 1
            end
            log(("arbitrate request=%d player=%s class=%s gang=%s maxMembers=%d target=%.2f/%.2f live=%d/%d deficit=%.2f/%.2f roll=%.2f/%.2f"):format(
                    nextRequestId, getPlayerName(player), selection.populationClass, tostring(selection.gang), selection.maximumGroupMembers or 1,
                    selection.civilianTarget, selection.gangTarget, selection.civilianCount, selection.totalGangCount,
                    selection.civilianDeficit, selection.gangDeficit,
                    selection.civilianChance, selection.gangChance))
            writePopulationTrace("candidate_request", {
                request_id = nextRequestId,
                player_id = getPopulationClientId(player),
                population_class = selection.populationClass,
                gang = selection.gang,
                maximum_group_members = selection.maximumGroupMembers or 1,
                targets = {total = selection.totalTarget, civilian = selection.civilianTarget, gang = selection.gangTarget},
                live = {total = selection.totalCount, civilian = selection.civilianCount, gang = selection.totalGangCount},
                deficits = {civilian = selection.civilianDeficit, gang = selection.gangDeficit},
            })
            triggerClientEvent(player, "pedTraffic:candidateRequest", resourceRoot, nextRequestId, populationWorld.revision,
                               selection.populationClass, selection.gang, selection.maximumGroupMembers or 1)
            break
        end
    end
end, config.requestInterval, 0)

setTimer(function()
    if not enabled then
        return
    end

    local now = getTickCount()
    local groups = {}
    for _, group in pairs(trafficGroups) do groups[#groups + 1] = group end
    for _, group in ipairs(groups) do
        if not group.removing then
            if group.state == "suspending" then
                if now >= (group.handoffDeadline or 0) then
                    finishGroupSuspension(group, "group-suspension-release-timeout")
                end
            elseif group.state == "suspended" then
                local resident = findClosestGroupResident(group, nil, true)
                if resident then
                    if group.visibilityCheckId then
                        finishRemovalVisibilityCheck(pendingVisibilityChecks[group.visibilityCheckId], true, "resident-returned")
                    end
                    writePopulationTrace("group_collision_residency_resuming", {
                        group_id = group.id,
                        epoch = group.epoch,
                        owner_id = getPopulationClientId(resident),
                    })
                    assignGroupOwner(group, resident, "group-resident-returned")
                elseif now - (group.outsideResidencySince or group.suspendedAt or now) >= config.despawnGrace then
                    beginRemovalVisibilityCheck(group, "group-outside-residency")
                end
            elseif group.state == "revoking" then
                if now >= (group.handoffDeadline or 0) then
                    finishGroupHandoff(group, "group-release-timeout")
                end
            elseif group.state == "assigning" then
                if now - (group.assignmentStartedAt or now) >= 10000 then
                    removeGroup(group, "group-assignment-timeout")
                elseif now - (group.assignmentLastSent or 0) >= 1000 then
                    sendGroupAssignment(group, "group-assignment-retry")
                end
            elseif group.state == "active" then
                local x, y, z = getGroupCentre(group)
                if not x then
                    removeGroup(group, "group-empty")
                else
                    local resident = findClosestGroupResident(group)
                    if not resident then
                        beginGroupSuspension(group, "group-no-collision-resident")
                    else
                        if group.visibilityCheckId then
                            finishRemovalVisibilityCheck(pendingVisibilityChecks[group.visibilityCheckId], true, "resident-returned")
                        end
                        group.outsideResidencySince = nil
                        local closest, closestDistanceSquared = findClosestGroupResident(group, nil, true)
                        if not closest then
                            group.handoffCandidate = nil
                            group.handoffCandidateSince = nil
                        elseif not isEligiblePlayer(group.owner) then
                            beginGroupHandoff(group, closest, "group-owner-ineligible")
                        elseif closest ~= group.owner then
                            local ownerDistance = math.sqrt(getGroupDistanceSquaredToPlayer(group, group.owner))
                            local closestDistance = math.sqrt(closestDistanceSquared)
                            if closestDistance + config.handoffMargin < ownerDistance then
                                if group.handoffCandidate ~= closest then
                                    group.handoffCandidate = closest
                                    group.handoffCandidateSince = now
                                elseif now - group.handoffCandidateSince >= config.handoffHold then
                                    beginGroupHandoff(group, closest, "group-closer-owner")
                                end
                            else
                                group.handoffCandidate = nil
                                group.handoffCandidateSince = nil
                            end
                        else
                            group.handoffCandidate = nil
                            group.handoffCandidateSince = nil
                        end
                    end
                end
            end
        end
    end

    local records = {}
    for _, record in pairs(trafficPeds) do
        records[#records + 1] = record
    end
    for _, record in ipairs(records) do
        if not record.group and not record.removing and isElement(record.ped) then
            if record.state == "suspending" then
                if now >= (record.handoffDeadline or 0) then
                    finishSuspension(record, "suspension-release-timeout")
                end
            elseif record.state == "suspended" then
                local x, y, z = getElementPosition(record.ped)
                local resident = findClosestPopulationResident(x, y, z, record.populationClass)
                if resident then
                    if record.visibilityCheckId then
                        finishRemovalVisibilityCheck(pendingVisibilityChecks[record.visibilityCheckId], true, "resident-returned")
                    end
                    writePopulationTrace("collision_residency_resuming", {
                        traffic_id = record.id,
                        epoch = record.epoch,
                        owner_id = getPopulationClientId(resident),
                    })
                    assignOwner(record, resident, "resident-returned")
                elseif now - (record.outsideResidencySince or record.suspendedAt or now) >= config.despawnGrace then
                    beginRemovalVisibilityCheck(record, "outside-residency")
                end
            elseif record.state == "revoking" then
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
                local closest, closestDistanceSquared = findClosestPopulationResident(x, y, z, record.populationClass)
                if not closest then
                    beginSuspension(record, "no-collision-resident")
                else
                    if record.visibilityCheckId then
                        finishRemovalVisibilityCheck(pendingVisibilityChecks[record.visibilityCheckId], true, "resident-returned")
                    end
                    record.outsideResidencySince = nil
                    if not isEligiblePlayer(record.owner) then
                        beginHandoff(record, closest, "owner-ineligible")
                    elseif record.airTest or record.climbTest then
                        -- Keep the deterministic baseline on one owner. The
                        -- handoff variants transfer only when their owner
                        -- reports the requested physical phase above.
                        record.handoffCandidate = nil
                        record.handoffCandidateSince = nil
                    elseif closest ~= record.owner then
                        local ownerX, ownerY, ownerZ = getElementPosition(record.owner)
                        local ownerDistance = math.sqrt(squaredDistance2D(x, y, ownerX, ownerY))
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
    end
end, 1000, 0)

setTimer(function()
    if not debugEnabled then
        return
    end
    for _, player in ipairs(getEligiblePlayers()) do
        local totalTarget, civilianTarget, gangTarget = getNativeTargetsNearPlayer(player)
        if totalTarget then
            local totalCount, civilianCount, gangCounts = getPopulationCountsNearPlayer(player)
            local totalGangCount = 0
            for index = 1, 10 do totalGangCount = totalGangCount + gangCounts[index] end
            local profile = populationProfiles[player]
            local radii = getPopulationRadii(profile)
            writePopulationTrace("population_snapshot", {
                player_id = getPopulationClientId(player),
                targets = {total = totalTarget, civilian = civilianTarget, gang = gangTarget},
                live = {total = totalCount, civilian = civilianCount, gang = totalGangCount, gang_families = gangCounts},
                deficits = {
                    total = totalTarget - totalCount,
                    civilian = civilianTarget - civilianCount,
                    gang = gangTarget - totalGangCount,
                },
                radii = {civilian = radii.civilian, gang = radii.gang},
                position = {getElementPosition(player)},
            })
        end
    end
end, 2000, 0)

setTimer(function()
    if debugEnabled then
        local activeCivilians, activeGangs = getActivePopulationSummary()
        local activeGroups = getTrafficGroupCount()
        log(("telemetry active=%d activeCiv=%d activeGangs=%s groups=%d groupSpawns=%d groupHandoffs=%d groupPromotions=%d groupRemovals=%d ready=%d preset=%s revision=%d requests=%d misses=%d rejected=%d spawned=%d despawned=%d handoffs=%d selections=%s selectedGangs=%s models=%s missReasons=%s rejectionReasons=%s"):format(
                getTrafficPedCount(), activeCivilians, formatNumericMap(activeGangs), activeGroups, stats.groupSpawns,
                stats.groupHandoffs, stats.groupPromotions, stats.groupRemovals,
                #getEligiblePlayers(), populationWorld.preset, populationWorld.revision, stats.requests, stats.candidateMisses,
                stats.rejected, stats.spawned, stats.despawned, stats.handoffs,
                formatNumericMap(stats.populationSelections), formatNumericMap(stats.gangSelections), formatNumericMap(stats.spawnedModels),
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
        local requested = tostring(value or "on"):lower() ~= "off"
        if requested and not debugEnabled then
            resetPopulationTrace()
        elseif not requested and debugEnabled then
            writePopulationTrace("trace_stopped", {active = getTrafficPedCount()})
        end
        debugEnabled = requested
        triggerClientEvent(root, "pedTraffic:setDebug", resourceRoot, debugEnabled)
        log("debug=" .. tostring(debugEnabled), true)
        if debugEnabled then
            writePopulationTrace("trace_started", {active = getTrafficPedCount(), global_cap = config.globalCap})
        end
    elseif action == "preset" then
        local requested = populationWorld:resolvePreset(value)
        if not populationWorld:setPreset(requested) then
            outputChatBox("Usage: /pedtraffic preset " .. table.concat(populationWorld:listPresets(), "|"), player, 255, 160, 80)
            return
        end

        clearTraffic("population-preset-change")
        pendingRequests = {}
        populationProfiles = {}
        populationWorldRevisions = {}
        if enabled then
            sendPopulationWorldState(root)
        end
        outputChatBox(("Ped traffic population preset = %s (revision %d)"):format(populationWorld.preset, populationWorld.revision), root, 120, 220, 255)
        log(("population-world preset=%s revision=%d actor=%s"):format(
                populationWorld.preset, populationWorld.revision, isElement(player) and getPlayerName(player) or "console"), true)
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
    elseif action == "residency" and isElement(player) then
        startResidencyTest(player)
    else
        local activeCount = 0
        for ped in pairs(trafficPeds) do
            if isElement(ped) then activeCount = activeCount + 1 end
        end
        outputChatBox(("Ped traffic: enabled=%s active=%d cap=%d preset=%s revision=%d requests=%d misses=%d rejected=%d handoffs=%d"):format(
                          tostring(enabled), activeCount, config.globalCap, populationWorld.preset, populationWorld.revision, stats.requests,
                          stats.candidateMisses, stats.rejected, stats.handoffs),
                      player, 120, 220, 255)
    end
end)

addEventHandler("onResourceStart", resourceRoot, function()
    outputServerLog(("[ped-traffic] V1 loaded disabled; population preset=%s revision=%d; use /pedtraffic on, /pedtraffic debug on"):format(
        populationWorld.preset, populationWorld.revision))
end)

addEventHandler("onResourceStop", resourceRoot, function()
    if stopResidencyTest then
        stopResidencyTest("CANCEL", "resource-stop")
    end
    triggerClientEvent(root, "pedTraffic:setEnabled", resourceRoot, false, false)
    clearTraffic("resource-stop")
    clearTestVehicles()
    populationProfiles = {}
    populationWorldRevisions = {}
end)
