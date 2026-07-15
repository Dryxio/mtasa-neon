local activeTest

local function stopTimers(test)
    if isTimer(test.retryTimer) then
        killTimer(test.retryTimer)
    end
    if isTimer(test.monitorTimer) then
        killTimer(test.monitorTimer)
    end
end

local function report(result, details, terminal)
    local test = activeTest
    if not test then
        return
    end
    local elapsed = test.startedAt and getTickCount() - test.startedAt or nil
    triggerServerEvent("nativeVehicleRecording:result", resourceRoot, test.id, test.vehicle, result, details, elapsed)
    if terminal then
        stopTimers(test)
        activeTest = nil
    end
end

local function stopNativePlayback(test)
    if isElement(test.vehicle) and type(isVehiclePlaybackActive) == "function" and isVehiclePlaybackActive(test.vehicle) and
        type(stopVehiclePlayback) == "function" then
        return stopVehiclePlayback(test.vehicle)
    end
    return false
end

local function beginPlayback()
    local test = activeTest
    if not test then
        return
    end
    if not isElement(test.ped) or not isElement(test.vehicle) then
        return report("destroyed", "Sweet ou Greenwood absent avant le playback", true)
    end
    if not isElementStreamedIn(test.vehicle) or not isElementSyncer(test.vehicle) or not isElementSyncer(test.ped) then
        if getTickCount() - test.requestedAt < 5000 then
            test.retryTimer = setTimer(beginPlayback, 250, 1)
            return
        end
        return report("ownership_refused", "Sweet/Greenwood non stream ou client pas double-syncer apres 5 s", true)
    end
    if getVehicleController(test.vehicle) or getPedOccupiedVehicle(test.ped) ~= test.vehicle or getPedOccupiedVehicleSeat(test.ped) ~= 1 then
        return report("invalid_state", "le conducteur doit rester vide et Sweet passager", true)
    end
    if type(requestVehicleRecording) ~= "function" or type(isVehicleRecordingLoaded) ~= "function" or type(startVehiclePlayback) ~= "function" or
        type(stopVehiclePlayback) ~= "function" or type(isVehiclePlaybackActive) ~= "function" then
        return report("api_unavailable", "API recorded-car absente du client Neon", true)
    end

    if not test.guardsChecked then
        if requestVehicleRecording(-1) then
            return report("invalid_id_accepted", "un ID inconnu a ete accepte", true)
        end
        if startVehiclePlayback(test.vehicle, test.recordingId) then
            stopVehiclePlayback(test.vehicle)
            return report("unrequested_start_accepted", "playback accepte avant request/load", true)
        end
        test.guardsChecked = true
        report("guards", "ID inconnu et start non demande correctement refuses", false)
    end

    if not test.requestAccepted then
        if not requestVehicleRecording(test.recordingId) then
            return report("request_refused", "requestVehicleRecording a retourne false", true)
        end
        test.requestAccepted = true
        report("requested", ("recording %d demande"):format(test.recordingId), false)
    end

    if not isVehicleRecordingLoaded(test.recordingId) then
        if getTickCount() - test.requestedAt < 10000 then
            test.retryTimer = setTimer(beginPlayback, 100, 1)
            return
        end
        return report("load_timeout", "recording jamais charge apres 10 s", true)
    end
    if not test.loadReported then
        test.loadReported = true
        report("loaded", ("recording %d charge"):format(test.recordingId), false)
    end
    if not startVehiclePlayback(test.vehicle, test.recordingId) then
        return report("start_refused", "startVehiclePlayback a retourne false", true)
    end

    test.startedAt = getTickCount()
    test.observedActive = isVehiclePlaybackActive(test.vehicle)
    report("started", "05EB actif sur le syncer du vehicule", false)
    test.monitorTimer = setTimer(function()
        local current = activeTest
        if not current then
            return
        end
        if not isElement(current.vehicle) then
            return report("destroyed", "Greenwood detruite pendant le playback", true)
        end
        if not isElementStreamedIn(current.vehicle) then
            return report("streamed_out", "Greenwood sortie du streaming pendant le playback", true)
        end
        if not isElementSyncer(current.vehicle) then
            stopNativePlayback(current)
            return report("ownership_lost", "sync vehicule perdu; slot natif arrete", true)
        end

        local active = isVehiclePlaybackActive(current.vehicle)
        current.observedActive = current.observedActive or active
        if current.observedActive and not active then
            return report("completed", "fin naturelle du recording 207 observee", true)
        end
        if getTickCount() - current.startedAt > 12000 then
            stopNativePlayback(current)
            return report("playback_timeout", "recording encore actif apres 12 s", true)
        end
    end, 50, 0)
end

addEvent("nativeVehicleRecording:start", true)
addEventHandler("nativeVehicleRecording:start", resourceRoot, function(sessionId, ped, vehicle, recordingId)
    if activeTest then
        stopNativePlayback(activeTest)
        stopTimers(activeTest)
    end
    activeTest = {
        id = sessionId,
        ped = ped,
        vehicle = vehicle,
        recordingId = recordingId,
        requestedAt = getTickCount(),
    }
    beginPlayback()
end)

addEvent("nativeVehicleRecording:cancel", true)
addEventHandler("nativeVehicleRecording:cancel", resourceRoot, function(sessionId, vehicle)
    local test = activeTest
    if not test or test.id ~= sessionId or test.vehicle ~= vehicle then
        return
    end
    local stopped = stopNativePlayback(test)
    report("cancelled", stopped and "slot natif libere" or "aucun playback natif actif", true)
end)

addEventHandler("onClientElementStreamOut", root, function()
    if activeTest and source == activeTest.vehicle then
        report("streamed_out", "Greenwood sortie du streaming pendant le playback", true)
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    if activeTest then
        stopNativePlayback(activeTest)
        stopTimers(activeTest)
    end
    activeTest = nil
end)
