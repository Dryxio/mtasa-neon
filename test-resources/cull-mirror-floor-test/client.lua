local test = MirrorFloorTest.custom
local mirrorZoneId
local mirrorEnabled = true
local debugEnabled = true
local currentMode

local function message(text, r, g, b)
    outputChatBox("[Mirror Floor] " .. text, r or 170, g or 225, b or 255)
end

local function createMirrorZone()
    mirrorZoneId = engineCreateCullZone(
        "mirror",
        test.x, test.y, (test.minZ + test.maxZ) * 0.5,
        test.width, test.depth, test.maxZ - test.minZ,
        0x1, 0,
        test.mirrorV, test.normalX, test.normalY, test.normalZ
    )
    if mirrorZoneId then
        message(("created floor mirror zone #%d (plane Z=%.2f)"):format(mirrorZoneId, test.mirrorV), 90, 255, 160)
    else
        message("failed to create the custom floor mirror zone", 255, 90, 90)
    end
end

local function pointInside(zone, x, y, z)
    local radians = math.rad(zone.rotation)
    local cosine, sine = math.cos(radians), math.sin(radians)
    local deltaX, deltaY = x - zone.x, y - zone.y
    local localX = deltaX * cosine + deltaY * sine
    local localY = -deltaX * sine + deltaY * cosine
    return math.abs(localX) < zone.width * 0.5 and math.abs(localY) < zone.depth * 0.5 and math.abs(z - zone.z) < zone.height * 0.5
end

local function nearestMirrorZone(x, y, z)
    local nearest
    local nearestDistance
    for _, zone in ipairs(engineGetCullZones("mirror")) do
        local dx, dy, dz = x - zone.x, y - zone.y, z - zone.z
        local distance = math.sqrt(dx * dx + dy * dy + dz * dz)
        if not nearestDistance or distance < nearestDistance then
            nearest = zone
            nearestDistance = distance
        end
    end
    return nearest, nearestDistance
end

local function drawBox()
    local halfWidth = test.width * 0.5
    local halfDepth = test.depth * 0.5
    local color = mirrorEnabled and tocolor(70, 200, 255, 190) or tocolor(150, 150, 150, 150)
    local corners = {
        { test.x - halfWidth, test.y - halfDepth },
        { test.x + halfWidth, test.y - halfDepth },
        { test.x + halfWidth, test.y + halfDepth },
        { test.x - halfWidth, test.y + halfDepth },
    }
    for index = 1, 4 do
        local nextIndex = index % 4 + 1
        dxDrawLine3D(corners[index][1], corners[index][2], test.minZ, corners[nextIndex][1], corners[nextIndex][2], test.minZ, color, 2, true)
        dxDrawLine3D(corners[index][1], corners[index][2], test.maxZ, corners[nextIndex][1], corners[nextIndex][2], test.maxZ, color, 2, true)
        dxDrawLine3D(corners[index][1], corners[index][2], test.minZ, corners[index][1], corners[index][2], test.maxZ, color, 2, true)
    end

    local planeColor = tocolor(255, 70, 220, 235)
    dxDrawLine3D(test.x - halfWidth, test.y - halfDepth, test.mirrorV, test.x + halfWidth, test.y - halfDepth, test.mirrorV, planeColor, 4, true)
    dxDrawLine3D(test.x + halfWidth, test.y - halfDepth, test.mirrorV, test.x + halfWidth, test.y + halfDepth, test.mirrorV, planeColor, 4, true)
    dxDrawLine3D(test.x + halfWidth, test.y + halfDepth, test.mirrorV, test.x - halfWidth, test.y + halfDepth, test.mirrorV, planeColor, 4, true)
    dxDrawLine3D(test.x - halfWidth, test.y + halfDepth, test.mirrorV, test.x - halfWidth, test.y - halfDepth, test.mirrorV, planeColor, 4, true)
    dxDrawLine3D(test.x - halfWidth, test.y - halfDepth, test.mirrorV, test.x + halfWidth, test.y + halfDepth, test.mirrorV, planeColor, 2, true)
    dxDrawLine3D(test.x - halfWidth, test.y + halfDepth, test.mirrorV, test.x + halfWidth, test.y - halfDepth, test.mirrorV, planeColor, 2, true)
end

addEventHandler("onClientRender", root, function()
    if not currentMode then
        return
    end

    local cameraX, cameraY, cameraZ = getCameraMatrix()
    local activeZone
    for _, zone in ipairs(engineGetCullZones("mirror")) do
        if zone.enabled and pointInside(zone, cameraX, cameraY, cameraZ) then
            activeZone = zone
            break
        end
    end

    local status = activeZone and ("ACTIVE ZONE #%d  %s"):format(activeZone.id, activeZone.original and "VANILLA" or "CUSTOM") or "NO MIRROR ZONE AT CAMERA"
    dxDrawRectangle(24, 24, 660, 68, tocolor(4, 8, 16, 220), true)
    dxDrawRectangle(24, 24, 5, 68, activeZone and tocolor(70, 255, 130) or tocolor(255, 90, 90), true)
    local text = ("MIRROR FLOOR CONTROL  |  %s\n%s\ncamera %.2f, %.2f, %.2f  |  /mirrorfloorinfo"):format(currentMode:upper(), status, cameraX, cameraY, cameraZ)
    dxDrawText(text, 40, 27, 674, 89, tocolor(238, 247, 255), 1, "default-bold", "left", "center", false, false, true)

    if debugEnabled and currentMode == "custom" then
        drawBox()
    end
end)

addCommandHandler("mirrorfloortoggle", function(_, state)
    if not mirrorZoneId then
        message("custom mirror zone was not created", 255, 90, 90)
        return
    end
    local enabled = state == "on" or (state ~= "off" and not mirrorEnabled)
    if engineSetCullZoneEnabled(mirrorZoneId, enabled) then
        mirrorEnabled = enabled
        message("custom mirror " .. (enabled and "enabled" or "disabled"), 90, 255, 160)
    end
end)

addCommandHandler("mirrorfloordebug", function(_, state)
    debugEnabled = state == "on" or (state ~= "off" and not debugEnabled)
    message("debug plane " .. (debugEnabled and "enabled" or "disabled"))
end)

addCommandHandler("mirrorfloorinfo", function()
    local cameraX, cameraY, cameraZ = getCameraMatrix()
    local zone, distance = nearestMirrorZone(cameraX, cameraY, cameraZ)
    if not zone then
        message("no mirror zones available", 255, 90, 90)
        return
    end
    message(("nearest #%d %s distance=%.2f enabled=%s flags=0x%X"):format(zone.id, zone.original and "VANILLA" or "CUSTOM", distance, tostring(zone.enabled), zone.flags))
    message(("center=%.2f %.2f %.2f size=%.2f %.2f %.2f"):format(zone.x, zone.y, zone.z, zone.width, zone.depth, zone.height))
    message(("plane: normal=(%.2f,%.2f,%.2f) V=%.3f"):format(zone.normalX, zone.normalY, zone.normalZ, zone.mirrorV))
end)

addCommandHandler("mirrorfloorhelp", function()
    message("/mirrorfloor - custom floor | /mirrorvanilla - stock barbershop")
    message("/mirrorfloortoggle [on|off] | /mirrorfloordebug [on|off]")
    message("/mirrorfloorinfo | /mirrorfloorleave")
end)

addEvent("mirrorFloorTestEntered", true)
addEventHandler("mirrorFloorTestEntered", resourceRoot, function(mode)
    currentMode = mode
    setCameraTarget(localPlayer)
    message(mode == "custom" and "Custom single-floor test active." or "Vanilla barbershop control active.", 90, 255, 160)
end)

addEvent("mirrorFloorTestLeft", true)
addEventHandler("mirrorFloorTestLeft", resourceRoot, function()
    currentMode = nil
    setCameraTarget(localPlayer)
end)

addEventHandler("onClientResourceStart", resourceRoot, createMirrorZone)
addEventHandler("onClientResourceStop", resourceRoot, function()
    if mirrorZoneId then
        engineRemoveCullZone(mirrorZoneId)
    end
end)
