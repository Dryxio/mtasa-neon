local TASK_NAME = "TASK_COMPLEX_FOLLOW_NODE_ROUTE"
local run

local function report(phase, ok, details)
    if run then
        triggerServerEvent("nativePedNavigateHarness:evidence", resourceRoot, run.id, phase, ok, details)
    end
end

local function stopRun()
    if run and isTimer(run.timer) then
        killTimer(run.timer)
    end
    run = nil
end

local function beginOwner(runId, ped, targetX, targetY, targetZ, attempt)
    attempt = attempt or 1
    if not run or run.id ~= runId or not isElement(ped) then
        return
    end
    if not isElementStreamedIn(ped) or not isElementSyncer(ped) then
        if attempt < 20 then
            return setTimer(beginOwner, 250, 1, runId, ped, targetX, targetY, targetZ, attempt + 1)
        end
        report("accepted", false, "owner non streame ou non-syncer apres 5 s")
        return stopRun()
    end

    local target = Vector3(targetX, targetY, targetZ)
    local invalidRejected = not setPedNavigateTo(ped, target, "crawl") and not setPedNavigateTo(ped, target, "run", 0.0) and
        not setPedNavigateTo(ped, target, "run", 2.0, 1.0) and not setPedNavigateTo(ped, target, "run", 0.5, 3.0, 0.0) and
        not setPedNavigateTo(ped, target, "run", 0.5, 3.0, 2.0, false, -2)
    report("validation", invalidRejected, invalidRejected and "invalid descriptors rejected" or "an invalid descriptor was accepted")
    if not invalidRejected then
        return stopRun()
    end

    local accepted = setPedNavigateTo(ped, target, "run", 0.75, 3.0, 2.0, false, -1, true)
    report("accepted", accepted, accepted and "native script command submitted" or "setPedNavigateTo returned false")
    if not accepted then
        return stopRun()
    end

    run.startedAt = getTickCount()
    run.seenTask = false
    run.timer = setTimer(function()
        if not run or not isElement(run.ped) then
            return
        end
        local running = isPedDoingTask(run.ped, TASK_NAME)
        run.seenTask = run.seenTask or running
        local x, y, z = getElementPosition(run.ped)
        local distance = getDistanceBetweenPoints3D(x, y, z, run.targetX, run.targetY, run.targetZ)
        local elapsed = getTickCount() - run.startedAt

        if run.seenTask and not running then
            local ok = distance <= 1.25
            report("arrived", ok, ("distance=%.2f elapsed=%d taskSeen=true"):format(distance, elapsed))
            return stopRun()
        end
        if not run.seenTask and elapsed > 2500 then
            report("arrived", false, "TASK_COMPLEX_FOLLOW_NODE_ROUTE never became active")
            return stopRun()
        end
    end, 100, 0)
end

addEvent("nativePedNavigateHarness:start", true)
addEventHandler("nativePedNavigateHarness:start", resourceRoot, function(runId, ped, targetX, targetY, targetZ)
    stopRun()
    run = {id = runId, role = "owner", ped = ped, targetX = targetX, targetY = targetY, targetZ = targetZ}
    beginOwner(runId, ped, targetX, targetY, targetZ)
end)

addEvent("nativePedNavigateHarness:observe", true)
addEventHandler("nativePedNavigateHarness:observe", resourceRoot, function(runId, ped, startX, startY, startZ)
    stopRun()
    run = {id = runId, role = "observer", ped = ped, startedAt = getTickCount()}
    run.timer = setTimer(function()
        if not run or not isElement(run.ped) then
            return
        end
        if isPedDoingTask(run.ped, TASK_NAME) then
            report("observer", false, "non-syncer constructed the native navigation task")
            return stopRun()
        end

        local x, y, z = getElementPosition(run.ped)
        local displacement = getDistanceBetweenPoints3D(x, y, z, startX, startY, startZ)
        if displacement >= 3.0 then
            report("observer", true, ("passive synchronized displacement=%.2f moveState=%s"):format(displacement, tostring(getPedMoveState(run.ped))))
            return stopRun()
        end
        if getTickCount() - run.startedAt > 30000 then
            report("observer", false, ("no passive movement evidence; displacement=%.2f"):format(displacement))
            return stopRun()
        end
    end, 100, 0)
end)

addEventHandler("onClientResourceStop", resourceRoot, stopRun)
