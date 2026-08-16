local savedState

local function chat(message, r, g, b)
    outputChatBox("[native-radio-playback] " .. message, r or 100, g or 220, b or 255)
end

local function validateState(state)
    if not state then
        return false, "getter returned false"
    end

    if state.trackId < 0 then
        return false, "trackId < 0"
    end

    if state.position < 0 then
        return false, "position < 0"
    end

    if state.length >= 0 and state.length <= state.position then
        return false, ("length (%d) <= position (%d)"):format(state.length, state.position)
    end

    if not state.queue or not state.queue[1] then
        return false, "queue[1] missing"
    end

    if state.queue[1].id ~= state.trackId then
        return false, ("queue[1].id (%d) ~= trackId (%d)"):format(state.queue[1].id, state.trackId)
    end

    return true
end

local function dumpRadioState()
    local state = getRadioPlaybackState()
    local valid, reason = validateState(state)
    if not valid then
        chat("FAIL state: " .. reason, 255, 80, 80)
        return
    end

    chat(("PASS state radioOn=%s station=%d mode=%d track=%d type=%d index=%d pos=%d len=%d flags=%d"):format(
        tostring(state.radioOn),
        state.station,
        state.mode,
        state.trackId,
        state.trackType,
        state.trackIndex,
        state.position,
        state.length,
        state.flags
    ), 100, 255, 140)

    for index, queuedTrack in ipairs(state.queue) do
        chat(("queue[%d] id=%d type=%d index=%d"):format(
            index,
            queuedTrack.id,
            queuedTrack.type,
            queuedTrack.index
        ), 180, 220, 255)
    end
end

local function saveRadioState()
    savedState = getRadioPlaybackState()
    local valid, reason = validateState(savedState)
    if not valid then
        savedState = nil
        chat("FAIL save: " .. reason, 255, 80, 80)
        return
    end

    chat(("PASS save snapshot at %d ms, track=%d"):format(savedState.position, savedState.trackId), 100, 255, 140)
end

local function restoreRadioState()
    if not savedState then
        chat("FAIL restore: no snapshot; use /radiosave first", 255, 80, 80)
        return
    end

    local ok = setRadioPlaybackState(savedState)
    chat(ok and "PASS restore" or "FAIL restore", ok and 100 or 255, ok and 255 or 80, ok and 140 or 80)
end

local function seekBackTenSeconds()
    local state = getRadioPlaybackState()
    if not state or state.position < 10000 then
        chat("FAIL seek: state unavailable or position < 10 seconds", 255, 80, 80)
        return
    end

    state.position = state.position - 10000
    local ok = setRadioPlaybackState(state)
    chat(ok and ("PASS seek: requested position %d ms"):format(state.position) or "FAIL seek", ok and 100 or 255,
        ok and 255 or 80, ok and 140 or 80)
end

local function sendRadioState()
    local state = getRadioPlaybackState()
    local valid, reason = validateState(state)
    if not valid then
        chat("FAIL sync send: " .. reason, 255, 80, 80)
        return
    end

    triggerServerEvent("native-radio-playback:relay", resourceRoot, state)
    chat(("PASS sync send: track=%d position=%d"):format(state.trackId, state.position), 100, 255, 140)
end

addCommandHandler("radiostate", dumpRadioState)
addCommandHandler("radiosave", saveRadioState)
addCommandHandler("radiorestore", restoreRadioState)
addCommandHandler("radioseek", seekBackTenSeconds)
addCommandHandler("radiosync", sendRadioState)

addEvent("native-radio-playback:receive", true)
addEventHandler("native-radio-playback:receive", resourceRoot, function(state, sender)
    local valid, reason = validateState(state)
    if not valid then
        chat("FAIL sync receive: " .. reason, 255, 80, 80)
        return
    end

    local expectedTrack = state.trackId
    local expectedPosition = state.position
    local ok = setRadioPlaybackState(state)
    if not ok then
        chat("FAIL sync apply", 255, 80, 80)
        return
    end

    chat(("PASS sync apply from %s: track=%d position=%d"):format(
        sender and getPlayerName(sender) or "client",
        expectedTrack,
        expectedPosition
    ), 100, 255, 140)

    setTimer(function()
        local current = getRadioPlaybackState()
        local currentValid = validateState(current)
        local positionDelta = current and math.abs(current.position - expectedPosition) or -1
        if currentValid and current.trackId == expectedTrack and positionDelta <= 3000 then
            chat(("PASS sync verify: track=%d positionDelta=%dms"):format(current.trackId, positionDelta), 100, 255, 140)
        else
            chat(("FAIL sync verify: track=%s positionDelta=%dms"):format(
                current and tostring(current.trackId) or "false",
                positionDelta
            ), 255, 80, 80)
        end
    end, 1500, 1)
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    chat("READY: /radiostate /radiosave /radiorestore /radioseek /radiosync", 100, 220, 255)
end)
