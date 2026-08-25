local PREFIX = "[NEON_DEVTOOLS_TEST]"

local function emitBaseline()
    outputDebugString(PREFIX .. " server debug message", 0, 117, 205, 255)
    outputDebugString(PREFIX .. " server informational message", 3)
    outputDebugString(PREFIX .. " server custom violet message", 4, 181, 118, 255)
    outputDebugString(PREFIX .. " UTF-8: échec contrôlé — مرحبا — こんにちは", 2)
    outputDebugString(PREFIX .. " long payload: " .. string.rep("0123456789abcdef", 100), 2)
end

local function triggerNilIndex()
    local missing = nil
    return missing.inventory.slot
end

local function triggerNativeWarning()
    setElementPosition(root, "not-a-number", 0, 0)
end

local function triggerCompileError()
    local chunk, compileError = loadstring("local broken = function( return 42 end", "@generated/broken-module.lua")
    if not chunk then
        error(compileError, 0)
    end
end

local function triggerEventError()
    triggerEvent("neonDevToolsBrokenServerEvent", resourceRoot)
end

local function triggerDatabaseCallbackError()
    local connection = dbConnect("sqlite", "neon-devtools-harness.db")
    if not connection then
        error("failed to create isolated SQLite harness database")
    end
    dbQuery(function(queryHandle)
        dbPoll(queryHandle, 0)
        local missingResult = nil
        return missingResult.account.id
    end, connection, "SELECT 1 AS ok")
end

local function triggerInterleavedErrors()
    setTimer(triggerNilIndex, 70, 3)
    setTimer(function()
        outputDebugString(PREFIX .. " interleaved warning between recurring timer failures", 2)
    end, 105, 2)
end

addEvent("neonDevToolsBrokenServerEvent", false)
addEventHandler("neonDevToolsBrokenServerEvent", resourceRoot, function()
    error("event handler exploded while processing order #NEON-42")
end)

addEvent("neonDevToolsBrokenRemoteEvent", true)
addEventHandler("neonDevToolsBrokenRemoteEvent", resourceRoot, function(payload)
    local missingRemoteState = nil
    return missingRemoteState[payload]
end)

local scenarios = {
    baseline = emitBaseline,
    nilindex = triggerNilIndex,
    warning = triggerNativeWarning,
    compile = triggerCompileError,
    event = triggerEventError,
    database = triggerDatabaseCallbackError,
    interleaved = triggerInterleavedErrors,
}

local function runScenario(name)
    local scenario = scenarios[name]
    if not scenario then
        outputDebugString(PREFIX .. " unknown server scenario: " .. tostring(name), 2)
        return
    end
    setTimer(scenario, 50, 1)
end

local function runAll()
    emitBaseline()
    setTimer(triggerNilIndex, 120, 1)
    setTimer(triggerNativeWarning, 220, 1)
    setTimer(triggerCompileError, 320, 1)
    setTimer(triggerEventError, 420, 1)
    setTimer(triggerDatabaseCallbackError, 700, 1)
    setTimer(triggerInterleavedErrors, 900, 1)

    -- The same callback and source line fires repeatedly so native duplicate
    -- coalescing and its occurrence badge are exercised by real Lua errors.
    for index = 1, 12 do
        setTimer(triggerNilIndex, 520 + index * 15, 1)
    end
end

addCommandHandler("devtest-server", function(_, scenario)
    if scenario and scenario ~= "all" then runScenario(scenario) else runAll() end
end)

addEventHandler("onResourceStart", resourceRoot, function()
    outputDebugString(PREFIX .. " headless-ready", 3)
    runAll()
end)
