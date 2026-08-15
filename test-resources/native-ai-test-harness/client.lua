local activeRun

local function isRotationKind(kind)
    return kind == "rotation" or kind == "rotation_handoff"
end

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
    if run.groupToken and type(releasePedNativeGroup) == "function" then
        releasePedNativeGroup(run.groupToken)
    end
    run.groupToken = nil
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
        (kind ~= "melee" and kind ~= "handoff" and kind ~= "rotation" and kind ~= "rotation_handoff") then
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
        fightingStyle = NATIVE_AI_HARNESS.scenarios[kind].fightingStyle,
        requestedAt = getTickCount(),
        profileTokens = {},
        streamingLeases = {},
        damageNonces = {},
        appliedNonces = {},
    }
    finishPreparation()
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
    if not run or run.kind ~= "melee" or run.role ~= "owner" or localPlayer ~= run.owner or source ~= run.victim or
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
        (run.kind ~= "handoff" and run.kind ~= "rotation_handoff") then
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
        actionId ~= "group-handoff-acquire" or (run.kind ~= "handoff" and run.kind ~= "rotation_handoff") then
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
        stopRun()
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, stopRun)
