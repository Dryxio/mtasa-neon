local BUFFER_DURATION_MS = 60000
local SAMPLE_INTERVAL_MS = 16
local KEYFRAME_INTERVAL_MS = 500
local SOFT_CORRECTION_DISTANCE = 0.75
local HARD_CORRECTION_DISTANCE = 4.0
local SOFT_CORRECTION_FACTOR = 0.08

local CONTROL_NAMES = {
    "forwards",
    "backwards",
    "left",
    "right",
    "sprint",
    "walk",
    "jump",
    "crouch",
    "aim_weapon",
    "fire",
    "action",
}

local state = {
    recording = false,
    recordStartedAt = 0,
    lastSampleAt = 0,
    lastKeyframeAt = 0,
    frames = {},
    firstFrame = 1,
    playback = false,
    paused = false,
    playbackStartedAt = 0,
    pausedAt = 0,
    playbackOffset = 0,
    playbackIndex = 1,
    ghost = nil,
    ghostWeapon = nil,
    corrections = 0,
    hardResyncs = 0,
    currentDrift = 0,
    maxDrift = 0,
    lastCorrectedKeyframe = nil,
    replayMode = "tracked",
    debug = true,
}

local function chat(message, r, g, b)
    outputChatBox("[REPLAY] " .. message, r or 120, g or 220, b or 255)
end

local function distance3D(ax, ay, az, bx, by, bz)
    local dx, dy, dz = ax - bx, ay - by, az - bz
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

local function lerp(a, b, t)
    return a + (b - a) * t
end

local function lerpAngle(a, b, t)
    local delta = (b - a + 180) % 360 - 180
    return a + delta * t
end

local function getControls(ped)
    local controls = {}
    for i, control in ipairs(CONTROL_NAMES) do
        controls[i] = getPedControlState(ped, control) == true
    end
    return controls
end

local function applyControls(ped, controls)
    for i, control in ipairs(CONTROL_NAMES) do
        setPedControlState(ped, control, controls[i] == true)
    end
end

local function clearControls(ped)
    if not isElement(ped) then
        return
    end

    for _, control in ipairs(CONTROL_NAMES) do
        setPedControlState(ped, control, false)
    end
end

local function getAimTarget(player, cameraTargetX, cameraTargetY, cameraTargetZ)
    if type(getPedTargetEnd) == "function" then
        local ok, x, y, z = pcall(getPedTargetEnd, player)
        if ok and x then
            return x, y, z
        end
    end

    return cameraTargetX, cameraTargetY, cameraTargetZ
end

local function captureFrame(now)
    local player = localPlayer
    if not isElement(player) or isPedInVehicle(player) or isPedDead(player) then
        return
    end

    local x, y, z = getElementPosition(player)
    local rx, ry, rz = getElementRotation(player)
    local vx, vy, vz = getElementVelocity(player)
    local cx, cy, cz, tx, ty, tz = getCameraMatrix()
    local aimX, aimY, aimZ = getAimTarget(player, tx, ty, tz)
    local timestamp = now - state.recordStartedAt
    local keyframe = timestamp - state.lastKeyframeAt >= KEYFRAME_INTERVAL_MS or #state.frames == 0

    if keyframe then
        state.lastKeyframeAt = timestamp
    end

    state.frames[#state.frames + 1] = {
        t = timestamp,
        x = x,
        y = y,
        z = z,
        rx = rx,
        ry = ry,
        rz = rz,
        vx = vx,
        vy = vy,
        vz = vz,
        cx = cx,
        cy = cy,
        cz = cz,
        tx = tx,
        ty = ty,
        tz = tz,
        aimX = aimX,
        aimY = aimY,
        aimZ = aimZ,
        weapon = getPedWeapon(player),
        weaponSlot = getPedWeaponSlot(player),
        controls = getControls(player),
        keyframe = keyframe,
    }

    local cutoff = timestamp - BUFFER_DURATION_MS
    while state.firstFrame < #state.frames and state.frames[state.firstFrame + 1].t < cutoff do
        state.firstFrame = state.firstFrame + 1
    end

    if state.firstFrame > 512 then
        local compacted = {}
        for i = state.firstFrame, #state.frames do
            compacted[#compacted + 1] = state.frames[i]
        end
        state.frames = compacted
        state.firstFrame = 1
    end
end

local function stopGhost()
    if isElement(state.ghost) then
        clearControls(state.ghost)
        destroyElement(state.ghost)
    end

    state.ghost = nil
    state.ghostWeapon = nil
    state.playback = false
    state.paused = false
    state.playbackIndex = 1
    state.playbackOffset = 0
    state.currentDrift = 0
    state.lastCorrectedKeyframe = nil
end

local function ensureGhostWeapon(frame)
    if not isElement(state.ghost) or not frame then
        return
    end

    if state.ghostWeapon == frame.weapon then
        return
    end

    state.ghostWeapon = frame.weapon

    if frame.weapon and frame.weapon > 0 and type(givePedWeapon) == "function" then
        givePedWeapon(state.ghost, frame.weapon, 9999, true)
    elseif type(setPedWeaponSlot) == "function" then
        setPedWeaponSlot(state.ghost, frame.weaponSlot or 0)
    end
end

local function getReplayBounds()
    if #state.frames < 2 then
        return nil
    end

    local first = state.frames[state.firstFrame]
    local last = state.frames[#state.frames]
    return first, last
end

local function findPlaybackFrames(time)
    local first, last = getReplayBounds()
    if not first then
        return nil
    end

    if time <= first.t then
        state.playbackIndex = state.firstFrame
        return first, first, 0
    end

    if time >= last.t then
        return last, last, 0
    end

    local index = math.max(state.playbackIndex, state.firstFrame)
    while index < #state.frames and state.frames[index + 1].t <= time do
        index = index + 1
    end

    while index > state.firstFrame and state.frames[index].t > time do
        index = index - 1
    end

    state.playbackIndex = index

    local a = state.frames[index]
    local b = state.frames[math.min(index + 1, #state.frames)]
    local duration = math.max(b.t - a.t, 1)
    local alpha = math.max(0, math.min(1, (time - a.t) / duration))
    return a, b, alpha
end

local function spawnGhost()
    local first = state.frames[state.firstFrame]
    if not first then
        return false
    end

    stopGhost()

    local model = getElementModel(localPlayer)
    local ghost = createPed(model, first.x, first.y, first.z, first.rz)
    if not isElement(ghost) then
        chat("Could not create replay ghost.", 255, 80, 80)
        return false
    end

    state.ghost = ghost
    setElementInterior(ghost, getElementInterior(localPlayer))
    setElementDimension(ghost, getElementDimension(localPlayer))
    setElementCollisionsEnabled(ghost, true)
    setElementAlpha(ghost, 190)
    setElementVelocity(ghost, first.vx, first.vy, first.vz)
    ensureGhostWeapon(first)
    applyControls(ghost, first.controls)

    return true
end

local function startPlayback(offsetMs)
    local first, last = getReplayBounds()
    if not first then
        chat("Need at least two recorded samples first. Use /replayrec.", 255, 120, 80)
        return
    end

    if not spawnGhost() then
        return
    end

    offsetMs = tonumber(offsetMs) or 0
    offsetMs = math.max(first.t, math.min(last.t, first.t + offsetMs))

    state.playback = true
    state.paused = false
    state.playbackOffset = offsetMs
    state.playbackStartedAt = getTickCount()
    state.playbackIndex = state.firstFrame
    state.corrections = 0
    state.hardResyncs = 0
    state.currentDrift = 0
    state.maxDrift = 0
    state.lastCorrectedKeyframe = nil

    chat(("Playback started (%s mode, %.2fs buffered, %d samples)."):format(state.replayMode, (last.t - first.t) / 1000, #state.frames - state.firstFrame + 1), 120, 255, 120)
end

local function seekPlayback(seconds)
    local first, last = getReplayBounds()
    if not first then
        return
    end

    local offset = math.max(0, tonumber(seconds) or 0) * 1000
    local target = math.min(last.t, first.t + offset)

    state.playbackOffset = target
    state.playbackStartedAt = getTickCount()
    state.playbackIndex = state.firstFrame
    state.lastCorrectedKeyframe = nil

    local frame = select(1, findPlaybackFrames(target))
    if frame and isElement(state.ghost) then
        setElementPosition(state.ghost, frame.x, frame.y, frame.z)
        setElementRotation(state.ghost, frame.rx, frame.ry, frame.rz)
        setElementVelocity(state.ghost, frame.vx, frame.vy, frame.vz)
        ensureGhostWeapon(frame)
        applyControls(state.ghost, frame.controls)
    end
end

local function getPlaybackTime(now)
    if state.paused then
        return state.playbackOffset
    end

    return state.playbackOffset + (now - state.playbackStartedAt)
end

local function updateTrackedMovement(a, b, alpha)
    local targetX = lerp(a.x, b.x, alpha)
    local targetY = lerp(a.y, b.y, alpha)
    local targetZ = lerp(a.z, b.z, alpha)
    local targetRX = lerpAngle(a.rx, b.rx, alpha)
    local targetRY = lerpAngle(a.ry, b.ry, alpha)
    local targetRZ = lerpAngle(a.rz, b.rz, alpha)
    local targetVX = lerp(a.vx, b.vx, alpha)
    local targetVY = lerp(a.vy, b.vy, alpha)
    local targetVZ = lerp(a.vz, b.vz, alpha)

    local gx, gy, gz = getElementPosition(state.ghost)
    state.currentDrift = distance3D(gx, gy, gz, targetX, targetY, targetZ)
    state.maxDrift = math.max(state.maxDrift, state.currentDrift)

    setElementPosition(state.ghost, targetX, targetY, targetZ)
    setElementRotation(state.ghost, targetRX, targetRY, targetRZ)
    setElementVelocity(state.ghost, targetVX, targetVY, targetVZ)
end

local function updateControlDrivenMovement(a, b, alpha)
    local targetX = lerp(a.x, b.x, alpha)
    local targetY = lerp(a.y, b.y, alpha)
    local targetZ = lerp(a.z, b.z, alpha)
    local gx, gy, gz = getElementPosition(state.ghost)
    local errorDistance = distance3D(gx, gy, gz, targetX, targetY, targetZ)

    state.currentDrift = errorDistance
    state.maxDrift = math.max(state.maxDrift, errorDistance)

    if not a.keyframe or state.lastCorrectedKeyframe == a.t then
        return
    end

    state.lastCorrectedKeyframe = a.t

    if errorDistance > HARD_CORRECTION_DISTANCE then
        setElementPosition(state.ghost, a.x, a.y, a.z)
        setElementRotation(state.ghost, a.rx, a.ry, a.rz)
        setElementVelocity(state.ghost, a.vx, a.vy, a.vz)
        state.hardResyncs = state.hardResyncs + 1
    elseif errorDistance > SOFT_CORRECTION_DISTANCE then
        local correction = math.min(0.18, SOFT_CORRECTION_FACTOR + errorDistance * 0.02)
        setElementPosition(
            state.ghost,
            lerp(gx, a.x, correction),
            lerp(gy, a.y, correction),
            lerp(gz, a.z, correction)
        )
        state.corrections = state.corrections + 1
    end
end

local function updatePlayback(now)
    if not state.playback or not isElement(state.ghost) then
        return
    end

    local first, last = getReplayBounds()
    if not first then
        stopGhost()
        return
    end

    local playbackTime = getPlaybackTime(now)
    if playbackTime >= last.t then
        clearControls(state.ghost)
        state.playback = false
        chat(("Playback complete. Max drift %.2fm."):format(state.maxDrift), 120, 255, 120)
        return
    end

    if state.paused then
        return
    end

    local a, b, alpha = findPlaybackFrames(playbackTime)
    if not a then
        return
    end

    applyControls(state.ghost, a.controls)
    ensureGhostWeapon(a)

    if type(setPedAimTarget) == "function" and (a.controls[9] or a.controls[10]) then
        local aimX = lerp(a.aimX, b.aimX, alpha)
        local aimY = lerp(a.aimY, b.aimY, alpha)
        local aimZ = lerp(a.aimZ, b.aimZ, alpha)
        setPedAimTarget(state.ghost, aimX, aimY, aimZ)
    end

    if state.replayMode == "controls" then
        updateControlDrivenMovement(a, b, alpha)
    else
        updateTrackedMovement(a, b, alpha)
    end
end

addEventHandler("onClientPreRender", root, function()
    local now = getTickCount()

    if state.recording and now - state.lastSampleAt >= SAMPLE_INTERVAL_MS then
        state.lastSampleAt = now
        captureFrame(now)
    end

    updatePlayback(now)
end)

addEventHandler("onClientRender", root, function()
    if not state.debug then
        return
    end

    local first, last = getReplayBounds()
    local buffered = first and ((last.t - first.t) / 1000) or 0
    local sampleCount = first and (#state.frames - state.firstFrame + 1) or 0
    local playbackTime = state.playback and getPlaybackTime(getTickCount()) / 1000 or 0

    dxDrawText(
        ("Replay prototype\nrecording: %s | samples: %d | buffer: %.2fs\nplayback: %s%s | mode: %s | time: %.2fs\ndrift: %.2fm | max: %.2fm | corrections: %d | hard resyncs: %d")
            :format(
                tostring(state.recording),
                sampleCount,
                buffered,
                tostring(state.playback),
                state.paused and " (paused)" or "",
                state.replayMode,
                playbackTime,
                state.currentDrift,
                state.maxDrift,
                state.corrections,
                state.hardResyncs
            ),
        24,
        180,
        760,
        320,
        tocolor(255, 255, 255, 230),
        1.0,
        "default-bold"
    )
end)

addCommandHandler("replayrec", function()
    stopGhost()
    state.frames = {}
    state.firstFrame = 1
    state.recording = true
    state.recordStartedAt = getTickCount()
    state.lastSampleAt = 0
    state.lastKeyframeAt = -KEYFRAME_INTERVAL_MS
    chat("Recording started. On-foot only; keeps the last 60 seconds.", 120, 255, 120)
end)

addCommandHandler("replaystop", function()
    if state.recording then
        state.recording = false
        local first, last = getReplayBounds()
        if first then
            chat(("Recording stopped: %.2fs, %d samples."):format((last.t - first.t) / 1000, #state.frames - state.firstFrame + 1), 255, 220, 100)
        else
            chat("Recording stopped with no usable samples.", 255, 120, 80)
        end
        return
    end

    if state.playback or isElement(state.ghost) then
        stopGhost()
        chat("Playback stopped.", 255, 220, 100)
    end
end)

addCommandHandler("replayplay", function(_, seconds)
    state.recording = false
    startPlayback((tonumber(seconds) or 0) * 1000)
end)

addCommandHandler("replaypause", function()
    if not state.playback or state.paused then
        return
    end

    state.playbackOffset = getPlaybackTime(getTickCount())
    state.paused = true
    state.pausedAt = getTickCount()
    clearControls(state.ghost)
    setElementFrozen(state.ghost, true)
    chat("Playback paused.")
end)

addCommandHandler("replayresume", function()
    if not state.playback or not state.paused then
        return
    end

    state.paused = false
    state.playbackStartedAt = getTickCount()
    setElementFrozen(state.ghost, false)
    chat("Playback resumed.")
end)

addCommandHandler("replayseek", function(_, seconds)
    if not state.playback then
        chat("Start playback first with /replayplay.", 255, 120, 80)
        return
    end

    seekPlayback(seconds)
    chat(("Seeked to %.2fs."):format(tonumber(seconds) or 0))
end)

addCommandHandler("replaymode", function(_, mode)
    mode = mode and mode:lower() or nil
    if mode ~= "tracked" and mode ~= "controls" then
        chat("Usage: /replaymode [tracked|controls]. Current: " .. state.replayMode, 255, 220, 100)
        return
    end

    state.replayMode = mode
    state.currentDrift = 0
    state.maxDrift = 0
    state.lastCorrectedKeyframe = nil
    chat("Replay mode: " .. mode .. (mode == "tracked" and " (smooth recorded trajectory)." or " (GTA controls + sparse corrections)."), 120, 255, 120)
end)

addCommandHandler("replaydebug", function()
    state.debug = not state.debug
    chat("Debug overlay: " .. tostring(state.debug))
end)

addCommandHandler("replayclear", function()
    state.recording = false
    stopGhost()
    state.frames = {}
    state.firstFrame = 1
    chat("Replay buffer cleared.")
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    stopGhost()
end)
