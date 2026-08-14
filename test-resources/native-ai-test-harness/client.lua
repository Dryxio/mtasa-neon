local activeRun

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
        return report("ready", {role = run.role, groupActive = run.role == "owner"})
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
        (role ~= "owner" and role ~= "observer") or (kind ~= "melee" and kind ~= "handoff") then
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
        run.role ~= "owner" or localPlayer ~= run.owner or actionId ~= "group-handoff" then
        return
    end
    local released = run.groupToken and releasePedNativeGroup(run.groupToken) == true or false
    run.groupToken = nil
    report("owner-released", {released = released})
end)

addEvent("nativeAIHarness:acquireOwner", true)
addEventHandler("nativeAIHarness:acquireOwner", resourceRoot, function(runId, epoch, actionId)
    local run = activeRun
    if source ~= resourceRoot or not run or run.id ~= tostring(runId) or localPlayer ~= run.victim or
        actionId ~= "group-handoff-acquire" then
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

addEvent("nativeAIHarness:stop", true)
addEventHandler("nativeAIHarness:stop", resourceRoot, function(runId)
    if source == resourceRoot and activeRun and activeRun.id == tostring(runId) then
        stopRun()
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, stopRun)
