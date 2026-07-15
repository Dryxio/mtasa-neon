local TASK_NAME = "TASK_COMPLEX_GO_TO_POINT_AND_STAND_STILL"
local activeTest

local function distanceToTarget(test)
    local x, y, z = getElementPosition(test.ped)
    return getDistanceBetweenPoints2D(x, y, test.x, test.y), math.abs(z - test.z), getDistanceBetweenPoints3D(x, y, z, test.x, test.y, test.z)
end

local function report(result, details)
    if not activeTest then
        return
    end

    triggerServerEvent("nativePedGoTo:result", resourceRoot, activeTest.ped, result, details)
    if isTimer(activeTest.monitor) then
        killTimer(activeTest.monitor)
    end
    activeTest = nil
end

local function beginNativeTask(ped, x, y, z, movement, timeout, attempt)
    attempt = attempt or 1
    if not isElement(ped) then
        return report("destroyed", "ped detruit avant la creation de la task")
    end

    if not isElementStreamedIn(ped) or not isElementSyncer(ped) then
        if attempt < 20 then
            setTimer(beginNativeTask, 250, 1, ped, x, y, z, movement, timeout, attempt + 1)
            return
        end
        return report("refused", "ped non streame ou client non-syncer apres 5 s")
    end

    local accepted = setPedGoTo(ped, Vector3(x, y, z), movement, 0.5, 2.0, timeout)
    if not accepted then
        return report("refused", "setPedGoTo a retourne false")
    end

    activeTest.acceptedAt = getTickCount()
    activeTest.seenNativeTask = false
    activeTest.monitor = setTimer(function()
        local test = activeTest
        if not test then
            return
        end
        if not isElement(test.ped) then
            return report("destroyed", "ped detruit pendant la task")
        end

        local running = isPedDoingTask(test.ped, TASK_NAME)
        test.seenNativeTask = test.seenNativeTask or running
        local distance2D, deltaZ, distance3D = distanceToTarget(test)
        local elapsed = getTickCount() - test.acceptedAt

        if test.seenNativeTask and not running then
            local details = ("distance2D=%.2f m, deltaZ=%.2f m, distance3D=%.2f m, elapsed=%d ms"):format(distance2D, deltaZ, distance3D,
                                                                                                           elapsed)
            if distance2D <= 0.75 then
                local result = elapsed >= test.timeout - 250 and "timeout_relocated" or "arrived"
                return report(result, details)
            end

            -- A disappearing complex task is not enough evidence for an
            -- interruption: GTA may also end GoToPoint after overshooting or
            -- circling the target. The future native task manager will retain
            -- GTA's success flag; this harness reports the ambiguous state.
            return report("ended_outside_radius", details)
        end

        if not test.seenNativeTask and elapsed > 1500 then
            return report("refused", "task native jamais observee dans le task manager")
        end
    end, 100, 0)
end

addEvent("nativePedGoTo:start", true)
addEventHandler("nativePedGoTo:start", resourceRoot, function(ped, x, y, z, movement, timeout)
    if activeTest and isTimer(activeTest.monitor) then
        killTimer(activeTest.monitor)
    end

    activeTest = {
        ped = ped,
        x = x,
        y = y,
        z = z,
        movement = movement,
        timeout = timeout,
    }
    beginNativeTask(ped, x, y, z, movement, timeout)
end)

addEvent("nativePedGoTo:cancel", true)
addEventHandler("nativePedGoTo:cancel", resourceRoot, function(ped)
    if not activeTest or activeTest.ped ~= ped or not isElement(ped) then
        return
    end

    local killed = killPedTask(ped, "primary", 3, false)
    report("cancelled", killed and "slot PRIMARY supprime" or "killPedTask a retourne false")
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    if activeTest and isTimer(activeTest.monitor) then
        killTimer(activeTest.monitor)
    end
    activeTest = nil
end)
