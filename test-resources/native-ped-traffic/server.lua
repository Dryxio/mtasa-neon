local config = {
    -- Start the public population with the resource. Tests can still disable
    -- it explicitly before exercising an isolated harness.
    autoStart = true,
    -- Cap all scripted peds at ninety, including vehicle occupants created by
    -- other resources. This leaves twenty logical slots for players, missions
    -- and short-lived gameplay actors while disjoint population bubbles share
    -- the remaining capacity.
    globalCap = 90,
    pedPoolSoftLimit = 90,
    despawnGrace = 4000,
    cellSize = 64,
    maxPerCell = 12,
    minSeparation = 2,
    requestInterval = 100,
    requestTimeout = 3500,
    visibilityTimeout = 1000,
    handoffHold = 3000,
    handoffTimeout = 2000,
    corpseLifetime = 30000,
    nativeMeleeDamageRadius = 5,
    nativeMeleeDamageInterval = 250,
    nativeFirearmDamageInterval = 40,
    nativeBikeJackDistance = 8,
    nativeBikeJackInterval = 750,
    -- GTA admits the drag branch at 0.1 2D move-speed units. The small margin
    -- covers one delayed server velocity sample without allowing a moving
    -- motorcycle to manufacture a stationary carjack.
    nativeBikeJackMaximumSpeed = 0.12,
    nativeGangWeaponChance = 33,
    nativeGangWeaponAmmo = 25001,
    nativeDealerFightWeaponAmmo = 50,
    dealerStrengthGrowthInterval = 60000,
    populationWorldConvergenceGrace = 5000,
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
local stockUnarmedDealerDamageFactors = {[6] = true, [7] = true, [10] = true, [18] = true, [30] = true}
local stockKnifeDealerDamageFactors = {[12] = true, [30] = true}
local stockUnarmedCivilianDamageFactors = {
    [1] = true, [2] = true, [3] = true, [4] = true, [5] = true, [6] = true, [7] = true, [8] = true,
    [9] = true, [10] = true, [12] = true, [13] = true, [15] = true, [16] = true, [17] = true,
    [18] = true, [20] = true, [22] = true, [25] = true, [27] = true, [30] = true, [37] = true,
}

local stockGangFirearmDamage = {
    [22] = {factor = 25, radius = 40},  -- pistol STD: weapon range 35
    [24] = {factor = 140, radius = 40}, -- desert eagle STD: weapon range 35
    [28] = {factor = 20, radius = 40},  -- micro uzi STD: weapon range 35
    [30] = {factor = 30, radius = 75},  -- AK-47 STD: weapon range 70
    [32] = {factor = 20, radius = 40},  -- Tec-9 STD: weapon range 35
}

local function getCanonicalTrafficNativeDamage(record, weapon, damageFactor)
    if weapon == 0 then
        if record.populationClass == "gang" and stockUnarmedGangDamageFactors[damageFactor] == true or
            record.populationClass == "dealer" and stockUnarmedDealerDamageFactors[damageFactor] == true or
            record.populationClass == "civilian" and stockUnarmedCivilianDamageFactors[damageFactor] == true then
            return config.nativeMeleeDamageRadius, config.nativeMeleeDamageInterval
        end
        return false
    end
    if weapon == 4 and record.populationClass == "dealer" and record.dealerFightArmed == true and
        record.dealerHasKnife == true and stockKnifeDealerDamageFactors[damageFactor] == true then
        return config.nativeMeleeDamageRadius, config.nativeMeleeDamageInterval
    end
    local firearm = record.populationClass == "gang" and stockGangFirearmDamage[weapon] or false
    if record.populationClass == "dealer" and record.dealerFightArmed == true and record.dealerHasPistol == true and weapon == 22 then
        firearm = stockGangFirearmDamage[22]
    end
    if firearm and damageFactor == firearm.factor then
        return firearm.radius, config.nativeFirearmDamageInterval
    end
    return false
end

-- Candidate identity is rebuilt from a checked-in catalog generated from the
-- retail data files. Keeping this mapping in one versioned artifact prevents
-- cops, special occupations and future relations from accumulating separate
-- hand-maintained Lua allowlists.
local populationCatalog = assert(PedTrafficPopulationCatalog, "population catalog missing")
local gangByModel = populationCatalog.gangByModel
local civilianPedTypeByModel = populationCatalog.civilianPedTypeByModel
local dealerModels = {}
for _, model in ipairs(populationCatalog.dealerModels) do dealerModels[model] = true end
local copModelsByLevel = populationCatalog.copModelsByLevel

-- CPopulation::AddPed performs two independent CGeneral rolls for every gang
-- ped: the first arms it when [0, 100) is below 33, then the second selects
-- uniformly from CGangs' non-zero weapon slots. The server owns this adapted
-- distributed draw so the resulting weapon, ammo and current slot are already
-- canonical before any client acquires the native group. GTA's process-global
-- RNG cannot be shared across syncers; preserving its draw order and exact
-- distribution is the deterministic network equivalent.
local gangWeaponClipAmmo = {
    [22] = 17, -- pistol
    [24] = 7,  -- desert eagle (standard skill)
    [28] = 50, -- micro uzi
    [30] = 30, -- AK-47
    [32] = 50, -- Tec-9
}

local gangStandardWeaponStats = {
    {69, 40},  -- pistol
    {71, 200}, -- desert eagle
    {75, 50},  -- micro uzi / Tec-9
    {77, 200}, -- AK-47
}

local function selectGangPedWeapon(slots)
    local armedRoll = math.random(0, 99)
    local slotRoll = false
    local source = type(slots) == "table" and slots or {}
    local slot1 = math.floor(tonumber(source[1]) or 0)
    local slot2 = math.floor(tonumber(source[2]) or 0)
    local slot3 = math.floor(tonumber(source[3]) or 0)
    local slotCount = (slot1 ~= 0 and 1 or 0) + (slot2 ~= 0 and 1 or 0) + (slot3 ~= 0 and 1 or 0)

    if armedRoll >= config.nativeGangWeaponChance then
        return 0, armedRoll, slotRoll, slotCount
    end

    -- AddPed consumes the second roll whenever the first succeeds. It does
    -- not compact slots: slot 3 selects 33/33/34, otherwise slot 2 selects
    -- 50/50, otherwise slot 1 wins. Preserve zero selections too, because a
    -- script-set sparse CGangs table can legitimately cancel this ped's grant.
    slotRoll = math.random(0, 99)
    local weapon
    if slot3 ~= 0 then
        weapon = slotRoll < 33 and slot1 or (slotRoll < 66 and slot2 or slot3)
    elseif slot2 ~= 0 then
        weapon = slotRoll < 50 and slot1 or slot2
    else
        weapon = slot1
    end
    return weapon > 0 and weapon or 0, armedRoll, slotRoll, slotCount
end

local function applyGangPedWeapon(ped, weapon)
    -- Stock CPed stores one explicit STD weapon-skill byte. Script peds are
    -- CPlayerPed wrappers and resolve skill through synchronized player stats,
    -- which start at POOR. Pin every gang weapon family to its exact STD
    -- threshold before the grant so both decision/damage and presentation read
    -- the same weapon.dat row vanilla CPed would use.
    for _, stat in ipairs(gangStandardWeaponStats) do
        if not setPedStat(ped, stat[1], stat[2]) then
            return false
        end
    end
    if weapon == 0 then
        return getPedWeapon(ped) == 0
    end
    local clip = gangWeaponClipAmmo[weapon]
    if not clip or not giveWeapon(ped, weapon, 9999, true) then
        return false
    end
    -- GTA gives ambient gang peds 25001 rounds. MTA's generic giveWeapon RPC
    -- intentionally clamps one grant to 9999, while setWeaponAmmo transports a
    -- full ushort. Compose the two public operations instead of weakening that
    -- unrelated global clamp.
    return setWeaponAmmo(ped, weapon, config.nativeGangWeaponAmmo, clip) == true
end

local function selectDealerFightWeapons(seed, knifeModelLoaded)
    if seed < 200 then
        -- GiveWeaponAtStartOfFight tries the knife first. A resident knife
        -- model clears GTA's one-entry delayed slot synchronously, allowing
        -- the independent pistol check to run; a cold knife leaves that slot
        -- occupied and suppresses the pistol on this first fight.
        return true, knifeModelLoaded == true
    end
    return false, seed < 400
end

local function applyDealerFightWeapons(record, knifeModelLoaded)
    local ped = record.ped
    if not isElement(ped) or getPedWeapon(ped) ~= 0 then
        return false, "dealer-not-unarmed"
    end
    local hasKnife, hasPistol = selectDealerFightWeapons(record.dealerFightSeed, knifeModelLoaded)
    if hasKnife and not giveWeapon(ped, 4, config.nativeDealerFightWeaponAmmo, not hasPistol) then
        return false, "dealer-knife-grant-refused"
    end
    if hasPistol and (not setPedStat(ped, 69, 40) or not giveWeapon(ped, 22, config.nativeDealerFightWeaponAmmo, true) or
        not setWeaponAmmo(ped, 22, config.nativeDealerFightWeaponAmmo, math.min(17, config.nativeDealerFightWeaponAmmo))) then
        return false, "dealer-pistol-grant-refused"
    end
    record.dealerFightArmed = true
    record.dealerHasKnife = hasKnife
    record.dealerHasPistol = hasPistol
    record.weapon = hasPistol and 22 or (hasKnife and 4 or 0)
    record.weaponAmmo = record.weapon ~= 0 and config.nativeDealerFightWeaponAmmo or 0
    setElementData(ped, "neon:ambientPedWeapon", record.weapon)
    setElementData(ped, "neon:ambientPedWeaponAmmo", record.weaponAmmo)
    setElementData(ped, "neon:ambientPedDealerKnife", hasKnife)
    setElementData(ped, "neon:ambientPedDealerPistol", hasPistol)
    setElementData(ped, "neon:ambientPedDealerFightArmed", true)
    return true
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
local populationWorld = PedTrafficPopulationWorld.create("post_home_coming")
pedTrafficDemoDensity = {
    enabled = false,
    target = 32,
    epoch = 0,
    anchor = false,
    previousEnabled = false,
    fallbackCursor = 0,
}
local lastDealerStrengthMinute = math.floor(getTickCount() / config.dealerStrengthGrowthInterval)
local populationWorldPublishedAt = getTickCount()
local populationWorldConvergenceTraceRevision = false
local populationWorldRevisions = {}
local trafficPeds = {}
local trafficGroups = {}
local coupleRuntime = {relations = {}, rngState = 0x13579BDF}

local function getTrafficPedCount()
    local count = 0
    for ped in pairs(trafficPeds) do
        if isElement(ped) then count = count + 1 end
    end
    return count
end

local nextGroupDamageId = 0
local testVehicles = {}
local pendingNativeBikeJacks = {}
local bikeJackTest = false
local nextBikeJackTestId = 0
local residencyTest = false
local nextResidencyTestId = 0
local dealerTest = false
local nextDealerTestId = 0
local copTest = false
local nextCopTestId = 0
local coupleTest = false
local nextCoupleTestId = 0
local nextCoupleRelationId = 0
local stopResidencyTest
local stopBikeJackTest
local stopDealerTest
local failDealerTest
local finishCoupleTest
local startBikeJackSeatConvergence
local stats = {
    requests = 0,
    candidateMisses = 0,
    rejected = 0,
    spawned = 0,
    handoffs = 0,
    despawned = 0,
    missReasons = {},
    rejectionReasons = {},
    populationSelections = {civilian = 0, gang = 0, dealer = 0, cop = 0},
    gangSelections = {[0] = 0, [1] = 0, [2] = 0, [3] = 0, [4] = 0, [5] = 0, [6] = 0, [7] = 0, [8] = 0, [9] = 0},
    spawnedModels = {},
    groupSpawns = 0,
    groupHandoffs = 0,
    groupRemovals = 0,
    groupPromotions = 0,
    coupleAttempts = 0,
    coupleSpawns = 0,
    coupleRollbacks = 0,
}

function coupleRuntime.roll()
    -- Distributed ownership cannot share GTA's process-global CRT state. The
    -- server therefore owns the same 32-bit LCG and exact 15-bit threshold
    -- used by AddToPopulation's strict `random > 0.9f` branch.
    coupleRuntime.rngState = (coupleRuntime.rngState * 0x343FD + 0x269EC3) % 4294967296
    local sample15 = math.floor(coupleRuntime.rngState / 65536) % 32768
    return sample15, sample15 >= 29491
end

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
        schema_version = 2,
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

local function publishPopulationWorldMutation(kind, details)
    pendingRequests = {}
    -- Keep the last authenticated profile long enough to prove that an
    -- existing owner is still spatially resident while clients apply the new
    -- world revision. New admission remains fenced below by an exact profile
    -- revision check, so this continuity cannot authorize a stale spawn.
    populationWorldRevisions = {}
    populationWorldPublishedAt = getTickCount()
    populationWorldConvergenceTraceRevision = false
    writePopulationTrace("dealer_strength_world_revision", {
        scenario_id = dealerTest and dealerTest.id or false,
        mutation = kind,
        revision = populationWorld.revision,
        catalog_revision = populationCatalog.revision,
        details = details or {},
    })
    if enabled then
        sendPopulationWorldState(root)
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
        profile.catalogRevision ~= populationCatalog.revision or type(profile.zoneLabel) ~= "string" or
        #profile.zoneLabel < 1 or #profile.zoneLabel > 8 or profile.zoneLabel:find("[^A-Z0-9]") or
        populationWorld.zones[profile.zoneLabel] == nil or
        not isFiniteNumber(profile.target) or not isFiniteNumber(profile.supportedTarget) or not isFiniteNumber(profile.civilianTarget) or
        not isFiniteNumber(profile.rawCopTarget) or not isFiniteNumber(profile.copTarget) or not isFiniteNumber(profile.gangTarget) or
        not isFiniteNumber(profile.dealerTarget) or not isFiniteNumber(profile.pedDensityMultiplier) or
        not isFiniteNumber(profile.fewerPedsMultiplier) or not isFiniteNumber(profile.creationDistanceMultiplier) or
        not isFiniteNumber(profile.generationDistanceMultiplier) or not isIntegerInRange(profile.maximumPedsInUse, 0, 110) or
        not isIntegerInRange(profile.zoneType, 0, 19) or
        not isIntegerInRange(profile.timeIndex, 0, 11) or type(profile.weekend) ~= "boolean" or
        not isIntegerInRange(profile.dealerStrength, 0, 255) or not isIntegerInRange(profile.raceFlags, 0, 15) or
        type(profile.noCops) ~= "boolean" or not isIntegerInRange(profile.worldLevel, 0, 3) or
        not isIntegerInRange(profile.copSuppressionFlags, 0, 15) or type(profile.gangWeights) ~= "table" then
        return false
    end

    if profile.target < 0 or profile.target > 110 or profile.supportedTarget < 0 or profile.supportedTarget > 110 or
        profile.civilianTarget < 0 or profile.civilianTarget > 110 or profile.rawCopTarget < 0 or profile.rawCopTarget > 110 or
        profile.copTarget < 0 or profile.copTarget > 110 or
        profile.gangTarget < 0 or profile.gangTarget > 110 or profile.dealerTarget < 0 or profile.dealerTarget > 110 or
        math.abs(profile.supportedTarget - profile.civilianTarget - profile.gangTarget - profile.dealerTarget - profile.copTarget) > 0.05 or
        math.abs(profile.target - profile.supportedTarget) > 0.05 or
        ((profile.copSuppressionFlags ~= 0) and profile.copTarget > 0.05) or
        ((profile.copSuppressionFlags == 0) and math.abs(profile.copTarget - profile.rawCopTarget) > 0.05) or
        (((profile.copSuppressionFlags % 2) == 1) ~= profile.noCops) or
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
    local zoneState = populationWorld.zones[profile.zoneLabel]
    if profile.zoneType ~= zoneState.populationType or profile.dealerStrength ~= zoneState.dealerStrength or
        profile.raceFlags ~= zoneState.races or profile.noCops ~= zoneState.noCops then
        return false
    end
    for index = 1, 10 do
        if gangWeights[index] ~= zoneState.gangStrengths[index] then
            return false
        end
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
        zoneLabel = profile.zoneLabel,
        raceFlags = profile.raceFlags,
        noCops = profile.noCops,
        worldLevel = profile.worldLevel,
        copSuppressionFlags = profile.copSuppressionFlags,
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

local function calculateNativeTargets(profile, applyDemoDensity)
    -- Retail gates AddToPopulation with ms_nTotalPeds, which deliberately
    -- excludes dealers, and only then compares each family's own deficit.
    -- Preserve those two notions instead of charging dealers against the
    -- stock-counted gate.
    local civilianNativeTarget = profile.civilianTarget * populationWorld.densityMultiplier
    local dealerNativeTarget = profile.dealerTarget * populationWorld.densityMultiplier
    local gangNativeTarget = populationWorld.randomGangMembers and profile.gangTarget * populationWorld.densityMultiplier or 0
    local copNativeTarget = profile.effectiveCopTarget * populationWorld.densityMultiplier
    local supportedGangWeight = 0
    for index = 1, 8 do supportedGangWeight = supportedGangWeight + profile.gangWeights[index] end
    if profile.totalGangWeight > 0 and supportedGangWeight < profile.totalGangWeight then
        gangNativeTarget = gangNativeTarget * supportedGangWeight / profile.totalGangWeight
    end
    local fullPopulationTarget = civilianNativeTarget + dealerNativeTarget + gangNativeTarget + copNativeTarget
    if fullPopulationTarget <= 0 then
        return 0, 0, 0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
    end

    -- The demo deliberately preserves the current zone's civilian/gang/cop/
    -- dealer mix while scaling its combined target. It remains bounded by the
    -- ordinary global and per-cell admission fences, so disabling the demo can
    -- return to retail density without leaving a second population lifecycle.
    if applyDemoDensity then
        local scale = pedTrafficDemoDensity.target / fullPopulationTarget
        civilianNativeTarget = civilianNativeTarget * scale
        dealerNativeTarget = dealerNativeTarget * scale
        gangNativeTarget = gangNativeTarget * scale
        copNativeTarget = copNativeTarget * scale
        fullPopulationTarget = pedTrafficDemoDensity.target
    end

    local fullPopulationGate = applyDemoDensity and pedTrafficDemoDensity.target or
        profile.pedDensityMultiplier * profile.fewerPedsMultiplier * math.min(profile.maximumPedsInUse, fullPopulationTarget)
    local total = math.max(0, math.ceil(fullPopulationGate - 0.0001))
    local gangTargets = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
    if gangNativeTarget > 0 and supportedGangWeight > 0 then
        for index = 1, 8 do
            gangTargets[index] = gangNativeTarget * profile.gangWeights[index] / supportedGangWeight
        end
    end
    return total, civilianNativeTarget, dealerNativeTarget, copNativeTarget, gangNativeTarget, gangTargets
end

local function getPopulationRadii(profile, resume)
    if not profile then
        return false
    end
    local scaledDistance = profile.creationDistanceMultiplier * profile.generationDistanceMultiplier
    -- Retail ManagePed uses 54.5 as its outer lifecycle boundary, but a ped
    -- returning from that boundary must cross back inside 50.5. Preserve that
    -- spatial hysteresis so a frozen edge pose cannot suspend/resume forever.
    local civilian = scaledDistance * (resume and 50.5 or 54.5)
    return {
        civilian = civilian,
        gang = civilian + 30,
        maximum = civilian + 30,
    }
end

local function getNativeTargetsNearPlayer(player)
    local profile = populationProfiles[player]
    if not profile or profile.worldRevision ~= populationWorld.revision or getTickCount() - profile.receivedAt > 2500 then
        return false
    end
    return calculateNativeTargets(profile, pedTrafficDemoDensity.enabled and player == pedTrafficDemoDensity.anchor)
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

local function getPopulationWorldConvergencePlayerCount()
    local count = 0
    for _, player in ipairs(getElementsByType("player")) do
        if isEligiblePlayer(player) and not isPopulationWorldReady(player) then
            count = count + 1
        end
    end
    return count
end

local function getPopulationRadiusForClass(profile, populationClass)
    local radii = getPopulationRadii(profile)
    return radii and (populationClass == "gang" and radii.gang or radii.civilian) or false
end

local function findClosestPopulationResident(x, y, z, populationClass, excludedPlayer, resume)
    local closest, closestDistanceSquared
    for _, player in ipairs(getEligiblePlayers()) do
        if player ~= excludedPlayer then
            local profile = populationProfiles[player]
            local radii = profile and profile.worldRevision == populationWorld.revision and getPopulationRadii(profile, resume) or false
            local radius = radii and (populationClass == "gang" and radii.gang or radii.civilian) or false
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
        return 0, 0, 0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
    end

    local x, y, z = getElementPosition(player)
    local stockCounted = 0
    local physical = 0
    local civilians = 0
    local dealers = 0
    local cops = 0
    local gangs = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
    for ped, record in pairs(trafficPeds) do
        if isElement(ped) then
            local px, py, pz = getElementPosition(ped)
            local radius = record.populationClass == "gang" and radii.gang or radii.civilian
            if squaredDistance2D(x, y, px, py) <= radius * radius then
                physical = physical + 1
                if record.populationClass == "gang" and type(record.gang) == "number" then
                    stockCounted = stockCounted + 1
                    gangs[record.gang + 1] = gangs[record.gang + 1] + 1
                elseif record.populationClass == "dealer" then
                    dealers = dealers + 1
                elseif record.populationClass == "cop" then
                    stockCounted = stockCounted + 1
                    cops = cops + 1
                else
                    stockCounted = stockCounted + 1
                    civilians = civilians + 1
                end
            end
        end
    end
    return stockCounted, physical, civilians, dealers, cops, gangs
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
    local totalTarget, civilianTarget, dealerTarget, copTarget, gangTarget, gangTargets = getNativeTargetsNearPlayer(player)
    if totalTarget == false or not profile then
        return false
    end

    local stockCountedLive, physicalLive, civilianCount, dealerCount, copCount, gangCounts = getPopulationCountsNearPlayer(player)
    if pedTrafficDemoDensity.enabled and player == pedTrafficDemoDensity.anchor and
        (physicalLive >= pedTrafficDemoDensity.target or getTrafficPedCount() >= pedTrafficDemoDensity.target) then
        return false
    end
    if stockCountedLive >= totalTarget then
        return false
    end

    local totalGangCount = 0
    for index = 1, 10 do
        totalGangCount = totalGangCount + gangCounts[index]
    end
    local civilianDeficit = civilianTarget - civilianCount
    local dealerDeficit = dealerTarget - dealerCount
    local gangDeficit = gangTarget - totalGangCount
    local copDeficit = copTarget - copCount
    local civilianChance = civilianDeficit
    local dealerChance = dealerDeficit
    local gangChance = gangDeficit
    local copChance = copDeficit
    local demoFallback = false
    -- This small independent randomization is part of FindNewPedType itself;
    -- it prevents low remaining deficits from producing a rigid cadence.
    if civilianChance < 2 then civilianChance = civilianChance * math.random() end
    if dealerChance < 2 then dealerChance = dealerChance * math.random() end
    if gangChance < 2 then gangChance = gangChance * math.random() end
    if copChance < 2 then copChance = copChance * math.random() end

    if pedTrafficDemoDensity.enabled and player == pedTrafficDemoDensity.anchor then
        local nativeGroupCount = 0
        for _, group in pairs(trafficGroups) do
            if not group.removing and (group.owner == player or group.pendingOwner == player) then nativeGroupCount = nativeGroupCount + 1 end
        end
        if nativeGroupCount >= config.maximumNativeGangGroups then gangChance = -math.huge end
        if math.max(civilianChance, dealerChance, gangChance, copChance) <= 0 and
            physicalLive < pedTrafficDemoDensity.target and getTrafficPedCount() < pedTrafficDemoDensity.target then
            -- GTA exposes only five safe ambient group slots per owner. Once a
            -- gang-heavy zone consumes them, alternate already resident dealer
            -- and cop models to complete the visual target. A civilian-only
            -- fallback can return no model indefinitely in gang-only zones.
            pedTrafficDemoDensity.fallbackCursor = pedTrafficDemoDensity.fallbackCursor % 2 + 1
            if pedTrafficDemoDensity.fallbackCursor == 1 then
                dealerChance = pedTrafficDemoDensity.target - physicalLive
            else
                copChance = pedTrafficDemoDensity.target - physicalLive
            end
            demoFallback = true
        end
    end

    if math.max(civilianChance, dealerChance, gangChance, copChance) <= 0 then
        return false
    end

    local selection = {
        profileSignature = profile.signature,
        totalTarget = totalTarget,
        civilianTarget = civilianTarget,
        dealerTarget = dealerTarget,
        copTarget = copTarget,
        gangTarget = gangTarget,
        gangTargets = gangTargets,
        totalCount = stockCountedLive,
        physicalCount = physicalLive,
        civilianCount = civilianCount,
        dealerCount = dealerCount,
        copCount = copCount,
        gangCounts = gangCounts,
        totalGangCount = totalGangCount,
        civilianDeficit = civilianDeficit,
        dealerDeficit = dealerDeficit,
        gangDeficit = gangDeficit,
        copDeficit = copDeficit,
        civilianChance = civilianChance,
        dealerChance = dealerChance,
        gangChance = gangChance,
        copChance = copChance,
        demoEpoch = pedTrafficDemoDensity.enabled and player == pedTrafficDemoDensity.anchor and pedTrafficDemoDensity.epoch or false,
        demoPhysicalCeiling = pedTrafficDemoDensity.enabled and player == pedTrafficDemoDensity.anchor and pedTrafficDemoDensity.target or false,
        demoFallback = demoFallback,
    }
    -- FindNewPedType's strict tie order is dealer, gang, cop, civilian.
    if dealerChance >= gangChance and dealerChance >= copChance and dealerChance >= civilianChance then
        selection.populationClass = "dealer"
        selection.gang = false
    elseif gangChance >= copChance and gangChance >= civilianChance then
        local gang, scores = chooseGangFromDeficit(profile, gangTarget, gangCounts)
        if gang == false then
            return false
        end
        selection.populationClass = "gang"
        selection.gang = gang
        selection.gangScores = scores
        selection.gangScore = scores[gang + 1]
    elseif copChance >= civilianChance then
        selection.populationClass = "cop"
        selection.gang = false
    else
        selection.populationClass = "civilian"
        selection.gang = false
    end
    return selection
end

local function isSelectionStillNeeded(player, selection)
    local profile = populationProfiles[player]
    local totalTarget, civilianTarget, dealerTarget, copTarget, gangTarget = getNativeTargetsNearPlayer(player)
    if totalTarget == false or not profile or profile.signature ~= selection.profileSignature then
        return false, "stale-population-profile"
    end

    local stockCountedLive, physicalLive, civilianCount, dealerCount, copCount, gangCounts = getPopulationCountsNearPlayer(player)
    if selection.demoEpoch then
        if not pedTrafficDemoDensity.enabled or player ~= pedTrafficDemoDensity.anchor or
            selection.demoEpoch ~= pedTrafficDemoDensity.epoch or physicalLive >= selection.demoPhysicalCeiling or
            getTrafficPedCount() >= selection.demoPhysicalCeiling then
            return false, "demo-density-stale-or-full"
        end
    end
    if stockCountedLive ~= selection.totalCount or physicalLive ~= selection.physicalCount or civilianCount ~= selection.civilianCount or
        dealerCount ~= selection.dealerCount or copCount ~= selection.copCount then
        return false, "population-selection-stale"
    end
    for index = 1, 10 do
        if gangCounts[index] ~= selection.gangCounts[index] then
            return false, "population-selection-stale"
        end
    end
    if stockCountedLive >= totalTarget then
        return false, "population-total-target"
    end
    if selection.demoFallback then return true end
    if selection.populationClass == "civilian" then
        if civilianTarget - civilianCount <= 0 then
            return false, "population-selection-stale"
        end
        return true
    end
    if selection.populationClass == "dealer" then
        if dealerTarget - dealerCount <= 0 then
            return false, "population-selection-stale"
        end
        return true
    end
    if selection.populationClass == "cop" then
        if copTarget - copCount <= 0 then
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
            local nativeTarget, civilianTarget, dealerTarget, copTarget, _, gangTargets = getNativeTargetsNearPlayer(player)
            if nativeTarget == false then
                return false
            end
            local _, _, civilianCount, dealerCount, copCount, gangCounts = getPopulationCountsNearPlayer(player)
            if record.populationClass == "gang" then
                if gangCounts[record.gang + 1] - gangTargets[record.gang + 1] < 1 then
                    return false
                end
            elseif record.populationClass == "dealer" then
                if dealerCount - dealerTarget < 1 then
                    return false
                end
            elseif record.populationClass == "cop" then
                if copCount - copTarget < 1 then
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
        if isElement(ped) and not record.group and not record.couple and not record.removing and record.state == "active" and not isPedDead(ped) and not record.airTest and
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

local function isCurrentPopulationOwnerSpatiallyResident(record)
    local owner = record and record.owner
    local profile = isEligiblePlayer(owner) and populationProfiles[owner] or false
    local radius = profile and getPopulationRadiusForClass(profile, record.populationClass) or false
    if not radius then
        return false
    end
    local pedX, pedY = getElementPosition(record.ped)
    local ownerX, ownerY = getElementPosition(owner)
    return squaredDistance2D(pedX, pedY, ownerX, ownerY) <= radius * radius
end

local function isCurrentGroupOwnerSpatiallyResident(group)
    local owner = group and group.owner
    local profile = isEligiblePlayer(owner) and populationProfiles[owner] or false
    local radii = profile and getPopulationRadii(profile) or false
    local distanceSquared = radii and getGroupDistanceSquaredToPlayer(group, owner) or false
    return distanceSquared and distanceSquared <= radii.gang * radii.gang or false
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
                        local nativeTarget, _, _, _, _, gangTargets = getNativeTargetsNearPlayer(player)
                        if nativeTarget == false then
                            surplusForEveryResident = false
                            break
                        end
                        local stockCountedLive, _, _, _, _, gangCounts = getPopulationCountsNearPlayer(player)
                        if (requireTotalSurplus and stockCountedLive - nativeTarget < memberCount) or
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
    local dealers = 0
    local cops = 0
    local gangs = {[0] = 0, [1] = 0, [2] = 0, [3] = 0, [4] = 0, [5] = 0, [6] = 0, [7] = 0, [8] = 0, [9] = 0}
    for ped, record in pairs(trafficPeds) do
        if isElement(ped) then
            if record.populationClass == "gang" and type(record.gang) == "number" then
                gangs[record.gang] = gangs[record.gang] + 1
            elseif record.populationClass == "dealer" then
                dealers = dealers + 1
            elseif record.populationClass == "cop" then
                cops = cops + 1
            else
                civilians = civilians + 1
            end
        end
    end
    return civilians, dealers, cops, gangs
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
local assignOwner

local function removeRecord(record, reason)
    if not record or record.removing then
        return
    end
    if record.group then
        return removeGroup(record.group, reason)
    end
    if record.couple then
        return coupleRuntime.remove(record.couple, reason)
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

local function bridgeDamageResponse(record, attackingPlayer, weapon, bodypart, force, damageId)
    if not record or record.removing or record.state ~= "active" or not isElement(record.owner) or not isElement(attackingPlayer) or
        (record.owner == attackingPlayer and force ~= true) then
        return false
    end

    log(("damage-bridge id=%d damage=%s attacker=%s owner=%s weapon=%d bodypart=%d"):format(
            record.id, tostring(damageId or false), getPlayerName(attackingPlayer), getPlayerName(record.owner), weapon, bodypart))
    return triggerClientEvent(record.owner, "pedTraffic:damageResponse", resourceRoot, record.ped, attackingPlayer, weapon, bodypart,
                              damageId or false)
end

local function rememberGroupCombatContext(record, attackingPlayer, weapon, bodypart, source, observerNonce)
    if not record or not record.group or record.group.removing or not isElement(attackingPlayer) or
        getElementType(attackingPlayer) ~= "player" then
        return false
    end
    local group = record.group
    local now = getTickCount()
    local canonicalWeapon = math.floor(tonumber(weapon) or 0)
    local canonicalBodypart = math.floor(tonumber(bodypart) or 0)
    local sourceName = tostring(source or "unknown")
    local context = group.combatContext
    local matchesCurrent = context and context.target == record and context.attacker == attackingPlayer and
        context.weapon == canonicalWeapon and context.bodypart == canonicalBodypart and now - context.observedAt <= 1000
    local canMerge = matchesCurrent and
        (sourceName == "server-ped-damage" and context.serverObserved ~= true or
         sourceName == "observer-bridge" and context.observerNonce == nil)

    if not canMerge then
        nextGroupDamageId = nextGroupDamageId % 2147483647 + 1
        context = {
            damageId = nextGroupDamageId,
            target = record,
            attacker = attackingPlayer,
            weapon = canonicalWeapon,
            bodypart = canonicalBodypart,
            observedAt = now,
            source = sourceName,
            decisionState = "pending",
        }
        group.combatContext = context
    end
    context.observedAt = now
    context.source = sourceName
    if sourceName == "server-ped-damage" then
        context.serverObserved = true
        -- An active owner receives the authentic GTA damage event through the
        -- ordinary damage pipeline. Remember it for diagnostics, but never
        -- turn a later technical rebuild into a second decision-maker query.
        if group.state == "active" and isElement(group.owner) then
            context.decisionState = "native-observed"
        end
    elseif sourceName == "observer-bridge" then
        context.observerNonce = observerNonce
    end
    writePopulationTrace("group_combat_context", {
        group_id = group.id,
        traffic_id = record.id,
        damage_id = context.damageId,
        epoch = group.epoch,
        owner_id = isElement(group.owner) and getPopulationClientId(group.owner) or false,
        attacker_id = getPopulationClientId(attackingPlayer),
        weapon = context.weapon,
        bodypart = context.bodypart,
        source = context.source,
        decision_state = context.decisionState,
        merged = canMerge == true,
        observer_nonce = observerNonce or false,
    })
    return context
end

local function rememberDealerCombatContext(record, attackingPlayer, weapon, bodypart, source)
    if not record or record.group or record.populationClass ~= "dealer" or record.removing or not isElement(attackingPlayer) or
        getElementType(attackingPlayer) ~= "player" then
        return false
    end
    record.combatContext = {
        attacker = attackingPlayer,
        weapon = math.floor(tonumber(weapon) or 0),
        bodypart = math.floor(tonumber(bodypart) or 0),
        observedAt = getTickCount(),
        source = tostring(source or "unknown"),
    }
    writePopulationTrace("dealer_combat_context", {
        traffic_id = record.id,
        epoch = record.epoch,
        owner_id = isElement(record.owner) and getPopulationClientId(record.owner) or false,
        attacker_id = getPopulationClientId(attackingPlayer),
        weapon = record.combatContext.weapon,
        bodypart = record.combatContext.bodypart,
        source = record.combatContext.source,
    })
    return true
end

local function restoreDealerCombatContext(record)
    local context = record and record.combatContext
    if not context then
        return false
    end

    local pedX, pedY, pedZ
    local attackerX, attackerY, attackerZ
    if isElement(record.ped) then
        pedX, pedY, pedZ = getElementPosition(record.ped)
    end
    if isElement(context.attacker) then
        attackerX, attackerY, attackerZ = getElementPosition(context.attacker)
    end
    local contextAge = getTickCount() - context.observedAt
    if contextAge <= 8000 and not record.removing and isElement(record.ped) and isElement(context.attacker) and
        getElementHealth(context.attacker) > 0 and getElementDimension(context.attacker) == getElementDimension(record.ped) and
        getElementInterior(context.attacker) == getElementInterior(record.ped) and pedX and attackerX and
        squaredDistance(pedX, pedY, pedZ, attackerX, attackerY, attackerZ) <= 250 * 250 then
        local restored = bridgeDamageResponse(record, context.attacker, context.weapon, context.bodypart, true)
        writePopulationTrace("dealer_combat_context_restored", {
            traffic_id = record.id,
            epoch = record.epoch,
            owner_id = isElement(record.owner) and getPopulationClientId(record.owner) or false,
            attacker_id = getPopulationClientId(context.attacker),
            weapon = context.weapon,
            bodypart = context.bodypart,
            accepted = restored == true,
            context_age_ms = contextAge,
        })
        return restored
    end

    writePopulationTrace("dealer_combat_context_expired", {
        traffic_id = record.id,
        epoch = record.epoch,
        context_age_ms = contextAge,
    })
    record.combatContext = nil
    return false
end

assignOwner = function(record, owner, reason)
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
    -- The release handshake can take up to two seconds. Re-resolve residency
    -- at commit time instead of installing a now-stale pending owner and
    -- immediately revoking the freshly recreated Wander task.
    local x, y, z = getElementPosition(record.ped)
    local owner = findClosestPopulationResident(x, y, z, record.populationClass)
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
    if dealerTest and dealerTest.record == record then
        dealerTest.unexpectedSuspensionReason = tostring(reason)
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

local function findClosestGroupResident(group, excludedPlayer, requireCapacity, resume)
    local closest, closestDistanceSquared
    for _, player in ipairs(getEligiblePlayers()) do
        if player ~= excludedPlayer and
            (not requireCapacity or countNativeGroupsForOwner(player, group) < config.maximumNativeGangGroups) then
            local profile = populationProfiles[player]
            local radii = profile and profile.worldRevision == populationWorld.revision and getPopulationRadii(profile, resume) or false
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
    if bikeJackTest and bikeJackTest.group == group and stopBikeJackTest then
        stopBikeJackTest("FAIL", "group-removed:" .. tostring(reason))
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
    -- Group release is asynchronous too; only commit an owner that is still a
    -- collision resident when the old native task has actually been released.
    local owner = findClosestGroupResident(group, nil, true)
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
    group.suspendedAt = getTickCount()
    -- The three-second active hold is not despawn grace. Start the removal
    -- window only after the old native owner has actually released the group.
    group.outsideResidencySince = group.suspendedAt
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

local function clearGroupZeroResidentHold(group, reason)
    if not group or not group.outsideResidencySince then
        return
    end
    writePopulationTrace("group_collision_residency_hold_released", {
        scenario_id = residencyTest and residencyTest.group == group and residencyTest.id or false,
        group_id = group.id,
        epoch = group.epoch,
        owner_id = isElement(group.owner) and getPopulationClientId(group.owner) or false,
        hold_age_ms = getTickCount() - group.outsideResidencySince,
        reason = tostring(reason),
    })
    group.outsideResidencySince = nil
end

local function updateGroupZeroResidentHold(group, reason, now)
    if not group or group.removing or group.state ~= "active" then
        return false
    end
    now = now or getTickCount()
    if not isEligiblePlayer(group.owner) then
        beginGroupSuspension(group, "group-owner-ineligible")
        return true
    end
    if not group.outsideResidencySince then
        group.outsideResidencySince = now
        writePopulationTrace("group_collision_residency_hold_started", {
            scenario_id = residencyTest and residencyTest.group == group and residencyTest.id or false,
            group_id = group.id,
            epoch = group.epoch,
            owner_id = getPopulationClientId(group.owner),
            hold_ms = config.handoffHold,
            reason = tostring(reason),
        })
        return false
    end
    if now - group.outsideResidencySince < config.handoffHold then
        return false
    end
    writePopulationTrace("group_collision_residency_hold_expired", {
        scenario_id = residencyTest and residencyTest.group == group and residencyTest.id or false,
        group_id = group.id,
        epoch = group.epoch,
        owner_id = getPopulationClientId(group.owner),
        hold_age_ms = now - group.outsideResidencySince,
        reason = tostring(reason),
    })
    -- Spatial departure alone must not tear down a healthy native group.
    -- Retail removes distant peds only once they are no longer visible; a
    -- suspend/resume cycle here destroys and recreates the whole group task
    -- whenever its wander crosses the population boundary.
    return true
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

-- Couple assignment and presentation need a short watchdog cadence. Spatial
-- ownership itself remains sticky and uses the continuous-residency hold below;
-- a single boundary sample must never tear down the native locomotion task.
setTimer(function()
    if not enabled then
        return
    end
    local now = getTickCount()

    local couples = {}
    for _, relation in pairs(coupleRuntime.relations) do couples[#couples + 1] = relation end
    for _, relation in ipairs(couples) do
        if not relation.removing then
            if relation.state == "assigning" then
                if now - (relation.assignmentStartedAt or now) >= 10000 then
                    coupleRuntime.remove(relation, "couple-assignment-timeout")
                elseif now - (relation.assignmentLastSent or 0) >= 1000 then
                    relation.assignmentLastSent = now
                    coupleRuntime.sendAssignments(relation, "couple-assignment-retry")
                end
            elseif relation.state == "revoking" then
                if now >= (relation.handoffDeadline or 0) then
                    local x, y, z = coupleRuntime.getCentre(relation)
                    local nextOwner = x and findClosestPopulationResident(x, y, z, "civilian") or false
                    if nextOwner then
                        coupleRuntime.assignOwner(relation, nextOwner, "couple-release-timeout")
                    else
                        coupleRuntime.remove(relation, "couple-handoff-timeout")
                    end
                end
            elseif relation.state == "active" then
                -- Idempotently cover observers that joined, streamed in or
                -- recovered their local presentation after the owner commit.
                if now - (relation.presentationLastSent or 0) >= 2000 then
                    coupleRuntime.sendPresentations(relation, "couple-presentation-refresh")
                end
                local x, y, z = coupleRuntime.getCentre(relation)
                local closest = x and findClosestPopulationResident(x, y, z, "civilian") or false
                if not closest then
                    relation.outsideResidencySince = relation.outsideResidencySince or now
                    if now - relation.outsideResidencySince >= config.handoffHold + config.despawnGrace then
                        coupleRuntime.remove(relation, "couple-outside-residency")
                    end
                else
                    relation.outsideResidencySince = nil
                    if not isEligiblePlayer(relation.owner) then
                        coupleRuntime.beginHandoff(relation, closest, "couple-owner-ineligible")
                    else
                        local ownerRadius = getPopulationRadiusForClass(populationProfiles[relation.owner], "civilian")
                        local ownerX, ownerY = getElementPosition(relation.owner)
                        local ownerResident = ownerRadius and squaredDistance2D(x, y, ownerX, ownerY) <= ownerRadius * ownerRadius
                        if not ownerResident and closest ~= relation.owner then
                            if relation.handoffCandidate ~= closest then
                                relation.handoffCandidate = closest
                                relation.handoffCandidateSince = now
                            elseif now - relation.handoffCandidateSince >= config.handoffHold then
                                coupleRuntime.beginHandoff(relation, closest, "couple-owner-left-residency")
                            end
                        else
                            relation.handoffCandidate = nil
                            relation.handoffCandidateSince = nil
                        end
                    end
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
    if not isIntegerInRange(model, 7, 288) or not isIntegerInRange(pedType, 4, 17) or
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
    elseif populationClass == "dealer" then
        if pedType ~= 17 or dealerModels[model] ~= true or (gang ~= false and gang ~= -1) then
            return false, "dealer-contract"
        end
        gang = false
    elseif populationClass == "cop" then
        local profile = populationProfiles[player]
        if pedType ~= 6 or (gang ~= false and gang ~= -1) or not profile or
            not isIntegerInRange(candidate.worldLevel, 0, 3) or candidate.worldLevel ~= profile.worldLevel or
            copModelsByLevel[candidate.worldLevel] ~= model then
            return false, "cop-contract"
        end
        gang = false
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
    -- The coordinated showcase intentionally trades density cost for a busy
    -- street. Production keeps the conservative spatial cap unchanged, and
    -- the ordinary separation check still prevents overlapping actors.
    local maximumPedsPerCell = pedTrafficDemoDensity.enabled and pedTrafficDemoDensity.target or config.maxPerCell
    if countPedsInCell(cellX, cellY) >= maximumPedsPerCell then
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
        local maximumPedsPerCell = pedTrafficDemoDensity.enabled and pedTrafficDemoDensity.target or config.maxPerCell
        if countPedsInCell(cellX, cellY) + plannedCells[cellKey] > maximumPedsPerCell then
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

coupleRuntime.nativePedTypeIds = {
    CIVMALE = 4, CIVFEMALE = 5, COP = 6,
    GANG1 = 7, GANG2 = 8, GANG3 = 9, GANG4 = 10, GANG5 = 11,
    GANG6 = 12, GANG7 = 13, GANG8 = 14, GANG9 = 15, GANG10 = 16,
    DEALER = 17, PROSTITUTE = 18, CRIMINAL = 20, BUM = 21,
}

function coupleRuntime.validateCandidate(player, candidate, selection)
    local members = type(candidate) == "table" and candidate.members or false
    if type(selection) ~= "table" or selection.populationClass ~= "civilian" or selection.coupleAttempt ~= true or
        type(members) ~= "table" or #members ~= 2 then
        return false, "couple-shape"
    end
    if getTrafficPedCount() + 2 > config.globalCap or #getElementsByType("ped") + 2 > config.pedPoolSoftLimit then
        return false, "couple-capacity-changed"
    end

    local profile = populationProfiles[player]
    if not profile then return false, "population-profile-missing" end
    local playerX, playerY, playerZ = getElementPosition(player)
    local visibleMaximum = profile.creationDistanceMultiplier * profile.generationDistanceMultiplier * 50.5
    local hiddenMinimum = math.max(0, profile.creationDistanceMultiplier * 25 - 10)
    local hiddenMaximum = profile.creationDistanceMultiplier * 25
    local maximum = math.max(visibleMaximum, hiddenMaximum) + config.maximumGangGroupSpan
    local plannedCells = {}
    local validated = {}
    for index, member in ipairs(members) do
        if type(member) ~= "table" or not isFiniteNumber(member.x) or not isFiniteNumber(member.y) or
            not isFiniteNumber(member.z) or not isFiniteNumber(member.model) or not isFiniteNumber(member.pedType) or
            not isFiniteNumber(member.direction) then
            return false, "couple-member-" .. tostring(index) .. ":shape"
        end
        if not isIntegerInRange(member.model, 7, 288) or not isIntegerInRange(member.pedType, 4, 21) or
            not isIntegerInRange(member.direction, 0, 7) or member.populationClass ~= "civilian" or
            member.gang ~= false and member.gang ~= -1 then
            return false, "couple-member-" .. tostring(index) .. ":contract"
        end
        local catalog = populationCatalog.models[member.model]
        if not catalog or coupleRuntime.nativePedTypeIds[catalog.nativePedType] ~= member.pedType then
            return false, "couple-member-" .. tostring(index) .. ":catalog"
        end
        local distanceSquared = squaredDistance2D(member.x, member.y, playerX, playerY)
        if distanceSquared < math.max(0, hiddenMinimum - config.maximumGangGroupSpan) ^ 2 or
            distanceSquared > maximum * maximum or math.abs(member.z - playerZ) > 35 then
            return false, "couple-member-" .. tostring(index) .. ":distance"
        end
        if hasNearbyTrafficPed(member.x, member.y, member.z) then
            return false, "couple-member-" .. tostring(index) .. ":separation"
        end
        local cellX, cellY = cellForPosition(member.x, member.y)
        local cellKey = tostring(cellX) .. ":" .. tostring(cellY)
        plannedCells[cellKey] = (plannedCells[cellKey] or 0) + 1
        local maximumPedsPerCell = pedTrafficDemoDensity.enabled and pedTrafficDemoDensity.target or config.maxPerCell
        if countPedsInCell(cellX, cellY) + plannedCells[cellKey] > maximumPedsPerCell then
            return false, "couple-cell-full"
        end
        validated[index] = {candidate = member, model = member.model, direction = member.direction, declaredPedType = member.pedType}
    end
    if members[1].model == members[2].model then return false, "couple-models-not-distinct" end
    local pairDistance = math.sqrt(squaredDistance(members[1].x, members[1].y, members[1].z, members[2].x, members[2].y, members[2].z))
    if pairDistance < 0.5 or pairDistance > 2.0 then return false, "couple-placement-span" end
    return true, validated
end

function coupleRuntime.getCentre(couple)
    local x, y, z, count = 0, 0, 0, 0
    for _, record in ipairs(couple.members or {}) do
        if isElement(record.ped) then
            local px, py, pz = getElementPosition(record.ped)
            x, y, z, count = x + px, y + py, z + pz, count + 1
        end
    end
    if count == 0 then return false end
    return x / count, y / count, z / count
end

function coupleRuntime.sendPresentations(couple, reason)
    if not couple or couple.removing or not isElement(couple.owner) then return false end
    local pedA, pedB = couple.members[1].ped, couple.members[2].ped
    local sideA = couple.armSides and couple.armSides[1] or false
    local sideB = couple.armSides and couple.armSides[2] or false
    for _, observer in ipairs(getEligiblePlayers()) do
        if observer ~= couple.owner then
            triggerClientEvent(observer, "pedTraffic:assignCouplePresentation", resourceRoot, couple.id, couple.epoch,
                               pedA, pedB, reason, sideA, sideB)
        end
    end
    couple.presentationLastSent = getTickCount()
    return true
end

function coupleRuntime.sendAssignments(couple, reason)
    if not couple or couple.removing or not isElement(couple.owner) then return false end
    couple.assignmentLastSent = getTickCount()
    local pedA, pedB = couple.members[1].ped, couple.members[2].ped
    triggerClientEvent(couple.owner, "pedTraffic:assignCouple", resourceRoot, couple.id, couple.epoch, pedA, pedB,
                       couple.leaderIndex or false, reason)
    coupleRuntime.sendPresentations(couple, reason)
    return true
end

function coupleRuntime.assignOwner(couple, owner, reason)
    if not couple or couple.removing or not isEligiblePlayer(owner) or #couple.members ~= 2 then return false end
    couple.owner = owner
    couple.pendingOwner = nil
    couple.epoch = couple.epoch + 1
    couple.armSides = nil
    couple.state = "assigning"
    couple.handoffDeadline = nil
    couple.assignmentStartedAt = getTickCount()
    for index, record in ipairs(couple.members) do
        if not isElement(record.ped) then return false end
        record.owner = owner
        record.epoch = couple.epoch
        record.state = "assigning"
        setElementFrozen(record.ped, true)
        if not setElementSyncer(record.ped, owner, true) then return false end
        setElementData(record.ped, "neon:ambientPedTrafficEpoch", couple.epoch)
        setElementData(record.ped, "neon:ambientPedRelationEpoch", couple.epoch)
        setElementData(record.ped, "neon:ambientPedRelationLeader", couple.leaderIndex == index)
    end
    writePopulationTrace("couple_owner_assigned", {
        relation_id = couple.id,
        relation_epoch = couple.epoch,
        owner_id = getPopulationClientId(owner),
        member_ids = {couple.members[1].id, couple.members[2].id},
        leader_index = couple.leaderIndex or false,
        reason = tostring(reason),
    })
    return coupleRuntime.sendAssignments(couple, reason)
end

function coupleRuntime.remove(couple, reason)
    if not couple or couple.removing then return end
    couple.removing = true
    coupleRuntime.relations[couple.id] = nil
    triggerClientEvent(root, "pedTraffic:revokeCouple", resourceRoot, couple.id, couple.epoch, reason)
    local removed = 0
    for _, record in ipairs(couple.members or {}) do
        record.couple = nil
        record.removing = true
        trafficPeds[record.ped] = nil
        if isElement(record.ped) then destroyElement(record.ped) end
        removed = removed + 1
    end
    stats.despawned = stats.despawned + removed
    writePopulationTrace("couple_removed", {
        relation_id = couple.id,
        relation_epoch = couple.epoch,
        member_count = removed,
        reason = tostring(reason),
    })
end

function coupleRuntime.dissolve(couple, reason)
    if not couple or couple.removing then return end
    coupleRuntime.relations[couple.id] = nil
    local owner = couple.owner
    triggerClientEvent(root, "pedTraffic:revokeCouple", resourceRoot, couple.id, couple.epoch, reason)
    for _, record in ipairs(couple.members) do
        record.couple = nil
        if isElement(record.ped) then
            setElementData(record.ped, "neon:ambientPedRelationId", false)
            setElementData(record.ped, "neon:ambientPedRelationEpoch", false)
            setElementData(record.ped, "neon:ambientPedRelationRole", false)
            setElementData(record.ped, "neon:ambientPedRelationLeader", false)
            if not isPedDead(record.ped) then
                local x, y, z = getElementPosition(record.ped)
                local nextOwner = isEligiblePlayer(owner) and owner or findClosestPopulationResident(x, y, z, "civilian")
                if nextOwner then assignOwner(record, nextOwner, "couple-dissolved") end
            else
                record.state = "active"
            end
        end
    end
    writePopulationTrace("couple_dissolved", {
        relation_id = couple.id,
        relation_epoch = couple.epoch,
        reason = tostring(reason),
    })
end

function coupleRuntime.beginHandoff(couple, newOwner, reason)
    if not couple or couple.removing or couple.state ~= "active" or newOwner == couple.owner or not isEligiblePlayer(newOwner) then return false end
    couple.pendingOwner = newOwner
    couple.state = "revoking"
    couple.handoffDeadline = getTickCount() + config.handoffTimeout
    for _, record in ipairs(couple.members) do
        if isElement(record.ped) then setElementFrozen(record.ped, true) end
    end
    if isElement(couple.owner) then
        -- The epoch is ending for every participant, including presentation-
        -- only observers. The owner ACK remains the handoff fence.
        triggerClientEvent(root, "pedTraffic:revokeCouple", resourceRoot, couple.id, couple.epoch, reason)
    else
        return coupleRuntime.assignOwner(couple, newOwner, "couple-owner-departed")
    end
    writePopulationTrace("couple_handoff_started", {
        relation_id = couple.id,
        relation_epoch = couple.epoch,
        old_owner_id = getPopulationClientId(couple.owner),
        new_owner_id = getPopulationClientId(newOwner),
        reason = tostring(reason),
    })
    return true
end

function coupleRuntime.spawn(player, candidate, selection)
    if not enabled or not isEligiblePlayer(player) then return false, "runtime-unavailable" end
    local valid, validatedOrReason = coupleRuntime.validateCandidate(player, candidate, selection)
    if not valid then return false, validatedOrReason end
    local needed, staleReason = isSelectionStillNeeded(player, selection)
    if not needed then return false, staleReason end

    nextCoupleRelationId = nextCoupleRelationId + 1
    local couple = {
        id = nextCoupleRelationId,
        epoch = 0,
        members = {},
        owner = nil,
        state = "created",
        createdAt = getTickCount(),
        rngSample = selection.coupleSample,
    }
    coupleRuntime.relations[couple.id] = couple
    for index, member in ipairs(validatedOrReason) do
        local source = member.candidate
        local ped = createPed(member.model, source.x, source.y, source.z, tonumber(source.heading) or 0)
        if not ped then
            stats.coupleRollbacks = stats.coupleRollbacks + 1
            coupleRuntime.remove(couple, "couple-create-refused-" .. tostring(index))
            return false, "couple-create-ped"
        end
        nextPedId = nextPedId + 1
        local record = {
            id = nextPedId,
            ped = ped,
            owner = nil,
            epoch = 0,
            direction = member.direction,
            populationClass = "civilian",
            logicalPedType = index == 1 and 4 or 5,
            declaredPedType = member.declaredPedType,
            gang = false,
            state = "created",
            createdAt = couple.createdAt,
            catalogRevision = populationCatalog.revision,
            couple = couple,
            coupleIndex = index,
        }
        couple.members[index] = record
        trafficPeds[ped] = record
        setElementDimension(ped, 0)
        setElementInterior(ped, 0)
        setElementData(ped, "neon:ambientPedTraffic", true)
        setElementData(ped, "neon:ambientPedTrafficId", record.id)
        setElementData(ped, "neon:ambientPedPopulationClass", "civilian")
        setElementData(ped, "neon:ambientPedLogicalType", record.logicalPedType)
        setElementData(ped, "neon:ambientPedDeclaredType", record.declaredPedType)
        setElementData(ped, "neon:ambientPedCatalogRevision", populationCatalog.revision)
        setElementData(ped, "neon:ambientPedRelationId", couple.id)
        setElementData(ped, "neon:ambientPedRelationRole", index == 1 and "a" or "b")
        if not setPedFightingStyle(ped, 4) or not setPedUseNativeWalkingStyle(ped, true) then
            stats.coupleRollbacks = stats.coupleRollbacks + 1
            coupleRuntime.remove(couple, "couple-initialization-refused")
            return false, "couple-initialization-refused"
        end
    end

    beginSpawnFade({couple.members[1].ped, couple.members[2].ped})
    local centreX, centreY, centreZ = coupleRuntime.getCentre(couple)
    local owner = centreX and findClosestPopulationResident(centreX, centreY, centreZ, "civilian") or false
    if not owner or not coupleRuntime.assignOwner(couple, owner, "couple-spawn") then
        stats.coupleRollbacks = stats.coupleRollbacks + 1
        coupleRuntime.remove(couple, "couple-no-owner")
        return false, "couple-no-owner"
    end
    stats.spawned = stats.spawned + 2
    stats.coupleSpawns = stats.coupleSpawns + 1
    for _, member in ipairs(validatedOrReason) do
        stats.spawnedModels[member.model] = (stats.spawnedModels[member.model] or 0) + 1
    end
    writePopulationTrace("couple_spawn", {
        request_id = selection.requestId,
        relation_id = couple.id,
        relation_epoch = couple.epoch,
        member_ids = {couple.members[1].id, couple.members[2].id},
        models = {validatedOrReason[1].model, validatedOrReason[2].model},
        declared_types = {validatedOrReason[1].declaredPedType, validatedOrReason[2].declaredPedType},
        rng_sample15 = selection.coupleSample,
        owner_id = getPopulationClientId(owner),
        position = {x = centreX, y = centreY, z = centreZ},
    })
    return true
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
        local weapon, armedRoll, slotRoll, weaponSlotCount =
            selectGangPedWeapon(populationWorld.gangWeapons[selection.gang])
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
            weapon = weapon,
            weaponAmmo = weapon ~= 0 and config.nativeGangWeaponAmmo or 0,
            weaponClipAmmo = weapon ~= 0 and gangWeaponClipAmmo[weapon] or 0,
            armedRoll = armedRoll,
            weaponSlotRoll = slotRoll,
            weaponSlotCount = weaponSlotCount,
            catalogRevision = populationCatalog.revision,
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
        setElementData(ped, "neon:ambientPedCatalogRevision", populationCatalog.revision)
        setElementData(ped, "neon:ambientPedWeapon", weapon)
        setElementData(ped, "neon:ambientPedWeaponAmmo", record.weaponAmmo)
        setElementData(ped, "neon:ambientPedWeaponClipAmmo", record.weaponClipAmmo)
        if not applyGangPedWeapon(ped, weapon) then
            removeGroup(group, "group-vanilla-weapon-refused")
            return false, "group-vanilla-weapon"
        end
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
    local weapons = {}
    local weaponSelections = {}
    local spawnTransforms = {}
    for _, member in ipairs(validated) do
        stats.spawnedModels[member.model] = (stats.spawnedModels[member.model] or 0) + 1
        models[#models + 1] = tostring(member.model)
        modelIds[#modelIds + 1] = member.model
    end
    for _, record in ipairs(group.members) do
        memberIds[#memberIds + 1] = record.id
        weapons[#weapons + 1] = record.weapon
        weaponSelections[#weaponSelections + 1] = {
            traffic_id = record.id,
            weapon = record.weapon,
            ammo = record.weaponAmmo,
            clip_ammo = record.weaponClipAmmo,
            armed_roll = record.armedRoll,
            slot_roll = record.weaponSlotRoll,
            nonzero_slot_count = record.weaponSlotCount,
        }
        local x, y, z = getElementPosition(record.ped)
        local _, _, heading = getElementRotation(record.ped)
        spawnTransforms[#spawnTransforms + 1] = ("id=%d:(%.2f,%.2f,%.2f)@%.1f"):format(record.id, x, y, z, heading)
    end
    log(("group-spawn group=%d gang=%d members=%d models=%s weapons=%s owner=%s epoch=%d target=%.2f live=%d pos=%.1f,%.1f,%.1f"):format(
            group.id, group.gang, memberCount, table.concat(models, "/"), table.concat(weapons, "/"), getPlayerName(owner), group.epoch,
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
        weapons = weapons,
        weapon_selections = weaponSelections,
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
        logicalPedType = candidate.pedType,
        gang = gang,
        state = "created",
        createdAt = getTickCount(),
        catalogRevision = populationCatalog.revision,
        worldLevel = candidate.worldLevel,
    }
    if populationClass == "dealer" then
        record.dealerFightSeed = math.random(0, 1023)
        record.dealerFightArmed = false
        record.dealerHasKnife = false
        record.dealerHasPistol = false
    end
    trafficPeds[ped] = record
    setElementDimension(ped, 0)
    setElementInterior(ped, 0)
    -- These two small immutable values let every observer correlate bounded
    -- telemetry for the same network ped without synchronizing AI state.
    setElementData(ped, "neon:ambientPedTraffic", true)
    setElementData(ped, "neon:ambientPedTrafficId", record.id)
    setElementData(ped, "neon:ambientPedPopulationClass", populationClass)
    setElementData(ped, "neon:ambientPedLogicalType", record.logicalPedType)
    setElementData(ped, "neon:ambientPedGang", gang)
    setElementData(ped, "neon:ambientPedCatalogRevision", populationCatalog.revision)
    if populationClass == "dealer" then
        setElementData(ped, "neon:ambientPedWeapon", 0)
        setElementData(ped, "neon:ambientPedWeaponAmmo", 0)
        setElementData(ped, "neon:ambientPedDealerKnife", false)
        setElementData(ped, "neon:ambientPedDealerPistol", false)
        setElementData(ped, "neon:ambientPedDealerFightArmed", false)
    elseif populationClass == "cop" then
        setElementData(ped, "neon:ambientPedCopLevel", record.worldLevel)
        giveWeapon(ped, 3, 1000, false)
        giveWeapon(ped, 22, 1000, false)
        setPedWeaponSlot(ped, 0)
        setPedArmor(ped, 0)
        setPedStat(ped, 69, 40)
        setElementData(ped, "neon:ambientPedWeapon", 0)
        setElementData(ped, "neon:ambientPedCopSafe", true)
    end
    beginSpawnFade({ped})

    if populationClass == "dealer" and getPedWeapon(ped) ~= 0 then
        removeRecord(record, "dealer-initial-weapon-mismatch")
        return false, "dealer-initial-weapon"
    end

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
    log(("spawn id=%d model=%d class=%s pedType=%d gang=%s owner=%s target=%.2f/%.2f/%.2f live=%d/%d/%d deficit=%.2f/%.2f/%.2f roll=%.2f/%.2f/%.2f gangScore=%.3f pos=%.1f,%.1f,%.1f"):format(
            record.id, modelOrReason, populationClass, record.logicalPedType, tostring(gang), getPlayerName(owner), selection.civilianTarget,
            selection.dealerTarget, selection.gangTarget, selection.civilianCount, selection.dealerCount, selection.totalGangCount,
            selection.civilianDeficit, selection.dealerDeficit, selection.gangDeficit, selection.civilianChance, selection.dealerChance,
            selection.gangChance, selection.gangScore or 0, candidate.x, candidate.y, candidate.z))
    writePopulationTrace("spawn", {
        request_id = selection.requestId,
        player_id = getPopulationClientId(player),
        traffic_id = record.id,
        model = modelOrReason,
        population_class = populationClass,
        logical_ped_type = record.logicalPedType,
        catalog_revision = record.catalogRevision,
        initial_weapon = getPedWeapon(ped),
        dealer_fight_seed = record.dealerFightSeed or false,
        task_profile = populationClass == "cop" and "wander-cop-ambient" or "wander-standard",
        gang = gang,
        targets = {civilian = selection.civilianTarget, dealer = selection.dealerTarget, cop = selection.copTarget, gang = selection.gangTarget},
        live_before = {civilian = selection.civilianCount, dealer = selection.dealerCount, cop = selection.copCount, gang = selection.totalGangCount},
        deficits = {civilian = selection.civilianDeficit, dealer = selection.dealerDeficit, cop = selection.copDeficit, gang = selection.gangDeficit},
        position = {x = candidate.x, y = candidate.y, z = candidate.z},
    })
    return true
end

local function getCandidateVisibilityProbes(candidate, populationClass)
    local source
    if type(candidate) == "table" and type(candidate.members) == "table" then
        source = candidate.members
    elseif populationClass == "gang" and type(candidate) == "table" then
        -- The gang oracle returns its members as the top-level Lua array,
        -- while the couple oracle uses a keyed `members` array and singleton
        -- candidates use scalar fields. Normalize those three wire shapes
        -- before asking every resident camera to veto an on-screen spawn.
        source = candidate
    else
        source = {candidate}
    end
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
    if check.request.selection.coupleAttempt == true then
        created, spawnReason = coupleRuntime.spawn(check.player, check.candidate, check.request.selection)
    elseif check.request.selection.populationClass == "gang" then
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
    if request.selection.coupleAttempt == true then
        valid, reason = coupleRuntime.validateCandidate(player, candidate, request.selection)
    elseif request.selection.populationClass == "gang" then
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
    pendingNativeBikeJacks = {}
end

stopBikeJackTest = function(outcome, reason)
    local test = bikeJackTest
    if not test then
        return
    end
    bikeJackTest = false
    if isTimer(test.timer) then
        killTimer(test.timer)
    end
    if isTimer(test.seatTimer) then
        killTimer(test.seatTimer)
    end
    if isTimer(test.readyTimer) then
        killTimer(test.readyTimer)
    end
    writePopulationTrace("native_bike_jack_test_result", {
        scenario_id = test.id,
        outcome = outcome,
        reason = reason,
        group_id = test.group and test.group.id or false,
        epoch = test.group and test.group.epoch or false,
        expected_attacker_id = test.attacker and test.attacker.id or false,
        damage_target_id = test.damageTarget and test.damageTarget.id or false,
        driver_id = isElement(test.driver) and getPopulationClientId(test.driver) or false,
        passenger_id = isElement(test.passenger) and getPopulationClientId(test.passenger) or false,
    })
    local color = outcome == "PASS" and {120, 255, 160} or {255, 160, 80}
    outputChatBox(("Native bike-jack test %s: %s (scenario %d)"):format(outcome, tostring(reason), test.id),
                  root, color[1], color[2], color[3])
    -- The deterministic harness owns its synthetic group, but removeGroup
    -- calls back here when ordinary lifecycle cleanup wins the race. Avoid
    -- recursing in that path and let the original removal finish.
    if test.ownsGroup and test.group and not test.group.removing and
        not tostring(reason):match("^group%-removed:") then
        removeGroup(test.group, "bike-jack-test-" .. tostring(outcome):lower())
    end
end

startBikeJackSeatConvergence = function(test, pending)
    if bikeJackTest ~= test or test.awaitingSeatConvergence then
        return
    end
    test.awaitingSeatConvergence = true
    test.seatConvergenceStartedAt = getTickCount()
    local function poll()
        if bikeJackTest ~= test then
            return
        end
        if not isElement(test.vehicle) or not isElement(test.driver) or not isElement(test.passenger) then
            stopBikeJackTest("FAIL", "seat-convergence-state-lost")
            return
        end
        local driverOut = getPedOccupiedVehicle(test.driver) ~= test.vehicle
        local passengerOut = getPedOccupiedVehicle(test.passenger) ~= test.vehicle
        local driverSeatEmpty = not getVehicleOccupant(test.vehicle, 0)
        local passengerSeatEmpty = not getVehicleOccupant(test.vehicle, 1)
        if driverOut and passengerOut and driverSeatEmpty and passengerSeatEmpty then
            writePopulationTrace("native_bike_jack_test_seat_converged", {
                scenario_id = test.id,
                group_id = test.group.id,
                epoch = pending.epoch,
                nonce = pending.nonce,
                expected_attacker_id = test.attacker.id,
                elapsed_ms = getTickCount() - test.seatConvergenceStartedAt,
                driver_out = true,
                passenger_out = true,
                driver_seat_empty = true,
                passenger_seat_empty = true,
            })
            stopBikeJackTest("PASS", "driver-and-passenger-seat-converged")
        elseif getTickCount() - test.seatConvergenceStartedAt >= 8000 then
            writePopulationTrace("native_bike_jack_test_seat_converged", {
                scenario_id = test.id,
                group_id = test.group.id,
                epoch = pending.epoch,
                nonce = pending.nonce,
                expected_attacker_id = test.attacker.id,
                elapsed_ms = getTickCount() - test.seatConvergenceStartedAt,
                driver_out = driverOut,
                passenger_out = passengerOut,
                driver_seat_empty = driverSeatEmpty,
                passenger_seat_empty = passengerSeatEmpty,
                success = false,
            })
            stopBikeJackTest("FAIL", "seat-convergence-timeout")
        end
    end
    poll()
    if bikeJackTest == test and test.awaitingSeatConvergence then
        test.seatTimer = setTimer(poll, 100, 0)
    end
end

local function removeTestVehicle(player)
    local vehicle = testVehicles[player]
    testVehicles[player] = nil
    if bikeJackTest and bikeJackTest.vehicle == vehicle then
        stopBikeJackTest("CANCEL", "test-vehicle-removed")
    end
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

local function createBikeJackTestGroup(owner, vehicle, scenarioId)
    if getTrafficPedCount() + 3 > config.globalCap or #getElementsByType("ped") + 3 > config.pedPoolSoftLimit then
        return false, "ped-capacity"
    end
    if countNativeGroupsForOwner(owner) >= config.maximumNativeGangGroups then
        return false, "native-group-cap"
    end

    local vehicleX, vehicleY, vehicleZ = getElementPosition(vehicle)
    local _, _, vehicleHeading = getElementRotation(vehicle)
    local radians = math.rad(vehicleHeading)
    local forwardX, forwardY = math.sin(radians), -math.cos(radians)
    local rightX, rightY = math.cos(radians), math.sin(radians)
    local centreX, centreY = vehicleX + forwardX * 5, vehicleY + forwardY * 5
    local facing = (vehicleHeading + 180) % 360
    local direction = math.floor((facing + 22.5) / 45) % 8
    local groveSlots = type(populationWorld.gangWeapons[0]) == "table" and populationWorld.gangWeapons[0] or {}
    local slot1 = math.floor(tonumber(groveSlots[1]) or 0)
    local slot2 = math.floor(tonumber(groveSlots[2]) or 0)
    local slot3 = math.floor(tonumber(groveSlots[3]) or 0)
    local weaponSlotCount = (slot1 ~= 0 and 1 or 0) + (slot2 ~= 0 and 1 or 0) + (slot3 ~= 0 and 1 or 0)
    if slot1 ~= 22 then
        return false, "grove-pistol-slot-unavailable"
    end
    local definitions = {
        {model = 102, weapon = 22, lateral = 0},
        {model = 103, weapon = 0, lateral = -1.25},
        {model = 104, weapon = 0, lateral = 1.25},
    }

    nextGroupId = nextGroupId + 1
    local group = {
        id = nextGroupId,
        gang = 0,
        members = {},
        owner = nil,
        epoch = 0,
        state = "created",
        createdAt = getTickCount(),
        bikeJackTestOwned = true,
    }
    trafficGroups[group.id] = group

    for index, definition in ipairs(definitions) do
        local x = centreX + rightX * definition.lateral
        local y = centreY + rightY * definition.lateral
        local ped = createPed(definition.model, x, y, vehicleZ + 0.15, facing)
        if not ped then
            removeGroup(group, "bike-jack-test-create-ped-refused")
            return false, "create-ped"
        end
        nextPedId = nextPedId + 1
        local record = {
            id = nextPedId,
            ped = ped,
            owner = nil,
            epoch = 0,
            direction = direction,
            populationClass = "gang",
            gang = 0,
            state = "created",
            createdAt = group.createdAt,
            group = group,
            groupIndex = index,
            weapon = definition.weapon,
            weaponAmmo = definition.weapon ~= 0 and config.nativeGangWeaponAmmo or 0,
            weaponClipAmmo = definition.weapon ~= 0 and gangWeaponClipAmmo[definition.weapon] or 0,
            -- These values describe a valid retail draw. The harness pins one
            -- armed member so the checkpoint tests bike-jack transport rather
            -- than spending attempts on the independent 33% weapon RNG.
            armedRoll = definition.weapon ~= 0 and 0 or 99,
            weaponSlotRoll = definition.weapon ~= 0 and 0 or false,
            weaponSlotCount = weaponSlotCount,
        }
        group.members[index] = record
        trafficPeds[ped] = record
        setElementDimension(ped, getElementDimension(vehicle))
        setElementInterior(ped, getElementInterior(vehicle))
        setElementData(ped, "neon:ambientPedTraffic", true)
        setElementData(ped, "neon:ambientPedTrafficId", record.id)
        setElementData(ped, "neon:ambientPedPopulationClass", "gang")
        setElementData(ped, "neon:ambientPedGang", 0)
        setElementData(ped, "neon:ambientPedGroupId", group.id)
        setElementData(ped, "neon:ambientPedGroupIndex", index)
        setElementData(ped, "neon:ambientPedGroupRole", index == 1 and "leader" or "member")
        setElementData(ped, "neon:ambientPedWeapon", record.weapon)
        setElementData(ped, "neon:ambientPedWeaponAmmo", record.weaponAmmo)
        setElementData(ped, "neon:ambientPedWeaponClipAmmo", record.weaponClipAmmo)
        if not applyGangPedWeapon(ped, record.weapon) or not setPedFightingStyle(ped, 4) or
            not setPedUseNativeWalkingStyle(ped, true) then
            removeGroup(group, "bike-jack-test-ped-setup-refused")
            return false, "ped-setup"
        end
    end
    group.leader = group.members[1]

    local fadingPeds = {}
    for _, record in ipairs(group.members) do fadingPeds[#fadingPeds + 1] = record.ped end
    beginSpawnFade(fadingPeds)
    if not assignGroupOwner(group, owner, "bike-jack-test-spawn") then
        removeGroup(group, "bike-jack-test-owner-refused")
        return false, "assign-owner"
    end

    group.counted = true
    stats.groupSpawns = stats.groupSpawns + 1
    stats.spawned = stats.spawned + #group.members
    for _, definition in ipairs(definitions) do
        stats.spawnedModels[definition.model] = (stats.spawnedModels[definition.model] or 0) + 1
    end
    writePopulationTrace("native_bike_jack_test_group_spawned", {
        scenario_id = scenarioId,
        group_id = group.id,
        epoch = group.epoch,
        owner_id = getPopulationClientId(owner),
        attacker_id = group.members[1].id,
        damage_target_id = group.members[2].id,
        position = {x = centreX, y = centreY, z = vehicleZ},
    })
    log(("bike-jack-test-group scenario=%d group=%d owner=%s attacker=%d target=%d"):format(
            scenarioId, group.id, getPlayerName(owner), group.members[1].id, group.members[2].id), true)
    return {group = group, attacker = group.members[1], damageTarget = group.members[2]}
end

local function announceBikeJackTestReady(test)
    if bikeJackTest ~= test or test.ready then
        return
    end
    if not test.group or test.group.removing then
        stopBikeJackTest("FAIL", "group-lost-before-ready")
        return
    end
    if test.group.state ~= "active" or test.group.owner ~= test.passenger or
        getElementSyncer(test.attacker.ped) ~= test.passenger then
        if getTickCount() - test.createdAt >= 8000 then
            stopBikeJackTest("FAIL", "group-assignment-timeout")
        end
        return
    end

    test.ready = true
    test.startedAt = getTickCount()
    if isTimer(test.readyTimer) then
        killTimer(test.readyTimer)
    end
    test.timer = setTimer(function()
        if bikeJackTest == test then
            stopBikeJackTest("FAIL", "attempt-timeout")
        end
    end, 90000, 1)
    writePopulationTrace("native_bike_jack_test_started", {
        scenario_id = test.id,
        group_id = test.group.id,
        epoch = test.group.epoch,
        expected_attacker_id = test.attacker.id,
        damage_target_id = test.damageTarget.id,
        owner_id = getPopulationClientId(test.group.owner),
        driver_id = getPopulationClientId(test.driver),
        passenger_id = getPopulationClientId(test.passenger),
        vehicle_model = getElementModel(test.vehicle),
        spawned_group = true,
    })
    outputChatBox(("Bike-jack scenario %d ready: driver %s, passenger/AI owner %s, group %d, expected armed jacker %d"):format(
                      test.id, getPlayerName(test.driver), getPlayerName(test.passenger), test.group.id, test.attacker.id), root, 120, 220, 255)
    outputChatBox(("Driver: exit, hit spawned unarmed traffic ped %d once, wait for task 1502, then remount without accelerating. Passenger stays seated."):format(
                      test.damageTarget.id),
                  root, 120, 220, 255)
    outputChatBox("If vanilla chooses task 1505 flee, rerun /pedtraffic bikejack; the resource does not override that 50/50 decision.",
                  root, 255, 200, 120)
end

local function startBikeJackTest(player)
    if not enabled or not isEligiblePlayer(player) then
        outputChatBox("Run /pedtraffic on before starting the bike-jack test", player, 255, 160, 80)
        return false
    end
    local vehicle = getPedOccupiedVehicle(player)
    if not isElement(vehicle) or getElementType(vehicle) ~= "vehicle" or getVehicleType(vehicle) ~= "Bike" then
        outputChatBox("Seat both clients on the same stationary motorcycle before running /pedtraffic bikejack", player, 255, 160, 80)
        return false
    end
    local driver = getVehicleOccupant(vehicle, 0)
    local passenger = getVehicleOccupant(vehicle, 1)
    if not isElement(driver) or getElementType(driver) ~= "player" or not isElement(passenger) or
        getElementType(passenger) ~= "player" or not isEligiblePlayer(driver) or not isEligiblePlayer(passenger) then
        outputChatBox("The motorcycle needs one eligible client in seat 0 and another in seat 1", player, 255, 160, 80)
        return false
    end
    if getPedOccupiedVehicle(driver) ~= vehicle or getPedOccupiedVehicleSeat(driver) ~= 0 or
        getPedOccupiedVehicle(passenger) ~= vehicle or getPedOccupiedVehicleSeat(passenger) ~= 1 or
        getElementDimension(vehicle) ~= 0 or getElementInterior(vehicle) ~= 0 then
        outputChatBox("The two clients must occupy seats 0/1 on a motorcycle in world 0", player, 255, 160, 80)
        return false
    end

    if bikeJackTest then
        stopBikeJackTest("CANCEL", "restarted")
    end
    -- Isolate this checkpoint from naturally spawned groups while preserving
    -- the caller's real network vehicle and its seat ownership.
    clearTraffic("bike-jack-harness-reset")
    setElementVelocity(vehicle, 0, 0, 0)
    giveWeapon(driver, 22, 200, true)

    nextBikeJackTestId = nextBikeJackTestId + 1
    local selection, spawnReason = createBikeJackTestGroup(passenger, vehicle, nextBikeJackTestId)
    if not selection then
        outputChatBox("Could not create the deterministic bike-jack group: " .. tostring(spawnReason), player, 255, 80, 80)
        return false
    end
    local group = selection.group
    local test = {
        id = nextBikeJackTestId,
        group = group,
        attacker = selection.attacker,
        damageTarget = selection.damageTarget,
        driver = driver,
        passenger = passenger,
        vehicle = vehicle,
        createdAt = getTickCount(),
        ownsGroup = true,
        ready = false,
    }
    bikeJackTest = test
    outputChatBox(("Bike-jack scenario %d: deterministic group spawned; waiting for passenger-owner native group ACK..."):format(test.id),
                  root, 120, 220, 255)
    test.readyTimer = setTimer(function() announceBikeJackTestReady(test) end, 100, 0)
    announceBikeJackTestReady(test)
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
            pendingNativeBikeJacks = {}
            populationProfiles = {}
            populationWorldRevisions = {}
            if coupleTest then finishCoupleTest("CANCEL", "traffic-disabled") end
        end
        return
    end
    enabled = value
    if enabled then
        sendPopulationWorldState(root)
    end
    if not enabled then
        if coupleTest then
            finishCoupleTest("CANCEL", "traffic-disabled")
        end
        if stopDealerTest then
            stopDealerTest("CANCEL", "traffic-disabled")
        end
        if stopResidencyTest then
            stopResidencyTest("CANCEL", "traffic-disabled")
        end
        if stopBikeJackTest then
            stopBikeJackTest("CANCEL", "traffic-disabled")
        end
        pendingNativeBikeJacks = {}
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
    for index, record in ipairs(test.group.members) do
        if isElement(record.ped) then
            setElementData(record.ped, "neon:nativeAIActorId", "ped-" .. tostring(index))
            setElementData(record.ped, "neon:nativeAIActionId", action)
            setElementData(record.ped, "neon:nativeAIStep", action)
        end
    end
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
    if test.group then
        for _, record in ipairs(test.group.members) do
            if isElement(record.ped) then
                removeElementData(record.ped, "neon:nativeAIRunId")
                removeElementData(record.ped, "neon:nativeAIScenarioId")
                removeElementData(record.ped, "neon:nativeAIActorId")
                removeElementData(record.ped, "neon:nativeAIActionId")
                removeElementData(record.ped, "neon:nativeAIStep")
            end
        end
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
        damage_id = test.damageId or false,
        damage_dispatches = test.damageDispatches or 0,
        weapon_state_commits = test.weaponStateCommits or 0,
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
    local expectedA = test.action == "prepare" or test.action == "damage-settle" or test.action == "handoff-b-to-a" or
        test.action == "boundary-hold-return" or test.action == "resume"
    local expectedB = test.action == "prepare" or test.action == "damage-settle" or test.action == "handoff-a-to-b"
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
        local target = test.group.members[1]
        local context = rememberGroupCombatContext(target, test.ownerB, 0, 3, "residency-harness")
        local dispatched = context and bridgeDamageResponse(target, test.ownerB, 0, 3, false, context.damageId)
        if not dispatched then
            return failResidencyTest("damage-context-dispatch-refused")
        end
        context.decisionState = "dispatched"
        context.dispatchedAt = getTickCount()
        test.damageId = context.damageId
        test.damageDispatches = 1
        writePopulationTrace("group_combat_context_dispatched", {
            scenario_id = test.id,
            group_id = test.group.id,
            traffic_id = target.id,
            damage_id = context.damageId,
            epoch = test.group.epoch,
            owner_id = getPopulationClientId(test.group.owner),
            source = "residency-harness",
        })
        traceResidencyTestAction(test, "damage-settle")
    elseif test.action == "damage-settle" then
        if test.group.state ~= "active" or test.group.owner ~= test.ownerA or test.group.epoch ~= test.firstEpoch then
            return failResidencyTest("damage-changed-owner-or-epoch")
        end
        if now - test.actionStartedAt < 1200 then
            return
        end
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
            test.boundaryEpoch = test.group.epoch
            test.boundaryOwner = test.group.owner
            moveResidencyTestPlayer(test, test.ownerA, test.farDistance)
            moveResidencyTestPlayer(test, test.ownerB, -test.farDistance)
            traceResidencyTestAction(test, "boundary-hold-out")
        end
    elseif test.action == "boundary-hold-out" then
        if test.group.state ~= "active" or test.group.owner ~= test.boundaryOwner or test.group.epoch ~= test.boundaryEpoch then
            return failResidencyTest("short-hold-revoked-owner-or-epoch")
        end
        if now - test.actionStartedAt >= math.floor(config.handoffHold / 2) then
            moveResidencyTestPlayer(test, test.ownerA, test.nearA)
            traceResidencyTestAction(test, "boundary-hold-return")
        end
    elseif test.action == "boundary-hold-return" then
        if test.group.state ~= "active" or test.group.owner ~= test.boundaryOwner or test.group.epoch ~= test.boundaryEpoch then
            return failResidencyTest("short-hold-return-changed-owner-or-epoch")
        end
        if test.group.outsideResidencySince then
            return
        end
        if now - test.actionStartedAt >= 500 then
            moveResidencyTestPlayer(test, test.ownerA, test.farDistance)
            moveResidencyTestPlayer(test, test.ownerB, -test.farDistance)
            test.longHoldStartedAt = getTickCount()
            traceResidencyTestAction(test, "no-resident")
        end
    elseif test.action == "no-resident" then
        if test.group.state == "suspended" then
            if test.group.epoch ~= test.boundaryEpoch or now - test.longHoldStartedAt < config.handoffHold then
                return failResidencyTest("long-hold-suspended-too-early-or-changed-epoch")
            end
            for _, record in ipairs(test.group.members) do
                if not isElementFrozen(record.ped) then
                    return failResidencyTest("suspended-ped-not-frozen:id=" .. tostring(record.id))
                end
            end
            moveResidencyTestPlayer(test, test.ownerA, test.nearA)
            traceResidencyTestAction(test, "resume")
        end
    elseif test.action == "resume" and test.group.state == "active" and test.group.owner == test.ownerA and
        test.group.epoch > test.boundaryEpoch and isResidencyOwnerReady(test, test.ownerA) then
        local context = test.group.combatContext
        if not context or context.damageId ~= test.damageId or context.decisionState ~= "dispatched" or test.damageDispatches ~= 1 then
            return failResidencyTest("damage-identity-or-dispatch-count-changed")
        end
        if test.weaponStateCommits ~= 3 then
            return failResidencyTest("weapon-state-commit-count:" .. tostring(test.weaponStateCommits or 0))
        end
        stopResidencyTest("PASS", "single-damage-short-hold-hard-suspend-resume; analyzer-must-confirm-at-most-one-decision-query")
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
    for index, record in ipairs(group.members) do
        if isElement(record.ped) then
            setElementData(record.ped, "neon:nativeAIRunId", "pedtraffic-residency-" .. tostring(test.id))
            setElementData(record.ped, "neon:nativeAIScenarioId", "gang-collision-boundary-hold-v1")
            setElementData(record.ped, "neon:nativeAIActorId", "ped-" .. tostring(index))
            setElementData(record.ped, "neon:nativeAIActionId", "prepare")
            setElementData(record.ped, "neon:nativeAIStep", "prepare")
        end
    end
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
    -- MTA clones table arguments passed through timers, which would break the
    -- identity guard in pulseResidencyTest and silently stop this state machine.
    test.timer = setTimer(function()
        pulseResidencyTest(test)
    end, 100, 0)
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
    if not enabled or not isEligiblePlayer(client) then
        populationProfiles[client] = nil
        return
    end
    if not isPopulationWorldReady(client) then
        return
    end

    local validated = validatePopulationProfile(profile)
    if not validated then
        populationProfiles[client] = nil
        return
    end

    local totalTarget, civilianTarget, dealerTarget, copTarget, gangTarget, gangTargets =
        calculateNativeTargets(validated, pedTrafficDemoDensity.enabled and client == pedTrafficDemoDensity.anchor)
    local targetSignature = {}
    for index = 1, 10 do targetSignature[index] = ("%.3f"):format(gangTargets[index]) end
    validated.signature = ("%d:%.3f:%.3f:%.3f:%.3f:%s:%s:%d:%d:%s:%d:%.3f:%.3f:%s:%d:%d:%.3f:%.3f:%d:%.3f:%.3f"):format(
        totalTarget, civilianTarget, dealerTarget, copTarget, gangTarget, table.concat(targetSignature, ","), validated.zoneLabel, validated.zoneType,
        validated.timeIndex, tostring(validated.weekend), validated.worldRevision, validated.effectiveCopTarget, validated.dealerTarget,
        tostring(validated.noCops), validated.worldLevel, validated.copSuppressionFlags,
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
        log(("population-profile player=%s revision=%d target=%.1f effective=%.1f supported=%.1f civilian=%.1f gang=%.1f cops=%.1f rawCops=%.1f dealers=%.1f weights=%s zone=%s/%d time=%d weekend=%s noCops=%s"):format(
                getPlayerName(client), validated.worldRevision, validated.target, validated.effectiveTarget, validated.supportedTarget, validated.civilianTarget,
                validated.gangTarget, validated.effectiveCopTarget, validated.rawCopTarget, validated.dealerTarget, table.concat(validated.gangWeights, "/"),
                validated.zoneLabel, validated.zoneType, validated.timeIndex, tostring(validated.weekend), tostring(validated.noCops)))
        writePopulationTrace("population_profile", {
            player_id = getPopulationClientId(client),
            target = validated.target,
            supported_target = validated.supportedTarget,
            civilian_target = validated.civilianTarget,
            gang_target = validated.gangTarget,
            cop_target = validated.effectiveCopTarget,
            raw_cop_target = validated.rawCopTarget,
            cop_suppression_flags = validated.copSuppressionFlags,
            world_level = validated.worldLevel,
            dealer_target = validated.dealerTarget,
            gang_weights = validated.gangWeights,
            zone_label = validated.zoneLabel,
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

    if success == true and type(capabilities) == "table" and capabilities.zones == true and
        capabilities.catalogRevision == populationCatalog.revision then
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
        restoreDealerCombatContext(record)
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
local function commitReleasedGroupWeaponState(group, data, releaseReason)
    local reportedWeapons = type(data) == "table" and type(data.weapons) == "table" and data.weapons or false
    if not reportedWeapons or #reportedWeapons ~= #group.members then
        return false, "invalid-row-count"
    end

    local recordsByTrafficId = {}
    for _, record in ipairs(group.members) do recordsByTrafficId[record.id] = record end

    local seen = {}
    local staged = {}
    for _, row in ipairs(reportedWeapons) do
        local trafficId = type(row) == "table" and tonumber(row.trafficId) or false
        local record = trafficId and recordsByTrafficId[trafficId] or false
        if not record or seen[trafficId] or not isElement(record.ped) then
            return false, "invalid-member"
        end
        seen[trafficId] = true

        local expectedWeapon = tonumber(row.expected)
        local weapon = tonumber(row.weapon)
        local serverWeapon = getPedWeapon(record.ped)
        local serverStatsConverged = true
        for _, stat in ipairs(gangStandardWeaponStats) do
            serverStatsConverged = serverStatsConverged and getPedStat(record.ped, stat[1]) == stat[2]
        end
        if expectedWeapon ~= record.weapon or weapon ~= record.weapon or serverWeapon ~= record.weapon or
            row.statsConverged ~= true or not serverStatsConverged then
            return false, "weapon-or-stats-mismatch"
        end

        if record.weapon == 0 then
            staged[#staged + 1] = {record = record, ammo = 0, clipAmmo = 0}
        else
            local expectedAmmo = tonumber(row.expectedAmmo)
            local expectedClipAmmo = tonumber(row.expectedClipAmmo)
            local ammo = tonumber(row.totalAmmo)
            local clipAmmo = tonumber(row.clipAmmo)
            local clipCapacity = gangWeaponClipAmmo[record.weapon]
            if expectedAmmo ~= record.weaponAmmo or expectedClipAmmo ~= record.weaponClipAmmo or not clipCapacity or
                not isIntegerInRange(ammo, 0, record.weaponAmmo) or not isIntegerInRange(clipAmmo, 0, clipCapacity) or
                clipAmmo > ammo then
                return false, "ammo-mismatch"
            end
            staged[#staged + 1] = {record = record, ammo = ammo, clipAmmo = clipAmmo}
        end
    end

    local changes = {}
    for _, state in ipairs(staged) do
        local record = state.record
        local previousAmmo = record.weaponAmmo
        local previousClipAmmo = record.weaponClipAmmo
        if record.weapon ~= 0 and setWeaponAmmo(record.ped, record.weapon, state.ammo, state.clipAmmo) ~= true then
            return false, "server-ammo-commit-refused"
        end
        record.weaponAmmo = state.ammo
        record.weaponClipAmmo = state.clipAmmo
        setElementData(record.ped, "neon:ambientPedWeaponAmmo", state.ammo)
        setElementData(record.ped, "neon:ambientPedWeaponClipAmmo", state.clipAmmo)
        changes[#changes + 1] = {
            traffic_id = record.id,
            weapon = record.weapon,
            previous_ammo = previousAmmo,
            ammo = state.ammo,
            previous_clip_ammo = previousClipAmmo,
            clip_ammo = state.clipAmmo,
        }
    end

    writePopulationTrace("group_weapon_state_committed", {
        scenario_id = residencyTest and residencyTest.group == group and residencyTest.id or false,
        group_id = group.id,
        epoch = group.epoch,
        owner_id = getPopulationClientId(client),
        reason = tostring(releaseReason),
        weapons = changes,
    })
    if residencyTest and residencyTest.group == group then
        residencyTest.weaponStateCommits = (residencyTest.weaponStateCommits or 0) + 1
    end
    return true
end

addEventHandler("pedTraffic:groupEvidence", resourceRoot, function(groupId, epoch, evidence, data)
    local group = trafficGroups[tonumber(groupId)]
    if not group or group.removing or group.epoch ~= epoch or client ~= group.owner then
        return
    end
    if evidence == "accepted" and group.state == "assigning" then
        local reportedWeapons = type(data) == "table" and type(data.weapons) == "table" and data.weapons or false
        local weaponsByTrafficId = {}
        if reportedWeapons then
            for _, row in ipairs(reportedWeapons) do
                if type(row) == "table" and isIntegerInRange(tonumber(row.trafficId), 1, 2147483647) then
                    weaponsByTrafficId[tonumber(row.trafficId)] = row
                end
            end
        end
        for _, record in ipairs(group.members) do
            local row = weaponsByTrafficId[record.id]
            local serverWeapon = isElement(record.ped) and getPedWeapon(record.ped) or -1
            local serverAmmo = isElement(record.ped) and getPedTotalAmmo(record.ped) or 0
            local serverClipAmmo = isElement(record.ped) and getPedAmmoInClip(record.ped) or 0
            local serverStatsConverged = true
            for _, stat in ipairs(gangStandardWeaponStats) do
                serverStatsConverged = serverStatsConverged and getPedStat(record.ped, stat[1]) == stat[2]
            end
            -- Unarmed occupies a no-ammo slot for which MTA may expose a
            -- synthetic total of 1. Authenticate the selected weapon and STD
            -- stats, but require exact ammo and clip values only when vanilla
            -- actually granted a firearm.
            local clientAmmoConverged = record.weapon == 0 or
                row and row.ammoConverged == true and tonumber(row.totalAmmo) == record.weaponAmmo and
                    tonumber(row.clipAmmo) == record.weaponClipAmmo
            local serverAmmoConverged = record.weapon == 0 or
                serverAmmo == record.weaponAmmo and serverClipAmmo == record.weaponClipAmmo
            local validWeapon = row and tonumber(row.expected) == record.weapon and tonumber(row.weapon) == record.weapon and
                row.converged == true and row.statsConverged == true and serverStatsConverged and serverWeapon == record.weapon and
                clientAmmoConverged and serverAmmoConverged
            if not validWeapon then
                writePopulationTrace("group_weapon_authority_rejected", {
                    group_id = group.id,
                    traffic_id = record.id,
                    epoch = group.epoch,
                    expected_weapon = record.weapon,
                    server_weapon = serverWeapon,
                    server_ammo = serverAmmo,
                    server_clip_ammo = serverClipAmmo,
                    server_stats_converged = serverStatsConverged,
                    reported = row or false,
                })
                removeGroup(group, "group-weapon-authority-mismatch")
                return
            end
        end
        writePopulationTrace("group_weapon_authority_granted", {
            group_id = group.id,
            epoch = group.epoch,
            owner_id = getPopulationClientId(client),
            weapons = reportedWeapons,
        })
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
        local context = group.combatContext
        local contextTargetX, contextTargetY, contextTargetZ
        local contextAttackerX, contextAttackerY, contextAttackerZ
        if context and context.target and isElement(context.target.ped) then
            contextTargetX, contextTargetY, contextTargetZ = getElementPosition(context.target.ped)
        end
        if context and isElement(context.attacker) then
            contextAttackerX, contextAttackerY, contextAttackerZ = getElementPosition(context.attacker)
        end
        if context and context.decisionState == "pending" and getTickCount() - context.observedAt <= 8000 and context.target and
            not context.target.removing and
            isElement(context.target.ped) and isElement(context.attacker) and getElementHealth(context.attacker) > 0 and
            getElementDimension(context.attacker) == getElementDimension(context.target.ped) and
            getElementInterior(context.attacker) == getElementInterior(context.target.ped) and contextTargetX and contextAttackerX and
            squaredDistance(contextTargetX, contextTargetY, contextTargetZ, contextAttackerX, contextAttackerY, contextAttackerZ) <= 250 * 250 then
            local restored = bridgeDamageResponse(context.target, context.attacker, context.weapon, context.bodypart, true,
                                                   context.damageId)
            if restored then
                context.decisionState = "dispatched"
                context.dispatchedAt = getTickCount()
            end
            writePopulationTrace("group_combat_context_restored", {
                group_id = group.id,
                traffic_id = context.target.id,
                damage_id = context.damageId,
                epoch = group.epoch,
                owner_id = getPopulationClientId(client),
                attacker_id = getPopulationClientId(context.attacker),
                weapon = context.weapon,
                bodypart = context.bodypart,
                accepted = restored == true,
                decision_state = context.decisionState,
                context_age_ms = getTickCount() - context.observedAt,
            })
        elseif context and getTickCount() - context.observedAt > 8000 then
            writePopulationTrace("group_combat_context_expired", {
                group_id = group.id,
                damage_id = context.damageId,
                epoch = group.epoch,
                context_age_ms = getTickCount() - context.observedAt,
            })
            group.combatContext = nil
        end
        log(("group-accepted group=%d epoch=%d gang=%d members=%d owner=%s"):format(
                group.id, group.epoch, group.gang, #group.members, getPlayerName(client)), true)
        for _, record in ipairs(group.members) do
            for _, player in ipairs(getEligiblePlayers()) do
                if hasValidGunAimContext(player, record.ped, true) then
                    bridgeGunAim(record, player)
                end
            end
        end
    elseif evidence == "released" and (group.state == "revoking" or group.state == "suspending") then
        local committed, rejectionReason =
            commitReleasedGroupWeaponState(group, data, type(data) == "table" and data.reason or evidence)
        if not committed then
            writePopulationTrace("group_weapon_state_rejected", {
                scenario_id = residencyTest and residencyTest.group == group and residencyTest.id or false,
                group_id = group.id,
                epoch = group.epoch,
                owner_id = getPopulationClientId(client),
                reason = tostring(rejectionReason),
            })
            removeGroup(group, "group-release-weapon-state-invalid")
            return
        end
        if group.state == "revoking" then
            finishGroupHandoff(group, "group-release-ack")
        else
            finishGroupSuspension(group, "group-suspension-release-ack")
        end
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
addEventHandler("pedTraffic:damageObserved", resourceRoot, function(ped, weapon, bodypart, observerNonce)
    local record = trafficPeds[ped]
    weapon = tonumber(weapon)
    bodypart = tonumber(bodypart)
    observerNonce = tonumber(observerNonce)
    if not record or not isElement(client) or not weapon or not bodypart or weapon < 0 or weapon > 54 or
        (bodypart ~= 0 and (bodypart < 3 or bodypart > 9)) or not isIntegerInRange(observerNonce, 1, 2147483647) or
        observerNonce <= (record.damageObserverNonces and record.damageObserverNonces[client] or 0) then
        return
    end

    local px, py, pz = getElementPosition(ped)
    local ax, ay, az = getElementPosition(client)
    if getElementDimension(client) ~= getElementDimension(ped) or getElementInterior(client) ~= getElementInterior(ped) or
        squaredDistance(px, py, pz, ax, ay, az) > 250 * 250 then
        return
    end

    record.lastInteractionAt = getTickCount()
    record.damageObserverNonces = record.damageObserverNonces or {}
    record.damageObserverNonces[client] = observerNonce
    local context = rememberGroupCombatContext(record, client, weapon, bodypart, "observer-bridge", observerNonce)
    local dispatched = bridgeDamageResponse(record, client, math.floor(weapon), math.floor(bodypart), false,
                                            context and context.damageId or false)
    if context and dispatched then
        context.decisionState = "dispatched"
        context.dispatchedAt = getTickCount()
        writePopulationTrace("group_combat_context_dispatched", {
            group_id = record.group and record.group.id or false,
            traffic_id = record.id,
            damage_id = context.damageId,
            epoch = record.group and record.group.epoch or record.epoch,
            owner_id = isElement(record.owner) and getPopulationClientId(record.owner) or false,
            observer_nonce = observerNonce,
        })
    end
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
        not isIntegerInRange(nonce, 1, 2147483647) or not isIntegerInRange(weapon, 0, 46) or
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
    -- epoch, canonical active weapon, collision envelope and cadence, then
    -- require the exact melee set or STD weapon.dat firearm factor reachable
    -- by this traffic ped's stock class.
    local canonicalWeapon = getPedWeapon(attackingPed)
    local damageRadius, damageInterval = getCanonicalTrafficNativeDamage(record, canonicalWeapon, math.floor(damageFactor))
    if canonicalWeapon ~= math.floor(weapon) or not damageRadius then
        return
    end

    local px, py, pz = getElementPosition(attackingPed)
    local vx, vy, vz = getElementPosition(victim)
    if getElementDimension(attackingPed) ~= getElementDimension(victim) or
        getElementInterior(attackingPed) ~= getElementInterior(victim) or
        squaredDistance(px, py, pz, vx, vy, vz) > damageRadius * damageRadius then
        return
    end

    local now = getTickCount()
    if record.nativePlayerDamageOwner == client and record.nativePlayerDamageEpoch == epoch then
        if nonce <= (record.nativePlayerDamageNonce or 0) or now - (record.nativePlayerDamageAt or 0) < damageInterval then
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

local nativeBikeJackTargetDoors = {[8] = true, [9] = true, [10] = true, [11] = true, [18] = true}
local nativeBikeJackResultReasons = {
    accepted = true,
    duplicate = true,
    ["lease-missing"] = true,
    ["occupant-state-changed"] = true,
    ["native-api-unavailable"] = true,
    ["native-task-refused"] = true,
}

local function nativeBikeJackKey(record, epoch, nonce)
    return ("%d:%d:%d"):format(record.id, epoch, nonce)
end

local function traceNativeBikeJackRejected(record, reason, epoch, nonce, targetPlayer, vehicle)
    writePopulationTrace("native_bike_jack_rejected", {
        traffic_id = record and record.id or false,
        group_id = record and record.group and record.group.id or false,
        owner_id = isElement(client) and getPopulationClientId(client) or false,
        epoch = epoch or false,
        nonce = nonce or false,
        victim_id = isElement(targetPlayer) and getElementType(targetPlayer) == "player" and
                        getPopulationClientId(targetPlayer) or false,
        vehicle_model = isElement(vehicle) and getElementType(vehicle) == "vehicle" and getElementModel(vehicle) or false,
        reason = reason,
    })
end

local function finishNativeBikeJackForward(pending, reason)
    if pendingNativeBikeJacks[pending.key] ~= pending then
        return
    end
    pendingNativeBikeJacks[pending.key] = nil
    local accepted = 0
    local received = 0
    for _, expected in pairs(pending.recipients) do
        if expected.received then
            received = received + 1
            if expected.accepted then
                accepted = accepted + 1
            end
        end
    end
    local passed = received == pending.recipientCount and accepted == pending.recipientCount
    writePopulationTrace("native_bike_jack_bridge_complete", {
        traffic_id = pending.record.id,
        group_id = pending.record.group and pending.record.group.id or false,
        epoch = pending.epoch,
        nonce = pending.nonce,
        recipients = pending.recipientCount,
        receipts = received,
        accepted = accepted,
        success = passed,
        reason = reason,
        scenario_id = pending.scenarioId or false,
    })
    if pending.scenarioId and bikeJackTest and bikeJackTest.id == pending.scenarioId then
        if passed then
            startBikeJackSeatConvergence(bikeJackTest, pending)
        else
            stopBikeJackTest("FAIL", reason)
        end
    end
end

addEvent("pedTraffic:nativeBikeJackObserved", true)
addEventHandler("pedTraffic:nativeBikeJackObserved", resourceRoot,
                function(attackingPed, targetPlayer, vehicle, epoch, nonce, targetDoor, secondaryHint)
    local record = trafficPeds[attackingPed]
    epoch = tonumber(epoch)
    nonce = tonumber(nonce)
    targetDoor = tonumber(targetDoor)
    if not record or record.removing or record.state ~= "active" or not record.group or record.group.removing or
        record.group.state ~= "active" or client ~= record.owner or client ~= record.group.owner or
        getElementSyncer(attackingPed) ~= client or not isIntegerInRange(epoch, 1, 2147483647) or
        epoch ~= record.epoch or epoch ~= record.group.epoch then
        traceNativeBikeJackRejected(record, "owner-or-epoch", epoch, nonce, targetPlayer, vehicle)
        return
    end
    if not isIntegerInRange(nonce, 1, 2147483647) or not isIntegerInRange(targetDoor, 8, 18) or
        nativeBikeJackTargetDoors[targetDoor] ~= true or not isElement(targetPlayer) or
        getElementType(targetPlayer) ~= "player" or getElementHealth(targetPlayer) <= 0 or not isElement(vehicle) or
        getElementType(vehicle) ~= "vehicle" or getVehicleType(vehicle) ~= "Bike" then
        traceNativeBikeJackRejected(record, "invalid-payload", epoch, nonce, targetPlayer, vehicle)
        return
    end

    local targetSeat = getPedOccupiedVehicle(targetPlayer) == vehicle and getPedOccupiedVehicleSeat(targetPlayer) or false
    local driverDoor = targetDoor == 8 or targetDoor == 10 or targetDoor == 18
    local passengerDoor = targetDoor == 9 or targetDoor == 11
    if (targetSeat ~= 0 and targetSeat ~= 1) or (targetSeat == 0 and not driverDoor) or
        (targetSeat == 1 and not passengerDoor) then
        traceNativeBikeJackRejected(record, "target-seat-door", epoch, nonce, targetPlayer, vehicle)
        return
    end

    local passenger = targetSeat == 0 and getVehicleOccupant(vehicle, 1) or false
    if passenger and (getElementType(passenger) ~= "player" or getElementHealth(passenger) <= 0) then
        traceNativeBikeJackRejected(record, "invalid-passenger", epoch, nonce, targetPlayer, vehicle)
        return
    end
    if secondaryHint ~= false and (targetSeat ~= 0 or not isElement(secondaryHint) or
        getElementType(secondaryHint) ~= "player" or secondaryHint ~= passenger) then
        traceNativeBikeJackRejected(record, "secondary-mismatch", epoch, nonce, targetPlayer, vehicle)
        return
    end

    local dimension = getElementDimension(vehicle)
    local interior = getElementInterior(vehicle)
    if getElementDimension(attackingPed) ~= dimension or getElementInterior(attackingPed) ~= interior or
        getElementDimension(targetPlayer) ~= dimension or getElementInterior(targetPlayer) ~= interior or
        (passenger and (getElementDimension(passenger) ~= dimension or getElementInterior(passenger) ~= interior)) then
        traceNativeBikeJackRejected(record, "world-mismatch", epoch, nonce, targetPlayer, vehicle)
        return
    end

    local ax, ay, az = getElementPosition(attackingPed)
    local vx, vy, vz = getElementPosition(vehicle)
    local moveX, moveY = getElementVelocity(vehicle)
    local maximumDistance = config.nativeBikeJackDistance
    local maximumSpeed = config.nativeBikeJackMaximumSpeed
    if not isFiniteNumber(moveX) or not isFiniteNumber(moveY) or
        squaredDistance(ax, ay, az, vx, vy, vz) > maximumDistance * maximumDistance or
        moveX * moveX + moveY * moveY > maximumSpeed * maximumSpeed then
        traceNativeBikeJackRejected(record, "kinematic-envelope", epoch, nonce, targetPlayer, vehicle)
        return
    end

    local now = getTickCount()
    if record.nativeBikeJackOwner == client and record.nativeBikeJackEpoch == epoch and
        (nonce <= (record.nativeBikeJackNonce or 0) or now - (record.nativeBikeJackAt or 0) < config.nativeBikeJackInterval) then
        traceNativeBikeJackRejected(record, "duplicate-or-cadence", epoch, nonce, targetPlayer, vehicle)
        return
    end

    record.nativeBikeJackOwner = client
    record.nativeBikeJackEpoch = epoch
    record.nativeBikeJackNonce = nonce
    record.nativeBikeJackAt = now
    record.lastInteractionAt = now
    local scenarioId = bikeJackTest and bikeJackTest.group == record.group and bikeJackTest.attacker == record and
                           bikeJackTest.vehicle == vehicle and bikeJackTest.driver == targetPlayer and bikeJackTest.id or false
    writePopulationTrace("native_bike_jack_owner_attempt", {
        traffic_id = record.id,
        group_id = record.group.id,
        owner_id = getPopulationClientId(client),
        epoch = epoch,
        nonce = nonce,
        victim_id = getPopulationClientId(targetPlayer),
        victim_seat = targetSeat,
        target_door = targetDoor,
        passenger_id = isElement(passenger) and getPopulationClientId(passenger) or false,
        vehicle_model = getElementModel(vehicle),
        scenario_id = scenarioId,
    })

    -- Retail computes the first victim's BikeJacked parameters from the door
    -- chosen by EnterCar, then adds passenger[0] only when that first victim is
    -- the motorcycle driver. The passenger receives the same native task 200
    -- ms later; never trust the owner to nominate or parameterize that second
    -- victim.
    local primaryDoor = (targetDoor ~= 18 or passenger) and 10 or 11
    if primaryDoor ~= 10 and primaryDoor ~= 11 then
        primaryDoor = 10
    end
    local recipients = {{player = targetPlayer, seat = targetSeat, door = primaryDoor, down = 0, primary = true}}
    if targetSeat == 0 and isElement(passenger) then
        recipients[#recipients + 1] = {player = passenger, seat = 1, door = 11, down = 200, primary = false}
    end

    local key = nativeBikeJackKey(record, epoch, nonce)
    local pending = {
        key = key,
        record = record,
        epoch = epoch,
        nonce = nonce,
        recipients = {},
        recipientCount = #recipients,
        scenarioId = scenarioId,
    }
    pendingNativeBikeJacks[key] = pending
    for _, recipient in ipairs(recipients) do
        pending.recipients[recipient.player] = recipient
        writePopulationTrace("native_bike_jack_server_forward", {
            traffic_id = record.id,
            group_id = record.group.id,
            epoch = epoch,
            nonce = nonce,
            victim_id = getPopulationClientId(recipient.player),
            expected_seat = recipient.seat,
            door = recipient.door,
            dragged_down_time = recipient.down,
            primary_victim = recipient.primary,
            scenario_id = scenarioId,
        })
        triggerClientEvent(recipient.player, "pedTraffic:nativeBikeJack", resourceRoot, attackingPed, vehicle, epoch, nonce,
                           recipient.door, recipient.down, recipient.primary, recipient.seat)
    end
    setTimer(function()
        if pendingNativeBikeJacks[key] == pending then
            finishNativeBikeJackForward(pending, "receipt-timeout")
        end
    end, 5000, 1)
end)

addEvent("pedTraffic:nativeBikeJackResult", true)
addEventHandler("pedTraffic:nativeBikeJackResult", resourceRoot, function(attackingPed, epoch, nonce, accepted, reason, currentSeat)
    local record = trafficPeds[attackingPed]
    epoch = tonumber(epoch)
    nonce = tonumber(nonce)
    currentSeat = tonumber(currentSeat)
    if not record or not isIntegerInRange(epoch, 1, 2147483647) or not isIntegerInRange(nonce, 1, 2147483647) or
        type(accepted) ~= "boolean" or nativeBikeJackResultReasons[reason] ~= true or
        not isIntegerInRange(currentSeat, -1, 1) then
        return
    end
    local pending = pendingNativeBikeJacks[nativeBikeJackKey(record, epoch, nonce)]
    local expected = pending and pending.recipients[client]
    if not expected or expected.received then
        return
    end
    expected.received = true
    expected.accepted = accepted
    writePopulationTrace("native_bike_jack_victim_result", {
        traffic_id = record.id,
        group_id = record.group and record.group.id or false,
        epoch = epoch,
        nonce = nonce,
        victim_id = getPopulationClientId(client),
        expected_seat = expected.seat,
        current_seat = currentSeat,
        accepted = accepted,
        reason = reason,
        scenario_id = pending.scenarioId or false,
    })
    local complete = true
    for _, recipient in pairs(pending.recipients) do
        complete = complete and recipient.received == true
    end
    if complete then
        finishNativeBikeJackForward(pending, "all-receipts")
    end
end)

addEventHandler("onPedDamage", root, function(attacker, weapon, bodypart)
    local record = trafficPeds[source]
    if not record or record.removing or not isElement(attacker) or getElementType(attacker) ~= "player" then
        return
    end
    record.lastInteractionAt = getTickCount()
    rememberGroupCombatContext(record, attacker, weapon, bodypart, "server-ped-damage")
    rememberDealerCombatContext(record, attacker, weapon, bodypart, "server-ped-damage")
end)

addEvent("pedTraffic:dealerFightStarted", true)
addEventHandler("pedTraffic:dealerFightStarted", resourceRoot, function(ped, epoch, knifeModelLoaded, hierarchy)
    local record = trafficPeds[ped]
    if not record or record.removing or record.populationClass ~= "dealer" or client ~= record.owner or
        record.epoch ~= epoch or record.state ~= "active" or type(knifeModelLoaded) ~= "boolean" or record.dealerFightArmed then
        return
    end
    local traceHierarchy = {}
    if type(hierarchy) == "table" then
        for index = 1, math.min(#hierarchy, 12) do
            local taskName = tostring(hierarchy[index])
            if #taskName <= 80 then traceHierarchy[index] = taskName end
        end
    end
    local accepted, reason = applyDealerFightWeapons(record, knifeModelLoaded)
    if not accepted then
        writePopulationTrace("dealer_fight_weapon_rejected", {
            traffic_id = record.id,
            epoch = epoch,
            owner_id = getPopulationClientId(client),
            reason = reason,
        })
        removeRecord(record, reason)
        return
    end
    writePopulationTrace("dealer_fight_weapon_committed", {
        scenario_id = dealerTest and dealerTest.record == record and dealerTest.id or false,
        traffic_id = record.id,
        epoch = epoch,
        owner_id = getPopulationClientId(client),
        seed = record.dealerFightSeed,
        knife_model_loaded = knifeModelLoaded,
        has_knife = record.dealerHasKnife,
        has_pistol = record.dealerHasPistol,
        active_weapon = record.weapon,
        ammo = record.weaponAmmo,
        task_hierarchy = traceHierarchy,
    })
end)

addEventHandler("onPedWasted", root, function(_, killer, weapon, bodypart)
    local record = trafficPeds[source]
    if not record then
        return
    end
    if record.couple then
        coupleRuntime.dissolve(record.couple, "partner-wasted")
    end
    if record.populationClass == "dealer" and not record.dealerDeathApplied and isElement(killer) and
        getElementType(killer) == "player" then
        record.dealerDeathApplied = true
        local profile = populationProfiles[killer]
        local freshProfile = profile and profile.worldRevision == populationWorld.revision and
            getTickCount() - profile.receivedAt <= 2500
        local change = freshProfile and populationWorld:decrementDealerStrength(profile.zoneLabel) or false
        if dealerTest and dealerTest.record == record then
            dealerTest.deathEventObserved = true
            dealerTest.deathChange = change or false
        end
        writePopulationTrace("dealer_strength_death", {
            scenario_id = dealerTest and dealerTest.record == record and dealerTest.id or false,
            traffic_id = record.id,
            killer_id = getPopulationClientId(killer),
            weapon = tonumber(weapon) or false,
            bodypart = tonumber(bodypart) or false,
            zone_label = profile and profile.zoneLabel or false,
            profile_fresh = freshProfile == true,
            applied = change ~= false,
            change = change or false,
            revision = populationWorld.revision,
        })
        if change then
            publishPopulationWorldMutation("player-killed-dealer", change)
        end
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
    if bikeJackTest and source == bikeJackTest.vehicle then
        stopBikeJackTest("FAIL", "test-vehicle-destroyed")
    end
    local record = trafficPeds[source]
    if record and record.couple and not record.couple.removing then
        coupleRuntime.remove(record.couple, "couple-member-destroyed")
        return
    end
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
    if pedTrafficDemoDensity.enabled and pedTrafficDemoDensity.anchor == source then
        pedTrafficSetDemo(false, false)
    end
    if coupleTest and (source == coupleTest.players[1] or source == coupleTest.players[2]) then
        finishCoupleTest("FAIL", "client-left")
    end
    if dealerTest and (source == dealerTest.players[1] or source == dealerTest.players[2]) then
        failDealerTest("client-left")
    end
    if residencyTest and (source == residencyTest.ownerA or source == residencyTest.ownerB) then
        failResidencyTest("client-left")
    end
    if bikeJackTest and (source == bikeJackTest.driver or source == bikeJackTest.passenger) then
        stopBikeJackTest("FAIL", "client-left")
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
    local couples = {}
    for _, relation in pairs(coupleRuntime.relations) do couples[#couples + 1] = relation end
    for _, relation in ipairs(couples) do
        if relation.owner == source and not relation.removing then
            local x, y, z = coupleRuntime.getCentre(relation)
            local newOwner = x and findClosestPopulationResident(x, y, z, "civilian", source) or false
            if newOwner then
                relation.owner = nil
                coupleRuntime.assignOwner(relation, newOwner, "couple-owner-quit")
            else
                coupleRuntime.remove(relation, "couple-owner-quit-no-resident")
            end
        end
    end
    for _, record in pairs(trafficPeds) do
        if not record.group and not record.couple and record.owner == source and not record.removing then
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
        local convergingPlayers = getPopulationWorldConvergencePlayerCount()
        local convergenceAge = getTickCount() - populationWorldPublishedAt
        if convergingPlayers > 0 and convergenceAge <= config.populationWorldConvergenceGrace then
            -- A world mutation invalidates every ACK together. Existing
            -- traffic must survive that bounded convergence window; only new
            -- admission remains fenced by getEligiblePlayers(). Clearing here
            -- would bypass the camera veto and visibly pop every nearby ped.
            if populationWorldConvergenceTraceRevision ~= populationWorld.revision then
                populationWorldConvergenceTraceRevision = populationWorld.revision
                writePopulationTrace("population_world_convergence_hold", {
                    scenario_id = dealerTest and dealerTest.id or false,
                    revision = populationWorld.revision,
                    players = convergingPlayers,
                    grace_ms = config.populationWorldConvergenceGrace,
                })
            end
            return
        end
        clearTraffic(convergingPlayers > 0 and "world-convergence-timeout" or "no-eligible-player")
        return
    end
    if dealerTest or copTest or coupleTest then
        return
    end

    if pedTrafficDemoDensity.enabled then
        local anchorReady = false
        for _, resident in ipairs(players) do
            if resident == pedTrafficDemoDensity.anchor then
                anchorReady = true
                break
            end
        end
        if not anchorReady then
            -- A world revision briefly removes otherwise eligible players from
            -- the ready set while their population profiles reconverge. Keep
            -- the demo transaction alive during that bounded handshake; real
            -- departures are handled by the eligibility and quit paths.
            if isEligiblePlayer(pedTrafficDemoDensity.anchor) then return end
            pedTrafficSetDemo(false, false)
            return
        end
        -- Only the selected camera bubble admits demo peds. Every other
        -- eligible player remains a resident, observer and visibility voter.
        players = {pedTrafficDemoDensity.anchor}
    end

    requestCursor = requestCursor % #players + 1
    for offset = 0, #players - 1 do
        local player = players[(requestCursor + offset - 1) % #players + 1]
        local x, y, z = getElementPosition(player)
        local radii = getPopulationRadii(populationProfiles[player])
        local nativeTarget, civilianTarget, dealerTarget, copTarget, gangTarget, gangTargets = getNativeTargetsNearPlayer(player)
        local request = pendingRequests[player]
        if request and getTickCount() - request.issuedAt > config.requestTimeout then
            pendingRequests[player] = nil
            request = nil
        end

        local stockCountedLive, physicalLive, civilianCount, dealerCount, copCount, gangCounts = getPopulationCountsNearPlayer(player)
        local totalGangCount = 0
        for index = 1, 10 do totalGangCount = totalGangCount + gangCounts[index] end
        local classDeficit = nativeTarget and (civilianTarget - civilianCount >= 1 or dealerTarget - dealerCount >= 1 or
            copTarget - copCount >= 1)
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
        local demoAtPhysicalCeiling = pedTrafficDemoDensity.enabled and player == pedTrafficDemoDensity.anchor and
            physicalLive >= pedTrafficDemoDensity.target
        if not demoAtPhysicalCeiling and nativeTarget and profile and now >= (profile.nextRebalanceAt or 0) and
            (stockCountedLive > nativeTarget or (stockCountedLive >= nativeTarget and classDeficit) or dealerCount - dealerTarget >= 1 or
                copCount - copTarget >= 1) then
            local surplus = false
            local surplusGroup = false
            if civilianCount - civilianTarget >= 1 then
                surplus = findFurthestPopulationPed(x, y, z, radii.civilian, function(record)
                    return record.populationClass == "civilian"
                end)
            end
            if not surplus and dealerCount - dealerTarget >= 1 then
                surplus = findFurthestPopulationPed(x, y, z, radii.civilian, function(record)
                    return record.populationClass == "dealer"
                end)
            end
            if not surplus and copCount - copTarget >= 1 then
                surplus = findFurthestPopulationPed(x, y, z, radii.civilian, function(record)
                    return record.populationClass == "cop"
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
            if not surplus and not surplusGroup and stockCountedLive > nativeTarget then
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
        if selection and selection.populationClass == "civilian" and not request and not next(pendingRequests) and
            not next(pendingVisibilityChecks) then
            local sample15, taken = coupleRuntime.roll()
            local demoRemaining = selection.demoPhysicalCeiling and
                math.min(selection.demoPhysicalCeiling - selection.physicalCount,
                         selection.demoPhysicalCeiling - getTrafficPedCount()) or math.huge
            selection.coupleSample = sample15
            selection.coupleAttempt = taken
            stats.coupleAttempts = stats.coupleAttempts + 1
            writePopulationTrace("couple_rng", {
                player_id = getPopulationClientId(player),
                sample15 = sample15,
                threshold = 29491,
                branch = taken and "taken" or "singleton",
            })
            if taken and (demoRemaining < 2 or getTrafficPedCount() + 2 > config.globalCap or
                #getElementsByType("ped") + 2 > config.pedPoolSoftLimit) then
                stats.coupleRollbacks = stats.coupleRollbacks + 1
                writePopulationTrace("couple_rollback", {
                    player_id = getPopulationClientId(player),
                    sample15 = sample15,
                    reason = "couple-capacity",
                })
                selection = false
            end
        elseif selection then
            selection.coupleAttempt = false
        end
        if selection and selection.populationClass == "gang" and
            countNativeGroupsForOwner(player) >= config.maximumNativeGangGroups then
            selection = false
        end
        if selection and selection.populationClass == "gang" then
            local demoRemaining = selection.demoPhysicalCeiling and
                math.min(selection.demoPhysicalCeiling - selection.physicalCount,
                         selection.demoPhysicalCeiling - getTrafficPedCount()) or config.maximumGangGroupSize
            selection.maximumGroupMembers = math.min(
                config.maximumGangGroupSize,
                demoRemaining,
                config.globalCap - getTrafficPedCount(),
                config.pedPoolSoftLimit - #getElementsByType("ped"))
            if selection.maximumGroupMembers < config.minimumGangGroupSize then
                selection = false
            end
        end
        if selection and not request and not next(pendingRequests) and not next(pendingVisibilityChecks) and
            (not selection.demoPhysicalCeiling or getTrafficPedCount() < selection.demoPhysicalCeiling) and
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
            log(("arbitrate request=%d player=%s class=%s gang=%s maxMembers=%d target=%.2f/%.2f/%.2f live=%d/%d/%d deficit=%.2f/%.2f/%.2f roll=%.2f/%.2f/%.2f"):format(
                    nextRequestId, getPlayerName(player), selection.populationClass, tostring(selection.gang), selection.maximumGroupMembers or 1,
                    selection.civilianTarget, selection.dealerTarget, selection.gangTarget, selection.civilianCount, selection.dealerCount,
                    selection.totalGangCount, selection.civilianDeficit, selection.dealerDeficit, selection.gangDeficit,
                    selection.civilianChance, selection.dealerChance, selection.gangChance))
            writePopulationTrace("candidate_request", {
                request_id = nextRequestId,
                player_id = getPopulationClientId(player),
                population_class = selection.populationClass,
                gang = selection.gang,
                maximum_group_members = selection.maximumGroupMembers or 1,
                couple_attempt = selection.coupleAttempt == true,
                couple_sample15 = selection.coupleSample or false,
                targets = {total = selection.totalTarget, civilian = selection.civilianTarget, dealer = selection.dealerTarget,
                           cop = selection.copTarget, gang = selection.gangTarget},
                live = {
                    total = selection.totalCount,
                    physical = selection.physicalCount,
                    civilian = selection.civilianCount,
                    dealer = selection.dealerCount,
                    cop = selection.copCount,
                    gang = selection.totalGangCount,
                },
                deficits = {civilian = selection.civilianDeficit, dealer = selection.dealerDeficit, cop = selection.copDeficit,
                            gang = selection.gangDeficit},
            })
            triggerClientEvent(player, "pedTraffic:candidateRequest", resourceRoot, nextRequestId, populationWorld.revision,
                               selection.populationClass, selection.gang, selection.maximumGroupMembers or 1,
                               selection.coupleAttempt == true)
            break
        end
    end
end, config.requestInterval, 0)

setTimer(function()
    if not enabled then
        return
    end

    local now = getTickCount()
    local worldConverging = getPopulationWorldConvergencePlayerCount() > 0 and
        now - populationWorldPublishedAt <= config.populationWorldConvergenceGrace
    local groups = {}
    for _, group in pairs(trafficGroups) do groups[#groups + 1] = group end
    for _, group in ipairs(groups) do
        if not group.removing then
            if group.state == "suspending" then
                if now >= (group.handoffDeadline or 0) then
                    finishGroupSuspension(group, "group-suspension-release-timeout")
                end
            elseif group.state == "suspended" then
                local resident = findClosestGroupResident(group, nil, true, true)
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
                        if worldConverging then
                            clearGroupZeroResidentHold(group, "world-convergence")
                        elseif not isCurrentGroupOwnerSpatiallyResident(group) and
                            updateGroupZeroResidentHold(group, "group-no-collision-resident", now) then
                            beginRemovalVisibilityCheck(group, "group-outside-residency")
                        end
                    else
                        if group.visibilityCheckId then
                            finishRemovalVisibilityCheck(pendingVisibilityChecks[group.visibilityCheckId], true, "resident-returned")
                        end
                        clearGroupZeroResidentHold(group, "resident-returned")
                        local closest = findClosestGroupResident(group, nil, true)
                        if not closest then
                            group.handoffCandidate = nil
                            group.handoffCandidateSince = nil
                        elseif not isEligiblePlayer(group.owner) then
                            beginGroupHandoff(group, closest, "group-owner-ineligible")
                        elseif not isCurrentGroupOwnerSpatiallyResident(group) and closest ~= group.owner then
                            if group.handoffCandidate ~= closest then
                                group.handoffCandidate = closest
                                group.handoffCandidateSince = now
                            elseif now - group.handoffCandidateSince >= config.handoffHold then
                                beginGroupHandoff(group, closest, "group-owner-left-residency")
                            end
                        else
                            -- Being marginally closer is not an authority
                            -- change: revoking a healthy resident owner
                            -- restarts the whole native group task. A real
                            -- departure still transfers after the hold above.
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
        if not record.group and not record.couple and not record.removing and isElement(record.ped) then
            if record.state == "suspending" then
                if now >= (record.handoffDeadline or 0) then
                    finishSuspension(record, "suspension-release-timeout")
                end
            elseif record.state == "suspended" then
                local x, y, z = getElementPosition(record.ped)
                local resident = findClosestPopulationResident(x, y, z, record.populationClass, nil, true)
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
                local closest = findClosestPopulationResident(x, y, z, record.populationClass)
                if not closest then
                    if not isEligiblePlayer(record.owner) then
                        beginSuspension(record, "owner-ineligible")
                    elseif worldConverging then
                        record.outsideResidencySince = nil
                    elseif not record.outsideResidencySince then
                        record.outsideResidencySince = now
                        writePopulationTrace("collision_residency_hold_started", {
                            traffic_id = record.id,
                            epoch = record.epoch,
                            owner_id = getPopulationClientId(record.owner),
                            hold_ms = config.handoffHold,
                            reason = "no-collision-resident",
                        })
                    elseif now - record.outsideResidencySince >= config.handoffHold then
                        -- Keep the existing owner/task alive while any camera
                        -- can still see the ped. The visibility transaction
                        -- removes it atomically once it is safely off-screen,
                        -- matching retail lifecycle without walk/stop churn.
                        beginRemovalVisibilityCheck(record, "outside-residency")
                    end
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
                    elseif not isCurrentPopulationOwnerSpatiallyResident(record) and closest ~= record.owner then
                        if record.handoffCandidate ~= closest then
                            record.handoffCandidate = closest
                            record.handoffCandidateSince = now
                        elseif now - record.handoffCandidateSince >= config.handoffHold then
                            beginHandoff(record, closest, "owner-left-residency")
                        end
                    else
                        -- Keep a healthy resident owner sticky. The native
                        -- Wander task is more valuable than a small distance
                        -- improvement to another client, and the collision
                        -- fence still handles a real residency departure.
                        record.handoffCandidate = nil
                        record.handoffCandidateSince = nil
                    end
                end
            end
        end
    end
end, 1000, 0)

local COP_TEST_CANDIDATE_RETRY = 750
-- This south-LS fixture produced a stable post_home_coming profile with a
-- positive normal cop target and no no-cops suppression on both clients. The
-- old City Hall point could leave GTA's popcycle producer stale after a
-- scripted teleport and turn the harness into a timeout-only test.
local COP_TEST_POSITION = {x = 2526.7, y = -1773, z = 13.38}

local function restoreCopTestPlayers(test)
    for player, state in pairs(test.savedPlayers or {}) do
        if isElement(player) then
            setElementDimension(player, state.dimension)
            setElementInterior(player, state.interior)
            setElementPosition(player, state.x, state.y, state.z)
            setElementRotation(player, state.rx, state.ry, state.rz)
            setElementFrozen(player, state.frozen)
            setCameraTarget(player, player)
        end
    end
    if test.savedTime then setTime(test.savedTime[1], test.savedTime[2]) end
end

local function finishCopTest(result, reason)
    local test = copTest
    if not test then return end
    copTest = false
    for player, request in pairs(pendingRequests) do
        if test.requestIds and test.requestIds[request.id] then pendingRequests[player] = nil end
    end
    for checkId, check in pairs(pendingVisibilityChecks) do
        if check.kind == "candidate" and check.request and test.requestIds and test.requestIds[check.request.id] then
            pendingVisibilityChecks[checkId] = nil
        end
    end
    if result ~= "PASS" and test.record and not test.record.removing then
        removeRecord(test.record, "cop-test-" .. tostring(result):lower())
    end
    restoreCopTestPlayers(test)
    writePopulationTrace("cop_test_result", {
        scenario_id = test.id, result = result, reason = reason,
        traffic_id = test.record and test.record.id or false,
        initial_epoch = test.initialEpoch or false,
        final_epoch = test.record and test.record.epoch or false,
        cleanup_acks = test.cleanupAcks or 0,
        patrol_distance = test.patrolDistance or 0,
        patrol_observed = (test.patrolDistance or 0) >= 3 and test.branches and test.branches.go_to == true or false,
        observed_branches = test.branches or {},
        forbidden_path_seen = test.forbiddenPathSeen == true,
        wanted_changed = test.wantedChanged == true,
        double_owner_seen = test.doubleOwnerSeen == true,
    })
    local message = ("Cop test %s: %s (scenario %d)"):format(result, tostring(reason), test.id)
    outputChatBox(message, root, result == "PASS" and 80 or 255, result == "PASS" and 220 or 80, 120)
    log(message, true)
end

local function requestCopTestSamples(test, phase)
    test.sampleSequence = (test.sampleSequence or 0) + 1
    test.pendingSample = {id = test.sampleSequence, phase = phase, samples = {}}
    for _, player in ipairs(test.players) do
        triggerClientEvent(player, "pedTraffic:copTestSample", resourceRoot, test.id, test.sampleSequence, test.record.ped, test.record.epoch, phase)
    end
end

local function scheduleCopTestSamples(test, phase, expectedPhase, delay)
    -- MTA clones table arguments passed through timers. Keep only scalar
    -- identity in the timer and reacquire the live harness state before the
    -- sample request so replies update the same table inspected by the test.
    setTimer(function(testId, samplePhase, requiredPhase)
        local activeTest = copTest
        if not activeTest or activeTest.id ~= testId or activeTest.phase ~= requiredPhase then return end
        requestCopTestSamples(activeTest, samplePhase)
    end, delay or 750, 1, test.id, phase, expectedPhase)
end

local function issueCopTestCandidate(test)
    if next(pendingRequests) or next(pendingVisibilityChecks) then return false end
    local player = test.players[1]
    local profile = populationProfiles[player]
    local totalTarget, civilianTarget, dealerTarget, copTarget, gangTarget, gangTargets = getNativeTargetsNearPlayer(player)
    if not profile or profile.receivedAt < test.startedAt or totalTarget == false or copTarget <= 0 or profile.worldLevel ~= 1 or
        profile.copSuppressionFlags ~= 0 then
        return false
    end

    local stockCountedLive, physicalLive, civilianCount, dealerCount, copCount, gangCounts = getPopulationCountsNearPlayer(player)
    local totalGangCount = 0
    for index = 1, 10 do totalGangCount = totalGangCount + gangCounts[index] end
    if stockCountedLive >= totalTarget or copCount >= copTarget then
        return finishCopTest("FAIL", "cop-deficit-unavailable")
    end

    nextRequestId = nextRequestId + 1
    local selection = {
        requestId = nextRequestId,
        populationClass = "cop",
        gang = false,
        maximumGroupMembers = 1,
        profileSignature = profile.signature,
        totalTarget = totalTarget,
        civilianTarget = civilianTarget,
        dealerTarget = dealerTarget,
        copTarget = copTarget,
        gangTarget = gangTarget,
        gangTargets = gangTargets,
        totalCount = stockCountedLive,
        physicalCount = physicalLive,
        civilianCount = civilianCount,
        dealerCount = dealerCount,
        copCount = copCount,
        gangCounts = gangCounts,
        totalGangCount = totalGangCount,
        civilianDeficit = civilianTarget - civilianCount,
        dealerDeficit = dealerTarget - dealerCount,
        copDeficit = copTarget - copCount,
        gangDeficit = gangTarget - totalGangCount,
        civilianChance = math.min(100, math.max(0, civilianTarget - civilianCount) * 100),
        dealerChance = math.min(100, math.max(0, dealerTarget - dealerCount) * 100),
        copChance = math.min(100, math.max(0, copTarget - copCount) * 100),
        gangChance = math.min(100, math.max(0, gangTarget - totalGangCount) * 100),
    }
    pendingRequests[player] = {
        id = nextRequestId,
        issuedAt = getTickCount(),
        worldRevision = populationWorld.revision,
        selection = selection,
    }
    test.lastRequestAt = getTickCount()
    test.requestIds[nextRequestId] = true
    stats.requests = stats.requests + 1
    stats.populationSelections.cop = stats.populationSelections.cop + 1
    writePopulationTrace("candidate_request", {
        request_id = nextRequestId,
        player_id = getPopulationClientId(player),
        population_class = "cop",
        gang = false,
        maximum_group_members = 1,
        scenario_id = test.id,
        targets = {total = totalTarget, civilian = civilianTarget, dealer = dealerTarget, cop = copTarget, gang = gangTarget},
        live = {
            total = stockCountedLive,
            physical = physicalLive,
            civilian = civilianCount,
            dealer = dealerCount,
            cop = copCount,
            gang = totalGangCount,
        },
        deficits = {
            civilian = selection.civilianDeficit,
            dealer = selection.dealerDeficit,
            cop = selection.copDeficit,
            gang = selection.gangDeficit,
        },
    })
    triggerClientEvent(player, "pedTraffic:candidateRequest", resourceRoot, nextRequestId, populationWorld.revision, "cop", false, 1, false)
    return true
end

local function validateCopTestSamples(test, pending)
    local phase = pending.phase
    local samples = pending.samples
    if not samples or not samples[test.players[1]] or not samples[test.players[2]] then return false end
    local expectedOwner = phase == "handoff" and test.ownerB or test.ownerA
    local activeOwners = 0
    for _, player in ipairs(test.players) do
        local sample = samples[player]
        local isOwner = player == expectedOwner
        if sample.ambientCopTask then activeOwners = activeOwners + 1 end
        if sample.wanted ~= test.savedPlayers[player].wanted then test.wantedChanged = true end
        if sample.forbiddenTask then test.forbiddenPathSeen = true end
        if sample.model ~= 280 or sample.populationClass ~= "cop" or sample.logicalPedType ~= 6 or sample.worldLevel ~= 1 or
            sample.activeWeapon ~= 0 or sample.nightstick ~= 3 or sample.pistol ~= 22 or sample.pistolAmmo ~= 1000 or
            math.abs(sample.pistolSkillStat - 40) > 0.01 or math.abs(sample.armor) > 0.01 or
            sample.wanted ~= test.savedPlayers[player].wanted or sample.syncer ~= isOwner or sample.profilePresent ~= true or
            sample.profileName ~= "ambient-cop-safe" or sample.assignment ~= isOwner or sample.assignmentAccepted ~= isOwner or
            sample.profileActive ~= isOwner or
            sample.ambientCopTask ~= isOwner or sample.vtableSafe ~= isOwner or sample.forbiddenTask ~= false or
            (isOwner and (sample.hasWander ~= true or sample.rootTask ~= "TASK_COMPLEX_WANDER" or
                          sample.shootingRate ~= 30 or sample.accuracy ~= 60)) then
            finishCopTest("FAIL", "cop-sample-mismatch-" .. phase .. "-client-" .. getPopulationClientId(player))
            return false
        end
    end
    if activeOwners ~= 1 then
        test.doubleOwnerSeen = activeOwners > 1
        finishCopTest("FAIL", activeOwners > 1 and "double-owner-task" or "owner-task-missing")
        return false
    end
    return true
end

addEvent("pedTraffic:copTestSampleResult", true)
addEventHandler("pedTraffic:copTestSampleResult", resourceRoot, function(testId, sampleId, phase, data)
    local test = copTest
    local pending = test and test.pendingSample
    if not test or test.id ~= testId or type(data) ~= "table" or (phase ~= "initial" and phase ~= "handoff") or
        not pending or pending.id ~= sampleId or pending.phase ~= phase then return end
    writePopulationTrace("cop_test_sample", {
        scenario_id = test.id,
        sample_id = sampleId,
        phase = phase,
        player_id = getPopulationClientId(client),
        owner_id = getPopulationClientId(phase == "handoff" and test.ownerB or test.ownerA),
        traffic_id = test.record.id,
        epoch = test.record.epoch,
        model = data.model,
        population_class = data.populationClass,
        logical_ped_type = data.logicalPedType,
        world_level = data.worldLevel,
        active_weapon = data.activeWeapon,
        nightstick = data.nightstick,
        pistol = data.pistol,
        pistol_ammo = data.pistolAmmo,
        pistol_skill_stat = data.pistolSkillStat,
        armor = data.armor,
        shooting_rate = data.shootingRate,
        accuracy = data.accuracy,
        wanted = data.wanted,
        syncer = data.syncer,
        assignment = data.assignment,
        assignment_accepted = data.assignmentAccepted,
        profile_present = data.profilePresent,
        profile_name = data.profileName,
        profile_active = data.profileActive,
        has_wander = data.hasWander,
        root_task = data.rootTask,
        branch = data.branch,
        forbidden_task = data.forbiddenTask,
        ambient_cop_task = data.ambientCopTask,
        vtable_safe = data.vtableSafe,
        position = {x = data.x, y = data.y, z = data.z},
    })
    pending.samples[client] = data
    if not pending.samples[test.players[1]] or not pending.samples[test.players[2]] then return end
    test.pendingSample = nil
    if not validateCopTestSamples(test, pending) then return end
    if phase == "initial" then
        local ownerSample = pending.samples[test.ownerA]
        if ownerSample.branch and not test.branches[ownerSample.branch] then
            test.branches[ownerSample.branch] = true
            writePopulationTrace("cop_test_branch", {
                scenario_id = test.id,
                sample_id = sampleId,
                phase = phase,
                traffic_id = test.record.id,
                owner_id = getPopulationClientId(test.ownerA),
                epoch = test.record.epoch,
                branch = ownerSample.branch,
            })
        end
        if test.patrolLastPosition then
            local dx = ownerSample.x - test.patrolLastPosition.x
            local dy = ownerSample.y - test.patrolLastPosition.y
            local dz = ownerSample.z - test.patrolLastPosition.z
            test.patrolDistance = test.patrolDistance + math.sqrt(dx * dx + dy * dy + dz * dz)
        end
        test.patrolLastPosition = {x = ownerSample.x, y = ownerSample.y, z = ownerSample.z}
        if test.patrolDistance >= 3 and test.branches.go_to then
            beginHandoff(test.record, test.ownerB, "cop-test-handoff")
            test.phase = "handoff"
        else
            scheduleCopTestSamples(test, "initial", "initial-patrol", 250)
        end
    else
        test.phase = "cleanup"
        local trafficId = test.record.id
        removeRecord(test.record, "cop-test-cleanup")
        for _, player in ipairs(test.players) do
            triggerClientEvent(player, "pedTraffic:copTestCleanup", resourceRoot, test.id, trafficId)
        end
    end
end)

addEvent("pedTraffic:copTestCleanupResult", true)
addEventHandler("pedTraffic:copTestCleanupResult", resourceRoot, function(testId, trafficId, data)
    local test = copTest
    if not test or test.id ~= testId or test.phase ~= "cleanup" or trafficId ~= test.record.id or type(data) ~= "table" then return end
    if data.elementPresent or data.assignmentPresent or data.profilePresent then
        return finishCopTest("FAIL", "cop-cleanup-residual-client-" .. getPopulationClientId(client))
    end
    if not test.cleanupPlayers[client] then
        test.cleanupPlayers[client] = true
        test.cleanupAcks = test.cleanupAcks + 1
        writePopulationTrace("cop_test_cleanup_ack", {
            scenario_id = test.id,
            traffic_id = trafficId,
            player_id = getPopulationClientId(client),
            accepted = true,
        })
    end
    if test.cleanupAcks == 2 then finishCopTest("PASS", "cop-native-locomotion-handoff-wanted-cleanup") end
end)

local function startCopTest(player)
    if copTest then return outputChatBox("A cop test is already running", player, 255, 160, 80) end
    local players = getElementsByType("player")
    if #players ~= 2 then return outputChatBox("The cop test requires exactly two connected clients", player, 255, 160, 80) end
    for _, candidate in ipairs(players) do
        if isPedDead(candidate) or isPedInVehicle(candidate) then
            return outputChatBox("Both cop-test clients must be alive and on foot", player, 255, 160, 80)
        end
    end
    if not debugEnabled then
        debugEnabled = true
        triggerClientEvent(root, "pedTraffic:setDebug", resourceRoot, true)
    end
    resetPopulationTrace()
    clearTraffic("cop-test-reset")
    if not enabled then setEnabled(true, player) end
    nextCopTestId = nextCopTestId + 1
    local hour, minute = getTime()
    local test = {id = nextCopTestId, players = players, savedPlayers = {}, savedTime = {hour, minute}, startedAt = getTickCount(),
                  phase = "await-profile", cleanupPlayers = {}, cleanupAcks = 0, requestIds = {}, sampleSequence = 0,
                  patrolDistance = 0, branches = {}, forbiddenPathSeen = false, wantedChanged = false, doubleOwnerSeen = false}
    copTest = test
    writePopulationTrace("cop_test_started", {
        scenario_id = test.id,
        player_ids = {getPopulationClientId(players[1]), getPopulationClientId(players[2])},
    })
    setTime(12, 0)
    for index, candidate in ipairs(players) do
        local x, y, z = getElementPosition(candidate)
        local rx, ry, rz = getElementRotation(candidate)
        test.savedPlayers[candidate] = {x = x, y = y, z = z, rx = rx, ry = ry, rz = rz, dimension = getElementDimension(candidate),
                                        interior = getElementInterior(candidate), frozen = isElementFrozen(candidate),
                                        wanted = getPlayerWantedLevel(candidate)}
        setElementDimension(candidate, 0)
        setElementInterior(candidate, 0)
        setElementPosition(candidate, COP_TEST_POSITION.x + (index - 1) * 2, COP_TEST_POSITION.y, COP_TEST_POSITION.z)
    end
    outputChatBox("Cop test started: converging the LS profile and authentic cop candidate", root, 120, 220, 255)
end

setTimer(function()
    local test = copTest
    if not test then return end
    if getTickCount() - test.startedAt > 60000 then return finishCopTest("FAIL", "cop-test-timeout-" .. test.phase) end
    if test.phase == "await-profile" then
        local ready = true
        for _, player in ipairs(test.players) do
            local profile = populationProfiles[player]
            local totalTarget, _, _, copTarget = getNativeTargetsNearPlayer(player)
            if not profile or profile.receivedAt < test.startedAt or profile.worldLevel ~= 1 or profile.copSuppressionFlags ~= 0 or
                totalTarget == false or type(copTarget) ~= "number" or copTarget <= 0 then
                ready = false
                break
            end
        end
        if ready then
            test.phase = "await-cop"
            issueCopTestCandidate(test)
        end
    elseif test.phase == "await-cop" then
        for _, record in pairs(trafficPeds) do
            if record.populationClass == "cop" and record.createdAt >= test.startedAt and record.state == "active" then
                test.record = record
                test.ownerA = record.owner
                test.ownerB = test.players[1] == test.ownerA and test.players[2] or test.players[1]
                test.initialEpoch = record.epoch
                for _, player in ipairs(test.players) do setElementFrozen(player, true) end
                test.phase = "initial-patrol"
                scheduleCopTestSamples(test, "initial", "initial-patrol")
                return
            end
        end
        if not test.lastRequestAt or getTickCount() - test.lastRequestAt >= COP_TEST_CANDIDATE_RETRY then
            issueCopTestCandidate(test)
        end
    elseif test.phase == "handoff" and test.record.state == "active" and test.record.owner == test.ownerB and
        test.record.epoch == test.initialEpoch + 1 then
        test.phase = "handoff-sample"
        scheduleCopTestSamples(test, "handoff", "handoff-sample")
    end
end, 200, 0)

local COUPLE_TEST_POSITION = {x = 2528.5, y = -1767.5, z = 13.38}

local function restoreCoupleTestPlayers(test)
    for player, state in pairs(test.savedPlayers or {}) do
        if isElement(player) then
            setElementDimension(player, state.dimension)
            setElementInterior(player, state.interior)
            setElementPosition(player, state.x, state.y, state.z)
            setElementRotation(player, state.rx, state.ry, state.rz)
            setElementFrozen(player, state.frozen)
            setCameraTarget(player, player)
        end
    end
    if test.savedTime then setTime(test.savedTime[1], test.savedTime[2]) end
end

local function destroyCoupleTestPeds(test)
    if isElement(test.damageAttacker) then destroyElement(test.damageAttacker) end
    test.damageAttacker = nil
    for _, ped in ipairs(test.peds or {}) do
        if isElement(ped) then destroyElement(ped) end
    end
end

finishCoupleTest = function(result, reason)
    local test = coupleTest
    if not test then return end
    coupleTest = false
    if result ~= "PASS" then
        triggerClientEvent(root, "pedTraffic:revokeCouple", resourceRoot, test.relationId, test.relationEpoch,
                           "couple-test-failed")
        destroyCoupleTestPeds(test)
    end
    restoreCoupleTestPlayers(test)
    writePopulationTrace("couple_test_result", {
        scenario_id = test.id,
        result = result,
        reason = reason,
        relation_id = test.relationId,
        initial_epoch = 1,
        final_epoch = test.relationEpoch,
        leader_index = test.leaderIndex or false,
        cleanup_acks = test.cleanupAckCount or 0,
        pair_cardinality = #test.peds,
        owner_release_before_acquire = test.ownerAReleased == true,
        initial_active = test.initialActive == true,
        handoff_active = test.handoffActive == true,
        initial_soak_samples = test.soakCounts and test.soakCounts.initial or 0,
        handoff_soak_samples = test.soakCounts and test.soakCounts.handoff or 0,
        initial_distance_travelled = test.soakDistanceTravelled and test.soakDistanceTravelled.initial or 0,
        handoff_distance_travelled = test.soakDistanceTravelled and test.soakDistanceTravelled.handoff or 0,
        max_pair_distance = test.maxPairDistance or 0,
        follower_sprint_samples = test.followerSprintSamples or 0,
        observer_fast_gait_samples_initial = test.observerFastGaitSamples and test.observerFastGaitSamples.initial or 0,
        observer_fast_gait_samples_handoff = test.observerFastGaitSamples and test.observerFastGaitSamples.handoff or 0,
        observer_presentation_active_samples_initial = test.observerPresentationActiveSamples and
            test.observerPresentationActiveSamples.initial or 0,
        observer_presentation_active_samples_handoff = test.observerPresentationActiveSamples and
            test.observerPresentationActiveSamples.handoff or 0,
        observer_presentation_eligible_samples_initial = test.observerPresentationEligibleSamples and
            test.observerPresentationEligibleSamples.initial or 0,
        observer_presentation_eligible_samples_handoff = test.observerPresentationEligibleSamples and
            test.observerPresentationEligibleSamples.handoff or 0,
        observer_presentation_eligible_active_samples_initial = test.observerPresentationEligibleActiveSamples and
            test.observerPresentationEligibleActiveSamples.initial or 0,
        observer_presentation_eligible_active_samples_handoff = test.observerPresentationEligibleActiveSamples and
            test.observerPresentationEligibleActiveSamples.handoff or 0,
        observer_presentation_far_active_samples_initial = test.observerPresentationFarActiveSamples and
            test.observerPresentationFarActiveSamples.initial or 0,
        observer_presentation_far_active_samples_handoff = test.observerPresentationFarActiveSamples and
            test.observerPresentationFarActiveSamples.handoff or 0,
        social_event_forwarded = test.socialEventForwarded == true,
        dissolved = test.dissolved == true,
    })
    local message = ("Couple test %s: %s (scenario %d)"):format(result, tostring(reason), test.id)
    outputChatBox(message, root, result == "PASS" and 80 or 255, result == "PASS" and 220 or 80, 120)
    log(message, true)
end

local function beginCoupleTestCleanup(test)
    if coupleTest ~= test or test.phase == "cleanup" then return end
    test.phase = "cleanup"
    destroyCoupleTestPeds(test)
    local trafficIds = {test.trafficIds[1], test.trafficIds[2]}
    for _, player in ipairs(test.players) do
        triggerClientEvent(player, "pedTraffic:coupleTestCleanup", resourceRoot, test.id, test.relationId, trafficIds)
    end
end

local function requestCoupleTestSamples(test, phase)
    test.sampleSequence = test.sampleSequence + 1
    test.pendingSample = {id = test.sampleSequence, phase = phase, samples = {}, requestedAt = getTickCount()}
    for _, player in ipairs(test.players) do
        triggerClientEvent(player, "pedTraffic:coupleTestSample", resourceRoot, test.id, test.sampleSequence, test.relationId,
                           test.relationEpoch, test.peds[1], test.peds[2], phase)
    end
end

local function scheduleCoupleTestSamples(test, phase, delay)
    setTimer(function()
        if coupleTest == test and not test.pendingSample then requestCoupleTestSamples(test, phase) end
    end, delay or 250, 1)
end

addEvent("pedTraffic:coupleArmState", true)
addEventHandler("pedTraffic:coupleArmState", resourceRoot, function(relationId, relationEpoch, sideA, sideB)
    relationId = tonumber(relationId)
    relationEpoch = tonumber(relationEpoch)
    sideA = tonumber(sideA)
    sideB = tonumber(sideB)
    if not relationId or not relationEpoch or (sideA ~= 1 and sideA ~= 2) or (sideB ~= 1 and sideB ~= 2) then return end

    local peds = false
    local observers = {}
    local couple = coupleRuntime.relations[relationId]
    if couple and couple.epoch == relationEpoch and couple.owner == client then
        peds = {couple.members[1].ped, couple.members[2].ped}
        couple.armSides = {sideA, sideB}
        for _, observer in ipairs(getEligiblePlayers()) do
            if observer ~= couple.owner then observers[#observers + 1] = observer end
        end
    elseif coupleTest and coupleTest.relationId == relationId and coupleTest.relationEpoch == relationEpoch and
        coupleTest.owner == client then
        peds = coupleTest.peds
        coupleTest.armSides = {sideA, sideB}
        for _, observer in ipairs(coupleTest.players) do
            if observer ~= coupleTest.owner and isElement(observer) then observers[#observers + 1] = observer end
        end
    end
    if not peds or not isElement(peds[1]) or not isElement(peds[2]) then return end

    -- Publish the pair as one epoch-fenced message. Two element-data writes
    -- can be observed in different frames and briefly select an exterior arm.
    for _, observer in ipairs(observers) do
        triggerClientEvent(observer, "pedTraffic:updateCouplePresentationSides", resourceRoot,
                           relationId, relationEpoch, sideA, sideB)
    end
    setElementData(peds[1], "neon:ambientPedCoupleHandSide", sideA)
    setElementData(peds[2], "neon:ambientPedCoupleHandSide", sideB)
    writePopulationTrace("couple_arm_state", {
        relation_id = relationId,
        relation_epoch = relationEpoch,
        owner_id = getPopulationClientId(client),
        sides = {sideA, sideB},
    })
end)

addEvent("pedTraffic:coupleEvidence", true)
addEventHandler("pedTraffic:coupleEvidence", resourceRoot, function(relationId, relationEpoch, evidence, data)
    local couple = coupleRuntime.relations[relationId]
    if couple then
        if relationEpoch ~= couple.epoch or client ~= couple.owner or type(data) ~= "table" then return end
        writePopulationTrace("couple_evidence", {
            relation_id = relationId,
            relation_epoch = relationEpoch,
            owner_id = getPopulationClientId(client),
            state = couple.state,
            evidence = evidence,
            leader_index = data.leaderIndex or false,
            walk_speeds = data.walkSpeeds or false,
            native_diagnostic = data.nativeDiagnostic or false,
        })
        if evidence == "failure" or evidence == "ownership-lost" then
            coupleRuntime.remove(couple, tostring(data.reason or evidence))
        elseif evidence == "accepted" and couple.state == "assigning" then
            local leaderIndex = tonumber(data.leaderIndex)
            if leaderIndex ~= 1 and leaderIndex ~= 2 then return coupleRuntime.remove(couple, "couple-invalid-leader") end
            if couple.leaderIndex and couple.leaderIndex ~= leaderIndex then return coupleRuntime.remove(couple, "couple-leader-changed") end
            couple.leaderIndex = leaderIndex
            couple.walkSpeeds = data.walkSpeeds
            couple.state = "active"
            for index, record in ipairs(couple.members) do
                record.state = "active"
                setElementFrozen(record.ped, false)
                setElementData(record.ped, "neon:ambientPedRelationLeader", index == leaderIndex)
            end
            writePopulationTrace("couple_committed", {
                relation_id = couple.id,
                relation_epoch = couple.epoch,
                leader_index = leaderIndex,
                walk_speeds = data.walkSpeeds,
            })
        elseif evidence == "released" and couple.state == "revoking" then
            local x, y, z = coupleRuntime.getCentre(couple)
            local newOwner = x and findClosestPopulationResident(x, y, z, "civilian") or false
            if not newOwner or not coupleRuntime.assignOwner(couple, newOwner, "couple-release-ack") then
                coupleRuntime.remove(couple, "couple-handoff-owner-missing")
            end
        elseif evidence == "dissolved" then
            coupleRuntime.dissolve(couple, data.nativeDiagnostic and data.nativeDiagnostic.reason or "native-task-ended")
        end
        return
    end

    local test = coupleTest
    if not test or relationId ~= test.relationId or relationEpoch ~= test.relationEpoch or client ~= test.owner or
        type(data) ~= "table" then
        return
    end
    writePopulationTrace("couple_test_evidence", {
        scenario_id = test.id,
        relation_id = relationId,
        relation_epoch = relationEpoch,
        owner_id = getPopulationClientId(client),
        phase = test.phase,
        evidence = evidence,
        leader_index = data.leaderIndex or false,
        walk_speeds = data.walkSpeeds or false,
        native_diagnostic = data.nativeDiagnostic or false,
    })
    if evidence == "failure" or evidence == "ownership-lost" then
        return finishCoupleTest("FAIL", tostring(data.reason or evidence))
    end
    if evidence == "accepted" then
        local leaderIndex = tonumber(data.leaderIndex)
        local speeds = data.walkSpeeds
        if (leaderIndex ~= 1 and leaderIndex ~= 2) or type(speeds) ~= "table" or tonumber(speeds[1]) == nil or
            tonumber(speeds[2]) == nil then
            return finishCoupleTest("FAIL", "invalid-couple-readiness")
        end
        if test.leaderIndex and test.leaderIndex ~= leaderIndex then
            return finishCoupleTest("FAIL", "leader-changed-during-handoff")
        end
        test.leaderIndex = leaderIndex
        test.walkSpeeds = {tonumber(speeds[1]), tonumber(speeds[2])}
        if test.phase == "await-owner-a" then
            test.phase = "soak-initial"
            triggerClientEvent(test.ownerB, "pedTraffic:assignCouplePresentation", resourceRoot, test.relationId,
                               test.relationEpoch, test.peds[1], test.peds[2], "couple-test-owner-ready",
                               test.armSides and test.armSides[1] or false, test.armSides and test.armSides[2] or false)
            scheduleCoupleTestSamples(test, "soak-initial", 500)
        elseif test.phase == "await-owner-b" then
            test.phase = "soak-handoff"
            triggerClientEvent(test.ownerA, "pedTraffic:assignCouplePresentation", resourceRoot, test.relationId,
                               test.relationEpoch, test.peds[1], test.peds[2], "couple-test-owner-ready",
                               test.armSides and test.armSides[1] or false, test.armSides and test.armSides[2] or false)
            scheduleCoupleTestSamples(test, "soak-handoff", 500)
        end
    elseif evidence == "released" and test.phase == "revoke-owner-a" and client == test.ownerA then
        test.ownerAReleased = true
        test.owner = test.ownerB
        test.relationEpoch = test.relationEpoch + 1
        test.armSides = nil
        for _, ped in ipairs(test.peds) do
            setElementData(ped, "neon:ambientPedRelationEpoch", test.relationEpoch)
            if not setElementSyncer(ped, test.ownerB, true) then
                return finishCoupleTest("FAIL", "couple-handoff-syncer-refused")
            end
        end
        test.phase = "await-owner-b"
        triggerClientEvent(test.ownerB, "pedTraffic:assignCouple", resourceRoot, test.relationId, test.relationEpoch,
                           test.peds[1], test.peds[2], test.leaderIndex, "couple-test-handoff")
        triggerClientEvent(test.ownerA, "pedTraffic:assignCouplePresentation", resourceRoot, test.relationId,
                           test.relationEpoch, test.peds[1], test.peds[2], "couple-test-handoff-observer", false, false)
    elseif evidence == "dissolved" and (test.phase == "await-dissolution" or test.phase == "await-forward-dissolution") then
        test.dissolved = true
        writePopulationTrace("couple_test_dissolved", {
            scenario_id = test.id,
            relation_id = test.relationId,
            relation_epoch = test.relationEpoch,
            owner_id = getPopulationClientId(client),
            reason = data.nativeDiagnostic and data.nativeDiagnostic.reason or "native-task-ended",
        })
        -- Exercise the production dissolution broadcast before the fixture
        -- peds disappear, so the observer releases its surviving IK lease for
        -- the lifecycle reason rather than only as a side effect of streaming.
        triggerClientEvent(root, "pedTraffic:revokeCouple", resourceRoot, test.relationId, test.relationEpoch,
                           "couple-test-dissolved")
        setTimer(function()
            if coupleTest == test then beginCoupleTestCleanup(test) end
        end, 500, 1)
    end
end)

local function validateCoupleTestSample(test, player, data, owner)
    if type(data) ~= "table" or data.relationId ~= test.relationId or data.relationEpoch ~= test.relationEpoch or
        type(data.members) ~= "table" or type(data.members[1]) ~= "table" or type(data.members[2]) ~= "table" then
        return false, "malformed-sample"
    end
    if owner then
        if not data.assignment or not data.assignmentAccepted or not data.active or data.leaderIndex ~= test.leaderIndex then
            return false, "owner-relation-inactive"
        end
        local diagnostic = data.diagnostic
        if type(diagnostic) ~= "table" or not diagnostic.active or type(diagnostic.members) ~= "table" then
            return false, "owner-diagnostic-inactive"
        end
        for index = 1, 2 do
            local member = data.members[index]
            local nativeMember = diagnostic.members[index]
            if not member.present or not member.syncer or not member.profileActive or not member.hasCouple or
                type(nativeMember) ~= "table" or nativeMember.primaryTaskType ~= 1215 or not nativeMember.primaryTaskMatchesLease or
                not nativeMember.reciprocalPartner or not nativeMember.leaderRoleMatches then
                return false, "owner-member-mismatch-" .. tostring(index)
            end
        end
        local leader = diagnostic.members[test.leaderIndex]
        local follower = diagnostic.members[test.leaderIndex == 1 and 2 or 1]
        if leader.subTaskType ~= 912 or follower.subTaskType ~= 1208 then
            return false, "native-subtasks-not-ready"
        end
    else
        if data.assignment or data.assignmentAccepted or data.active then
            return false, "observer-native-ai-active"
        end
        if not data.presentation then return false, "observer-presentation-missing" end
        -- The presentation lease can legitimately have no active PointArm on
        -- a sample where the retail side/orientation gate rejects either arm.
        -- A valid token plus a successful update proves the observer lease;
        -- requiring both arms continuously would reject vanilla-compatible
        -- turns even though no observer AI or lifecycle mismatch exists.
        if not data.presentationAccepted or not data.presentationToken or data.presentationStage ~= "updated" then
            return false, "observer-presentation-not-ready"
        end
        for index = 1, 2 do
            if data.members[index].syncer or data.members[index].hasCouple or data.members[index].hasWalkAlongside or
                data.members[index].hasWander then
                return false, "observer-native-task-active-" .. tostring(index)
            end
        end
    end
    return true
end

local function recordCoupleSoakSample(test, phase, data, observerData)
    local key = phase == "soak-initial" and "initial" or "handoff"
    local leader = data.members[test.leaderIndex]
    local follower = data.members[test.leaderIndex == 1 and 2 or 1]
    local observerFollower = type(observerData) == "table" and type(observerData.members) == "table" and
                                 observerData.members[test.leaderIndex == 1 and 2 or 1] or false
    local pairDistance = tonumber(data.pairDistance)
    if not pairDistance or pairDistance < 0 or pairDistance > 10 then
        return false, "couple-soak-distance-invalid"
    end
    test.maxPairDistance = math.max(test.maxPairDistance or 0, pairDistance)
    if tostring(follower.moveState) == "sprint" then
        test.followerSprintSamples = (test.followerSprintSamples or 0) + 1
    end
    local observerMoveState = observerFollower and tostring(observerFollower.moveState) or "unavailable"
    if observerMoveState == "run" or observerMoveState == "sprint" then
        test.observerFastGaitSamples[key] = test.observerFastGaitSamples[key] + 1
    end
    if type(observerData) == "table" and observerData.presentationActive == true then
        test.observerPresentationActiveSamples[key] = test.observerPresentationActiveSamples[key] + 1
    end
    local observerPairDistance = type(observerData) == "table" and tonumber(observerData.pairDistance) or false
    if observerPairDistance and observerPairDistance < 1.5 then
        test.observerPresentationEligibleSamples[key] = test.observerPresentationEligibleSamples[key] + 1
        if observerData.presentationActive == true then
            test.observerPresentationEligibleActiveSamples[key] = test.observerPresentationEligibleActiveSamples[key] + 1
        end
    elseif observerPairDistance and observerData.presentationActive == true then
        test.observerPresentationFarActiveSamples[key] = test.observerPresentationFarActiveSamples[key] + 1
    end

    local x, y = tonumber(leader.x), tonumber(leader.y)
    if not x or not y then return false, "couple-soak-position-missing" end
    local previous = test.lastSoakLeaderPosition[key]
    if previous then
        local stepDistance = getDistanceBetweenPoints2D(previous.x, previous.y, x, y)
        -- The harness samples once per second. A multi-metre discontinuity
        -- here is a sync/ownership jump, not ordinary couple locomotion.
        if stepDistance > 5 then return false, "couple-soak-transform-jump" end
        test.soakDistanceTravelled[key] = test.soakDistanceTravelled[key] + stepDistance
    end
    test.lastSoakLeaderPosition[key] = {x = x, y = y}
    test.soakCounts[key] = test.soakCounts[key] + 1
    writePopulationTrace("couple_test_soak", {
        scenario_id = test.id,
        relation_id = test.relationId,
        relation_epoch = test.relationEpoch,
        owner_id = getPopulationClientId(test.owner),
        phase = key,
        sample = test.soakCounts[key],
        pair_distance = pairDistance,
        leader_move_state = leader.moveState,
        follower_move_state = follower.moveState,
        observer_follower_move_state = observerMoveState,
        leader_speed = leader.speed,
        follower_speed = follower.speed,
        distance_travelled = test.soakDistanceTravelled[key],
    })
    return true
end

addEvent("pedTraffic:coupleTestSampleResult", true)
addEventHandler("pedTraffic:coupleTestSampleResult", resourceRoot, function(testId, sampleId, phase, data)
    local test = coupleTest
    local pending = test and test.pendingSample or false
    if not test or test.id ~= testId or not pending or pending.id ~= sampleId or pending.phase ~= phase or
        (client ~= test.players[1] and client ~= test.players[2]) or pending.samples[client] then
        return
    end
    pending.samples[client] = data
    writePopulationTrace("couple_test_sample", {
        scenario_id = test.id,
        sample_id = sampleId,
        phase = phase,
        relation_id = test.relationId,
        relation_epoch = test.relationEpoch,
        player_id = getPopulationClientId(client),
        owner = client == test.owner,
        data = data,
    })
    if not pending.samples[test.players[1]] or not pending.samples[test.players[2]] then return end
    test.pendingSample = nil

    local allValid = true
    local firstReason = false
    for _, player in ipairs(test.players) do
        local valid, reason = validateCoupleTestSample(test, player, pending.samples[player], player == test.owner)
        if not valid then
            allValid = false
            firstReason = firstReason or reason
        end
    end
    if not allValid then
        test.sampleRetries = (test.sampleRetries or 0) + 1
        if (firstReason == "native-subtasks-not-ready" or firstReason == "observer-presentation-missing" or
            firstReason == "observer-presentation-not-ready") and test.sampleRetries <= 20 then
            scheduleCoupleTestSamples(test, phase, 250)
            return
        end
        return finishCoupleTest("FAIL", firstReason or "sample-mismatch")
    end
    test.sampleRetries = 0

    if phase == "soak-initial" or phase == "soak-handoff" then
        local observer = test.owner == test.players[1] and test.players[2] or test.players[1]
        local key = phase == "soak-initial" and "initial" or "handoff"
        local observerData = pending.samples[observer]
        if test.soakCounts[key] == 0 and observerData.presentationActive ~= true then
            test.presentationWarmupRetries[key] = test.presentationWarmupRetries[key] + 1
            if test.presentationWarmupRetries[key] <= 40 then
                scheduleCoupleTestSamples(test, phase, 250)
                return
            end
            return finishCoupleTest("FAIL", "couple-observer-presentation-convergence-" .. key)
        end
        local recorded, recordReason = recordCoupleSoakSample(test, phase, pending.samples[test.owner], observerData)
        if not recorded then return finishCoupleTest("FAIL", recordReason) end
        if test.soakCounts[key] < 8 then
            scheduleCoupleTestSamples(test, phase, 1000)
        elseif test.observerFastGaitSamples[key] > 1 then
            return finishCoupleTest("FAIL", "couple-follower-fast-gait-" .. key)
        elseif test.observerPresentationEligibleSamples[key] < 3 then
            return finishCoupleTest("FAIL", "couple-observer-presentation-insufficient-eligible-" .. key)
        elseif test.observerPresentationEligibleActiveSamples[key] ~= test.observerPresentationEligibleSamples[key] then
            return finishCoupleTest("FAIL", "couple-observer-presentation-inactive-eligible-" .. key)
        elseif test.observerPresentationFarActiveSamples[key] > 0 then
            return finishCoupleTest("FAIL", "couple-observer-presentation-active-far-" .. key)
        elseif test.soakDistanceTravelled[key] < 3 then
            return finishCoupleTest("FAIL", "couple-locomotion-distance-" .. key)
        elseif test.followerSprintSamples > 1 then
            return finishCoupleTest("FAIL", "couple-owner-follower-sprint")
        elseif phase == "soak-initial" then
            test.initialActive = true
            test.phase = "revoke-owner-a"
            triggerClientEvent(root, "pedTraffic:revokeCouple", resourceRoot, test.relationId, test.relationEpoch,
                               "couple-test-handoff")
        else
            test.handoffActive = true
            test.phase = "forwarding-social-event"
            triggerClientEvent(test.ownerB, "pedTraffic:coupleTestForwardSocialEvent", resourceRoot, test.id,
                               test.relationId, test.relationEpoch, test.peds[1])
        end
    end
end)

addEvent("pedTraffic:coupleTestForwardSocialEventResult", true)
addEventHandler("pedTraffic:coupleTestForwardSocialEventResult", resourceRoot,
    function(testId, relationId, relationEpoch, forwarded, data)
        local test = coupleTest
        if not test or test.id ~= testId or relationId ~= test.relationId or relationEpoch ~= test.relationEpoch or
            client ~= test.ownerB or test.phase ~= "forwarding-social-event" or type(data) ~= "table" then
            return
        end
        writePopulationTrace("couple_test_forwarding", {
            scenario_id = test.id,
            relation_id = relationId,
            relation_epoch = relationEpoch,
            owner_id = getPopulationClientId(client),
            event_type = tonumber(data.eventType) or -1,
            forwarded = forwarded == true,
            data = data,
        })
        if isElement(test.damageAttacker) then destroyElement(test.damageAttacker) end
        test.damageAttacker = nil
        if forwarded ~= true then return finishCoupleTest("FAIL", "couple-social-event-not-forwarded") end
        test.socialEventForwarded = true
        test.phase = "await-forward-dissolution"
        setTimer(function()
            if coupleTest == test and test.phase == "await-forward-dissolution" then
                test.phase = "separating"
                triggerClientEvent(test.ownerB, "pedTraffic:coupleTestSeparate", resourceRoot, test.id, test.relationId,
                                   test.relationEpoch, test.peds[1], test.peds[2])
            end
        end, 500, 1)
    end)

addEvent("pedTraffic:coupleTestSeparated", true)
addEventHandler("pedTraffic:coupleTestSeparated", resourceRoot, function(testId, relationId, relationEpoch, moved)
    local test = coupleTest
    if not test or test.id ~= testId or relationId ~= test.relationId or relationEpoch ~= test.relationEpoch or
        client ~= test.ownerB or test.phase ~= "separating" then
        return
    end
    if moved ~= true then return finishCoupleTest("FAIL", "separation-stimulus-refused") end
    test.phase = "await-dissolution"
    writePopulationTrace("couple_test_separation", {
        scenario_id = test.id,
        relation_id = test.relationId,
        relation_epoch = test.relationEpoch,
        distance = 10.25,
    })
end)

addEvent("pedTraffic:coupleTestCleanupResult", true)
addEventHandler("pedTraffic:coupleTestCleanupResult", resourceRoot, function(testId, relationId, data)
    local test = coupleTest
    if not test or test.id ~= testId or relationId ~= test.relationId or test.phase ~= "cleanup" or
        (client ~= test.players[1] and client ~= test.players[2]) or test.cleanupAcks[client] or type(data) ~= "table" then
        return
    end
    if data.elementPresent or data.assignmentPresent or data.profilePresent then
        return finishCoupleTest("FAIL", "couple-cleanup-residual-client-" .. tostring(getPopulationClientId(client)))
    end
    test.cleanupAcks[client] = true
    test.cleanupAckCount = test.cleanupAckCount + 1
    writePopulationTrace("couple_test_cleanup_ack", {
        scenario_id = test.id,
        relation_id = test.relationId,
        player_id = getPopulationClientId(client),
        accepted = true,
    })
    if test.cleanupAckCount == 2 then
        finishCoupleTest("PASS", "couple-presentation-soak-handoff-social-dissolution-cleanup")
    end
end)

local function startCoupleTest(player)
    if coupleTest or dealerTest or copTest then
        return outputChatBox("Another population harness is already running", player, 255, 160, 80)
    end
    local players = getElementsByType("player")
    if #players ~= 2 then return outputChatBox("The couple test requires exactly two connected clients", player, 255, 160, 80) end
    for _, candidate in ipairs(players) do
        if isPedDead(candidate) or isPedInVehicle(candidate) then
            return outputChatBox("Both couple-test clients must be alive and on foot", player, 255, 160, 80)
        end
    end
    if not debugEnabled then
        debugEnabled = true
        triggerClientEvent(root, "pedTraffic:setDebug", resourceRoot, true)
    end
    resetPopulationTrace()
    clearTraffic("couple-test-reset")
    if not enabled then setEnabled(true, player) end

    nextCoupleTestId = nextCoupleTestId + 1
    nextCoupleRelationId = nextCoupleRelationId + 1
    local hour, minute = getTime()
    local test = {
        id = nextCoupleTestId,
        relationId = nextCoupleRelationId,
        relationEpoch = 1,
        players = players,
        ownerA = players[1],
        ownerB = players[2],
        owner = players[1],
        peds = {},
        trafficIds = {},
        savedPlayers = {},
        savedTime = {hour, minute},
        startedAt = getTickCount(),
        phase = "creating",
        sampleSequence = 0,
        cleanupAcks = {},
        cleanupAckCount = 0,
        soakCounts = {initial = 0, handoff = 0},
        soakDistanceTravelled = {initial = 0, handoff = 0},
        lastSoakLeaderPosition = {},
        maxPairDistance = 0,
        followerSprintSamples = 0,
        observerFastGaitSamples = {initial = 0, handoff = 0},
        observerPresentationActiveSamples = {initial = 0, handoff = 0},
        observerPresentationEligibleSamples = {initial = 0, handoff = 0},
        observerPresentationEligibleActiveSamples = {initial = 0, handoff = 0},
        observerPresentationFarActiveSamples = {initial = 0, handoff = 0},
        presentationWarmupRetries = {initial = 0, handoff = 0},
    }
    coupleTest = test
    setTime(12, 0)
    for index, candidate in ipairs(players) do
        local x, y, z = getElementPosition(candidate)
        local rx, ry, rz = getElementRotation(candidate)
        test.savedPlayers[candidate] = {x = x, y = y, z = z, rx = rx, ry = ry, rz = rz, dimension = getElementDimension(candidate),
                                        interior = getElementInterior(candidate), frozen = isElementFrozen(candidate)}
        setElementDimension(candidate, 0)
        setElementInterior(candidate, 0)
        -- A nearby frozen player is still a real ped to GTA's wander scanner.
        -- The former four-metre viewing position made the leader enter
        -- AvoidOtherPedWhileWandering and abort the retail relation before the
        -- locomotion soak had even started. Keep the actors physically remote
        -- and give both clients a non-physical camera view of the fixture.
        setElementPosition(candidate, COUPLE_TEST_POSITION.x - 20 + (index - 1) * 2, COUPLE_TEST_POSITION.y - 20,
                           COUPLE_TEST_POSITION.z)
        setElementFrozen(candidate, true)
        setCameraMatrix(candidate, COUPLE_TEST_POSITION.x, COUPLE_TEST_POSITION.y - 8, COUPLE_TEST_POSITION.z + 5,
                        COUPLE_TEST_POSITION.x, COUPLE_TEST_POSITION.y, COUPLE_TEST_POSITION.z + 0.8)
    end

    -- MALE01/SENSIBLE_GUY and VBFYCRP/SUIT_GIRL pass the retail declared-sex,
    -- pedstat and non-skater filters. BFORI/COWARD was useful visually but is
    -- rejected by ArePedStatsCompatible and could never be an automatic pair.
    local models = {7, 11}
    for index = 1, 2 do
        local ped = createPed(models[index], COUPLE_TEST_POSITION.x + (index - 1), COUPLE_TEST_POSITION.y,
                              COUPLE_TEST_POSITION.z, 0)
        if not ped then
            destroyCoupleTestPeds(test)
            return finishCoupleTest("FAIL", "couple-create-ped-" .. tostring(index))
        end
        nextPedId = nextPedId + 1
        test.peds[index] = ped
        test.trafficIds[index] = nextPedId
        setElementDimension(ped, 0)
        setElementInterior(ped, 0)
        setElementData(ped, "neon:ambientPedPopulationClass", "civilian")
        setElementData(ped, "neon:ambientPedLogicalType", index == 1 and 4 or 5)
        setElementData(ped, "neon:ambientPedTrafficId", nextPedId)
        setElementData(ped, "neon:ambientPedCatalogRevision", populationCatalog.revision)
        setElementData(ped, "neon:ambientPedRelationId", test.relationId)
        setElementData(ped, "neon:ambientPedRelationEpoch", test.relationEpoch)
        setElementData(ped, "neon:ambientPedRelationRole", index == 1 and "a" or "b")
        setElementData(ped, "neon:ambientPedTraffic", true)
        if not setPedFightingStyle(ped, 4) or not setPedUseNativeWalkingStyle(ped, true) or
            not setElementSyncer(ped, test.ownerA, true) then
            destroyCoupleTestPeds(test)
            return finishCoupleTest("FAIL", "couple-initialization-refused-" .. tostring(index))
        end
    end

    test.phase = "await-owner-a"
    writePopulationTrace("couple_test_started", {
        scenario_id = test.id,
        relation_id = test.relationId,
        relation_epoch = test.relationEpoch,
        traffic_ids = test.trafficIds,
        models = models,
        owner_a = getPopulationClientId(test.ownerA),
        owner_b = getPopulationClientId(test.ownerB),
    })
    triggerClientEvent(test.ownerA, "pedTraffic:assignCouple", resourceRoot, test.relationId, test.relationEpoch,
                       test.peds[1], test.peds[2], false, "couple-test-initial")
    triggerClientEvent(test.ownerB, "pedTraffic:assignCouplePresentation", resourceRoot, test.relationId,
                       test.relationEpoch, test.peds[1], test.peds[2], "couple-test-initial-observer", false, false)
    outputChatBox("Couple test started: two walking soak phases, handoff and strict 10m dissolution", root, 120, 220, 255)
end

setTimer(function()
    local test = coupleTest
    if not test then return end
    if getTickCount() - test.startedAt > 45000 then finishCoupleTest("FAIL", "couple-test-timeout-" .. test.phase) end
end, 200, 0)

setTimer(function()
    if not debugEnabled then
        return
    end
    for _, player in ipairs(getEligiblePlayers()) do
        local totalTarget, civilianTarget, dealerTarget, copTarget, gangTarget = getNativeTargetsNearPlayer(player)
        if totalTarget then
            local stockCountedLive, physicalLive, civilianCount, dealerCount, copCount, gangCounts = getPopulationCountsNearPlayer(player)
            local totalGangCount = 0
            for index = 1, 10 do totalGangCount = totalGangCount + gangCounts[index] end
            local profile = populationProfiles[player]
            local radii = getPopulationRadii(profile)
            writePopulationTrace("population_snapshot", {
                player_id = getPopulationClientId(player),
                demo = {
                    enabled = pedTrafficDemoDensity.enabled,
                    target = pedTrafficDemoDensity.target,
                    anchor = isElement(pedTrafficDemoDensity.anchor) and getPopulationClientId(pedTrafficDemoDensity.anchor) or false,
                },
                targets = {total = totalTarget, civilian = civilianTarget, dealer = dealerTarget, cop = copTarget, gang = gangTarget},
                live = {
                    total = stockCountedLive,
                    physical = physicalLive,
                    civilian = civilianCount,
                    dealer = dealerCount,
                    cop = copCount,
                    gang = totalGangCount,
                    gang_families = gangCounts,
                },
                deficits = {
                    total = totalTarget - stockCountedLive,
                    civilian = civilianTarget - civilianCount,
                    dealer = dealerTarget - dealerCount,
                    cop = copTarget - copCount,
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
        local activeCivilians, activeDealers, activeCops, activeGangs = getActivePopulationSummary()
        local activeGroups = getTrafficGroupCount()
        log(("telemetry active=%d activeCiv=%d activeDealers=%d activeCops=%d activeGangs=%s groups=%d groupSpawns=%d groupHandoffs=%d groupPromotions=%d groupRemovals=%d ready=%d preset=%s revision=%d requests=%d misses=%d rejected=%d spawned=%d despawned=%d handoffs=%d selections=%s selectedGangs=%s models=%s missReasons=%s rejectionReasons=%s"):format(
                getTrafficPedCount(), activeCivilians, activeDealers, activeCops, formatNumericMap(activeGangs), activeGroups, stats.groupSpawns,
                stats.groupHandoffs, stats.groupPromotions, stats.groupRemovals,
                #getEligiblePlayers(), populationWorld.preset, populationWorld.revision, stats.requests, stats.candidateMisses,
                stats.rejected, stats.spawned, stats.despawned, stats.handoffs,
                formatNumericMap(stats.populationSelections), formatNumericMap(stats.gangSelections), formatNumericMap(stats.spawnedModels),
                formatReasons(stats.missReasons), formatReasons(stats.rejectionReasons)))
    end
end, 10000, 0)

local DEALER_TEST_PHASE_TIMEOUT = 60000
local DEALER_TEST_CANDIDATE_RETRY = 750
-- This Grove Street fixture produced the expected post_home_coming profile
-- (`dealerTarget > 0.03`) on both clients in the runtime logs. The former City
-- Hall point never produced a native profile after a scripted teleport, so it
-- could only exercise the harness timeout rather than dealer population.
local DEALER_TEST_POSITION = {x = 2484, y = -1668, z = 13.35}

local function traceDealerTestAction(test, action)
    test.action = action
    test.actionStartedAt = getTickCount()
    writePopulationTrace("dealer_test_action", {
        scenario_id = test.id,
        action = action,
        scenario_tick = test.actionStartedAt - test.startedAt,
        traffic_id = test.record and test.record.id or false,
        owner_id = test.record and isElement(test.record.owner) and getPopulationClientId(test.record.owner) or false,
        epoch = test.record and test.record.epoch or false,
    })
end

local function restoreDealerTestPlayers(test)
    local hour, minute = unpack(test.savedTime)
    setTime(hour, minute)
    for _, player in ipairs(test.players) do
        if isElement(player) then
            local saved = test.savedPlayers[player]
            if saved then
                setElementDimension(player, saved.dimension)
                setElementInterior(player, saved.interior)
                setElementPosition(player, saved.x, saved.y, saved.z)
                setElementRotation(player, saved.rx, saved.ry, saved.rz)
                setElementHealth(player, saved.health)
                setPedArmor(player, saved.armor)
                setElementFrozen(player, saved.frozen)
            end
        end
    end
end

stopDealerTest = function(outcome, reason)
    local test = dealerTest
    dealerTest = false
    if not test then
        return
    end
    if isTimer(test.timer) then
        killTimer(test.timer)
    end
    if test.record and not test.record.removing then
        removeRecord(test.record, "dealer-test-" .. tostring(outcome):lower())
    end
    if test.zoneLabel and test.originalDealerStrength ~= nil then
        local restored = populationWorld:setDealerStrength(test.zoneLabel, test.originalDealerStrength)
        if restored and not restored.unchanged then
            publishPopulationWorldMutation("dealer-test-stop-restore", restored)
        end
    end
    restoreDealerTestPlayers(test)
    writePopulationTrace("dealer_test_result", {
        scenario_id = test.id,
        result = outcome,
        reason = tostring(reason),
        action = test.action,
        scenario_tick = getTickCount() - test.startedAt,
        traffic_id = test.record and test.record.id or false,
        initial_epoch = test.initialEpoch or false,
        final_epoch = test.record and test.record.epoch or false,
        cleanup_acks = test.cleanupAckCount or 0,
    })
    local message = ("Dealer test %s: %s (scenario %d)"):format(outcome, tostring(reason), test.id)
    outputChatBox(message, root, outcome == "PASS" and 80 or 255, outcome == "PASS" and 220 or 80, 120)
    log(message, true)
end

failDealerTest = function(reason)
    stopDealerTest("FAIL", reason)
end

local function issueDealerTestCandidate(test)
    if next(pendingRequests) or next(pendingVisibilityChecks) then
        return false
    end
    local player = test.players[1]
    local profile = populationProfiles[player]
    local totalTarget, civilianTarget, dealerTarget, _, gangTarget, gangTargets = getNativeTargetsNearPlayer(player)
    if not profile or totalTarget == false or dealerTarget <= 0.03 then
        return false
    end
    local stockCountedLive, physicalLive, civilianCount, dealerCount, _, gangCounts = getPopulationCountsNearPlayer(player)
    local totalGangCount = 0
    for index = 1, 10 do totalGangCount = totalGangCount + gangCounts[index] end
    if stockCountedLive >= totalTarget or dealerCount >= dealerTarget then
        return failDealerTest("dealer-deficit-unavailable")
    end

    nextRequestId = nextRequestId + 1
    local selection = {
        requestId = nextRequestId,
        populationClass = "dealer",
        gang = false,
        maximumGroupMembers = 1,
        profileSignature = profile.signature,
        totalTarget = totalTarget,
        civilianTarget = civilianTarget,
        dealerTarget = dealerTarget,
        gangTarget = gangTarget,
        gangTargets = gangTargets,
        totalCount = stockCountedLive,
        physicalCount = physicalLive,
        civilianCount = civilianCount,
        dealerCount = dealerCount,
        gangCounts = gangCounts,
        totalGangCount = totalGangCount,
        civilianDeficit = civilianTarget - civilianCount,
        dealerDeficit = dealerTarget - dealerCount,
        gangDeficit = gangTarget - totalGangCount,
        civilianChance = math.min(100, math.max(0, civilianTarget - civilianCount) * 100),
        dealerChance = math.min(100, math.max(0, dealerTarget - dealerCount) * 100),
        gangChance = math.min(100, math.max(0, gangTarget - totalGangCount) * 100),
    }
    pendingRequests[player] = {
        id = nextRequestId,
        issuedAt = getTickCount(),
        worldRevision = populationWorld.revision,
        selection = selection,
    }
    test.lastRequestAt = getTickCount()
    stats.requests = stats.requests + 1
    stats.populationSelections.dealer = stats.populationSelections.dealer + 1
    writePopulationTrace("candidate_request", {
        request_id = nextRequestId,
        player_id = getPopulationClientId(player),
        population_class = "dealer",
        gang = false,
        maximum_group_members = 1,
        scenario_id = test.id,
        targets = {total = totalTarget, civilian = civilianTarget, dealer = dealerTarget, gang = gangTarget},
        live = {total = stockCountedLive, physical = physicalLive, civilian = civilianCount, dealer = dealerCount, gang = totalGangCount},
        deficits = {
            civilian = selection.civilianDeficit,
            dealer = selection.dealerDeficit,
            gang = selection.gangDeficit,
        },
    })
    triggerClientEvent(player, "pedTraffic:candidateRequest", resourceRoot, nextRequestId, populationWorld.revision, "dealer", false, 1, false)
    return true
end

local function requestDealerTestSamples(test, phase)
    test.samples[phase] = {}
    for _, player in ipairs(test.players) do
        triggerClientEvent(player, "pedTraffic:dealerTestSample", resourceRoot, test.id, test.record.ped, test.record.epoch, phase)
    end
end

local function validateDealerTestSamples(test, phase)
    local samples = test.samples[phase]
    if not samples or not samples[test.ownerA] or not samples[test.ownerB] then
        return false
    end
    local expectedOwner = phase == "handoff" and test.ownerB or test.ownerA
    for _, player in ipairs(test.players) do
        local sample = samples[player]
        local expectedArmed = phase ~= "initial"
        local expectedWeapon = expectedArmed and test.record.weapon or 0
        -- The server-side commit proves that the retail grant requested 50
        -- rounds. Once the pistol is active, the owner can legitimately spend
        -- rounds before this asynchronous sample arrives, while an observer
        -- can still expose the last replicated total. Validate that bounded
        -- runtime state instead of requiring both peers to remain at exactly
        -- the pre-fight total. Melee slots canonically expose one through the
        -- public getter; unarmed exposes the same synthetic value and carries
        -- no ammo state worth validating.
        local ammoValid = expectedWeapon == 0 or expectedWeapon == 4 and sample.weaponAmmo == 1 or
            expectedWeapon == 22 and type(sample.weaponAmmo) == "number" and sample.weaponAmmo >= 1 and
                sample.weaponAmmo <= config.nativeDealerFightWeaponAmmo
        if sample.model ~= getElementModel(test.record.ped) or dealerModels[sample.model] ~= true or
            sample.populationClass ~= "dealer" or sample.logicalPedType ~= 17 or sample.weapon ~= expectedWeapon or
            sample.epoch ~= test.record.epoch or sample.catalogRevision ~= populationCatalog.revision or
            sample.dealerFightArmed ~= expectedArmed or
            (expectedArmed and (sample.dealerHasKnife ~= test.record.dealerHasKnife or
                sample.dealerHasPistol ~= test.record.dealerHasPistol or
                not ammoValid or
                (test.record.dealerHasPistol and sample.pistolSkillStat ~= 40))) then
            failDealerTest(("invalid-sample:%s:client=%d"):format(phase, getPopulationClientId(player)))
            return false
        end
        local isOwner = player == expectedOwner
        if sample.syncer ~= isOwner or sample.assignment ~= isOwner or sample.assignmentAccepted ~= isOwner or
            sample.profilePresent ~= true or sample.profileActive ~= isOwner or
            (phase == "initial" and isOwner and sample.hasWander ~= true) or
            ((phase == "combat" or phase == "handoff") and isOwner and sample.hasFight ~= true) then
            failDealerTest(("authority-sample:%s:client=%d"):format(phase, getPopulationClientId(player)))
            return false
        end
    end
    return true
end

local function beginDealerTestCleanup(test)
    traceDealerTestAction(test, "cleanup")
    test.cleanupAcks = {}
    test.cleanupAckCount = 0
    local trafficId = test.record.id
    removeRecord(test.record, "dealer-test-cleanup")
    setTimer(function()
        if dealerTest ~= test then
            return
        end
        for _, player in ipairs(test.players) do
            if isElement(player) then
                triggerClientEvent(player, "pedTraffic:dealerTestCleanup", resourceRoot, test.id, trafficId)
            end
        end
    end, 500, 1)
end

local function pulseDealerTest(testId)
    local test = dealerTest
    if not test or test.id ~= testId then
        return
    end
    for _, player in ipairs(test.players) do
        if not isElement(player) then
            return failDealerTest("client-left")
        end
    end
    if test.unexpectedSuspensionReason then
        return failDealerTest("unexpected-suspension:" .. test.unexpectedSuspensionReason)
    end
    if getTickCount() - test.actionStartedAt > DEALER_TEST_PHASE_TIMEOUT then
        return failDealerTest("phase-timeout:" .. test.action)
    end

    if test.action == "await-profile" then
        local first = populationProfiles[test.players[1]]
        local second = populationProfiles[test.players[2]]
        if not isPopulationWorldReady(test.players[1]) or not isPopulationWorldReady(test.players[2]) or
            not first or not second or first.zoneLabel ~= second.zoneLabel then
            return
        end
        test.zoneLabel = first.zoneLabel
        test.originalDealerStrength = populationWorld.zones[test.zoneLabel].dealerStrength
        local fixture = populationWorld:setDealerStrength(test.zoneLabel, 6)
        writePopulationTrace("dealer_test_strength_fixture", {
            scenario_id = test.id,
            zone_label = test.zoneLabel,
            change = fixture,
            revision = populationWorld.revision,
        })
        if fixture and not fixture.unchanged then
            publishPopulationWorldMutation("dealer-test-fixture", fixture)
        end
        traceDealerTestAction(test, "await-fixture")
    elseif test.action == "await-fixture" then
        for _, player in ipairs(test.players) do
            local profile = populationProfiles[player]
            if not isPopulationWorldReady(player) or not profile or profile.worldRevision ~= populationWorld.revision or
                profile.zoneLabel ~= test.zoneLabel or profile.dealerStrength ~= 6 or profile.dealerTarget <= 0.03 then
                return
            end
        end
        traceDealerTestAction(test, "await-spawn")
    elseif test.action == "await-spawn" then
        for _, record in pairs(trafficPeds) do
            if not record.removing and record.populationClass == "dealer" and record.createdAt >= test.startedAt then
                test.record = record
                break
            end
        end
        if test.record then
            if test.record.state ~= "active" or not isElement(test.record.owner) then
                return
            end
            -- A freshly teleported frozen player does not advance GTA's
            -- popcycle state, so the native profile can remain unavailable
            -- indefinitely. Keep both clients unfrozen until the dealer has
            -- been proposed and activated, then stabilize them for the
            -- authority samples and explicit handoff.
            for _, player in ipairs(test.players) do
                setElementFrozen(player, true)
            end
            test.ownerA = test.record.owner
            test.ownerB = test.players[1] == test.ownerA and test.players[2] or test.players[1]
            test.initialEpoch = test.record.epoch
            traceDealerTestAction(test, "sample-initial")
            requestDealerTestSamples(test, "initial")
        elseif not test.lastRequestAt or getTickCount() - test.lastRequestAt >= DEALER_TEST_CANDIDATE_RETRY then
            issueDealerTestCandidate(test)
        end
    elseif test.action == "sample-initial" then
        if validateDealerTestSamples(test, "initial") then
            local x, y, z = getElementPosition(test.record.ped)
            setElementPosition(test.ownerA, x + 1.25, y, z + 0.2)
            setElementRotation(test.ownerA, 0, 0, 90)
            test.combatAttempts = 0
            test.nextCombatAttemptAt = 0
            traceDealerTestAction(test, "await-combat")
        end
    elseif test.action == "await-combat" then
        if test.record.dealerFightArmed then
            test.combatStableAt = test.combatStableAt or getTickCount() + 750
            if getTickCount() >= test.combatStableAt then
                traceDealerTestAction(test, "sample-combat")
                requestDealerTestSamples(test, "combat")
            end
        elseif getTickCount() >= test.nextCombatAttemptAt and test.record.state == "active" and test.record.owner == test.ownerA then
            test.combatAttempts = test.combatAttempts + 1
            test.nextCombatAttemptAt = getTickCount() + 1500
            writePopulationTrace("dealer_test_combat_stimulus", {
                scenario_id = test.id,
                attempt = test.combatAttempts,
                traffic_id = test.record.id,
                epoch = test.record.epoch,
                owner_id = getPopulationClientId(test.ownerA),
            })
            rememberDealerCombatContext(test.record, test.ownerA, 0, 3, "dealer-test-stimulus")
            -- Dealer combat has its own single-actor context lifecycle. Pass an
            -- explicit untracked identity so the group-only replay guard does
            -- not reject this deterministic harness stimulus.
            triggerClientEvent(test.ownerA, "pedTraffic:damageResponse", resourceRoot, test.record.ped, test.ownerA, 0, 3, false)
        end
    elseif test.action == "sample-combat" then
        if validateDealerTestSamples(test, "combat") then
            traceDealerTestAction(test, "handoff")
            beginHandoff(test.record, test.ownerB, "dealer-test-handoff")
        end
    elseif test.action == "handoff" then
        if test.record.removing then
            return failDealerTest("dealer-removed-during-handoff")
        end
        if test.record.state == "active" and test.record.owner == test.ownerB and test.record.epoch == test.initialEpoch + 1 then
            test.handoffStableAt = test.handoffStableAt or getTickCount() + 750
            if getTickCount() >= test.handoffStableAt then
                traceDealerTestAction(test, "sample-handoff")
                requestDealerTestSamples(test, "handoff")
            end
        end
    elseif test.action == "sample-handoff" then
        if validateDealerTestSamples(test, "handoff") then
            local changes, rolls = populationWorld:advanceDealerStrengths(math.random)
            writePopulationTrace("dealer_test_growth_roll", {
                scenario_id = test.id,
                roll_count = #rolls,
                changes = changes,
                revision = populationWorld.revision,
            })
            if #changes > 0 then
                publishPopulationWorldMutation("dealer-test-minute-growth", changes)
            end
            test.growthRevision = populationWorld.revision
            traceDealerTestAction(test, "await-growth")
        end
    elseif test.action == "await-growth" then
        for _, player in ipairs(test.players) do
            local profile = populationProfiles[player]
            if not isPopulationWorldReady(player) or not profile or profile.worldRevision ~= test.growthRevision then
                return
            end
        end
        if test.record.state ~= "active" or test.record.owner ~= test.ownerB then
            return
        end
        test.deathBefore = populationWorld.zones[test.zoneLabel].dealerStrength
        if test.deathBefore <= 0 or not killPed(test.record.ped, test.ownerA, 22, 3, false) then
            return failDealerTest("dealer-test-kill-refused")
        end
        test.deathAfter = test.deathBefore - 1
        writePopulationTrace("dealer_test_kill_requested", {
            scenario_id = test.id,
            traffic_id = test.record.id,
            killer_id = getPopulationClientId(test.ownerA),
            before = test.deathBefore,
            expected_after = test.deathAfter,
        })
        traceDealerTestAction(test, "await-death-event")
    elseif test.action == "await-death-event" then
        if not test.deathEventObserved then
            return
        end
        if test.deathChange == false or populationWorld.zones[test.zoneLabel].dealerStrength ~= test.deathAfter then
            return failDealerTest("dealer-strength-decrement-mismatch")
        end
        test.deathRevision = populationWorld.revision
        traceDealerTestAction(test, "await-death-revision")
    elseif test.action == "await-death-revision" then
        if populationWorld.zones[test.zoneLabel].dealerStrength ~= test.deathAfter then
            return failDealerTest("dealer-strength-decrement-mismatch")
        end
        for _, player in ipairs(test.players) do
            local profile = populationProfiles[player]
            if not isPopulationWorldReady(player) or not profile or profile.worldRevision ~= test.deathRevision or
                profile.dealerStrength ~= test.deathAfter then
                return
            end
        end
        local restored = populationWorld:setDealerStrength(test.zoneLabel, test.originalDealerStrength)
        if not restored then
            return failDealerTest("dealer-strength-restore-refused")
        end
        if not restored.unchanged then
            publishPopulationWorldMutation("dealer-test-restore", restored)
        end
        test.restoreRevision = populationWorld.revision
        traceDealerTestAction(test, "await-restore")
    elseif test.action == "await-restore" then
        for _, player in ipairs(test.players) do
            local profile = populationProfiles[player]
            if not isPopulationWorldReady(player) or not profile or profile.worldRevision ~= test.restoreRevision or
                profile.dealerStrength ~= test.originalDealerStrength then
                return
            end
        end
        beginDealerTestCleanup(test)
    elseif test.action == "cleanup" and test.cleanupAckCount == #test.players then
        stopDealerTest("PASS", "dealer-catalog-combat-ecology-handoff-cleanup")
    end
end

local function startDealerTest(player)
    if dealerTest then
        outputChatBox("A dealer test is already running", player, 255, 160, 80)
        return false
    end
    local players = getElementsByType("player")
    if #players ~= 2 then
        outputChatBox("The dealer test requires exactly two connected clients", player, 255, 160, 80)
        return false
    end
    table.sort(players, function(left, right) return getPopulationClientId(left) < getPopulationClientId(right) end)
    for _, candidate in ipairs(players) do
        if isPedDead(candidate) or isPedInVehicle(candidate) then
            outputChatBox("Both dealer-test clients must be alive and on foot", player, 255, 160, 80)
            return false
        end
    end

    if not debugEnabled then
        debugEnabled = true
        triggerClientEvent(root, "pedTraffic:setDebug", resourceRoot, true)
    end
    resetPopulationTrace()
    clearTraffic("dealer-test-reset")
    if not enabled then
        setEnabled(true, player)
    end
    nextDealerTestId = nextDealerTestId + 1
    local hour, minute = getTime()
    local test = {
        id = nextDealerTestId,
        players = players,
        savedPlayers = {},
        savedTime = {hour, minute},
        startedAt = getTickCount(),
        actionStartedAt = getTickCount(),
        action = "await-profile",
        samples = {},
    }
    dealerTest = test
    setTime(12, 0)
    for index, candidate in ipairs(players) do
        local x, y, z = getElementPosition(candidate)
        local rx, ry, rz = getElementRotation(candidate)
        test.savedPlayers[candidate] = {
            x = x, y = y, z = z, rx = rx, ry = ry, rz = rz,
            dimension = getElementDimension(candidate), interior = getElementInterior(candidate), frozen = isElementFrozen(candidate),
            health = getElementHealth(candidate), armor = getPedArmor(candidate),
        }
        setElementDimension(candidate, 0)
        setElementInterior(candidate, 0)
        setElementPosition(candidate, DEALER_TEST_POSITION.x + (index - 1) * 4, DEALER_TEST_POSITION.y, DEALER_TEST_POSITION.z)
        setElementRotation(candidate, 0, 0, 0)
        setElementHealth(candidate, 100)
        setPedArmor(candidate, 100)
        -- Require a report captured after this teleport instead of accepting
        -- a still-fresh profile from the player's previous zone.
        populationProfiles[candidate] = nil
    end
    writePopulationTrace("dealer_test_started", {
        scenario_id = test.id,
        client_ids = {getPopulationClientId(players[1]), getPopulationClientId(players[2])},
        position = DEALER_TEST_POSITION,
    })
    -- MTA clones table arguments passed to setTimer, so passing `test` would
    -- make the identity guard reject every pulse. The scalar scenario ID keeps
    -- the timer bound to this run without retaining a cloned state snapshot.
    test.timer = setTimer(pulseDealerTest, 100, 0, test.id)
    outputChatBox(("Dealer test %d started; both clients are isolated until automatic cleanup"):format(test.id), root, 120, 220, 255)
    return true
end

addEvent("pedTraffic:dealerTestSampleResult", true)
addEventHandler("pedTraffic:dealerTestSampleResult", resourceRoot, function(testId, phase, data)
    local test = dealerTest
    if not test or test.id ~= testId or (client ~= test.players[1] and client ~= test.players[2]) or
        (phase ~= "initial" and phase ~= "combat" and phase ~= "handoff") or type(data) ~= "table" or test.action ~= "sample-" .. phase then
        return
    end
    test.samples[phase][client] = {
        model = tonumber(data.model),
        populationClass = tostring(data.populationClass),
        logicalPedType = tonumber(data.logicalPedType),
        weapon = tonumber(data.weapon),
        weaponAmmo = tonumber(data.weaponAmmo),
        knifeAmmo = tonumber(data.knifeAmmo),
        pistolAmmo = tonumber(data.pistolAmmo),
        pistolSkillStat = tonumber(data.pistolSkillStat),
        dealerFightArmed = data.dealerFightArmed == true,
        dealerHasKnife = data.dealerHasKnife == true,
        dealerHasPistol = data.dealerHasPistol == true,
        catalogRevision = tostring(data.catalogRevision),
        epoch = tonumber(data.epoch),
        syncer = data.syncer == true,
        assignment = data.assignment == true,
        assignmentAccepted = data.assignmentAccepted == true,
        profilePresent = data.profilePresent == true,
        profileActive = data.profileActive == true,
        hasWander = data.hasWander == true,
        hasFight = data.hasFight == true,
    }
    writePopulationTrace("dealer_test_sample", {
        scenario_id = test.id,
        phase = phase,
        client_id = getPopulationClientId(client),
        traffic_id = test.record and test.record.id or false,
        owner_id = test.record and isElement(test.record.owner) and getPopulationClientId(test.record.owner) or false,
        epoch = tonumber(data.epoch),
        syncer = data.syncer == true,
        assignment = data.assignment == true,
        assignment_accepted = data.assignmentAccepted == true,
        profile_present = data.profilePresent == true,
        profile_active = data.profileActive == true,
        has_wander = data.hasWander == true,
        has_fight = data.hasFight == true,
        model = tonumber(data.model),
        population_class = tostring(data.populationClass),
        logical_ped_type = tonumber(data.logicalPedType),
        weapon = tonumber(data.weapon),
        weapon_ammo = tonumber(data.weaponAmmo),
        knife_ammo = tonumber(data.knifeAmmo),
        pistol_ammo = tonumber(data.pistolAmmo),
        pistol_skill_stat = tonumber(data.pistolSkillStat),
        dealer_fight_armed = data.dealerFightArmed == true,
        dealer_has_knife = data.dealerHasKnife == true,
        dealer_has_pistol = data.dealerHasPistol == true,
        catalog_revision = tostring(data.catalogRevision),
        tasks = data.tasks,
    })
end)

addEvent("pedTraffic:dealerTestCleanupResult", true)
addEventHandler("pedTraffic:dealerTestCleanupResult", resourceRoot, function(testId, trafficId, data)
    local test = dealerTest
    if not test or test.id ~= testId or test.action ~= "cleanup" or trafficId ~= test.record.id or
        (client ~= test.players[1] and client ~= test.players[2]) or type(data) ~= "table" or test.cleanupAcks[client] then
        return
    end
    local clean = data.elementPresent ~= true and data.assignmentPresent ~= true and data.profilePresent ~= true
    test.cleanupAcks[client] = clean
    test.cleanupAckCount = test.cleanupAckCount + 1
    writePopulationTrace("dealer_test_cleanup_ack", {
        scenario_id = test.id,
        client_id = getPopulationClientId(client),
        traffic_id = trafficId,
        accepted = clean,
        element_present = data.elementPresent == true,
        assignment_present = data.assignmentPresent == true,
        profile_present = data.profilePresent == true,
    })
    if not clean then
        failDealerTest("cleanup-leak:client=" .. tostring(getPopulationClientId(client)))
    end
end)

-- Retail advances dealer ecology only when the 60-second game-timer bucket
-- changes and territory wars are enabled. GTA's local update is suppressed by
-- Neon, so the server performs the same bounded rolls once and republishes the
-- resulting world revision to every client.
setTimer(function()
    if dealerTest then
        return
    end
    local currentMinute = math.floor(getTickCount() / config.dealerStrengthGrowthInterval)
    if currentMinute == lastDealerStrengthMinute then
        return
    end
    lastDealerStrengthMinute = currentMinute
    local changes, rolls = populationWorld:advanceDealerStrengths(math.random)
    writePopulationTrace("dealer_strength_growth_boundary", {
        minute = currentMinute,
        gang_wars_active = populationWorld.gangWarsActive,
        roll_count = #rolls,
        changes = changes,
        revision = populationWorld.revision,
    })
    if #changes > 0 then
        publishPopulationWorldMutation("minute-growth", changes)
    end
end, 1000, 0)

function pedTrafficSetDemo(requested, actor, coordinated)
    requested = requested == true
    if requested == pedTrafficDemoDensity.enabled then
        if requested and coordinated then return false, "ped-demo-already-active" end
        log(("demo-density enabled=%s target=%d anchor=%s"):format(
                tostring(pedTrafficDemoDensity.enabled), pedTrafficDemoDensity.target,
                isElement(pedTrafficDemoDensity.anchor) and getPlayerName(pedTrafficDemoDensity.anchor) or "none"), true)
        return true
    end

    if requested then
        if residencyTest or bikeJackTest or dealerTest or copTest or coupleTest then
            log("demo-density refused reason=population-harness-active", true)
            return false, "population-harness-active"
        end
        local eligiblePlayers = getEligiblePlayers()
        if coordinated and #eligiblePlayers ~= 2 then
            log(("demo-density refused reason=exactly-two-ready-clients-required players=%d"):format(#eligiblePlayers), true)
            return false, "exactly-two-ready-clients-required"
        end
        local anchor = isEligiblePlayer(actor) and actor or getEligiblePlayers()[1]
        if not isElement(anchor) then
            log("demo-density refused reason=no-eligible-anchor", true)
            return false, "no-eligible-anchor"
        end
        local availablePedSlots = config.pedPoolSoftLimit - (#getElementsByType("ped") - getTrafficPedCount())
        local requiredPedSlots = pedTrafficDemoDensity.target + (coordinated and 16 or 0)
        if config.globalCap < pedTrafficDemoDensity.target or availablePedSlots < requiredPedSlots then
            log(("demo-density refused reason=insufficient-capacity global=%d available-ped-slots=%d required=%d target=%d"):format(
                    config.globalCap, availablePedSlots, requiredPedSlots, pedTrafficDemoDensity.target), true)
            return false, "insufficient-capacity"
        end
        pedTrafficDemoDensity.previousEnabled = enabled
        pedTrafficDemoDensity.enabled = true
        pedTrafficDemoDensity.epoch = pedTrafficDemoDensity.epoch + 1
        pedTrafficDemoDensity.anchor = anchor
        pedTrafficDemoDensity.fallbackCursor = 0
        clearTraffic("demo-density-start")
        if not enabled then setEnabled(true, actor) end
    else
        local previousEnabled = pedTrafficDemoDensity.previousEnabled
        pedTrafficDemoDensity.enabled = false
        pedTrafficDemoDensity.epoch = pedTrafficDemoDensity.epoch + 1
        pedTrafficDemoDensity.anchor = false
        pedTrafficDemoDensity.previousEnabled = false
        clearTraffic("demo-density-stop")
        if not previousEnabled then setEnabled(false, actor) end
    end

    outputChatBox(("Traffic demo pedestrians: %s (target=%d)"):format(requested and "ON" or "OFF",
                                                                       pedTrafficDemoDensity.target), root, 120, 220, 255)
    log(("demo-density enabled=%s target=%d anchor=%s actor=%s"):format(
            tostring(pedTrafficDemoDensity.enabled), pedTrafficDemoDensity.target,
            isElement(pedTrafficDemoDensity.anchor) and getPlayerName(pedTrafficDemoDensity.anchor) or "none",
            isElement(actor) and getPlayerName(actor) or "console"), true)
    return true
end

addCommandHandler("pedtraffic", function(player, _, action, value)
    if isElement(player) and not hasObjectPermissionTo(player, "function.kickPlayer", false) then
        outputChatBox("Ped traffic controls are restricted to server staff", player, 255, 100, 80)
        return
    end

    action = tostring(action or "status"):lower()
    if action == "on" then
        setEnabled(true, player)
    elseif action == "off" then
        if pedTrafficDemoDensity.enabled then pedTrafficSetDemo(false, player) end
        setEnabled(false, player)
    elseif action == "demo" then
        local requested = tostring(value or "on"):lower()
        if requested == "on" or requested == "off" then
            pedTrafficSetDemo(requested == "on", player)
        else
            outputChatBox("Usage: /pedtraffic demo on|off", player, 255, 160, 80)
        end
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
        if pedTrafficDemoDensity.enabled then
            outputChatBox("Disable /trafficdemo before changing the ped cap", player, 255, 160, 80)
            return
        end
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
    elseif action == "bikejack" and isElement(player) then
        startBikeJackTest(player)
    elseif action == "dealertest" and isElement(player) then
        startDealerTest(player)
    elseif action == "coptest" and isElement(player) then
        startCopTest(player)
    elseif action == "coupletest" then
        -- Keep the long-running harness operable from a headless server console.
        -- Interactive players still select themselves exactly as before.
        if not isElement(player) then
            player = getElementsByType("player")[1]
        end
        if isElement(player) then
            startCoupleTest(player)
        else
            outputServerLog("[ped-traffic] coupletest requires at least one connected player")
        end
    else
        local activeCount = 0
        for ped in pairs(trafficPeds) do
            if isElement(ped) then activeCount = activeCount + 1 end
        end
        outputChatBox(("Ped traffic: enabled=%s active=%d cap=%d demo=%s target=%d anchor=%s preset=%s revision=%d requests=%d misses=%d rejected=%d handoffs=%d"):format(
                          tostring(enabled), activeCount, config.globalCap, tostring(pedTrafficDemoDensity.enabled),
                          pedTrafficDemoDensity.target,
                          isElement(pedTrafficDemoDensity.anchor) and getPlayerName(pedTrafficDemoDensity.anchor) or "none",
                          populationWorld.preset, populationWorld.revision, stats.requests, stats.candidateMisses, stats.rejected,
                          stats.handoffs),
                      player, 120, 220, 255)
    end
end)

addEventHandler("onResourceStart", resourceRoot, function()
    if config.autoStart then setEnabled(true, false) end
    outputServerLog(("[ped-traffic] V1 loaded enabled=%s cap=%d population preset=%s revision=%d"):format(
        tostring(enabled), config.globalCap, populationWorld.preset, populationWorld.revision))
end)

addEventHandler("onResourceStop", resourceRoot, function()
    if coupleTest then
        finishCoupleTest("CANCEL", "resource-stop")
    end
    if copTest then
        finishCopTest("CANCEL", "resource-stop")
    end
    if stopDealerTest then
        stopDealerTest("CANCEL", "resource-stop")
    end
    if stopResidencyTest then
        stopResidencyTest("CANCEL", "resource-stop")
    end
    if stopBikeJackTest then
        stopBikeJackTest("CANCEL", "resource-stop")
    end
    pendingNativeBikeJacks = {}
    triggerClientEvent(root, "pedTraffic:setEnabled", resourceRoot, false, false)
    clearTraffic("resource-stop")
    clearTestVehicles()
    populationProfiles = {}
    populationWorldRevisions = {}
end)
