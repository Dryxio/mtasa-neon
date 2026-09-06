local SEND_INTERVAL_MS = 80
local DIRECTION_SMOOTHING = 0.35
local TARGET_DISTANCE = 50

local pointing = {}
local keyHeld = false
local lastSendTick = 0

local function normalize(x, y, z)
    local length = math.sqrt(x * x + y * y + z * z)
    if length < 0.0001 then
        return nil
    end
    return x / length, y / length, z / length
end

local function cameraDirection()
    local cameraX, cameraY, cameraZ, lookX, lookY, lookZ = getCameraMatrix()
    return normalize(lookX - cameraX, lookY - cameraY, lookZ - cameraZ)
end

local function canPoint()
    return not isPedDead(localPlayer)
        and not isPedInVehicle(localPlayer)
        and not isChatBoxInputActive()
        and not isConsoleActive()
        and not isMainMenuActive()
end

local function releasePointArm(state)
    if state and state.token then
        releasePedNativePointArm(state.token)
        state.token = nil
    end
end

local function setPointingState(player, active, directionX, directionY, directionZ)
    if not isElement(player) then
        return
    end

    if not active then
        releasePointArm(pointing[player])
        pointing[player] = nil
        return
    end

    local normalizedX, normalizedY, normalizedZ = normalize(directionX, directionY, directionZ)
    if not normalizedX then
        return
    end

    local state = pointing[player]
    if state then
        state.targetX, state.targetY, state.targetZ = normalizedX, normalizedY, normalizedZ
    else
        pointing[player] = {
            currentX = normalizedX,
            currentY = normalizedY,
            currentZ = normalizedZ,
            targetX = normalizedX,
            targetY = normalizedY,
            targetZ = normalizedZ,
        }
    end
end

local function stopLocalPointing()
    if pointing[localPlayer] then
        setPointingState(localPlayer, false)
        triggerServerEvent("cameraPointing:update", resourceRoot, false)
    end
end

local function handlePointingKey(_, keyState)
    keyHeld = keyState == "down"

    if not keyHeld then
        stopLocalPointing()
        return
    end

    if not canPoint() then
        outputChatBox("[Pointing] E detecte, mais le personnage ne peut pas pointer ici.", 255, 180, 80)
        return
    end

    local directionX, directionY, directionZ = cameraDirection()
    if directionX then
        setPointingState(localPlayer, true, directionX, directionY, directionZ)
        triggerServerEvent("cameraPointing:update", resourceRoot, true, directionX, directionY, directionZ)
        lastSendTick = getTickCount()
    end
end

local function updatePointArm(player, state)
    if isPedDead(player) or isPedInVehicle(player) or not isElementStreamedIn(player) then
        releasePointArm(state)
        return
    end

    if not state.token then
        state.token = acquirePedNativePointArm(player)
        if not state.token then
            if player == localPlayer and not state.acquireFailureReported then
                outputDebugString("[camera-pointing] GTA refused the local point-arm lease", 2)
                state.acquireFailureReported = true
            end
            return
        end
        state.acquireFailureReported = nil
    end

    local playerX, playerY, playerZ = getElementPosition(player)
    local updated = updatePedNativePointArm(
        state.token,
        Vector3(
            playerX + state.currentX * TARGET_DISTANCE,
            playerY + state.currentY * TARGET_DISTANCE,
            playerZ + 1.0 + state.currentZ * TARGET_DISTANCE
        )
    )
    if not updated then
        if player == localPlayer then
            outputDebugString("[camera-pointing] GTA refused a local point-arm update", 2)
        end
        releasePointArm(state)
    end
end

addEvent("cameraPointing:setState", true)
addEventHandler("cameraPointing:setState", resourceRoot, function(player, active, directionX, directionY, directionZ)
    if player == localPlayer then
        return
    end
    setPointingState(player, active, directionX, directionY, directionZ)
end)

addEventHandler("onClientPedsProcessed", root, function()
    if keyHeld then
        if not canPoint() then
            keyHeld = false
            stopLocalPointing()
        else
            local directionX, directionY, directionZ = cameraDirection()
            if directionX then
                setPointingState(localPlayer, true, directionX, directionY, directionZ)

                local now = getTickCount()
                if now - lastSendTick >= SEND_INTERVAL_MS then
                    triggerServerEvent("cameraPointing:update", resourceRoot, true, directionX, directionY, directionZ)
                    lastSendTick = now
                end
            end
        end
    end

    for player, state in pairs(pointing) do
        if not isElement(player) then
            releasePointArm(state)
            pointing[player] = nil
        else
            local blend = player == localPlayer and 1 or DIRECTION_SMOOTHING
            local currentX, currentY, currentZ = normalize(
                state.currentX + (state.targetX - state.currentX) * blend,
                state.currentY + (state.targetY - state.currentY) * blend,
                state.currentZ + (state.targetZ - state.currentZ) * blend
            )
            if currentX then
                state.currentX, state.currentY, state.currentZ = currentX, currentY, currentZ
                updatePointArm(player, state)
            end
        end
    end
end)

addEventHandler("onClientElementStreamOut", root, function()
    releasePointArm(pointing[source])
end)

addEventHandler("onClientElementDestroy", root, function()
    releasePointArm(pointing[source])
    pointing[source] = nil
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    bindKey("e", "both", handlePointingKey)
    triggerServerEvent("cameraPointing:requestState", resourceRoot)
    outputChatBox("[Pointing] Maintiens E pour pointer dans la direction de la camera.", 110, 220, 255)
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    unbindKey("e", "both", handlePointingKey)
    for _, state in pairs(pointing) do
        releasePointArm(state)
    end
end)
