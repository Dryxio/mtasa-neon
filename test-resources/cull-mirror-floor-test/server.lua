local testElements = {}
local playerReturnStates = {}

local function remember(element)
    if not element then
        return nil
    end
    testElements[#testElements + 1] = element
    setElementInterior(element, MirrorFloorTest.interior)
    setElementDimension(element, MirrorFloorTest.dimension)
    local elementType = getElementType(element)
    if elementType == "object" or elementType == "vehicle" then
        setElementFrozen(element, true)
    end
    if elementType == "object" then
        setObjectBreakable(element, false)
    end
    return element
end

local function buildPlatform()
    local test = MirrorFloorTest.custom
    for offsetY = -1, 1 do
        for offsetX = -1, 1 do
            remember(createObject(MirrorFloorTest.panelModel, test.x + offsetX * 10, test.y + offsetY * 10, test.floorZ - 1))
        end
    end

    local vehicle = remember(createVehicle(411, test.x + 6, test.y + 4, test.floorZ + 1, 0, 0, 145))
    if vehicle then
        setVehicleColor(vehicle, 255, 45, 60, 255, 45, 60)
    end

    local marker = remember(createMarker(test.x - 6, test.y + 5, test.floorZ + 0.1, "cylinder", 1.3, 60, 180, 255, 160))
    local pickup = remember(createPickup(test.x + 2, test.y - 6, test.floorZ + 0.5, 3, 1240))
    return marker and pickup
end

local function rememberPlayer(player)
    if playerReturnStates[player] then
        return
    end
    local x, y, z = getElementPosition(player)
    playerReturnStates[player] = {
        x = x,
        y = y,
        z = z,
        interior = getElementInterior(player),
        dimension = getElementDimension(player),
        frozen = isElementFrozen(player),
        mode = "custom",
    }
end

local function preparePlayer(player)
    if getPedOccupiedVehicle(player) then
        outputChatBox("[Mirror Floor] Leave your vehicle first.", player, 255, 120, 100)
        return false
    end
    rememberPlayer(player)
    setElementFrozen(player, false)
    return true
end

local function enterCustomFloor(player)
    if not preparePlayer(player) then
        return
    end
    local test = MirrorFloorTest.custom
    playerReturnStates[player].mode = "custom"
    setElementInterior(player, MirrorFloorTest.interior)
    setElementDimension(player, MirrorFloorTest.dimension)
    setElementPosition(player, test.x, test.y, test.floorZ + 1)
    setPedRotation(player, 180)
    triggerClientEvent(player, "mirrorFloorTestEntered", resourceRoot, "custom")
    outputChatBox(("[Mirror Floor] Custom floor: one zone, normal=(0,0,1), plane Z=%.2f."):format(test.mirrorV), player, 90, 220, 255)
end

local function enterVanillaBarber(player)
    if not preparePlayer(player) then
        return
    end
    local test = MirrorFloorTest.vanillaBarber
    playerReturnStates[player].mode = "vanilla"
    setElementInterior(player, test.interior)
    setElementDimension(player, test.dimension)
    setElementPosition(player, test.x, test.y, test.z)
    setPedRotation(player, test.rotation)
    triggerClientEvent(player, "mirrorFloorTestEntered", resourceRoot, "vanilla")
    outputChatBox("[Mirror Floor] Stock GTA barbershop mirror control. Use /mirrorfloorinfo.", player, 255, 205, 90)
end

local function leaveTest(player, quiet)
    local state = playerReturnStates[player]
    if not state then
        if not quiet then
            outputChatBox("[Mirror Floor] You are not in the test.", player, 255, 160, 90)
        end
        return
    end
    setElementInterior(player, state.interior)
    setElementDimension(player, state.dimension)
    setElementPosition(player, state.x, state.y, state.z)
    setElementFrozen(player, state.frozen)
    playerReturnStates[player] = nil
    triggerClientEvent(player, "mirrorFloorTestLeft", resourceRoot)
    if not quiet then
        outputChatBox("[Mirror Floor] Returned to your previous position.", player, 90, 220, 255)
    end
end

addCommandHandler("mirrorfloor", enterCustomFloor)
addCommandHandler("mirrorvanilla", enterVanillaBarber)
addCommandHandler("mirrorfloorleave", function(player) leaveTest(player, false) end)

addEventHandler("onPlayerQuit", root, function()
    playerReturnStates[source] = nil
end)

addEventHandler("onPlayerSpawn", root, function()
    local state = playerReturnStates[source]
    if state then
        setTimer(state.mode == "vanilla" and enterVanillaBarber or enterCustomFloor, 250, 1, source)
    end
end)

addEventHandler("onResourceStart", resourceRoot, function()
    buildPlatform()
    outputServerLog(("[cull-mirror-floor-test] Ready: %d elements, /mirrorfloor and /mirrorvanilla."):format(#testElements))
end)

addEventHandler("onResourceStop", resourceRoot, function()
    for player in pairs(playerReturnStates) do
        if isElement(player) then
            leaveTest(player, true)
        end
    end
end)
