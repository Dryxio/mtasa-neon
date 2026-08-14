local config = {
    globalCap = 24,
    pedPoolSoftLimit = 90,
    targetNearPlayer = 12,
    nearRadius = 120,
    despawnRadius = 120,
    despawnGrace = 4000,
    retireSafeRadius = 90,
    cellSize = 64,
    maxPerCell = 4,
    minSeparation = 10,
    requestInterval = 250,
    requestTimeout = 2500,
    handoffMargin = 20,
    handoffHold = 3000,
    handoffTimeout = 2000,
    corpseLifetime = 30000,
    nativeMeleeDamageRadius = 5,
    nativeMeleeDamageInterval = 250,
    minimumGangGroupSize = 2,
    maximumGangGroupSize = 4,
    maximumGangGroupSpan = 8,
    -- GTA has eight global group slots and normally reserves one for the
    -- player. Leave two more available to unrelated mission/resource logic.
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
local nextRequestId = 0
local nextPedId = 0
local nextGroupId = 0
local nextAirTestId = 0
local nextClimbTestId = 0
local requestCursor = 0
local pendingRequests = {}
local populationProfiles = {}
local populationWorld = PedTrafficPopulationWorld.create("post_intro")
local populationWorldRevisions = {}
local trafficPeds = {}
local trafficGroups = {}
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
        not isFiniteNumber(profile.dealerTarget) or
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
        (profile.noCops and profile.copTarget > 0.05) or (not profile.noCops and math.abs(profile.copTarget - profile.rawCopTarget) > 0.05) then
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
        worldRevision = profile.worldRevision,
        receivedAt = getTickCount(),
    }
end

local function calculateNativeTargets(profile)
    -- Stock adds while integerCount < floatTarget, which is equivalent to a
    -- ceiling once represented by server-owned elements.
    local civilianNativeTarget = profile.civilianTarget * populationWorld.densityMultiplier
    local gangNativeTarget = populationWorld.randomGangMembers and profile.gangTarget * populationWorld.densityMultiplier or 0
    local supportedTarget = civilianNativeTarget + gangNativeTarget
    local supportedGangWeight = 0
    for index = 1, 8 do supportedGangWeight = supportedGangWeight + profile.gangWeights[index] end
    if profile.totalGangWeight > 0 and supportedGangWeight < profile.totalGangWeight then
        gangNativeTarget = gangNativeTarget * supportedGangWeight / profile.totalGangWeight
        supportedTarget = civilianNativeTarget + gangNativeTarget
    end
    if supportedTarget <= 0 then
        return 0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
    end

    -- The V1 cap scales every supported native class by the same factor. Keep
    -- those scaled targets as floats: FindNewPedType compares target - live
    -- count on every creation and never rounds a per-class quota.
    local scale = math.min(1, config.targetNearPlayer / supportedTarget)
    local civilian = civilianNativeTarget * scale
    local gang = gangNativeTarget * scale
    local total = math.max(0, math.ceil(civilian + gang - 0.0001))
    local gangTargets = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
    if gang > 0 and supportedGangWeight > 0 then
        for index = 1, 8 do
            gangTargets[index] = gang * profile.gangWeights[index] / supportedGangWeight
        end
    end
    return total, civilian, gang, gangTargets
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

local function getEligiblePlayers()
    local players = {}
    for _, player in ipairs(getElementsByType("player")) do
        if isEligiblePlayer(player) and isPopulationWorldReady(player) then
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

local function getPopulationCountsNear(x, y, z, radius)
    local total = 0
    local civilians = 0
    local gangs = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
    local radiusSquared = radius * radius
    for ped, record in pairs(trafficPeds) do
        if isElement(ped) then
            local px, py, pz = getElementPosition(ped)
            if squaredDistance(x, y, z, px, py, pz) <= radiusSquared then
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

    local x, y, z = getElementPosition(player)
    local totalCount, civilianCount, gangCounts = getPopulationCountsNear(x, y, z, config.nearRadius)
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

    local x, y, z = getElementPosition(player)
    local totalCount, civilianCount, gangCounts = getPopulationCountsNear(x, y, z, config.nearRadius)
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
    local residencySquared = config.nearRadius * config.nearRadius
    local residentCount = 0
    for _, player in ipairs(getEligiblePlayers()) do
        local playerX, playerY, playerZ = getElementPosition(player)
        if squaredDistance(pedX, pedY, pedZ, playerX, playerY, playerZ) <= residencySquared then
            residentCount = residentCount + 1
            local nativeTarget, civilianTarget, _, gangTargets = getNativeTargetsNearPlayer(player)
            if nativeTarget == false then
                return false
            end
            local _, civilianCount, gangCounts = getPopulationCountsNear(playerX, playerY, playerZ, config.nearRadius)
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
            local distanceSquared = squaredDistance(x, y, z, px, py, pz)
            if distanceSquared <= radiusSquared and distanceSquared > furthestDistanceSquared then
                local safeForEveryPlayer = true
                local safeRadiusSquared = config.retireSafeRadius * config.retireSafeRadius
                for _, player in ipairs(getEligiblePlayers()) do
                    local playerX, playerY, playerZ = getElementPosition(player)
                    if squaredDistance(px, py, pz, playerX, playerY, playerZ) < safeRadiusSquared then
                        safeForEveryPlayer = false
                        break
                    end
                end
                if safeForEveryPlayer then
                    furthestRecord = record
                    furthestDistanceSquared = distanceSquared
                end
            end
        end
    end
    return furthestRecord
end

local function findFurthestSurplusPopulationGroup(x, y, z, radius, gang, requireTotalSurplus)
    local furthestGroup = false
    local furthestDistanceSquared = -1
    local radiusSquared = radius * radius
    local safeRadiusSquared = config.retireSafeRadius * config.retireSafeRadius
    for _, group in pairs(trafficGroups) do
        if not group.removing and group.state == "active" and (gang == nil or group.gang == gang) then
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
                local distanceSquared = squaredDistance(x, y, z, centreX, centreY, centreZ)
                local surplusForEveryResident = true
                local residentCount = 0
                for _, player in ipairs(getEligiblePlayers()) do
                    local playerX, playerY, playerZ = getElementPosition(player)
                    local residentDistanceSquared = squaredDistance(centreX, centreY, centreZ, playerX, playerY, playerZ)
                    if residentDistanceSquared < safeRadiusSquared then
                        surplusForEveryResident = false
                        break
                    end
                    if residentDistanceSquared <= config.nearRadius * config.nearRadius then
                        residentCount = residentCount + 1
                        local nativeTarget, _, _, gangTargets = getNativeTargetsNearPlayer(player)
                        if nativeTarget == false then
                            surplusForEveryResident = false
                            break
                        end
                        local totalCount, _, gangCounts = getPopulationCountsNear(playerX, playerY, playerZ, config.nearRadius)
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

local removeGroup

local function removeRecord(record, reason)
    if not record or record.removing then
        return
    end
    if record.group then
        return removeGroup(record.group, reason)
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
end

local function assignGroupOwner(group, owner, reason)
    if not group or group.removing or not isEligiblePlayer(owner) then
        return false
    end
    for _, record in ipairs(group.members) do
        if not isElement(record.ped) then
            removeGroup(group, "member-missing-before-assign")
            return false
        end
    end

    local isHandoff = group.epoch > 0
    group.owner = owner
    group.pendingOwner = nil
    group.epoch = group.epoch + 1
    group.handoffCandidate = nil
    group.handoffCandidateSince = nil
    group.handoffDeadline = nil
    group.assignmentStartedAt = getTickCount()
    group.assignmentLastSent = 0
    setGroupState(group, "assigning")

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
    return true
end

local function finishGroupHandoff(group, reason)
    if not group or group.removing then
        return
    end
    local owner = group.pendingOwner
    if not isEligiblePlayer(owner) then
        local x, y, z = getGroupCentre(group)
        owner = x and findClosestPlayer(x, y, z, config.despawnRadius) or false
    end
    if not owner then
        removeGroup(group, "group-handoff-no-owner")
        return
    end
    assignGroupOwner(group, owner, reason)
end

local function beginGroupHandoff(group, newOwner, reason)
    if not group or group.removing or group.state == "revoking" or newOwner == group.owner then
        return
    end
    group.pendingOwner = newOwner
    group.handoffDeadline = getTickCount() + config.handoffTimeout
    setGroupState(group, "revoking")
    if isElement(group.owner) then
        triggerClientEvent(group.owner, "pedTraffic:revokeGroup", resourceRoot, group.id, group.epoch, reason)
        log(("group-revoke group=%d epoch=%d old=%s new=%s reason=%s"):format(
                group.id, group.epoch, getPlayerName(group.owner), getPlayerName(newOwner), tostring(reason)))
    else
        finishGroupHandoff(group, "group-owner-departed")
    end
end

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
    if getTrafficGroupCount() >= config.maximumNativeGangGroups then
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

    local centreX, centreY, centreZ = getGroupCentre(group)
    local owner = centreX and findClosestPlayer(centreX, centreY, centreZ, config.despawnRadius) or false
    if not owner then
        removeGroup(group, "group-no-owner")
        return false, "group-no-owner"
    end
    if not assignGroupOwner(group, owner, "group-spawn") then
        return false, "group-assign-owner"
    end

    group.counted = true
    stats.groupSpawns = stats.groupSpawns + 1
    stats.spawned = stats.spawned + memberCount
    local models = {}
    local spawnTransforms = {}
    for _, member in ipairs(validated) do
        stats.spawnedModels[member.model] = (stats.spawnedModels[member.model] or 0) + 1
        models[#models + 1] = tostring(member.model)
    end
    for _, record in ipairs(group.members) do
        local x, y, z = getElementPosition(record.ped)
        local _, _, heading = getElementRotation(record.ped)
        spawnTransforms[#spawnTransforms + 1] = ("id=%d:(%.2f,%.2f,%.2f)@%.1f"):format(record.id, x, y, z, heading)
    end
    log(("group-spawn group=%d gang=%d members=%d models=%s owner=%s epoch=%d target=%.2f live=%d pos=%.1f,%.1f,%.1f"):format(
            group.id, group.gang, memberCount, table.concat(models, "/"), getPlayerName(owner), group.epoch,
            selection.gangTarget, selection.totalGangCount, centreX, centreY, centreZ), true)
    log(("group-spawn-transform group=%d epoch=%d members=[%s]"):format(
            group.id, group.epoch, table.concat(spawnTransforms, ";")), true)
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
        clearTraffic("disabled")
        clearTestVehicles()
        populationProfiles = {}
        populationWorldRevisions = {}
    end
    triggerClientEvent(root, "pedTraffic:setEnabled", resourceRoot, enabled, debugEnabled)
    log(("enabled=%s actor=%s"):format(tostring(enabled), isElement(actor) and getPlayerName(actor) or "console"), true)
end

addEvent("pedTraffic:candidate", true)
addEventHandler("pedTraffic:candidate", resourceRoot, function(requestId, worldRevision, candidate, elapsedMs, missReason)
    local player = client
    local request = pendingRequests[player]
    pendingRequests[player] = nil
    if not request or request.id ~= requestId or request.worldRevision ~= worldRevision or worldRevision ~= populationWorld.revision or
        not isPopulationWorldReady(player) or getTickCount() - request.issuedAt > config.requestTimeout or not isEligiblePlayer(player) then
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

    local created, reason
    if request.selection.populationClass == "gang" then
        created, reason = spawnGangGroup(player, candidate, request.selection)
    else
        created, reason = spawnCandidate(player, candidate, request.selection)
    end
    if not created then
        stats.rejected = stats.rejected + 1
        countReason(stats.rejectionReasons, reason)
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
    validated.signature = ("%d:%.3f:%.3f:%s:%d:%d:%s:%d:%.3f:%.3f:%s"):format(
        totalTarget, civilianTarget, gangTarget, table.concat(targetSignature, ","), validated.zoneType, validated.timeIndex,
        tostring(validated.weekend), validated.worldRevision, validated.effectiveCopTarget, validated.dealerTarget, tostring(validated.noCops))
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

addEvent("pedTraffic:groupEvidence", true)
addEventHandler("pedTraffic:groupEvidence", resourceRoot, function(groupId, epoch, evidence, data)
    local group = trafficGroups[tonumber(groupId)]
    if not group or group.removing or group.epoch ~= epoch or client ~= group.owner then
        return
    end
    if evidence == "accepted" and group.state == "assigning" then
        setGroupState(group, "active")
        group.acceptedAt = getTickCount()
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
    pendingRequests[source] = nil
    populationProfiles[source] = nil
    populationWorldRevisions[source] = nil
    removeTestVehicle(source)
    local groups = {}
    for _, group in pairs(trafficGroups) do groups[#groups + 1] = group end
    for _, group in ipairs(groups) do
        if group.owner == source and not group.removing then
            local x, y, z = getGroupCentre(group)
            local newOwner = x and findClosestPlayer(x, y, z, config.despawnRadius, source) or false
            if newOwner then
                group.owner = nil
                group.pendingOwner = newOwner
                setGroupState(group, "revoking")
                finishGroupHandoff(group, "group-owner-quit")
            else
                removeGroup(group, "group-owner-quit-no-fallback")
            end
        end
    end
    for _, record in pairs(trafficPeds) do
        if not record.group and record.owner == source and not record.removing then
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
        local nativeTarget, civilianTarget, gangTarget, gangTargets = getNativeTargetsNearPlayer(player)
        local request = pendingRequests[player]
        if request and getTickCount() - request.issuedAt > config.requestTimeout then
            pendingRequests[player] = nil
            request = nil
        end

        local totalCount, civilianCount, gangCounts = getPopulationCountsNear(x, y, z, config.nearRadius)
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
                surplus = findFurthestPopulationPed(x, y, z, config.nearRadius, function(record)
                    return record.populationClass == "civilian"
                end)
            end
            if not surplus then
                for gangIndex = 1, 10 do
                    if gangCounts[gangIndex] - gangTargets[gangIndex] >= 1 then
                        local gang = gangIndex - 1
                        surplus = findFurthestPopulationPed(x, y, z, config.nearRadius, function(record)
                            return record.populationClass == "gang" and record.gang == gang
                        end)
                        if not surplus then
                            surplusGroup = findFurthestSurplusPopulationGroup(x, y, z, config.nearRadius, gang, false)
                        end
                        if surplus or surplusGroup then break end
                    end
                end
            end
            if not surplus and not surplusGroup and totalCount > nativeTarget then
                surplus = findFurthestPopulationPed(x, y, z, config.nearRadius, function()
                    return true
                end)
                if not surplus then
                    surplusGroup = findFurthestSurplusPopulationGroup(x, y, z, config.nearRadius, nil, true)
                end
            end
            if surplus or surplusGroup then
                profile.nextRebalanceAt = now + 2000
                if surplusGroup then
                    removeGroup(surplusGroup, "group-population-rebalance")
                else
                    removeRecord(surplus, "population-rebalance")
                end
                break
            end
            profile.nextRebalanceAt = now + 1000
        end

        local selection = nativeTarget and selectPopulationForPlayer(player)
        if selection and selection.populationClass == "gang" and
            getTrafficGroupCount() >= config.maximumNativeGangGroups then
            selection = false
        end
        if selection and selection.populationClass == "gang" then
            selection.maximumGroupMembers = math.min(
                config.maximumGangGroupSize,
                config.globalCap - getTrafficPedCount(),
                config.pedPoolSoftLimit - #getElementsByType("ped"),
                selection.totalTarget - selection.totalCount)
            if selection.maximumGroupMembers < config.minimumGangGroupSize then
                selection = false
            end
        end
        if selection and not request and getTrafficPedCount() < config.globalCap and #getElementsByType("ped") < config.pedPoolSoftLimit then
            nextRequestId = nextRequestId + 1
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
            if group.state == "revoking" then
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
                    local closest, closestDistanceSquared = findClosestPlayer(x, y, z, config.despawnRadius)
                    if not closest then
                        if not group.outsideResidencySince then
                            group.outsideResidencySince = now
                        elseif now - group.outsideResidencySince >= config.despawnGrace then
                            removeGroup(group, "group-outside-residency")
                        end
                    else
                        group.outsideResidencySince = nil
                        if not isEligiblePlayer(group.owner) then
                            beginGroupHandoff(group, closest, "group-owner-ineligible")
                        elseif closest ~= group.owner then
                            local ownerX, ownerY, ownerZ = getElementPosition(group.owner)
                            local ownerDistance = math.sqrt(squaredDistance(x, y, z, ownerX, ownerY, ownerZ))
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
                    if not record.outsideResidencySince then
                        record.outsideResidencySince = now
                    elseif now - record.outsideResidencySince >= config.despawnGrace then
                        removeRecord(record, "outside-residency")
                    end
                else
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
    end
end, 1000, 0)

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
        debugEnabled = tostring(value or "on"):lower() ~= "off"
        triggerClientEvent(root, "pedTraffic:setDebug", resourceRoot, debugEnabled)
        log("debug=" .. tostring(debugEnabled), true)
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
    triggerClientEvent(root, "pedTraffic:setEnabled", resourceRoot, false, false)
    clearTraffic("resource-stop")
    clearTestVehicles()
    populationProfiles = {}
    populationWorldRevisions = {}
end)
