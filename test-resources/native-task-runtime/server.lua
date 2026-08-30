local tasks = {}
local tasksByElement = {}
local nextTaskId = 0

addEvent("onNativeTaskRuntimeStopping", false)
addEventHandler("onResourceStop", root, function(stoppedResource)
    if stoppedResource == getThisResource() then
        triggerEvent("onNativeTaskRuntimeStopping", resourceRoot)
    end
end)

local function copyRoute(route)
    if type(route) ~= "table" or #route == 0 then
        return false, "route vide"
    end

    local result = {}
    for index, point in ipairs(route) do
        if type(point) ~= "table" or type(point.x) ~= "number" or type(point.y) ~= "number" or type(point.z) ~= "number" or
            type(point.speed) ~= "number" then
            return false, "point " .. tostring(index) .. " invalide"
        end
        result[index] = {
            task = "drive_to",
            x = point.x,
            y = point.y,
            z = point.z,
            speed = point.speed,
            mode = point.mode or "normal",
            vehicleModel = point.vehicleModel,
            drivingStyle = point.drivingStyle or "avoid_cars",
        }
    end
    return result
end

local function copyOptions(options)
    options = type(options) == "table" and options or {}
    local result = {
        loadCollision = options.loadCollision,
        validZMin = tonumber(options.validZMin),
        validZMax = tonumber(options.validZMax),
        fallbackOwners = {},
    }
    if type(options.fallbackOwners) == "table" then
        for _, player in ipairs(options.fallbackOwners) do
            if isElement(player) and getElementType(player) == "player" then
                result.fallbackOwners[#result.fallbackOwners + 1] = player
            end
        end
    end
    return result
end

local function snapshot(task, extra)
    local data = {
        id = task.id,
        state = task.state,
        epoch = task.epoch,
        owner = task.owner,
        pendingOwner = task.pendingOwner,
        routeIndex = task.routeIndex,
        routeLength = #task.route,
        resumeIndex = task.resumeIndex,
        ped = task.ped,
        vehicle = task.vehicle,
        streamedOutPed = task.streamedOutPed == true,
        streamedOutVehicle = task.streamedOutVehicle == true,
        handoffDistance = task.handoffDistance,
        reason = task.reason,
    }
    if type(extra) == "table" then
        for key, value in pairs(extra) do
            data[key] = value
        end
    end
    return data
end

local function emit(task, state, extra)
    task.state = state
    triggerEvent("onNativeDriveRouteStateChange", task.handle, state, snapshot(task, extra))
end

local function clearTimer(task, name)
    if isTimer(task[name]) then
        killTimer(task[name])
    end
    task[name] = nil
end

local function restoreAutomaticSync(task)
    if isElement(task.ped) then
        setElementSyncer(task.ped, true)
    end
    if isElement(task.vehicle) then
        setElementSyncer(task.vehicle, true)
    end
end

local function removeTask(task, destroyHandle)
    if not task or task.removing then
        return
    end
    task.removing = true
    clearTimer(task, "dispatchTimer")
    clearTimer(task, "ackTimer")
    clearTimer(task, "handoffTimer")
    if isElement(task.owner) then
        triggerClientEvent(task.owner, "nativeTaskRuntime:stop", resourceRoot, task.handle, task.epoch)
    end
    restoreAutomaticSync(task)
    tasks[task.handle] = nil
    tasksByElement[task.ped] = nil
    tasksByElement[task.vehicle] = nil
    if destroyHandle and isElement(task.handle) then
        destroyElement(task.handle)
    end
end

local function failTask(task, reason)
    if not task or task.removing then
        return
    end
    task.reason = reason
    clearTimer(task, "ackTimer")
    if isElement(task.owner) then
        triggerClientEvent(task.owner, "nativeTaskRuntime:stop", resourceRoot, task.handle, task.epoch)
    end
    restoreAutomaticSync(task)
    emit(task, "failed", {reason = reason})
end

local function validOwner(player)
    return isElement(player) and getElementType(player) == "player"
end

local tryDispatch
local sendAssignment

local function assignEpoch(task, owner)
    if not task or task.removing or not validOwner(owner) then
        return false
    end
    if not isElement(task.ped) or not isElement(task.vehicle) then
        failTask(task, "elements detruits avant le nouvel epoch")
        return false
    end

    task.owner = owner
    task.pendingOwner = nil
    task.epoch = task.epoch + 1
    task.resumeIndex = task.routeIndex
    task.dispatchedEpoch = nil
    task.dispatchAttempts = 0
    task.assignmentStartedAt = getTickCount()
    task.streamedOutPed = false
    task.streamedOutVehicle = false
    task.reason = nil
    emit(task, "assigning")

    -- Both elements must move as one simulation unit. Persistent overrides keep
    -- the selected machine authoritative even after every nearby player leaves.
    local pedAccepted = setElementSyncer(task.ped, owner, true)
    local vehicleAccepted = setElementSyncer(task.vehicle, owner, true)
    if not pedAccepted or not vehicleAccepted then
        failTask(task, "override syncer refuse")
        return false
    end

    clearTimer(task, "dispatchTimer")
    task.dispatchTimer = setTimer(function()
        tryDispatch(task)
    end, 100, 1)
    return true
end

tryDispatch = function(task)
    if not task or task.removing or task.state ~= "assigning" then
        return
    end
    if not validOwner(task.owner) or not isElement(task.ped) or not isElement(task.vehicle) then
        return failTask(task, "owner ou elements absents au dispatch")
    end
    if getElementSyncer(task.ped) ~= task.owner or getElementSyncer(task.vehicle) ~= task.owner then
        if getTickCount() - task.assignmentStartedAt >= 10000 then
            return failTask(task, "double syncer absent apres 10 s")
        end
        clearTimer(task, "dispatchTimer")
        task.dispatchTimer = setTimer(function()
            tryDispatch(task)
        end, 100, 1)
        return
    end

    sendAssignment(task)
end

sendAssignment = function(task)
    if not task or task.removing or (task.state ~= "assigning" and task.state ~= "dispatched") then
        return
    end
    if not validOwner(task.owner) or not isElement(task.ped) or not isElement(task.vehicle) then
        return failTask(task, "owner ou elements absents a l'envoi")
    end

    task.dispatchAttempts = task.dispatchAttempts + 1
    local sent = triggerClientEvent(task.owner, "nativeTaskRuntime:assign", resourceRoot, task.handle, task.epoch, task.ped,
                                    task.vehicle, task.route, task.resumeIndex, task.options)
    if not sent then
        return failTask(task, "envoi de l'epoch client refuse")
    end
    outputDebugString(("[native task runtime] route=%d epoch=%d assignment sent attempt=%d"):format(
                          task.id, task.epoch, task.dispatchAttempts))
    if task.dispatchedEpoch ~= task.epoch then
        task.dispatchedEpoch = task.epoch
        emit(task, "dispatched", {dispatchAttempts = task.dispatchAttempts})
    end

    -- A freshly created custom handle can reach the client after the first
    -- remote event which references it. Repeating the same immutable epoch is
    -- safe: the client ignores a duplicate it already owns, while a client
    -- which did not resolve the first handle gets another ordered chance.
    clearTimer(task, "ackTimer")
    local expectedEpoch = task.epoch
    task.ackTimer = setTimer(function()
        if not task.removing and task.state == "dispatched" and task.epoch == expectedEpoch then
            if getTickCount() - task.assignmentStartedAt >= 10000 then
                failTask(task, "epoch client non acquitte apres 10 s")
            else
                sendAssignment(task)
            end
        end
    end, 1000, 1)
end

local function beginPendingEpoch(task)
    if not task or task.removing then
        return
    end
    clearTimer(task, "handoffTimer")
    local owner = task.pendingOwner
    if not validOwner(owner) then
        task.owner = nil
        task.pendingOwner = nil
        return emit(task, "orphaned", {reason = "aucun owner disponible"})
    end
    assignEpoch(task, owner)
end

local function oldOwnerReleased(task)
    if not task or task.removing or task.state ~= "revoking" then
        return
    end

    -- Disabling first forces the old native entities to disappear locally once
    -- its resource-owned leases are gone. The next assignment reenables sync.
    if isElement(task.ped) then
        setElementSyncer(task.ped, false)
    end
    if isElement(task.vehicle) then
        setElementSyncer(task.vehicle, false)
    end

    if task.requireStreamOut then
        emit(task, "awaiting_streamout")
        if task.streamedOutPed and task.streamedOutVehicle then
            beginPendingEpoch(task)
        end
        return
    end
    task.handoffTimer = setTimer(function()
        beginPendingEpoch(task)
    end, 100, 1)
end

local function chooseFallback(task, departed)
    for _, player in ipairs(task.options.fallbackOwners) do
        if player ~= departed and validOwner(player) then
            return player
        end
    end
    return nil
end

function createNativeDriveRoute(ped, vehicle, route, owner, options)
    if not isElement(ped) or getElementType(ped) ~= "ped" or not isElement(vehicle) or getElementType(vehicle) ~= "vehicle" or
        not validOwner(owner) then
        return false, "ped, vehicule ou owner invalide"
    end
    if tasksByElement[ped] or tasksByElement[vehicle] then
        return false, "ped ou vehicule deja gere"
    end

    local immutableRoute, routeError = copyRoute(route)
    if not immutableRoute then
        return false, routeError
    end

    nextTaskId = nextTaskId + 1
    local handle = createElement("native-drive-route", "native-drive-route-" .. tostring(nextTaskId))
    if not handle then
        return false, "creation du handle refusee"
    end

    local task = {
        id = nextTaskId,
        handle = handle,
        caller = sourceResourceRoot or resourceRoot,
        ped = ped,
        vehicle = vehicle,
        route = immutableRoute,
        options = copyOptions(options),
        routeIndex = 0,
        resumeIndex = 0,
        epoch = 0,
        state = "created",
    }
    tasks[handle] = task
    tasksByElement[ped] = task
    tasksByElement[vehicle] = task
    setElementParent(handle, task.caller)

    if not assignEpoch(task, owner) then
        removeTask(task, true)
        return false, "premier epoch refuse"
    end
    return handle
end

function handoffNativeDriveRoute(handle, newOwner, requireStreamOut)
    local task = tasks[handle]
    if not task or task.removing or task.caller ~= (sourceResourceRoot or resourceRoot) then
        return false, "handle inconnu ou non possede"
    end
    if not validOwner(newOwner) then
        return false, "nouvel owner invalide"
    end
    if task.state == "revoking" or task.state == "awaiting_streamout" then
        return false, "handoff deja en cours"
    end

    task.pendingOwner = newOwner
    task.requireStreamOut = requireStreamOut == true
    task.resumeIndex = task.routeIndex
    task.streamedOutPed = false
    task.streamedOutVehicle = false
    task.handoffX, task.handoffY, task.handoffZ = getElementPosition(task.vehicle)

    if not validOwner(task.owner) then
        return assignEpoch(task, newOwner)
    end

    emit(task, "revoking")
    triggerClientEvent(task.owner, "nativeTaskRuntime:revoke", resourceRoot, task.handle, task.epoch, task.requireStreamOut)
    clearTimer(task, "handoffTimer")
    task.handoffTimer = setTimer(function()
        if task.state == "revoking" then
            failTask(task, "revoke non acquitte apres 10 s")
        elseif task.state == "awaiting_streamout" then
            failTask(task, "double stream-out non observe apres 15 s")
        end
    end, task.requireStreamOut and 15000 or 10000, 1)
    return true
end

function cancelNativeDriveRoute(handle)
    local task = tasks[handle]
    if not task or task.caller ~= (sourceResourceRoot or resourceRoot) then
        return false
    end
    emit(task, "cancelled")
    removeTask(task, true)
    return true
end

function getNativeDriveRouteState(handle)
    local task = tasks[handle]
    if not task or task.caller ~= (sourceResourceRoot or resourceRoot) then
        return false
    end
    return snapshot(task)
end

addEvent("onNativeDriveRouteStateChange", false)

addEvent("nativeTaskRuntime:evidence", true)
addEventHandler("nativeTaskRuntime:evidence", resourceRoot, function(handle, epoch, evidence, data)
    local task = tasks[handle]
    if not task or client ~= task.owner or epoch ~= task.epoch or type(evidence) ~= "string" then
        return
    end
    data = type(data) == "table" and data or {}

    if evidence == "accepted" then
        if task.state ~= "dispatched" then
            return
        end
        clearTimer(task, "ackTimer")
        if data.accepted ~= true then
            return failTask(task, data.reason or "sequence native refusee")
        end
        outputDebugString(("[native task runtime] route=%d epoch=%d accepted after %d assignment(s)"):format(
                              task.id, task.epoch, task.dispatchAttempts))
        return emit(task, "active", {accepted = true, clientRouteIndex = data.routeIndex})
    end

    if evidence == "sample" then
        if task.state ~= "active" then
            return
        end
        if getElementSyncer(task.ped) ~= task.owner or getElementSyncer(task.vehicle) ~= task.owner or
            getPedOccupiedVehicle(task.ped) ~= task.vehicle or getPedOccupiedVehicleSeat(task.ped) ~= 0 then
            return failTask(task, "double ownership ou siege conducteur perdu")
        end
        local logicalIndex = tonumber(data.routeIndex)
        local x, y, z = getElementPosition(task.vehicle)
        if logicalIndex and logicalIndex > task.routeIndex and logicalIndex < #task.route then
            local completedPoint = task.route[logicalIndex]
            local acceptanceRadius = math.max(50, completedPoint.speed * 4)
            if getDistanceBetweenPoints3D(x, y, z, completedPoint.x, completedPoint.y, completedPoint.z) <= acceptanceRadius then
                task.routeIndex = logicalIndex
            end
        end
        if task.options.validZMin and z < task.options.validZMin or task.options.validZMax and z > task.options.validZMax then
            return failTask(task, "Z serveur hors plage: " .. string.format("%.2f", z))
        end
        if task.handoffX then
            task.handoffDistance = getDistanceBetweenPoints3D(task.handoffX, task.handoffY, task.handoffZ, x, y, z)
            task.handoffX, task.handoffY, task.handoffZ = nil, nil, nil
        end
        return emit(task, "active", {sample = true, x = x, y = y, z = z, active = data.active == true})
    end

    if evidence == "completed" then
        if task.state ~= "active" or task.routeIndex < #task.route - 1 then
            return
        end
        local last = task.route[#task.route]
        local x, y, z = getElementPosition(task.vehicle)
        if getDistanceBetweenPoints3D(x, y, z, last.x, last.y, last.z) > 12 then
            return failTask(task, "completion loin de la cible finale")
        end
        triggerClientEvent(task.owner, "nativeTaskRuntime:stop", resourceRoot, task.handle, task.epoch)
        restoreAutomaticSync(task)
        return emit(task, "completed")
    end

    if evidence == "released" then
        if task.state == "revoking" then
            oldOwnerReleased(task)
        end
        return
    end

    if evidence == "streamout" and (task.state == "revoking" or task.state == "awaiting_streamout") then
        if data.element == "ped" then
            task.streamedOutPed = true
        elseif data.element == "vehicle" then
            task.streamedOutVehicle = true
        end
        emit(task, task.state, {streamOutElement = data.element})
        if task.state == "awaiting_streamout" and task.streamedOutPed and task.streamedOutVehicle then
            beginPendingEpoch(task)
        end
        return
    end

    if evidence == "failure" then
        failTask(task, data.reason or "echec client sans detail")
    end
end)

addEventHandler("onElementStartSync", root, function()
    local task = tasksByElement[source]
    if task then
        tryDispatch(task)
    end
end)

addEventHandler("onElementDestroy", root, function()
    local task = tasks[source] or tasksByElement[source]
    if task and not task.removing then
        if source == task.handle then
            removeTask(task, false)
        else
            failTask(task, "ped ou vehicule detruit")
            removeTask(task, true)
        end
    end
end)

addEventHandler("onPlayerQuit", root, function()
    for _, task in pairs(tasks) do
        if task.owner == source or task.pendingOwner == source then
            local fallback = chooseFallback(task, source)
            task.routeIndex = task.resumeIndex or task.routeIndex
            if isElement(task.ped) then setElementSyncer(task.ped, false) end
            if isElement(task.vehicle) then setElementSyncer(task.vehicle, false) end
            task.owner = nil
            task.pendingOwner = nil
            clearTimer(task, "handoffTimer")
            if fallback then
                assignEpoch(task, fallback)
            else
                emit(task, "orphaned", {reason = "owner deconnecte sans fallback"})
            end
        end
    end
end)

addEventHandler("onResourceStop", root, function(stoppedResource)
    local stoppedRoot = getResourceRootElement(stoppedResource)
    local owned = {}
    for _, task in pairs(tasks) do
        if task.caller == stoppedRoot then
            owned[#owned + 1] = task
        end
    end
    for _, task in ipairs(owned) do
        removeTask(task, true)
    end
end)

outputServerLog("[native task runtime] Ready: stable drive-route handles and owner epochs available.")
