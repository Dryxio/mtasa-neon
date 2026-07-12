local WARMUP_MS = 5000
local DEFAULT_SECONDS = 15
local MAX_SECONDS = 60

local entities = {}
local benchmark = nil
local savedCamera = nil
local models = {
    vehicle = 411,
    ped = 7,
    object = 1271,
}

local function output(message, errorMessage)
    outputChatBox(message, errorMessage and 255 or 255, errorMessage and 90 or 210, errorMessage and 90 or 80)
    outputDebugString(message)
end

local function percentile(sortedSamples, fraction)
    if #sortedSamples == 0 then
        return 0
    end
    return sortedSamples[math.max(1, math.ceil(#sortedSamples * fraction))]
end

local function destroyEntities()
    for _, element in ipairs(entities) do
        if isElement(element) then
            destroyElement(element)
        end
    end
    entities = {}
end

local function restoreCamera()
    if savedCamera then
        setCameraMatrix(unpack(savedCamera))
        savedCamera = nil
    else
        setCameraTarget(localPlayer)
    end
end

local function clearTest(keepCamera)
    destroyEntities()
    if not keepCamera then
        restoreCamera()
    end
end

local function saveCamera()
    if savedCamera then
        return
    end
    savedCamera = {getCameraMatrix()}
end

local function getAnchor(view)
    local px, py, pz = getElementPosition(localPlayer)
    local rotation = math.rad(select(3, getElementRotation(localPlayer)))
    local forwardX = -math.sin(rotation)
    local forwardY = math.cos(rotation)
    local distance = view == "far" and 1000 or 28
    return px + forwardX * distance, py + forwardY * distance, pz + 1, forwardX, forwardY
end

local function configureCamera(anchorX, anchorY, anchorZ, forwardX, forwardY, view)
    saveCamera()
    local cameraX = anchorX - forwardX * 34
    local cameraY = anchorY - forwardY * 34
    local cameraZ = anchorZ + 16
    if view == "far" then
        local px, py, pz = getElementPosition(localPlayer)
        cameraX, cameraY, cameraZ = px, py, pz + 6
    end

    if view == "hidden" or view == "far" then
        setCameraMatrix(cameraX, cameraY, cameraZ, cameraX - forwardX * 20, cameraY - forwardY * 20, cameraZ)
    else
        setCameraMatrix(cameraX, cameraY, cameraZ, anchorX, anchorY, anchorZ)
    end
end

local function getGridOffset(index, count, contact)
    if contact then
        local angle = (index - 1) * 2.399963
        local radius = math.sqrt(index - 1) * 0.18
        return math.cos(angle) * radius, math.sin(angle) * radius
    end

    local columns = math.ceil(math.sqrt(count))
    local row = math.floor((index - 1) / columns)
    local column = (index - 1) % columns
    local spacing = 6
    return (column - (columns - 1) / 2) * spacing, (row - (columns - 1) / 2) * spacing
end

local function applyState(element, kind, moving, collisions, index)
    setElementCollisionsEnabled(element, collisions)
    setElementFrozen(element, not moving)

    if not moving then
        return
    end

    if kind == "ped" then
        setPedAnimation(element, "ped", "WALK_player", -1, true, true, false, false)
    elseif kind == "vehicle" then
        local angle = (index % 8) * math.pi / 4
        setElementVelocity(element, math.cos(angle) * 0.08, math.sin(angle) * 0.08, 0)
        setElementAngularVelocity(element, 0, 0, (index % 2 == 0) and 0.01 or -0.01)
    else
        local angle = (index % 8) * math.pi / 4
        setElementVelocity(element, math.cos(angle) * 0.04, math.sin(angle) * 0.04, 0.02)
        setElementAngularVelocity(element, 0.01, 0.015, 0.02)
    end
end

local function createOne(kind, x, y, z, heading)
    if kind == "vehicle" then
        return createVehicle(models.vehicle, x, y, z + 0.5, 0, 0, heading)
    elseif kind == "ped" then
        return createPed(models.ped, x, y, z, heading)
    end
    return createObject(models.object, x, y, z + 0.5)
end

local function createScenario(config)
    clearTest(true)

    local anchorX, anchorY, anchorZ, forwardX, forwardY = getAnchor(config.view)
    configureCamera(anchorX, anchorY, anchorZ, forwardX, forwardY, config.view)

    for index = 1, config.count do
        local kind = config.kind
        if kind == "mixed" then
            kind = ({"vehicle", "ped", "object"})[((index - 1) % 3) + 1]
        end

        local offsetX, offsetY = getGridOffset(index, config.count, config.contact)
        local element = createOne(kind, anchorX + offsetX, anchorY + offsetY, anchorZ, 0)
        if element then
            table.insert(entities, element)
            applyState(element, kind, config.moving, config.collisions, index)
        end
    end

    return #entities
end

local function finishBenchmark(cancelled)
    if not benchmark then
        return
    end
    if isTimer(benchmark.timer) then
        killTimer(benchmark.timer)
    end

    if cancelled then
        output("[entitybench] Cancelled; entities and camera restored")
    else
        local samples = benchmark.samples
        table.sort(samples)
        local total = 0
        for _, frameTime in ipairs(samples) do
            total = total + frameTime
        end

        local elapsedMs = math.max(1, getTickCount() - benchmark.measureStart)
        local fps = #samples * 1000 / elapsedMs
        local average = #samples > 0 and total / #samples or 0
        local p95 = percentile(samples, 0.95)
        local p99 = percentile(samples, 0.99)
        local worst = samples[#samples] or 0
        local config = benchmark.config
        output(("[entitybench] %s requested=%d created=%d %s/%s/%s collisions=%s: %.1f FPS | avg %.2f ms | p95 %.2f | p99 %.2f | worst %.2f"):format(
            config.kind, config.count, benchmark.created, config.moving and "moving" or "static", config.view,
            config.contact and "contact" or "separate", tostring(config.collisions), fps, average, p95, p99, worst
        ))

        if fps >= 70 and fps <= 76 then
            output("[entitybench] Warning: result is near the known ~74 FPS cap; compare frame time after disabling limiter/VSync")
        end

        if engineGetRendererStats then
            local stats = engineGetRendererStats()
            output(("[entitybench] renderer HW: visible %d/%d | LOD %d/%d | RwObjects %d/%d"):format(
                stats.visibleEntityHighWater, stats.visibleEntityCapacity,
                stats.visibleLodHighWater, stats.visibleLodCapacity,
                stats.streamingRwObjectHighWater, stats.streamingRwObjectCapacity
            ))
        end
    end

    benchmark = nil
    clearTest(false)
end

local function beginMeasurement()
    if not benchmark or benchmark.phase ~= "warmup" then
        return
    end
    benchmark.phase = "measure"
    benchmark.samples = {}
    benchmark.measureStart = getTickCount()
    if engineResetRendererStats then
        engineResetRendererStats()
    end
    output(("[entitybench] Measuring for %d seconds; do not move or change graphics settings"):format(benchmark.durationMs / 1000))
    benchmark.timer = setTimer(function() finishBenchmark(false) end, benchmark.durationMs, 1)
end

local validKinds = {baseline = true, vehicle = true, ped = true, object = true, mixed = true}
local validViews = {visible = true, hidden = true, far = true}

local function runBenchmark(_, kind, countText, state, view, layout, collisionText, secondsText)
    if benchmark then
        output("[entitybench] A benchmark is already running; use /entitybenchcancel", true)
        return
    end

    kind = kind and kind:lower() or ""
    state = state and state:lower() or ""
    view = view and view:lower() or ""
    layout = layout and layout:lower() or ""
    collisionText = collisionText and collisionText:lower() or ""
    local count = math.floor(tonumber(countText) or 0)
    local seconds = math.floor(tonumber(secondsText) or DEFAULT_SECONDS)

    local validCount = (kind == "baseline" and count == 0) or (kind ~= "baseline" and count >= 1 and count <= 2000)
    if not validKinds[kind] or not validCount or (state ~= "static" and state ~= "moving") or
        not validViews[view] or (layout ~= "separate" and layout ~= "contact") or
        (collisionText ~= "on" and collisionText ~= "off") or seconds < 5 or seconds > MAX_SECONDS then
        output("[entitybench] /entitybench [baseline|vehicle|ped|object|mixed] [0|1-2000] [static|moving] [visible|hidden|far] [separate|contact] [on|off collisions] [5-60 seconds]", true)
        return
    end

    local config = {
        kind = kind,
        count = count,
        moving = state == "moving",
        view = view,
        contact = layout == "contact",
        collisions = collisionText == "on",
    }
    local created = createScenario(config)
    if created == 0 and kind ~= "baseline" then
        clearTest(false)
        output("[entitybench] No entity could be created; check the selected models", true)
        return
    end

    benchmark = {
        config = config,
        created = created,
        durationMs = seconds * 1000,
        phase = "warmup",
        samples = {},
    }
    output(("[entitybench] Created %d/%d entities; warming up for %d seconds"):format(created, count, WARMUP_MS / 1000))
    benchmark.timer = setTimer(beginMeasurement, WARMUP_MS, 1)
end

local function setModels(_, vehicleText, pedText, objectText)
    local vehicle = tonumber(vehicleText)
    local ped = tonumber(pedText)
    local object = tonumber(objectText)
    if not vehicle or not ped or not object then
        output(("[entitybench] models: vehicle=%d ped=%d object=%d; usage /entitybenchmodels [vehicle] [ped] [object]"):format(
            models.vehicle, models.ped, models.object
        ))
        return
    end
    models.vehicle, models.ped, models.object = math.floor(vehicle), math.floor(ped), math.floor(object)
    output(("[entitybench] models set: vehicle=%d ped=%d object=%d"):format(models.vehicle, models.ped, models.object))
end

addEventHandler("onClientPreRender", root, function(frameTime)
    if benchmark and benchmark.phase == "measure" and frameTime and frameTime > 0 then
        table.insert(benchmark.samples, frameTime)
    end
end)

addCommandHandler("entitybench", runBenchmark)
addCommandHandler("entitybenchmodels", setModels)
addCommandHandler("entitybenchcancel", function() finishBenchmark(true) end)
addCommandHandler("entitybenchclear", function()
    if benchmark then
        finishBenchmark(true)
    else
        clearTest(false)
        output("[entitybench] Cleared")
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    if benchmark and isTimer(benchmark.timer) then
        killTimer(benchmark.timer)
    end
    benchmark = nil
    clearTest(false)
end)

output("[entitybench] Ready. Use /entitybench or read test-resources/entity-performance-test/README.md")
