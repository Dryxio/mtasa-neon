local activeRun
local nextRunSequence = 0
local logPath = "@harness-server.jsonl"

local function isRotationKind(kind)
    return kind == "rotation" or kind == "rotation_handoff"
end

local function isHandoffKind(kind)
    return kind == "handoff" or kind == "rotation_handoff" or kind == "gang_armed_handoff"
end

local function isGangDecisionKind(kind)
    return kind == "gang_unarmed_flee" or kind == "gang_armed_leader" or kind == "gang_armed_member" or
        kind == "gang_armed_handoff" or kind == "gang_friendly_source" or kind == "gang_two_armed_two_unarmed_melee"
end

-- GetStrikeDamage (GTA 1.0 US 0x61C740) reads the current move's damage byte
-- from the active melee combo, then applies the non-player pedstats attack
-- multiplier. The harness pins STYLE_STANDARD/UNARMED_1 and gang pedstats use
-- 1.0, so these are the complete non-zero factors reachable in this scenario.
local canonicalGangMeleeFactors = {
    [5] = true,
    [6] = true,
    [9] = true,
    [15] = true,
    [25] = true,
}

local canonicalGangFirearmFactors = {
    [22] = 25,
    [24] = 140,
    [28] = 20,
    [30] = 30,
    [32] = 20,
}

local standardWeaponStats = {
    {69, 40},
    {71, 200},
    {75, 50},
    {77, 200},
}

local weaponClipAmmo = {[22] = 17, [24] = 7, [28] = 50, [30] = 30, [32] = 50}

local meleeStages = {
    "action_dispatched",
    "owner_native_damage_attempt",
    "server_validated_forward",
    "victim_injection_result",
    "final_observation",
}

local function utcTimestamp()
    local localTime = getRealTime()
    local utc = getRealTime(localTime.timestamp, false)
    return ("%04d-%02d-%02dT%02d:%02d:%02dZ"):format(
        utc.year + 1900, utc.month + 1, utc.monthday, utc.hour, utc.minute, utc.second)
end

local function resetLog()
    if fileExists(logPath) then
        fileDelete(logPath)
    end
end

local function writeTrace(event, fields)
    local run = activeRun
    local row = {
        schema = NATIVE_AI_HARNESS.schema,
        schema_version = NATIVE_AI_HARNESS.schemaVersion,
        wall_utc = utcTimestamp(),
        monotonic_ms = getTickCount(),
        relative_ms = run and (getTickCount() - run.startedAt) or 0,
        run_id = run and run.id or false,
        scenario_id = run and run.scenario.id or false,
        event = event,
    }
    for key, value in pairs(fields or {}) do
        row[key] = value
    end

    local file = fileExists(logPath) and fileOpen(logPath) or fileCreate(logPath)
    if not file then
        outputDebugString("[native-ai-harness] FAIL: cannot open " .. logPath, 1)
        return false
    end
    local encoded = toJSON(row, true)
    -- MTA serializes Lua arguments as a JSON array, even when the one
    -- argument is a keyed table. JSONL consumers need the contained object,
    -- not one single-element array per line.
    if type(encoded) == "string" and encoded:sub(1, 2) == "[{" and encoded:sub(-2) == "}]" then
        encoded = encoded:sub(2, -2)
    end
    if type(encoded) ~= "string" or encoded:sub(1, 1) ~= "{" or encoded:sub(-1) ~= "}" then
        fileClose(file)
        outputDebugString("[native-ai-harness] FAIL: toJSON did not produce one JSON object", 1)
        return false
    end
    fileSetPos(file, fileGetSize(file))
    fileWrite(file, encoded .. "\n")
    fileFlush(file)
    fileClose(file)
    return true
end

local function writeStage(event, fields)
    if activeRun then
        activeRun.stages[event] = true
    end
    return writeTrace(event, fields)
end

local function announce(message, level)
    outputServerLog("[native-ai-harness] " .. message)
    if activeRun then
        for _, player in ipairs({activeRun.initialOwner, activeRun.victim}) do
            if isElement(player) then
                outputChatBox("[native AI] " .. message, player, level == "fail" and 255 or 160,
                              level == "fail" and 90 or 225, level == "fail" and 90 or 255)
            end
        end
    end
end

local function assertion(name, passed, expected, actual, details)
    writeTrace("assertion", {
        assertion = {
            name = name,
            passed = passed == true,
            expected = expected,
            actual = actual,
        },
        details = details,
    })
    announce(("%s: %s (expected=%s actual=%s)"):format(passed and "PASS" or "FAIL", name,
                                                        tostring(expected), tostring(actual)), passed and "pass" or "fail")
    return passed
end

local function snapshotPlayer(player)
    local x, y, z = getElementPosition(player)
    local rx, ry, rz = getElementRotation(player)
    local allData = getAllElementData(player)
    local traceData = {}
    for _, key in ipairs(NATIVE_AI_HARNESS_TRACE_KEYS) do
        traceData[key] = allData[key]
    end
    local weapons = {}
    local activeWeapon = getPedWeapon(player)
    for slot = 0, 12 do
        local weapon = getPedWeapon(player, slot)
        if weapon and weapon ~= 0 then
            weapons[#weapons + 1] = {
                weapon = weapon,
                ammo = getPedTotalAmmo(player, slot),
                clip = getPedAmmoInClip(player, slot),
                current = weapon == activeWeapon,
            }
        end
    end
    return {
        x = x,
        y = y,
        z = z,
        rx = rx,
        ry = ry,
        rz = rz,
        interior = getElementInterior(player),
        dimension = getElementDimension(player),
        health = getElementHealth(player),
        armor = getPedArmor(player),
        frozen = isElementFrozen(player),
        weapons = weapons,
        traceData = traceData,
    }
end

local function restorePlayer(player, snapshot)
    if not isElement(player) or not snapshot then
        return
    end
    setElementInterior(player, snapshot.interior)
    setElementDimension(player, snapshot.dimension)
    setElementPosition(player, snapshot.x, snapshot.y, snapshot.z)
    setElementRotation(player, snapshot.rx, snapshot.ry, snapshot.rz)
    setElementHealth(player, snapshot.health)
    setPedArmor(player, snapshot.armor)
    setElementFrozen(player, snapshot.frozen)
    takeAllWeapons(player)
    for _, weapon in ipairs(snapshot.weapons or {}) do
        giveWeapon(player, weapon.weapon, math.min(weapon.ammo, 9999), weapon.current == true)
        setWeaponAmmo(player, weapon.weapon, weapon.ammo, weapon.clip)
    end
    for _, key in ipairs(NATIVE_AI_HARNESS_TRACE_KEYS) do
        if snapshot.traceData[key] == nil then
            removeElementData(player, key)
        else
            setElementData(player, key, snapshot.traceData[key])
        end
    end
end

local function setActorTrace(element, actorId, actionId, step)
    local run = activeRun
    if not run or not isElement(element) then
        return
    end
    setElementData(element, "neon:nativeAIRunId", run.id)
    setElementData(element, "neon:nativeAIScenarioId", run.scenario.id)
    setElementData(element, "neon:nativeAIActorId", actorId)
    setElementData(element, "neon:nativeAIActionId", actionId or "prepare")
    setElementData(element, "neon:nativeAIStep", step or "prepare")
end

local function setRunStep(actionId, step)
    local run = activeRun
    if not run then
        return
    end
    run.actionId = actionId
    run.step = step
    setActorTrace(run.initialOwner, "owner-player", actionId, step)
    setActorTrace(run.victim, isHandoffKind(run.kind) and "next-owner-player" or "victim-player", actionId, step)
    for index, ped in ipairs(run.peds) do
        setActorTrace(ped, "ped-" .. index, actionId, step)
    end
    writeTrace("step", {action_id = actionId, step = step})
end

local function cancelRunTimers(run)
    for _, timer in pairs(run.timers or {}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    run.timers = {}
end

local function finalizeCleanup(run)
    if activeRun ~= run then
        return
    end
    cancelRunTimers(run)
    for _, ped in ipairs(run.peds) do
        if isElement(ped) then
            destroyElement(ped)
        end
    end
    restorePlayer(run.initialOwner, run.snapshots[run.initialOwner])
    restorePlayer(run.victim, run.snapshots[run.victim])
    activeRun = nil
end

local function cleanupRun(reason, immediate)
    local run = activeRun
    if not run or run.cleanupPending then
        return
    end
    run.cleanupPending = true
    run.cleanupAcks = {}
    writeTrace("cleanup", {details = reason or "cleanup"})
    cancelRunTimers(run)
    for _, player in ipairs({run.initialOwner, run.victim}) do
        if isElement(player) then
            triggerClientEvent(player, "nativeAIHarness:stop", resourceRoot, run.id, reason or "cleanup")
        else
            run.cleanupAcks[player] = true
        end
    end
    if immediate then
        return finalizeCleanup(run)
    end
    -- Keep the exact ped elements alive until both clients have released
    -- their native group leases. Destroying them in the same server frame as
    -- the stop event leaves the client-side release without its GTA members
    -- and can strand one of the eight stock group slots.
    run.timers.cleanupAckTimeout = setTimer(function(expectedId)
        if activeRun and activeRun.id == expectedId then
            writeTrace("cleanup_ack_timeout", {details = "forcing cleanup after missing client acknowledgement"})
            finalizeCleanup(activeRun)
        end
    end, 2000, 1, run.id)
end

local function finishRun(passed, reason)
    local run = activeRun
    if not run or run.finished then
        return
    end
    -- Freeze the causal timeline at the first verdict. A pending dispatch,
    -- timeout or handoff callback must not append new scenario evidence after
    -- run_end while the short visual-cleanup grace period is still active.
    cancelRunTimers(run)
    if run.kind == "melee" or (isGangDecisionKind(run.kind) and run.selectedResponse == "fight" and
        run.kind ~= "gang_armed_handoff" and run.kind ~= "gang_two_armed_two_unarmed_melee") then
        local missing = {}
        for _, stage in ipairs(meleeStages) do
            if not run.stages[stage] then
                missing[#missing + 1] = stage
                assertion("stage-present:" .. stage, false, true, false)
            end
        end
        if passed and #missing > 0 then
            passed = false
            reason = "causal chain incomplete: " .. table.concat(missing, ",")
        elseif #missing == 0 then
            assertion("causal-chain-complete", true, table.concat(meleeStages, " -> "), "complete")
        end
    end
    run.finished = true
    setRunStep(passed and "scenario-pass" or "scenario-fail", "complete")
    writeTrace("run_end", {passed = passed == true, details = reason})
    announce((passed and "SCENARIO PASS: " or "SCENARIO FAIL: ") .. tostring(reason), passed and "pass" or "fail")
    run.timers.cleanup = setTimer(function(expectedId)
        if activeRun and activeRun.id == expectedId then
            cleanupRun("automatic-after-verdict")
        end
    end, NATIVE_AI_HARNESS.cleanupDelay, 1, run.id)
end

local function findPlayer(name)
    if not name or name == "" then
        return false, "missing-player"
    end
    local lowered = name:lower()
    local partial
    for _, player in ipairs(getElementsByType("player")) do
        local playerName = getPlayerName(player)
        if playerName:lower() == lowered then
            return player
        end
        if playerName:lower():find(lowered, 1, true) then
            if partial then
                return false, "ambiguous-player"
            end
            partial = player
        end
    end
    return partial or false, partial and nil or "unknown-player"
end

local function resolveRoles(caller, ownerName, victimName)
    if ownerName or victimName then
        local owner, ownerReason = findPlayer(ownerName)
        local victim, victimReason = findPlayer(victimName)
        if not owner or not victim then
            return false, false, ownerReason or victimReason
        end
        return owner, victim
    end
    local other
    for _, player in ipairs(getElementsByType("player")) do
        if player ~= caller then
            if other then
                return false, false, "more-than-two-players-use-names"
            end
            other = player
        end
    end
    return caller, other, other and nil or "need-two-players"
end

local function placePlayer(player, position)
    setElementInterior(player, 0)
    setElementDimension(player, NATIVE_AI_HARNESS.dimension)
    setElementPosition(player, position[1], position[2], position[3])
    setElementRotation(player, 0, 0, position[4])
    setElementFrozen(player, false)
    setElementHealth(player, 100)
    setPedArmor(player, 0)
end

local function dispatchRotation(rotationIndex)
    local run = activeRun
    if not run or run.finished or not isRotationKind(run.kind) then
        return
    end

    local targetHeading = run.scenario.rotationTargets[rotationIndex]
    local actionId = ("rotation-%03d"):format(targetHeading)
    setRunStep(actionId, "rotation-dispatch")
    run.rotationActions[actionId] = {
        converged = {},
        ownerApplied = false,
        ownerHeld = false,
        targetHeading = targetHeading,
    }
    writeStage("rotation_action_dispatched", {
        action_id = actionId,
        actor_id = "ped-2",
        owner_actor_id = run.rotationOwner == run.owner and "owner-player" or "next-owner-player",
        target_heading = targetHeading,
        rotation_index = rotationIndex,
        epoch = run.epoch,
    })
    for _, player in ipairs({run.owner, run.victim}) do
        triggerClientEvent(player, "nativeAIHarness:rotationAction", resourceRoot, run.id, run.epoch,
                           actionId, run.peds[2], targetHeading, run.rotationOwner)
    end
end

local function beginRotationSequence(rotationOwner)
    local run = activeRun
    if not run or run.finished then
        return
    end
    run.rotationOwner = rotationOwner
    run.rotationActions = {}
    for rotationIndex = 1, #run.scenario.rotationTargets do
        local delay = NATIVE_AI_HARNESS.traceLeadTime + (rotationIndex - 1) * NATIVE_AI_HARNESS.rotationTurnInterval
        run.timers["rotationDispatch" .. rotationIndex] = setTimer(function(expectedId, expectedIndex)
            if activeRun and activeRun.id == expectedId then
                dispatchRotation(expectedIndex)
            end
        end, delay, 1, run.id, rotationIndex)
    end
    local completionDelay = NATIVE_AI_HARNESS.traceLeadTime +
                                (#run.scenario.rotationTargets - 1) * NATIVE_AI_HARNESS.rotationTurnInterval +
                                NATIVE_AI_HARNESS.rotationFinalObservation
    run.timers.rotationComplete = setTimer(function(expectedId)
        local active = activeRun
        if not active or active.id ~= expectedId then
            return
        end
        for _, targetHeading in ipairs(active.scenario.rotationTargets) do
            local actionId = ("rotation-%03d"):format(targetHeading)
            local action = active.rotationActions[actionId]
            if not action or not action.ownerApplied or not action.ownerHeld then
                assertion("rotation-owner-held:" .. actionId, false, true, action and action.ownerHeld or false)
                return finishRun(false, "owner did not hold predetermined action " .. actionId)
            end
        end
        local finalTarget = active.scenario.rotationTargets[#active.scenario.rotationTargets]
        local finalActionId = ("rotation-%03d"):format(finalTarget)
        local finalAction = active.rotationActions[finalActionId]
        if not finalAction or not finalAction.converged[active.owner] or not finalAction.converged[active.victim] then
            assertion("rotation-final-convergence", false, "owner+observer", "incomplete")
            return finishRun(false, "final rotation did not converge during the capture window")
        end
        writeStage("rotation_schedule_complete", {
            action_id = finalActionId,
            actor_id = "ped-2",
            target_heading = finalTarget,
            turn_interval_ms = NATIVE_AI_HARNESS.rotationTurnInterval,
            final_observation_ms = NATIVE_AI_HARNESS.rotationFinalObservation,
            epoch = active.epoch,
        })
        finishRun(true, "predetermined rotation trace captured; causal analyzer verdict required")
    end, completionDelay, 1, run.id)
end

local function beginScheduledAction()
    local run = activeRun
    if not run or run.finished then
        return
    end
    if isGangDecisionKind(run.kind) and run.kind ~= "gang_armed_handoff" then
        setRunStep("prepare", "settle")
        run.pendingActionId = run.kind == "gang_friendly_source" and "friendly-source" or
            (run.kind == "gang_two_armed_two_unarmed_melee" and "melee-threat" or "firearm-threat")
        run.actionDueAt = getTickCount() + NATIVE_AI_HARNESS.traceLeadTime
        writeTrace("action_scheduled", {
            action_id = run.pendingActionId,
            due_relative_ms = run.actionDueAt - run.startedAt,
            owner_actor_id = "owner-player",
        })
        run.timers.dispatch = setTimer(function(expectedId)
            local active = activeRun
            if not active or active.id ~= expectedId then
                return
            end
            setRunStep(active.pendingActionId, "decision-dispatch")
            local attackedPed = active.peds[active.scenario.attackedIndex]
            local sourcePed = active.scenario.sourcePedIndex and active.peds[active.scenario.sourcePedIndex] or active.victim
            triggerClientEvent(active.owner, "nativeAIHarness:decisionAttack", resourceRoot, active.id, active.epoch,
                               active.actionId, attackedPed, sourcePed, active.scenario.expectedResponse)
            writeStage("action_dispatched", {
                action_id = active.actionId,
                attacked_actor_id = active.actorByPed[attackedPed],
                source_actor_id = active.actorByPed[sourcePed] or "victim-player",
                expected_response = active.scenario.expectedResponse or false,
                expected_source_type = active.scenario.expectedSourceType or false,
                expected_allocator = active.scenario.expectedAllocator or false,
                source_weapon = active.scenario.sourceWeapon or 22,
            })
        end, NATIVE_AI_HARNESS.traceLeadTime, 1, run.id)
    elseif run.kind == "melee" then
        setRunStep("prepare", "settle")
        run.pendingActionId = "melee-attack"
        run.actionDueAt = getTickCount() + NATIVE_AI_HARNESS.traceLeadTime
        writeTrace("action_scheduled", {
            action_id = run.pendingActionId,
            due_relative_ms = run.actionDueAt - run.startedAt,
            owner_actor_id = "owner-player",
        })
        run.timers.dispatch = setTimer(function(expectedId)
            if activeRun and activeRun.id == expectedId then
                setRunStep(activeRun.pendingActionId, "dispatch")
                triggerClientEvent(activeRun.owner, "nativeAIHarness:attack", resourceRoot, activeRun.id,
                                   activeRun.epoch, activeRun.actionId)
                writeStage("action_dispatched", {
                    action_id = activeRun.actionId,
                    owner_actor_id = "owner-player",
                    due_relative_ms = activeRun.actionDueAt - activeRun.startedAt,
                    dispatch_relative_ms = getTickCount() - activeRun.startedAt,
                })
            end
        end, NATIVE_AI_HARNESS.traceLeadTime, 1, run.id)
    elseif run.kind == "rotation" then
        setRunStep("prepare", "settle")
        writeTrace("action_scheduled", {
            action_id = "rotation-000",
            due_relative_ms = getTickCount() + NATIVE_AI_HARNESS.traceLeadTime - run.startedAt,
            owner_actor_id = "owner-player",
        })
        beginRotationSequence(run.owner)
    else
        setRunStep("prepare", "settle")
        run.pendingActionId = "group-handoff"
        run.actionDueAt = getTickCount() + NATIVE_AI_HARNESS.traceLeadTime
        writeTrace("action_scheduled", {
            action_id = run.pendingActionId,
            due_relative_ms = run.actionDueAt - run.startedAt,
            owner_actor_id = "owner-player",
        })
        run.timers.dispatch = setTimer(function(expectedId)
            if activeRun and activeRun.id == expectedId then
                setRunStep(activeRun.pendingActionId, "revoke-old-owner")
                triggerClientEvent(activeRun.owner, "nativeAIHarness:releaseOwner", resourceRoot, activeRun.id,
                                   activeRun.epoch, activeRun.actionId)
                writeStage("action_dispatched", {
                    action_id = activeRun.actionId,
                    owner_actor_id = "owner-player",
                    due_relative_ms = activeRun.actionDueAt - activeRun.startedAt,
                    dispatch_relative_ms = getTickCount() - activeRun.startedAt,
                })
            end
        end, NATIVE_AI_HARNESS.traceLeadTime, 1, run.id)
    end
end

local function clientsReady()
    local run = activeRun
    if not run or run.actionScheduled then
        return
    end
    if run.ready[run.owner] and run.ready[run.victim] then
        run.actionScheduled = true
        local syncersGood = true
        for _, ped in ipairs(run.peds) do
            syncersGood = syncersGood and getElementSyncer(ped) == run.owner
        end
        if not assertion("explicit-initial-owner", syncersGood, "owner-player",
                         syncersGood and "owner-player" or "mixed") then
            return finishRun(false, "initial ped ownership mismatch")
        end
        beginScheduledAction()
    end
end

local function startRun(caller, kind, ownerName, victimName)
    if activeRun then
        return outputChatBox("[native AI] A run is already active; use /nativeai cleanup.", caller, 255, 120, 80)
    end
    local scenario = NATIVE_AI_HARNESS.scenarios[kind]
    if not scenario then
        return outputChatBox("[native AI] Unknown scenario. Use /nativeai status for the supported scenario list.", caller,
                             255, 180, 80)
    end
    local owner, victim, reason = resolveRoles(caller, ownerName, victimName)
    if not owner or not victim or owner == victim then
        return outputChatBox("[native AI] Cannot resolve two distinct roles: " .. tostring(reason or "same-player"), caller, 255, 100, 80)
    end
    if getPedOccupiedVehicle(owner) or getPedOccupiedVehicle(victim) then
        return outputChatBox("[native AI] Both players must be on foot before starting.", caller, 255, 100, 80)
    end

    resetLog()
    nextRunSequence = nextRunSequence + 1
    local now = getRealTime()
    activeRun = {
        id = ("run-%d-%03d"):format(now.timestamp, nextRunSequence),
        kind = kind,
        scenario = scenario,
        owner = owner,
        initialOwner = owner,
        victim = victim,
        epoch = 1,
        actionId = "prepare",
        step = "create-actors",
        startedAt = getTickCount(),
        snapshots = {},
        peds = {},
        actorByPed = {},
        ready = {},
        nonceByPed = {},
        bridgeSeen = {},
        stages = {},
        timers = {},
    }
    local run = activeRun
    run.snapshots[owner] = snapshotPlayer(owner)
    run.snapshots[victim] = snapshotPlayer(victim)
    placePlayer(owner, scenario.owner)
    placePlayer(victim, scenario.victim)
    if isGangDecisionKind(kind) and kind ~= "gang_friendly_source" then
        -- IsKillTaskAppropriate reads the threat ped's active weapon, not the
        -- weapon field carried by CEventDamage. Pin the player source to the
        -- exact firearm/melee branch named by this fixture.
        takeAllWeapons(victim)
        local sourceWeapon = tonumber(scenario.sourceWeapon) or 22
        if sourceWeapon ~= 0 and (not giveWeapon(victim, sourceWeapon, 17, true) or getPedWeapon(victim) ~= sourceWeapon) then
            assertion("threat-active-weapon", false, sourceWeapon, getPedWeapon(victim))
            return finishRun(false, "failed to pin the deterministic threat weapon")
        elseif sourceWeapon == 0 and getPedWeapon(victim) ~= 0 then
            assertion("threat-active-weapon", false, 0, getPedWeapon(victim))
            return finishRun(false, "failed to pin the deterministic melee threat")
        end
        -- The native decision is intentionally random, but the resulting
        -- bullet path must not depend on human movement during the capture.
        -- The exact previous frozen state is already part of snapshotPlayer
        -- and is restored by every cleanup path.
        setElementFrozen(victim, true)
    end

    for index, position in ipairs(scenario.peds) do
        local ped = createPed(scenario.pedModels[index], position[1], position[2], position[3], position[4])
        if not ped then
            assertion("actor-creation", false, #scenario.peds, index - 1)
            return finishRun(false, "ped creation refused")
        end
        setElementInterior(ped, 0)
        setElementDimension(ped, NATIVE_AI_HARNESS.dimension)
        setElementHealth(ped, 100)
        takeAllWeapons(ped)
        local expectedWeapon = tonumber(scenario.pedWeapons and scenario.pedWeapons[index]) or 0
        for _, stat in ipairs(standardWeaponStats) do
            setPedStat(ped, stat[1], stat[2])
        end
        if expectedWeapon ~= 0 then
            local clip = weaponClipAmmo[expectedWeapon]
            if not clip or not giveWeapon(ped, expectedWeapon, 9999, true) or
                not setWeaponAmmo(ped, expectedWeapon, 25001, clip) then
                assertion("actor-weapon-state", false, expectedWeapon, getPedWeapon(ped))
                return finishRun(false, "failed to apply deterministic vanilla weapon state")
            end
        end
        setElementData(ped, "neon:nativeAIExpectedWeapon", expectedWeapon)
        -- MTA script peds wrap CPlayerPed and therefore default to
        -- STYLE_GRAB_KICK (15). Stock CPed starts at STYLE_STANDARD (4);
        -- pin it here before the native task so GetStrikeDamage reads the
        -- vanilla UNARMED_1 combo instead of KICK_STD's 8/20 factors.
        if not setPedFightingStyle(ped, scenario.fightingStyle) or
            getPedFightingStyle(ped) ~= scenario.fightingStyle then
            assertion("actor-fighting-style", false, scenario.fightingStyle, getPedFightingStyle(ped))
            return finishRun(false, "failed to apply stock ped fighting style")
        end
        setElementSyncer(ped, owner, true, true)
        run.peds[index] = ped
        run.actorByPed[ped] = "ped-" .. index
    end

    local traceActors = {
        {actor_id = "owner-player", type = "player", role = "initial-owner", transform = scenario.owner},
        {actor_id = isHandoffKind(kind) and "next-owner-player" or "victim-player", type = "player",
         role = isHandoffKind(kind) and "next-owner" or "victim", transform = scenario.victim},
    }
    for index, position in ipairs(scenario.peds) do
        local groupId = isRotationKind(kind) and false or "group-1"
        for partitionIndex, partition in ipairs(scenario.groupPartitions or {}) do
            for _, memberIndex in ipairs(partition) do
                if memberIndex == index then groupId = "group-" .. partitionIndex end
            end
        end
        traceActors[#traceActors + 1] = {
            actor_id = "ped-" .. index,
            type = "ped",
            model = scenario.pedModels[index],
            group_id = groupId,
            owner_actor_id = "owner-player",
            fighting_style = scenario.fightingStyle,
            weapon = scenario.pedWeapons and scenario.pedWeapons[index] or 0,
            transform = position,
        }
    end

    setRunStep("prepare", "wait-client-leases")
    writeTrace("run_start", {
        owner_actor_id = "owner-player",
        victim_actor_id = isHandoffKind(kind) and "next-owner-player" or "victim-player",
        actors = traceActors,
        epoch = run.epoch,
        victim_input_pinned = isGangDecisionKind(kind) and kind ~= "gang_friendly_source",
    })
    announce(("Run %s: owner and %s roles assigned. Do not move; action is automatic."):format(
                 run.id, isHandoffKind(kind) and "next-owner" or "victim"))
    triggerClientEvent(owner, "nativeAIHarness:prepare", resourceRoot, run.id, scenario.id, run.epoch,
                       run.peds, owner, victim, "owner", kind)
    triggerClientEvent(victim, "nativeAIHarness:prepare", resourceRoot, run.id, scenario.id, run.epoch,
                       run.peds, owner, victim, "observer", kind)

    run.timers.timeout = setTimer(function(expectedId)
        if activeRun and activeRun.id == expectedId and not activeRun.finished then
            assertion("scenario-timeout", false, "completed", activeRun.step)
            finishRun(false, "timeout at step " .. tostring(activeRun.step))
        end
    end, NATIVE_AI_HARNESS.scenarioTimeout, 1, run.id)
end

addCommandHandler("nativeai", function(player, _, action, kind, ownerName, victimName)
    action = action and action:lower() or "status"
    if action == "run" then
        return startRun(player, kind and kind:lower() or "", ownerName, victimName)
    elseif action == "cleanup" then
        if activeRun then
            finishRun(false, "manual cleanup before verdict")
            cleanupRun("manual")
        else
            outputChatBox("[native AI] No active run.", player, 255, 180, 80)
        end
    elseif action == "status" then
        if activeRun then
            outputChatBox(("[native AI] %s scenario=%s step=%s action=%s owner=%s victim=%s"):format(
                              activeRun.id, activeRun.scenario.id, activeRun.step, activeRun.actionId,
                              getPlayerName(activeRun.owner), getPlayerName(activeRun.victim)), player, 180, 220, 255)
        else
            outputChatBox("[native AI] No active run. Core: melee, handoff, rotation, rotation_handoff. Gang: gang_unarmed_flee, gang_armed_leader, gang_armed_member, gang_armed_handoff, gang_friendly_source, gang_two_armed_two_unarmed_melee.", player, 180, 220, 255)
        end
    else
        outputChatBox("[native AI] /nativeai run <scenario> [owner] [victim-or-next-owner]", player, 255, 180, 80)
    end
end)

local function completeRotationActionIfReady(run, actionId)
    local action = run.rotationActions and run.rotationActions[actionId]
    if not action or action.completed or not action.ownerApplied or not action.converged[run.owner] or not action.converged[run.victim] then
        return
    end
    local ownerEvidence = action.converged[run.rotationOwner]
    local observer = run.rotationOwner == run.owner and run.victim or run.owner
    local observerEvidence = action.converged[observer]
    local converged = ownerEvidence and observerEvidence and ownerEvidence.role == "owner" and observerEvidence.role == "observer"
    assertion("rotation-owner-observer-convergence:" .. actionId, converged == true, "owner+observer", converged and "complete" or "role mismatch",
              {owner = ownerEvidence, observer = observerEvidence})
    if not converged then
        return finishRun(false, "rotation convergence roles mismatched")
    end
    action.completed = true
    writeStage("rotation_action_converged", {
        action_id = actionId,
        actor_id = "ped-2",
        target_heading = observerEvidence.targetHeading,
        owner_elapsed_ms = ownerEvidence.elapsedMs,
        observer_elapsed_ms = observerEvidence.elapsedMs,
        owner_delta_degrees = ownerEvidence.deltaDegrees,
        observer_delta_degrees = observerEvidence.deltaDegrees,
        epoch = run.epoch,
    })
end

addEvent("nativeAIHarness:evidence", true)
addEventHandler("nativeAIHarness:evidence", resourceRoot, function(runId, epoch, evidence, data)
    local run = activeRun
    if source ~= resourceRoot or not run or run.id ~= tostring(runId) or run.epoch ~= tonumber(epoch) or
        (client ~= run.owner and client ~= run.victim) or type(data) ~= "table" then
        return
    end
    writeTrace("client_evidence", {
        actor_id = client == run.initialOwner and "owner-player" or
            (isHandoffKind(run.kind) and "next-owner-player" or "victim-player"),
        action_id = data.actionId and tostring(data.actionId) or run.actionId,
        evidence = tostring(evidence),
        details = data,
    })

    if evidence == "ready" and not run.ready[client] then
        if data.role ~= (client == run.owner and "owner" or "observer") then
            return finishRun(false, "client reported the wrong role")
        end
        run.ready[client] = true
        clientsReady()
    elseif evidence == "failure" then
        finishRun(false, ("%s: %s"):format(client == run.owner and "owner-player" or "victim-player",
                                            tostring(data.reason)))
    elseif evidence == "attack-dispatched" and client == run.owner and run.kind == "melee" then
        local accepted = tonumber(data.accepted) or 0
        if not assertion("native-attack-dispatch", accepted == #run.peds, #run.peds, accepted) then
            finishRun(false, "native combat task dispatch failed")
        end
    elseif evidence == "owner-released" and client == run.owner and isHandoffKind(run.kind) and run.step == "revoke-old-owner" then
        assertion("old-owner-release", data.released == true, true, data.released)
        run.epoch = run.epoch + 1
        for _, ped in ipairs(run.peds) do
            setElementSyncer(ped, run.victim, true, true)
        end
        if run.kind == "rotation_handoff" then
            triggerClientEvent(run.owner, "nativeAIHarness:updateObserverEpoch", resourceRoot, run.id, run.epoch)
        end
        setRunStep("group-handoff-acquire", "acquire-new-owner")
        run.timers.acquire = setTimer(function(expectedId)
            if activeRun and activeRun.id == expectedId then
                triggerClientEvent(activeRun.victim, "nativeAIHarness:acquireOwner", resourceRoot,
                                   activeRun.id, activeRun.epoch, activeRun.actionId)
            end
        end, NATIVE_AI_HARNESS.traceLeadTime, 1, run.id)
    elseif evidence == "owner-acquired" and client == run.victim and isHandoffKind(run.kind) and run.step == "acquire-new-owner" then
        local syncersGood = true
        for _, ped in ipairs(run.peds) do
            syncersGood = syncersGood and getElementSyncer(ped) == run.victim
        end
        local acquired = data.acquired == true and syncersGood
        assertion("new-owner-acquire", acquired, "next-owner-player", acquired and "next-owner-player" or "inactive/mixed")
        if not acquired then
            finishRun(false, "new owner failed to acquire native group")
        elseif run.kind == "handoff" then
            finishRun(true, "native group transferred to epoch 2")
        elseif run.kind == "gang_armed_handoff" then
            run.owner = run.victim
            setRunStep("firearm-threat-after-handoff", "decision-dispatch")
            local attackedPed = run.peds[run.scenario.attackedIndex]
            triggerClientEvent(run.owner, "nativeAIHarness:decisionAttack", resourceRoot, run.id, run.epoch,
                               run.actionId, attackedPed, run.victim, run.scenario.expectedResponse)
            writeStage("handoff_decision_dispatched", {
                action_id = run.actionId,
                attacked_actor_id = run.actorByPed[attackedPed],
                owner_actor_id = "next-owner-player",
                epoch = run.epoch,
            })
        else
            beginRotationSequence(run.victim)
        end
    elseif evidence == "rotation-owner-applied" and isRotationKind(run.kind) and client == run.rotationOwner then
        local actionId = tostring(data.actionId)
        local action = run.rotationActions and run.rotationActions[actionId]
        if not action then
            return
        end
        action.ownerApplied = data.accepted == true
        if not assertion("rotation-owner-applied:" .. actionId, action.ownerApplied, true, data.accepted,
                         {target_heading = data.targetHeading, actual_heading = data.actualHeading}) then
            finishRun(false, "owner failed to apply " .. actionId)
        else
            completeRotationActionIfReady(run, actionId)
        end
    elseif evidence == "rotation-owner-held" and isRotationKind(run.kind) and client == run.rotationOwner then
        local actionId = tostring(data.actionId)
        local action = run.rotationActions and run.rotationActions[actionId]
        if not action then
            return
        end
        action.ownerHeld = true
        writeStage("rotation_owner_held", {
            action_id = actionId,
            actor_id = tostring(data.actorId or "ped-2"),
            target_heading = tonumber(data.targetHeading),
            actual_heading = tonumber(data.actualHeading),
            delta_degrees = tonumber(data.deltaDegrees),
            elapsed_ms = tonumber(data.elapsedMs),
            consecutive_frames = tonumber(data.consecutiveFrames),
            epoch = run.epoch,
        })
        completeRotationActionIfReady(run, actionId)
    elseif evidence == "rotation-owner-drift" and isRotationKind(run.kind) and client == run.rotationOwner then
        local actionId = tostring(data.actionId)
        assertion("rotation-owner-held:" .. actionId, false, "stable native heading", data.actualHeading, {
            target_heading = data.targetHeading,
            delta_degrees = data.deltaDegrees,
            elapsed_ms = data.elapsedMs,
        })
        finishRun(false, "owner heading drifted before network attribution for " .. actionId)
    elseif evidence == "rotation-converged" and isRotationKind(run.kind) then
        local actionId = tostring(data.actionId)
        local action = run.rotationActions and run.rotationActions[actionId]
        if not action then
            return
        end
        action.converged[client] = data
        completeRotationActionIfReady(run, actionId)
    elseif evidence == "decision-injected" and client == run.owner and isGangDecisionKind(run.kind) then
        if not assertion("native-group-decision-injection", data.accepted == true, true, data.accepted, data) then
            finishRun(false, "native group decision event was rejected")
        end
    elseif evidence == "collective-response-timeout" and client == run.owner and isGangDecisionKind(run.kind) then
        assertion("collective-" .. tostring(data.response), false, "every group member", data.members, data)
        finishRun(false, "group did not reach the expected collective response")
    elseif evidence == "allocator-capture" and client == run.owner and run.kind == "gang_two_armed_two_unarmed_melee" then
        writeStage("allocator_capture_complete", {
            action_id = run.actionId,
            members = data.members,
            epoch = run.epoch,
        })
        finishRun(true, "active tasks were masked or incomplete; allocator JSONL is the authoritative verdict")
    elseif evidence == "collective-response" and client == run.owner and isGangDecisionKind(run.kind) then
        local response = tostring(data.response)
        local responseAllowed = response == run.scenario.expectedResponse or
                                    run.scenario.branchAware == true and response == "flee"
        if not assertion("native-selected-collective-response", responseAllowed, run.scenario.branchAware and "fight|flee" or
                             run.scenario.expectedResponse, response, data) then
            return finishRun(false, "group allocation did not match an allowed native response")
        end
        writeStage("collective_response_observed", {
            action_id = run.actionId,
            response = response,
            members = data.members,
            epoch = run.epoch,
        })
        run.selectedResponse = response
        local expectedAllocation = run.scenario.expectedAllocator == "all-kill" and "all four members assigned kill" or
            (response == "fight" and "armed attack plus unarmed cover" or "every group member")
        assertion("collective-" .. response, true, expectedAllocation, "matched", data)
        if response == "flee" and run.scenario.branchAware then
            finishRun(true, "weighted native decision selected the collective flee branch; damage chain not exercised")
        elseif run.scenario.expectedResponse == "flee" then
            finishRun(true, "unarmed group selected the collective firearm-threat flee path")
        elseif run.kind == "gang_armed_handoff" then
            finishRun(true, "armed collective fight response survived the owner handoff")
        elseif run.kind == "gang_two_armed_two_unarmed_melee" then
            finishRun(true, "four-member melee allocation captured; analyzer must confirm four kill assignments and any DUCK overlay")
        end
    elseif evidence == "classification-captured" and client == run.owner and run.kind == "gang_friendly_source" then
        writeStage("classification_trace_captured", {
            action_id = run.actionId,
            expected_source_type = run.scenario.expectedSourceType,
        })
        finishRun(true, "classification trace captured; analyzer must confirm event_source_type=2")
    elseif evidence == "damage-applied" and client == run.victim and
        (run.kind == "melee" or run.scenario.expectedResponse == "fight") then
        local before = tonumber(data.healthBefore)
        local after = tonumber(data.healthAfter)
        writeStage("victim_injection_result", {
            actor_id = tostring(data.actorId or "unknown"),
            action_id = run.actionId,
            nonce = tonumber(data.nonce),
            accepted = data.accepted == true,
            health_before = before,
            health_immediate = tonumber(data.healthImmediate),
            health_after = after,
            armor_before = tonumber(data.armorBefore),
            armor_immediate = tonumber(data.armorImmediate),
            armor_after = tonumber(data.armorAfter),
        })
        local applied = data.accepted == true and before and after and after < before
        assertion("victim-native-damage", applied == true, "accepted and healthAfter < healthBefore",
                  ("accepted=%s %.2f->%.2f"):format(tostring(data.accepted), before or -1, after or -1))
        if applied then
            triggerClientEvent(run.owner, "nativeAIHarness:stopCombat", resourceRoot, run.id, run.epoch)
        end
        writeStage("final_observation", {
            action_id = run.actionId,
            passed = applied == true,
            expected = "accepted native injection and lower authoritative-victim health",
            actual = ("accepted=%s health=%.2f->%.2f armor=%.2f->%.2f"):format(
                tostring(data.accepted), before or -1, after or -1, tonumber(data.armorBefore) or -1,
                tonumber(data.armorAfter) or -1),
        })
        finishRun(applied == true,
                  applied and "remote native attack reached the authoritative victim" or
                      "victim replay produced no health loss")
    end
end)

addEvent("nativeAIHarness:nativeDamageObserved", true)
addEventHandler("nativeAIHarness:nativeDamageObserved", resourceRoot,
                function(runId, epoch, actionId, attackingPed, victim, nonce, weapon, bodypart, damageFactor, direction)
    local run = activeRun
    if source ~= resourceRoot or not run or run.finished or
        (run.kind ~= "melee" and run.scenario.expectedResponse ~= "fight") or client ~= run.owner or
        run.id ~= tostring(runId) then
        return
    end
    epoch = tonumber(epoch)
    nonce = tonumber(nonce)
    weapon = tonumber(weapon)
    bodypart = tonumber(bodypart)
    damageFactor = tonumber(damageFactor)
    direction = tonumber(direction)
    local actorId = run.actorByPed[attackingPed] or "unknown"
    writeStage("owner_native_damage_attempt", {
        actor_id = actorId,
        action_id = tostring(actionId),
        epoch = epoch,
        nonce = nonce,
        weapon = weapon,
        bodypart = bodypart,
        damage_factor = damageFactor,
        direction = direction,
    })

    local function reject(reason, expected, actual)
        if run.validationFailed then
            return
        end
        run.validationFailed = true
        writeTrace("server_validation_rejected", {
            actor_id = actorId,
            action_id = tostring(actionId),
            reason = reason,
            expected = expected,
            actual = actual,
        })
        assertion("server-forward-validation:" .. reason, false, expected, actual)
        writeStage("final_observation", {
            action_id = tostring(actionId),
            passed = false,
            first_divergence = "server_validation_rejected",
            actual = reason,
        })
        finishRun(false, "first causal failure: server validation rejected " .. reason)
    end

    if run.epoch ~= epoch then
        return reject("epoch", run.epoch, epoch)
    elseif run.actionId ~= tostring(actionId) then
        return reject("action-id", run.actionId, tostring(actionId))
    elseif actorId == "unknown" or not isElement(attackingPed) then
        return reject("actor", "scenario ped", actorId)
    elseif victim ~= run.victim then
        return reject("victim", "victim-player", "other")
    elseif getElementSyncer(attackingPed) ~= run.owner then
        return reject("syncer", "owner-player", "other")
    elseif run.forwarded then
        writeTrace("server_validation_suppressed", {
            actor_id = actorId,
            action_id = tostring(actionId),
            reason = "scenario-already-forwarded-one-hit",
            nonce = nonce,
        })
        return
    elseif not nonce or nonce < 1 then
        return reject("nonce", "integer >= 1", nonce)
    elseif not weapon or weapon ~= getPedWeapon(attackingPed) then
        return reject("weapon", getPedWeapon(attackingPed), weapon)
    elseif not bodypart or (bodypart ~= 0 and (bodypart < 3 or bodypart > 9)) then
        return reject("bodypart", "0 or 3..9", bodypart)
    elseif getPedFightingStyle(attackingPed) ~= run.scenario.fightingStyle then
        return reject("fighting-style", run.scenario.fightingStyle, getPedFightingStyle(attackingPed))
    elseif not damageFactor or
        (weapon == 0 and not canonicalGangMeleeFactors[damageFactor]) or
        (weapon ~= 0 and canonicalGangFirearmFactors[weapon] ~= damageFactor) then
        return reject("damage-factor", weapon == 0 and "5|6|9|15|25" or canonicalGangFirearmFactors[weapon], damageFactor)
    elseif not direction or direction < 0 or direction > 3 then
        return reject("direction", "0..3", direction)
    end
    local px, py, pz = getElementPosition(attackingPed)
    local vx, vy, vz = getElementPosition(victim)
    if getElementDimension(attackingPed) ~= getElementDimension(victim) or
        getElementInterior(attackingPed) ~= getElementInterior(victim) or
        getDistanceBetweenPoints3D(px, py, pz, vx, vy, vz) > (weapon == 30 and 75 or weapon == 0 and NATIVE_AI_HARNESS.meleeDamageRadius or 40) then
        local allowedRadius = weapon == 30 and 75 or weapon == 0 and NATIVE_AI_HARNESS.meleeDamageRadius or 40
        return reject("distance-or-world", "same world within " .. allowedRadius .. "m",
                      ("distance=%.2f"):format(getDistanceBetweenPoints3D(px, py, pz, vx, vy, vz)))
    end
    local previous = run.nonceByPed[attackingPed] or 0
    if nonce <= previous then
        assertion("damage-nonce-monotonic", false, "> " .. previous, nonce)
        return
    end
    run.nonceByPed[attackingPed] = nonce
    local bridgeKey = run.actorByPed[attackingPed] .. ":" .. nonce
    if run.bridgeSeen[bridgeKey] then
        assertion("single-damage-bridge", false, "unique", bridgeKey)
        return
    end
    run.bridgeSeen[bridgeKey] = true
    assertion("owner-native-damage-attempt", true, "one authenticated attempt", bridgeKey)
    writeStage("server_validated_forward", {
        actor_id = run.actorByPed[attackingPed],
        action_id = run.actionId,
        epoch = run.epoch,
        nonce = nonce,
        victim_actor_id = "victim-player",
        validation = "owner+epoch+syncer+target+weapon+factor+direction+distance+nonce",
    })
    run.forwarded = true
    triggerClientEvent(run.victim, "nativeAIHarness:applyDamage", resourceRoot, run.id, run.epoch,
                       run.actionId, attackingPed, nonce, weapon, bodypart, damageFactor, direction)
end)

addEvent("nativeAIHarness:stopped", true)
addEventHandler("nativeAIHarness:stopped", resourceRoot, function(runId, groupReleased)
    local run = activeRun
    if source ~= resourceRoot or not run or not run.cleanupPending or run.id ~= tostring(runId) or
        (client ~= run.initialOwner and client ~= run.victim) or run.cleanupAcks[client] then
        return
    end
    run.cleanupAcks[client] = true
    writeTrace("cleanup_ack", {
        actor_id = client == run.initialOwner and "owner-player" or
            (isHandoffKind(run.kind) and "next-owner-player" or "victim-player"),
        group_released = groupReleased == true,
    })
    if groupReleased ~= true then
        writeTrace("cleanup_release_failure", {details = "client native group release returned false"})
    end
    if run.cleanupAcks[run.initialOwner] and run.cleanupAcks[run.victim] then
        finalizeCleanup(run)
    end
end)

addEventHandler("onPlayerQuit", root, function()
    if activeRun and (source == activeRun.initialOwner or source == activeRun.victim) then
        finishRun(false, "participant quit before verdict")
        cleanupRun("participant-quit")
    end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    finishRun(false, "resource stopped before verdict")
    cleanupRun("resource-stop", true)
end)

outputServerLog("[native-ai-harness] Ready: core melee/handoff/rotation plus gang_unarmed_flee, gang_armed_leader, gang_armed_member, gang_armed_handoff, gang_friendly_source, gang_two_armed_two_unarmed_melee")
