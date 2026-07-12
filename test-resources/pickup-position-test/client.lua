local currentPickup
local requestedPosition
local nativeStoredX

local function report(message, r, g, b)
    outputChatBox("[Pickup position] " .. message, r or 220, g or 220, b or 220)
    outputConsole("[Pickup position] " .. message)
end

local function pointCameraAt(x, y, z)
    setCameraMatrix(x + 8, y - 12, z + 4, x, y, z)
end

addEvent("pickupPositionRunTest", true)
addEventHandler("pickupPositionRunTest", resourceRoot, function(pickup, x, y, z, storedX)
    currentPickup = pickup
    requestedPosition = { x, y, z }
    nativeStoredX = storedX
    pointCameraAt(x, y, z)

    setTimer(function()
        if not isElement(currentPickup) then
            report("pickup element was not synchronized", 255, 80, 80)
            return
        end

        local px, py, pz = getElementPosition(currentPickup)
        report(("element position %.3f, %.3f, %.3f; streamed=%s"):format(px, py, pz, tostring(isElementStreamedIn(currentPickup))), 100, 220, 255)
        if math.abs(nativeStoredX - requestedPosition[1]) < 0.001 then
            report("inside GTA native range: pickup should be visible in the red cylinder", 100, 255, 140)
        else
            report(("extended position: visual object must be in the red cylinder; native placeholder x=%.3f"):format(nativeStoredX), 100, 255, 140)
        end
    end, 1500, 1)
end)

addEvent("pickupPositionShowNative", true)
addEventHandler("pickupPositionShowNative", resourceRoot, function(x, y, z)
    pointCameraAt(x, y, z)
    report(("camera moved to GTA's saturated placeholder x=%.3f; the visual must not be here"):format(x), 255, 220, 100)
end)

addEvent("pickupPositionStreamPhase", true)
addEventHandler("pickupPositionStreamPhase", resourceRoot, function(atPickup, x, y, z)
    if atPickup then
        pointCameraAt(x, y, z)
    else
        setCameraTarget(localPlayer)
    end

    setTimer(function()
        report(("stream cycle %s: element=%s streamed=%s"):format(atPickup and "returned" or "away", tostring(isElement(currentPickup)),
            tostring(isElement(currentPickup) and isElementStreamedIn(currentPickup))), 120, 220, 255)
    end, 750, 1)
end)

addEvent("pickupPositionWalk", true)
addEventHandler("pickupPositionWalk", resourceRoot, function()
    setCameraTarget(localPlayer)
    report("walk through the pickup to validate hit/leave, then use /pickupposback", 100, 255, 140)
end)

addEvent("pickupPositionResetCamera", true)
addEventHandler("pickupPositionResetCamera", resourceRoot, function()
    currentPickup = nil
    requestedPosition = nil
    nativeStoredX = nil
    setCameraTarget(localPlayer)
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    report("/pickuppostest [x], /pickupposmove [x], /pickupposrecreate, /pickupposstream")
    report("/pickupposcontext [dimension] [interior], /pickupposwalk, /pickupposnative, /pickupposback")
    report("recommended boundary sequence: 4095, 4096, 5000, 8192, 9500, 9999")
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    setCameraTarget(localPlayer)
end)
