local WARMUP_MS = 5000
local DEFAULT_SECONDS = 15
local MAX_SECONDS = 60

local entities = {}
local benchmark = nil
local profile = nil
local runNextProfile
local savedCamera = nil
local testOrigin = nil
local modelSetName = "homogeneous"
local preloadedModels = {}
local models = {
    vehicle = 411,
    ped = 7,
    object = 1271,
}
-- Fixed native sets make model diversity reproducible instead of depending on
-- whichever production resources happen to be running during a profile.
local variedModels = {
    vehicle = {400, 401, 404, 409, 411, 415, 429, 445, 451, 470, 477, 482, 495, 506, 541, 560, 579, 580, 587, 589},
    ped = {7, 9, 14, 21, 28, 46, 60, 73, 105, 120, 147, 170, 190, 211, 240, 270},
    object = {1210, 1212, 1225, 1230, 1235, 1264, 1271, 1299, 1337, 1344, 1352, 1431, 1440, 1458, 1558, 1654, 1685, 2912},
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
    if not testOrigin then
        local px, py, pz = getElementPosition(localPlayer)
        local rotation = math.rad(select(3, getElementRotation(localPlayer)))
        testOrigin = {x = px, y = py, z = pz, forwardX = -math.sin(rotation), forwardY = math.cos(rotation)}
        output(("[entitybench] Locked origin at %.2f, %.2f, %.2f"):format(px, py, pz))
    end

    local px, py, pz = testOrigin.x, testOrigin.y, testOrigin.z
    local forwardX, forwardY = testOrigin.forwardX, testOrigin.forwardY
    local distance = view == "far" and 1000 or 28
    return px + forwardX * distance, py + forwardY * distance, pz + 1, forwardX, forwardY
end

local function configureCamera(anchorX, anchorY, anchorZ, forwardX, forwardY, view)
    saveCamera()
    local cameraX = anchorX - forwardX * 34
    local cameraY = anchorY - forwardY * 34
    local cameraZ = anchorZ + 16
    if view == "far" then
        cameraX, cameraY, cameraZ = testOrigin.x, testOrigin.y, testOrigin.z + 6
    end

    if view == "hidden" or view == "far" then
        setCameraMatrix(cameraX, cameraY, cameraZ, cameraX - forwardX * 20, cameraY - forwardY * 20, cameraZ)
    else
        setCameraMatrix(cameraX, cameraY, cameraZ, anchorX, anchorY, anchorZ)
    end
end

local function getGridOffset(index, count, layout)
    if layout == "contact" then
        local angle = (index - 1) * 2.399963
        local radius = math.sqrt(index - 1) * 0.18
        return math.cos(angle) * radius, math.sin(angle) * radius
    end

    local columns = math.ceil(math.sqrt(count))
    local row = math.floor((index - 1) / columns)
    local column = (index - 1) % columns
    local spacingX = layout == "touching" and 2.0 or 6
    local spacingY = layout == "touching" and 4.2 or 6
    return (column - (columns - 1) / 2) * spacingX, (row - (columns - 1) / 2) * spacingY
end

local function applyState(element, kind, state, collisions, index)
    setElementCollisionsEnabled(element, collisions)
    setElementFrozen(element, state == "static")

    if state ~= "moving" then
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

local function getModel(kind, index)
    if modelSetName == "varied" then
        local list = variedModels[kind]
        return list[((index - 1) % #list) + 1]
    end
    return models[kind]
end

local function createOne(kind, index, x, y, z, heading)
    local model = getModel(kind, index)
    if kind == "vehicle" then
        return createVehicle(model, x, y, z + 0.5, 0, 0, heading)
    elseif kind == "ped" then
        return createPed(model, x, y, z, heading)
    end
    return createObject(model, x, y, z + 0.5)
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

        local offsetX, offsetY = getGridOffset(index, config.count, config.layout)
        local element = createOne(kind, index, anchorX + offsetX, anchorY + offsetY, anchorZ, 0)
        if element then
            table.insert(entities, element)
            applyState(element, kind, config.state, config.collisions, index)
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
        if profile then
            table.insert(profile.results, {
                label = ("%s %d %s/%s/%s collisions=%s models=%s"):format(
                    config.kind, config.count, config.state, config.view, config.layout, tostring(config.collisions), config.modelSet
                ),
                fps = fps,
                average = average,
                p95 = p95,
                p99 = p99,
                worst = worst,
            })
        end
        output(("[entitybench] %s requested=%d created=%d %s/%s/%s collisions=%s models=%s: %.1f FPS | avg %.2f ms | p95 %.2f | p99 %.2f | worst %.2f"):format(
            config.kind, config.count, benchmark.created, config.state, config.view,
            config.layout, tostring(config.collisions), config.modelSet, fps, average, p95, p99, worst
        ))

        if engineGetRendererStats then
            local stats = engineGetRendererStats()
            output(("[entitybench] renderer HW: visible %d/%d | LOD %d/%d | RwObjects %d/%d"):format(
                stats.visibleEntityHighWater, stats.visibleEntityCapacity,
                stats.visibleLodHighWater, stats.visibleLodCapacity,
                stats.streamingRwObjectHighWater, stats.streamingRwObjectCapacity
            ))
        end
    end

    local continueProfile = profile and not cancelled
    if cancelled then
        profile = nil
    end
    benchmark = nil
    clearTest(false)
    if continueProfile then
        setTimer(runNextProfile, 1000, 1)
    end
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
    if not validKinds[kind] or not validCount or (state ~= "static" and state ~= "idle" and state ~= "moving") or
        not validViews[view] or (layout ~= "separate" and layout ~= "touching" and layout ~= "contact") or
        (collisionText ~= "on" and collisionText ~= "off") or seconds < 5 or seconds > MAX_SECONDS then
        output("[entitybench] /entitybench [baseline|vehicle|ped|object|mixed] [0|1-2000] [static|idle|moving] [visible|hidden|far] [separate|touching|contact] [on|off collisions] [5-60 seconds]", true)
        return
    end

    local config = {
        kind = kind,
        count = count,
        state = state,
        view = view,
        layout = layout,
        collisions = collisionText == "on",
        modelSet = modelSetName,
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

local function releasePreloadedModels()
    for _, model in ipairs(preloadedModels) do
        engineStreamingReleaseModel(model, true)
    end
    preloadedModels = {}
end

local function preloadVariedModels()
    -- Hold every model through the profile so first-use streaming I/O cannot be
    -- mistaken for steady-state per-frame entity cost.
    releasePreloadedModels()
    local seen = {}
    local requested = 0
    for _, list in pairs(variedModels) do
        for _, model in ipairs(list) do
            if not seen[model] then
                seen[model] = true
                local callSucceeded, loaded = pcall(engineStreamingRequestModel, model, true, true)
                if callSucceeded and loaded then
                    table.insert(preloadedModels, model)
                    requested = requested + 1
                else
                    output(("[entitybench] Could not preload model %d"):format(model), true)
                end
            end
        end
    end
    output(("[entitybench] Preloaded %d varied GTA models"):format(requested))
end

local function setModelSet(_, requestedSet)
    requestedSet = requestedSet and requestedSet:lower() or ""
    if requestedSet ~= "homogeneous" and requestedSet ~= "varied" then
        output(("[entitybench] model set=%s; usage /entitybenchmodelset [homogeneous|varied]"):format(modelSetName))
        return false
    end
    if benchmark or profile then
        output("[entitybench] Cancel the active benchmark/profile before changing model set", true)
        return false
    end

    modelSetName = requestedSet
    if modelSetName == "varied" then
        preloadVariedModels()
    else
        releasePreloadedModels()
    end
    output(("[entitybench] model set=%s"):format(modelSetName))
    return true
end

local profileStages = {
    "baseline 0 static visible separate off",
    "baseline 0 static hidden separate off",
    "vehicle 16 idle visible separate on",
    "vehicle 32 idle visible separate on",
    "vehicle 48 idle visible separate on",
    "vehicle 64 idle visible separate on",
    "vehicle 64 idle hidden separate on",
    "vehicle 64 idle far separate on",
    "vehicle 64 moving visible separate on",
    "vehicle 16 moving visible touching on",
    "vehicle 32 moving visible touching on",
    "vehicle 64 moving visible touching on",
    "vehicle 4 moving visible contact on",
    "vehicle 8 moving visible contact on",
    "vehicle 16 moving visible contact on",
    "vehicle 16 moving visible contact off",
    "ped 32 idle visible separate on",
    "ped 64 idle visible separate on",
    "ped 96 idle visible separate on",
    "ped 110 idle visible separate on",
    "ped 110 moving visible separate on",
    "ped 110 moving hidden separate on",
    "ped 110 moving far separate on",
    "object 128 static visible separate on",
    "object 512 static visible separate on",
    "object 900 static visible separate on",
    "object 1000 static visible separate on",
    "object 1000 static hidden separate on",
    "object 1000 static far separate on",
    "object 900 moving visible separate on",
    "mixed 96 idle visible separate on",
    "mixed 192 idle visible separate on",
    "mixed 192 moving visible separate on",
}

runNextProfile = function()
    if not profile or benchmark then
        return
    end
    if profile.index > #profileStages then
        output(("[entitybench profile] Complete: %d stages recorded"):format(#profile.results))
        profile = nil
        return
    end

    local stage = profileStages[profile.index]
    output(("[entitybench profile] Stage %d/%d: %s"):format(profile.index, #profileStages, stage))
    profile.index = profile.index + 1
    executeCommandHandler("entitybench", stage .. " " .. profile.seconds)
end

local function runProfile(_, secondsText)
    if benchmark or profile then
        output("[entitybench profile] A benchmark or profile is already running", true)
        return
    end
    local seconds = math.floor(tonumber(secondsText) or 10)
    if seconds < 5 or seconds > MAX_SECONDS then
        output("[entitybench profile] /entitybenchprofile [5-60 seconds per stage]", true)
        return
    end

    profile = {index = 1, seconds = seconds, results = {}}
    testOrigin = nil
    output(("[entitybench profile] Starting %d stages with %d-second samples"):format(#profileStages, seconds))
    runNextProfile()
end

local function runVariedProfile(_, secondsText)
    if not setModelSet(nil, "varied") then
        return
    end
    runProfile(nil, secondsText)
end

addEventHandler("onClientPreRender", root, function(frameTime)
    if benchmark and benchmark.phase == "measure" and frameTime and frameTime > 0 then
        table.insert(benchmark.samples, frameTime)
    end
end)

addCommandHandler("entitybench", runBenchmark)
addCommandHandler("entitybenchprofile", runProfile)
addCommandHandler("entitybenchvariedprofile", runVariedProfile)
addCommandHandler("entitybenchmodels", setModels)
addCommandHandler("entitybenchmodelset", setModelSet)
addCommandHandler("entitybenchcancel", function()
    if benchmark then
        finishBenchmark(true)
    elseif profile then
        profile = nil
        clearTest(false)
        output("[entitybench profile] Cancelled")
    end
end)
addCommandHandler("entitybenchclear", function()
    if benchmark then
        finishBenchmark(true)
    else
        profile = nil
        clearTest(false)
        output("[entitybench] Cleared")
    end
end)
addCommandHandler("entitybenchresetorigin", function()
    if benchmark or profile then
        output("[entitybench] Wait for the benchmark or cancel it before resetting the origin", true)
        return
    end
    testOrigin = nil
    output("[entitybench] Origin reset; the next benchmark will lock the current player position and heading")
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    if benchmark and isTimer(benchmark.timer) then
        killTimer(benchmark.timer)
    end
    benchmark = nil
    profile = nil
    clearTest(false)
    releasePreloadedModels()
end)

output("[entitybench] Ready. Use /entitybench or read test-resources/entity-performance-test/README.md")
