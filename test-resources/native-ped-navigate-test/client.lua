local TASK_NAME = "TASK_COMPLEX_FOLLOW_NODE_ROUTE"
local active
local DRIVE_TASK_NAME = "TASK_COMPLEX_CAR_DRIVE_TO_POINT"
local activeDrive

local function finish(result, details)
    if not active then
        return
    end

    if isElement(active.ped) then
        triggerServerEvent("nativePedNavigateVisual:result", resourceRoot, active.ped, result, details)
    end
    if isTimer(active.timer) then
        killTimer(active.timer)
    end
    active = nil
end

local function finishDrive(result, details)
    local drive = activeDrive
    if not drive then
        return
    end

    if isElement(drive.ped) and isElement(drive.vehicle) then
        triggerServerEvent("nativePedDriveVisual:result", resourceRoot, drive.ped, drive.vehicle, result, details)
    end
    if isTimer(drive.timer) then
        killTimer(drive.timer)
    end
    if isTimer(drive.retryTimer) then
        killTimer(drive.retryTimer)
    end
    if drive.missionActorApplied and isElement(drive.ped) then
        setPedMissionActor(drive.ped, drive.wasMissionActor)
    end
    activeDrive = nil
end

local function beginDrive(ped, vehicle, x, y, z, speed, attempt)
    attempt = attempt or 1
    if not activeDrive or activeDrive.ped ~= ped or activeDrive.vehicle ~= vehicle then
        return
    end
    if not isElement(ped) or not isElement(vehicle) then
        return finishDrive("destroyed", "ped ou vehicule detruit avant le dispatch")
    end

    local ready = isElementStreamedIn(ped) and isElementStreamedIn(vehicle) and isElementSyncer(ped) and isElementSyncer(vehicle) and
                      getPedOccupiedVehicle(ped) == vehicle and getPedOccupiedVehicleSeat(ped) == 0
    if not ready then
        if attempt < 32 then
            activeDrive.retryTimer = setTimer(beginDrive, 250, 1, ped, vehicle, x, y, z, speed, attempt + 1)
            return
        end
        return finishDrive("refused", "stream, double ownership ou siege conducteur absent apres 8 s")
    end

    activeDrive.wasMissionActor = isPedMissionActor(ped)
    if not setPedMissionActor(ped, true) then
        return finishDrive("refused", "setPedMissionActor a retourne false")
    end
    activeDrive.missionActorApplied = true

    if not setPedDriveTo(ped, vehicle, Vector3(x, y, z), speed, "normal", "stop_for_cars") then
        return finishDrive("refused", "setPedDriveTo a retourne false")
    end

    activeDrive.acceptedAt = getTickCount()
    activeDrive.seenTask = false
    triggerServerEvent("nativePedDriveVisual:result", resourceRoot, ped, vehicle, "accepted", "task native soumise sur le syncer")
    activeDrive.timer = setTimer(function()
        local drive = activeDrive
        if not drive or not isElement(drive.ped) or not isElement(drive.vehicle) then
            return finishDrive("destroyed", "ped ou vehicule detruit pendant la conduite")
        end

        local running = isPedDoingTask(drive.ped, DRIVE_TASK_NAME)
        drive.seenTask = drive.seenTask or running
        local vx, vy, vz = getElementPosition(drive.vehicle)
        local distance = getDistanceBetweenPoints3D(vx, vy, vz, drive.x, drive.y, drive.z)
        local elapsed = getTickCount() - drive.acceptedAt

        if drive.seenTask and not running then
            return finishDrive(distance <= 15.0 and "arrived" or "ended_outside_radius", ("distance=%.2f m, elapsed=%d ms"):format(distance, elapsed))
        end
        if not drive.seenTask and elapsed > 3000 then
            return finishDrive("refused", "TASK_COMPLEX_CAR_DRIVE_TO_POINT jamais observee")
        end
        if elapsed > 120000 then
            return finishDrive("timeout", ("distance restante=%.2f m"):format(distance))
        end
    end, 100, 0)
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

addEvent("nativePedDriveVisual:start", true)
addEventHandler("nativePedDriveVisual:start", resourceRoot, function(ped, vehicle, x, y, z, speed)
    if activeDrive then
        finishDrive("replaced", "ordre remplace")
    end
    activeDrive = {ped = ped, vehicle = vehicle, x = x, y = y, z = z}
    beginDrive(ped, vehicle, x, y, z, speed)
end)

addEvent("nativePedDriveVisual:stop", true)
addEventHandler("nativePedDriveVisual:stop", resourceRoot, function(ped, vehicle)
    if activeDrive and activeDrive.ped == ped and activeDrive.vehicle == vehicle then
        local killed = killPedTask(ped, "primary", 3, false)
        finishDrive("cancelled", killed and "slot primaire supprime" or "killPedTask a retourne false")
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    if active and isTimer(active.timer) then
        killTimer(active.timer)
    end
    active = nil
    if activeDrive then
        finishDrive("stopped", "resource client arretee")
    end
end)
