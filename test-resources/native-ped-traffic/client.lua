local enabled = false
local debugEnabled = false
local populationWorldReady = false
local populationWorldRevision = 0
local populationWorldPreset = "none"
local assignments = {}
local nativeEventProfiles = {}
local avoidanceStates = {}
local threatStates = {}
local vehicleReactionStates = {}
local airTestSessions = {}
local climbTestSessions = {}
local healthStates = {}
local observedAimTargets = {}
local nativeDamageObservations = {}
local stats = {
    candidateHits = 0,
    candidateMisses = 0,
    assignments = 0,
    failures = 0,
    missReasons = {},
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
    if nativeEventProfiles[ped] then
        return true
    end
    if not isElement(ped) or getElementData(ped, "neon:ambientPedTraffic") ~= true or
        type(acquirePedNativeEventProfile) ~= "function" then
        return false
    end

    local token = acquirePedNativeEventProfile(ped, "ambient-wander")
    if not token then
        return false
    end
    nativeEventProfiles[ped] = token
    healthStates[ped] = getElementHealth(ped)
    log(("profile-acquired id=%s syncer=%s"):format(tostring(getElementData(ped, "neon:ambientPedTrafficId")),
                                                      tostring(isElementSyncer(ped))))
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
    avoidanceStates[ped] = nil
    threatStates[ped] = nil
    vehicleReactionStates[ped] = nil
    healthStates[ped] = nil
    nativeDamageObservations[ped] = nil
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

    if not isPedNativeEventProfileActive(task.ped, nativeEventProfiles[task.ped]) then
        return fail(task, "ambient-profile-inactive")
    end

    if not setPedUseNativeWalkingStyle(task.ped, true) then
        return fail(task, "native-walking-style-refused")
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
                releaseTask(task, false)
            end
        end, 1000, 0)
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
            installWander()
        end
        waitForPhysicalCompletion()
        return
    end

    installWander()
end

addEvent("pedTraffic:populationWorldState", true)
addEventHandler("pedTraffic:populationWorldState", resourceRoot, function(snapshot)
    local revision = type(snapshot) == "table" and snapshot.revision or false
    local success, reason = applyPopulationWorldState(snapshot)
    triggerServerEvent("pedTraffic:populationWorldApplied", resourceRoot, revision, {zones = success == true}, success, reason)
    if success then
        log(("population-world-applied revision=%d preset=%s"):format(populationWorldRevision, populationWorldPreset), 3)
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
        populationWorldReady = false
        airTestSessions = {}
        climbTestSessions = {}
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
addEventHandler("pedTraffic:candidateRequest", resourceRoot, function(requestId, worldRevision, populationClass, gang)
    local startedAt = getTickCount()
    if not enabled or not populationWorldReady or worldRevision ~= populationWorldRevision or getElementDimension(localPlayer) ~= 0 or
        getElementInterior(localPlayer) ~= 0 or isPedDead(localPlayer) or
        type(getAmbientPedSpawnCandidate) ~= "function" then
        stats.candidateMisses = stats.candidateMisses + 1
        triggerServerEvent("pedTraffic:candidate", resourceRoot, requestId, worldRevision, false, getTickCount() - startedAt, "world-not-ready")
        return
    end

    if populationClass ~= "civilian" and populationClass ~= "gang" or
        (populationClass == "gang" and (type(gang) ~= "number" or gang ~= math.floor(gang) or gang < 0 or gang > 7)) or
        (populationClass == "civilian" and gang ~= false) then
        stats.candidateMisses = stats.candidateMisses + 1
        triggerServerEvent("pedTraffic:candidate", resourceRoot, requestId, worldRevision, false, getTickCount() - startedAt,
                           "invalid-population-hint")
        return
    end

    local x, y, z = getElementPosition(localPlayer)
    local candidate, missReason = getAmbientPedSpawnCandidate(x, y, z, populationClass, populationClass == "gang" and gang or -1)
    if candidate then
        stats.candidateHits = stats.candidateHits + 1
    else
        stats.candidateMisses = stats.candidateMisses + 1
        countReason(stats.missReasons, missReason)
    end
    if debugEnabled then
        log(("candidate request=%d class=%s gang=%s result=%s model=%s elapsed=%d reason=%s"):format(
                requestId, populationClass, tostring(gang), tostring(candidate ~= false and candidate ~= nil),
                tostring(candidate and candidate.model), getTickCount() - startedAt, tostring(missReason)))
    end
    triggerServerEvent("pedTraffic:candidate", resourceRoot, requestId, worldRevision, candidate or false, getTickCount() - startedAt, missReason)
end)

setTimer(function()
    if not enabled or not populationWorldReady then
        return
    end

    if type(getAmbientPedPopulationProfile) ~= "function" then
        triggerServerEvent("pedTraffic:populationProfile", resourceRoot, false)
        return
    end

    local profile = getAmbientPedPopulationProfile()
    if type(profile) == "table" then
        profile.worldRevision = populationWorldRevision
    end
    triggerServerEvent("pedTraffic:populationProfile", resourceRoot, type(profile) == "table" and profile or false)
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
addEventHandler("pedTraffic:damageResponse", resourceRoot, function(ped, attackingPed, weapon, bodypart)
    local deadline = getTickCount() + 1000
    local function inject()
        if not isElement(ped) or not isElement(attackingPed) then
            return
        end
        if consumeNativeDamage(ped, attackingPed, weapon, bodypart) then
            log(("damage-bridge id=%s skipped=local-native source=%s weapon=%s bodypart=%s"):format(
                    tostring(getElementData(ped, "neon:ambientPedTrafficId")), getPlayerName(attackingPed), tostring(weapon),
                    tostring(bodypart)))
            return
        end

        local token = nativeEventProfiles[ped]
        if isElement(ped) and isElement(attackingPed) and token and isPedNativeEventProfileActive(ped, token) and
            type(addPedNativeDamageResponseEvent) == "function" then
            local accepted = addPedNativeDamageResponseEvent(ped, attackingPed, weapon, bodypart, token)
            log(("damage-bridge id=%s accepted=%s source=%s weapon=%s bodypart=%s"):format(
                    tostring(getElementData(ped, "neon:ambientPedTrafficId")), tostring(accepted), getPlayerName(attackingPed),
                    tostring(weapon), tostring(bodypart)))
            return
        end
        if getTickCount() < deadline then
            setTimer(inject, 100, 1)
        else
            log(("damage-bridge id=%s accepted=false reason=owner-not-ready"):format(
                    tostring(isElement(ped) and getElementData(ped, "neon:ambientPedTrafficId") or false)), 2)
        end
    end
    -- Remote bullet replay normally creates GTA's real CEventDamage on the
    -- owner too. Give that event a bounded interval to arrive and use the
    -- bridge only as a fallback, avoiding two personality responses.
    setTimer(inject, 100, 1)
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

    local token = nativeEventProfiles[source]
    if token and isPedNativeEventProfileActive(source, token) then
        rememberNativeDamage(source, attacker, weapon, bodypart)
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

    log(("damage-observed id=%s role=observer weapon=%s bodypart=%s"):format(
            tostring(getElementData(source, "neon:ambientPedTrafficId")), tostring(weapon), tostring(bodypart)))
    triggerServerEvent("pedTraffic:damageObserved", resourceRoot, source, weapon, bodypart)
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

    if enabled and populationWorldReady and getElementDimension(localPlayer) == 0 and getElementInterior(localPlayer) == 0 and
        type(updateAmbientPedPopulationModels) == "function" then
        local x, y, z = getElementPosition(localPlayer)
        updateAmbientPedPopulationModels(x, y, z)
    end
end)

addEventHandler("onClientElementDestroy", root, function()
    observedAimTargets[source] = nil
    nativeDamageObservations[source] = nil
    vehicleReactionStates[source] = nil
    airTestSessions[source] = nil
    climbTestSessions[source] = nil
    local task = assignments[source]
    if task then releaseTask(task, false) end
    releaseTrafficEventProfile(source)
end)

addEventHandler("onClientElementDataChange", root, function(dataName)
    if dataName ~= "neon:ambientPedTraffic" or getElementType(source) ~= "ped" then
        return
    end
    if getElementData(source, dataName) == true then
        acquireTrafficEventProfile(source)
    else
        releaseTrafficEventProfile(source)
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    airTestSessions = {}
    climbTestSessions = {}
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
        local profiles = 0
        for _, task in pairs(assignments) do
            if task.accepted then active = active + 1 end
        end
        for _ in pairs(nativeEventProfiles) do profiles = profiles + 1 end
        log(("telemetry active=%d profiles=%d worldReady=%s preset=%s revision=%d hits=%d misses=%d assignments=%d failures=%d missReasons=%s"):format(
                active, profiles, tostring(populationWorldReady), populationWorldPreset, populationWorldRevision, stats.candidateHits,
                stats.candidateMisses, stats.assignments, stats.failures, formatReasons(stats.missReasons)))
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
            local vx, vy, vz = getElementVelocity(ped)
            local horizontalSpeed = math.sqrt(vx * vx + vy * vy)
            local moveState = type(getPedMoveState) == "function" and getPedMoveState(ped) or "unavailable"
            local nativeStyle = type(isPedUsingNativeWalkingStyle) == "function" and isPedUsingNativeWalkingStyle(ped) or false
            local profileToken = nativeEventProfiles[ped]
            local profileRole = profileToken and isPedNativeEventProfileActive(ped, profileToken) and "owner" or
                                    (profileToken and "observer" or "missing")
            local threatRoot, threatLeaf = getThreatState(ped)
            local vehicleRoot, vehicleLeaf = getVehicleReactionState(ped)
            log(("ped id=%d model=%d syncer=%s streamed=%s profile=%s avoid=%s threat=%s/%s vehicle=%s/%s nativeStyle=%s move=%s speed=%.5f velocity=(%.5f,%.5f,%.5f) pos=(%.2f,%.2f,%.2f)"):format(
                    id, getElementModel(ped), tostring(isElementSyncer(ped)), tostring(isElementStreamedIn(ped)), profileRole,
                    tostring(getAvoidanceState(ped)), tostring(threatRoot), tostring(threatLeaf), tostring(vehicleRoot), tostring(vehicleLeaf),
                    tostring(nativeStyle), tostring(moveState), horizontalSpeed, vx, vy, vz, x, y, z))
        end
    end
end, 2000, 0)

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
