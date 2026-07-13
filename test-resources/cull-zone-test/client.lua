local activeZone
local activeDefinition
local vanillaZone
local createdZones = {}
local visualizationMode = "off"
local visualizationRange = 250
local visualizationZones = {}
local visualizationRefreshTick = 0

local defaults = {
    attribute = 0x8,   -- NO_RAIN
    tunnel = 0x800,    -- TUNNEL
    mirror = 0x1       -- MIRROR
}

local attributeFlags = {
    { 0x1, "CAM_CLOSE_IN" },
    { 0x2, "STAIRS" },
    { 0x4, "1ST_PERSON" },
    { 0x8, "NO_RAIN" },
    { 0x10, "NO_POLICE" },
    { 0x40, "LOAD_COLLISION" },
    { 0x80, "TUNNEL_TRANSITION" },
    { 0x100, "POLICE_ABANDON_CARS" },
    { 0x200, "IN_ROOM_FOR_AUDIO" },
    { 0x400, "FEWER_PEDS" },
    { 0x800, "TUNNEL" },
    { 0x1000, "MILITARY_ZONE" },
    { 0x4000, "EXTRA_AIR_RESISTANCE" },
    { 0x8000, "FEWER_CARS" }
}

local mirrorFlags = {
    { 0x1, "MIRROR" },
    { 0x2, "SCREEN_1" },
    { 0x4, "SCREEN_2" },
    { 0x8, "SCREEN_3" },
    { 0x10, "SCREEN_4" },
    { 0x20, "SCREEN_5" },
    { 0x40, "SCREEN_6" }
}

local typeColors = {
    attribute = { 40, 190, 255 },
    tunnel = { 255, 170, 35 },
    mirror = { 225, 75, 255 }
}

local function message(text, r, g, b)
    outputChatBox("[CULL] " .. text, r or 210, g or 235, b or 255)
end

local function decodeFlags(zoneType, flags)
    local definitions = zoneType == "mirror" and mirrorFlags or attributeFlags
    local names = {}
    local known = 0
    for _, definition in ipairs(definitions) do
        if bitAnd(flags, definition[1]) ~= 0 then
            names[#names + 1] = definition[2]
            known = bitOr(known, definition[1])
        end
    end

    local unknown = flags - bitAnd(flags, known)
    if unknown ~= 0 then
        names[#names + 1] = ("UNKNOWN_0x%X"):format(unknown)
    end
    return #names > 0 and table.concat(names, " | ") or "NONE"
end

local function isPointInsideZone(zone, x, y, z)
    local radians = math.rad(zone.rotation)
    local cosine, sine = math.cos(radians), math.sin(radians)
    local deltaX, deltaY = x - zone.x, y - zone.y
    local localX = deltaX * cosine + deltaY * sine
    local localY = -deltaX * sine + deltaY * cosine
    return math.abs(localX) <= zone.width * 0.5 and math.abs(localY) <= zone.depth * 0.5 and math.abs(z - zone.z) <= zone.height * 0.5
end

local function getZoneCorners(zone)
    local radians = math.rad(zone.rotation)
    local cosine, sine = math.cos(radians), math.sin(radians)
    local widthX, widthY = cosine * zone.width * 0.5, sine * zone.width * 0.5
    local depthX, depthY = -sine * zone.depth * 0.5, cosine * zone.depth * 0.5
    local bottom, top = zone.z - zone.height * 0.5, zone.z + zone.height * 0.5
    local corners = {}
    local signs = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } }
    for index, sign in ipairs(signs) do
        local x = zone.x + sign[1] * widthX + sign[2] * depthX
        local y = zone.y + sign[1] * widthY + sign[2] * depthY
        corners[index] = { x, y, bottom }
        corners[index + 4] = { x, y, top }
    end
    return corners
end

local function drawZone(zone, inside, distance)
    local color = typeColors[zone.type]
    local red, green, blue = color[1], color[2], color[3]
    if not zone.enabled then
        red, green, blue = 145, 145, 145
    elseif inside then
        red, green, blue = 70, 255, 120
    elseif zone.original then
        red, green, blue = red * 0.72, green * 0.72, blue * 0.72
    end

    local alpha = zone.enabled and (zone.original and 185 or 245) or 150
    local lineColor = tocolor(red, green, blue, alpha)
    local lineWidth = inside and 4 or zone.original and 1.5 or 2.5
    local corners = getZoneCorners(zone)
    for index = 1, 4 do
        local nextIndex = index % 4 + 1
        dxDrawLine3D(corners[index][1], corners[index][2], corners[index][3], corners[nextIndex][1], corners[nextIndex][2], corners[nextIndex][3], lineColor, lineWidth)
        dxDrawLine3D(corners[index + 4][1], corners[index + 4][2], corners[index + 4][3], corners[nextIndex + 4][1], corners[nextIndex + 4][2], corners[nextIndex + 4][3], lineColor, lineWidth)
        dxDrawLine3D(corners[index][1], corners[index][2], corners[index][3], corners[index + 4][1], corners[index + 4][2], corners[index + 4][3], lineColor, lineWidth)
    end

    local screenX, screenY = getScreenFromWorldPosition(zone.x, zone.y, zone.z + zone.height * 0.5 + 1.5, 0.08)
    if not screenX or distance > math.min(visualizationRange, 350) then
        return
    end

    local state = inside and "  [ INSIDE ]" or not zone.enabled and "  [ DISABLED ]" or ""
    local origin = zone.original and "VANILLA" or "CUSTOM"
    local label = ("%s #%d  %s%s\n%s  (0x%X)\n%.0f x %.0f x %.0f m"):format(zone.type:upper(), zone.id, origin, state, decodeFlags(zone.type, zone.flags), zone.flags, zone.width, zone.depth, zone.height)
    local labelWidth, labelHeight = 330, 58
    dxDrawRectangle(screenX - labelWidth * 0.5, screenY - labelHeight * 0.5, labelWidth, labelHeight, tocolor(5, 10, 18, 205), true)
    dxDrawRectangle(screenX - labelWidth * 0.5, screenY - labelHeight * 0.5, 4, labelHeight, tocolor(red, green, blue, 255), true)
    dxDrawText(label, screenX - labelWidth * 0.5 + 10, screenY - labelHeight * 0.5, screenX + labelWidth * 0.5 - 6, screenY + labelHeight * 0.5, tocolor(240, 247, 255, 255), 1, "default-bold", "center", "center", false, false, true)
end

local function refreshVisualizationZones()
    visualizationZones = engineGetCullZones()
    visualizationRefreshTick = getTickCount()
end

addEventHandler("onClientRender", root, function()
    if visualizationMode == "off" then
        return
    end
    if getTickCount() - visualizationRefreshTick > 500 then
        refreshVisualizationZones()
    end

    local playerX, playerY, playerZ = getElementPosition(localPlayer)
    local rangeSquared = visualizationRange * visualizationRange
    local candidates = {}
    for _, zone in ipairs(visualizationZones) do
        local deltaX, deltaY, deltaZ = playerX - zone.x, playerY - zone.y, playerZ - zone.z
        local distanceSquared = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ
        if distanceSquared <= rangeSquared and (visualizationMode == "all" or not zone.original) then
            candidates[#candidates + 1] = { zone = zone, distance = math.sqrt(distanceSquared) }
        end
    end
    table.sort(candidates, function(left, right) return left.distance > right.distance end)

    local first = math.max(1, #candidates - 149)
    for index = first, #candidates do
        local candidate = candidates[index]
        drawZone(candidate.zone, isPointInsideZone(candidate.zone, playerX, playerY, playerZ), candidate.distance)
    end

    local legend = ("CULL ZONE VISUALIZER  |  %s  |  %.0f m  |  %d visible\nATTRIBUTE  cyan     TUNNEL  amber     MIRROR  magenta     INSIDE  green\n/cullvisual custom [range]     /cullvisual all [range]     /cullvisual off"):format(visualizationMode:upper(), visualizationRange, #candidates)
    dxDrawRectangle(24, 24, 700, 66, tocolor(4, 9, 16, 215), true)
    dxDrawRectangle(24, 24, 5, 66, tocolor(40, 190, 255, 255), true)
    dxDrawText(legend, 40, 27, 714, 87, tocolor(235, 244, 255, 255), 1, "default-bold", "left", "center", false, false, true)
end)

addCommandHandler("cullvisual", function(_, mode, rangeText)
    mode = mode and mode:lower() or (visualizationMode == "off" and "custom" or "off")
    if mode == "on" then
        mode = "custom"
    elseif mode == "nearby" then
        mode = "all"
    end
    if mode ~= "off" and mode ~= "custom" and mode ~= "all" then
        message("Usage: /cullvisual [custom|all|off] [range]", 255, 120, 100)
        return
    end

    visualizationMode = mode
    visualizationRange = math.max(25, math.min(2000, tonumber(rangeText) or visualizationRange))
    refreshVisualizationZones()
    message(("visualizer %s (range %.0f m)"):format(visualizationMode, visualizationRange), 100, 255, 160)
end)

local function zoneCounts()
    local counts = {
        attribute = { active = 0, original = 0, custom = 0 },
        tunnel = { active = 0, original = 0, custom = 0 },
        mirror = { active = 0, original = 0, custom = 0 }
    }

    for _, zone in ipairs(engineGetCullZones()) do
        local count = counts[zone.type]
        if zone.enabled then
            count.active = count.active + 1
        end
        if zone.original then
            count.original = count.original + 1
        else
            count.custom = count.custom + 1
        end
    end
    return counts
end

local function printStats()
    local counts = zoneCounts()
    for _, zoneType in ipairs({ "attribute", "tunnel", "mirror" }) do
        local count = counts[zoneType]
        message(('%s: active=%d original=%d custom=%d'):format(zoneType, count.active, count.original, count.custom))
    end
end

local function getDefinition(zoneType, flags, size)
    local x, y, z = getElementPosition(localPlayer)
    local definition = {
        type = zoneType,
        x = x,
        y = y,
        z = z,
        width = size,
        depth = size,
        height = 30,
        flags = flags,
        rotation = 0,
        mirrorV = x,
        normalX = 1,
        normalY = 0,
        normalZ = 0
    }
    return definition
end

local function createZone(definition)
    return engineCreateCullZone(
        definition.type,
        definition.x, definition.y, definition.z,
        definition.width, definition.depth, definition.height,
        definition.flags, definition.rotation,
        definition.mirrorV, definition.normalX, definition.normalY, definition.normalZ
    )
end

local function setZone(id, definition)
    return engineSetCullZone(
        id, definition.type,
        definition.x, definition.y, definition.z,
        definition.width, definition.depth, definition.height,
        definition.flags, definition.rotation,
        definition.mirrorV, definition.normalX, definition.normalY, definition.normalZ
    )
end

addCommandHandler("cullhelp", function()
    message("/cullstats")
    message("/culltest [attribute|tunnel|mirror] [flags] [size]")
    message("/culledit [flags] [size] - moves and edits the active custom zone")
    message("/cullenable [on|off], /culldelete, /cullclear")
    message("/cullboundary [attribute|tunnel|mirror] [count]")
    message("/cullvisual [custom|all|off] [range] - draw zones and decoded flags")
    message("/cullnearest [attribute|tunnel|mirror]")
    message("/cullvanilladisable, /cullvanillarestore")
end)

addCommandHandler("cullstats", printStats)

addCommandHandler("culltest", function(_, zoneType, flagsText, sizeText)
    zoneType = zoneType or "attribute"
    if not defaults[zoneType] then
        message("Type must be attribute, tunnel, or mirror", 255, 100, 100)
        return
    end

    local flags = tonumber(flagsText) or defaults[zoneType]
    local size = math.max(2, tonumber(sizeText) or 40)
    local definition = getDefinition(zoneType, flags, size)
    local id = createZone(definition)
    if not id then
        message("Creation failed", 255, 100, 100)
        return
    end

    activeZone = id
    activeDefinition = definition
    createdZones[id] = true
    message(("created %s zone id=%d flags=0x%X at %.1f %.1f %.1f"):format(zoneType, id, flags, definition.x, definition.y, definition.z), 100, 255, 140)
    if zoneType == "attribute" and bitAnd(flags, 0x8) ~= 0 then
        message("NO_RAIN is active inside the box; use /setweather 8 if rain is not currently visible")
    elseif zoneType == "mirror" then
        message("Mirror plane uses the current X coordinate and +X normal")
    end
end)

addCommandHandler("culledit", function(_, flagsText, sizeText)
    if not activeZone or not activeDefinition then
        message("Create a zone with /culltest first", 255, 100, 100)
        return
    end

    local updated = getDefinition(activeDefinition.type, tonumber(flagsText) or activeDefinition.flags, math.max(2, tonumber(sizeText) or activeDefinition.width))
    if setZone(activeZone, updated) then
        activeDefinition = updated
        message(("edited id=%d and moved it to your position"):format(activeZone), 100, 255, 140)
    else
        message("Edit failed", 255, 100, 100)
    end
end)

addCommandHandler("cullenable", function(_, state)
    if not activeZone then
        message("No active custom zone", 255, 100, 100)
        return
    end
    local enabled = state ~= "off"
    message(engineSetCullZoneEnabled(activeZone, enabled) and ("zone " .. (enabled and "enabled" or "disabled")) or "Enable change failed")
end)

addCommandHandler("culldelete", function()
    if not activeZone then
        message("No active custom zone", 255, 100, 100)
        return
    end
    if engineRemoveCullZone(activeZone) then
        createdZones[activeZone] = nil
        message(("deleted custom zone id=%d"):format(activeZone), 100, 255, 140)
        activeZone = nil
        activeDefinition = nil
    else
        message("Delete failed", 255, 100, 100)
    end
end)

addCommandHandler("cullclear", function()
    local removed = 0
    for id in pairs(createdZones) do
        if engineRemoveCullZone(id) then
            createdZones[id] = nil
            removed = removed + 1
        end
    end
    activeZone = nil
    activeDefinition = nil
    message(("removed %d custom zones"):format(removed), 100, 255, 140)
    printStats()
end)

addCommandHandler("cullboundary", function(_, zoneType, countText)
    zoneType = zoneType or "tunnel"
    local count = math.max(1, math.min(300, tonumber(countText) or (zoneType == "attribute" and 1301 or zoneType == "tunnel" and 41 or 73)))
    if not defaults[zoneType] then
        message("Type must be attribute, tunnel, or mirror", 255, 100, 100)
        return
    end

    local created = 0
    for i = 1, count do
        local definition = getDefinition(zoneType, defaults[zoneType], 10)
        definition.x = definition.x + (i % 20) * 12
        definition.y = definition.y + math.floor(i / 20) * 12
        definition.mirrorV = definition.x
        local id = createZone(definition)
        if not id then
            break
        end
        createdZones[id] = true
        created = created + 1
    end
    message(("boundary run created %d/%d %s zones"):format(created, count, zoneType), created == count and 100 or 255, created == count and 255 or 100, 140)
    printStats()
end)

addCommandHandler("cullnearest", function(_, zoneType)
    zoneType = zoneType or "attribute"
    if not defaults[zoneType] then
        message("Type must be attribute, tunnel, or mirror", 255, 100, 100)
        return
    end

    local x, y, z = getElementPosition(localPlayer)
    local bestDistance
    vanillaZone = nil
    for _, zone in ipairs(engineGetCullZones(zoneType)) do
        if zone.original then
            local distance = getDistanceBetweenPoints3D(x, y, z, zone.x, zone.y, zone.z)
            if not bestDistance or distance < bestDistance then
                bestDistance = distance
                vanillaZone = zone
            end
        end
    end

    if vanillaZone then
        message(("nearest vanilla %s id=%d distance=%.1f enabled=%s flags=0x%X"):format(zoneType, vanillaZone.id, bestDistance, tostring(vanillaZone.enabled), vanillaZone.flags))
    else
        message("No vanilla zone found", 255, 100, 100)
    end
end)

addCommandHandler("cullvanilladisable", function()
    if not vanillaZone then
        message("Select one with /cullnearest first", 255, 100, 100)
        return
    end
    message(engineSetCullZoneEnabled(vanillaZone.id, false) and ("disabled vanilla id=" .. vanillaZone.id) or "Disable failed")
end)

addCommandHandler("cullvanillarestore", function()
    if not vanillaZone then
        message("Select one with /cullnearest first", 255, 100, 100)
        return
    end
    message(engineRestoreCullZone(vanillaZone.id) and ("restored vanilla id=" .. vanillaZone.id) or "Restore failed")
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    -- The native manager also performs owner-based cleanup. Explicit deletion
    -- makes this resource useful for checking the public API itself.
    for id in pairs(createdZones) do
        engineRemoveCullZone(id)
    end
end)
