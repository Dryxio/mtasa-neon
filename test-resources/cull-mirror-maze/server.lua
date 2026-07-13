local mazeElements = {}
local playerReturnStates = {}
local passages = MirrorMaze.generatePassages()
local goalMarker

local function remember(element)
    if not element then
        return nil
    end
    mazeElements[#mazeElements + 1] = element
    setElementInterior(element, MirrorMaze.interior)
    setElementDimension(element, MirrorMaze.dimension)
    if getElementType(element) == "object" then
        setElementFrozen(element, true)
        setObjectBreakable(element, false)
    end
    return element
end

local function createPanel(x, y, z, rotationX, rotationY, rotationZ)
    return remember(createObject(MirrorMaze.panelModel, x, y, z, rotationX or 0, rotationY or 0, rotationZ or 0))
end

local function buildMaze()
    for y = 0, MirrorMaze.gridSize - 1 do
        for x = 0, MirrorMaze.gridSize - 1 do
            local centerX, centerY = MirrorMaze.cellCenter(x, y)
            createPanel(centerX, centerY, MirrorMaze.floorZ - 1)
            createPanel(centerX, centerY, MirrorMaze.floorZ + MirrorMaze.roomHeight + 1)
        end
    end

    for lineX = 0, MirrorMaze.gridSize do
        for y = 0, MirrorMaze.gridSize - 1 do
            local closed = lineX == 0 or lineX == MirrorMaze.gridSize or not MirrorMaze.hasPassage(passages, lineX - 1, y, lineX, y)
            if closed then
                local wallX = MirrorMaze.minX + lineX * MirrorMaze.cellSize
                local _, wallY = MirrorMaze.cellCenter(0, y)
                createPanel(wallX, wallY, MirrorMaze.floorZ + MirrorMaze.roomHeight * 0.5, 0, 90, 0)
            end
        end
    end

    for lineY = 0, MirrorMaze.gridSize do
        for x = 0, MirrorMaze.gridSize - 1 do
            local entrance = lineY == 0 and x == 0
            local exit = lineY == MirrorMaze.gridSize and x == MirrorMaze.gridSize - 1
            local closed = lineY == 0 or lineY == MirrorMaze.gridSize or not MirrorMaze.hasPassage(passages, x, lineY - 1, x, lineY)
            if closed and not entrance and not exit then
                local wallX = MirrorMaze.cellCenter(x, 0)
                local wallY = MirrorMaze.minY + lineY * MirrorMaze.cellSize
                createPanel(wallX, wallY, MirrorMaze.floorZ + MirrorMaze.roomHeight * 0.5, 90, 0, 0)
            end
        end
    end

    local goalX, goalY = MirrorMaze.cellCenter(MirrorMaze.gridSize - 1, MirrorMaze.gridSize - 1)
    goalMarker = remember(createMarker(goalX, goalY, MirrorMaze.floorZ + 0.1, "cylinder", 1.4, 70, 255, 150, 150))
    if goalMarker then
        addEventHandler("onMarkerHit", goalMarker, function(hitElement, matchingDimension)
            if matchingDimension and getElementType(hitElement) == "player" then
                outputChatBox("[Mirror Maze] You escaped! :mreow:", hitElement, 80, 255, 150)
            end
        end)
    end
end

local function placeAtStart(player)
    local startX, startY = MirrorMaze.cellCenter(0, 0)
    setElementInterior(player, MirrorMaze.interior)
    setElementDimension(player, MirrorMaze.dimension)
    setElementPosition(player, startX, startY, MirrorMaze.floorZ + 1)
    setPedRotation(player, 0)
    setElementFrozen(player, false)
    triggerClientEvent(player, "mirrorMazeEntered", resourceRoot)
end

local function enterMaze(player)
    if getPedOccupiedVehicle(player) then
        outputChatBox("[Mirror Maze] Leave your vehicle first.", player, 255, 120, 100)
        return
    end

    if not playerReturnStates[player] then
        local x, y, z = getElementPosition(player)
        playerReturnStates[player] = {
            x = x,
            y = y,
            z = z,
            interior = getElementInterior(player),
            dimension = getElementDimension(player),
            frozen = isElementFrozen(player),
        }
    end

    placeAtStart(player)
    outputChatBox("[Mirror Maze] Find the green exit. /mirrorreset or /mirrorleave", player, 90, 220, 255)
end

local function leaveMaze(player, quiet)
    local state = playerReturnStates[player]
    if not state then
        if not quiet then
            outputChatBox("[Mirror Maze] You are not inside the maze.", player, 255, 170, 90)
        end
        return
    end

    setElementInterior(player, state.interior)
    setElementDimension(player, state.dimension)
    setElementPosition(player, state.x, state.y, state.z)
    setElementFrozen(player, state.frozen)
    playerReturnStates[player] = nil
    triggerClientEvent(player, "mirrorMazeLeft", resourceRoot)
    if not quiet then
        outputChatBox("[Mirror Maze] Returned to your previous position.", player, 90, 220, 255)
    end
end

addCommandHandler("mirrormaze", enterMaze)

addCommandHandler("mirrorreset", function(player)
    if not playerReturnStates[player] then
        enterMaze(player)
        return
    end
    placeAtStart(player)
end)

addCommandHandler("mirrorleave", function(player)
    leaveMaze(player, false)
end)

addEventHandler("onPlayerQuit", root, function()
    playerReturnStates[source] = nil
end)

addEventHandler("onPlayerSpawn", root, function()
    if playerReturnStates[source] then
        setTimer(placeAtStart, 250, 1, source)
    end
end)

addEventHandler("onResourceStart", resourceRoot, function()
    buildMaze()
    outputServerLog(("[cull-mirror-maze] Ready: %dx%d maze, %d world elements. Use /mirrormaze."):format(MirrorMaze.gridSize, MirrorMaze.gridSize, #mazeElements))
end)

addEventHandler("onResourceStop", resourceRoot, function()
    for player in pairs(playerReturnStates) do
        if isElement(player) then
            leaveMaze(player, true)
        end
    end
end)
