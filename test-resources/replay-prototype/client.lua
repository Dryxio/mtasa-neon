local BUFFER_DURATION_MS = 60000
local SAMPLE_INTERVAL_MS = 16
local HARD_RESYNC_DISTANCE = 8.0
local VELOCITY_ASSIST_START = 1.0
local MAX_VELOCITY_ASSIST = 0.018

local CONTROL_NAMES = {
    "forwards", "backwards", "left", "right", "sprint", "walk",
    "jump", "crouch", "aim_weapon", "fire", "action",
}

-- Normal GTA locomotion animations are task-driven. Moving a ped with
-- setElementPosition does not automatically select them, so hybrid playback
-- mirrors the recorded move state onto the smooth tracked ghost.
local MOVE_ANIMATIONS = {
    stand = {"PED", "IDLE_stance", true},
    walk = {"PED", "WALK_player", true},
    jog = {"PED", "RUN_player", true},
    sprint = {"PED", "sprint_civi", true},
    jump = {"PED", "JUMP_launch", false},
    fall = {"PED", "FALL_fall", true},
    land = {"PED", "JUMP_land", false},
}

local state = {
    recording = false,
    recordStartedAt = 0,
    lastSampleAt = 0,
    frames = {},
    firstFrame = 1,

    playback = false,
    paused = false,
    playbackStartedAt = 0,
    playbackOffset = 0,
    playbackIndex = 1,
    replayMode = "hybrid",

    ghost = nil,
    ghostWeapon = nil,
    ghostMoveState = nil,

    corrections = 0,
    hardResyncs = 0,
    currentDrift = 0,
    maxDrift = 0,
    debug = true,
}

local function chat(message, r, g, b)
    outputChatBox("[REPLAY] " .. message, r or 120, g or 220, b or 255)
end

local function clamp(value, minimum, maximum)
    return math.max(minimum, math.min(maximum, value))
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

local function getMoveState(ped)
    if type(getPedMoveState) == "function" then
        local ok, moveState = pcall(getPedMoveState, ped)
        if ok and type(moveState) == "string" then
            return moveState
        end
    end
    return "stand"
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

    state.frames[#state.frames + 1] = {
        t = now - state.recordStartedAt,
        x = x, y = y, z = z,
        rx = rx, ry = ry, rz = rz,
        vx = vx, vy = vy, vz = vz,
        cx = cx, cy = cy, cz = cz,
        tx = tx, ty = ty, tz = tz,
        aimX = aimX, aimY = aimY, aimZ = aimZ,
        weapon = getPedWeapon(player),
        weaponSlot = getPedWeaponSlot(player),
        controls = getControls(player),
        moveState = getMoveState(player),
    }

    local timestamp = state.frames[#state.frames].t
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
        setPedAnimation(state.ghost, false)
        destroyElement(state.ghost)
    end

    state.ghost = nil
    state.ghostWeapon = nil
    state.ghostMoveState = nil
    state.playback = false
    state.paused = false
    state.playbackIndex = 1
    state.playbackOffset = 0
    state.currentDrift = 0
end

local function ensureGhostWeapon(frame)
    if not isElement(state.ghost) or not frame or state.ghostWeapon == frame.weapon then
        return
    end

    state.ghostWeapon = frame.weapon
    if frame.weapon and frame.weapon > 0 and type(givePedWeapon) == "function" then
        givePedWeapon(state.ghost, frame.weapon, 9999, true)
    elseif type(setPedWeaponSlot) == "function" then
        setPedWeaponSlot(state.ghost, frame.weaponSlot or 0)
    end
end

local function applyMoveAnimation(frame)
    if not isElement(state.ghost) then
        return
    end

    local moveState = frame.moveState or "stand"
    if state.ghostMoveState == moveState then
        return
    end
    state.ghostMoveState = moveState

    -- Crouch/aim are better left to GTA controls because their weapon-specific
    -- upper body pose matters more than a generic animation replacement.
    if moveState == "crouch" or frame.controls[9] then
        setPedAnimation(state.ghost, false)
        return
    end

    local animation = MOVE_ANIMATIONS[moveState]
    if not animation then
        setPedAnimation(state.ghost, false)
        return
    end

    setPedAnimation(state.ghost, animation[1], animation[2], -1, animation[3], true, false, false)
end

local function getReplayBounds()
    if #state.frames < 2 then
        return nil
    end
    return state.frames[state.firstFrame], state.frames[#state.frames]
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
    return a, b, clamp((time - a.t) / duration, 0, 1)
end

local function spawnGhost()
    local first = state.frames[state.firstFrame]
    if not first then
        return false
    end

    stopGhost()
    local ghost = createPed(getElementModel(localPlayer), first.x, first.y, first.z, first.rz)
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
    state.playback = true
    state.paused = false
    state.playbackOffset = clamp(first.t + offsetMs, first.t, last.t)
    state.playbackStartedAt = getTickCount()
    state.playbackIndex = state.firstFrame
    state.corrections = 0
    state.hardResyncs = 0
    state.currentDrift = 0
    state.maxDrift = 0

    chat(("Playback started (%s mode, %.2fs buffered, %d samples)."):format(
        state.replayMode, (last.t - first.t) / 1000, #state.frames - state.firstFrame + 1
    ), 120, 255, 120)
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
    local gx, gy, gz = getElementPosition(state.ghost)

    state.currentDrift = distance3D(gx, gy, gz, targetX, targetY, targetZ)
    state.maxDrift = math.max(state.maxDrift, state.currentDrift)

    setElementPosition(state.ghost, targetX, targetY, targetZ)
    setElementRotation(state.ghost,
        lerpAngle(a.rx, b.rx, alpha),
        lerpAngle(a.ry, b.ry, alpha),
        lerpAngle(a.rz, b.rz, alpha)
    )
    setElementVelocity(state.ghost,
        lerp(a.vx, b.vx, alpha),
        lerp(a.vy, b.vy, alpha),
        lerp(a.vz, b.vz, alpha)
    )
end

local function updateControlDrivenMovement(a, b, alpha)
    local targetX = lerp(a.x, b.x, alpha)
    local targetY = lerp(a.y, b.y, alpha)
    local targetZ = lerp(a.z, b.z, alpha)
    local gx, gy, gz = getElementPosition(state.ghost)
    local dx, dy, dz = targetX - gx, targetY - gy, targetZ - gz
    local drift = math.sqrt(dx * dx + dy * dy + dz * dz)

    state.currentDrift = drift
    state.maxDrift = math.max(state.maxDrift, drift)

    -- Do not touch position during normal controls playback. setElementPosition
    -- resets/interferes with the ped locomotion task, which is what caused the
    -- repeated teleport + animation restart behaviour in the first prototype.
    if drift > HARD_RESYNC_DISTANCE then
        setElementPosition(state.ghost, targetX, targetY, targetZ)
        setElementRotation(state.ghost, 0, 0, lerpAngle(a.rz, b.rz, alpha))
        setElementVelocity(state.ghost, lerp(a.vx, b.vx, alpha), lerp(a.vy, b.vy, alpha), lerp(a.vz, b.vz, alpha))
        state.hardResyncs = state.hardResyncs + 1
        return
    end

    if drift <= VELOCITY_ASSIST_START then
        return
    end

    local vx, vy, vz = getElementVelocity(state.ghost)
    local horizontal = math.sqrt(dx * dx + dy * dy)
    if horizontal > 0.001 then
        local assist = math.min(MAX_VELOCITY_ASSIST, (horizontal - VELOCITY_ASSIST_START) * 0.004)
        vx = vx + dx / horizontal * assist
        vy = vy + dy / horizontal * assist
    end

    if math.abs(dz) > 1.0 then
        vz = vz + clamp(dz * 0.002, -0.006, 0.006)
    end

    setElementVelocity(state.ghost, vx, vy, vz)
    state.corrections = state.corrections + 1
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
        setPedAnimation(state.ghost, false)
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

    ensureGhostWeapon(a)

    if state.replayMode == "controls" then
        -- Pure diagnostic mode: GTA owns locomotion and we only bias velocity.
        setPedAnimation(state.ghost, false)
        state.ghostMoveState = nil
        applyControls(state.ghost, a.controls)
        updateControlDrivenMovement(a, b, alpha)
    else
        -- Smooth recorded trajectory. Hybrid additionally mirrors locomotion
        -- animations while controls keep crouch/aim/fire states available.
        updateTrackedMovement(a, b, alpha)
        applyControls(state.ghost, a.controls)
        if state.replayMode == "hybrid" then
            applyMoveAnimation(a)
        else
            setPedAnimation(state.ghost, false)
            state.ghostMoveState = nil
        end
    end

    if type(setPedAimTarget) == "function" and (a.controls[9] or a.controls[10]) then
        setPedAimTarget(state.ghost,
            lerp(a.aimX, b.aimX, alpha),
            lerp(a.aimY, b.aimY, alpha),
            lerp(a.aimZ, b.aimZ, alpha)
        )
    end
end

local function seekPlayback(seconds)
    local first, last = getReplayBounds()
    if not first or not isElement(state.ghost) then
        return
    end

    local target = math.min(last.t, first.t + math.max(0, tonumber(seconds) or 0) * 1000)
    state.playbackOffset = target
    state.playbackStartedAt = getTickCount()
    state.playbackIndex = state.firstFrame
    state.ghostMoveState = nil

    local frame = select(1, findPlaybackFrames(target))
    if frame then
        setElementPosition(state.ghost, frame.x, frame.y, frame.z)
        setElementRotation(state.ghost, frame.rx, frame.ry, frame.rz)
        setElementVelocity(state.ghost, frame.vx, frame.vy, frame.vz)
        ensureGhostWeapon(frame)
        applyControls(state.ghost, frame.controls)
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

    dxDrawText(("Replay prototype\nrecording: %s | samples: %d | buffer: %.2fs\nplayback: %s%s | mode: %s | time: %.2fs\nmove: %s | drift: %.2fm | max: %.2fm | corrections: %d | hard resyncs: %d")
        :format(
            tostring(state.recording), sampleCount, buffered,
            tostring(state.playback), state.paused and " (paused)" or "",
            state.replayMode, playbackTime,
            tostring(state.ghostMoveState or "native"), state.currentDrift, state.maxDrift,
            state.corrections, state.hardResyncs
        ),
        24, 180, 800, 330, tocolor(255, 255, 255, 230), 1.0, "default-bold")
end)

addCommandHandler("replayrec", function()
    stopGhost()
    state.frames = {}
    state.firstFrame = 1
    state.recording = true
    state.recordStartedAt = getTickCount()
    state.lastSampleAt = 0
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
    clearControls(state.ghost)
    setPedAnimation(state.ghost, false)
    setElementFrozen(state.ghost, true)
    chat("Playback paused.")
end)

addCommandHandler("replayresume", function()
    if not state.playback or not state.paused then
        return
    end
    state.paused = false
    state.playbackStartedAt = getTickCount()
    state.ghostMoveState = nil
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
    if mode ~= "hybrid" and mode ~= "tracked" and mode ~= "controls" then
        chat("Usage: /replaymode [hybrid|tracked|controls]. Current: " .. state.replayMode, 255, 220, 100)
        return
    end

    state.replayMode = mode
    state.currentDrift = 0
    state.maxDrift = 0
    state.ghostMoveState = nil
    chat("Replay mode: " .. mode, 120, 255, 120)
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
