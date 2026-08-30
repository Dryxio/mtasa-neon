local enabled = false
local debugEnabled = false
local populationWorldReady = false
local populationWorldRevision = 0
local populationWorldPreset = "none"
local populationCatalogRevision = "none"
local latestPopulationProfile = false
local assignments = {}
local groupAssignments = {}
local groupByPed = {}
local coupleAssignments = {}
local coupleByPed = {}
local nativeEventProfiles = {}
local nativeEventProfileNames = {}
local avoidanceStates = {}
local threatStates = {}
local vehicleReactionStates = {}
local airTestSessions = {}
local climbTestSessions = {}
local healthStates = {}
local observedAimTargets = {}
local nativeDamageObservations = {}
local nativeDamageTraceTimes = {}
local nativeDamageReplayReceipts = {}
local nativePlayerDamageReceipts = {}
local nativeBikeJackReceipts = {}
local coupleForwardSocialProbe = false
local spawnFades = {}
local residencyTest = false
local nextNativePlayerDamageNonce = 0

local function cleanupCoupleForwardSocialProbe(probe)
    if type(probe) ~= "table" then return end
    if probe.attackerCreated and isElement(probe.attacker) then
        destroyElement(probe.attacker)
    elseif probe.attacker == localPlayer and type(probe.playerState) == "table" then
        if type(setPedStandStill) == "function" then setPedStandStill(localPlayer, 1) end
        setElementInterior(localPlayer, probe.playerState.interior)
        setElementDimension(localPlayer, probe.playerState.dimension)
        setElementPosition(localPlayer, probe.playerState.x, probe.playerState.y, probe.playerState.z)
        if type(setPedRotation) == "function" then setPedRotation(localPlayer, probe.playerState.rotation) end
        if type(setPedWeaponSlot) == "function" then setPedWeaponSlot(localPlayer, probe.playerState.weaponSlot) end
    end
    if coupleForwardSocialProbe == probe then coupleForwardSocialProbe = false end
end
local nextNativeBikeJackNonce = 0
local nextObservedDamageNonce = 0
local stats = {
    candidateHits = 0,
    candidateMisses = 0,
    assignments = 0,
    failures = 0,
    groupAssignments = 0,
    groupFailures = 0,
    missReasons = {},
}

local gangStandardWeaponStats = {
    [69] = 40,
    [71] = 200,
    [75] = 50,
    [77] = 200,
}

local trafficNativeDamageWeapons = {
    [0] = true,
    [4] = true,
    [22] = true,
    [24] = true,
    [28] = true,
    [30] = true,
    [32] = true,
}

local function log(message, level)
    if debugEnabled or (level or 0) > 1 then
        outputDebugString("[ped-traffic][client] " .. message, level or 3)
    end
end

local function rememberNativeDamage(ped, attacker, weapon, bodypart)
    local observations = nativeDamageObservations[ped] or {}
    observations[#observations + 1] = {attacker = attacker, weapon = weapon, bodypart = bodypart, at = getTickCount()}
    while #observations > 32 do
        table.remove(observations, 1)
    end
    nativeDamageObservations[ped] = observations
end

local function consumeNativeDamage(ped, attacker, weapon, bodypart)
    local observations = nativeDamageObservations[ped]
    if not observations then
        return false
    end

    local now = getTickCount()
    local matched = false
    for index = #observations, 1, -1 do
        local observed = observations[index]
        if now - observed.at > 1000 then
            table.remove(observations, index)
        elseif not matched and observed.attacker == attacker and observed.weapon == weapon and observed.bodypart == bodypart then
            table.remove(observations, index)
            matched = true
        end
    end
    if #observations == 0 then
        nativeDamageObservations[ped] = nil
    end
    return matched
end

local function traceNativeDamageSource(ped, attacker, weapon, bodypart, role)
    if not debugEnabled then
        return
    end

    local victimId = getElementData(ped, "neon:ambientPedTrafficId")
    local victimGroup = getElementData(ped, "neon:ambientPedGroupId")
    local attackerType = isElement(attacker) and getElementType(attacker) or "none"
    local attackerId = attackerType == "ped" and getElementData(attacker, "neon:ambientPedTrafficId") or false
    local attackerGroup = attackerType == "ped" and getElementData(attacker, "neon:ambientPedGroupId") or false
    local attackerName = attackerType == "player" and getPlayerName(attacker) or "none"
    local sameGroup = victimGroup ~= false and victimGroup ~= nil and attackerGroup == victimGroup
    local signature = table.concat({attackerType, tostring(attackerId), tostring(attackerName), tostring(weapon),
                                    tostring(bodypart), tostring(role)}, ":")
    local now = getTickCount()
    local traceTimes = nativeDamageTraceTimes[ped] or {}
    if now - (traceTimes[signature] or -1000) < 1000 then
        return
    end
    traceTimes[signature] = now
    nativeDamageTraceTimes[ped] = traceTimes

    log(("damage-source victim=%s victimGroup=%s role=%s attackerType=%s attackerId=%s attackerGroup=%s attackerName=%s " ..
         "sameGroup=%s weapon=%s bodypart=%s"):format(tostring(victimId), tostring(victimGroup), tostring(role), attackerType,
                                                       tostring(attackerId), tostring(attackerGroup), tostring(attackerName),
                                                       tostring(sameGroup), tostring(weapon), tostring(bodypart)))
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

local function isIntegerInRange(value, minimum, maximum)
    return type(value) == "number" and value == value and value == math.floor(value) and value >= minimum and value <= maximum
end

local function validatePopulationWorldState(snapshot)
    if type(snapshot) ~= "table" or snapshot.schema ~= 1 or snapshot.baseline ~= "stock-main-scm-bootstrap-601def3b" or
        type(snapshot.catalogRevision) ~= "string" or #snapshot.catalogRevision ~= 64 or snapshot.catalogRevision:find("[^0-9a-f]") or
        not isIntegerInRange(snapshot.revision, 1, 2147483647) or type(snapshot.preset) ~= "string" or
        type(snapshot.capabilities) ~= "table" or snapshot.capabilities.zones ~= true or type(snapshot.zones) ~= "table" then
        return false, "invalid-header"
    end

    for label, state in pairs(snapshot.zones) do
        if type(label) ~= "string" or #label < 1 or #label > 7 or label:find("[^A-Z0-9]") or type(state) ~= "table" then
            return false, "invalid-zone"
        end
        if state.populationType ~= nil and not isIntegerInRange(state.populationType, 0, 19) then
            return false, "invalid-population-type"
        end
        if state.races ~= nil and not isIntegerInRange(state.races, 0, 15) then
            return false, "invalid-races"
        end
        if state.dealerStrength ~= nil and not isIntegerInRange(state.dealerStrength, 0, 255) then
            return false, "invalid-dealer-strength"
        end
        if state.noCops ~= nil and type(state.noCops) ~= "boolean" then
            return false, "invalid-no-cops"
        end
        if state.gangStrengths ~= nil then
            if type(state.gangStrengths) ~= "table" then
                return false, "invalid-gang-strengths"
            end
            for index, strength in pairs(state.gangStrengths) do
                if not isIntegerInRange(index, 1, 10) or not isIntegerInRange(strength, 0, 255) then
                    return false, "invalid-gang-strength"
                end
            end
        end
    end
    return true
end

local function applyPopulationWorldState(snapshot)
    populationWorldReady = false
    local valid, reason = validatePopulationWorldState(snapshot)
    if not valid then
        return false, reason
    end
    if type(resetAmbientPedPopulationZonesToBootstrap) ~= "function" or type(setAmbientPedPopulationZoneState) ~= "function" then
        return false, "native-api-unavailable"
    end
    if not resetAmbientPedPopulationZonesToBootstrap() then
        return false, "bootstrap-reset-failed"
    end

    local labels = {}
    for label in pairs(snapshot.zones) do
        labels[#labels + 1] = label
    end
    table.sort(labels)
    for _, label in ipairs(labels) do
        if not setAmbientPedPopulationZoneState(label, snapshot.zones[label]) then
            resetAmbientPedPopulationZonesToBootstrap()
            return false, "zone-apply-failed:" .. label
        end
    end

    populationWorldRevision = snapshot.revision
    populationWorldPreset = snapshot.preset
    populationCatalogRevision = snapshot.catalogRevision
    populationWorldReady = true
    return true
end

local function clearTimer(task, name)
    if isTimer(task[name]) then
        killTimer(task[name])
    end
    task[name] = nil
end

local function report(task, evidence, data)
    triggerServerEvent("pedTraffic:evidence", resourceRoot, task.ped, task.epoch, evidence, data or {})
end

local function acquireTrafficEventProfile(ped)
    if not isElement(ped) or getElementData(ped, "neon:ambientPedTraffic") ~= true or
        type(acquirePedNativeEventProfile) ~= "function" then
        return false
    end

    local populationClass = getElementData(ped, "neon:ambientPedPopulationClass")
    if populationClass ~= "civilian" and populationClass ~= "gang" and populationClass ~= "dealer" and populationClass ~= "cop" then
        return false
    end
    local profileName = populationClass == "cop" and "ambient-cop-safe" or "ambient-wander"
    local existingToken = nativeEventProfiles[ped]
    if existingToken then
        if nativeEventProfileNames[ped] == profileName then
            return true
        end

        -- Element data can arrive after the ped itself streams in. Replace an
        -- early generic lease once the immutable server-authenticated class is
        -- known; otherwise a cop would keep WanderStandard for its lifetime.
        if type(releasePedNativeEventProfile) ~= "function" or not releasePedNativeEventProfile(existingToken) then
            return false
        end
        nativeEventProfiles[ped] = nil
        nativeEventProfileNames[ped] = nil
    end

    local token = acquirePedNativeEventProfile(ped, profileName)
    if not token then
        return false
    end
    nativeEventProfiles[ped] = token
    nativeEventProfileNames[ped] = profileName
    healthStates[ped] = getElementHealth(ped)
    log(("profile-acquired id=%s profile=%s syncer=%s"):format(tostring(getElementData(ped, "neon:ambientPedTrafficId")),
                                                                 profileName, tostring(isElementSyncer(ped))))
    return true
end

local function releaseTrafficEventProfile(ped)
    local token = nativeEventProfiles[ped]
    if not token then
        return
    end
    if type(releasePedNativeEventProfile) == "function" then
        releasePedNativeEventProfile(token)
    end
    nativeEventProfiles[ped] = nil
    nativeEventProfileNames[ped] = nil
    avoidanceStates[ped] = nil
    threatStates[ped] = nil
    vehicleReactionStates[ped] = nil
    healthStates[ped] = nil
    nativeDamageObservations[ped] = nil
    nativeDamageTraceTimes[ped] = nil
end

local function getAvoidanceState(ped)
    if type(getPedTask) ~= "function" then
        return false
    end
    for slot = 1, 2 do
        local hierarchy = {getPedTask(ped, "primary", slot)}
        if hierarchy[1] ~= false then
            for _, taskName in ipairs(hierarchy) do
                if taskName == "TASK_COMPLEX_AVOID_OTHER_PED_WHILE_WANDERING" or taskName == "TASK_COMPLEX_WALK_ROUND_CAR" then
                    return true
                end
            end
        end
    end
    return false
end

local threatTaskNames = {
    TASK_COMPLEX_REACT_TO_GUN_AIMED_AT = true,
    TASK_COMPLEX_TURN_TO_FACE_ENTITY = true,
    TASK_COMPLEX_FLEE_ENTITY = true,
    TASK_COMPLEX_SMART_FLEE_ENTITY = true,
    TASK_COMPLEX_FLEE_ANY_MEANS = true,
    TASK_COMPLEX_CAR_DRIVE_MISSION_FLEE_SCENE = true,
    TASK_COMPLEX_KILL_PED_ON_FOOT = true,
    TASK_COMPLEX_KILL_PED_ON_FOOT_STEALTH = true,
    TASK_COMPLEX_KILL_CRIMINAL = true,
    TASK_COMPLEX_USE_CLOSEST_FREE_SCRIPTED_ATTRACTOR = true,
    TASK_COMPLEX_USE_CLOSEST_FREE_SCRIPTED_ATTRACTOR_RUN = true,
    TASK_COMPLEX_USE_CLOSEST_FREE_SCRIPTED_ATTRACTOR_SPRINT = true,
    TASK_SIMPLE_COWER = true,
    TASK_SIMPLE_HANDS_UP = true,
    TASK_SIMPLE_DUCK = true,
}

local function getThreatState(ped)
    if type(getPedTask) ~= "function" then
        return false, false
    end
    for slot = 1, 2 do
        local hierarchy = {getPedTask(ped, "primary", slot)}
        if hierarchy[1] ~= false then
            for _, taskName in ipairs(hierarchy) do
                if threatTaskNames[taskName] then
                    return hierarchy[1], hierarchy[#hierarchy]
                end
            end
        end
    end
    return false, false
end

local function hasDealerFightingControl(ped)
    if type(getPedTask) ~= "function" then
        return false
    end
    for slot = 0, 3 do
        local hierarchy = {getPedTask(ped, "primary", slot)}
        if hierarchy[1] ~= false then
            for _, taskName in ipairs(hierarchy) do
                if taskName == "TASK_SIMPLE_FIGHT_CTRL" then
                    return true, hierarchy
                end
            end
        end
    end
    return false
end

local vehicleReactionTaskNames = {
    TASK_COMPLEX_EVASIVE_STEP = true,
    TASK_SIMPLE_EVASIVE_STEP = true,
    TASK_COMPLEX_EVASIVE_DIVE_AND_GET_UP = true,
    TASK_SIMPLE_EVASIVE_DIVE = true,
    TASK_COMPLEX_HIT_PED_WITH_CAR = true,
    TASK_SIMPLE_HURT_PED_WITH_CAR = true,
    TASK_SIMPLE_KILL_PED_WITH_CAR = true,
    TASK_COMPLEX_FALL_AND_GET_UP = true,
    TASK_SIMPLE_FALL = true,
    TASK_SIMPLE_GET_UP = true,
    TASK_SIMPLE_SHAKE_FIST = true,
    TASK_COMPLEX_JUMP = true,
    TASK_SIMPLE_IN_AIR = true,
}

local jumpLifecycleTaskNames = {
    TASK_COMPLEX_JUMP = true,
    TASK_SIMPLE_JUMP = true,
    TASK_COMPLEX_IN_AIR_AND_LAND = true,
    TASK_SIMPLE_IN_AIR = true,
    TASK_SIMPLE_LAND = true,
    TASK_SIMPLE_HIT_HEAD = true,
    TASK_SIMPLE_FALL = true,
    TASK_SIMPLE_GET_UP = true,
    TASK_SIMPLE_CLIMB = true,
}

local function getJumpLifecycleState(ped)
    if type(getPedTask) ~= "function" then
        return false, false
    end
    for _, slot in ipairs({0, 1, 2, 3}) do
        local hierarchy = {getPedTask(ped, "primary", slot)}
        if hierarchy[1] ~= false then
            for _, taskName in ipairs(hierarchy) do
                if jumpLifecycleTaskNames[taskName] then
                    return hierarchy[1], hierarchy[#hierarchy]
                end
            end
        end
    end
    return false, false
end

local function classifyJumpLifecyclePhase(leaf)
    if leaf == "TASK_SIMPLE_JUMP" then
        return "launch"
    elseif leaf == "TASK_SIMPLE_IN_AIR" then
        return "in_air"
    elseif leaf == "TASK_SIMPLE_LAND" then
        return "land"
    elseif leaf == "TASK_SIMPLE_HIT_HEAD" then
        return "blocked"
    elseif leaf == "TASK_SIMPLE_FALL" then
        return "fall"
    elseif leaf == "TASK_SIMPLE_GET_UP" then
        return "get_up"
    elseif leaf == "TASK_SIMPLE_CLIMB" then
        return "climb"
    end
    return "none"
end

local PHYSICAL_LIFECYCLE_CLEAR_GRACE = 250

local function hasJumpLifecycleSettled(state, leaf, sawField, clearSinceField)
    local now = getTickCount()
    if leaf then
        state[sawField] = true
        state[clearSinceField] = nil
        return false
    end
    if not state[sawField] then
        return false
    end

    -- isPedOnGround remains false after a successful climb onto some network
    -- objects. The native jump chain disappearing is the authoritative end of
    -- the physical lifecycle; debounce it so a one-frame task transition does
    -- not restore Wander in the middle of a handoff.
    if not state[clearSinceField] then
        state[clearSinceField] = now
        return false
    end
    return now - state[clearSinceField] >= PHYSICAL_LIFECYCLE_CLEAR_GRACE
end

local function getVehicleReactionState(ped)
    if type(getPedTask) ~= "function" then
        return false, false
    end
    -- getPedTask exposes GTA's primary task-manager slots numerically:
    -- physical response, temporary event response, non-temporary event
    -- response, then the ordinary primary task.
    for _, slot in ipairs({0, 1, 2, 3}) do
        local hierarchy = {getPedTask(ped, "primary", slot)}
        if hierarchy[1] ~= false then
            for _, taskName in ipairs(hierarchy) do
                if vehicleReactionTaskNames[taskName] then
                    return hierarchy[1], hierarchy[#hierarchy]
                end
            end
        end
    end
    return false, false
end

local function reportGroup(task, evidence, data)
    triggerServerEvent("pedTraffic:groupEvidence", resourceRoot, task.id, task.epoch, evidence, data or {})
end

local function captureGroupDiagnostic(task, context, level)
    if not task.token or type(getPedNativeGroupDiagnostic) ~= "function" then
        return false
    end

    local diagnostic = getPedNativeGroupDiagnostic(task.token)
    if type(diagnostic) ~= "table" then
        return false
    end

    local evidence = {
        reason = tostring(diagnostic.reason or "unknown"),
        nativeGroupId = tonumber(diagnostic.nativeGroupId) or -1,
        slotActive = diagnostic.slotActive == true,
        resourceLeasePresent = diagnostic.resourceLeasePresent == true,
        gameLeasePresent = diagnostic.gameLeasePresent == true,
        memberCountMatches = diagnostic.memberCountMatches == true,
        hasTrackedMember = diagnostic.hasTrackedMember == true,
        members = {},
    }
    local memberLog = {}
    for index, member in ipairs(diagnostic.members or {}) do
        local compact = {
            resourceElementPresent = member.resourceElementPresent == true,
            resourceElementIsPed = member.resourceElementIsPed == true,
            resourcePedSyncing = member.resourcePedSyncing == true,
            gamePedPresent = member.gamePedPresent == true,
            leaseMemberMatches = member.leaseMemberMatches == true,
            nativeAmbientGroupFlag = member.nativeAmbientGroupFlag == true,
            attachedToExpectedGroup = member.attachedToExpectedGroup == true,
            primaryTask = tostring(member.primaryTask or "none"),
            expectedPrimaryTask = tostring(member.expectedPrimaryTask or "none"),
            defaultTask = tostring(member.defaultTask or "none"),
            expectedDefaultTask = tostring(member.expectedDefaultTask or "none"),
            primaryTaskType = tonumber(member.primaryTaskType) or -1,
            defaultTaskType = tonumber(member.defaultTaskType) or -1,
        }
        evidence.members[index] = compact
        memberLog[index] = ("m%d(sync=%s game=%s lease=%s flag=%s attached=%s primary=%s/%s default=%s/%s)"):format(
            index, tostring(compact.resourcePedSyncing), tostring(compact.gamePedPresent), tostring(compact.leaseMemberMatches),
            tostring(compact.nativeAmbientGroupFlag), tostring(compact.attachedToExpectedGroup), compact.primaryTask,
            compact.expectedPrimaryTask, compact.defaultTask, compact.expectedDefaultTask)
    end
    log(("group-diagnostic group=%d epoch=%d context=%s nativeReason=%s nativeGroup=%d slotActive=%s tracked=%s members=[%s]"):format(
            task.id, task.epoch, tostring(context), evidence.reason, evidence.nativeGroupId, tostring(evidence.slotActive),
            tostring(evidence.hasTrackedMember), table.concat(memberLog, ";")), level)
    return evidence
end

local function releaseGroupAssignment(task, releaseNativeGroup)
    clearTimer(task, "retryTimer")
    clearTimer(task, "monitorTimer")
    if releaseNativeGroup and task.token and type(releasePedNativeGroup) == "function" then
        releasePedNativeGroup(task.token)
    end
    task.token = nil
    task.accepted = false
    for _, ped in ipairs(task.peds) do
        local lease = task.leases[ped]
        if lease then
            releaseElementStreamingLease(lease)
            task.leases[ped] = nil
        end
        if groupByPed[ped] == task then
            groupByPed[ped] = nil
        end
    end
    if groupAssignments[task.id] == task then
        groupAssignments[task.id] = nil
    end
end

local function reportCouple(task, evidence, data)
    triggerServerEvent("pedTraffic:coupleEvidence", resourceRoot, task.id, task.epoch, evidence, data or {})
end

local function captureCoupleDiagnostic(task)
    if not task.token or type(getPedNativeCoupleDiagnostic) ~= "function" then
        return false
    end
    local diagnostic = getPedNativeCoupleDiagnostic(task.token)
    if type(diagnostic) ~= "table" then
        return false
    end
    local compact = {
        active = diagnostic.active == true,
        reason = tostring(diagnostic.reason or "unknown"),
        nativeCoupleId = tonumber(diagnostic.nativeCoupleId) or -1,
        resourceLeasePresent = diagnostic.resourceLeasePresent == true,
        gameLeasePresent = diagnostic.gameLeasePresent == true,
        aLeader = diagnostic.aLeader == true,
        members = {},
    }
    for index, member in ipairs(diagnostic.members or {}) do
        compact.members[index] = {
            resourceElementPresent = member.resourceElementPresent == true,
            resourceElementIsPed = member.resourceElementIsPed == true,
            resourcePedSyncing = member.resourcePedSyncing == true,
            gamePedPresent = member.gamePedPresent == true,
            leaseMemberMatches = member.leaseMemberMatches == true,
            primaryTaskMatchesLease = member.primaryTaskMatchesLease == true,
            reciprocalPartner = member.reciprocalPartner == true,
            leaderRoleMatches = member.leaderRoleMatches == true,
            primaryTaskType = tonumber(member.primaryTaskType) or -1,
            subTaskType = tonumber(member.subTaskType) or -1,
            currentEventType = tonumber(member.currentEventType) or -1,
            previousSide = tonumber(member.previousSide) or 0,
            damageEventPresent = member.damageEventPresent == true,
            shotFiredEventPresent = member.shotFiredEventPresent == true,
            gunAimedAtEventPresent = member.gunAimedAtEventPresent == true,
            forwardedDamageEventCount = tonumber(member.forwardedDamageEventCount) or 0,
            forwardedShotFiredEventCount = tonumber(member.forwardedShotFiredEventCount) or 0,
            forwardedGunAimedAtEventCount = tonumber(member.forwardedGunAimedAtEventCount) or 0,
            walkSpeed = tonumber(member.walkSpeed) or -1,
        }
    end
    return compact
end

local function reportCoupleArmState(task, diagnostic)
    if task.presentation or type(diagnostic) ~= "table" or type(diagnostic.members) ~= "table" then return end
    local sideA = tonumber(diagnostic.members[1] and diagnostic.members[1].previousSide)
    local sideB = tonumber(diagnostic.members[2] and diagnostic.members[2].previousSide)
    if (sideA ~= 1 and sideA ~= 2) or (sideB ~= 1 and sideB ~= 2) then return end
    if task.reportedSideA == sideA and task.reportedSideB == sideB then return end
    task.reportedSideA = sideA
    task.reportedSideB = sideB
    triggerServerEvent("pedTraffic:coupleArmState", resourceRoot, task.id, task.epoch, sideA, sideB)
end

local function releaseCoupleAssignment(task, releaseNative)
    clearTimer(task, "retryTimer")
    clearTimer(task, "monitorTimer")
    if type(coupleForwardSocialProbe) == "table" and coupleForwardSocialProbe.relationId == task.id then
        cleanupCoupleForwardSocialProbe(coupleForwardSocialProbe)
    end
    if releaseNative and task.token then
        if task.presentation and type(releasePedNativeCouplePresentation) == "function" then
            releasePedNativeCouplePresentation(task.token)
        elseif not task.presentation and type(releasePedNativeCouple) == "function" then
            releasePedNativeCouple(task.token)
        end
    end
    task.token = nil
    task.accepted = false
    for _, ped in ipairs(task.peds) do
        if task.leases[ped] then
            releaseElementStreamingLease(task.leases[ped])
            task.leases[ped] = nil
        end
        if coupleByPed[ped] == task then
            coupleByPed[ped] = nil
        end
    end
    if coupleAssignments[task.id] == task then
        coupleAssignments[task.id] = nil
    end
end

local function failCoupleAssignment(task, reason)
    stats.failures = stats.failures + 1
    local diagnostic = captureCoupleDiagnostic(task)
    reportCouple(task, "failure", {reason = reason, nativeDiagnostic = diagnostic or nil})
    releaseCoupleAssignment(task, true)
end

local function beginCoupleAssignment(task)
    if coupleAssignments[task.id] ~= task then
        return
    end
    if task.presentation then
        if type(acquireElementStreamingLease) ~= "function" or type(releaseElementStreamingLease) ~= "function" or
            type(acquirePedNativeCouplePresentation) ~= "function" or
            type(releasePedNativeCouplePresentation) ~= "function" or
            type(updatePedNativeCouplePresentation) ~= "function" or
            type(isPedNativeCouplePresentationActive) ~= "function" then
            return failCoupleAssignment(task, "couple-presentation-api-missing")
        end
        for _, ped in ipairs(task.peds) do
            if not isElement(ped) or isElementSyncer(ped) then
                if getTickCount() - task.requestedAt >= 5000 then
                    return failCoupleAssignment(task, "couple-presentation-not-ready")
                end
                clearTimer(task, "retryTimer")
                -- MTA copies table arguments passed to setTimer. Capture the
                -- authoritative assignment instead so the identity fence at
                -- beginCoupleAssignment accepts this retry.
                task.retryTimer = setTimer(function() beginCoupleAssignment(task) end, 100, 1)
                return
            end
            -- Observer presentation still needs a materialized GTA ped. Keep
            -- both actors resident without installing any owner-side AI.
            if not task.leases[ped] then
                task.leases[ped] = acquireElementStreamingLease(ped)
            end
            if not task.leases[ped] then
                return failCoupleAssignment(task, "couple-presentation-streaming-lease-refused")
            end
        end
        task.presentationStage = "acquiring"
        if not task.token then task.token = acquirePedNativeCouplePresentation(task.peds[1], task.peds[2]) end
        if not task.token then
            task.presentationStage = "acquire-refused"
            if getTickCount() - task.requestedAt >= 5000 then
                return failCoupleAssignment(task, "couple-presentation-refused")
            end
            clearTimer(task, "retryTimer")
            task.retryTimer = setTimer(function() beginCoupleAssignment(task) end, 100, 1)
            return
        end
        task.presentationStage = "acquired"
        -- A background second client can have onClientPreRender throttled for
        -- several seconds. Prime the retail IK presentation immediately so
        -- its observer state is ready before the harness samples it.
        task.presentationStage = "updating"
        -- Mark the resource lease accepted before entering the native task.
        -- If the game aborts this Lua call internally, the harness can still
        -- query the native-active predicate and report that exact boundary.
        task.accepted = true
        local updated = updatePedNativeCouplePresentation(task.token, task.sideA or 0, task.sideB or 0)
        task.presentationStage = updated and "updated" or "update-refused"
        if not updated then
            task.accepted = false
            releasePedNativeCouplePresentation(task.token)
            task.token = nil
            if getTickCount() - task.requestedAt >= 5000 then
                return failCoupleAssignment(task, "couple-presentation-update-refused")
            end
            clearTimer(task, "retryTimer")
            task.retryTimer = setTimer(function() beginCoupleAssignment(task) end, 100, 1)
            return
        end
        task.accepted = true
        task.presentationRetryStartedAt = nil
        return
    end
    if type(acquireElementStreamingLease) ~= "function" or type(releaseElementStreamingLease) ~= "function" or
        type(setPedUseNativeWalkingStyle) ~= "function" or type(isPedNativeEventProfileActive) ~= "function" or
        type(validatePedNativeCouple) ~= "function" or type(acquirePedNativeCouple) ~= "function" or
        type(releasePedNativeCouple) ~= "function" or type(isPedNativeCoupleActive) ~= "function" or
        type(getPedNativeCoupleDiagnostic) ~= "function" then
        return failCoupleAssignment(task, "couple-native-api-missing")
    end

    local ready = true
    for _, ped in ipairs(task.peds) do
        if not isElement(ped) then
            return failCoupleAssignment(task, "couple-member-missing")
        end
        if not acquireTrafficEventProfile(ped) then
            return failCoupleAssignment(task, "couple-profile-refused")
        end
        if not task.leases[ped] then
            task.leases[ped] = acquireElementStreamingLease(ped)
        end
        if not task.leases[ped] then
            return failCoupleAssignment(task, "couple-streaming-lease-refused")
        end
        if not isElementStreamedIn(ped) or not isElementSyncer(ped) or
            not isPedNativeEventProfileActive(ped, nativeEventProfiles[ped]) then
            ready = false
        end
    end

    if not ready then
        if getTickCount() - task.requestedAt < 10000 then
            clearTimer(task, "retryTimer")
            task.retryTimer = setTimer(function() beginCoupleAssignment(task) end, 200, 1)
            return
        end
        return failCoupleAssignment(task, "couple-stream-or-syncer-timeout")
    end

    for _, ped in ipairs(task.peds) do
        if not setPedUseNativeWalkingStyle(ped, true) then
            return failCoupleAssignment(task, "couple-native-walking-style-refused")
        end
    end

    local validation = validatePedNativeCouple(task.peds[1], task.peds[2])
    if type(validation) ~= "table" or validation.compatible ~= true or
        (task.leaderIndex and tonumber(validation.leaderIndex) ~= task.leaderIndex) then
        return failCoupleAssignment(task, "couple-native-validation-refused")
    end
    task.leaderIndex = tonumber(validation.leaderIndex)
    task.walkSpeeds = {tonumber(validation.walkSpeedA) or -1, tonumber(validation.walkSpeedB) or -1}
    task.token = acquirePedNativeCouple(task.peds[1], task.peds[2], task.leaderIndex)
    if not task.token or not isPedNativeCoupleActive(task.token) then
        return failCoupleAssignment(task, "couple-acquire-refused")
    end

    local diagnostic = captureCoupleDiagnostic(task)
    if not diagnostic or not diagnostic.active then
        return failCoupleAssignment(task, "couple-diagnostic-inactive")
    end
    task.accepted = true
    reportCoupleArmState(task, diagnostic)
    reportCouple(task, "accepted", {
        leaderIndex = task.leaderIndex,
        walkSpeeds = task.walkSpeeds,
        nativeDiagnostic = diagnostic,
    })
    task.monitorTimer = setTimer(function()
        if coupleAssignments[task.id] ~= task then
            return
        end
        for _, ped in ipairs(task.peds) do
            if not isElement(ped) then
                return releaseCoupleAssignment(task, true)
            end
            if not isElementSyncer(ped) then
                reportCouple(task, "ownership-lost", {})
                return releaseCoupleAssignment(task, true)
            end
        end
        if not isPedNativeCoupleActive(task.token) then
            if task.forwardingProbe then return end
            local ended = captureCoupleDiagnostic(task)
            reportCouple(task, "dissolved", {nativeDiagnostic = ended or nil})
            releaseCoupleAssignment(task, true)
            for _, ped in ipairs(task.peds) do
                if isElement(ped) and isElementSyncer(ped) then
                    setPedWander(ped, "walk", math.random(0, 7), true)
                end
            end
        else
            reportCoupleArmState(task, captureCoupleDiagnostic(task))
        end
    end, 100, 0)
end

local function failGroupAssignment(task, reason)
    stats.failures = stats.failures + 1
    stats.groupFailures = stats.groupFailures + 1
    local diagnostic = captureGroupDiagnostic(task, reason, 2)
    log(("group-failure group=%d epoch=%d reason=%s"):format(task.id, task.epoch, tostring(reason)), 2)
    reportGroup(task, "failure", {reason = reason, nativeDiagnostic = diagnostic or nil})
    releaseGroupAssignment(task, true)
end

local function captureGroupWeaponState(task)
    local ready = true
    local rows = {}
    for _, ped in ipairs(task.peds) do
        local expected = tonumber(getElementData(ped, "neon:ambientPedWeapon")) or 0
        local expectedAmmo = tonumber(getElementData(ped, "neon:ambientPedWeaponAmmo")) or 0
        local expectedClipAmmo = tonumber(getElementData(ped, "neon:ambientPedWeaponClipAmmo")) or 0
        local weapon = tonumber(getPedWeapon(ped)) or -1
        local totalAmmo = tonumber(getPedTotalAmmo(ped)) or 0
        local clipAmmo = tonumber(getPedAmmoInClip(ped)) or 0
        local weaponStats = {}
        local statsConverged = true
        for stat, expectedValue in pairs(gangStandardWeaponStats) do
            local actualValue = tonumber(getPedStat(ped, stat)) or -1
            weaponStats[tostring(stat)] = actualValue
            statsConverged = statsConverged and actualValue == expectedValue
        end
        -- MTA exposes a synthetic total ammo of 1 for slots which do not use
        -- ammunition, including unarmed. There is no vanilla ammo payload to
        -- authenticate for those slots; keep the exact total/clip check only
        -- for an actual gang weapon.
        local ammoConverged = expected == 0 or totalAmmo == expectedAmmo and clipAmmo == expectedClipAmmo
        local converged = weapon == expected and ammoConverged and statsConverged
        ready = ready and converged
        rows[#rows + 1] = {
            trafficId = getElementData(ped, "neon:ambientPedTrafficId"),
            expected = expected,
            expectedAmmo = expectedAmmo,
            expectedClipAmmo = expectedClipAmmo,
            weapon = weapon,
            totalAmmo = totalAmmo,
            clipAmmo = clipAmmo,
            ammoConverged = ammoConverged,
            weaponStats = weaponStats,
            statsConverged = statsConverged,
            converged = converged,
        }
    end
    return ready, rows
end

local function beginGroupAssignment(task)
    if groupAssignments[task.id] ~= task then
        return
    end
    if type(acquireElementStreamingLease) ~= "function" or type(releaseElementStreamingLease) ~= "function" or
        type(setPedUseNativeWalkingStyle) ~= "function" or type(isPedNativeEventProfileActive) ~= "function" or
        type(acquirePedNativeGroup) ~= "function" or type(releasePedNativeGroup) ~= "function" or
        type(isPedNativeGroupActive) ~= "function" then
        return failGroupAssignment(task, "group-native-api-missing")
    end

    local ready = true
    local readinessReason = "group-stream-or-syncer-timeout"
    for _, ped in ipairs(task.peds) do
        if not isElement(ped) then
            return failGroupAssignment(task, "group-member-missing")
        end
        if not acquireTrafficEventProfile(ped) then
            return failGroupAssignment(task, "group-profile-refused")
        end
        if not task.leases[ped] then
            task.leases[ped] = acquireElementStreamingLease(ped)
        end
        if not task.leases[ped] then
            return failGroupAssignment(task, "group-streaming-lease-refused")
        end
        if not isElementStreamedIn(ped) or not isElementSyncer(ped) or
            not isPedNativeEventProfileActive(ped, nativeEventProfiles[ped]) then
            ready = false
        end
    end

    local weaponsReady, weaponStates = captureGroupWeaponState(task)
    if not weaponsReady then
        ready = false
        readinessReason = "group-weapon-state-timeout"
    end

    if not ready then
        if getTickCount() - task.requestedAt < 10000 then
            clearTimer(task, "retryTimer")
            task.retryTimer = setTimer(function() beginGroupAssignment(task) end, 200, 1)
            return
        end
        return failGroupAssignment(task, readinessReason)
    end

    for _, ped in ipairs(task.peds) do
        if not setPedUseNativeWalkingStyle(ped, true) then
            return failGroupAssignment(task, "group-native-walking-style-refused")
        end
    end
    if not task.token then
        task.token = acquirePedNativeGroup(task.peds, "ambient-random")
    end
    if not task.token or not isPedNativeGroupActive(task.token) then
        return failGroupAssignment(task, "group-acquire-refused")
    end

    task.accepted = true
    stats.assignments = stats.assignments + #task.peds
    stats.groupAssignments = stats.groupAssignments + 1
    -- The deterministic residency test treats this evidence as the authority
    -- commit point. Carry the collision-readiness proof used by this exact
    -- branch so the server can reject an acceptance that races the lease.
    reportGroup(task, "accepted", {collisionReady = ready, weapons = weaponStates})
    log(("group-accepted group=%d epoch=%d members=%d reason=%s"):format(
            task.id, task.epoch, #task.peds, tostring(task.reason)), 3)
    task.monitorTimer = setTimer(function()
        if groupAssignments[task.id] ~= task then
            return
        end
        for _, ped in ipairs(task.peds) do
            if not isElement(ped) then
                captureGroupDiagnostic(task, "group-member-missing", 2)
                return releaseGroupAssignment(task, true)
            end
            if not isElementSyncer(ped) then
                captureGroupDiagnostic(task, "group-ownership-lost", 3)
                log(("group-ownership-lost group=%d epoch=%d"):format(task.id, task.epoch))
                return releaseGroupAssignment(task, true)
            end
        end
        if not isPedNativeGroupActive(task.token) then
            return failGroupAssignment(task, "group-became-inactive")
        end
    end, 1000, 0)
end

local function releaseTask(task, killNativeTask, preservePhysicalTask)
    clearTimer(task, "retryTimer")
    clearTimer(task, "monitorTimer")
    clearTimer(task, "resumeTimer")
    clearTimer(task, "physicalRestoreTimer")
    if killNativeTask and task.accepted and not preservePhysicalTask and isElement(task.ped) then
        killPedTask(task.ped, "primary", 3, false)
    end
    if task.lease then
        releaseElementStreamingLease(task.lease)
        task.lease = nil
    end
    task.accepted = false
    if assignments[task.ped] == task then
        assignments[task.ped] = nil
    end
end

local function fail(task, reason)
    stats.failures = stats.failures + 1
    log(("failure epoch=%d reason=%s"):format(task.epoch, reason), 2)
    report(task, "failure", {reason = reason})
    releaseTask(task, true)
end

local function beginAssignment(task)
    if assignments[task.ped] ~= task or not isElement(task.ped) then
        return
    end
    if type(acquireElementStreamingLease) ~= "function" or type(releaseElementStreamingLease) ~= "function" or
        type(setPedWander) ~= "function" or type(setPedUseNativeWalkingStyle) ~= "function" or
        type(isPedNativeEventProfileActive) ~= "function" then
        return fail(task, "native-api-missing")
    end

    if not acquireTrafficEventProfile(task.ped) then
        return fail(task, "ambient-profile-refused")
    end

    if not task.lease then
        task.lease = acquireElementStreamingLease(task.ped)
    end
    if not task.lease then
        return fail(task, "streaming-lease-refused")
    end

    if not isElementStreamedIn(task.ped) or not isElementSyncer(task.ped) then
        if getTickCount() - task.requestedAt < 10000 then
            clearTimer(task, "retryTimer")
            task.retryTimer = setTimer(function() beginAssignment(task) end, 200, 1)
            return
        end
        return fail(task, "stream-or-syncer-timeout")
    end

    if not task.resumePhysical and not isPedNativeEventProfileActive(task.ped, nativeEventProfiles[task.ped]) then
        if getTickCount() - task.requestedAt < 10000 then
            clearTimer(task, "retryTimer")
            task.retryTimer = setTimer(function() beginAssignment(task) end, 200, 1)
            return
        end
        return fail(task, "collision-residency-timeout")
    end

    if not setPedUseNativeWalkingStyle(task.ped, true) then
        return fail(task, "native-walking-style-refused")
    end
    if getElementData(task.ped, "neon:ambientPedPopulationClass") == "cop" then
        if type(setPedWeaponShootingRate) ~= "function" or type(setPedWeaponAccuracy) ~= "function" or
            not setPedWeaponShootingRate(task.ped, 30) or not setPedWeaponAccuracy(task.ped, 60) then
            return fail(task, "cop-safe-attributes-refused")
        end
    end
    local function installWander()
        if assignments[task.ped] ~= task or not isElement(task.ped) or not isElementSyncer(task.ped) then
            return
        end
        if task.accepted then
            return
        end

        task.accepted = setPedWander(task.ped, "walk", task.direction, true)
        if not task.accepted then
            return fail(task, "wander-refused")
        end

        stats.assignments = stats.assignments + 1
        report(task, "accepted")
        log(("accepted epoch=%d direction=%d reason=%s resumedPhysical=%s"):format(
                task.epoch, task.direction, tostring(task.reason), tostring(task.resumePhysical)))
        task.monitorTimer = setTimer(function()
            if assignments[task.ped] ~= task then
                return
            end
            if not isElement(task.ped) then
                return releaseTask(task, false)
            end
            if not isElementSyncer(task.ped) then
                log(("ownership-lost epoch=%d"):format(task.epoch))
                return releaseTask(task, true)
            end
            if isPedDead(task.ped) then
                return releaseTask(task, false)
            end
            if not task.dealerFightReported and getElementData(task.ped, "neon:ambientPedPopulationClass") == "dealer" then
                local fighting, hierarchy = hasDealerFightingControl(task.ped)
                if fighting then
                    task.dealerFightReported = true
                    local knifeModelLoaded = type(engineStreamingGetModelLoadState) == "function" and
                        engineStreamingGetModelLoadState(335) == "loaded"
                    triggerServerEvent("pedTraffic:dealerFightStarted", resourceRoot, task.ped, task.epoch, knifeModelLoaded, hierarchy)
                end
            end
        end, 250, 0)
    end

    if task.resumePhysical then
        -- Assignment retries are normal while the server waits for the
        -- accepted evidence. Keep exactly one physical-completion waiter per
        -- epoch.
        if task.resumePending then
            return
        end
        task.resumePending = true
        task.resumeStartedAt = getTickCount()
        local function waitForPhysicalCompletion()
            if assignments[task.ped] ~= task or not isElement(task.ped) or not isElementSyncer(task.ped) then
                return
            end

            local _, leaf = getJumpLifecycleState(task.ped)
            local elapsed = getTickCount() - task.resumeStartedAt
            if not hasJumpLifecycleSettled(task, leaf, "resumeSawPhysical", "resumeClearSince") then
                if elapsed >= 6000 then
                    task.resumePending = false
                    return fail(task, "physical-resume-timeout")
                end
                clearTimer(task, "resumeTimer")
                task.resumeTimer = setTimer(waitForPhysicalCompletion, 50, 1)
                return
            end
            task.resumePending = false
            -- A transferred physical task must run to completion, but native
            -- ambient simulation may resume only after the new owner's COL
            -- lease has observed both loaded collision and ground support.
            task.resumePhysical = false
            beginAssignment(task)
        end
        waitForPhysicalCompletion()
        return
    end

    installWander()
end

local function stopResidencyObservation()
    local test = residencyTest
    residencyTest = false
    if not test then
        return
    end
    if isTimer(test.timer) then
        killTimer(test.timer)
    end
    if type(setPedStayInSamePlace) == "function" then
        for ped, previous in pairs(test.previousStay) do
            if isElement(ped) then
                setPedStayInSamePlace(ped, previous)
            end
        end
    end
end

local function sampleResidencyTest(test)
    if residencyTest ~= test then
        return
    end
    local rows = {}
    for _, ped in ipairs(test.peds) do
        if not isElement(ped) then
            triggerServerEvent("pedTraffic:residencySample", resourceRoot, test.id, {error = "ped-missing"})
            return
        end
        local rootTask, leafTask = getJumpLifecycleState(ped)
        local _, _, z = getElementPosition(ped)
        local _, _, vz = getElementVelocity(ped)
        local token = nativeEventProfiles[ped]
        rows[#rows + 1] = {
            ped = ped,
            trafficId = getElementData(ped, "neon:ambientPedTrafficId"),
            epoch = getElementData(ped, "neon:ambientPedTrafficEpoch"),
            z = z,
            velocityZ = vz,
            frozen = isElementFrozen(ped),
            grounded = type(isPedOnGround) == "function" and isPedOnGround(ped) or false,
            syncer = isElementSyncer(ped),
            ready = token and type(isPedNativeEventProfileActive) == "function" and
                isPedNativeEventProfileActive(ped, token) == true or false,
            rootTask = rootTask or false,
            leafTask = leafTask or false,
            phase = classifyJumpLifecyclePhase(leafTask),
        }
    end
    test.sequence = test.sequence + 1
    triggerServerEvent("pedTraffic:residencySample", resourceRoot, test.id, {
        clientTick = getTickCount(),
        sequence = test.sequence,
        peds = rows,
    })
end

addEvent("pedTraffic:residencyObserve", true)
addEventHandler("pedTraffic:residencyObserve", resourceRoot, function(id, peds)
    stopResidencyObservation()
    if not isIntegerInRange(id, 1, 2147483647) or type(peds) ~= "table" or #peds < 1 or
        type(getPedStayInSamePlace) ~= "function" or type(setPedStayInSamePlace) ~= "function" then
        triggerServerEvent("pedTraffic:residencySample", resourceRoot, id, {error = "client-api-unavailable"})
        return
    end
    local test = {id = id, peds = {}, previousStay = {}, sequence = 0}
    residencyTest = test
    for _, ped in ipairs(peds) do
        if not isElement(ped) or getElementData(ped, "neon:ambientPedTraffic") ~= true then
            triggerServerEvent("pedTraffic:residencySample", resourceRoot, id, {error = "invalid-ped"})
            stopResidencyObservation()
            return
        end
        test.peds[#test.peds + 1] = ped
        test.previousStay[ped] = getPedStayInSamePlace(ped) == true
        if not setPedStayInSamePlace(ped, true) then
            triggerServerEvent("pedTraffic:residencySample", resourceRoot, id, {error = "stay-put-refused"})
            stopResidencyObservation()
            return
        end
    end
    sampleResidencyTest(test)
    -- MTA clones table arguments passed through timers, which would break the
    -- identity guard in sampleResidencyTest and silently stop sampling.
    test.timer = setTimer(function()
        sampleResidencyTest(test)
    end, 100, 0)
end)

addEvent("pedTraffic:residencyStop", true)
addEventHandler("pedTraffic:residencyStop", resourceRoot, function(id)
    if residencyTest and residencyTest.id == id then
        stopResidencyObservation()
    end
end)

addEvent("pedTraffic:populationWorldState", true)
addEventHandler("pedTraffic:populationWorldState", resourceRoot, function(snapshot)
    local revision = type(snapshot) == "table" and snapshot.revision or false
    local success, reason = applyPopulationWorldState(snapshot)
    triggerServerEvent("pedTraffic:populationWorldApplied", resourceRoot, revision,
                       {zones = success == true, catalogRevision = success and populationCatalogRevision or false}, success, reason)
    if success then
        log(("population-world-applied revision=%d preset=%s catalog=%s"):format(
                populationWorldRevision, populationWorldPreset, populationCatalogRevision), 3)
    else
        log(("population-world-rejected revision=%s reason=%s"):format(tostring(revision), tostring(reason)), 2)
    end
end)

addEvent("pedTraffic:setEnabled", true)
addEventHandler("pedTraffic:setEnabled", resourceRoot, function(value, debugValue)
    enabled = value == true
    debugEnabled = debugValue == true
    observedAimTargets = {}
    log("enabled=" .. tostring(enabled))
    if not enabled then
        cleanupCoupleForwardSocialProbe(coupleForwardSocialProbe)
        stopResidencyObservation()
        populationWorldReady = false
        latestPopulationProfile = false
        airTestSessions = {}
        climbTestSessions = {}
        nativeDamageReplayReceipts = {}
        local couples = {}
        for _, task in pairs(coupleAssignments) do couples[#couples + 1] = task end
        for _, task in ipairs(couples) do releaseCoupleAssignment(task, true) end
        local groups = {}
        for _, task in pairs(groupAssignments) do groups[#groups + 1] = task end
        for _, task in ipairs(groups) do releaseGroupAssignment(task, true) end
        local current = {}
        for _, task in pairs(assignments) do current[#current + 1] = task end
        for _, task in ipairs(current) do releaseTask(task, true) end
        if type(resetAmbientPedPopulationModels) == "function" then
            resetAmbientPedPopulationModels()
        end
    end
end)

addEvent("pedTraffic:setDebug", true)
addEventHandler("pedTraffic:setDebug", resourceRoot, function(value)
    debugEnabled = value == true
    log("debug=" .. tostring(debugEnabled))
end)

addEvent("pedTraffic:candidateRequest", true)
addEventHandler("pedTraffic:candidateRequest", resourceRoot, function(requestId, worldRevision, populationClass, gang, maximumGroupMembers,
                                                                      coupleAttempt, originX, originY, originZ)
    local startedAt = getTickCount()
    if not enabled or not populationWorldReady or worldRevision ~= populationWorldRevision or getElementDimension(localPlayer) ~= 0 or
        getElementInterior(localPlayer) ~= 0 or isPedDead(localPlayer) or
        (populationClass == "gang" and type(getAmbientPedGangGroupCandidate) ~= "function" or
            coupleAttempt == true and type(getAmbientPedCivilianCoupleCandidate) ~= "function" or
            populationClass ~= "gang" and coupleAttempt ~= true and type(getAmbientPedSpawnCandidate) ~= "function") then
        stats.candidateMisses = stats.candidateMisses + 1
        triggerServerEvent("pedTraffic:candidate", resourceRoot, requestId, worldRevision, false, getTickCount() - startedAt, "world-not-ready")
        return
    end

    if populationClass ~= "civilian" and populationClass ~= "gang" and populationClass ~= "dealer" and populationClass ~= "cop" or
        (populationClass == "gang" and (type(gang) ~= "number" or gang ~= math.floor(gang) or gang < 0 or gang > 7)) or
        (populationClass ~= "gang" and gang ~= false) then
        stats.candidateMisses = stats.candidateMisses + 1
        triggerServerEvent("pedTraffic:candidate", resourceRoot, requestId, worldRevision, false, getTickCount() - startedAt,
                           "invalid-population-hint")
        return
    end

    if coupleAttempt ~= true and coupleAttempt ~= false or coupleAttempt == true and populationClass ~= "civilian" then
        stats.candidateMisses = stats.candidateMisses + 1
        triggerServerEvent("pedTraffic:candidate", resourceRoot, requestId, worldRevision, false, getTickCount() - startedAt,
                           "invalid-couple-hint")
        return
    end

    if populationClass == "gang" and not isIntegerInRange(maximumGroupMembers, 2, 4) then
        stats.candidateMisses = stats.candidateMisses + 1
        triggerServerEvent("pedTraffic:candidate", resourceRoot, requestId, worldRevision, false, getTickCount() - startedAt,
                           "invalid-group-size")
        return
    end

    local playerX, playerY, playerZ = getElementPosition(localPlayer)
    local x, y, z = tonumber(originX), tonumber(originY), tonumber(originZ)
    -- The server caps its latency prediction at 45 m. Keep one metre of
    -- numeric tolerance so a valid high-speed request is not silently reset
    -- to the player's stale position before reaching the native oracle.
    local predictedOriginValid = x and y and z and x == x and y == y and z == z and
        getDistanceBetweenPoints3D(playerX, playerY, playerZ, x, y, z) <= 46
    if not predictedOriginValid then x, y, z = playerX, playerY, playerZ end
    local candidate, missReason
    if coupleAttempt == true then
        candidate, missReason = getAmbientPedCivilianCoupleCandidate(x, y, z)
    elseif populationClass == "gang" then
        candidate, missReason = getAmbientPedGangGroupCandidate(x, y, z, gang, maximumGroupMembers)
        if type(candidate) == "table" then
            for _, member in ipairs(candidate.members or candidate) do
                if type(member) == "table" then
                    -- The group oracle's gang argument is authoritative for
                    -- this read-only proposal; retain the shared single-ped
                    -- wire shape so the server can authenticate every member.
                    member.populationClass = "gang"
                end
            end
        end
    else
        candidate, missReason = getAmbientPedSpawnCandidate(x, y, z, populationClass, -1)
    end
    if candidate then
        stats.candidateHits = stats.candidateHits + 1
    else
        stats.candidateMisses = stats.candidateMisses + 1
        countReason(stats.missReasons, missReason)
    end
    if debugEnabled then
        local memberCount = (populationClass == "gang" or coupleAttempt == true) and
            (type(candidate) == "table" and #(candidate.members or candidate) or 0) or 1
        log(("candidate request=%d class=%s gang=%s result=%s model=%s members=%d elapsed=%d reason=%s"):format(
                requestId, populationClass, tostring(gang), tostring(candidate ~= false and candidate ~= nil),
                tostring(candidate and candidate.model), memberCount, getTickCount() - startedAt, tostring(missReason)))
    end
    triggerServerEvent("pedTraffic:candidate", resourceRoot, requestId, worldRevision, candidate or false, getTickCount() - startedAt, missReason)
end)

addEvent("pedTraffic:visibilityProbe", true)
addEventHandler("pedTraffic:visibilityProbe", resourceRoot, function(checkId, kind, probes, populationClass)
    local visible = false
    local detail = {probeIndex = false, tooClose = false}
    if not enabled or not populationWorldReady or type(checkId) ~= "number" or type(probes) ~= "table" then
        visible = true
        detail.reason = "runtime-not-ready"
    elseif kind == "candidate" then
        local profile = latestPopulationProfile
        if type(profile) ~= "table" or profile.worldRevision ~= populationWorldRevision or
            getTickCount() - (profile.capturedAt or 0) > 2500 or
            type(isAmbientPedSphereVisible) ~= "function" then
            visible = true
            detail.reason = "native-visibility-unavailable"
        else
            local tooCloseDistance = profile.creationDistanceMultiplier * 42.5 + (populationClass == "gang" and 30 or 0)
            local playerX, playerY = getElementPosition(localPlayer)
            for index, probe in ipairs(probes) do
                if type(probe) ~= "table" or type(probe.x) ~= "number" or type(probe.y) ~= "number" or
                    type(probe.z) ~= "number" or type(probe.radius) ~= "number" then
                    visible = true
                    detail.reason = "invalid-probe"
                    detail.probeIndex = index
                    break
                end
                local dx, dy = probe.x - playerX, probe.y - playerY
                local tooClose = dx * dx + dy * dy < tooCloseDistance * tooCloseDistance
                if tooClose and isAmbientPedSphereVisible(probe.x, probe.y, probe.z, probe.radius) then
                    visible = true
                    detail.probeIndex = index
                    detail.tooClose = true
                    break
                end
            end
        end
    elseif kind == "ped-removal" or kind == "group-removal" then
        for index, ped in ipairs(probes) do
            if isElement(ped) and isElementOnScreen(ped) then
                visible = true
                detail.probeIndex = index
                break
            end
        end
    else
        visible = true
        detail.reason = "invalid-kind"
    end
    log(("visibility-probe check=%d kind=%s visible=%s probe=%s tooClose=%s reason=%s"):format(
            checkId, tostring(kind), tostring(visible), tostring(detail.probeIndex), tostring(detail.tooClose), tostring(detail.reason)))
    triggerServerEvent("pedTraffic:visibilityProbeResult", resourceRoot, checkId, visible, detail)
end)

addEvent("pedTraffic:spawnFadeIn", true)
addEventHandler("pedTraffic:spawnFadeIn", resourceRoot, function(peds, duration)
    if type(peds) ~= "table" or type(duration) ~= "number" or duration < 1 or duration > 1000 then
        return
    end
    local now = getTickCount()
    for _, ped in ipairs(peds) do
        if isElement(ped) and getElementType(ped) == "ped" and getElementData(ped, "neon:ambientPedTraffic") == true then
            setElementAlpha(ped, 0)
            spawnFades[ped] = {startedAt = now, duration = duration}
        end
    end
end)

local function updateLocalPopulationModels()
    if enabled and populationWorldReady and getElementDimension(localPlayer) == 0 and getElementInterior(localPlayer) == 0 and
        type(updateAmbientPedPopulationModels) == "function" then
        local x, y, z = getElementPosition(localPlayer)
        updateAmbientPedPopulationModels(x, y, z)
    end
end

setTimer(function()
    if not enabled or not populationWorldReady then
        return
    end

    -- onClientPreRender is throttled or suspended for an unfocused/minimized
    -- MTA window. Refresh the native model lease from this timer as well so a
    -- background second client can still produce its population profile and
    -- participate in deterministic owner/observer tests.
    updateLocalPopulationModels()

    -- The native API intentionally collapses unsupported runtime states to
    -- false. Send bounded Lua-side context with the ordinary report so the
    -- server can distinguish that case from a missing function or rejection.
    local profileFunctionPresent = type(getAmbientPedPopulationProfile) == "function"
    local diagnostic = {
        profileFunctionPresent = profileFunctionPresent,
        updateFunctionPresent = type(updateAmbientPedPopulationModels) == "function",
        dimension = getElementDimension(localPlayer),
        interior = getElementInterior(localPlayer),
        worldRevision = populationWorldRevision,
        catalogRevision = populationCatalogRevision,
    }
    if not profileFunctionPresent then
        diagnostic.profileType = "function-missing"
        triggerServerEvent("pedTraffic:populationProfile", resourceRoot, false, diagnostic)
        return
    end

    local profile = getAmbientPedPopulationProfile()
    diagnostic.profileType = type(profile)
    if type(profile) == "table" then
        profile.worldRevision = populationWorldRevision
        profile.catalogRevision = populationCatalogRevision
        profile.capturedAt = getTickCount()
        latestPopulationProfile = profile
    end
    triggerServerEvent("pedTraffic:populationProfile", resourceRoot, type(profile) == "table" and profile or false, diagnostic)
end, 1000, 0)

addEvent("pedTraffic:assign", true)
addEventHandler("pedTraffic:assign", resourceRoot, function(ped, epoch, direction, reason, resumePhysical)
    if not enabled or not isElement(ped) then
        return
    end
    local old = assignments[ped]
    if old then
        if old.epoch == epoch then
            if old.accepted then
                report(old, "accepted")
            else
                beginAssignment(old)
            end
            return
        end
        releaseTask(old, true)
    end
    local task = {
        ped = ped,
        epoch = epoch,
        direction = direction,
        reason = reason,
        requestedAt = getTickCount(),
        accepted = false,
        resumePhysical = resumePhysical == true,
    }
    assignments[ped] = task
    beginAssignment(task)
end)

addEvent("pedTraffic:revoke", true)
addEventHandler("pedTraffic:revoke", resourceRoot, function(ped, epoch, reason)
    local task = assignments[ped]
    if not task or task.epoch ~= epoch then
        return
    end
    log(("revoke epoch=%d reason=%s"):format(epoch, tostring(reason)))
    -- During a deterministic physical handoff, the C++ stop-sync transition
    -- removes the exact jump chain. Killing primary here would publish NONE
    -- before the reliable physical edge can seed the new owner.
    releaseTask(task, true, reason == "airtest-in-air" or reason == "climbtest-climb")
    report(task, "released", {reason = reason})
end)

addEvent("pedTraffic:stop", true)
addEventHandler("pedTraffic:stop", resourceRoot, function(ped, epoch, reason)
    local task = assignments[ped]
    if task and task.epoch == epoch then
        log(("stop epoch=%d reason=%s"):format(epoch, tostring(reason)))
        releaseTask(task, true)
    end
end)

local function collectDealerTestTasks(ped)
    local tasks = {}
    local hasWander = false
    if type(getPedTask) ~= "function" then
        return tasks, hasWander
    end
    for slot = 0, 3 do
        local hierarchy = {getPedTask(ped, "primary", slot)}
        if hierarchy[1] ~= false then
            for _, taskName in ipairs(hierarchy) do
                tasks[#tasks + 1] = ("%d:%s"):format(slot, tostring(taskName))
                if taskName == "TASK_COMPLEX_WANDER" or taskName == "TASK_COMPLEX_WANDER_STANDARD" then
                    hasWander = true
                end
            end
        end
    end
    return tasks, hasWander
end

local copTestBranches = {
    TASK_SIMPLE_GO_TO_POINT = "go_to",
    TASK_SIMPLE_STAND_STILL = "stand_still",
    TASK_SIMPLE_SCRATCH_HEAD = "scratch_head",
    TASK_COMPLEX_OBSERVE_TRAFFIC_LIGHTS_AND_ACHIEVE_HEADING = "traffic_light",
    TASK_COMPLEX_CROSS_ROAD_LOOK_AND_ACHIEVE_HEADING = "cross_road",
    TASK_COMPLEX_SEQUENCE = "roadcross_sequence",
}

local copForbiddenTasks = {
    TASK_SIMPLE_ARREST_PED = true,
    TASK_COMPLEX_ARREST_PED = true,
    TASK_SIMPLE_BE_ARRESTED = true,
    TASK_COMPLEX_POLICE_PURSUIT = true,
    TASK_COMPLEX_BE_COP = true,
    TASK_COMPLEX_KILL_CRIMINAL = true,
    TASK_COMPLEX_COP_IN_CAR = true,
}

local function collectCopTestTaskState(ped)
    local tasks = {}
    local hasWander = false
    local rootTask = false
    local branch = false
    local forbiddenTask = false
    if type(getPedTask) ~= "function" then
        return tasks, hasWander, rootTask, branch, forbiddenTask
    end

    for slot = 0, 3 do
        local hierarchy = {getPedTask(ped, "primary", slot)}
        if hierarchy[1] ~= false then
            if slot == 3 then rootTask = hierarchy[1] end
            for _, taskName in ipairs(hierarchy) do
                tasks[#tasks + 1] = ("%d:%s"):format(slot, tostring(taskName))
                if taskName == "TASK_COMPLEX_WANDER" or taskName == "TASK_COMPLEX_WANDER_STANDARD" then
                    hasWander = true
                end
                branch = branch or copTestBranches[taskName]
                if copForbiddenTasks[taskName] then forbiddenTask = taskName end
            end
        end
    end
    return tasks, hasWander, rootTask, branch, forbiddenTask
end

addEvent("pedTraffic:dealerTestSample", true)
addEventHandler("pedTraffic:dealerTestSample", resourceRoot, function(testId, ped, epoch, phase)
    if not enabled or not isElement(ped) or getElementType(ped) ~= "ped" or
        getElementData(ped, "neon:ambientPedTrafficId") == false then
        return
    end
    local task = assignments[ped]
    local token = nativeEventProfiles[ped]
    local tasks, hasWander = collectDealerTestTasks(ped)
    local hasFight = hasDealerFightingControl(ped)
    triggerServerEvent("pedTraffic:dealerTestSampleResult", resourceRoot, testId, phase, {
        model = getElementModel(ped),
        populationClass = getElementData(ped, "neon:ambientPedPopulationClass"),
        logicalPedType = getElementData(ped, "neon:ambientPedLogicalType"),
        weapon = getPedWeapon(ped),
        weaponAmmo = getPedTotalAmmo(ped),
        knifeAmmo = getPedTotalAmmo(ped, 1),
        pistolAmmo = getPedTotalAmmo(ped, 2),
        pistolSkillStat = getPedStat(ped, 69),
        dealerFightArmed = getElementData(ped, "neon:ambientPedDealerFightArmed") == true,
        dealerHasKnife = getElementData(ped, "neon:ambientPedDealerKnife") == true,
        dealerHasPistol = getElementData(ped, "neon:ambientPedDealerPistol") == true,
        catalogRevision = populationCatalogRevision,
        epoch = epoch,
        syncer = isElementSyncer(ped),
        assignment = task ~= nil and task.epoch == epoch,
        assignmentAccepted = task ~= nil and task.epoch == epoch and task.accepted == true,
        profilePresent = token ~= nil,
        profileName = nativeEventProfileNames[ped],
        profileActive = token ~= nil and isPedNativeEventProfileActive(ped, token) == true,
        hasWander = hasWander,
        hasFight = hasFight == true,
        tasks = tasks,
    })
end)

addEvent("pedTraffic:dealerTestCleanup", true)
addEventHandler("pedTraffic:dealerTestCleanup", resourceRoot, function(testId, trafficId)
    local function reportCleanup(attempt)
        local elementPresent = false
        local assignmentPresent = false
        local profilePresent = false
        for ped, task in pairs(assignments) do
            if isElement(ped) and tonumber(getElementData(ped, "neon:ambientPedTrafficId")) == trafficId then
                elementPresent = true
                assignmentPresent = task ~= nil
            end
        end
        for ped in pairs(nativeEventProfiles) do
            if isElement(ped) and tonumber(getElementData(ped, "neon:ambientPedTrafficId")) == trafficId then
                elementPresent = true
                profilePresent = true
            end
        end
        if elementPresent and attempt < 20 then
            setTimer(reportCleanup, 100, 1, attempt + 1)
            return
        end
        triggerServerEvent("pedTraffic:dealerTestCleanupResult", resourceRoot, testId, trafficId, {
            elementPresent = elementPresent,
            assignmentPresent = assignmentPresent,
            profilePresent = profilePresent,
        })
    end
    reportCleanup(1)
end)

addEvent("pedTraffic:copTestSample", true)
addEventHandler("pedTraffic:copTestSample", resourceRoot, function(testId, sampleId, ped, epoch, phase)
    if not enabled or not isElement(ped) then return end
    local task = assignments[ped]
    local token = nativeEventProfiles[ped]
    local tasks, hasWander, rootTask, branch, forbiddenTask = collectCopTestTaskState(ped)
    local ambientCopTask = type(isPedNativeAmbientCopWanderTask) == "function" and isPedNativeAmbientCopWanderTask(ped) == true
    local x, y, z = getElementPosition(ped)
    triggerServerEvent("pedTraffic:copTestSampleResult", resourceRoot, testId, sampleId, phase, {
        model = getElementModel(ped),
        populationClass = getElementData(ped, "neon:ambientPedPopulationClass"),
        logicalPedType = getElementData(ped, "neon:ambientPedLogicalType"),
        worldLevel = getElementData(ped, "neon:ambientPedCopLevel"),
        activeWeapon = getPedWeapon(ped),
        nightstick = getPedWeapon(ped, 1),
        pistol = getPedWeapon(ped, 2),
        pistolAmmo = getPedTotalAmmo(ped, 2),
        pistolSkillStat = getPedStat(ped, 69),
        armor = getPedArmor(ped),
        shootingRate = type(getPedWeaponShootingRate) == "function" and getPedWeaponShootingRate(ped) or -1,
        accuracy = type(getPedWeaponAccuracy) == "function" and getPedWeaponAccuracy(ped) or -1,
        wanted = getPlayerWantedLevel(localPlayer),
        epoch = epoch,
        syncer = isElementSyncer(ped),
        assignment = task ~= nil and task.epoch == epoch,
        assignmentAccepted = task ~= nil and task.epoch == epoch and task.accepted == true,
        profilePresent = token ~= nil,
        profileName = nativeEventProfileNames[ped],
        profileActive = token ~= nil and isPedNativeEventProfileActive(ped, token) == true,
        hasWander = hasWander,
        rootTask = rootTask,
        branch = branch,
        forbiddenTask = forbiddenTask,
        ambientCopTask = ambientCopTask,
        vtableSafe = ambientCopTask,
        x = x,
        y = y,
        z = z,
        tasks = tasks,
    })
end)

addEvent("pedTraffic:copTestCleanup", true)
addEventHandler("pedTraffic:copTestCleanup", resourceRoot, function(testId, trafficId)
    local function reportCleanup(attempt)
        local elementPresent = false
        local assignmentPresent = false
        local profilePresent = false
        for ped, task in pairs(assignments) do
            if isElement(ped) and tonumber(getElementData(ped, "neon:ambientPedTrafficId")) == trafficId then
                elementPresent = true
                assignmentPresent = task ~= nil
            end
        end
        for ped in pairs(nativeEventProfiles) do
            if isElement(ped) and tonumber(getElementData(ped, "neon:ambientPedTrafficId")) == trafficId then
                elementPresent = true
                profilePresent = true
            end
        end
        if elementPresent and attempt < 20 then
            setTimer(reportCleanup, 100, 1, attempt + 1)
            return
        end
        triggerServerEvent("pedTraffic:copTestCleanupResult", resourceRoot, testId, trafficId, {
            elementPresent = elementPresent,
            assignmentPresent = assignmentPresent,
            profilePresent = profilePresent,
        })
    end
    reportCleanup(1)
end)

addEvent("pedTraffic:assignCouple", true)
addEventHandler("pedTraffic:assignCouple", resourceRoot, function(relationId, relationEpoch, pedA, pedB, leaderIndex, reason)
    if not enabled or not isIntegerInRange(relationId, 1, 2147483647) or
        not isIntegerInRange(relationEpoch, 1, 2147483647) or not isElement(pedA) or not isElement(pedB) or pedA == pedB or
        leaderIndex ~= false and not isIntegerInRange(leaderIndex, 1, 2) then
        return
    end
    local old = coupleAssignments[relationId]
    if old then
        if old.epoch == relationEpoch then
            if old.accepted then
                reportCouple(old, "accepted", {
                    leaderIndex = old.leaderIndex,
                    walkSpeeds = old.walkSpeeds,
                    nativeDiagnostic = captureCoupleDiagnostic(old) or nil,
                })
            else
                beginCoupleAssignment(old)
            end
            return
        end
        releaseCoupleAssignment(old, true)
    end

    local task = {
        id = relationId,
        epoch = relationEpoch,
        peds = {pedA, pedB},
        leaderIndex = leaderIndex ~= false and leaderIndex or nil,
        reason = reason,
        requestedAt = getTickCount(),
        accepted = false,
        leases = {},
    }
    coupleAssignments[relationId] = task
    coupleByPed[pedA] = task
    coupleByPed[pedB] = task
    beginCoupleAssignment(task)
end)

addEvent("pedTraffic:assignCouplePresentation", true)
addEventHandler("pedTraffic:assignCouplePresentation", resourceRoot, function(relationId, relationEpoch, pedA, pedB, reason, sideA, sideB)
    if not enabled or not isIntegerInRange(relationId, 1, 2147483647) or
        not isIntegerInRange(relationEpoch, 1, 2147483647) or not isElement(pedA) or not isElement(pedB) or pedA == pedB then
        return
    end
    local old = coupleAssignments[relationId]
    if old then
        if old.epoch == relationEpoch and old.presentation then
            if (sideA == 1 or sideA == 2) and (sideB == 1 or sideB == 2) then
                old.sideA, old.sideB = sideA, sideB
            end
            if not old.accepted then beginCoupleAssignment(old) end
            return
        end
        releaseCoupleAssignment(old, true)
    end
    local task = {
        id = relationId,
        epoch = relationEpoch,
        peds = {pedA, pedB},
        reason = reason,
        requestedAt = getTickCount(),
        accepted = false,
        presentation = true,
        sideA = (sideA == 1 or sideA == 2) and sideA or 0,
        sideB = (sideB == 1 or sideB == 2) and sideB or 0,
        leases = {},
    }
    coupleAssignments[relationId] = task
    coupleByPed[pedA] = task
    coupleByPed[pedB] = task
    beginCoupleAssignment(task)
end)

addEvent("pedTraffic:updateCouplePresentationSides", true)
addEventHandler("pedTraffic:updateCouplePresentationSides", resourceRoot, function(relationId, relationEpoch, sideA, sideB)
    local task = coupleAssignments[relationId]
    if not task or not task.presentation or task.epoch ~= relationEpoch or
        (sideA ~= 1 and sideA ~= 2) or (sideB ~= 1 and sideB ~= 2) then
        return
    end
    task.sideA, task.sideB = sideA, sideB
end)

addEvent("pedTraffic:revokeCouple", true)
addEventHandler("pedTraffic:revokeCouple", resourceRoot, function(relationId, relationEpoch, reason)
    local task = coupleAssignments[relationId]
    if not task or task.epoch ~= relationEpoch then
        return
    end
    local diagnostic = captureCoupleDiagnostic(task)
    releaseCoupleAssignment(task, true)
    reportCouple(task, "released", {reason = reason, nativeDiagnostic = diagnostic or nil})
end)

local function collectCoupleTestTasks(ped)
    local tasks = {}
    local hasCouple = false
    local hasWalkAlongside = false
    local hasWander = false
    if type(getPedTask) ~= "function" or not isElement(ped) then
        return tasks, hasCouple, hasWalkAlongside, hasWander
    end
    for slot = 0, 3 do
        local hierarchy = {getPedTask(ped, "primary", slot)}
        if hierarchy[1] ~= false then
            for _, taskName in ipairs(hierarchy) do
                tasks[#tasks + 1] = ("%d:%s"):format(slot, tostring(taskName))
                hasCouple = hasCouple or taskName == "TASK_COMPLEX_BE_IN_COUPLE"
                hasWalkAlongside = hasWalkAlongside or taskName == "TASK_COMPLEX_WALK_ALONGSIDE_PED"
                hasWander = hasWander or taskName == "TASK_COMPLEX_WANDER" or taskName == "TASK_COMPLEX_WANDER_STANDARD"
            end
        end
    end
    return tasks, hasCouple, hasWalkAlongside, hasWander
end

addEvent("pedTraffic:coupleTestSample", true)
addEventHandler("pedTraffic:coupleTestSample", resourceRoot, function(testId, sampleId, relationId, relationEpoch, pedA, pedB, phase)
    local function reportSample()
    local task = coupleAssignments[relationId]
    local diagnostic = task and task.epoch == relationEpoch and captureCoupleDiagnostic(task) or false
    local members = {}
    for index, ped in ipairs({pedA, pedB}) do
        local tasks, hasCouple, hasWalkAlongside, hasWander = collectCoupleTestTasks(ped)
        local x, y, z = false, false, false
        local vx, vy, vz = false, false, false
        local speed = false
        if isElement(ped) then
            x, y, z = getElementPosition(ped)
            vx, vy, vz = getElementVelocity(ped)
            speed = math.sqrt(vx * vx + vy * vy + vz * vz)
        end
        members[index] = {
            present = isElement(ped),
            trafficId = isElement(ped) and getElementData(ped, "neon:ambientPedTrafficId") or false,
            syncer = isElement(ped) and isElementSyncer(ped) or false,
            profilePresent = isElement(ped) and nativeEventProfiles[ped] ~= nil or false,
            profileActive = isElement(ped) and nativeEventProfiles[ped] ~= nil and
                isPedNativeEventProfileActive(ped, nativeEventProfiles[ped]) == true or false,
            hasCouple = hasCouple,
            hasWalkAlongside = hasWalkAlongside,
            hasWander = hasWander,
            moveState = isElement(ped) and getPedMoveState(ped) or false,
            x = x,
            y = y,
            z = z,
            vx = vx,
            vy = vy,
            vz = vz,
            speed = speed,
            tasks = tasks,
        }
    end
    local pairDistance = false
    if members[1].x and members[2].x then
        pairDistance = getDistanceBetweenPoints2D(members[1].x, members[1].y, members[2].x, members[2].y)
    end
    triggerServerEvent("pedTraffic:coupleTestSampleResult", resourceRoot, testId, sampleId, phase, {
        relationId = relationId,
        relationEpoch = relationEpoch,
        assignment = task ~= nil and task.epoch == relationEpoch and not task.presentation,
        assignmentAccepted = task ~= nil and task.epoch == relationEpoch and not task.presentation and task.accepted == true,
        presentation = task ~= nil and task.epoch == relationEpoch and task.presentation == true,
        presentationAccepted = task ~= nil and task.epoch == relationEpoch and task.presentation == true and task.accepted == true,
        presentationToken = task ~= nil and task.epoch == relationEpoch and task.presentation == true and task.token ~= nil and task.token ~= false,
        presentationStage = task ~= nil and task.epoch == relationEpoch and task.presentation == true and task.presentationStage or false,
        presentationSideA = task ~= nil and task.epoch == relationEpoch and task.presentation == true and task.sideA or false,
        presentationSideB = task ~= nil and task.epoch == relationEpoch and task.presentation == true and task.sideB or false,
        presentationActive = task ~= nil and task.epoch == relationEpoch and task.presentation == true and task.accepted == true and
            task.token ~= nil and isPedNativeCouplePresentationActive(task.token) == true or false,
        leaderIndex = task and not task.presentation and task.leaderIndex or false,
        active = diagnostic and diagnostic.active == true or false,
        diagnostic = diagnostic or false,
        pairDistance = pairDistance,
        members = members,
    })
    end

    if isElement(pedA) and isElement(pedB) then
        local ax, ay, az = getElementPosition(pedA)
        local bx, by, bz = getElementPosition(pedB)
        local midX, midY, midZ = (ax + bx) * 0.5, (ay + by) * 0.5, (az + bz) * 0.5
        -- The pair travels during both soaks. Keep it inside the observer's
        -- render set, then allow GTA one frame to materialize bone data before
        -- asking whether the retail PointArm presentation is active.
        setCameraMatrix(midX, midY - 8, midZ + 5, midX, midY, midZ + 0.8)
        setTimer(reportSample, 100, 1)
    else
        reportSample()
    end
end)

addEvent("pedTraffic:coupleTestForwardSocialEvent", true)
addEventHandler("pedTraffic:coupleTestForwardSocialEvent", resourceRoot, function(testId, relationId, relationEpoch, pedA)
    local function beginProbe()
        local task = coupleAssignments[relationId]
        local profileToken = isElement(pedA) and nativeEventProfiles[pedA] or false
        if not task or task.epoch ~= relationEpoch or task.presentation or not task.accepted or not task.token or
            not isElement(pedA) or not isElementSyncer(pedA) or not profileToken or
            not isPedNativeEventProfileActive(pedA, profileToken) or
            type(addPedNativeGunAimedAtEvent) ~= "function" then
            triggerServerEvent("pedTraffic:coupleTestForwardSocialEventResult", resourceRoot, testId, relationId, relationEpoch,
                               false, {reason = "couple-probe-not-ready"})
            return
        end

        local before = captureCoupleDiagnostic(task)
        local forwardedEventBefore = before and before.members and before.members[2] and
                                         before.members[2].forwardedGunAimedAtEventCount or 0
        task.forwardingProbe = true
        local playerX, playerY, playerZ = getElementPosition(localPlayer)
        local _, _, playerRotation = getElementRotation(localPlayer)
        local playerState = {
            x = playerX,
            y = playerY,
            z = playerZ,
            rotation = playerRotation,
            interior = getElementInterior(localPlayer),
            dimension = getElementDimension(localPlayer),
            weaponSlot = getPedWeaponSlot(localPlayer),
        }
        setElementInterior(localPlayer, getElementInterior(pedA))
        setElementDimension(localPlayer, getElementDimension(pedA))
        local probeOffsets = {{0, -1.0}, {1.0, 0}, {0, 1.0}, {-1.0, 0}}
        local function positionProbeSource(attempt)
            if not isElement(pedA) then return false end
            local x, y, z = getElementPosition(pedA)
            local offset = probeOffsets[(attempt - 1) % #probeOffsets + 1]
            return setElementPosition(localPlayer, x + offset[1], y + offset[2], z, false) == true
        end
        positionProbeSource(1)

        local probe = {
            testId = testId,
            relationId = relationId,
            relationEpoch = relationEpoch,
            ped = pedA,
            attacker = localPlayer,
            playerState = playerState,
            forwardedEventBefore = forwardedEventBefore,
            sourceObserved = false,
            forwardedObserved = false,
            forwardedDiagnostic = false,
            eventType = 31,
        }
        coupleForwardSocialProbe = probe
        -- CEventGunAimedAt::AffectsPed requires its source to be inside the
        -- target's seeing range. The harness players normally stay remote to
        -- avoid perturbing locomotion, so bring the current owner beside the
        -- pair only for this post-soak event and restore it after the probe.
        local accepted = addPedNativeGunAimedAtEvent(pedA, localPlayer, profileToken) == true
        probe.sourceObserved = accepted
        log(("couple-forward-probe relation=%d epoch=%d accepted=%s stimulus=gun-aimed-at"):format(
                relationId, relationEpoch, tostring(accepted)))
        local attempts = 0
        local injectionAttempts = 1
        local function sampleForwarding()
            attempts = attempts + 1
            if not accepted and attempts % 4 == 0 and isElement(pedA) then
                injectionAttempts = injectionAttempts + 1
                positionProbeSource(injectionAttempts)
                accepted = addPedNativeGunAimedAtEvent(pedA, localPlayer, profileToken) == true
                probe.sourceObserved = accepted
            end
            local diagnostic = captureCoupleDiagnostic(task)
            local partner = diagnostic and diagnostic.members and diagnostic.members[2] or false
            local forwarded = probe.forwardedObserved or
                                  (partner and partner.forwardedGunAimedAtEventCount > forwardedEventBefore or false) or
                                  (partner and (partner.gunAimedAtEventPresent == true or partner.currentEventType == 31) or false)
            if forwarded or attempts >= 120 then
                cleanupCoupleForwardSocialProbe(probe)
                task.forwardingProbe = nil
                triggerServerEvent("pedTraffic:coupleTestForwardSocialEventResult", resourceRoot, testId, relationId,
                                   relationEpoch, forwarded, {
                                       accepted = accepted,
                                       injectionAttempts = injectionAttempts,
                                       sourceObserved = accepted,
                                       forwardedObserved = forwarded,
                                       eventType = probe.eventType,
                                       before = before or false,
                                       partner = partner or false,
                                       forwardedDiagnostic = probe.forwardedDiagnostic or false,
                                       diagnostic = diagnostic or false,
                })
                return
            end
            setTimer(sampleForwarding, 25, 1)
        end
        setTimer(sampleForwarding, 25, 1)
    end
    beginProbe()
end)

addEvent("pedTraffic:coupleTestSeparate", true)
addEventHandler("pedTraffic:coupleTestSeparate", resourceRoot, function(testId, relationId, relationEpoch, pedA, pedB)
    local task = coupleAssignments[relationId]
    if not task or task.epoch ~= relationEpoch or not task.accepted or not isElement(pedA) or not isElement(pedB) or
        not isElementSyncer(pedA) or not isElementSyncer(pedB) then
        triggerServerEvent("pedTraffic:coupleTestSeparated", resourceRoot, testId, relationId, relationEpoch, false)
        return
    end
    local x, y, z = getElementPosition(pedA)
    local moved = setElementPosition(pedB, x + 10.25, y, z, false)
    triggerServerEvent("pedTraffic:coupleTestSeparated", resourceRoot, testId, relationId, relationEpoch, moved == true)
end)

addEvent("pedTraffic:coupleTestCleanup", true)
addEventHandler("pedTraffic:coupleTestCleanup", resourceRoot, function(testId, relationId, trafficIds)
    local function reportCleanup(attempt)
        local assignmentPresent = coupleAssignments[relationId] ~= nil
        local elementPresent = false
        local profilePresent = false
        for _, ped in ipairs(getElementsByType("ped")) do
            local trafficId = tonumber(getElementData(ped, "neon:ambientPedTrafficId"))
            if trafficId and (trafficId == trafficIds[1] or trafficId == trafficIds[2]) then
                elementPresent = true
                break
            end
        end
        for ped in pairs(nativeEventProfiles) do
            local trafficId = isElement(ped) and tonumber(getElementData(ped, "neon:ambientPedTrafficId")) or false
            if trafficId and (trafficId == trafficIds[1] or trafficId == trafficIds[2]) then
                profilePresent = true
            end
        end
        if (elementPresent or assignmentPresent) and attempt < 20 then
            setTimer(reportCleanup, 100, 1, attempt + 1)
            return
        end
        triggerServerEvent("pedTraffic:coupleTestCleanupResult", resourceRoot, testId, relationId, {
            elementPresent = elementPresent,
            assignmentPresent = assignmentPresent,
            profilePresent = profilePresent,
        })
    end
    reportCleanup(1)
end)

addEvent("pedTraffic:assignGroup", true)
addEventHandler("pedTraffic:assignGroup", resourceRoot, function(groupId, epoch, peds, reason)
    if not enabled or not isIntegerInRange(groupId, 1, 2147483647) or not isIntegerInRange(epoch, 1, 2147483647) or
        type(peds) ~= "table" or #peds < 2 or #peds > 4 then
        return
    end
    local old = groupAssignments[groupId]
    if old then
        if old.epoch == epoch then
            if old.accepted then
                reportGroup(old, "accepted")
            else
                beginGroupAssignment(old)
            end
            return
        end
        releaseGroupAssignment(old, true)
    end

    local task = {
        id = groupId,
        epoch = epoch,
        peds = {},
        leases = {},
        reason = reason,
        requestedAt = getTickCount(),
        accepted = false,
    }
    local seen = {}
    for index, ped in ipairs(peds) do
        if not isElement(ped) or getElementType(ped) ~= "ped" or seen[ped] or groupByPed[ped] then
            return
        end
        seen[ped] = true
        task.peds[index] = ped
    end
    groupAssignments[groupId] = task
    for _, ped in ipairs(task.peds) do groupByPed[ped] = task end
    beginGroupAssignment(task)
end)

addEvent("pedTraffic:revokeGroup", true)
addEventHandler("pedTraffic:revokeGroup", resourceRoot, function(groupId, epoch, reason)
    local task = groupAssignments[groupId]
    if not task or task.epoch ~= epoch then
        return
    end
    log(("group-revoke group=%d epoch=%d reason=%s"):format(groupId, epoch, tostring(reason)))
    -- The syncer owns GTA's live weapon inventory. Capture it before tearing
    -- down the native group so shots consumed during this epoch survive the
    -- authority transfer instead of being reset to the spawn metadata.
    local _, weaponStates = captureGroupWeaponState(task)
    releaseGroupAssignment(task, true)
    reportGroup(task, "released", {reason = reason, weapons = weaponStates})
end)

addEvent("pedTraffic:stopGroup", true)
addEventHandler("pedTraffic:stopGroup", resourceRoot, function(groupId, epoch, reason)
    local task = groupAssignments[groupId]
    if task and task.epoch == epoch then
        log(("group-stop group=%d epoch=%d reason=%s"):format(groupId, epoch, tostring(reason)))
        releaseGroupAssignment(task, true)
    end
end)

local function watchPhysicalTest(sessions, kind, ped, epoch, nonce, forceHandoff)
    if not isElement(ped) then
        return
    end

    local previous = sessions[ped]
    local sameTest = previous and previous.nonce == nonce
    local continuedAfterHandoff = sameTest and epoch > previous.epoch
    sessions[ped] = {
        ped = ped,
        epoch = epoch,
        nonce = nonce,
        forceHandoff = forceHandoff == true,
        startedAt = sameTest and previous.startedAt or getTickCount(),
        lastPhase = sameTest and previous.lastPhase or nil,
        -- A new owner may receive the physical StartSync state without
        -- exposing a local task leaf before contact. Advancing the same nonce
        -- to a newer epoch proves that the previous owner observed the target
        -- phase and that the server actually committed the forced handoff.
        sawActive = sameTest and (previous.sawActive or continuedAfterHandoff) or false,
        sawTarget = sameTest and (previous.sawTarget or continuedAfterHandoff) or false,
        startedLocally = sameTest and previous.startedLocally or false,
    }
    log(("%stest-watch id=%s epoch=%d nonce=%d handoff=%s"):format(
            kind, tostring(getElementData(ped, "neon:ambientPedTrafficId")), epoch, nonce, tostring(forceHandoff)))
end

local function dispatchPhysicalTest(sessions, kind, allowClimb, ped, epoch, nonce)
    local task = assignments[ped]
    local session = sessions[ped]
    local function reportFailure(reason)
        triggerServerEvent("pedTraffic:evidence", resourceRoot, ped, epoch, kind .. "test-phase",
                           {nonce = nonce, phase = "failed", reason = reason})
    end
    if not task or task.epoch ~= epoch or not session or session.nonce ~= nonce or not isElement(ped) or not isElementSyncer(ped) or
        type(setPedJump) ~= "function" then
        reportFailure("invalid-owner-or-api")
        return
    end

    local existingRoot = getJumpLifecycleState(ped)
    if existingRoot or (type(isPedOnGround) == "function" and not isPedOnGround(ped)) then
        reportFailure("ped-not-grounded")
        return
    end
    if not setPedJump(ped, allowClimb) then
        reportFailure("jump-dispatch-refused")
        return
    end
    session.startedLocally = true
    log(("%stest-dispatched id=%s epoch=%d nonce=%d"):format(
            kind, tostring(getElementData(ped, "neon:ambientPedTrafficId")), epoch, nonce), 3)
end

local function restoreWanderAfterPhysicalTest(ped, epoch)
    local task = assignments[ped]
    if not task or task.epoch ~= epoch or task.physicalRestorePending or not isElement(ped) or not isElementSyncer(ped) then
        return
    end

    -- A stopped diagnostic must not leave its ped idle, but restoring Wander
    -- while the native jump chain still owns the body would invalidate the
    -- test and can cut the landing short. Wait for genuine ground contact.
    task.physicalRestorePending = true
    local restoreStartedAt = getTickCount()
    local function restoreWander()
        if assignments[ped] ~= task or not isElement(ped) or not isElementSyncer(ped) then
            return
        end
        local _, leaf = getJumpLifecycleState(ped)
        local grounded = type(isPedOnGround) ~= "function" or isPedOnGround(ped)
        if (leaf or not grounded) and getTickCount() - restoreStartedAt < 6000 then
            clearTimer(task, "physicalRestoreTimer")
            task.physicalRestoreTimer = setTimer(restoreWander, 50, 1)
            return
        end
        task.physicalRestorePending = false
        if not leaf and grounded then
            setPedWander(ped, "walk", task.direction, true)
        end
    end
    restoreWander()
end

local function stopPhysicalTest(sessions, kind, ped, epoch, nonce, reason)
    local session = sessions[ped]
    if session and session.nonce == nonce then
        log(("%stest-stop id=%s epoch=%d nonce=%d reason=%s"):format(
                kind, tostring(isElement(ped) and getElementData(ped, "neon:ambientPedTrafficId") or false), session.epoch, nonce,
                tostring(reason)), 3)
        sessions[ped] = nil
    end

    if reason ~= "complete" then
        restoreWanderAfterPhysicalTest(ped, epoch)
    end
end

addEvent("pedTraffic:airTestWatch", true)
addEventHandler("pedTraffic:airTestWatch", resourceRoot, function(ped, epoch, nonce, forceHandoff)
    watchPhysicalTest(airTestSessions, "air", ped, epoch, nonce, forceHandoff)
end)

addEvent("pedTraffic:airTest", true)
addEventHandler("pedTraffic:airTest", resourceRoot, function(ped, epoch, nonce)
    dispatchPhysicalTest(airTestSessions, "air", false, ped, epoch, nonce)
end)

addEvent("pedTraffic:airTestStop", true)
addEventHandler("pedTraffic:airTestStop", resourceRoot, function(ped, epoch, nonce, reason)
    stopPhysicalTest(airTestSessions, "air", ped, epoch, nonce, reason)
end)

addEvent("pedTraffic:climbTestWatch", true)
addEventHandler("pedTraffic:climbTestWatch", resourceRoot, function(ped, epoch, nonce, forceHandoff)
    watchPhysicalTest(climbTestSessions, "climb", ped, epoch, nonce, forceHandoff)
end)

addEvent("pedTraffic:climbTest", true)
addEventHandler("pedTraffic:climbTest", resourceRoot, function(ped, epoch, nonce)
    dispatchPhysicalTest(climbTestSessions, "climb", true, ped, epoch, nonce)
end)

addEvent("pedTraffic:climbTestStop", true)
addEventHandler("pedTraffic:climbTestStop", resourceRoot, function(ped, epoch, nonce, reason)
    stopPhysicalTest(climbTestSessions, "climb", ped, epoch, nonce, reason)
end)

addEvent("pedTraffic:gunAimedAt", true)
addEventHandler("pedTraffic:gunAimedAt", resourceRoot, function(ped, aimingPed)
    local deadline = getTickCount() + 1000
    local function inject()
        local token = nativeEventProfiles[ped]
        if isElement(ped) and isElement(aimingPed) and token and isPedNativeEventProfileActive(ped, token) and
            type(addPedNativeGunAimedAtEvent) == "function" then
            local accepted = addPedNativeGunAimedAtEvent(ped, aimingPed, token)
            log(("gun-aim-bridge id=%s accepted=%s source=%s"):format(tostring(getElementData(ped, "neon:ambientPedTrafficId")),
                                                                       tostring(accepted), getPlayerName(aimingPed)))
            return
        end
        if getTickCount() < deadline then
            setTimer(inject, 100, 1)
        else
            log(("gun-aim-bridge id=%s accepted=false reason=owner-not-ready"):format(
                    tostring(isElement(ped) and getElementData(ped, "neon:ambientPedTrafficId") or false)), 2)
        end
    end
    inject()
end)

addEvent("pedTraffic:damageResponse", true)
addEventHandler("pedTraffic:damageResponse", resourceRoot, function(ped, attackingPed, weapon, bodypart, damageId)
    if damageId ~= false and not isIntegerInRange(damageId, 1, 2147483647) then
        return
    end
    if damageId ~= false then
        local now = getTickCount()
        for receiptId, receivedAt in pairs(nativeDamageReplayReceipts) do
            if now - receivedAt > 10000 then
                nativeDamageReplayReceipts[receiptId] = nil
            end
        end
        if nativeDamageReplayReceipts[damageId] then
            log(("damage-bridge id=%s damage=%d skipped=duplicate"):format(
                    tostring(isElement(ped) and getElementData(ped, "neon:ambientPedTrafficId") or false), damageId))
            return
        end
        nativeDamageReplayReceipts[damageId] = now
    end
    local deadline = getTickCount() + 1000
    local function inject()
        if not isElement(ped) or not isElement(attackingPed) then
            return
        end
        if consumeNativeDamage(ped, attackingPed, weapon, bodypart) then
            log(("damage-bridge id=%s damage=%s skipped=local-native source=%s weapon=%s bodypart=%s"):format(
                    tostring(getElementData(ped, "neon:ambientPedTrafficId")), tostring(damageId), getPlayerName(attackingPed),
                    tostring(weapon), tostring(bodypart)))
            return
        end

        local token = nativeEventProfiles[ped]
        if isElement(ped) and isElement(attackingPed) and token and isPedNativeEventProfileActive(ped, token) and
            type(addPedNativeDamageResponseEvent) == "function" then
            local accepted = addPedNativeDamageResponseEvent(ped, attackingPed, weapon, bodypart, token)
            log(("damage-bridge id=%s damage=%s accepted=%s source=%s weapon=%s bodypart=%s"):format(
                    tostring(getElementData(ped, "neon:ambientPedTrafficId")), tostring(damageId), tostring(accepted),
                    getPlayerName(attackingPed), tostring(weapon), tostring(bodypart)))
            return
        end
        if getTickCount() < deadline then
            setTimer(inject, 100, 1)
        else
            log(("damage-bridge id=%s damage=%s accepted=false reason=owner-not-ready"):format(
                    tostring(isElement(ped) and getElementData(ped, "neon:ambientPedTrafficId") or false), tostring(damageId)), 2)
        end
    end
    -- Remote bullet replay normally creates GTA's real CEventDamage on the
    -- owner too. Give that event a bounded interval to arrive and use the
    -- bridge only as a fallback, avoiding two personality responses.
    setTimer(inject, 100, 1)
end)

addEvent("pedTraffic:nativePlayerDamage", true)
addEventHandler("pedTraffic:nativePlayerDamage", resourceRoot, function(attackingPed, epoch, nonce, weapon, bodypart, damageFactor, direction)
    if not enabled or not isElement(attackingPed) or getElementType(attackingPed) ~= "ped" or
        getElementData(attackingPed, "neon:ambientPedTraffic") ~= true or not isIntegerInRange(epoch, 1, 2147483647) or
        getElementData(attackingPed, "neon:ambientPedTrafficEpoch") ~= epoch or
        not isIntegerInRange(nonce, 1, 2147483647) or not isIntegerInRange(weapon, 0, 46) or
        trafficNativeDamageWeapons[weapon] ~= true or
        (bodypart ~= 0 and not isIntegerInRange(bodypart, 3, 9)) or not isIntegerInRange(damageFactor, 1, 200) or
        not isIntegerInRange(direction, 0, 3) or type(addPedNativeDamageEvent) ~= "function" then
        return
    end

    -- A reliable hit from an older owner must not cross a completed handoff.
    -- The new syncer can generate its own native collision, which would make
    -- replaying the old epoch a double hit.
    if isElementSyncer(attackingPed) then
        return
    end

    local previous = nativePlayerDamageReceipts[attackingPed]
    if previous and (epoch < previous.epoch or (epoch == previous.epoch and nonce <= previous.nonce)) then
        return
    end

    local token = nativeEventProfiles[attackingPed]
    if not token then
        return
    end

    nativePlayerDamageReceipts[attackingPed] = {epoch = epoch, nonce = nonce}
    local accepted = addPedNativeDamageEvent(localPlayer, attackingPed, weapon, bodypart, damageFactor, direction, token)
    log(("native-player-damage id=%s epoch=%d nonce=%d accepted=%s weapon=%d bodypart=%d factor=%d direction=%d health=%.2f"):format(
            tostring(getElementData(attackingPed, "neon:ambientPedTrafficId")), epoch, nonce, tostring(accepted), weapon,
            bodypart, damageFactor, direction, getElementHealth(localPlayer)), accepted and 3 or 2)
end)

addEventHandler("onClientPedDamage", root, function(attacker, weapon, bodypart)
    if not enabled or getElementData(source, "neon:ambientPedTraffic") ~= true then
        return
    end

    -- Death remains entirely in MTA's physical/wasted pipeline. Replaying a
    -- non-fatal personality event for a zero-health ped would create a short
    -- flee task in the network delay before the owner receives the death.
    if getElementHealth(source) <= 0 then
        return
    end

    local probe = coupleForwardSocialProbe
    if probe and probe.eventType == 9 and source == probe.ped and attacker == probe.attacker then
        probe.damageObserved = true
        probe.weapon = weapon
        probe.bodypart = bodypart
        local task = coupleAssignments[probe.relationId]
        local diagnostic = task and task.epoch == probe.relationEpoch and captureCoupleDiagnostic(task) or false
        local partner = diagnostic and diagnostic.members and diagnostic.members[2] or false
        local baseline = probe.forwardedDamageBefore or 0
        probe.forwardedObserved = partner and
                                      (partner.forwardedDamageEventCount > baseline or partner.damageEventPresent == true or
                                          partner.currentEventType == 9) or false
        probe.forwardedDiagnostic = diagnostic or false
        log(("couple-forward-damage relation=%d epoch=%d weapon=%s bodypart=%s forwarded=%s"):format(
                probe.relationId, probe.relationEpoch, tostring(weapon), tostring(bodypart),
                tostring(probe.forwardedObserved)))
    end

    local token = nativeEventProfiles[source]
    if token and isPedNativeEventProfileActive(source, token) then
        rememberNativeDamage(source, attacker, weapon, bodypart)
        traceNativeDamageSource(source, attacker, weapon, bodypart, "owner")
        log(("damage-observed id=%s role=owner weapon=%s bodypart=%s"):format(
                tostring(getElementData(source, "neon:ambientPedTrafficId")), tostring(weapon), tostring(bodypart)))
        return
    end

    if attacker ~= localPlayer then
        return
    end

    if not token or isPedNativeEventProfileActive(source, token) then
        return
    end

    traceNativeDamageSource(source, attacker, weapon, bodypart, "observer")
    log(("damage-observed id=%s role=observer weapon=%s bodypart=%s"):format(
            tostring(getElementData(source, "neon:ambientPedTrafficId")), tostring(weapon), tostring(bodypart)))
    nextObservedDamageNonce = nextObservedDamageNonce % 2147483647 + 1
    triggerServerEvent("pedTraffic:damageObserved", resourceRoot, source, weapon, bodypart, nextObservedDamageNonce)
end)

addEventHandler("onClientPlayerNativeDamageAttempt", root, function(attacker, weapon, bodypart, damageFactor, direction)
    if not enabled or source == localPlayer or not isElement(attacker) or getElementType(attacker) ~= "ped" or
        getElementData(attacker, "neon:ambientPedTraffic") ~= true or not isElementSyncer(attacker) then
        return
    end

    local token = nativeEventProfiles[attacker]
    if not token or not isPedNativeEventProfileActive(attacker, token) then
        return
    end

    local task = assignments[attacker] or groupByPed[attacker]
    if not task or not task.accepted or not isIntegerInRange(task.epoch, 1, 2147483647) then
        return
    end

    -- Only CWeapon::GenerateDamageEvent carries the exact pre-calculator
    -- factor. Refuse to guess from loss: armour, victim stats and friendly-fire
    -- modifiers make that value non-invertible.
    if not isIntegerInRange(damageFactor, 1, 200) or not isIntegerInRange(direction, 0, 3) then
        log(("native-player-damage-observed id=%s accepted=false reason=missing-native-factor weapon=%s bodypart=%s factor=%s direction=%s"):format(
                tostring(getElementData(attacker, "neon:ambientPedTrafficId")), tostring(weapon), tostring(bodypart),
                tostring(damageFactor), tostring(direction)), 2)
        return
    end

    nextNativePlayerDamageNonce = nextNativePlayerDamageNonce % 2147483647 + 1
    log(("native-player-damage-observed id=%s epoch=%d nonce=%d victim=%s weapon=%s bodypart=%s factor=%d direction=%d"):format(
            tostring(getElementData(attacker, "neon:ambientPedTrafficId")), task.epoch, nextNativePlayerDamageNonce,
            getPlayerName(source), tostring(weapon), tostring(bodypart), damageFactor, direction))
    triggerServerEvent("pedTraffic:nativePlayerDamageObserved", resourceRoot, attacker, source, task.epoch,
                       nextNativePlayerDamageNonce, weapon, bodypart, damageFactor, direction)
end)

addEventHandler("onClientPedNativeBikeJackAttempt", root, function(targetPlayer, vehicle, targetDoor, secondaryPassenger)
    local attackingPed = source
    if not enabled or not isElement(attackingPed) or getElementType(attackingPed) ~= "ped" or
        getElementData(attackingPed, "neon:ambientPedTraffic") ~= true or not isElementSyncer(attackingPed) or
        not isElement(targetPlayer) or getElementType(targetPlayer) ~= "player" or not isElement(vehicle) or
        getElementType(vehicle) ~= "vehicle" or getVehicleType(vehicle) ~= "Bike" or
        not isIntegerInRange(targetDoor, 8, 18) or
        (targetDoor ~= 8 and targetDoor ~= 9 and targetDoor ~= 10 and targetDoor ~= 11 and targetDoor ~= 18) or
        (secondaryPassenger ~= false and (not isElement(secondaryPassenger) or getElementType(secondaryPassenger) ~= "player")) then
        return
    end

    local token = nativeEventProfiles[attackingPed]
    if not token or not isPedNativeEventProfileActive(attackingPed, token) then
        return
    end

    local task = assignments[attackingPed] or groupByPed[attackingPed]
    if not task or not task.accepted or not isIntegerInRange(task.epoch, 1, 2147483647) then
        return
    end

    nextNativeBikeJackNonce = nextNativeBikeJackNonce % 2147483647 + 1
    log(("native-bike-jack-observed id=%s epoch=%d nonce=%d target=%s door=%d passenger=%s"):format(
            tostring(getElementData(attackingPed, "neon:ambientPedTrafficId")), task.epoch, nextNativeBikeJackNonce,
            getPlayerName(targetPlayer), targetDoor,
            isElement(secondaryPassenger) and getPlayerName(secondaryPassenger) or "none"))
    triggerServerEvent("pedTraffic:nativeBikeJackObserved", resourceRoot, attackingPed, targetPlayer, vehicle, task.epoch,
                       nextNativeBikeJackNonce, targetDoor, secondaryPassenger)
end)

addEvent("pedTraffic:nativeBikeJack", true)
addEventHandler("pedTraffic:nativeBikeJack", resourceRoot,
                function(attackingPed, vehicle, epoch, nonce, door, draggedDownTime, primaryVictim, expectedSeat)
    local function reportResult(accepted, reason)
        local currentSeat = isElement(vehicle) and getPedOccupiedVehicle(localPlayer) == vehicle and
                                getPedOccupiedVehicleSeat(localPlayer) or -1
        triggerServerEvent("pedTraffic:nativeBikeJackResult", resourceRoot, attackingPed, epoch, nonce,
                           accepted == true, reason, currentSeat)
    end

    if not enabled or not isElement(attackingPed) or getElementType(attackingPed) ~= "ped" or
        getElementData(attackingPed, "neon:ambientPedTraffic") ~= true or
        not isIntegerInRange(epoch, 1, 2147483647) or getElementData(attackingPed, "neon:ambientPedTrafficEpoch") ~= epoch or
        not isIntegerInRange(nonce, 1, 2147483647) or not isElement(vehicle) or getElementType(vehicle) ~= "vehicle" or
        getVehicleType(vehicle) ~= "Bike" or not isIntegerInRange(door, 10, 11) or
        not isIntegerInRange(draggedDownTime, 0, 200) or
        (draggedDownTime ~= 0 and draggedDownTime ~= 200) or type(primaryVictim) ~= "boolean" or
        not isIntegerInRange(expectedSeat, 0, 1) then
        return
    end

    local previous = nativeBikeJackReceipts[attackingPed]
    if previous and (epoch < previous.epoch or epoch == previous.epoch and nonce <= previous.nonce) then
        reportResult(false, "duplicate")
        return
    end

    local token = nativeEventProfiles[attackingPed]
    if not token then
        reportResult(false, "lease-missing")
        return
    end
    if getElementHealth(localPlayer) <= 0 or getPedOccupiedVehicle(localPlayer) ~= vehicle or
        getPedOccupiedVehicleSeat(localPlayer) ~= expectedSeat or getElementDimension(localPlayer) ~= getElementDimension(vehicle) or
        getElementInterior(localPlayer) ~= getElementInterior(vehicle) then
        reportResult(false, "occupant-state-changed")
        return
    end
    if type(addPedNativeBikeJackTask) ~= "function" then
        reportResult(false, "native-api-unavailable")
        return
    end

    nativeBikeJackReceipts[attackingPed] = {epoch = epoch, nonce = nonce}
    local accepted = addPedNativeBikeJackTask(localPlayer, attackingPed, vehicle, door, draggedDownTime, primaryVictim, token)
    log(("native-bike-jack-replay id=%s epoch=%d nonce=%d accepted=%s seat=%d door=%d down=%d primary=%s"):format(
            tostring(getElementData(attackingPed, "neon:ambientPedTrafficId")), epoch, nonce, tostring(accepted), expectedSeat,
            door, draggedDownTime, tostring(primaryVictim)), accepted and 3 or 2)
    reportResult(accepted == true, accepted and "accepted" or "native-task-refused")
end)

local function getActiveGunAimTarget()
    if not enabled or not getPedControlState(localPlayer, "aim_weapon") then
        return false, false
    end

    -- MTA continuously raycasts GetShotData even while the player is not
    -- aiming. Require the real control state and a weapon GTA treats as a
    -- firearm before proposing a CEventGunAimedAt to the remote owner.
    local weapon = getPedWeapon(localPlayer)
    if weapon < 22 or weapon > 39 then
        return false, false
    end

    local target = getPedTarget(localPlayer)
    if not isElement(target) or getElementType(target) ~= "ped" or
        getElementData(target, "neon:ambientPedTraffic") ~= true then
        return true, false
    end
    return true, target
end

addEventHandler("onClientPreRender", root, function()
    local now = getTickCount()
    for _, task in pairs(coupleAssignments) do
        if task.presentation and task.accepted and task.token and type(updatePedNativeCouplePresentation) == "function" then
            if not updatePedNativeCouplePresentation(task.token, task.sideA or 0, task.sideB or 0) then
                -- Streaming and ownership can invalidate a presentation task
                -- for one frame. Reacquire locally within a bounded window;
                -- dropping the assignment here would make the loss permanent.
                releasePedNativeCouplePresentation(task.token)
                task.token = nil
                task.accepted = false
                task.presentationStage = "update-retry"
                task.presentationRetryStartedAt = task.presentationRetryStartedAt or now
                task.requestedAt = task.presentationRetryStartedAt
                clearTimer(task, "retryTimer")
                task.retryTimer = setTimer(function() beginCoupleAssignment(task) end, 100, 1)
            end
        end
    end
    for ped, fade in pairs(spawnFades) do
        if not isElement(ped) then
            spawnFades[ped] = nil
        else
            local progress = math.min(1, (now - fade.startedAt) / fade.duration)
            setElementAlpha(ped, math.floor(progress * 255 + 0.5))
            if progress >= 1 then
                spawnFades[ped] = nil
            end
        end
    end

    local aimActive, aimTarget = getActiveGunAimTarget()
    if not aimActive then
        observedAimTargets = {}
    elseif aimTarget then
        if isElementSyncer(aimTarget) then
            -- Re-arm this target while its native event is handled locally. If
            -- ownership moves away during the same aim hold, the new owner must
            -- still receive one bridge event.
            observedAimTargets[aimTarget] = nil
        elseif not observedAimTargets[aimTarget] then
            -- A target ray can briefly disappear while the aim button stays
            -- held. Relay each remotely-owned traffic ped only once per real
            -- aim session so jitter cannot restart its native threat response.
            observedAimTargets[aimTarget] = true
            log(("gun-aim-observed id=%s role=observer"):format(
                    tostring(getElementData(aimTarget, "neon:ambientPedTrafficId"))))
            triggerServerEvent("pedTraffic:gunAimObserved", resourceRoot, aimTarget)
        end
    end

    updateLocalPopulationModels()
end)

addEventHandler("onClientElementDestroy", root, function()
    spawnFades[source] = nil
    observedAimTargets[source] = nil
    nativeDamageObservations[source] = nil
    nativeDamageTraceTimes[source] = nil
    nativePlayerDamageReceipts[source] = nil
    nativeBikeJackReceipts[source] = nil
    vehicleReactionStates[source] = nil
    airTestSessions[source] = nil
    climbTestSessions[source] = nil
    local groupTask = groupByPed[source]
    if groupTask then releaseGroupAssignment(groupTask, true) end
    local coupleTask = coupleByPed[source]
    if coupleTask then releaseCoupleAssignment(coupleTask, true) end
    local task = assignments[source]
    if task then releaseTask(task, false) end
    releaseTrafficEventProfile(source)
end)

addEventHandler("onClientElementDataChange", root, function(dataName)
    if getElementType(source) ~= "ped" then
        return
    end

    if dataName == "neon:ambientPedTraffic" and getElementData(source, dataName) == true then
        acquireTrafficEventProfile(source)
    elseif dataName == "neon:ambientPedTraffic" then
        releaseTrafficEventProfile(source)
    elseif dataName == "neon:ambientPedPopulationClass" and getElementData(source, "neon:ambientPedTraffic") == true then
        acquireTrafficEventProfile(source)
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    cleanupCoupleForwardSocialProbe(coupleForwardSocialProbe)
    stopResidencyObservation()
    airTestSessions = {}
    climbTestSessions = {}
    nativeDamageTraceTimes = {}
    nativeDamageReplayReceipts = {}
    nativePlayerDamageReceipts = {}
    nativeBikeJackReceipts = {}
    local couples = {}
    for _, task in pairs(coupleAssignments) do couples[#couples + 1] = task end
    for _, task in ipairs(couples) do releaseCoupleAssignment(task, true) end
    local groups = {}
    for _, task in pairs(groupAssignments) do groups[#groups + 1] = task end
    for _, task in ipairs(groups) do releaseGroupAssignment(task, true) end
    local current = {}
    for _, task in pairs(assignments) do current[#current + 1] = task end
    for _, task in ipairs(current) do releaseTask(task, true) end
    local profiled = {}
    for ped in pairs(nativeEventProfiles) do profiled[#profiled + 1] = ped end
    for _, ped in ipairs(profiled) do releaseTrafficEventProfile(ped) end
    if type(resetAmbientPedPopulationModels) == "function" then
        resetAmbientPedPopulationModels()
    end
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    for _, ped in ipairs(getElementsByType("ped")) do
        acquireTrafficEventProfile(ped)
    end
    triggerServerEvent("pedTraffic:ready", resourceRoot)
end)

local function monitorPhysicalTestSessions(sessions, kind, targetPhase, maximumAge)
    for ped, session in pairs(sessions) do
        if not isElement(ped) or getTickCount() - session.startedAt > maximumAge then
            sessions[ped] = nil
        else
            local rootTask, leafTask = getJumpLifecycleState(ped)
            local phase = classifyJumpLifecyclePhase(leafTask)
            local owner = isElementSyncer(ped)
            local grounded = type(isPedOnGround) == "function" and isPedOnGround(ped) or false
            if phase ~= "none" then
                session.sawActive = true
                if owner then
                    session.sawLocalLifecycle = true
                end
            end
            if phase == targetPhase then
                session.sawTarget = true
            end

            if session.lastPhase ~= phase then
                local x, y, z = getElementPosition(ped)
                local vx, vy, vz = getElementVelocity(ped)
                log(("%stest-transition id=%s epoch=%d nonce=%d role=%s phase=%s task=%s/%s grounded=%s pos=(%.3f,%.3f,%.3f) velocity=(%.4f,%.4f,%.4f)"):format(
                        kind, tostring(getElementData(ped, "neon:ambientPedTrafficId")), session.epoch, session.nonce,
                        owner and "owner" or "observer", phase, tostring(rootTask), tostring(leafTask), tostring(grounded), x, y, z, vx, vy, vz), 3)
                session.lastPhase = phase

                local assignment = assignments[ped]
                if phase == targetPhase then
                    session.targetObservedAt = getTickCount()
                    session.targetReported = false
                elseif owner and assignment and assignment.epoch == session.epoch and phase ~= "none" then
                    triggerServerEvent("pedTraffic:evidence", resourceRoot, ped, session.epoch, kind .. "test-phase",
                                       {nonce = session.nonce, phase = phase})
                end
            end

            -- Keep at least one observation interval after the target edge so
            -- its reliable physical semantic can reach StartSync. GTA may
            -- enter SIMPLE_LAND quickly, so the airborne test keeps that early
            -- landing window eligible; climb must still be actively anchored.
            local handoffWindow = phase == targetPhase or (kind == "air" and phase == "land" and not grounded)
            local observationDelay = kind == "climb" and 250 or 50
            if owner and handoffWindow and not session.targetReported and session.targetObservedAt and
                getTickCount() - session.targetObservedAt >= observationDelay then
                local assignment = assignments[ped]
                if assignment and assignment.epoch == session.epoch then
                    session.targetReported = true
                    triggerServerEvent("pedTraffic:evidence", resourceRoot, ped, session.epoch, kind .. "test-phase",
                                       {nonce = session.nonce, phase = targetPhase})
                end
            end

            local lifecycleSettled = hasJumpLifecycleSettled(session, leafTask, "sawLocalLifecycle", "clearSince")
            if owner and lifecycleSettled and not session.completeSent then
                local assignment = assignments[ped]
                if assignment and assignment.epoch == session.epoch and assignment.accepted then
                    if kind == "climb" and not session.sawTarget then
                        session.completeSent = true
                        triggerServerEvent("pedTraffic:evidence", resourceRoot, ped, session.epoch, "climbtest-phase",
                                           {nonce = session.nonce, phase = "failed", reason = "climb-not-entered"})
                    elseif session.startedLocally and not assignment.resumePhysical and
                        not setPedWander(ped, "walk", assignment.direction, true) then
                        triggerServerEvent("pedTraffic:evidence", resourceRoot, ped, session.epoch, kind .. "test-phase",
                                           {nonce = session.nonce, phase = "failed", reason = "wander-restore-refused"})
                    else
                        session.completeSent = true
                        triggerServerEvent("pedTraffic:evidence", resourceRoot, ped, session.epoch, kind .. "test-phase",
                                           {nonce = session.nonce, phase = "complete"})
                    end
                end
            end
        end
    end
end

setTimer(function()
    monitorPhysicalTestSessions(airTestSessions, "air", "in_air", 8000)
    monitorPhysicalTestSessions(climbTestSessions, "climb", "climb", 12000)
end, 50, 0)

setTimer(function()
    if debugEnabled then
        local active = 0
        local activeGroups = 0
        local profiles = 0
        for _, task in pairs(assignments) do
            if task.accepted then active = active + 1 end
        end
        for _, task in pairs(groupAssignments) do
            if task.accepted then
                activeGroups = activeGroups + 1
                active = active + #task.peds
            end
        end
        for _ in pairs(nativeEventProfiles) do profiles = profiles + 1 end
        log(("telemetry active=%d groups=%d profiles=%d worldReady=%s preset=%s revision=%d hits=%d misses=%d assignments=%d groupAssignments=%d failures=%d groupFailures=%d missReasons=%s"):format(
                active, activeGroups, profiles, tostring(populationWorldReady), populationWorldPreset, populationWorldRevision,
                stats.candidateHits, stats.candidateMisses, stats.assignments, stats.groupAssignments, stats.failures,
                stats.groupFailures, formatReasons(stats.missReasons)))
    end
end, 10000, 0)

setTimer(function()
    if not debugEnabled then
        return
    end

    for _, ped in ipairs(getElementsByType("ped")) do
        if getElementData(ped, "neon:ambientPedTraffic") == true then
            local id = tonumber(getElementData(ped, "neon:ambientPedTrafficId")) or -1
            local x, y, z = getElementPosition(ped)
            local groundZ = getGroundPosition(x, y, z)
            local _, _, heading = getElementRotation(ped)
            local vx, vy, vz = getElementVelocity(ped)
            local horizontalSpeed = math.sqrt(vx * vx + vy * vy)
            local moveState = type(getPedMoveState) == "function" and getPedMoveState(ped) or "unavailable"
            local nativeStyle = type(isPedUsingNativeWalkingStyle) == "function" and isPedUsingNativeWalkingStyle(ped) or false
            local profileToken = nativeEventProfiles[ped]
            local profileRole = profileToken and isPedNativeEventProfileActive(ped, profileToken) and "owner" or
                                    (profileToken and "observer" or "missing")
            local groupId = getElementData(ped, "neon:ambientPedGroupId") or "none"
            local groupRole = getElementData(ped, "neon:ambientPedGroupRole") or "none"
            local threatRoot, threatLeaf = getThreatState(ped)
            local vehicleRoot, vehicleLeaf = getVehicleReactionState(ped)
            log(("ped id=%d model=%d group=%s/%s syncer=%s streamed=%s profile=%s avoid=%s threat=%s/%s vehicle=%s/%s nativeStyle=%s move=%s speed=%.5f velocity=(%.5f,%.5f,%.5f) pos=(%.2f,%.2f,%.2f) groundZ=%.2f heading=%.1f"):format(
                    id, getElementModel(ped), tostring(groupId), tostring(groupRole), tostring(isElementSyncer(ped)),
                    tostring(isElementStreamedIn(ped)), profileRole, tostring(getAvoidanceState(ped)), tostring(threatRoot),
                    tostring(threatLeaf), tostring(vehicleRoot), tostring(vehicleLeaf), tostring(nativeStyle), tostring(moveState),
                    horizontalSpeed, vx, vy, vz, x, y, z, groundZ, heading))
        end
    end
end, 2000, 0)

addEventHandler("onClientElementStreamIn", root, function()
    if not debugEnabled or getElementType(source) ~= "ped" or
        getElementData(source, "neon:ambientPedTraffic") ~= true then
        return
    end

    local id = tonumber(getElementData(source, "neon:ambientPedTrafficId")) or -1
    local groupId = getElementData(source, "neon:ambientPedGroupId") or "none"
    local x, y, z = getElementPosition(source)
    local _, _, heading = getElementRotation(source)
    log(("stream-in id=%d group=%s syncer=%s pos=(%.2f,%.2f,%.2f) heading=%.1f"):format(
            id, tostring(groupId), tostring(isElementSyncer(source)), x, y, z, heading))
end)

setTimer(function()
    if not debugEnabled then
        return
    end

    for ped, token in pairs(nativeEventProfiles) do
        if isElement(ped) and isElementStreamedIn(ped) then
            local id = tonumber(getElementData(ped, "neon:ambientPedTrafficId")) or -1
            local role = isPedNativeEventProfileActive(ped, token) and "owner" or "observer"
            local avoidanceActive = getAvoidanceState(ped)
            if avoidanceStates[ped] ~= nil and avoidanceStates[ped] ~= avoidanceActive then
                log(("avoid-transition id=%d role=%s active=%s"):format(id, role, tostring(avoidanceActive)))
            end
            avoidanceStates[ped] = avoidanceActive

            local threatRoot, threatLeaf = getThreatState(ped)
            local threatSignature = threatRoot and (threatRoot .. "/" .. threatLeaf) or "none"
            if threatStates[ped] ~= threatSignature and (threatStates[ped] ~= nil or threatSignature ~= "none") then
                log(("threat-transition id=%d role=%s state=%s"):format(id, role, threatSignature))
            end
            threatStates[ped] = threatSignature

            local vehicleRoot, vehicleLeaf = getVehicleReactionState(ped)
            local vehicleSignature = vehicleRoot and (vehicleRoot .. "/" .. vehicleLeaf) or "none"
            if vehicleReactionStates[ped] ~= vehicleSignature and
                (vehicleReactionStates[ped] ~= nil or vehicleSignature ~= "none") then
                log(("vehicle-transition id=%d role=%s state=%s"):format(id, role, vehicleSignature))
            end
            vehicleReactionStates[ped] = vehicleSignature

            local health = getElementHealth(ped)
            local previousHealth = healthStates[ped]
            if previousHealth and health < previousHealth - 0.01 then
                log(("damage-transition id=%d role=%s health=%.2f delta=%.2f response=%s"):format(
                        id, role, health, health - previousHealth, threatSignature))
            end
            healthStates[ped] = health
        end
    end
end, 250, 0)
