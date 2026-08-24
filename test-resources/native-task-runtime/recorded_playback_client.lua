local playbacks = {}

local function report(playback, evidence, data)
    triggerServerEvent("nativeTaskRuntime:recordedPlaybackEvidence", resourceRoot, playback.handle, playback.nonce,
                       evidence, data or {})
end

local function stopTimer(playback, name)
    if isTimer(playback[name]) then
        killTimer(playback[name])
    end
    playback[name] = nil
end

local function cleanup(playback, forget)
    stopTimer(playback, "retryTimer")
    stopTimer(playback, "monitorTimer")
    if isElement(playback.vehicle) and type(isVehiclePlaybackActive) == "function" and
        isVehiclePlaybackActive(playback.vehicle) then
        stopVehiclePlayback(playback.vehicle)
    end
    if playback.lease then
        releaseElementStreamingLease(playback.lease)
        playback.lease = nil
    end
    if playback.targetLease then
        releaseElementStreamingLease(playback.targetLease)
        playback.targetLease = nil
    end
    if forget then
        playbacks[playback.handle] = nil
    end
end

local function fail(playback, reason)
    report(playback, "failure", {reason = reason})
    cleanup(playback, true)
end

local function playbackSpeed(playback)
    local options = playback.options
    if not isElement(options.target) then
        return math.max(options.minimumSpeed, math.min(options.maximumSpeed, options.pivotSpeed))
    end
    local x, y, z = getElementPosition(playback.vehicle)
    local tx, ty, tz = getElementPosition(options.target)
    local distance = options.distanceMode == "2d" and getDistanceBetweenPoints2D(x, y, tx, ty) or
                         getDistanceBetweenPoints3D(x, y, z, tx, ty, tz)
    local speed
    if distance <= options.pivotDistance then
        local ratio = distance / options.pivotDistance
        speed = options.closeSpeed + (options.pivotSpeed - options.closeSpeed) * ratio
    else
        speed = options.pivotSpeed - (distance - options.pivotDistance) / options.farSlopeDistance
    end
    return math.max(options.minimumSpeed, math.min(options.maximumSpeed, speed)), distance
end

local function begin(playback)
    if playbacks[playback.handle] ~= playback then
        return
    end
    if not isElement(playback.vehicle) then
        return fail(playback, "vehicule absent avant le playback")
    end
    if type(requestVehicleRecording) ~= "function" or type(isVehicleRecordingLoaded) ~= "function" or
        type(startVehiclePlayback) ~= "function" or type(setVehiclePlaybackSpeed) ~= "function" or
        type(stopVehiclePlayback) ~= "function" or type(isVehiclePlaybackActive) ~= "function" or
        type(acquireElementStreamingLease) ~= "function" then
        return fail(playback, "API Neon de playback enregistre absente")
    end
    if not playback.lease then
        playback.lease = acquireElementStreamingLease(playback.vehicle)
        if not playback.lease then
            return fail(playback, "lease vehicule refuse")
        end
        if isElement(playback.options.target) then
            playback.targetLease = acquireElementStreamingLease(playback.options.target)
            if not playback.targetLease then
                return fail(playback, "lease cible refuse")
            end
        end
    end
    if not isElementStreamedIn(playback.vehicle) or not isElementSyncer(playback.vehicle) or
        (isElement(playback.options.target) and not isElementStreamedIn(playback.options.target)) then
        if getTickCount() - playback.requestedAt < playback.options.loadTimeout then
            playback.retryTimer = setTimer(function() begin(playback) end, 100, 1)
            return
        end
        return fail(playback, "streaming ou autorite non converges")
    end
    if not playback.requested then
        if not requestVehicleRecording(playback.recordingId) then
            return fail(playback, "requestVehicleRecording refuse")
        end
        playback.requested = true
    end
    if not isVehicleRecordingLoaded(playback.recordingId) then
        if getTickCount() - playback.requestedAt < playback.options.loadTimeout then
            playback.retryTimer = setTimer(function() begin(playback) end, 100, 1)
            return
        end
        return fail(playback, "recording non charge avant timeout")
    end
    if not startVehiclePlayback(playback.vehicle, playback.recordingId) then
        return fail(playback, "startVehiclePlayback refuse")
    end
    local speed, distance = playbackSpeed(playback)
    if not setVehiclePlaybackSpeed(playback.vehicle, speed) then
        return fail(playback, "setVehiclePlaybackSpeed refuse")
    end

    playback.startedAt = getTickCount()
    playback.observedActive = isVehiclePlaybackActive(playback.vehicle)
    playback.lastSampleAt = 0
    report(playback, "active", {speed = speed, distance = distance})
    playback.monitorTimer = setTimer(function()
        if playbacks[playback.handle] ~= playback then
            return
        end
        if not isElement(playback.vehicle) or not isElementStreamedIn(playback.vehicle) or
            not isElementSyncer(playback.vehicle) then
            return fail(playback, "vehicule detruit, de-stream ou autorite perdue")
        end
        local active = isVehiclePlaybackActive(playback.vehicle)
        playback.observedActive = playback.observedActive or active
        if playback.observedActive and not active then
            local elapsed = getTickCount() - playback.startedAt
            report(playback, "completed", {elapsed = elapsed})
            return cleanup(playback, true)
        end
        if getTickCount() - playback.startedAt > playback.options.playbackTimeout then
            return fail(playback, "playback encore actif apres timeout")
        end
        local currentSpeed, currentDistance = playbackSpeed(playback)
        if not setVehiclePlaybackSpeed(playback.vehicle, currentSpeed) then
            return fail(playback, "mise a jour dynamique de vitesse refusee")
        end
        if getTickCount() - playback.lastSampleAt >= 500 then
            playback.lastSampleAt = getTickCount()
            report(playback, "sample", {speed = currentSpeed, distance = currentDistance})
        end
    end, 50, 0)
end

addEvent("nativeTaskRuntime:recordedPlaybackStart", true)
addEventHandler("nativeTaskRuntime:recordedPlaybackStart", resourceRoot,
                function(handle, nonce, vehicle, recordingId, options)
    if not isElement(handle) or type(nonce) ~= "string" or not isElement(vehicle) or
        getElementType(vehicle) ~= "vehicle" then
        return
    end
    local old = playbacks[handle]
    if old then
        cleanup(old, true)
    end
    local playback = {
        handle = handle,
        nonce = nonce,
        vehicle = vehicle,
        recordingId = recordingId,
        options = options,
        requestedAt = getTickCount(),
    }
    playbacks[handle] = playback
    begin(playback)
end)

addEvent("nativeTaskRuntime:recordedPlaybackStop", true)
addEventHandler("nativeTaskRuntime:recordedPlaybackStop", resourceRoot, function(handle, nonce)
    local playback = playbacks[handle]
    if playback and playback.nonce == nonce then
        cleanup(playback, true)
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    local current = {}
    for _, playback in pairs(playbacks) do
        current[#current + 1] = playback
    end
    for _, playback in ipairs(current) do
        cleanup(playback, true)
    end
end)
