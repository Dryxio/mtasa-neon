local state = {
    pass = 0,
    fail = 0,
    warn = 0,
    logs = {},
    surfaces = {},
    surfaceSet = {},
    overlay = true,
    demo = {},
    demoMode = nil,
    demoCenter = nil,
    cinematic = false,
    cinematicTick = 0,
    video = false,
    lifetime = nil,
}

local function pushLog(level, text)
    local line = string.format("[%s] %s", level, text)
    table.insert(state.logs, 1, line)
    while #state.logs > 9 do
        table.remove(state.logs)
    end

    local r, g, b = 210, 210, 210
    if level == "PASS" then
        r, g, b = 90, 220, 120
    elseif level == "FAIL" then
        r, g, b = 255, 90, 90
    elseif level == "WARN" then
        r, g, b = 255, 190, 80
    end

    outputDebugString("[foliage-test] " .. line)
    outputChatBox("[foliage-test] " .. line, r, g, b)
end

local function check(name, condition, detail)
    if condition then
        state.pass = state.pass + 1
        pushLog("PASS", name)
        return true
    end

    state.fail = state.fail + 1
    pushLog("FAIL", name .. (detail and (" - " .. detail) or ""))
    return false
end

local function warning(text)
    state.warn = state.warn + 1
    pushLog("WARN", text)
end

local function nearlyEqual(a, b, epsilon)
    return type(a) == "number" and type(b) == "number" and math.abs(a - b) <= (epsilon or 0.001)
end

local function vectorEqual(a, b, epsilon)
    return a and b and nearlyEqual(a.x, b.x, epsilon) and nearlyEqual(a.y, b.y, epsilon) and nearlyEqual(a.z, b.z, epsilon)
end

local function surfaceListText()
    local result = {}
    for i, surface in ipairs(state.surfaces) do
        result[i] = tostring(surface)
    end
    return #result > 0 and table.concat(result, ", ") or "none"
end

local function playerOrigin()
    local x, y, z = getElementPosition(localPlayer)
    return x, y, z
end

local function groundZ(x, y, fallbackZ)
    local z = getGroundPosition(x, y, fallbackZ + 100.0)
    if type(z) ~= "number" then
        return fallbackZ
    end
    return z + 0.06
end

local function makeTriangle(cx, cy, size, fallbackZ)
    local half = size * 0.5
    local x1, y1 = cx - half, cy - half
    local x2, y2 = cx + half, cy - half
    local x3, y3 = cx, cy + half

    return Vector3(x1, y1, groundZ(x1, y1, fallbackZ)),
           Vector3(x2, y2, groundZ(x2, y2, fallbackZ)),
           Vector3(x3, y3, groundZ(x3, y3, fallbackZ))
end

local function destroyList(elements)
    for i = #elements, 1, -1 do
        if isElement(elements[i]) then
            destroyElement(elements[i])
        end
        elements[i] = nil
    end
end

local function stopCinematic()
    if state.cinematic then
        state.cinematic = false
        setCameraTarget(localPlayer)
    end
end

local function clearDemo()
    destroyList(state.demo)
    state.demoMode = nil
    state.demoCenter = nil
    state.video = false
    stopCinematic()
end

local function clearLifetime()
    if isElement(state.lifetime) then
        destroyElement(state.lifetime)
    end
    state.lifetime = nil
end

local function createAt(cx, cy, size, surface, density)
    local _, _, pz = playerOrigin()
    local v1, v2, v3 = makeTriangle(cx, cy, size, pz)
    local foliage = createFoliage(v1, v2, v3, surface, density)
    return foliage, v1, v2, v3
end

local function probeWorkingSurfaces(wanted, quiet)
    wanted = math.max(1, math.min(16, tonumber(wanted) or 4))

    if type(createFoliage) ~= "function" then
        if not quiet then
            pushLog("FAIL", "createFoliage is not registered")
        end
        return state.surfaces
    end

    if #state.surfaces >= wanted then
        return state.surfaces
    end

    local px, py = playerOrigin()
    local _, _, pz = playerOrigin()
    local v1, v2, v3 = makeTriangle(px + 4, py + 4, 30, pz)

    for surface = 0, 255 do
        if not state.surfaceSet[surface] then
            local foliage = createFoliage(v1, v2, v3, surface, 1.0)
            if isElement(foliage) then
                destroyElement(foliage)
                state.surfaceSet[surface] = true
                table.insert(state.surfaces, surface)
                if #state.surfaces >= wanted then
                    break
                end
            end
        end
    end

    if not quiet then
        if #state.surfaces > 0 then
            pushLog("INFO", "Working foliage surfaces: " .. surfaceListText())
        else
            pushLog("WARN", "No working foliage surface found at this test triangle")
        end
    end

    return state.surfaces
end

local function apiAvailable()
    local names = {
        "createFoliage",
        "getFoliageSurface",
        "setFoliageSurface",
        "getFoliageVertices",
        "setFoliageVertices",
        "getFoliageDensity",
        "setFoliageDensity",
    }

    local ok = true
    for _, name in ipairs(names) do
        ok = check("API registered: " .. name, type(_G[name]) == "function") and ok
    end
    return ok
end

local function runCoreTests()
    state.pass, state.fail, state.warn = 0, 0, 0
    state.logs = {}
    pushLog("INFO", "Starting custom foliage core regression suite")

    if not apiAvailable() then
        pushLog("FAIL", "Core suite aborted because one or more foliage functions are missing")
        return false
    end

    probeWorkingSurfaces(4, true)
    if not check("At least one native surface can create foliage", #state.surfaces > 0) then
        warning("Move to a normal streamed world area and retry /foliage_probe")
        return false
    end

    local surface = state.surfaces[1]
    local px, py, pz = playerOrigin()
    local v1, v2, v3 = makeTriangle(px + 4, py + 4, 24, pz)
    local foliage = createFoliage(v1, v2, v3, surface, 1.0)

    if not check("createFoliage returns an element", isElement(foliage)) then
        return false
    end

    check("Element type is foliage", getElementType(foliage) == "foliage", tostring(getElementType(foliage)))
    check("Surface getter round-trip", getFoliageSurface(foliage) == surface)
    check("Density getter round-trip", nearlyEqual(getFoliageDensity(foliage), 1.0))

    local densities = {0.0, 0.5, 1.0, 2.0, 10.0}
    for _, density in ipairs(densities) do
        local setOk = setFoliageDensity(foliage, density)
        check(string.format("Density setter accepts %.1f", density), setOk == true)
        check(string.format("Density getter reports %.1f", density), nearlyEqual(getFoliageDensity(foliage), density))
    end

    check("Density setter rejects negative values", setFoliageDensity(foliage, -0.1) == false)
    check("Density setter rejects values above 10", setFoliageDensity(foliage, 10.01) == false)
    setFoliageDensity(foliage, 1.0)

    local rv1, rv2, rv3 = getFoliageVertices(foliage)
    check("Vertex getter returns three Vector3 values", rv1 and rv2 and rv3 and rv1.x and rv2.x and rv3.x)

    if rv1 and rv2 and rv3 then
        local moved1 = Vector3(rv1.x + 2.0, rv1.y, rv1.z)
        local moved2 = Vector3(rv2.x + 2.0, rv2.y, rv2.z)
        local moved3 = Vector3(rv3.x + 2.0, rv3.y, rv3.z)
        check("Vertex setter rebuilds native foliage", setFoliageVertices(foliage, moved1, moved2, moved3) == true)

        local gv1, gv2, gv3 = getFoliageVertices(foliage)
        check("Vertex setter/getter round-trip", vectorEqual(gv1, moved1) and vectorEqual(gv2, moved2) and vectorEqual(gv3, moved3))
    end

    if #state.surfaces >= 2 then
        local second = state.surfaces[2]
        check("Surface setter rebuilds with another valid surface", setFoliageSurface(foliage, second) == true)
        check("Surface getter reports changed surface", getFoliageSurface(foliage) == second)
        check("Surface setter restores original surface", setFoliageSurface(foliage, surface) == true)
    else
        warning("Only one usable surface found; multi-surface setter test skipped")
    end

    check("Surface setter rejects -1", setFoliageSurface(foliage, -1) == false)
    check("Surface setter rejects 256", setFoliageSurface(foliage, 256) == false)

    local originalDimension = getElementDimension(foliage)
    local hiddenDimension = originalDimension == 0 and 1 or 0
    check("Element dimension can be changed", setElementDimension(foliage, hiddenDimension) == true)
    check("Dimension getter reports hidden dimension", getElementDimension(foliage) == hiddenDimension)
    check("Element dimension can be restored", setElementDimension(foliage, originalDimension) == true)
    check("Dimension getter reports restored dimension", getElementDimension(foliage) == originalDimension)

    local propertyOk, propertyDensity = pcall(function()
        foliage.density = 1.25
        return foliage.density
    end)
    check("OOP density property works", propertyOk and nearlyEqual(propertyDensity, 1.25))

    local oopClass = rawget(_G, "Foliage")
    local oopOk, oopFoliage = pcall(function()
        if not oopClass or not oopClass.create then
            return false
        end
        return oopClass.create(v1, v2, v3, surface, 1.0)
    end)
    check("Foliage OOP class/create is registered", oopOk and isElement(oopFoliage))
    if isElement(oopFoliage) then
        destroyElement(oopFoliage)
    end

    local invalidSurfaceLow = createFoliage(v1, v2, v3, -1, 1.0)
    local invalidSurfaceHigh = createFoliage(v1, v2, v3, 256, 1.0)
    check("createFoliage rejects surface -1", invalidSurfaceLow == false)
    check("createFoliage rejects surface 256", invalidSurfaceHigh == false)

    local invalidDensityLow = createFoliage(v1, v2, v3, surface, -0.1)
    local invalidDensityHigh = createFoliage(v1, v2, v3, surface, 10.01)
    check("createFoliage rejects density below 0", invalidDensityLow == false)
    check("createFoliage rejects density above 10", invalidDensityHigh == false)

    local same = Vector3(px, py, pz)
    local degenerate = createFoliage(same, same, same, surface, 1.0)
    check("createFoliage rejects degenerate triangle", degenerate == false)

    for i = 1, math.min(4, #state.surfaces) do
        local testElement = createFoliage(v1, v2, v3, state.surfaces[i], 1.0)
        check("Surface " .. tostring(state.surfaces[i]) .. " creates successfully", isElement(testElement))
        if isElement(testElement) then
            destroyElement(testElement)
        end
    end

    local destroyed = destroyElement(foliage)
    check("destroyElement succeeds", destroyed == true)
    check("Destroyed foliage handle becomes invalid", not isElement(foliage))

    pushLog("INFO", string.format("Core suite finished: %d pass, %d fail, %d warn", state.pass, state.fail, state.warn))
    return state.fail == 0
end

local function runCapTest()
    if type(createFoliage) ~= "function" then
        pushLog("FAIL", "createFoliage is not registered")
        return false
    end

    clearDemo()
    clearLifetime()
    probeWorkingSurfaces(1, true)
    if #state.surfaces == 0 then
        warning("Cap test skipped: no working surface")
        return false
    end

    local surface = state.surfaces[1]
    local px, py, pz = playerOrigin()
    local created = {}

    for i = 1, 65 do
        local col = (i - 1) % 8
        local row = math.floor((i - 1) / 8)
        local v1, v2, v3 = makeTriangle(px + (col - 3.5) * 3.0, py + (row - 3.5) * 3.0, 24, pz)
        local foliage = createFoliage(v1, v2, v3, surface, 1.0)
        if not isElement(foliage) then
            if i == 65 and #created == 64 then
                check("Global custom foliage cap rejects element 65", true)
            else
                warning(string.format("Creation stopped at %d/%d; native pool or other foliage may already consume capacity", #created, 64))
            end
            break
        end
        table.insert(created, foliage)
    end

    if #created == 65 then
        check("Global custom foliage cap rejects element 65", false, "65 custom foliage elements were created")
    elseif #created == 64 then
        pushLog("PASS", "Created 64 custom foliage elements before the cap")
    end

    destroyList(created)
    pushLog("INFO", "Cap test cleanup complete")
    return #created <= 64
end

local function runStress(count, cycles)
    count = math.max(1, math.min(64, tonumber(count) or 32))
    cycles = math.max(1, math.min(20, tonumber(cycles) or 5))

    probeWorkingSurfaces(1, true)
    if #state.surfaces == 0 then
        warning("Stress test skipped: no working surface")
        return
    end

    local surface = state.surfaces[1]
    local px, py, pz = playerOrigin()
    local totalCreated = 0

    for cycle = 1, cycles do
        local elements = {}
        for i = 1, count do
            local col = (i - 1) % 8
            local row = math.floor((i - 1) / 8)
            local v1, v2, v3 = makeTriangle(px + (col - 3.5) * 3.0, py + (row - 3.5) * 3.0, 24, pz)
            local foliage = createFoliage(v1, v2, v3, surface, 1.0 + (i % 4) * 0.25)
            if not isElement(foliage) then
                warning(string.format("Stress cycle %d stopped at %d/%d creations", cycle, #elements, count))
                break
            end
            table.insert(elements, foliage)
            totalCreated = totalCreated + 1
        end
        destroyList(elements)
    end

    pushLog("INFO", string.format("Stress complete: %d create/destroy operations across %d cycles", totalCreated, cycles))
end

local function addDemoPatch(cx, cy, size, surface, density, label)
    local foliage, v1, v2, v3 = createAt(cx, cy, size, surface, density)
    if not isElement(foliage) then
        return false
    end

    table.insert(state.demo, {
        element = foliage,
        v1 = v1,
        v2 = v2,
        v3 = v3,
        label = label,
        surface = surface,
        density = density,
    })
    return true
end

local function destroyDemoElements()
    for i = #state.demo, 1, -1 do
        local entry = state.demo[i]
        if entry and isElement(entry.element) then
            destroyElement(entry.element)
        end
        state.demo[i] = nil
    end
end

local function prepareDemoCenter()
    local px, py, pz = playerOrigin()
    state.demoCenter = Vector3(px, py + 8.0, groundZ(px, py + 8.0, pz) + 1.0)
    return px, py, pz
end

local function startDensityDemo(surfaceArg)
    destroyDemoElements()
    stopCinematic()
    state.video = false

    local requested = tonumber(surfaceArg)
    if requested then
        requested = math.floor(requested)
        if requested < 0 or requested > 255 then
            warning("Requested surface must be between 0 and 255")
            requested = nil
        end
    end

    probeWorkingSurfaces(1, true)
    local surface = requested or state.surfaces[1]
    if surface == nil then
        warning("Density demo could not find a working surface")
        return false
    end

    local px, py, pz = prepareDemoCenter()
    local offsets = {
        {-12, 0, 0.0},
        {12, 0, 0.5},
        {-12, 22, 1.0},
        {12, 22, 2.0},
    }

    for _, entry in ipairs(offsets) do
        if not addDemoPatch(px + entry[1], py + entry[2], 18, surface, entry[3], string.format("density %.1fx", entry[3])) then
            destroyDemoElements()
            warning("Density demo creation failed; try /foliage_probe then pass a working surface")
            return false
        end
    end

    state.demoMode = "density"
    pushLog("INFO", "Density showcase ready on surface " .. tostring(surface))
    return true
end

local function startSurfaceDemo()
    destroyDemoElements()
    stopCinematic()
    state.video = false
    probeWorkingSurfaces(4, true)

    if #state.surfaces == 0 then
        warning("Surface demo could not find any working surface")
        return false
    end

    local px, py = prepareDemoCenter()
    local offsets = {
        {-12, 0},
        {12, 0},
        {-12, 22},
        {12, 22},
    }

    local count = math.min(4, #state.surfaces)
    for i = 1, count do
        local surface = state.surfaces[i]
        if not addDemoPatch(px + offsets[i][1], py + offsets[i][2], 18, surface, 1.0, "surface " .. tostring(surface)) then
            warning("Surface " .. tostring(surface) .. " failed while building showcase")
        end
    end

    if #state.demo == 0 then
        return false
    end

    state.demoMode = "surfaces"
    pushLog("INFO", "Surface showcase ready with " .. tostring(#state.demo) .. " variants")
    return true
end

local function startCinematic()
    if #state.demo == 0 or not state.demoCenter then
        warning("Start /foliage_demo or /foliage_demo_surfaces first")
        return false
    end

    state.cinematic = true
    state.cinematicTick = getTickCount()
    pushLog("INFO", "Cinematic orbit enabled")
    return true
end

local function toggleVideo(arg)
    if state.video then
        clearDemo()
        pushLog("INFO", "Video showcase stopped")
        return
    end

    local mode = tostring(arg or "density")
    local ok
    if mode == "surfaces" then
        ok = startSurfaceDemo()
    else
        ok = startDensityDemo(tonumber(arg))
    end

    if ok and startCinematic() then
        state.video = true
        pushLog("INFO", "Video mode active - run /foliage_video again to stop")
    end
end

local function drawDemo()
    for _, entry in ipairs(state.demo) do
        local v1, v2, v3 = entry.v1, entry.v2, entry.v3
        local lineColor = tocolor(80, 220, 130, 230)
        dxDrawLine3D(v1.x, v1.y, v1.z + 0.08, v2.x, v2.y, v2.z + 0.08, lineColor, 2)
        dxDrawLine3D(v2.x, v2.y, v2.z + 0.08, v3.x, v3.y, v3.z + 0.08, lineColor, 2)
        dxDrawLine3D(v3.x, v3.y, v3.z + 0.08, v1.x, v1.y, v1.z + 0.08, lineColor, 2)

        local cx = (v1.x + v2.x + v3.x) / 3
        local cy = (v1.y + v2.y + v3.y) / 3
        local cz = (v1.z + v2.z + v3.z) / 3 + 2.2
        local sx, sy = getScreenFromWorldPosition(cx, cy, cz, 0.05)
        if sx and sy then
            dxDrawText(entry.label, sx - 100, sy - 18, sx + 100, sy + 18, tocolor(255, 255, 255, 245), 1.15, "default-bold", "center", "center", false, false, false, true)
        end
    end
end

local function drawOverlay()
    if not state.overlay then
        return
    end

    local screenW = guiGetScreenSize()
    local x, y = 28, 70
    local width = 560
    local height = state.video and 82 or 260

    dxDrawRectangle(x, y, width, height, tocolor(10, 15, 18, 205))
    dxDrawText("CUSTOM FOLIAGE HARNESS", x + 16, y + 10, x + width - 16, y + 34, tocolor(110, 235, 150, 255), 1.25, "default-bold", "left", "center")

    local mode = state.demoMode or "none"
    local summary = string.format("PASS %d   FAIL %d   WARN %d   |   demo: %s", state.pass, state.fail, state.warn, mode)
    dxDrawText(summary, x + 16, y + 38, x + width - 16, y + 62, tocolor(235, 235, 235, 255), 1.0, "default", "left", "center")

    if state.video then
        return
    end

    dxDrawText("surfaces: " .. surfaceListText(), x + 16, y + 62, x + width - 16, y + 84, tocolor(180, 200, 210, 255), 0.95, "default", "left", "center")

    local logY = y + 90
    for i = math.min(#state.logs, 7), 1, -1 do
        dxDrawText(state.logs[i], x + 16, logY, x + width - 16, logY + 20, tocolor(220, 220, 220, 245), 0.9, "default", "left", "center", true)
        logY = logY + 22
    end

    dxDrawText("/foliage_help for commands", x + 16, y + height - 28, x + width - 16, y + height - 8, tocolor(145, 165, 175, 255), 0.9, "default", "left", "center")
end

addEventHandler("onClientRender", root, function()
    if state.cinematic and state.demoCenter then
        local elapsed = getTickCount() - state.cinematicTick
        local angle = (elapsed / 14000) * math.pi * 2
        local center = state.demoCenter
        local radius = 38
        local cameraX = center.x + math.cos(angle) * radius
        local cameraY = center.y + math.sin(angle) * radius
        local cameraZ = center.z + 15
        setCameraMatrix(cameraX, cameraY, cameraZ, center.x, center.y + 7, center.z + 1.0)
    end

    if #state.demo > 0 then
        drawDemo()
    end
    drawOverlay()
end)

addCommandHandler("foliage_test", function()
    runCoreTests()
end)

addCommandHandler("foliage_test_all", function()
    runCoreTests()
    runCapTest()
end)

addCommandHandler("foliage_probe", function(_, wanted)
    probeWorkingSurfaces(tonumber(wanted) or 8, false)
end)

addCommandHandler("foliage_captest", function()
    runCapTest()
end)

addCommandHandler("foliage_stress", function(_, count, cycles)
    runStress(count, cycles)
end)

addCommandHandler("foliage_demo", function(_, surface)
    startDensityDemo(surface)
end)

addCommandHandler("foliage_demo_surfaces", function()
    startSurfaceDemo()
end)

addCommandHandler("foliage_cinematic", function()
    if state.cinematic then
        stopCinematic()
        pushLog("INFO", "Cinematic orbit disabled")
    else
        startCinematic()
    end
end)

addCommandHandler("foliage_video", function(_, modeOrSurface)
    toggleVideo(modeOrSurface)
end)

addCommandHandler("foliage_overlay", function()
    state.overlay = not state.overlay
    pushLog("INFO", "Overlay " .. (state.overlay and "enabled" or "disabled"))
end)

addCommandHandler("foliage_lifetime", function(_, surfaceArg)
    clearLifetime()
    probeWorkingSurfaces(1, true)
    local surface = tonumber(surfaceArg) or state.surfaces[1]
    if not surface then
        warning("Lifetime test needs a working surface")
        return
    end

    local px, py = playerOrigin()
    state.lifetime = select(1, createAt(px + 8, py, 24, math.floor(surface), 2.0))
    if isElement(state.lifetime) then
        pushLog("INFO", "Lifetime patch created. Restart/stop this resource: the patch must disappear with it.")
    else
        warning("Lifetime patch creation failed")
    end
end)

addCommandHandler("foliage_clear", function()
    clearDemo()
    clearLifetime()
    pushLog("INFO", "Harness-owned persistent foliage cleared")
end)

addCommandHandler("foliage_help", function()
    outputChatBox("--- Custom foliage harness ---", 110, 235, 150)
    outputChatBox("/foliage_test - functional API/getter/setter/dimension/OOP regression", 230, 230, 230)
    outputChatBox("/foliage_test_all - core suite + 64 element cap test", 230, 230, 230)
    outputChatBox("/foliage_probe [count] - discover surfaces that actually produce plants", 230, 230, 230)
    outputChatBox("/foliage_stress [count=32] [cycles=5] - repeated create/destroy", 230, 230, 230)
    outputChatBox("/foliage_captest - verify the custom 64 element guard", 230, 230, 230)
    outputChatBox("/foliage_demo [surface] - 0x / 0.5x / 1x / 2x visual comparison", 230, 230, 230)
    outputChatBox("/foliage_demo_surfaces - compare up to four working plant surfaces", 230, 230, 230)
    outputChatBox("/foliage_video [surface|surfaces] - demo + orbit camera for recording", 230, 230, 230)
    outputChatBox("/foliage_cinematic - toggle orbit camera", 230, 230, 230)
    outputChatBox("/foliage_lifetime [surface] - manual resource-stop ownership test", 230, 230, 230)
    outputChatBox("/foliage_overlay - toggle HUD; /foliage_clear - cleanup", 230, 230, 230)
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    pushLog("INFO", "Custom foliage harness loaded. Use /foliage_help or /foliage_test.")
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    -- Do not explicitly destroy foliage here. Resource-owned elements must be
    -- released by the normal resource ElementGroup teardown; /foliage_lifetime
    -- exists specifically so that lifetime path can be checked visually.
    if state.cinematic then
        setCameraTarget(localPlayer)
    end
end)
