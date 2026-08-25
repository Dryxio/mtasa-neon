local teardowns = {}

addEvent("storyWorldRuntime:measureVehicle", true)
addEventHandler("storyWorldRuntime:measureVehicle", resourceRoot, function(id, vehicle, model)
    local startedAt = getTickCount()
    local timer
    timer = setTimer(function()
        if not isElement(vehicle) then
            killTimer(timer)
            return triggerServerEvent("storyWorldRuntime:vehicleMeasured", resourceRoot, id, vehicle, false)
        end
        if isElementStreamedIn(vehicle) and isElementSyncer(vehicle) and getElementModel(vehicle) == tonumber(model) then
            local baseOffset = tonumber(getElementDistanceFromCentreOfMassToBaseOfModel(vehicle))
            if baseOffset and baseOffset > 0 then
                killTimer(timer)
                triggerServerEvent("storyWorldRuntime:vehicleMeasured", resourceRoot, id, vehicle, baseOffset)
            end
        elseif getTickCount() - startedAt > 10000 then
            killTimer(timer)
            triggerServerEvent("storyWorldRuntime:vehicleMeasured", resourceRoot, id, vehicle, false)
        end
    end, 50, 0)
end)

addEvent("storyWorldRuntime:verifyVehicle", true)
addEventHandler("storyWorldRuntime:verifyVehicle", resourceRoot, function(id, vehicle, expectedZ, tolerance)
    expectedZ, tolerance = tonumber(expectedZ), tonumber(tolerance) or 0.03
    local startedAt, stableSamples = getTickCount(), 0
    local timer
    timer = setTimer(function()
        if not isElement(vehicle) then
            killTimer(timer)
            return triggerServerEvent("storyWorldRuntime:vehicleVerified", resourceRoot, id, vehicle, false,
                                      "vehicle disappeared")
        end
        local _, _, z = getElementPosition(vehicle)
        if isElementStreamedIn(vehicle) and isElementSyncer(vehicle) and expectedZ and
            math.abs(z - expectedZ) <= tolerance then
            stableSamples = stableSamples + 1
            if stableSamples >= 3 then
                killTimer(timer)
                triggerServerEvent("storyWorldRuntime:vehicleVerified", resourceRoot, id, vehicle, true, nil, z)
            end
        else
            stableSamples = 0
        end
        if getTickCount() - startedAt > 10000 then
            killTimer(timer)
            triggerServerEvent("storyWorldRuntime:vehicleVerified", resourceRoot, id, vehicle, false,
                               "vehicle did not stabilize at converted SCM Z", z)
        end
    end, 50, 0)
end)

addEvent("storyWorldRuntime:prepareTeardown", true)
addEventHandler("storyWorldRuntime:prepareTeardown", resourceRoot, function(id, elements, fadeOut)
    if type(elements) ~= "table" then return end
    teardowns[id] = {elements = elements, armedAt = getTickCount()}
    fadeOut = math.max(0, math.min(3, tonumber(fadeOut) or 0))
    if fadeOut > 0 then fadeCamera(false, fadeOut) end
    setTimer(function()
        if teardowns[id] then triggerServerEvent("storyWorldRuntime:teardownArmed", resourceRoot, id) end
    end, math.max(50, math.ceil(fadeOut * 1000)), 1)
end)

addEvent("storyWorldRuntime:commitTeardown", true)
addEventHandler("storyWorldRuntime:commitTeardown", resourceRoot, function(id)
    local teardown = teardowns[id]
    if not teardown then return end
    local timer
    timer = setTimer(function()
        for _, element in ipairs(teardown.elements) do
            if isElement(element) then
                if getTickCount() - teardown.armedAt <= 10000 then return end
                killTimer(timer)
                teardowns[id] = nil
                return triggerServerEvent("storyWorldRuntime:teardownGone", resourceRoot, id, false,
                                          "mission element remained client-visible")
            end
        end
        killTimer(timer)
        teardowns[id] = nil
        triggerServerEvent("storyWorldRuntime:teardownGone", resourceRoot, id, true)
    end, 50, 0)
end)

addEventHandler("onClientResourceStop", resourceRoot, function() teardowns = {} end)
