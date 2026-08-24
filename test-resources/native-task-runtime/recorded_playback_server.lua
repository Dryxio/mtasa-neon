local playbacks = {}
local playbacksByVehicle = {}
local nextPlaybackId = 0

local function validPlayer(player)
    return isElement(player) and getElementType(player) == "player"
end

local function copyOptions(options)
    options = type(options) == "table" and options or {}
    local target = options.target
    if target ~= nil and not isElement(target) then
        return false, "cible de rattrapage invalide"
    end

    local pivotDistance = math.max(1, tonumber(options.pivotDistance) or 20)
    return {
        target = target,
        distanceMode = options.distanceMode == "2d" and "2d" or "3d",
        pivotDistance = pivotDistance,
        closeSpeed = math.max(0.05, tonumber(options.closeSpeed) or 2),
        pivotSpeed = math.max(0.05, tonumber(options.pivotSpeed) or 1),
        farSlopeDistance = math.max(1, tonumber(options.farSlopeDistance) or 90),
        minimumSpeed = math.max(0.05, tonumber(options.minimumSpeed) or 0.5),
        maximumSpeed = math.max(0.05, tonumber(options.maximumSpeed) or 2),
        loadTimeout = math.max(1000, math.floor(tonumber(options.loadTimeout) or 15000)),
        playbackTimeout = math.max(1000, math.floor(tonumber(options.playbackTimeout) or 120000)),
    }
end

local function snapshot(playback, extra)
    local data = {
        id = playback.id,
        state = playback.state,
        vehicle = playback.vehicle,
        recordingId = playback.recordingId,
        owner = playback.owner,
        reason = playback.reason,
    }
    for key, value in pairs(type(extra) == "table" and extra or {}) do
        data[key] = value
    end
    return data
end

local function emit(playback, state, extra)
    playback.state = state
    triggerEvent("onNativeRecordedVehiclePlaybackStateChange", playback.handle, state, snapshot(playback, extra))
end

local function removePlayback(playback, destroyHandle)
    if not playback or playback.removing then
        return
    end
    playback.removing = true
    if validPlayer(playback.owner) then
        triggerClientEvent(playback.owner, "nativeTaskRuntime:recordedPlaybackStop", resourceRoot, playback.handle,
                           playback.nonce)
    end
    playbacks[playback.handle] = nil
    if playbacksByVehicle[playback.vehicle] == playback then
        playbacksByVehicle[playback.vehicle] = nil
    end
    if destroyHandle and isElement(playback.handle) then
        destroyElement(playback.handle)
    end
end

local function fail(playback, reason)
    if not playback or playback.removing or playback.state == "failed" then
        return
    end
    playback.reason = reason
    emit(playback, "failed", {reason = reason})
    if validPlayer(playback.owner) then
        triggerClientEvent(playback.owner, "nativeTaskRuntime:recordedPlaybackStop", resourceRoot, playback.handle,
                           playback.nonce)
    end
end

function createNativeRecordedVehiclePlayback(vehicle, recordingId, owner, options)
    if not isElement(vehicle) or getElementType(vehicle) ~= "vehicle" then
        return false, "vehicule invalide"
    end
    recordingId = math.floor(tonumber(recordingId) or -1)
    if recordingId < 0 then
        return false, "recording invalide"
    end
    if not validPlayer(owner) then
        return false, "owner invalide"
    end
    if playbacksByVehicle[vehicle] then
        return false, "vehicule deja pilote par un recording"
    end
    local immutable, reason = copyOptions(options)
    if not immutable then
        return false, reason
    end

    nextPlaybackId = nextPlaybackId + 1
    local handle = createElement("native-recorded-vehicle-playback",
                                 "native-recorded-vehicle-playback-" .. tostring(nextPlaybackId))
    if not handle then
        return false, "creation du handle refusee"
    end
    local playback = {
        id = nextPlaybackId,
        handle = handle,
        caller = sourceResourceRoot or resourceRoot,
        vehicle = vehicle,
        recordingId = recordingId,
        owner = owner,
        options = immutable,
        nonce = tostring(getTickCount()) .. ":" .. tostring(math.random(100000, 999999)),
        state = "created",
    }
    playbacks[handle] = playback
    playbacksByVehicle[vehicle] = playback
    setElementParent(handle, playback.caller)
    emit(playback, "dispatched")
    triggerClientEvent(owner, "nativeTaskRuntime:recordedPlaybackStart", resourceRoot, handle, playback.nonce,
                       vehicle, recordingId, immutable)
    return handle
end

function cancelNativeRecordedVehiclePlayback(handle)
    local playback = playbacks[handle]
    if not playback or playback.caller ~= (sourceResourceRoot or resourceRoot) then
        return false
    end
    emit(playback, "cancelled")
    removePlayback(playback, true)
    return true
end

function getNativeRecordedVehiclePlaybackState(handle)
    local playback = playbacks[handle]
    if not playback or playback.caller ~= (sourceResourceRoot or resourceRoot) then
        return false
    end
    return snapshot(playback)
end

addEvent("onNativeRecordedVehiclePlaybackStateChange", false)
addEvent("nativeTaskRuntime:recordedPlaybackEvidence", true)
addEventHandler("nativeTaskRuntime:recordedPlaybackEvidence", resourceRoot,
                function(handle, nonce, evidence, data)
    local playback = playbacks[handle]
    if not playback or playback.removing or client ~= playback.owner or nonce ~= playback.nonce then
        return
    end
    data = type(data) == "table" and data or {}
    if evidence == "active" and playback.state == "dispatched" then
        return emit(playback, "active", data)
    end
    if evidence == "sample" and playback.state == "active" then
        if getElementSyncer(playback.vehicle) ~= playback.owner then
            return fail(playback, "autorite vehicule perdue")
        end
        data.sample = true
        return emit(playback, "active", data)
    end
    if evidence == "completed" and playback.state == "active" then
        -- Release the vehicle index before notifying the caller so a mission can
        -- chain the next recording synchronously from the completion event.
        if playbacksByVehicle[playback.vehicle] == playback then
            playbacksByVehicle[playback.vehicle] = nil
        end
        emit(playback, "completed", data)
        return removePlayback(playback, true)
    end
    if evidence == "failure" then
        return fail(playback, data.reason or "echec client sans detail")
    end
end)

addEventHandler("onElementDestroy", root, function()
    local playback = playbacks[source] or playbacksByVehicle[source]
    if playback and not playback.removing then
        if source ~= playback.handle then
            fail(playback, "vehicule detruit pendant le playback")
        end
        removePlayback(playback, source ~= playback.handle)
    end
end)

addEventHandler("onPlayerQuit", root, function()
    for _, playback in pairs(playbacks) do
        if playback.owner == source then
            fail(playback, "owner deconnecte pendant le playback")
        end
    end
end)

addEventHandler("onResourceStop", root, function(stoppedResource)
    local stoppedRoot = getResourceRootElement(stoppedResource)
    local owned = {}
    for _, playback in pairs(playbacks) do
        if playback.caller == stoppedRoot then
            owned[#owned + 1] = playback
        end
    end
    for _, playback in ipairs(owned) do
        removePlayback(playback, true)
    end
end)

outputServerLog("[native task runtime] Ready: synchronized recorded-vehicle playback available.")
