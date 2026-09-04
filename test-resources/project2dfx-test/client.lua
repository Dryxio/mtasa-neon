local enabledByResource = false
local benchmark = nil
local BENCHMARK_WARMUP_MS = 5000
local BENCHMARK_DEFAULT_SECONDS = 15

local function outputStats()
    local stats = engineGetDistantLightStats()
    local message = ("[Project2DFX] enabled=%s definitions=%d active=%d/%d distance=%.0f (%s)"):format(
        tostring(stats.enabled),
        stats.definitions,
        stats.activeCoronas,
        stats.coronaCapacity,
        stats.drawDistance,
        stats.automaticDrawDistance and "auto" or "manual"
    )
    outputChatBox(message, 255, 210, 80)
    outputDebugString(message)
end

local function setProject2DFX(_, state, requestedDistance)
    if benchmark then
        outputChatBox("[Project2DFX] Benchmark running; use /project2dfxbenchcancel first", 255, 100, 100)
        return
    end

    state = state and state:lower() or ""
    if state ~= "on" and state ~= "off" and state ~= "rebuild" then
        outputChatBox("[Project2DFX] /project2dfx [on|off|rebuild] [auto|300-5000]", 255, 210, 80)
        return
    end

    if state == "rebuild" then
        engineRebuildDistantLights()
        outputStats()
        return
    end

    if state == "on" then
        if not requestedDistance or requestedDistance == "auto" then
            engineSetDistantLightsAutomaticDrawDistance(true)
        else
            local distance = tonumber(requestedDistance)
            if not distance or not engineSetDistantLightsDrawDistance(distance) then
                outputChatBox("[Project2DFX] Use auto or a distance between 300 and 5000", 255, 100, 100)
                return
            end
        end
        engineSetDistantLightsEnabled(true)
        enabledByResource = true
    else
        engineSetDistantLightsEnabled(false)
        enabledByResource = false
    end

    outputStats()
end

local function outputBenchmark(message, color)
    outputChatBox(message, color or 255, color and 255 or 210, 80)
    outputDebugString(message)
end

local function percentile(sortedSamples, fraction)
    if #sortedSamples == 0 then
        return 0
    end

    local index = math.max(1, math.ceil(#sortedSamples * fraction))
    return sortedSamples[index]
end

local function restoreBenchmarkState()
    if not benchmark then
        return
    end

    engineSetDistantLightsDrawDistance(benchmark.initialStats.drawDistance)
    engineSetDistantLightsAutomaticDrawDistance(benchmark.initialStats.automaticDrawDistance)
    engineSetDistantLightsEnabled(benchmark.initialStats.enabled)
    if benchmark.initialStats.enabled then
        engineRebuildDistantLights()
    end
end

local function finishBenchmarkProfile(cancelled)
    if not benchmark then
        return
    end

    if isTimer(benchmark.timer) then
        killTimer(benchmark.timer)
    end

    restoreBenchmarkState()
    if cancelled then
        outputBenchmark("[2DFX profile] Cancelled; previous distant-light state restored")
    else
        outputBenchmark("[2DFX profile] Complete; previous distant-light state restored")
        for _, result in ipairs(benchmark.results) do
            outputBenchmark(("[2DFX summary] %s: %.1f FPS | p95 %.2f ms | p99 %.2f ms | lights %d"):format(
                result.label,
                result.fps,
                result.p95,
                result.p99,
                result.activeCoronas
            ))
        end
    end

    benchmark = nil
end

local startBenchmarkStage

local function finishBenchmarkStage()
    if not benchmark or benchmark.phase ~= "measure" then
        return
    end

    benchmark.phase = "transition"
    local elapsedMs = math.max(1, getTickCount() - benchmark.measureStart)
    local samples = benchmark.samples
    table.sort(samples)

    local frameCount = #samples
    local fps = frameCount * 1000 / elapsedMs
    local totalFrameTime = 0
    for _, frameTime in ipairs(samples) do
        totalFrameTime = totalFrameTime + frameTime
    end

    local averageFrameTime = frameCount > 0 and totalFrameTime / frameCount or 0
    local p95 = percentile(samples, 0.95)
    local p99 = percentile(samples, 0.99)
    local worst = samples[frameCount] or 0
    local lights = engineGetDistantLightStats()
    local renderer = engineGetRendererStats()
    local stage = benchmark.stages[benchmark.stageIndex]

    table.insert(benchmark.results, {
        label = stage.label,
        fps = fps,
        p95 = p95,
        p99 = p99,
        activeCoronas = lights.activeCoronas,
    })

    outputBenchmark(("[2DFX bench] %s: %.1f FPS | avg %.2f ms | p95 %.2f | p99 %.2f | worst %.2f"):format(
        stage.label,
        fps,
        averageFrameTime,
        p95,
        p99,
        worst
    ))
    outputBenchmark(("[2DFX bench] lights %d/%d | visible HW %d/%d | LOD HW %d/%d | RwObjects HW %d/%d"):format(
        lights.activeCoronas,
        lights.coronaCapacity,
        renderer.visibleEntityHighWater,
        renderer.visibleEntityCapacity,
        renderer.visibleLodHighWater,
        renderer.visibleLodCapacity,
        renderer.streamingRwObjectHighWater,
        renderer.streamingRwObjectCapacity
    ))

    benchmark.stageIndex = benchmark.stageIndex + 1
    if benchmark.stageIndex > #benchmark.stages then
        finishBenchmarkProfile(false)
    else
        benchmark.timer = setTimer(startBenchmarkStage, 1000, 1)
    end
end

local function beginBenchmarkMeasurement()
    if not benchmark or benchmark.phase ~= "warmup" then
        return
    end

    benchmark.phase = "measure"
    benchmark.samples = {}
    benchmark.measureStart = getTickCount()
    engineResetRendererStats()
    outputBenchmark(("[2DFX profile] Measuring %s for %d seconds; keep camera still"):format(
        benchmark.stages[benchmark.stageIndex].label,
        benchmark.durationMs / 1000
    ))
    benchmark.timer = setTimer(finishBenchmarkStage, benchmark.durationMs, 1)
end

startBenchmarkStage = function()
    if not benchmark then
        return
    end

    local stage = benchmark.stages[benchmark.stageIndex]
    benchmark.phase = "warmup"
    if stage.distance then
        engineSetDistantLightsDrawDistance(stage.distance)
        engineSetDistantLightsEnabled(true)
        engineRebuildDistantLights()
    else
        engineSetDistantLightsEnabled(false)
    end

    outputBenchmark(("[2DFX profile] %s warm-up: 5 seconds; do not move camera"):format(stage.label))
    benchmark.timer = setTimer(beginBenchmarkMeasurement, BENCHMARK_WARMUP_MS, 1)
end

local function readBenchmarkDuration(requestedSeconds)
    local seconds = tonumber(requestedSeconds) or BENCHMARK_DEFAULT_SECONDS
    if seconds < 5 or seconds > 60 then
        return false
    end
    return math.floor(seconds)
end

local function beginBenchmark(stages, requestedSeconds)
    if benchmark then
        outputChatBox("[Project2DFX] A benchmark is already running", 255, 100, 100)
        return
    end

    local seconds = readBenchmarkDuration(requestedSeconds)
    if not seconds then
        outputChatBox("[Project2DFX] Benchmark duration must be between 5 and 60 seconds", 255, 100, 100)
        return
    end

    benchmark = {
        stages = stages,
        stageIndex = 1,
        phase = "transition",
        durationMs = seconds * 1000,
        initialStats = engineGetDistantLightStats(),
        samples = {},
        results = {},
    }
    startBenchmarkStage()
end

local function runSingleBenchmark(_, requestedStage, requestedSeconds)
    requestedStage = requestedStage and requestedStage:lower() or ""
    local distance = tonumber(requestedStage)
    if requestedStage ~= "off" and distance ~= 2000 and distance ~= 3000 and distance ~= 5000 then
        outputChatBox("[Project2DFX] /project2dfxbench [off|2000|3000|5000] [5-60 seconds]", 255, 210, 80)
        return
    end

    beginBenchmark({{label = requestedStage, distance = distance}}, requestedSeconds)
end

local function runFullProfile(_, requestedSeconds)
    beginBenchmark({
        {label = "off", distance = false},
        {label = "2000", distance = 2000},
        {label = "3000", distance = 3000},
        {label = "5000", distance = 5000},
    }, requestedSeconds)
end

addEventHandler("onClientPreRender", root, function(frameTime)
    if benchmark and benchmark.phase == "measure" and frameTime and frameTime > 0 then
        table.insert(benchmark.samples, frameTime)
    end
end)

addCommandHandler("project2dfx", setProject2DFX)
addCommandHandler("project2dfxstats", outputStats)
addCommandHandler("project2dfxbench", runSingleBenchmark)
addCommandHandler("project2dfxprofile", runFullProfile)
addCommandHandler("project2dfxbenchcancel", function()
    finishBenchmarkProfile(true)
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    if benchmark then
        restoreBenchmarkState()
        benchmark = nil
    end
    if enabledByResource then
        engineSetDistantLightsEnabled(false)
    end
end)

outputChatBox("[Project2DFX] /project2dfx on [300-5000], /project2dfxstats, /project2dfx off", 255, 210, 80)
outputChatBox("[Project2DFX] /project2dfxprofile [5-60 seconds] or /project2dfxbench [off|2000|3000|5000] [seconds]", 255, 210, 80)
