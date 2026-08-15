local activeRun

local function isRotationKind(kind)
    return kind == "rotation" or kind == "rotation_handoff"
end

local function isGangDecisionKind(kind)
    return kind == "gang_unarmed_flee" or kind == "gang_armed_leader" or kind == "gang_armed_member" or
        kind == "gang_armed_handoff" or kind == "gang_friendly_source"
end

local harnessWeaponClips = {[22] = 17, [24] = 7, [28] = 50, [30] = 30, [32] = 50}
local harnessStandardWeaponStats = {[69] = 40, [71] = 200, [75] = 50, [77] = 200}

local function report(evidence, data)
    if activeRun then
        triggerServerEvent("nativeAIHarness:evidence", resourceRoot, activeRun.id, activeRun.epoch, evidence, data or {})
    end
end

local function clearTimer(run, name)
    if isTimer(run[name]) then
        killTimer(run[name])
    end
    run[name] = nil
end

local function stopRun()
    local run = activeRun
    if not run then
        return
    end
    clearTimer(run, "prepareTimer")
    local groupReleased = true
    if run.groupToken and type(releasePedNativeGroup) == "function" then
        groupReleased = releasePedNativeGroup(run.groupToken) == true
    end
    run.groupToken = nil
    for _, token in ipairs(run.groupTokens or {}) do
        if token and type(releasePedNativeGroup) == "function" then
            groupReleased = releasePedNativeGroup(token) == true and groupReleased
        end
    end
    run.groupTokens = {}
    for ped, token in pairs(run.profileTokens) do
        if token and type(releasePedNativeEventProfile) == "function" then
            releasePedNativeEventProfile(token)
        end
        local lease = run.streamingLeases[ped]
        if lease and type(releaseElementStreamingLease) == "function" then
            releaseElementStreamingLease(lease)
        end
    end
    activeRun = nil
    return groupReleased
end

local function acquireProfiles(run)
    if type(acquirePedNativeEventProfile) ~= "function" or type(releasePedNativeEventProfile) ~= "function" or
        type(acquireElementStreamingLease) ~= "function" or type(releaseElementStreamingLease) ~= "function" then
        return false, "native profile or streaming lease API missing"
    end
    for _, ped in ipairs(run.peds) do
        if not isElement(ped) then
            return false, "scenario ped missing"
        end
        if not run.profileTokens[ped] then
            run.profileTokens[ped] = acquirePedNativeEventProfile(ped, "ambient-wander")
        end
        if not run.streamingLeases[ped] then
            run.streamingLeases[ped] = acquireElementStreamingLease(ped)
        end
        if not run.profileTokens[ped] or not run.streamingLeases[ped] then
            return false, "profile or streaming lease refused"
        end
    end
    return true
end

local function acquireOwnerGroup(run)
    if type(setPedUseNativeWalkingStyle) ~= "function" or type(isPedNativeEventProfileActive) ~= "function" or
        type(acquirePedNativeGroup) ~= "function" or type(releasePedNativeGroup) ~= "function" or
        type(isPedNativeGroupActive) ~= "function" then
        return false, "native group API missing"
    end
    for _, ped in ipairs(run.peds) do
        if not isElementStreamedIn(ped) or not isElementSyncer(ped) or
            not isPedNativeEventProfileActive(ped, run.profileTokens[ped]) then
            return false, "owner ped not ready"
        end
        if getPedFightingStyle(ped) ~= run.fightingStyle then
            return false, "owner ped fighting style not converged"
        end
        local expectedWeapon = tonumber(getElementData(ped, "neon:nativeAIExpectedWeapon")) or 0
        if getPedWeapon(ped) ~= expectedWeapon or
            (expectedWeapon ~= 0 and
             (getPedTotalAmmo(ped) ~= 25001 or getPedAmmoInClip(ped) ~= harnessWeaponClips[expectedWeapon])) then
            return false, "owner ped weapon state not converged"
        end
        for stat, expectedValue in pairs(harnessStandardWeaponStats) do
            if getPedStat(ped, stat) ~= expectedValue then
                return false, "owner ped STD weapon skill not converged"
            end
        end
        if not setPedUseNativeWalkingStyle(ped, true) then
            return false, "native walking style refused"
        end
    end
    -- A native group necessarily installs WanderGang/GangFollower tasks. Both
    -- are allowed to change heading after a script write, which makes them an
    -- invalid fixture for locating a rotation transport divergence. Rotation
    -- scenarios deliberately stop here: the freshly created script peds keep
    -- their inert PlayerOnFoot default while melee/handoff still exercise the
    -- real native group implementation.
    if isRotationKind(run.kind) then
        return true
    end
    if run.scenario.groupPartitions then
        if #run.groupTokens == 0 then
            for _, partition in ipairs(run.scenario.groupPartitions) do
                local members = {}
                for _, index in ipairs(partition) do members[#members + 1] = run.peds[index] end
                local token = acquirePedNativeGroup(members, "ambient-random")
                if not token then
                    return false, "partitioned native group acquisition refused"
                end
                run.groupTokens[#run.groupTokens + 1] = token
            end
        end
        for _, token in ipairs(run.groupTokens) do
            if not isPedNativeGroupActive(token) then
                return false, "partitioned native group inactive"
            end
        end
        return true
    end
    if not run.groupToken then
        run.groupToken = acquirePedNativeGroup(run.peds, "ambient-random")
    end
    if not run.groupToken or not isPedNativeGroupActive(run.groupToken) then
        return false, "native group acquisition refused"
    end
    return true
end

local function finishPreparation()
    local run = activeRun
    if not run then
        return
    end
    local profilesReady, reason = acquireProfiles(run)
    local streamed = profilesReady
    if streamed then
        for _, ped in ipairs(run.peds) do
            streamed = streamed and isElementStreamedIn(ped) and getPedFightingStyle(ped) == run.fightingStyle
            local expectedWeapon = tonumber(getElementData(ped, "neon:nativeAIExpectedWeapon")) or 0
            streamed = streamed and getPedWeapon(ped) == expectedWeapon
            if expectedWeapon ~= 0 then
                streamed = streamed and getPedTotalAmmo(ped) == 25001 and
                               getPedAmmoInClip(ped) == harnessWeaponClips[expectedWeapon]
            end
            for stat, expectedValue in pairs(harnessStandardWeaponStats) do
                streamed = streamed and getPedStat(ped, stat) == expectedValue
            end
        end
    end
    local ownerReady = true
    if profilesReady and streamed and run.role == "owner" then
        ownerReady, reason = acquireOwnerGroup(run)
    end
    if profilesReady and streamed and ownerReady then
        return report("ready", {role = run.role, groupActive = run.role == "owner" and not isRotationKind(run.kind)})
    end
    if getTickCount() - run.requestedAt >= NATIVE_AI_HARNESS.preparationTimeout then
        return report("failure", {reason = reason or "streaming timeout"})
    end
    run.prepareTimer = setTimer(finishPreparation, 100, 1)
end

addEvent("nativeAIHarness:prepare", true)
addEventHandler("nativeAIHarness:prepare", resourceRoot,
                function(runId, scenarioId, epoch, peds, owner, victim, role, kind)
    if source ~= resourceRoot or type(runId) ~= "string" or type(scenarioId) ~= "string" or type(peds) ~= "table" or
        #peds < 2 or #peds > 5 or not isElement(owner) or not isElement(victim) or
        (role ~= "owner" and role ~= "observer") or
        (kind ~= "melee" and kind ~= "handoff" and kind ~= "rotation" and kind ~= "rotation_handoff" and
         not isGangDecisionKind(kind)) then
        return
    end
    stopRun()
    activeRun = {
        id = runId,
        scenarioId = scenarioId,
        epoch = tonumber(epoch),
        peds = peds,
        owner = owner,
        victim = victim,
        role = role,
        kind = kind,
        scenario = NATIVE_AI_HARNESS.scenarios[kind],
        fightingStyle = NATIVE_AI_HARNESS.scenarios[kind].fightingStyle,
        requestedAt = getTickCount(),
        profileTokens = {},
        groupTokens = {},
        streamingLeases = {},
        damageNonces = {},
        appliedNonces = {},
    }
    finishPreparation()
end)

local fightTaskNames = {
    TASK_COMPLEX_KILL_PED_ON_FOOT = true,
    TASK_COMPLEX_KILL_PED_ON_FOOT_ARMED = true,
    TASK_COMPLEX_KILL_PED_ON_FOOT_STEALTH = true,
    TASK_COMPLEX_KILL_CRIMINAL = true,
}

local coverTaskNames = {
    TASK_SEEK_COVER_UNTIL_TARGET_DEAD = true,
}

local fleeTaskNames = {
    TASK_COMPLEX_FLEE_ENTITY = true,
    TASK_COMPLEX_SMART_FLEE_ENTITY = true,
    TASK_COMPLEX_FLEE_ANY_MEANS = true,
    TASK_SIMPLE_COWER = true,
}

local function captureCollectiveResponse(run)
    local rows = {}
    local allFlee = true
    local allFightAllocation = true
    local armedFightCount = 0
    for index, ped in ipairs(run.peds) do
        local hasFight = false
        local hasFlee = false
        local hasCover = false
        local hierarchyRows = {}
        for _, slot in ipairs({0, 1, 2, 3}) do
            local hierarchy = {getPedTask(ped, "primary", slot)}
            if hierarchy[1] ~= false then
                hierarchyRows[tostring(slot)] = hierarchy
                for _, taskName in ipairs(hierarchy) do
                    hasFight = hasFight or fightTaskNames[taskName] == true
                    hasFlee = hasFlee or fleeTaskNames[taskName] == true
                    hasCover = hasCover or coverTaskNames[taskName] == true
                end
            end
        end
        local expectedWeapon = tonumber(run.scenario.pedWeapons and run.scenario.pedWeapons[index]) or 0
        local armed = expectedWeapon ~= 0
        -- TASK_GROUP_KILL_PLAYER_BASIC allocates the gun task only to armed
        -- members. Stock unarmed members participate by seeking cover until
        -- the target dies; accepting a kill task here would hide a wrong
        -- allocator branch.
        local fightAllocated = armed and hasFight or not armed and hasCover
        if armed and hasFight then
            armedFightCount = armedFightCount + 1
        end
        allFlee = allFlee and hasFlee
        allFightAllocation = allFightAllocation and fightAllocated
        rows[#rows + 1] = {
            actorId = "ped-" .. index,
            armed = armed,
            fight = hasFight,
            cover = hasCover,
            flee = hasFlee,
            tasks = hierarchyRows,
        }
    end
    if allFlee then
        return "flee", rows
    end
    if allFightAllocation and armedFightCount > 0 then
        return "fight", rows
    end
    return false, rows
end

addEvent("nativeAIHarness:decisionAttack", true)
addEventHandler("nativeAIHarness:decisionAttack", resourceRoot,
                function(runId, epoch, actionId, attackedPed, sourcePed, expectedResponse)
    local run = activeRun
    if source ~= resourceRoot or not run or not isGangDecisionKind(run.kind) or run.id ~= tostring(runId) or
        run.epoch ~= tonumber(epoch) or run.role ~= "owner" or localPlayer ~= run.owner or
        not isElement(attackedPed) or not isElement(sourcePed) or type(addPedNativeDamageResponseEvent) ~= "function" then
        return
    end
    local token = run.profileTokens[attackedPed]
    local accepted = token and isPedNativeEventProfileActive(attackedPed, token) and
                         addPedNativeDamageResponseEvent(attackedPed, sourcePed, 22, 3, token) == true
    run.actionId = tostring(actionId)
    run.expectedResponse = expectedResponse
    report("decision-injected", {
        accepted = accepted == true,
        actionId = run.actionId,
        attackedActorId = tostring(getElementData(attackedPed, "neon:nativeAIActorId") or "unknown"),
        sourceActorId = tostring(getElementData(sourcePed, "neon:nativeAIActorId") or
                                   (sourcePed == run.victim and "victim-player" or "unknown")),
        expectedResponse = expectedResponse,
    })
    if not accepted then
        return
    end
    if not expectedResponse then
        return setTimer(function(expectedRun)
            if activeRun and activeRun.id == expectedRun then
                report("classification-captured", {actionId = activeRun.actionId})
            end
        end, 1200, 1, run.id)
    end
    local startedAt = getTickCount()
    local function observe()
        if not activeRun or activeRun ~= run then
            return
        end
        local response, rows = captureCollectiveResponse(run)
        if response then
            return report("collective-response", {actionId = run.actionId, response = response, members = rows})
        end
        if getTickCount() - startedAt >= 5000 then
            return report("collective-response-timeout", {
                actionId = run.actionId,
                response = run.scenario.branchAware and "native-selected fight or flee" or expectedResponse,
                members = rows,
            })
        end
        run.prepareTimer = setTimer(observe, 100, 1)
    end
    observe()
end)

addEvent("nativeAIHarness:attack", true)
addEventHandler("nativeAIHarness:attack", resourceRoot, function(runId, epoch, actionId)
    local run = activeRun
    if source ~= resourceRoot or not run or run.id ~= tostring(runId) or run.epoch ~= tonumber(epoch) or
        run.role ~= "owner" or localPlayer ~= run.owner or actionId ~= "melee-attack" or
        type(setPedKillOnFoot) ~= "function" then
        return
    end
    run.actionId = actionId
    local accepted = 0
    for _, ped in ipairs(run.peds) do
        if isElement(ped) and isElementSyncer(ped) and setPedKillOnFoot(ped, run.victim) then
            accepted = accepted + 1
        end
    end
    report("attack-dispatched", {accepted = accepted, requested = #run.peds})
end)

addEventHandler("onClientPlayerNativeDamageAttempt", root, function(attacker, weapon, bodypart, damageFactor, direction)
    local run = activeRun
    if not run or (run.kind ~= "melee" and run.expectedResponse ~= "fight") or run.role ~= "owner" or localPlayer ~= run.owner or source ~= run.victim or
        not isElement(attacker) or not run.profileTokens[attacker] or not isElementSyncer(attacker) or
        not isPedNativeEventProfileActive(attacker, run.profileTokens[attacker]) then
        return
    end
    run.damageNonces[attacker] = (run.damageNonces[attacker] or 0) + 1
    triggerServerEvent("nativeAIHarness:nativeDamageObserved", resourceRoot, run.id, run.epoch, run.actionId,
                       attacker, source, run.damageNonces[attacker], weapon, bodypart, damageFactor, direction)
end)

addEvent("nativeAIHarness:applyDamage", true)
addEventHandler("nativeAIHarness:applyDamage", resourceRoot,
                function(runId, epoch, actionId, attacker, nonce, weapon, bodypart, damageFactor, direction)
    local run = activeRun
    if source ~= resourceRoot or not run or run.id ~= tostring(runId) or run.epoch ~= tonumber(epoch) or
        (run.actionId ~= nil and run.actionId ~= actionId) or localPlayer ~= run.victim or not isElement(attacker) or
        not run.profileTokens[attacker] or isElementSyncer(attacker) or type(addPedNativeDamageEvent) ~= "function" then
        return
    end
    nonce = tonumber(nonce)
    if not nonce or nonce <= (run.appliedNonces[attacker] or 0) then
        return
    end
    run.actionId = actionId
    run.appliedNonces[attacker] = nonce
    local healthBefore = getElementHealth(localPlayer)
    local armorBefore = getPedArmor(localPlayer)
    local accepted = addPedNativeDamageEvent(localPlayer, attacker, weapon, bodypart, damageFactor, direction,
                                             run.profileTokens[attacker]) == true
    local healthImmediate = getElementHealth(localPlayer)
    local armorImmediate = getPedArmor(localPlayer)
    setTimer(function(expectedRun, expectedNonce, before, beforeArmor, immediateHealth, immediateArmor, wasAccepted, actorId)
        if activeRun and activeRun.id == expectedRun then
            report("damage-applied", {
                accepted = wasAccepted,
                nonce = expectedNonce,
                actorId = actorId,
                healthBefore = before,
                healthImmediate = immediateHealth,
                healthAfter = getElementHealth(localPlayer),
                armorBefore = beforeArmor,
                armorImmediate = immediateArmor,
                armorAfter = getPedArmor(localPlayer),
            })
        end
    end, 350, 1, run.id, nonce, healthBefore, armorBefore, healthImmediate, armorImmediate, accepted,
             tostring(getElementData(attacker, "neon:nativeAIActorId") or "unknown"))
end)

addEvent("nativeAIHarness:stopCombat", true)
addEventHandler("nativeAIHarness:stopCombat", resourceRoot, function(runId, epoch)
    local run = activeRun
    if source == resourceRoot and run and run.id == tostring(runId) and run.epoch == tonumber(epoch) and run.role == "owner" then
        for _, ped in ipairs(run.peds) do
            if isElement(ped) and isElementSyncer(ped) then
                killPedTask(ped, "primary", 3, false)
            end
        end
    end
end)

addEvent("nativeAIHarness:releaseOwner", true)
addEventHandler("nativeAIHarness:releaseOwner", resourceRoot, function(runId, epoch, actionId)
    local run = activeRun
    if source ~= resourceRoot or not run or run.id ~= tostring(runId) or run.epoch ~= tonumber(epoch) or
        run.role ~= "owner" or localPlayer ~= run.owner or actionId ~= "group-handoff" or
        (run.kind ~= "handoff" and run.kind ~= "rotation_handoff" and run.kind ~= "gang_armed_handoff") then
        return
    end
    local released = true
    if not isRotationKind(run.kind) then
        released = run.groupToken and releasePedNativeGroup(run.groupToken) == true or false
        run.groupToken = nil
    end
    report("owner-released", {released = released})
    if run.kind == "rotation_handoff" then
        run.role = "observer"
    end
end)

addEvent("nativeAIHarness:acquireOwner", true)
addEventHandler("nativeAIHarness:acquireOwner", resourceRoot, function(runId, epoch, actionId)
    local run = activeRun
    if source ~= resourceRoot or not run or run.id ~= tostring(runId) or localPlayer ~= run.victim or
        actionId ~= "group-handoff-acquire" or
        (run.kind ~= "handoff" and run.kind ~= "rotation_handoff" and run.kind ~= "gang_armed_handoff") then
        return
    end
    run.epoch = tonumber(epoch)
    run.role = "owner"
    run.owner = localPlayer
    local startedAt = getTickCount()
    local function acquire()
        if not activeRun or activeRun ~= run then
            return
        end
        local acquired, reason = acquireOwnerGroup(run)
        if acquired then
            return report("owner-acquired", {acquired = true})
        end
        if getTickCount() - startedAt >= NATIVE_AI_HARNESS.preparationTimeout then
            return report("failure", {reason = reason or "new owner acquisition timeout"})
        end
        run.prepareTimer = setTimer(acquire, 100, 1)
    end
    acquire()
end)

addEvent("nativeAIHarness:updateObserverEpoch", true)
addEventHandler("nativeAIHarness:updateObserverEpoch", resourceRoot, function(runId, epoch)
    local run = activeRun
    if source == resourceRoot and run and run.id == tostring(runId) and run.kind == "rotation_handoff" and run.role == "observer" then
        run.epoch = tonumber(epoch)
    end
end)

local function headingDeltaDegrees(left, right)
    return math.abs((left - right + 180.0) % 360.0 - 180.0)
end

addEvent("nativeAIHarness:rotationAction", true)
addEventHandler("nativeAIHarness:rotationAction", resourceRoot,
                function(runId, epoch, actionId, ped, targetHeading, rotationOwner)
    local run = activeRun
    targetHeading = tonumber(targetHeading)
    if source ~= resourceRoot or not run or not isRotationKind(run.kind) or run.id ~= tostring(runId) or
        run.epoch ~= tonumber(epoch) or type(actionId) ~= "string" or not isElement(ped) or
        not targetHeading or not isElement(rotationOwner) then
        return
    end

    run.actionId = actionId
    run.rotationObservation = {
        actionId = actionId,
        ped = ped,
        targetHeading = targetHeading,
        startedAt = getTickCount(),
        convergedFrames = 0,
        reported = false,
        ownerHeldReported = false,
        ownerDriftReported = false,
        isRotationOwner = localPlayer == rotationOwner,
    }

    if localPlayer == rotationOwner then
        -- The script API converges to the requested element rotation after
        -- the native ped update. The owner-held observation below is the
        -- authoritative gate; the immediate read is diagnostic only.
        local scriptHeading = targetHeading
        local accepted = isElementSyncer(ped) and setElementVelocity(ped, 0, 0, 0) and
                             setElementRotation(ped, 0, 0, scriptHeading) == true
        local _, _, actualHeading = getElementRotation(ped)
        report("rotation-owner-applied", {
            actionId = actionId,
            actorId = tostring(getElementData(ped, "neon:nativeAIActorId") or "unknown"),
            accepted = accepted == true,
            targetHeading = targetHeading,
            actualHeading = actualHeading,
            scriptHeading = scriptHeading,
        })
    end
end)

addEventHandler("onClientPedsProcessed", root, function()
    local run = activeRun
    local observation = run and run.rotationObservation
    if not observation or observation.reported or not isElement(observation.ped) then
        return
    end

    local _, _, actualHeading = getElementRotation(observation.ped)
    local delta = headingDeltaDegrees(actualHeading, observation.targetHeading)
    if delta <= NATIVE_AI_HARNESS.rotationConvergenceDegrees then
        observation.convergedFrames = observation.convergedFrames + 1
    else
        observation.convergedFrames = 0
    end
    local elapsed = getTickCount() - observation.startedAt
    if observation.isRotationOwner then
        if not observation.ownerHeldReported and elapsed >= NATIVE_AI_HARNESS.rotationOwnerHoldMinimumMs and
            observation.convergedFrames >= NATIVE_AI_HARNESS.rotationConvergenceFrames then
            observation.ownerHeldReported = true
            report("rotation-owner-held", {
                actionId = observation.actionId,
                actorId = tostring(getElementData(observation.ped, "neon:nativeAIActorId") or "unknown"),
                targetHeading = observation.targetHeading,
                actualHeading = actualHeading,
                deltaDegrees = delta,
                elapsedMs = elapsed,
                consecutiveFrames = observation.convergedFrames,
            })
        elseif elapsed >= NATIVE_AI_HARNESS.rotationOwnerHoldMinimumMs and
            delta > NATIVE_AI_HARNESS.rotationConvergenceDegrees and not observation.ownerDriftReported then
            observation.ownerDriftReported = true
            report("rotation-owner-drift", {
                actionId = observation.actionId,
                actorId = tostring(getElementData(observation.ped, "neon:nativeAIActorId") or "unknown"),
                targetHeading = observation.targetHeading,
                actualHeading = actualHeading,
                deltaDegrees = delta,
                elapsedMs = elapsed,
            })
        end
    end
    if observation.convergedFrames < NATIVE_AI_HARNESS.rotationConvergenceFrames then
        return
    end
    if elapsed < NATIVE_AI_HARNESS.rotationObservationMinimumMs then
        return
    end

    observation.reported = true
    report("rotation-converged", {
        actionId = observation.actionId,
        actorId = tostring(getElementData(observation.ped, "neon:nativeAIActorId") or "unknown"),
        role = run.role,
        targetHeading = observation.targetHeading,
        actualHeading = actualHeading,
        deltaDegrees = delta,
        elapsedMs = elapsed,
        consecutiveFrames = observation.convergedFrames,
    })
end)

addEvent("nativeAIHarness:stop", true)
addEventHandler("nativeAIHarness:stop", resourceRoot, function(runId)
    if source == resourceRoot and activeRun and activeRun.id == tostring(runId) then
        local groupReleased = stopRun()
        triggerServerEvent("nativeAIHarness:stopped", resourceRoot, tostring(runId), groupReleased == true)
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, stopRun)
