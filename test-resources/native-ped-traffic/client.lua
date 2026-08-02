local enabled = false
local debugEnabled = false
local assignments = {}
local nativeEventProfiles = {}
local avoidanceStates = {}
local threatStates = {}
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

local function releaseTask(task, killNativeTask)
    clearTimer(task, "retryTimer")
    clearTimer(task, "monitorTimer")
    if killNativeTask and task.accepted and isElement(task.ped) then
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
    task.accepted = setPedWander(task.ped, "walk", task.direction, true)
    if not task.accepted then
        return fail(task, "wander-refused")
    end

    stats.assignments = stats.assignments + 1
    report(task, "accepted")
    log(("accepted epoch=%d direction=%d reason=%s"):format(task.epoch, task.direction, tostring(task.reason)))
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

addEvent("pedTraffic:setEnabled", true)
addEventHandler("pedTraffic:setEnabled", resourceRoot, function(value, debugValue)
    enabled = value == true
    debugEnabled = debugValue == true
    observedAimTargets = {}
    log("enabled=" .. tostring(enabled))
    if not enabled then
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
addEventHandler("pedTraffic:candidateRequest", resourceRoot, function(requestId)
    local startedAt = getTickCount()
    if not enabled or getElementDimension(localPlayer) ~= 0 or getElementInterior(localPlayer) ~= 0 or isPedDead(localPlayer) or
        type(getAmbientPedSpawnCandidate) ~= "function" then
        stats.candidateMisses = stats.candidateMisses + 1
        triggerServerEvent("pedTraffic:candidate", resourceRoot, requestId, false, getTickCount() - startedAt)
        return
    end

    local x, y, z = getElementPosition(localPlayer)
    local candidate, missReason = getAmbientPedSpawnCandidate(x, y, z)
    if candidate then
        stats.candidateHits = stats.candidateHits + 1
    else
        stats.candidateMisses = stats.candidateMisses + 1
        countReason(stats.missReasons, missReason)
    end
    triggerServerEvent("pedTraffic:candidate", resourceRoot, requestId, candidate or false, getTickCount() - startedAt, missReason)
end)

addEvent("pedTraffic:assign", true)
addEventHandler("pedTraffic:assign", resourceRoot, function(ped, epoch, direction, reason)
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
    releaseTask(task, true)
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

    if enabled and getElementDimension(localPlayer) == 0 and getElementInterior(localPlayer) == 0 and
        type(updateAmbientPedPopulationModels) == "function" then
        local x, y, z = getElementPosition(localPlayer)
        updateAmbientPedPopulationModels(x, y, z)
    end
end)

addEventHandler("onClientElementDestroy", root, function()
    observedAimTargets[source] = nil
    nativeDamageObservations[source] = nil
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

setTimer(function()
    if debugEnabled then
        local active = 0
        local profiles = 0
        for _, task in pairs(assignments) do
            if task.accepted then active = active + 1 end
        end
        for _ in pairs(nativeEventProfiles) do profiles = profiles + 1 end
        log(("telemetry active=%d profiles=%d hits=%d misses=%d assignments=%d failures=%d missReasons=%s"):format(
                active, profiles, stats.candidateHits, stats.candidateMisses, stats.assignments, stats.failures, formatReasons(stats.missReasons)))
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
            log(("ped id=%d syncer=%s streamed=%s profile=%s avoid=%s threat=%s/%s nativeStyle=%s move=%s speed=%.5f velocity=(%.5f,%.5f,%.5f) pos=(%.2f,%.2f,%.2f)"):format(
                    id, tostring(isElementSyncer(ped)), tostring(isElementStreamedIn(ped)), profileRole, tostring(getAvoidanceState(ped)),
                    tostring(threatRoot), tostring(threatLeaf), tostring(nativeStyle), tostring(moveState), horizontalSpeed, vx, vy, vz, x, y, z))
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
