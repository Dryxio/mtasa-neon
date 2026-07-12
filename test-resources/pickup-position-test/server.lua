local allowedXs = {
    [-9999] = true,
    [-8192] = true,
    [-4097] = true,
    [-4096] = true,
    [4095] = true,
    [4096] = true,
    [5000] = true,
    [8192] = true,
    [9500] = true,
    [9999] = true,
}

local testY = 0
local testZ = 100
local playerTests = {}

local function nativePickupCoordinate(value)
    return math.max(-32768, math.min(32767, value * 8)) / 8
end

local function destroyTestElements(test)
    if not test then
        return
    end

    if isElement(test.pickup) then
        destroyElement(test.pickup)
    end
    if isElement(test.marker) then
        destroyElement(test.marker)
    end
end

local function clearPlayerTest(player, restorePosition)
    local test = playerTests[player]
    if not test then
        triggerClientEvent(player, "pickupPositionResetCamera", resourceRoot)
        return
    end

    destroyTestElements(test)

    if restorePosition and isElement(player) then
        setElementInterior(player, test.interior)
        setElementDimension(player, test.dimension)
        setElementPosition(player, test.position[1], test.position[2], test.position[3])
        setElementFrozen(player, test.frozen)
    end

    playerTests[player] = nil
    if isElement(player) then
        triggerClientEvent(player, "pickupPositionResetCamera", resourceRoot)
    end
end

local function runPickupPositionTest(player, _, requestedX)
    local x = tonumber(requestedX) or 4096
    if not allowedXs[x] then
        outputChatBox("[Pickup position] Values: -9999, -8192, -4097, -4096, 4095, 4096, 5000, 8192, 9500, 9999.", player, 255, 180, 80)
        return
    end

    local previous = playerTests[player]
    local returnPosition
    local returnInterior
    local returnDimension
    local returnFrozen
    if previous then
        returnPosition = previous.position
        returnInterior = previous.interior
        returnDimension = previous.dimension
        returnFrozen = previous.frozen
        destroyTestElements(previous)
    else
        local px, py, pz = getElementPosition(player)
        returnPosition = { px, py, pz }
        returnInterior = getElementInterior(player)
        returnDimension = getElementDimension(player)
        returnFrozen = isElementFrozen(player)
    end

    setElementInterior(player, 0)
    setElementDimension(player, 0)
    setElementPosition(player, x, testY - 6, testZ)
    setElementFrozen(player, true)

    local pickup = createPickup(x, testY, testZ, 3, 1240)
    local marker = createMarker(x, testY, testZ - 1.25, "cylinder", 1.5, 255, 40, 40, 140)
    if not pickup or not marker then
        if isElement(pickup) then
            destroyElement(pickup)
        end
        if isElement(marker) then
            destroyElement(marker)
        end
        setElementPosition(player, returnPosition[1], returnPosition[2], returnPosition[3])
        setElementInterior(player, returnInterior)
        setElementDimension(player, returnDimension)
        setElementFrozen(player, returnFrozen)
        outputChatBox("[Pickup position] Failed to create the test elements.", player, 255, 80, 80)
        return
    end

    local nativeX = nativePickupCoordinate(x)
    playerTests[player] = {
        pickup = pickup,
        marker = marker,
        requestedX = x,
        nativeX = nativeX,
        position = returnPosition,
        interior = returnInterior,
        dimension = returnDimension,
        frozen = returnFrozen,
    }

    outputChatBox(("[Pickup position] MTA x=%.3f; GTA saturated placeholder x=%.3f."):format(x, nativeX), player, 80, 220, 255)
    outputChatBox("[Pickup position] Red cylinder = requested position. /pickupposnative checks that no visual remains at the placeholder.", player, 255, 220, 100)
    triggerClientEvent(player, "pickupPositionRunTest", resourceRoot, pickup, x, testY, testZ, nativeX)
end

local function showNativePosition(player)
    local test = playerTests[player]
    if not test then
        outputChatBox("[Pickup position] Run /pickuppostest [x] first.", player, 255, 180, 80)
        return
    end

    triggerClientEvent(player, "pickupPositionShowNative", resourceRoot, test.nativeX, testY, testZ)
end

addCommandHandler("pickuppostest", runPickupPositionTest)
addCommandHandler("pickupposnative", showNativePosition)
addCommandHandler("pickupposwrapped", showNativePosition)
addCommandHandler("pickupposmove", function(player, _, requestedX)
    local test = playerTests[player]
    local x = tonumber(requestedX)
    if not test or not x or not allowedXs[x] then
        outputChatBox("[Pickup position] Run /pickuppostest first, then use a listed boundary X.", player, 255, 180, 80)
        return
    end

    setElementPosition(test.pickup, x, testY, testZ)
    setElementPosition(test.marker, x, testY, testZ - 1.25)
    setElementPosition(player, x, testY - 6, testZ)
    test.requestedX = x
    test.nativeX = nativePickupCoordinate(x)
    triggerClientEvent(player, "pickupPositionRunTest", resourceRoot, test.pickup, x, testY, testZ, test.nativeX)
end)
addCommandHandler("pickupposrecreate", function(player)
    local test = playerTests[player]
    if not test then
        outputChatBox("[Pickup position] Run /pickuppostest first.", player, 255, 180, 80)
        return
    end
    runPickupPositionTest(player, "pickuppostest", test.requestedX)
end)
addCommandHandler("pickupposstream", function(player)
    local test = playerTests[player]
    if not test then
        outputChatBox("[Pickup position] Run /pickuppostest first.", player, 255, 180, 80)
        return
    end

    local x = test.requestedX
    setElementPosition(player, x + (x > 9500 and -500 or 500), testY - 6, testZ)
    triggerClientEvent(player, "pickupPositionStreamPhase", resourceRoot, false, x, testY, testZ)
    setTimer(function()
        if isElement(player) and playerTests[player] == test then
            setElementPosition(player, x, testY - 6, testZ)
            triggerClientEvent(player, "pickupPositionStreamPhase", resourceRoot, true, x, testY, testZ)
        end
    end, 2500, 1)
end)
addCommandHandler("pickupposcontext", function(player, _, requestedDimension, requestedInterior)
    local test = playerTests[player]
    local dimension = tonumber(requestedDimension) or 123
    local interior = tonumber(requestedInterior) or 0
    if not test or dimension < 0 or dimension > 65535 or interior < 0 or interior > 255 then
        outputChatBox("[Pickup position] Usage after a test: /pickupposcontext [0-65535] [0-255].", player, 255, 180, 80)
        return
    end

    setElementDimension(test.pickup, dimension)
    setElementDimension(test.marker, dimension)
    setElementDimension(player, dimension)
    setElementInterior(test.pickup, interior)
    setElementInterior(test.marker, interior)
    setElementInterior(player, interior)
    outputChatBox(("[Pickup position] Context set to dimension %d, interior %d."):format(dimension, interior), player, 100, 220, 255)
end)
addCommandHandler("pickupposwalk", function(player)
    local test = playerTests[player]
    if not test then
        outputChatBox("[Pickup position] Run /pickuppostest first.", player, 255, 180, 80)
        return
    end

    setElementPosition(player, test.requestedX, testY - 3, testZ)
    setElementFrozen(player, false)
    triggerClientEvent(player, "pickupPositionWalk", resourceRoot)
end)
addCommandHandler("pickupposback", function(player)
    clearPlayerTest(player, true)
end)

addEventHandler("onResourceStart", resourceRoot, function()
    outputServerLog("[Pickup position] Ready: boundary, move, recreate, stream, context, native-placeholder, and cleanup tests")
end)

addEventHandler("onPickupHit", root, function(player, matchingDimension)
    local test = playerTests[player]
    if test and source == test.pickup then
        outputChatBox(("[Pickup position] onPickupHit matchingDimension=%s"):format(tostring(matchingDimension)), player, 100, 255, 140)
    end
end)

addEventHandler("onPickupLeave", root, function(player, matchingDimension)
    local test = playerTests[player]
    if test and source == test.pickup then
        outputChatBox(("[Pickup position] onPickupLeave matchingDimension=%s"):format(tostring(matchingDimension)), player, 255, 220, 100)
    end
end)

addEventHandler("onPlayerQuit", root, function()
    local test = playerTests[source]
    destroyTestElements(test)
    playerTests[source] = nil
end)

addEventHandler("onResourceStop", resourceRoot, function()
    for player, test in pairs(playerTests) do
        destroyTestElements(test)
        if isElement(player) then
            setElementInterior(player, test.interior)
            setElementDimension(player, test.dimension)
            setElementPosition(player, test.position[1], test.position[2], test.position[3])
            setElementFrozen(player, test.frozen)
        end
    end
end)
