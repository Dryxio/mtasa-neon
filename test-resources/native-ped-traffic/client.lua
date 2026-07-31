local enabled = false
local debugEnabled = false
local assignments = {}
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
        type(setPedWander) ~= "function" or type(setPedUseNativeWalkingStyle) ~= "function" then
        return fail(task, "native-api-missing")
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

addEventHandler("onClientPreRender", root, function()
    if enabled and getElementDimension(localPlayer) == 0 and getElementInterior(localPlayer) == 0 and
        type(updateAmbientPedPopulationModels) == "function" then
        local x, y, z = getElementPosition(localPlayer)
        updateAmbientPedPopulationModels(x, y, z)
    end
end)

addEventHandler("onClientElementDestroy", root, function()
    local task = assignments[source]
    if task then releaseTask(task, false) end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    local current = {}
    for _, task in pairs(assignments) do current[#current + 1] = task end
    for _, task in ipairs(current) do releaseTask(task, true) end
    if type(resetAmbientPedPopulationModels) == "function" then
        resetAmbientPedPopulationModels()
    end
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    triggerServerEvent("pedTraffic:ready", resourceRoot)
end)

setTimer(function()
    if debugEnabled then
        local active = 0
        for _, task in pairs(assignments) do
            if task.accepted then active = active + 1 end
        end
        log(("telemetry active=%d hits=%d misses=%d assignments=%d failures=%d missReasons=%s"):format(
                active, stats.candidateHits, stats.candidateMisses, stats.assignments, stats.failures, formatReasons(stats.missReasons)))
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
            log(("ped id=%d syncer=%s streamed=%s nativeStyle=%s move=%s speed=%.5f velocity=(%.5f,%.5f,%.5f) pos=(%.2f,%.2f,%.2f)"):format(
                    id, tostring(isElementSyncer(ped)), tostring(isElementStreamedIn(ped)), tostring(nativeStyle), tostring(moveState), horizontalSpeed,
                    vx, vy, vz, x, y, z))
        end
    end
end, 2000, 0)
