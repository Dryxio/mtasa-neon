local cohorts = {}
local cohortsByElement = {}
local nextCohortId = 0

local function validPlayer(player)
    return isElement(player) and getElementType(player) == "player"
end

local function copyProofs(value)
    if type(value) ~= "table" then
        return nil
    end
    return {
        bullet = value.bullet == true,
        fire = value.fire == true,
        explosion = value.explosion == true,
        collision = value.collision == true,
        melee = value.melee == true,
    }
end

local function copyOptionalBoolean(value)
    if value == nil then return nil end
    return value == true
end

local function copyRoute(route)
    if type(route) ~= "table" or #route == 0 then
        return false, "route vide"
    end
    local result = {}
    for index, point in ipairs(route) do
        if type(point) ~= "table" or type(point.x) ~= "number" or type(point.y) ~= "number" or
            type(point.z) ~= "number" or type(point.speed) ~= "number" then
            return false, "point de route " .. tostring(index) .. " invalide"
        end
        result[index] = {
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

local function copySequence(sequence)
    if type(sequence) ~= "table" or #sequence == 0 or #sequence > 8 then
        return false, "sequence native vide ou superieure a huit taches"
    end
    local result = {}
    for index, step in ipairs(sequence) do
        if type(step) ~= "table" or type(step.task) ~= "string" then
            return false, "etape de sequence " .. tostring(index) .. " invalide"
        end
        local task = step.task
        if task == "go_to" then
            if type(step.x) ~= "number" or type(step.y) ~= "number" or type(step.z) ~= "number" then
                return false, "destination go_to " .. tostring(index) .. " invalide"
            end
            local movement = step.movement or "walk"
            if movement ~= "walk" and movement ~= "run" and movement ~= "sprint" then
                return false, "mouvement go_to " .. tostring(index) .. " invalide"
            end
            result[index] = {task = task, x = step.x, y = step.y, z = step.z, movement = movement,
                             radius = tonumber(step.radius) or 0.5,
                             slowdownRadius = tonumber(step.slowdownRadius) or 2,
                             timeout = math.floor(tonumber(step.timeout) or -2)}
        elseif task == "leave_car" or task == "leave_car_immediately" then
            if not isElement(step.vehicle) or getElementType(step.vehicle) ~= "vehicle" then
                return false, "vehicule de sortie " .. tostring(index) .. " invalide"
            end
            result[index] = {task = task, vehicle = step.vehicle}
        elseif task == "shoot_at" then
            if type(step.x) ~= "number" or type(step.y) ~= "number" or type(step.z) ~= "number" then
                return false, "cible shoot_at " .. tostring(index) .. " invalide"
            end
            result[index] = {task = task, x = step.x, y = step.y, z = step.z,
                             duration = math.floor(tonumber(step.duration) or 1000),
                             burstLength = math.floor(tonumber(step.burstLength) or 5)}
        elseif task == "smart_flee" then
            local targetType = isElement(step.target) and getElementType(step.target)
            if targetType ~= "ped" and targetType ~= "player" then
                return false, "cible smart_flee " .. tostring(index) .. " invalide"
            end
            result[index] = {task = task, target = step.target,
                             safeDistance = tonumber(step.safeDistance) or 100,
                             duration = math.floor(tonumber(step.duration) or 1000000)}
        elseif task == "die" then
            result[index] = {task = task}
        else
            return false, "type de sequence inconnu: " .. tostring(task)
        end
    end
    return result
end

local function copyTask(task)
    if type(task) ~= "table" or task.type == "none" then
        return {type = "none"}
    end
    if task.type == "drive_route" then
        local route, reason = copyRoute(task.route)
        if not route then
            return false, reason
        end
        return {type = "drive_route", route = route, loop = task.loop == true}
    end
    if task.type == "sequence" then
        local sequence, reason = copySequence(task.sequence)
        if not sequence then
            return false, reason
        end
        return {type = "sequence", sequence = sequence, loop = task.loop == true}
    end
    if task.type == "enter_vehicle" then
        return {type = "enter_vehicle"}
    end
    if task.type == "drive_mission" then
        if not isElement(task.targetVehicle) or getElementType(task.targetVehicle) ~= "vehicle" then
            return false, "cible de mission de conduite invalide"
        end
        if task.mission ~= "escort_left" and task.mission ~= 29 then
            return false, "mission de conduite non admise"
        end
        return {
            type = "drive_mission",
            targetVehicle = task.targetVehicle,
            mission = "escort_left",
            speed = tonumber(task.speed) or 30,
            drivingStyle = task.drivingStyle or "avoid_cars",
        }
    end
    if task.type == "drive_by" then
        local targetType = isElement(task.target) and getElementType(task.target)
        if targetType ~= "ped" and targetType ~= "player" then
            return false, "cible de drive-by invalide"
        end
        return {
            type = "drive_by",
            target = task.target,
            radius = tonumber(task.radius) or 5000,
            style = task.style or "ai_all_directions",
            rightHandSide = task.rightHandSide ~= false,
            frequency = math.max(1, math.floor(tonumber(task.frequency) or 40)),
            reissue = task.reissue ~= false,
        }
    end
    if task.type == "kill_on_foot" then
        local targetType = isElement(task.target) and getElementType(task.target)
        if targetType ~= "ped" and targetType ~= "player" then
            return false, "cible de combat a pied invalide"
        end
        return {type = "kill_on_foot", target = task.target, reissue = task.reissue ~= false}
    end
    return false, "type de tache inconnu: " .. tostring(task.type)
end

local function copyDescriptor(descriptor)
    if type(descriptor) ~= "table" or type(descriptor.members) ~= "table" or #descriptor.members == 0 then
        return false, "cohorte sans membre"
    end

    local result = {members = {}, vehicles = {}, dependencies = {}}
    local owned, dependencies = {}, {}
    for index, member in ipairs(descriptor.members) do
        if type(member) ~= "table" or not isElement(member.ped) or getElementType(member.ped) ~= "ped" then
            return false, "ped de cohorte " .. tostring(index) .. " invalide"
        end
        if owned[member.ped] then
            return false, "ped duplique dans la cohorte"
        end
        local vehicle = member.vehicle
        if vehicle ~= nil and (not isElement(vehicle) or getElementType(vehicle) ~= "vehicle") then
            return false, "vehicule du membre " .. tostring(index) .. " invalide"
        end
        local task, reason = copyTask(member.task)
        if not task then
            return false, reason
        end
        local seat = vehicle and math.floor(tonumber(member.seat) or 0) or nil
        if task.type == "drive_mission" or task.type == "drive_route" then
            if not vehicle or seat ~= 0 then
                return false, "une tache de conduite exige le siege conducteur"
            end
        elseif task.type == "enter_vehicle" and not vehicle then
            return false, "une tache d'entree exige un vehicule et un siege"
        elseif task.type == "sequence" then
            for sequenceIndex, step in ipairs(task.sequence) do
                if (step.task == "leave_car" or step.task == "leave_car_immediately") and step.vehicle ~= vehicle then
                    return false, "la sortie de sequence " .. tostring(sequenceIndex) .. " doit viser le vehicule du membre"
                end
            end
        end
        owned[member.ped] = true
        if vehicle then
            owned[vehicle] = true
        end
        result.members[#result.members + 1] = {
            ped = member.ped,
            vehicle = vehicle,
            seat = seat,
            missionActor = member.missionActor ~= false,
            proofs = copyProofs(member.proofs),
            weaponAccuracy = member.weaponAccuracy and math.max(0, math.min(100, math.floor(tonumber(member.weaponAccuracy) or 0))) or nil,
            weaponShootingRate = member.weaponShootingRate and
                math.max(0, math.min(65535, math.floor(tonumber(member.weaponShootingRate) or 0))) or nil,
            canBeKnockedOffBike = copyOptionalBoolean(member.canBeKnockedOffBike),
            suffersCriticalHits = copyOptionalBoolean(member.suffersCriticalHits),
            canBeDraggedOut = copyOptionalBoolean(member.canBeDraggedOut),
            onlyDamagedByPlayer = copyOptionalBoolean(member.onlyDamagedByPlayer),
            neverTargeted = copyOptionalBoolean(member.neverTargeted),
            task = task,
        }
    end

    for _, vehiclePolicy in ipairs(type(descriptor.vehicles) == "table" and descriptor.vehicles or {}) do
        local vehicle = type(vehiclePolicy) == "table" and vehiclePolicy.vehicle
        if not isElement(vehicle) or getElementType(vehicle) ~= "vehicle" or not owned[vehicle] then
            return false, "politique appliquee a un vehicule hors cohorte"
        end
        result.vehicles[#result.vehicles + 1] = {
            vehicle = vehicle,
            straightLineDistance = vehiclePolicy.straightLineDistance and
                math.max(0, math.min(255, math.floor(vehiclePolicy.straightLineDistance))) or nil,
        }
    end

    local function addDependency(element)
        if isElement(element) and not owned[element] and not dependencies[element] then
            dependencies[element] = true
            result.dependencies[#result.dependencies + 1] = element
        end
    end
    for _, member in ipairs(result.members) do
        if member.task.type == "drive_mission" then
            addDependency(member.task.targetVehicle)
        elseif member.task.type == "drive_by" then
            addDependency(member.task.target)
        elseif member.task.type == "kill_on_foot" then
            addDependency(member.task.target)
        elseif member.task.type == "sequence" then
            for _, step in ipairs(member.task.sequence) do
                if step.task == "smart_flee" then addDependency(step.target) end
            end
        end
    end
    for _, element in ipairs(type(descriptor.dependencies) == "table" and descriptor.dependencies or {}) do
        if not isElement(element) then
            return false, "dependance invalide"
        end
        addDependency(element)
    end

    result.owned = {}
    for element in pairs(owned) do
        result.owned[#result.owned + 1] = element
    end
    return result
end

local function copyOptions(options)
    options = type(options) == "table" and options or {}
    local result = {fallbackOwners = {}}
    for _, player in ipairs(type(options.fallbackOwners) == "table" and options.fallbackOwners or {}) do
        if validPlayer(player) then
            result.fallbackOwners[#result.fallbackOwners + 1] = player
        end
    end
    return result
end

local function snapshot(cohort, extra)
    local data = {
        id = cohort.id,
        state = cohort.state,
        epoch = cohort.epoch,
        owner = cohort.owner,
        pendingOwner = cohort.pendingOwner,
        reason = cohort.reason,
        memberCount = #cohort.descriptor.members,
        ownedElementCount = #cohort.descriptor.owned,
    }
    if type(extra) == "table" then
        for key, value in pairs(extra) do
            data[key] = value
        end
    end
    return data
end

local function emit(cohort, state, extra)
    cohort.state = state
    triggerEvent("onNativeTaskCohortStateChange", cohort.handle, state, snapshot(cohort, extra))
end

local function clearTimer(cohort, name)
    if isTimer(cohort[name]) then
        killTimer(cohort[name])
    end
    cohort[name] = nil
end

local function restoreAutomaticSync(cohort)
    for _, element in ipairs(cohort.descriptor.owned) do
        if isElement(element) then
            setElementSyncer(element, true)
        end
    end
end

local function finalizeCohortRemoval(cohort, destroyHandle)
    clearTimer(cohort, "dispatchTimer")
    clearTimer(cohort, "ackTimer")
    clearTimer(cohort, "handoffTimer")
    if validPlayer(cohort.owner) then
        triggerClientEvent(cohort.owner, "nativeTaskRuntime:cohortStop", resourceRoot, cohort.handle, cohort.epoch,
                           cohort.nonce)
    end
    restoreAutomaticSync(cohort)
    cohorts[cohort.handle] = nil
    for _, element in ipairs(cohort.descriptor.owned) do
        cohortsByElement[element] = nil
    end
    if destroyHandle and isElement(cohort.handle) then
        destroyElement(cohort.handle)
    end
end

local function removeCohort(cohort, destroyHandle)
    if not cohort or cohort.removing then return end
    cohort.removing = true
    finalizeCohortRemoval(cohort, destroyHandle)
end

local function failCohort(cohort, reason)
    if not cohort or cohort.removing then
        return
    end
    cohort.reason = reason
    clearTimer(cohort, "ackTimer")
    if validPlayer(cohort.owner) then
        triggerClientEvent(cohort.owner, "nativeTaskRuntime:cohortStop", resourceRoot, cohort.handle, cohort.epoch,
                           cohort.nonce)
    end
    restoreAutomaticSync(cohort)
    emit(cohort, "failed", {reason = reason})
end

local function allOwnedBy(cohort, owner)
    for _, element in ipairs(cohort.descriptor.owned) do
        if not isElement(element) or getElementSyncer(element) ~= owner then
            return false
        end
    end
    return true
end

local dispatch

local function assignEpoch(cohort, owner)
    if not validPlayer(owner) then
        return false
    end
    for _, element in ipairs(cohort.descriptor.owned) do
        if not isElement(element) then
            failCohort(cohort, "element detruit avant le nouvel epoch")
            return false
        end
    end
    cohort.owner = owner
    cohort.pendingOwner = nil
    cohort.epoch = cohort.epoch + 1
    cohort.nonce = tostring(getTickCount()) .. ":" .. tostring(math.random(100000, 999999))
    cohort.assignmentStartedAt = getTickCount()
    cohort.dispatchAttempts = 0
    cohort.reason = nil
    cohort.streamedOut = {}
    -- setElementSyncer synchronously emits onElementStartSync. Keep dispatch
    -- closed until createNativeTaskCohort has finished assigning every owned
    -- element and has scheduled the first epoch. Otherwise a fast client can
    -- acknowledge the cohort before the caller has even received its handle.
    cohort.assignmentInitializing = true
    local expectedEpoch, expectedNonce, expectedOwner = cohort.epoch, cohort.nonce, cohort.owner
    local function assignmentStillCurrent()
        return cohorts[cohort.handle] == cohort and not cohort.removing and cohort.state == "assigning" and
                   cohort.owner == expectedOwner and cohort.epoch == expectedEpoch and cohort.nonce == expectedNonce
    end
    emit(cohort, "assigning")
    if not assignmentStillCurrent() then
        cohort.assignmentInitializing = false
        return false
    end
    for _, element in ipairs(cohort.descriptor.owned) do
        -- Always promote the current owner to persistent authority. Both the
        -- ped and unoccupied-vehicle sync managers reduce a same-owner call to
        -- SetSyncerPersistent, without StopSync/StartSync or a network packet.
        -- Skipping that call leaves an automatic owner revocable underneath a
        -- newly active cohort after rapid cancel-and-replace lifecycles.
        local accepted = setElementSyncer(element, owner, true)
        if not assignmentStillCurrent() then
            cohort.assignmentInitializing = false
            return false
        end
        if not accepted then
            cohort.assignmentInitializing = false
            failCohort(cohort, "override syncer refuse")
            return false
        end
    end
    clearTimer(cohort, "dispatchTimer")
    if not assignmentStillCurrent() then
        cohort.assignmentInitializing = false
        return false
    end
    cohort.dispatchTimer = setTimer(function()
        dispatch(cohort)
    end, 100, 1)
    cohort.assignmentInitializing = false
    return true
end

dispatch = function(cohort)
    if not cohort or cohort.removing or cohort.assignmentInitializing or
        (cohort.state ~= "assigning" and cohort.state ~= "dispatched") then
        return
    end
    if not validPlayer(cohort.owner) then
        return failCohort(cohort, "owner absent au dispatch")
    end
    local authorityReady = allOwnedBy(cohort, cohort.owner)
    if not authorityReady then
        if getTickCount() - cohort.assignmentStartedAt >= 10000 then
            return failCohort(cohort, "cohorte d'autorite incomplete apres 10 s")
        end
        clearTimer(cohort, "dispatchTimer")
        cohort.dispatchTimer = setTimer(function()
            dispatch(cohort)
        end, 100, 1)
        return
    end

    cohort.dispatchAttempts = cohort.dispatchAttempts + 1
    -- Publish the server state before delivering work. A local owner can
    -- acknowledge in the same pulse; accepting that reply must not depend on
    -- network latency being long enough for this function to resume first.
    if cohort.state ~= "dispatched" then
        local expectedOwner, expectedEpoch, expectedNonce = cohort.owner, cohort.epoch, cohort.nonce
        emit(cohort, "dispatched", {dispatchAttempts = cohort.dispatchAttempts})
        if cohort.removing or cohort.state ~= "dispatched" then return end
        if cohort.owner ~= expectedOwner or cohort.epoch ~= expectedEpoch or cohort.nonce ~= expectedNonce or
            not validPlayer(cohort.owner) or not allOwnedBy(cohort, cohort.owner) then
            return failCohort(cohort, "autorite modifiee pendant la publication du dispatch")
        end
    end
    if cohort.removing or cohort.state ~= "dispatched" then
        return
    end
    clearTimer(cohort, "ackTimer")
    local epoch, nonce = cohort.epoch, cohort.nonce
    cohort.ackTimer = setTimer(function()
        if not cohort.removing and cohort.state == "dispatched" and cohort.epoch == epoch and cohort.nonce == nonce then
            if getTickCount() - cohort.assignmentStartedAt >= 10000 then
                failCohort(cohort, "epoch de cohorte non acquitte apres 10 s")
            else
                dispatch(cohort)
            end
        end
    end, 1000, 1)
    triggerClientEvent(cohort.owner, "nativeTaskRuntime:cohortAssign", resourceRoot, cohort.handle, cohort.epoch,
                       cohort.nonce, cohort.descriptor)
end

local function beginPendingEpoch(cohort)
    clearTimer(cohort, "handoffTimer")
    local owner = cohort.pendingOwner
    if not validPlayer(owner) then
        cohort.owner, cohort.pendingOwner = nil, nil
        return emit(cohort, "orphaned", {reason = "aucun owner disponible"})
    end
    assignEpoch(cohort, owner)
end

local function chooseFallback(cohort, departed)
    for _, player in ipairs(cohort.options.fallbackOwners) do
        if player ~= departed and validPlayer(player) then
            return player
        end
    end
end

function createNativeTaskCohort(owner, descriptor, options)
    if not validPlayer(owner) then
        return false, "owner invalide"
    end
    local immutable, reason = copyDescriptor(descriptor)
    if not immutable then
        return false, reason
    end
    for _, element in ipairs(immutable.owned) do
        if cohortsByElement[element] then
            return false, "element deja gere par une cohorte"
        end
    end

    nextCohortId = nextCohortId + 1
    local handle = createElement("native-task-cohort", "native-task-cohort-" .. tostring(nextCohortId))
    if not handle then
        return false, "creation du handle refusee"
    end
    local cohort = {
        id = nextCohortId,
        handle = handle,
        caller = sourceResourceRoot or resourceRoot,
        descriptor = immutable,
        options = copyOptions(options),
        state = "created",
        epoch = 0,
    }
    cohorts[handle] = cohort
    for _, element in ipairs(immutable.owned) do
        cohortsByElement[element] = cohort
    end
    setElementParent(handle, cohort.caller)
    if not assignEpoch(cohort, owner) then
        removeCohort(cohort, true)
        return false, "premier epoch refuse"
    end
    return handle
end

function handoffNativeTaskCohort(handle, newOwner, requireStreamOut)
    local cohort = cohorts[handle]
    if not cohort or cohort.removing or cohort.caller ~= (sourceResourceRoot or resourceRoot) then
        return false, "handle inconnu ou non possede"
    end
    if not validPlayer(newOwner) then
        return false, "nouvel owner invalide"
    end
    if cohort.state == "revoking" or cohort.state == "awaiting_streamout" then
        return false, "handoff deja en cours"
    end
    if newOwner == cohort.owner then
        return true
    end
    cohort.pendingOwner = newOwner
    cohort.requireStreamOut = requireStreamOut == true
    cohort.streamedOut = {}
    emit(cohort, "revoking")
    triggerClientEvent(cohort.owner, "nativeTaskRuntime:cohortRevoke", resourceRoot, cohort.handle, cohort.epoch,
                       cohort.nonce, cohort.requireStreamOut)
    clearTimer(cohort, "handoffTimer")
    cohort.handoffTimer = setTimer(function()
        if cohort.state == "revoking" then
            failCohort(cohort, "revoke de cohorte non acquitte")
        elseif cohort.state == "awaiting_streamout" then
            failCohort(cohort, "stream-out de cohorte incomplet")
        end
    end, cohort.requireStreamOut and 15000 or 10000, 1)
    return true
end

function cancelNativeTaskCohort(handle)
    local cohort = cohorts[handle]
    if not cohort or cohort.removing or cohort.caller ~= (sourceResourceRoot or resourceRoot) then
        return false
    end
    -- Mark terminal before notifying consumers. A listener may synchronously
    -- call cancel again or destroy the handle; neither path may recurse or
    -- resurrect authority while cancellation is being published.
    cohort.removing = true
    emit(cohort, "cancelled")
    finalizeCohortRemoval(cohort, true)
    return true
end

function getNativeTaskCohortState(handle)
    local cohort = cohorts[handle]
    if not cohort or cohort.caller ~= (sourceResourceRoot or resourceRoot) then
        return false
    end
    return snapshot(cohort)
end

addEvent("onNativeTaskCohortStateChange", false)
addEvent("nativeTaskRuntime:cohortEvidence", true)
addEventHandler("nativeTaskRuntime:cohortEvidence", resourceRoot, function(handle, epoch, nonce, evidence, data)
    local cohort = cohorts[handle]
    if not cohort or client ~= cohort.owner or epoch ~= cohort.epoch or nonce ~= cohort.nonce or
        type(evidence) ~= "string" then
        return
    end
    data = type(data) == "table" and data or {}
    if evidence == "accepted" then
        if cohort.state ~= "dispatched" then
            return
        end
        clearTimer(cohort, "ackTimer")
        if data.accepted ~= true then
            return failCohort(cohort, data.reason or "tache de cohorte refusee")
        end
        return emit(cohort, "active", {accepted = true, dispatchAttempts = cohort.dispatchAttempts})
    end
    if evidence == "sample" then
        if cohort.state ~= "active" or not allOwnedBy(cohort, cohort.owner) then
            return failCohort(cohort, "autorite de cohorte perdue")
        end
        for _, member in ipairs(cohort.descriptor.members) do
            if member.vehicle and
                (getPedOccupiedVehicle(member.ped) ~= member.vehicle or getPedOccupiedVehicleSeat(member.ped) ~= member.seat) then
                return failCohort(cohort, "siege de cohorte perdu")
            end
        end
        return emit(cohort, "active", {sample = true, tasks = data.tasks})
    end
    if evidence == "released" and cohort.state == "revoking" then
        for _, element in ipairs(cohort.descriptor.owned) do
            if isElement(element) then
                setElementSyncer(element, false)
            end
        end
        if cohort.requireStreamOut then
            return emit(cohort, "awaiting_streamout")
        end
        return beginPendingEpoch(cohort)
    end
    if evidence == "streamout" and cohort.state == "awaiting_streamout" and isElement(data.element) then
        cohort.streamedOut[data.element] = true
        local complete = true
        for _, element in ipairs(cohort.descriptor.owned) do
            complete = complete and cohort.streamedOut[element] == true
        end
        if complete then
            beginPendingEpoch(cohort)
        end
        return
    end
    if evidence == "failure" then
        failCohort(cohort, data.reason or "echec client sans detail")
    end
end)

addEventHandler("onElementStartSync", root, function()
    local cohort = cohortsByElement[source]
    if cohort then
        dispatch(cohort)
    end
end)

addEventHandler("onElementDestroy", root, function()
    local cohort = cohorts[source] or cohortsByElement[source]
    if cohort and not cohort.removing then
        if source == cohort.handle then
            removeCohort(cohort, false)
        else
            failCohort(cohort, "element de cohorte detruit")
            removeCohort(cohort, true)
        end
    end
end)

addEventHandler("onPlayerQuit", root, function()
    for _, cohort in pairs(cohorts) do
        if cohort.owner == source or cohort.pendingOwner == source then
            local fallback = chooseFallback(cohort, source)
            for _, element in ipairs(cohort.descriptor.owned) do
                if isElement(element) then
                    setElementSyncer(element, false)
                end
            end
            cohort.owner, cohort.pendingOwner = nil, nil
            clearTimer(cohort, "handoffTimer")
            if fallback then
                assignEpoch(cohort, fallback)
            else
                emit(cohort, "orphaned", {reason = "owner deconnecte sans fallback"})
            end
        end
    end
end)

addEventHandler("onResourceStop", root, function(stoppedResource)
    local stoppedRoot = getResourceRootElement(stoppedResource)
    local runtimeStopping = stoppedResource == getThisResource()
    local owned = {}
    for _, cohort in pairs(cohorts) do
        if runtimeStopping or cohort.caller == stoppedRoot then
            owned[#owned + 1] = cohort
        end
    end
    for _, cohort in ipairs(owned) do
        removeCohort(cohort, true)
    end
end)

outputServerLog("[native task runtime] Ready: transversal authority cohorts available.")
