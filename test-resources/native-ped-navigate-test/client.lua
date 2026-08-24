local TASK_NAME = "TASK_COMPLEX_FOLLOW_NODE_ROUTE"
local active

local function finish(result, details)
    if not active then
        return
    end

    triggerServerEvent("nativePedNavigateVisual:result", resourceRoot, active.ped, result, details)
    if isTimer(active.timer) then
        killTimer(active.timer)
    end
    active = nil
end

local function begin(ped, x, y, z, movement, attempt)
    attempt = attempt or 1
    if not isElement(ped) then
        return finish("destroyed", "ped detruit avant le dispatch")
    end

    if not isElementStreamedIn(ped) or not isElementSyncer(ped) then
        if attempt < 20 then
            return setTimer(begin, 250, 1, ped, x, y, z, movement, attempt + 1)
        end
        return finish("refused", "ped non streame ou client non-syncer apres 5 s")
    end

    if not setPedNavigateTo(ped, Vector3(x, y, z), movement) then
        return finish("refused", "setPedNavigateTo a retourne false")
    end

    active.acceptedAt = getTickCount()
    active.seenTask = false
    triggerServerEvent("nativePedNavigateVisual:result", resourceRoot, ped, "accepted", "task native soumise sur le syncer")
    active.timer = setTimer(function()
        if not active or not isElement(active.ped) then
            return finish("destroyed", "ped detruit pendant la navigation")
        end

        local running = isPedDoingTask(active.ped, TASK_NAME)
        active.seenTask = active.seenTask or running
        local px, py, pz = getElementPosition(active.ped)
        local distance = getDistanceBetweenPoints3D(px, py, pz, active.x, active.y, active.z)
        local elapsed = getTickCount() - active.acceptedAt

        if active.seenTask and not running then
            return finish(distance <= 1.25 and "arrived" or "ended_outside_radius",
                ("distance=%.2f m, elapsed=%d ms"):format(distance, elapsed))
        end
        if not active.seenTask and elapsed > 2000 then
            return finish("refused", "TASK_COMPLEX_FOLLOW_NODE_ROUTE jamais observee")
        end
        if elapsed > 60000 then
            return finish("timeout", ("distance restante=%.2f m"):format(distance))
        end
    end, 100, 0)
end

addEvent("nativePedNavigateVisual:start", true)
addEventHandler("nativePedNavigateVisual:start", resourceRoot, function(ped, x, y, z, movement)
    if active and isTimer(active.timer) then
        killTimer(active.timer)
    end
    active = {ped = ped, x = x, y = y, z = z}
    begin(ped, x, y, z, movement)
end)

addEvent("nativePedNavigateVisual:stop", true)
addEventHandler("nativePedNavigateVisual:stop", resourceRoot, function(ped)
    if active and active.ped == ped and isElement(ped) then
        local killed = killPedTask(ped, "primary", 3, false)
        finish("cancelled", killed and "slot primaire supprime" or "killPedTask a retourne false")
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    if active and isTimer(active.timer) then
        killTimer(active.timer)
    end
    active = nil
end)
