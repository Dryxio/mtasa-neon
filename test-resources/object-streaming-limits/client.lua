local testObjects = {}
local savedLimits
local testTimer

local function countStreamedObjects()
    local count = 0

    for _, object in ipairs(testObjects) do
        if isElement(object) and isElementStreamedIn(object) then
            count = count + 1
        end
    end

    return count
end

local function cleanupTest()
    if isTimer(testTimer) then
        killTimer(testTimer)
    end
    testTimer = nil

    for _, object in ipairs(testObjects) do
        if isElement(object) then
            destroyElement(object)
        end
    end
    testObjects = {}

    if savedLimits then
        engineSetObjectStreamingLimits(savedLimits.objects, savedLimits.lowLodObjects)
        savedLimits = nil
    end
end

local function runObjectStreamingLimitsTest()
    cleanupTest()

    local objects, lowLodObjects, hardMaximum = engineGetObjectStreamingLimits()
    savedLimits = {objects = objects, lowLodObjects = lowLodObjects}

    local invalidAccepted = pcall(engineSetObjectStreamingLimits, hardMaximum + 1, 0)
    if invalidAccepted then
        outputDebugString("[Object streaming limits test] FAIL: a limit above the physical maximum was accepted", 1)
        cleanupTest()
        return
    end

    engineSetObjectStreamingLimits(hardMaximum, 0)
    local maximumObjects, maximumLowLodObjects = engineGetObjectStreamingLimits()
    if maximumObjects ~= hardMaximum or maximumLowLodObjects ~= 0 then
        outputDebugString("[Object streaming limits test] FAIL: the full normal-object budget was not accepted", 1)
        cleanupTest()
        return
    end

    engineSetObjectStreamingLimits(12, 0)

    local x, y, z = getElementPosition(localPlayer)
    for index = 1, 12 do
        local object = createObject(1337, x + (index % 4) * 2, y + math.floor((index - 1) / 4) * 2 + 6, z)
        if object then
            testObjects[#testObjects + 1] = object
        end
    end

    testTimer = setTimer(function()
        local beforeReduction = countStreamedObjects()
        engineSetObjectStreamingLimits(4, 0)

        testTimer = setTimer(function()
            local afterReduction = countStreamedObjects()
            local currentObjects, currentLowLodObjects, currentHardMaximum = engineGetObjectStreamingLimits()
            local passed = beforeReduction > 4 and afterReduction <= 4 and currentObjects == 4 and currentLowLodObjects == 0
                and currentHardMaximum == hardMaximum

            outputDebugString(
                ("[Object streaming limits test] %s: streamed %d before reduction and %d after reduction; limits %d/%d (hard max %d)"):format(
                    passed and "PASS" or "FAIL",
                    beforeReduction,
                    afterReduction,
                    currentObjects,
                    currentLowLodObjects,
                    currentHardMaximum
                ),
                passed and 3 or 1
            )
            cleanupTest()
        end, 1500, 1)
    end, 1500, 1)
end

addCommandHandler("testobjectstreamlimits", runObjectStreamingLimitsTest)
addEventHandler("onClientResourceStop", resourceRoot, cleanupTest)

outputChatBox("[Object streaming limits test] Use /testobjectstreamlimits to run the test.", 80, 255, 160)
