local cameraGhost = nil

local function findReplayGhost()
    local candidates = getElementsByType("ped", resourceRoot)
    for i = #candidates, 1, -1 do
        local ped = candidates[i]
        if isElement(ped) and ped ~= localPlayer and getElementAlpha(ped) == 190 then
            return ped
        end
    end
    return nil
end

local function followReplayGhost()
    setTimer(function()
        local ghost = findReplayGhost()
        if not isElement(ghost) then
            return
        end

        cameraGhost = ghost
        setCameraTarget(ghost)
    end, 50, 1)
end

local function restorePlayerCamera()
    if isElement(localPlayer) then
        setCameraTarget(localPlayer)
    end
    cameraGhost = nil
end

addCommandHandler("replayplay", followReplayGhost)
addCommandHandler("replayresume", followReplayGhost)

addCommandHandler("replaystop", function()
    setTimer(restorePlayerCamera, 0, 1)
end)

addCommandHandler("replayclear", function()
    setTimer(restorePlayerCamera, 0, 1)
end)

addEventHandler("onClientChatMessage", root, function(message)
    if type(message) ~= "string" then
        return
    end

    if message:find("%[REPLAY%] Playback complete%.") or message:find("%[REPLAY%] Playback stopped%.") then
        restorePlayerCamera()
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, restorePlayerCamera)
