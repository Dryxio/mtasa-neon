-- Keep the provisional native surface in this table and the small wrappers
-- below. If the public API changes, the conformance scenarios need no rewrite.
local API_NAMES = {
    acquire = "acquireScriptCamera",
    release = "releaseScriptCamera",
    fixed = "setScriptCameraFixed",
    move = "moveScriptCamera",
    track = "trackScriptCamera",
    persist = "setScriptCameraPersist",
    reset = "resetScriptCamera",
    fade = "fadeScriptCamera",
    fading = "isScriptCameraFading",
    moving = "isScriptCameraMoveRunning",
    tracking = "isScriptCameraTrackRunning",
    widescreen = "setScriptCameraWidescreen",
    nearClip = "setScriptCameraNearClip",
}

local function getApiFunction(key)
    local name = API_NAMES[key]
    local fn = name and _G[name]
    return type(fn) == "function" and fn or nil, name
end

local function callApi(key, ...)
    local fn, name = getApiFunction(key)
    if not fn then
        return false, ("API absente: %s"):format(tostring(name))
    end

    local ok, result = pcall(fn, ...)
    if not ok then
        return false, ("%s a leve une erreur: %s"):format(name, tostring(result))
    end
    if result == false then
        return false, ("%s a retourne false"):format(name)
    end
    return true, result
end

local function callLeaseApi(test, key, ...)
    if not test or not test.token then
        return false, "token de lease absent"
    end
    return callApi(key, test.token, ...)
end

local activeTest
local nextTestId = 0

local REQUIRED_API = {
    "acquire",
    "release",
    "fixed",
    "move",
    "track",
    "persist",
    "reset",
    "fade",
    "fading",
    "moving",
    "tracking",
    "widescreen",
    "nearClip",
}

local function log(message, kind)
    local colors = {
        good = {100, 230, 130},
        warn = {255, 180, 80},
        bad = {255, 80, 80},
        info = {210, 210, 210},
    }
    local color = colors[kind] or colors.info
    local line = ("[native camera] %s"):format(message)
    outputDebugString(line, kind == "bad" and 2 or 3)
    outputChatBox(line, color[1], color[2], color[3])
end

local function stopTimers(test)
    if not test then
        return
    end
    for timer in pairs(test.timers) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    test.timers = {}
end

local function addTimer(test, callback, interval, times)
    local timer
    timer = setTimer(function()
        if activeTest ~= test then
            return
        end
        if times == 1 then
            test.timers[timer] = nil
        end
        callback()
    end, interval, times)
    test.timers[timer] = true
    return timer
end

local function releaseLease(test)
    if not test or not test.acquired then
        return true
    end

    local ok, details = callLeaseApi(test, "release")
    test.acquired = false
    if not ok then
        log(("release refuse: %s"):format(details), "bad")
    end
    return ok
end

local function finishTest(test, result, details, release)
    if activeTest ~= test then
        return
    end

    stopTimers(test)
    if release ~= false then
        releaseLease(test)
    end
    activeTest = nil
    log(("%s after %d ms: %s"):format(result, getTickCount() - test.startedAt, details or ""), result == "PASS" and "good" or "bad")
end

local function fail(test, phase, details)
    finishTest(test, "FAIL", ("%s - %s"):format(phase, details), true)
end

local function requireApi()
    for _, key in ipairs(REQUIRED_API) do
        local _, name = getApiFunction(key)
        if not _G[name] or type(_G[name]) ~= "function" then
            return false, name
        end
    end
    return true
end

local function localOffset(origin, heading, right, forward, up)
    local radians = math.rad(heading)
    local sinHeading = math.sin(radians)
    local cosHeading = math.cos(radians)
    return Vector3(origin.x + cosHeading * right - sinHeading * forward, origin.y + sinHeading * right + cosHeading * forward, origin.z + up)
end

local function makeScene()
    local x, y, z = getElementPosition(localPlayer)
    local _, _, heading = getElementRotation(localPlayer)
    local origin = Vector3(x, y, z)
    return {
        cameraStart = localOffset(origin, heading, -7.5, -9.0, 5.0),
        cameraEnd = localOffset(origin, heading, 8.0, -5.5, 3.5),
        targetStart = localOffset(origin, heading, 0.0, 0.0, 1.0),
        targetEnd = localOffset(origin, heading, 0.0, 9.0, 1.4),
    }
end

local function newTest(mode)
    if activeTest then
        finishTest(activeTest, "ABORT", "remplace par un nouveau test", true)
    end

    nextTestId = nextTestId + 1
    local test = {
        id = nextTestId,
        mode = mode,
        startedAt = getTickCount(),
        timers = {},
        scene = makeScene(),
        acquired = false,
    }
    activeTest = test
    return test
end

local function acquireAndPrepare(test)
    local available, missing = requireApi()
    if not available then
        fail(test, "api", ("fonction absente: %s"):format(missing))
        return false
    end

    local ok, details = callApi("acquire", true)
    if not ok then
        fail(test, "acquire", details)
        return false
    end
    test.acquired = true
    test.token = details

    ok, details = callLeaseApi(test, "reset")
    if not ok then
        fail(test, "reset", details)
        return false
    end
    ok, details = callLeaseApi(test, "widescreen", true)
    if not ok then
        fail(test, "widescreen", details)
        return false
    end
    ok, details = callLeaseApi(test, "nearClip", 0.15)
    if not ok then
        fail(test, "near clip", details)
        return false
    end
    ok, details = callLeaseApi(test, "persist", true, true)
    if not ok then
        fail(test, "persist", details)
        return false
    end
    ok, details = callLeaseApi(test, "fixed", test.scene.cameraStart, test.scene.targetStart, Vector3(0, 0, 0), true)
    if not ok then
        fail(test, "fixed", details)
        return false
    end
    return true
end

local function readState(test, key)
    local fn, name = getApiFunction(key)
    if not fn then
        return nil, ("API absente: %s"):format(tostring(name))
    end
    local ok, value = pcall(fn, test.token)
    if not ok then
        return nil, ("%s a leve une erreur: %s"):format(name, tostring(value))
    end
    return value == true
end

local function waitForStates(test, label, keys, timeout, onComplete)
    local startedAt = getTickCount()
    local observed = {}
    local monitor

    monitor = addTimer(test, function()
        local allEnded = true
        for _, key in ipairs(keys) do
            local running, details = readState(test, key)
            if running == nil then
                return fail(test, label, details)
            end
            observed[key] = observed[key] or running
            allEnded = allEnded and not running
        end

        local elapsed = getTickCount() - startedAt
        if elapsed > 750 then
            for _, key in ipairs(keys) do
                if not observed[key] then
                    return fail(test, label, ("%s jamais observe actif"):format(API_NAMES[key]))
                end
            end
        end
        if allEnded and elapsed > 50 then
            for _, key in ipairs(keys) do
                if not observed[key] then
                    return
                end
            end
            if isTimer(monitor) then
                killTimer(monitor)
            end
            test.timers[monitor] = nil
            log(("%s complete en %d ms"):format(label, elapsed), "good")
            onComplete()
        elseif elapsed > timeout then
            fail(test, label, ("encore actif apres %d ms"):format(timeout))
        end
    end, 50, 0)
end

local function startFadeIn(test)
    local ok, details = callLeaseApi(test, "fade", true, 1.0, 0, 0, 0)
    if not ok then
        return fail(test, "fade in", details)
    end
    log("fade in natif lance (1.0 s)", "info")
    waitForStates(test, "fade in", {"fading"}, 3000, function()
        finishTest(test, "PASS", "fixed, move, track, fade et restauration explicite valides", true)
    end)
end

local function startFadeOut(test)
    local ok, details = callLeaseApi(test, "fade", false, 1.0, 0, 0, 0)
    if not ok then
        return fail(test, "fade out", details)
    end
    log("fade out natif lance (1.0 s)", "info")
    waitForStates(test, "fade out", {"fading"}, 3000, function()
        addTimer(test, function()
            startFadeIn(test)
        end, 250, 1)
    end)
end

local function startTravel(test)
    local ok, details = callLeaseApi(test, "move", test.scene.cameraStart, test.scene.cameraEnd, 4000, true)
    if not ok then
        return fail(test, "move", details)
    end
    ok, details = callLeaseApi(test, "track", test.scene.targetStart, test.scene.targetEnd, 4000, true)
    if not ok then
        return fail(test, "track", details)
    end

    log("vector move + track natifs lances (4.0 s, easing)", "info")
    waitForStates(test, "move + track", {"moving", "tracking"}, 6000, function()
        startFadeOut(test)
    end)
end

local function startFullTest()
    local test = newTest("full")
    if not acquireAndPrepare(test) then
        return
    end

    log("fixed/look-at actif pendant 2.0 s; controles bloques, widescreen et near clip natifs", "info")
    addTimer(test, function()
        startTravel(test)
    end, 2000, 1)
end

local function startFixedTest()
    local test = newTest("fixed")
    if not acquireAndPrepare(test) then
        return
    end
    log("fixed/look-at maintenu. Lancez /nativecamabort pour verifier la restauration.", "info")
end

local function startRestartTest()
    local test = newTest("restart")
    if not acquireAndPrepare(test) then
        return
    end

    log("lease arme pour restart: camera, widescreen, near clip et controles sont modifies", "warn")
    log("la ressource va redemarrer sans appeler releaseScriptCamera", "warn")
    triggerServerEvent("nativeScriptCameraTest:restart", resourceRoot)
end

addCommandHandler("nativecam", startFullTest)
addCommandHandler("nativecamfixed", startFixedTest)
addCommandHandler("nativecamrestart", startRestartTest)

addCommandHandler("nativecamabort", function()
    if not activeTest then
        log("aucun test actif", "warn")
        return
    end
    finishTest(activeTest, "ABORT", "release explicite; camera/near clip/widescreen/controles doivent etre restaures", true)
end)

addCommandHandler("nativecamstatus", function()
    if not activeTest then
        log("idle", "info")
        return
    end

    local moving = readState(activeTest, "moving")
    local tracking = readState(activeTest, "tracking")
    local fading = readState(activeTest, "fading")
    log(("test=%d mode=%s lease=%s move=%s track=%s fade=%s elapsed=%d ms"):format(activeTest.id, activeTest.mode,
                                                                                   tostring(activeTest.acquired), tostring(moving),
                                                                                   tostring(tracking), tostring(fading),
                                                                                   getTickCount() - activeTest.startedAt), "info")
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    log("ready: /nativecam, /nativecamfixed, /nativecamabort, /nativecamrestart, /nativecamstatus", "good")
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    -- Do not explicitly release here. The service must revoke every lease owned
    -- by a stopping resource and restore the captured state by itself.
    stopTimers(activeTest)
    activeTest = nil
end)
