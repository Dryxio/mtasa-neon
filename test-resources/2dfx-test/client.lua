local CUSTOM_MODEL = 1337
local VANILLA_MODEL = 1226
local suiteObjects = {}
local suiteState = {
    customBase = 0,
    vanillaBase = 0,
    vanillaDrawDistance = nil,
    customLightIndex = nil,
    customParticleIndex = nil,
    customRoadsignIndex = nil,
}

local function log(message, level)
    outputDebugString("[2dfx-test] " .. tostring(message), level or 3)
end

local function info(message)
    log(message, 3)
    outputChatBox("[2dfx-test] " .. tostring(message), 170, 220, 255)
end

local function pass(name)
    local message = "PASS: " .. name
    log(message, 3)
    outputChatBox("[2dfx-test] " .. message, 90, 255, 120)
end

local function fail(name, detail)
    local message = "FAIL: " .. name .. (detail and (" - " .. tostring(detail)) or "")
    log(message, 1)
    outputChatBox("[2dfx-test] " .. message, 255, 90, 90)
end

local function skip(name, detail)
    local message = "SKIP: " .. name .. (detail and (" - " .. tostring(detail)) or "")
    log(message, 2)
    outputChatBox("[2dfx-test] " .. message, 255, 210, 90)
end

local function check(name, condition, detail)
    if condition then
        pass(name)
        return true
    end
    fail(name, detail)
    return false
end

local function almostEqual(a, b, epsilon)
    if type(a) ~= "number" or type(b) ~= "number" then
        return false
    end
    return math.abs(a - b) <= (epsilon or 0.001)
end

local function waitUntil(predicate, callback, timeoutMs, intervalMs)
    timeoutMs = timeoutMs or 4000
    intervalMs = intervalMs or 150
    local deadline = getTickCount() + timeoutMs

    local function poll()
        local ok, result = pcall(predicate)
        if ok and result then
            callback(true)
            return
        end

        if getTickCount() >= deadline then
            callback(false)
            return
        end

        setTimer(poll, intervalMs, 1)
    end

    poll()
end

local function retryAction(action, callback, timeoutMs, intervalMs)
    waitUntil(function()
        local ok, result = pcall(action)
        return ok and result == true
    end, callback, timeoutMs, intervalMs)
end

local function lightProperties(color)
    return {
        drawDistance = 90,
        lightRange = 18,
        coronaSize = 1.6,
        shadowSize = 8,
        shadowMultiplier = 40,
        showMode = "default",
        coronaReflection = false,
        flareType = 0,
        flags = { atDay = true, atNight = true, checkObstacles = true },
        shadowDistance = 0,
        offset = { 0, 0, 0 },
        color = color or tocolor(70, 180, 255, 255),
        coronaName = "coronamoon",
        shadowName = "shad_exp",
    }
end

local function roadsignProperties()
    return {
        size = { 2.6, 1.2 },
        rotation = { 0, 0, 0 },
        flags = { lines = 2, charactersPerLine = 16 },
        color = tocolor(90, 220, 255, 255),
        text1 = "____NEON 2DFX",
        text2 = "___TEST HARNESS",
        text3 = "",
        text4 = "",
    }
end

local function destroySuiteObjects()
    for _, object in ipairs(suiteObjects) do
        if isElement(object) then
            destroyElement(object)
        end
    end
    suiteObjects = {}
end

local function createSuiteObjects()
    destroySuiteObjects()
    local x, y, z = getElementPosition(localPlayer)
    suiteObjects[#suiteObjects + 1] = createObject(CUSTOM_MODEL, x + 3.0, y + 1.0, z - 0.8)
    suiteObjects[#suiteObjects + 1] = createObject(VANILLA_MODEL, x + 7.0, y + 1.0, z - 1.0)
    for _, object in ipairs(suiteObjects) do
        if isElement(object) then
            setElementDimension(object, getElementDimension(localPlayer))
            setElementInterior(object, getElementInterior(localPlayer))
        end
    end
end

local function findVanillaLight()
    local count = getModel2DFXCount(VANILLA_MODEL, false)
    for index = 0, count - 1 do
        if getModel2DFXType(VANILLA_MODEL, index) == "light" then
            return index
        end
    end
    return nil
end

local function verifyPreviousResourceCleanup()
    local allCount = getModel2DFXCount(CUSTOM_MODEL, true)
    local nativeCount = getModel2DFXCount(CUSTOM_MODEL, false)
    check("resource-stop cleanup from previous run", allCount == nativeCount,
        string.format("all=%s native=%s", tostring(allCount), tostring(nativeCount)))
end

local function testInvalidInputs()
    local before = getModel2DFXCount(CUSTOM_MODEL, true)

    local missingFlags = lightProperties()
    missingFlags.flags = nil
    local missingFlagsResult = addModel2DFX(CUSTOM_MODEL, 0, 0, 1, "light", missingFlags)
    check("missing flags returns false instead of crashing", missingFlagsResult == false)

    local tooLongParticle = string.rep("a", 24)
    local longParticleResult = addModel2DFX(CUSTOM_MODEL, 0, 0, 1, "particle", { name = tooLongParticle })
    check("24-byte particle name rejected", longParticleResult == false)

    local after = getModel2DFXCount(CUSTOM_MODEL, true)
    check("invalid calls do not mutate model count", before == after, string.format("before=%d after=%d", before, after))
end

local function testCustomEffects(done)
    suiteState.customBase = getModel2DFXCount(CUSTOM_MODEL, true)

    local addedLight = addModel2DFX(CUSTOM_MODEL, 0, 0, 1.2, "light", lightProperties())
    check("add custom light", addedLight == true)
    suiteState.customLightIndex = getModel2DFXCount(CUSTOM_MODEL, true) - 1
    check("custom light type", getModel2DFXType(CUSTOM_MODEL, suiteState.customLightIndex) == "light")

    local originalDistance = getModel2DFXProperty(CUSTOM_MODEL, suiteState.customLightIndex, "drawDistance")
    check("set custom light property", setModel2DFXProperty(CUSTOM_MODEL, suiteState.customLightIndex, "drawDistance", 143) == true)
    check("get custom light property", almostEqual(getModel2DFXProperty(CUSTOM_MODEL, suiteState.customLightIndex, "drawDistance"), 143))
    resetModel2DFXProperty(CUSTOM_MODEL, suiteState.customLightIndex, "drawDistance")
    check("reset custom light property", almostEqual(getModel2DFXProperty(CUSTOM_MODEL, suiteState.customLightIndex, "drawDistance"), originalDistance))

    local addedParticle = addModel2DFX(CUSTOM_MODEL, 0, 0, 1.0, "particle", { name = "fire" })
    check("add custom particle", addedParticle == true)
    suiteState.customParticleIndex = getModel2DFXCount(CUSTOM_MODEL, true) - 1
    check("custom particle getter", getModel2DFXProperty(CUSTOM_MODEL, suiteState.customParticleIndex, "name") == "fire")

    -- Particle creation restreams this model. Do not immediately issue another
    -- streaming-sensitive add while GTA is still rebuilding model 1337.
    retryAction(function()
        return addModel2DFX(CUSTOM_MODEL, 0, 0, 1.8, "roadsign", roadsignProperties())
    end, function(addedRoadsign)
        check("add custom roadsign", addedRoadsign, "model did not become ready again after particle restream")

        if addedRoadsign then
            suiteState.customRoadsignIndex = getModel2DFXCount(CUSTOM_MODEL, true) - 1
            check("roadsign text getter", getModel2DFXProperty(CUSTOM_MODEL, suiteState.customRoadsignIndex, "text1") == "____NEON 2DFX")
        else
            fail("roadsign text getter", "roadsign was not created")
        end

        check("custom count increment", getModel2DFXCount(CUSTOM_MODEL, true) == suiteState.customBase + 3)
        check("native-only count unchanged", getModel2DFXCount(CUSTOM_MODEL, false) == suiteState.customBase)
        done()
    end, 5000, 200)
end

local function testVanillaOverride(done)
    suiteState.vanillaBase = getModel2DFXCount(VANILLA_MODEL, false)
    if suiteState.vanillaBase == 0 then
        skip("vanilla 2DFX checks", "model 1226 has no native 2DFX in this game state")
        done()
        return
    end

    local lightIndex = findVanillaLight()
    if lightIndex == nil then
        skip("vanilla light checks", "model 1226 has no light 2DFX")
        done()
        return
    end

    local baseline = getModel2DFXProperty(VANILLA_MODEL, lightIndex, "drawDistance")
    suiteState.vanillaDrawDistance = baseline
    check("read vanilla light", type(baseline) == "number")
    check("override vanilla property", setModel2DFXProperty(VANILLA_MODEL, lightIndex, "drawDistance", baseline + 17) == true)
    check("vanilla override visible", almostEqual(getModel2DFXProperty(VANILLA_MODEL, lightIndex, "drawDistance"), baseline + 17))

    resetModel2DFXProperty(VANILLA_MODEL, lightIndex, "drawDistance")
    check("vanilla property reset", almostEqual(getModel2DFXProperty(VANILLA_MODEL, lightIndex, "drawDistance"), baseline))

    local removed = removeModel2DFX(VANILLA_MODEL, lightIndex)
    check("remove vanilla effect", removed == true and getModel2DFXType(VANILLA_MODEL, lightIndex) == "unknown")

    -- removeModel2DFX restreams the model. Give that targeted restream time to
    -- settle before restoring, then wait for the restored type to become live.
    setTimer(function()
        retryAction(function()
            return restoreModel2DFX(VANILLA_MODEL, lightIndex)
        end, function(restored)
            if not restored then
                fail("restore vanilla effect", "restore call never succeeded after model restream")
                done()
                return
            end

            waitUntil(function()
                return getModel2DFXType(VANILLA_MODEL, lightIndex) == "light"
            end, function(typeRestored)
                check("restore vanilla effect", typeRestored, "effect stayed unavailable after restore")
                done()
            end, 5000, 200)
        end, 5000, 200)
    end, 600, 1)
end

local function finishSuite()
    info("suite complete; custom effects are intentionally left owned by this resource")
    info("restart this resource to verify automatic resource-stop cleanup")
    info("global restream is NOT run automatically; use /2dfxrestream or /2dfxstress")
end

local function runSuite()
    info("starting 2DFX regression suite")
    verifyPreviousResourceCleanup()
    testInvalidInputs()
    testCustomEffects(function()
        testVanillaOverride(finishSuite)
    end)
end

local function runRestreamOnce()
    local expectedCount = getModel2DFXCount(CUSTOM_MODEL, true)
    local lightIndex = findVanillaLight()
    local baseline = lightIndex and getModel2DFXProperty(VANILLA_MODEL, lightIndex, "drawDistance") or nil

    if lightIndex and type(baseline) == "number" then
        check("prepare vanilla override for restream", setModel2DFXProperty(VANILLA_MODEL, lightIndex, "drawDistance", baseline + 17) == true)
    end

    info("running one engineRestreamWorld now; a noticeable hitch is expected for this MANUAL test")
    engineRestreamWorld()

    setTimer(function()
        check("global restream custom count stable", getModel2DFXCount(CUSTOM_MODEL, true) == expectedCount,
            string.format("expected=%d actual=%d", expectedCount, getModel2DFXCount(CUSTOM_MODEL, true)))
        if suiteState.customLightIndex then
            check("global restream custom light type", getModel2DFXType(CUSTOM_MODEL, suiteState.customLightIndex) == "light")
        end
        if lightIndex and type(baseline) == "number" then
            check("vanilla override survives engineRestreamWorld", almostEqual(getModel2DFXProperty(VANILLA_MODEL, lightIndex, "drawDistance"), baseline + 17))
            resetModel2DFXProperty(VANILLA_MODEL, lightIndex, "drawDistance")
        else
            skip("vanilla override survives engineRestreamWorld", "no vanilla light available")
        end
        info("single global restream test finished")
    end, 900, 1)
end

local function runStress(iterations)
    iterations = math.max(1, math.min(tonumber(iterations) or 10, 50))
    local expectedCount = getModel2DFXCount(CUSTOM_MODEL, true)
    local completed = 0

    info(string.format("starting %d MANUAL engineRestreamWorld cycles; repeated hitches are expected", iterations))

    local function cycle()
        engineRestreamWorld()
        completed = completed + 1
        if completed >= iterations then
            setTimer(function()
                check("restream stress count stable", getModel2DFXCount(CUSTOM_MODEL, true) == expectedCount,
                    string.format("expected=%d actual=%d", expectedCount, getModel2DFXCount(CUSTOM_MODEL, true)))
                if suiteState.customLightIndex then
                    check("restream stress custom light type", getModel2DFXType(CUSTOM_MODEL, suiteState.customLightIndex) == "light")
                end
                info(string.format("restream stress finished (%d cycles)", iterations))
            end, 900, 1)
            return
        end
        setTimer(cycle, 1000, 1)
    end

    cycle()
end

local function cleanup()
    resetModel2DFXEffects(CUSTOM_MODEL)
    resetModel2DFXEffects(VANILLA_MODEL)
    destroySuiteObjects()
    setTimer(function()
        check("explicit cleanup custom count", getModel2DFXCount(CUSTOM_MODEL, true) == getModel2DFXCount(CUSTOM_MODEL, false))
    end, 500, 1)
end

addCommandHandler("2dfxtest", function()
    createSuiteObjects()
    setTimer(runSuite, 900, 1)
end)

addCommandHandler("2dfxrestream", runRestreamOnce)

addCommandHandler("2dfxstress", function(_, iterations)
    runStress(iterations)
end)

addCommandHandler("2dfxcleanup", cleanup)

addEventHandler("onClientResourceStart", resourceRoot, function()
    createSuiteObjects()
    setTimer(runSuite, 900, 1)
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    destroySuiteObjects()
end)
