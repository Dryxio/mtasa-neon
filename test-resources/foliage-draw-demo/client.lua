local surfaces = {3, 9, 10, 11}
local surfaceIndex = #surfaces
local density = 1.0
local points = {}
local foliage
local cursorWorld
local errorText
local cursorWasShowing = isCursorShowing()

local function currentSurface()
    return surfaces[surfaceIndex]
end

local function destroyFoliage()
    if isElement(foliage) then
        destroyElement(foliage)
    end
    foliage = nil
end

local function clearTriangle()
    destroyFoliage()
    points = {}
    cursorWorld = nil
    errorText = nil
    showCursor(true)
end

local function createTriangleFoliage()
    destroyFoliage()

    foliage = createFoliage(points[1], points[2], points[3], currentSurface(), density)
    if not isElement(foliage) then
        foliage = nil
        errorText = "creation failed"
        return
    end

    setElementDimension(foliage, getElementDimension(localPlayer))
    errorText = nil
end

local function setSurfaceIndex(index)
    local nextIndex = ((index - 1) % #surfaces) + 1
    local nextSurface = surfaces[nextIndex]

    if isElement(foliage) and not setFoliageSurface(foliage, nextSurface) then
        errorText = "surface change failed"
        return
    end

    surfaceIndex = nextIndex
    errorText = nil
end

local function setDensity(value)
    local nextDensity = math.max(0, math.min(10, value))

    if isElement(foliage) and not setFoliageDensity(foliage, nextDensity) then
        errorText = "density change failed"
        return
    end

    density = nextDensity
    errorText = nil
end

addEventHandler("onClientClick", root, function(button, state, _, _, worldX, worldY, worldZ)
    if state ~= "down" then
        return
    end

    if button == "right" then
        clearTriangle()
        return
    end

    if button ~= "left" or #points >= 3 or not worldX then
        return
    end

    points[#points + 1] = Vector3(worldX, worldY, worldZ + 0.06)
    if #points == 3 then
        createTriangleFoliage()
        cursorWorld = nil
        showCursor(false)
    end
end)

addEventHandler("onClientCursorMove", root, function(_, _, _, _, worldX, worldY, worldZ)
    if worldX then
        cursorWorld = Vector3(worldX, worldY, worldZ + 0.06)
    else
        cursorWorld = nil
    end
end)

bindKey("arrow_l", "down", function()
    setSurfaceIndex(surfaceIndex - 1)
end)

bindKey("arrow_r", "down", function()
    setSurfaceIndex(surfaceIndex + 1)
end)

bindKey("mouse_wheel_up", "down", function()
    setDensity(density + 0.25)
end)

bindKey("mouse_wheel_down", "down", function()
    setDensity(density - 0.25)
end)

local function drawPoint(point)
    local size = 0.18
    dxDrawLine3D(point.x - size, point.y, point.z + 0.04, point.x + size, point.y, point.z + 0.04, tocolor(255, 255, 255, 230), 3)
    dxDrawLine3D(point.x, point.y - size, point.z + 0.04, point.x, point.y + size, point.z + 0.04, tocolor(255, 255, 255, 230), 3)
end

local function drawLine(a, b)
    dxDrawLine3D(a.x, a.y, a.z + 0.04, b.x, b.y, b.z + 0.04, tocolor(255, 255, 255, 210), 2)
end

addEventHandler("onClientRender", root, function()
    for _, point in ipairs(points) do
        drawPoint(point)
    end

    if #points >= 2 then
        drawLine(points[1], points[2])
    end

    if #points == 3 then
        drawLine(points[2], points[3])
        drawLine(points[3], points[1])
    elseif cursorWorld and #points > 0 then
        drawLine(points[#points], cursorWorld)
        if #points == 2 then
            drawLine(cursorWorld, points[1])
        end
    end

    local status = string.format("surface %d    density %.2f", currentSurface(), density)
    dxDrawText(status, 24, 24, 600, 48, tocolor(255, 255, 255, 245), 1.1, "default-bold")
    dxDrawText("LMB: point    RMB: reset    arrows: surface    wheel: density", 24, 50, 760, 72, tocolor(225, 225, 225, 235), 1.0, "default")

    if errorText then
        dxDrawText(errorText, 24, 76, 500, 98, tocolor(255, 120, 120, 245), 1.0, "default-bold")
    end
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    showCursor(true)
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    destroyFoliage()
    if not cursorWasShowing then
        showCursor(false)
    end
end)
