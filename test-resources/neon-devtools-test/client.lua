local PREFIX = "[NEON_DEVTOOLS_TEST]"

local function emitBaseline()
    outputDebugString(PREFIX .. " client debug message", 0, 103, 207, 255)
    outputDebugString(PREFIX .. " client informational message", 3)
    outputDebugString(PREFIX .. " client custom pink message", 4, 255, 113, 188)
end

local function triggerNilCall()
    local missingFunction = nil
    return missingFunction("client payload")
end

local function triggerNativeWarning()
    dxDrawText({}, 20, 20)
end

local function triggerCompileError()
    local chunk, compileError = loadstring("if true print('missing then') end", "@generated/client-broken.lua")
    if not chunk then error(compileError, 0) end
end

local function triggerEventError()
    triggerEvent("neonDevToolsBrokenClientEvent", resourceRoot)
end

local function triggerRemoteChain()
    triggerServerEvent("neonDevToolsBrokenRemoteEvent", resourceRoot, "client-checkpoint")
end

addEvent("neonDevToolsBrokenClientEvent", false)
addEventHandler("neonDevToolsBrokenClientEvent", resourceRoot, function()
    local object = false
    return object.position.x
end)

local scenarios = {
    baseline = emitBaseline,
    nilcall = triggerNilCall,
    warning = triggerNativeWarning,
    compile = triggerCompileError,
    event = triggerEventError,
    remote = triggerRemoteChain,
}

local function runAll()
    emitBaseline()
    setTimer(triggerNilCall, 150, 1)
    setTimer(triggerNativeWarning, 250, 1)
    setTimer(triggerCompileError, 350, 1)
    setTimer(triggerEventError, 450, 1)
    setTimer(triggerRemoteChain, 750, 1)
    for index = 1, 12 do setTimer(triggerNilCall, 550 + index * 15, 1) end
end

addCommandHandler("devtest-client", function(_, scenario)
    local action = scenarios[scenario or ""]
    if action then setTimer(action, 50, 1) else runAll() end
end)

addEventHandler("onClientResourceStart", resourceRoot, runAll)
