local cohorts = {}

local function report(cohort, evidence, data)
    triggerServerEvent("nativeTaskRuntime:cohortEvidence", resourceRoot, cohort.handle, cohort.epoch, cohort.nonce,
                       evidence, data or {})
end

local function clearTimer(cohort, name)
    if isTimer(cohort[name]) then
        killTimer(cohort[name])
    end
    cohort[name] = nil
end

local function releaseLeases(cohort)
    for _, token in ipairs(cohort.leases or {}) do
        releaseElementStreamingLease(token)
    end
    cohort.leases = nil
end

local function restorePolicies(cohort)
    for ped, previous in pairs(cohort.previousPeds or {}) do
        if isElement(ped) then
            if previous.missionActor ~= nil then
                setPedMissionActor(ped, previous.missionActor)
            end
            if previous.proofs then
                setPedPhysicalProofs(ped, previous.proofs[1], previous.proofs[2], previous.proofs[3],
                                     previous.proofs[4], previous.proofs[5])
            end
        end
    end
    for vehicle, distance in pairs(cohort.previousDistances or {}) do
        if isElement(vehicle) then
            setVehicleStraightLineDistance(vehicle, distance)
        end
    end
    cohort.previousPeds, cohort.previousDistances = nil, nil
end

local function stopCohort(cohort, keepForStreamOut)
    clearTimer(cohort, "retryTimer")
    clearTimer(cohort, "monitorTimer")
    for _, member in ipairs(cohort.descriptor.members) do
        if member.task.type ~= "none" and isElement(member.ped) then
            killPedTask(member.ped, "primary", 3, false)
        end
    end
    restorePolicies(cohort)
    releaseLeases(cohort)
    if not keepForStreamOut then
        cohorts[cohort.handle] = nil
    end
end

local function fail(cohort, reason)
    report(cohort, "failure", {reason = reason})
    stopCohort(cohort, false)
end

local function acquireLeases(cohort)
    cohort.leases = {}
    for _, element in ipairs(cohort.descriptor.owned) do
        local token = acquireElementStreamingLease(element)
        if not token then
            return false
        end
        cohort.leases[#cohort.leases + 1] = token
    end
    for _, element in ipairs(cohort.descriptor.dependencies) do
        local token = acquireElementStreamingLease(element)
        if not token then
            return false
        end
        cohort.leases[#cohort.leases + 1] = token
    end
    return true
end

local function ready(cohort)
    for _, element in ipairs(cohort.descriptor.owned) do
        if not isElement(element) or not isElementStreamedIn(element) or not isElementSyncer(element) then
            return false
        end
    end
    for _, element in ipairs(cohort.descriptor.dependencies) do
        if not isElement(element) or not isElementStreamedIn(element) then
            return false
        end
    end
    for _, member in ipairs(cohort.descriptor.members) do
        if member.vehicle and
            (getPedOccupiedVehicle(member.ped) ~= member.vehicle or getPedOccupiedVehicleSeat(member.ped) ~= member.seat) then
            return false
        end
    end
    return true
end

local function applyPolicies(cohort)
    cohort.previousPeds, cohort.previousDistances = {}, {}
    for _, member in ipairs(cohort.descriptor.members) do
        local previous = {}
        if member.missionActor then
            previous.missionActor = isPedMissionActor(member.ped)
            if not setPedMissionActor(member.ped, true) then
                return false, "PED_MISSION refuse"
            end
        end
        if member.proofs then
            previous.proofs = {getPedPhysicalProofs(member.ped)}
            if not setPedPhysicalProofs(member.ped, member.proofs.bullet, member.proofs.fire,
                                        member.proofs.explosion, member.proofs.collision, member.proofs.melee) then
                return false, "protections physiques refusees"
            end
        end
        cohort.previousPeds[member.ped] = previous
    end
    for _, policy in ipairs(cohort.descriptor.vehicles) do
        if policy.straightLineDistance ~= nil then
            cohort.previousDistances[policy.vehicle] = getVehicleStraightLineDistance(policy.vehicle)
            if not setVehicleStraightLineDistance(policy.vehicle, policy.straightLineDistance) then
                return false, "distance de conduite refusee"
            end
        end
    end
    return true
end

local function startTask(member)
    local task = member.task
    if task.type == "none" then
        return true
    end
    if task.type == "drive_mission" then
        return setPedDriveMission(member.ped, member.vehicle, task.targetVehicle, task.mission, task.speed,
                                  task.drivingStyle)
    end
    if task.type == "drive_by" then
        return setPedDriveBy(member.ped, task.target, task.radius, task.style, task.rightHandSide, task.frequency)
    end
    if task.type == "drive_route" then
        local sequence = {}
        for _, point in ipairs(task.route) do
            sequence[#sequence + 1] = {
                task = "drive_to",
                x = point.x,
                y = point.y,
                z = point.z,
                speed = point.speed,
                mode = point.mode,
                vehicleModel = point.vehicleModel or getElementModel(member.vehicle),
                drivingStyle = point.drivingStyle,
            }
        end
        return setPedTaskSequence(member.ped, sequence, task.loop)
    end
    return false
end

local function beginCohort(cohort)
    if cohorts[cohort.handle] ~= cohort then
        return
    end
    if type(acquireElementStreamingLease) ~= "function" or type(setPedMissionActor) ~= "function" or
        type(setPedDriveMission) ~= "function" or type(setPedDriveBy) ~= "function" or
        type(setPedPhysicalProofs) ~= "function" or type(getPedPhysicalProofs) ~= "function" or
        type(setVehicleStraightLineDistance) ~= "function" or type(getVehicleStraightLineDistance) ~= "function" then
        return fail(cohort, "API Neon de cohorte absente")
    end
    if not cohort.leases and not acquireLeases(cohort) then
        return fail(cohort, "acquisition des leases de cohorte refusee")
    end
    if not ready(cohort) then
        if getTickCount() - cohort.assignedAt < 10000 then
            clearTimer(cohort, "retryTimer")
            cohort.retryTimer = setTimer(function()
                beginCohort(cohort)
            end, 200, 1)
            return
        end
        return fail(cohort, "streaming, autorite ou sieges non converges apres 10 s")
    end
    local policyAccepted, policyReason = applyPolicies(cohort)
    if not policyAccepted then
        return fail(cohort, policyReason)
    end
    local accepted = true
    for _, member in ipairs(cohort.descriptor.members) do
        accepted = startTask(member) == true and accepted
    end
    report(cohort, "accepted", {accepted = accepted})
    if not accepted then
        return stopCohort(cohort, false)
    end

    cohort.monitorTimer = setTimer(function()
        if cohorts[cohort.handle] ~= cohort then
            return
        end
        if not ready(cohort) then
            return fail(cohort, "autorite, streaming ou siege perdu pendant la cohorte")
        end
        local tasks = {}
        for index, member in ipairs(cohort.descriptor.members) do
            if member.task.type == "drive_by" and member.task.reissue and not isPedDoingGangDriveby(member.ped) then
                startTask(member)
            end
            tasks[index] = getPedSimplestTask(member.ped) or ""
        end
        report(cohort, "sample", {tasks = tasks})
    end, 500, 0)
end

addEvent("nativeTaskRuntime:cohortAssign", true)
addEventHandler("nativeTaskRuntime:cohortAssign", resourceRoot, function(handle, epoch, nonce, descriptor)
    if not isElement(handle) or type(epoch) ~= "number" or type(nonce) ~= "string" or type(descriptor) ~= "table" then
        return
    end
    local old = cohorts[handle]
    if old and old.epoch == epoch and old.nonce == nonce then
        return
    end
    if old then
        stopCohort(old, false)
    end
    local cohort = {
        handle = handle,
        epoch = epoch,
        nonce = nonce,
        descriptor = descriptor,
        assignedAt = getTickCount(),
    }
    cohorts[handle] = cohort
    beginCohort(cohort)
end)

addEvent("nativeTaskRuntime:cohortRevoke", true)
addEventHandler("nativeTaskRuntime:cohortRevoke", resourceRoot, function(handle, epoch, nonce, requireStreamOut)
    local cohort = cohorts[handle]
    if not cohort or cohort.epoch ~= epoch or cohort.nonce ~= nonce then
        return
    end
    cohort.awaitingStreamOut = requireStreamOut == true
    stopCohort(cohort, cohort.awaitingStreamOut)
    report(cohort, "released", {awaitingStreamOut = cohort.awaitingStreamOut})
end)

addEvent("nativeTaskRuntime:cohortStop", true)
addEventHandler("nativeTaskRuntime:cohortStop", resourceRoot, function(handle, epoch, nonce)
    local cohort = cohorts[handle]
    if cohort and cohort.epoch == epoch and cohort.nonce == nonce then
        stopCohort(cohort, false)
    end
end)

addEventHandler("onClientElementStreamOut", root, function()
    for _, cohort in pairs(cohorts) do
        if cohort.awaitingStreamOut then
            for _, element in ipairs(cohort.descriptor.owned) do
                if source == element then
                    report(cohort, "streamout", {element = element})
                    break
                end
            end
        end
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    local current = {}
    for _, cohort in pairs(cohorts) do
        current[#current + 1] = cohort
    end
    for _, cohort in ipairs(current) do
        stopCohort(cohort, false)
    end
end)
