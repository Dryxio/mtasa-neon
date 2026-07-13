local passages = MirrorMaze.generatePassages()
local zones = {}
local debugEnabled = false
local mirrorsEnabled = true
local creationFailures = 0

local function message(text, r, g, b)
    outputChatBox("[Mirror Maze] " .. text, r or 160, g or 225, b or 255)
end

local function createMirrorZones()
    if type(engineCreateCullZone) ~= "function" then
        message("This client build does not expose the CULL zone API.", 255, 90, 90)
        return
    end

    for y = 0, MirrorMaze.gridSize - 1 do
        for x = 0, MirrorMaze.gridSize - 1 do
            local centerX, centerY = MirrorMaze.cellCenter(x, y)
            local wall = MirrorMaze.chooseMirrorWall(passages, x, y)
            local id = engineCreateCullZone(
                "mirror",
                centerX, centerY, MirrorMaze.floorZ + MirrorMaze.roomHeight * 0.5,
                MirrorMaze.cellSize - 0.2, MirrorMaze.cellSize - 0.2, MirrorMaze.roomHeight + 4,
                0x1, 0,
                wall.value, wall.normalX, wall.normalY, wall.normalZ
            )

            if id then
                zones[#zones + 1] = {
                    id = id,
                    x = x,
                    y = y,
                    centerX = centerX,
                    centerY = centerY,
                    wall = wall,
                }
            else
                creationFailures = creationFailures + 1
            end
        end
    end

    message(("created %d/%d native mirror zones"):format(#zones, MirrorMaze.gridSize * MirrorMaze.gridSize), creationFailures == 0 and 90 or 255, creationFailures == 0 and 255 or 120, 160)
end

local function removeMirrorZones()
    if type(engineRemoveCullZone) ~= "function" then
        return
    end
    for _, zone in ipairs(zones) do
        engineRemoveCullZone(zone.id)
    end
    zones = {}
end

local function setMirrorsEnabled(enabled)
    local changed = 0
    for _, zone in ipairs(zones) do
        if engineSetCullZoneEnabled(zone.id, enabled) then
            changed = changed + 1
        end
    end
    mirrorsEnabled = enabled
    message(("%s %d/%d mirror zones"):format(enabled and "enabled" or "disabled", changed, #zones), 100, 255, 170)
end

local function drawWorldLine(x1, y1, z1, x2, y2, z2, color, width)
    dxDrawLine3D(x1, y1, z1, x2, y2, z2, color, width or 2, true)
end

local function drawZone(zone, active)
    local half = (MirrorMaze.cellSize - 0.2) * 0.5
    local bottom = MirrorMaze.floorZ - 1
    local top = MirrorMaze.floorZ + MirrorMaze.roomHeight + 1
    local color = active and tocolor(80, 255, 140, 245) or tocolor(80, 180, 255, 150)
    local corners = {
        { zone.centerX - half, zone.centerY - half },
        { zone.centerX + half, zone.centerY - half },
        { zone.centerX + half, zone.centerY + half },
        { zone.centerX - half, zone.centerY + half },
    }

    for index = 1, 4 do
        local nextIndex = index % 4 + 1
        drawWorldLine(corners[index][1], corners[index][2], bottom, corners[nextIndex][1], corners[nextIndex][2], bottom, color, active and 3 or 1)
        drawWorldLine(corners[index][1], corners[index][2], top, corners[nextIndex][1], corners[nextIndex][2], top, color, active and 3 or 1)
        drawWorldLine(corners[index][1], corners[index][2], bottom, corners[index][1], corners[index][2], top, color, active and 3 or 1)
    end

    local planeColor = tocolor(255, 90, 230, active and 255 or 190)
    local planeHalf = MirrorMaze.cellSize * 0.42
    if zone.wall.axis == "x" then
        drawWorldLine(zone.wall.value, zone.centerY - planeHalf, bottom, zone.wall.value, zone.centerY + planeHalf, bottom, planeColor, 4)
        drawWorldLine(zone.wall.value, zone.centerY - planeHalf, top, zone.wall.value, zone.centerY + planeHalf, top, planeColor, 4)
        drawWorldLine(zone.wall.value, zone.centerY - planeHalf, bottom, zone.wall.value, zone.centerY - planeHalf, top, planeColor, 4)
        drawWorldLine(zone.wall.value, zone.centerY + planeHalf, bottom, zone.wall.value, zone.centerY + planeHalf, top, planeColor, 4)
    else
        drawWorldLine(zone.centerX - planeHalf, zone.wall.value, bottom, zone.centerX + planeHalf, zone.wall.value, bottom, planeColor, 4)
        drawWorldLine(zone.centerX - planeHalf, zone.wall.value, top, zone.centerX + planeHalf, zone.wall.value, top, planeColor, 4)
        drawWorldLine(zone.centerX - planeHalf, zone.wall.value, bottom, zone.centerX - planeHalf, zone.wall.value, top, planeColor, 4)
        drawWorldLine(zone.centerX + planeHalf, zone.wall.value, bottom, zone.centerX + planeHalf, zone.wall.value, top, planeColor, 4)
    end
end

local function drawMiniMap(activeX, activeY)
    local screenWidth = guiGetScreenSize()
    local mapSize = 175
    local originX = screenWidth - mapSize - 28
    local originY = 28
    local cellPixels = mapSize / MirrorMaze.gridSize

    dxDrawRectangle(originX - 10, originY - 28, mapSize + 20, mapSize + 48, tocolor(4, 8, 16, 220), true)
    dxDrawText("MIRROR MAZE DEBUG", originX, originY - 25, originX + mapSize, originY - 4, tocolor(225, 240, 255), 1, "default-bold", "center", "center", false, false, true)

    for y = 0, MirrorMaze.gridSize - 1 do
        for x = 0, MirrorMaze.gridSize - 1 do
            local left = originX + x * cellPixels
            local top = originY + (MirrorMaze.gridSize - 1 - y) * cellPixels
            if x == activeX and y == activeY then
                dxDrawRectangle(left + 2, top + 2, cellPixels - 4, cellPixels - 4, tocolor(60, 255, 130, 130), true)
            end
            local wall = MirrorMaze.chooseMirrorWall(passages, x, y)
            local label = wall.axis:upper()
            dxDrawText(label, left, top, left + cellPixels, top + cellPixels, tocolor(255, 100, 230, 210), 0.85, "default-bold", "center", "center", false, false, true)
        end
    end

    local wallColor = tocolor(225, 235, 255, 240)
    for lineX = 0, MirrorMaze.gridSize do
        for y = 0, MirrorMaze.gridSize - 1 do
            local closed = lineX == 0 or lineX == MirrorMaze.gridSize or not MirrorMaze.hasPassage(passages, lineX - 1, y, lineX, y)
            if closed then
                local x = originX + lineX * cellPixels
                local y1 = originY + (MirrorMaze.gridSize - 1 - y) * cellPixels
                dxDrawLine(x, y1, x, y1 + cellPixels, wallColor, 2, true)
            end
        end
    end

    for lineY = 0, MirrorMaze.gridSize do
        for x = 0, MirrorMaze.gridSize - 1 do
            local entrance = lineY == 0 and x == 0
            local exit = lineY == MirrorMaze.gridSize and x == MirrorMaze.gridSize - 1
            local closed = lineY == 0 or lineY == MirrorMaze.gridSize or not MirrorMaze.hasPassage(passages, x, lineY - 1, x, lineY)
            if closed and not entrance and not exit then
                local x1 = originX + x * cellPixels
                local y = originY + (MirrorMaze.gridSize - lineY) * cellPixels
                dxDrawLine(x1, y, x1 + cellPixels, y, wallColor, 2, true)
            end
        end
    end

    dxDrawText("X/Y = reflection-plane axis", originX, originY + mapSize + 4, originX + mapSize, originY + mapSize + 20, tocolor(190, 210, 230), 0.85, "default", "center", "center", false, false, true)
end

addEventHandler("onClientRender", root, function()
    if getElementDimension(localPlayer) ~= MirrorMaze.dimension then
        return
    end

    local cameraX, cameraY = getCameraMatrix()
    local activeX, activeY = MirrorMaze.cellFromPoint(cameraX, cameraY)
    local activeZone
    if activeX then
        local activeIndex = MirrorMaze.cellIndex(activeX, activeY)
        activeZone = zones[activeIndex]
    end

    dxDrawRectangle(24, 24, 590, 58, tocolor(4, 8, 16, 215), true)
    dxDrawRectangle(24, 24, 5, 58, mirrorsEnabled and tocolor(70, 255, 140) or tocolor(255, 100, 90), true)
    local activeText = activeZone and ("cell %d,%d  zone #%d  plane %s=%.1f"):format(activeZone.x, activeZone.y, activeZone.id, activeZone.wall.axis:upper(), activeZone.wall.value) or "camera outside mirror cells"
    local hud = ("NATIVE MIRROR MAZE  |  %s\n%s  |  /mirrorhelp"):format(mirrorsEnabled and "MIRRORS ON" or "MIRRORS OFF", activeText)
    dxDrawText(hud, 40, 27, 605, 79, tocolor(235, 245, 255), 1, "default-bold", "left", "center", false, false, true)

    if not debugEnabled then
        return
    end

    for _, zone in ipairs(zones) do
        drawZone(zone, zone == activeZone)
    end
    drawMiniMap(activeX, activeY)
end)

addCommandHandler("mirrordebug", function(_, state)
    debugEnabled = state == "on" or (state ~= "off" and not debugEnabled)
    message("debug overlay " .. (debugEnabled and "enabled" or "disabled"), 100, 255, 170)
end)

addCommandHandler("mirrormirrors", function(_, state)
    local enabled = state == "on" or (state ~= "off" and not mirrorsEnabled)
    setMirrorsEnabled(enabled)
end)

addCommandHandler("mirrorstats", function()
    message(("zones=%d failures=%d enabled=%s dimension=%d"):format(#zones, creationFailures, tostring(mirrorsEnabled), MirrorMaze.dimension))
end)

addCommandHandler("mirrorhelp", function()
    message("/mirrormaze - enter the maze | /mirrorleave - return")
    message("/mirrorreset - restart | /mirrordebug [on|off]")
    message("/mirrormirrors [on|off] | /mirrorstats")
end)

addEvent("mirrorMazeEntered", true)
addEventHandler("mirrorMazeEntered", resourceRoot, function()
    setCameraTarget(localPlayer)
    message("Native mirror maze entered. Find the green exit; /mirrordebug shows the real zones.", 90, 255, 170)
end)

addEvent("mirrorMazeLeft", true)
addEventHandler("mirrorMazeLeft", resourceRoot, function()
    setCameraTarget(localPlayer)
end)

addEventHandler("onClientResourceStart", resourceRoot, createMirrorZones)
addEventHandler("onClientResourceStop", resourceRoot, removeMirrorZones)
