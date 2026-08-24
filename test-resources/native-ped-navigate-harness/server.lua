local START = {2488.0, -1666.0, 13.34}
local TARGET = {2464.0, -1666.0, 13.34}
local session
local nextRunId = 0

local function snapshotPlayer(player)
    local x, y, z = getElementPosition(player)
    return {
        x = x,
        y = y,
        z = z,
        interior = getElementInterior(player),
        dimension = getElementDimension(player),
        frozen = isElementFrozen(player),
    }
end

local function restorePlayer(player, snapshot)
    if not isElement(player) or not snapshot then
        return
    end
    setElementInterior(player, snapshot.interior)
    setElementDimension(player, snapshot.dimension)
    setElementPosition(player, snapshot.x, snapshot.y, snapshot.z)
    setElementFrozen(player, snapshot.frozen)
end

local function cleanup()
    if not session then
        return
    end
    if isTimer(session.timeoutTimer) then
        killTimer(session.timeoutTimer)
    end
    if isTimer(session.sampleTimer) then
        killTimer(session.sampleTimer)
    end
    if isElement(session.ped) then
        destroyElement(session.ped)
    end
    restorePlayer(session.owner, session.ownerSnapshot)
    restorePlayer(session.observer, session.observerSnapshot)
    session = nil
end

local function finish(passed, details)
    if not session or session.finished then
        return
    end
    session.finished = true
    local prefix = passed and "PASS" or "FAIL"
    local color = passed and {100, 230, 130} or {255, 80, 80}
    outputChatBox(("[native navigate harness] %s: %s"):format(prefix, details), root, unpack(color))
    outputDebugString(("[native navigate harness] %s: %s"):format(prefix, details), passed and 3 or 1)
    setTimer(cleanup, 1500, 1)
end

local function maybePass()
    if not session or session.finished then
        return
    end
    if not session.validationPassed or not session.accepted or not session.ownerArrived or not session.observerPassive then
        return
    end

    local x, y, z = getElementPosition(session.ped)
    local finalDistance = getDistanceBetweenPoints3D(x, y, z, unpack(TARGET))
    if session.maxDisplacement < 10.0 or finalDistance > 1.5 then
        return finish(false, ("server displacement=%.2f finalDistance=%.2f"):format(session.maxDisplacement, finalDistance))
    end

    finish(true, ("native route active; server displacement=%.2f m, finalDistance=%.2f m; observer passive"):format(session.maxDisplacement,
                                                                                                                     finalDistance))
end

local function findObserver(owner)
    for _, player in ipairs(getElementsByType("player")) do
        if player ~= owner then
            return player
        end
    end
end

addCommandHandler("nativepathharness", function(owner)
    if session then
        return outputChatBox("[native navigate harness] Un run est deja actif.", owner, 255, 170, 80)
    end

    local observer = findObserver(owner)
    if not observer then
        return outputChatBox("[native navigate harness] Deux clients connectes sont requis.", owner, 255, 170, 80)
    end

    nextRunId = nextRunId + 1
    local dimension = 6800 + (nextRunId % 1000)
    session = {
        id = nextRunId,
        owner = owner,
        observer = observer,
        ownerSnapshot = snapshotPlayer(owner),
        observerSnapshot = snapshotPlayer(observer),
        maxDisplacement = 0,
    }

    setElementInterior(owner, 0)
    setElementInterior(observer, 0)
    setElementDimension(owner, dimension)
    setElementDimension(observer, dimension)
    setElementPosition(owner, START[1] + 3.0, START[2] - 4.0, START[3])
    setElementPosition(observer, START[1] - 8.0, START[2] - 5.0, START[3])
    setElementFrozen(owner, true)
    setElementFrozen(observer, true)

    local ped = createPed(270, START[1], START[2], START[3], 270.0)
    if not ped then
        cleanup()
        return outputChatBox("[native navigate harness] Creation du ped impossible.", owner, 255, 80, 80)
    end
    session.ped = ped
    setElementDimension(ped, dimension)
    setElementSyncer(ped, owner)

    outputChatBox(("[native navigate harness] Run %d: owner=%s observer=%s"):format(session.id, getPlayerName(owner), getPlayerName(observer)), root,
                  120, 210, 255)

    setTimer(function(runId)
        if not session or session.id ~= runId or not isElement(session.ped) then
            return
        end
        triggerClientEvent(session.owner, "nativePedNavigateHarness:start", resourceRoot, runId, session.ped, unpack(TARGET))
        triggerClientEvent(session.observer, "nativePedNavigateHarness:observe", resourceRoot, runId, session.ped, unpack(START))
    end, 750, 1, session.id)

    session.sampleTimer = setTimer(function()
        if not session or not isElement(session.ped) then
            return
        end
        local x, y, z = getElementPosition(session.ped)
        session.maxDisplacement = math.max(session.maxDisplacement, getDistanceBetweenPoints3D(x, y, z, unpack(START)))
        maybePass()
    end, 100, 0)

    session.timeoutTimer = setTimer(function()
        if session then
            finish(false, ("timeout; validation=%s accepted=%s arrived=%s observer=%s displacement=%.2f"):format(
                tostring(session.validationPassed), tostring(session.accepted), tostring(session.ownerArrived), tostring(session.observerPassive),
                session.maxDisplacement))
        end
    end, 45000, 1)
end)

addEvent("nativePedNavigateHarness:evidence", true)
addEventHandler("nativePedNavigateHarness:evidence", resourceRoot, function(runId, phase, ok, details)
    if source ~= resourceRoot or not session or session.id ~= runId or session.finished then
        return
    end

    local sender = client
    if phase == "validation" and sender == session.owner then
        session.validationPassed = ok == true
    elseif phase == "accepted" and sender == session.owner then
        session.accepted = ok == true
    elseif phase == "arrived" and sender == session.owner then
        session.ownerArrived = ok == true
    elseif phase == "observer" and sender == session.observer then
        session.observerPassive = ok == true
    else
        return
    end

    outputDebugString(("[native navigate harness] run=%d phase=%s ok=%s %s"):format(runId, phase, tostring(ok), tostring(details)))
    if ok ~= true then
        return finish(false, ("%s: %s"):format(phase, tostring(details)))
    end
    maybePass()
end)

addCommandHandler("nativepathcleanup", cleanup)

addEventHandler("onPlayerQuit", root, function()
    if session and (source == session.owner or source == session.observer) then
        finish(false, "owner ou observer deconnecte")
    end
end)

addEventHandler("onResourceStop", resourceRoot, cleanup)
