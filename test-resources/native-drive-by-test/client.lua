local DRIVE_BY_TASK = "TASK_SIMPLE_GANG_DRIVEBY"
local activeTest

local function report(evidence, a, b, c)
    local test = activeTest
    if not test then
        return false
    end
    triggerServerEvent("nativeDriveBy:evidence", resourceRoot, test.id, test.phase, evidence, a, b, c)
    return true
end

local function stopPhase(cancelTask)
    local test = activeTest
    if not test then
        return
    end
    if isTimer(test.retryTimer) then
        killTimer(test.retryTimer)
    end
    if isTimer(test.monitorTimer) then
        killTimer(test.monitorTimer)
    end
    if isTimer(test.destroyTimer) then
        killTimer(test.destroyTimer)
    end
    if isTimer(test.finishTimer) then
        killTimer(test.finishTimer)
    end
    if isTimer(test.cancelVerifyTimer) then
        killTimer(test.cancelVerifyTimer)
    end
    if cancelTask and test.accepted and isElement(test.shooter) then
        killPedTask(test.shooter, "primary", 3, false)
    end
    activeTest = nil
end

local function fail(reason)
    report("failure", reason)
    stopPhase(true)
end

local function finishCancellation(expected)
    local test = activeTest
    if not test or test ~= expected then
        return
    end
    if isTimer(test.monitorTimer) then
        killTimer(test.monitorTimer)
    end
    report("cancel_call", isPedDoingTask(test.shooter, DRIVE_BY_TASK))
    local killed = killPedTask(test.shooter, "primary", 3, false)
    report("cancel_return", killed)
    test.cancelVerifyTimer = setTimer(function()
        if activeTest ~= test then
            return
        end
        local stillActive = isElement(test.shooter) and isPedDoingTask(test.shooter, DRIVE_BY_TASK) or false
        report("cancelled", stillActive, killed)
        local id, phase = test.id, test.phase
        stopPhase(false)
        triggerServerEvent("nativeDriveBy:advance", resourceRoot, id, phase)
    end, 300, 1)
end

local function queueCancellation()
    local test = activeTest
    if not test or test.finishing then
        return
    end
    test.finishing = true
    -- Return from the repeating monitor callback before removing the native
    -- task. This also keeps cancellation out of the frame that observed the
    -- weapon event and damage response.
    test.finishTimer = setTimer(function()
        finishCancellation(test)
    end, 500, 1)
    report("cancel_queued", isTimer(test.finishTimer))
end

local function requestPedTargetDestruction()
    local test = activeTest
    if not test or test.destroyRequested then
        return
    end
    test.destroyRequested = true
    triggerServerEvent("nativeDriveBy:destroyTarget", resourceRoot, test.id, test.phase)
end

local function beginPhase()
    local test = activeTest
    if not test then
        return
    end
    if not isElement(test.shooter) or not isElement(test.vehicle) or not isElement(test.target) then
        return fail("elements absents avant dispatch")
    end
    if not isElementStreamedIn(test.shooter) or not isElementStreamedIn(test.vehicle) or not isElementStreamedIn(test.target) or
        not isElementSyncer(test.shooter) or not isElementSyncer(test.vehicle) then
        if getTickCount() - test.requestedAt < 8000 then
            test.retryTimer = setTimer(beginPhase, 250, 1)
            return
        end
        return fail("streaming ou ownership absent apres 8 s")
    end
    if getPedOccupiedVehicle(test.shooter) ~= test.vehicle or getPedOccupiedVehicleSeat(test.shooter) ~= 1 then
        return fail("Ballas2 n'est pas au siege passager 1")
    end
    if getPedWeapon(test.shooter) ~= NATIVE_DRIVE_BY.weapon then
        if getTickCount() - test.requestedAt < 8000 then
            test.retryTimer = setTimer(beginPhase, 250, 1)
            return
        end
        return fail(("TEC9 absent, weapon=%d"):format(getPedWeapon(test.shooter)))
    end
    if type(setPedDriveBy) ~= "function" then
        return fail("API setPedDriveBy absente du client Neon")
    end
    if type(setPedMissionActor) ~= "function" or not setPedMissionActor(test.shooter, true) then
        return fail("PED_MISSION refuse pour le tireur")
    end
    if type(setPedWeaponAccuracy) ~= "function" or not setPedWeaponAccuracy(test.shooter, NATIVE_DRIVE_BY.accuracy) then
        return fail("accuracy native refusee")
    end
    if type(setPedWeaponShootingRate) ~= "function" or not setPedWeaponShootingRate(test.shooter, NATIVE_DRIVE_BY.shootingRate) then
        return fail("shooting rate natif refuse")
    end

    local target = test.coordinate and Vector3(test.coordinate[1], test.coordinate[2], test.coordinate[3]) or test.target
    test.initialAmmo = getPedTotalAmmo(test.shooter)
    test.initialHealth = getElementHealth(test.target)
    test.initialShooterVehicleHealth = getElementHealth(test.vehicle)
    test.startedAt = getTickCount()
    test.accepted = setPedDriveBy(test.shooter, target, NATIVE_DRIVE_BY.abortRange, NATIVE_DRIVE_BY.style, NATIVE_DRIVE_BY.seatRHS,
                                  NATIVE_DRIVE_BY.frequency)
    report("acceptance", test.accepted)
    if not test.accepted then
        return stopPhase(false)
    end

    test.monitorTimer = setTimer(function()
        local current = activeTest
        if not current or not isElement(current.shooter) or not isElement(current.vehicle) then
            return fail("tireur ou Voodoo detruit pendant la task")
        end

        local elapsed = getTickCount() - current.startedAt
        local taskActive = isPedDoingTask(current.shooter, DRIVE_BY_TASK)
        if taskActive and not current.taskObserved then
            current.taskObserved = true
            report("task", elapsed)
        end

        local ammo = getPedTotalAmmo(current.shooter)
        local shots = current.initialAmmo - ammo
        if ammo < current.initialAmmo and not current.fireObserved then
            current.fireObserved = true
            report("fire", current.initialAmmo, ammo)
        end
        if shots >= NATIVE_DRIVE_BY.minimumShots and not current.sustainedFireObserved then
            current.sustainedFireObserved = true
            report("sustained_fire", shots, elapsed)
        end

        if isElement(current.target) then
            local health = getElementHealth(current.target)
            if health < current.initialHealth and not current.damageObserved then
                current.damageObserved = true
                report("damage", current.initialHealth, health)
            end
        end

        local shooterVehicleHealth = getElementHealth(current.vehicle)
        if shooterVehicleHealth < current.initialShooterVehicleHealth then
            report("source_vehicle_damage", current.initialShooterVehicleHealth, shooterVehicleHealth)
            return fail(("vehicule tireur endommage %.1f -> %.1f"):format(
                current.initialShooterVehicleHealth, shooterVehicleHealth))
        end

        if current.taskObserved and current.fireObserved and current.sustainedFireObserved and current.damageObserved then
            if not current.sourceVehicleIntactObserved then
                current.sourceVehicleIntactObserved = true
                report("source_vehicle_intact", current.initialShooterVehicleHealth, shooterVehicleHealth)
            end
            if current.phase == "ped" then
                requestPedTargetDestruction()
            else
                queueCancellation()
            end
            return
        end

        if elapsed > NATIVE_DRIVE_BY.phaseTimeout then
            return fail(("timeout task=%s fire=%s sustained=%s damage=%s shots=%d"):format(
                tostring(current.taskObserved), tostring(current.fireObserved), tostring(current.sustainedFireObserved),
                tostring(current.damageObserved), shots))
        end
    end, 100, 0)
end

addEvent("nativeDriveBy:phase", true)
addEventHandler("nativeDriveBy:phase", resourceRoot, function(sessionId, phase, shooter, vehicle, target, coordinate)
    stopPhase(true)
    activeTest = {
        id = sessionId,
        phase = phase,
        shooter = shooter,
        vehicle = vehicle,
        target = target,
        coordinate = coordinate,
        requestedAt = getTickCount(),
        accepted = false,
    }
    beginPhase()
end)

addEvent("nativeDriveBy:targetDestroyed", true)
addEventHandler("nativeDriveBy:targetDestroyed", resourceRoot, function(sessionId, phase)
    local test = activeTest
    if not test or test.id ~= sessionId or test.phase ~= phase or phase ~= "ped" then
        return
    end
    test.target = nil
    test.destroyTimer = setTimer(function()
        if activeTest ~= test or not isElement(test.shooter) then
            return
        end
        local taskActive = isPedDoingTask(test.shooter, DRIVE_BY_TASK)
        report("target_destroyed", taskActive)
        local killed = taskActive and killPedTask(test.shooter, "primary", 3, false) or true
        test.cancelVerifyTimer = setTimer(function()
            if activeTest ~= test then
                return
            end
            local stillActive = isElement(test.shooter) and isPedDoingTask(test.shooter, DRIVE_BY_TASK) or false
            report("cancelled", stillActive, killed)
            local id = test.id
            stopPhase(false)
            triggerServerEvent("nativeDriveBy:advance", resourceRoot, id, "ped")
        end, 300, 1)
    end, 500, 1)
end)

addEvent("nativeDriveBy:stop", true)
addEventHandler("nativeDriveBy:stop", resourceRoot, function(sessionId)
    if activeTest and activeTest.id == sessionId then
        stopPhase(false)
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    stopPhase(true)
end)
