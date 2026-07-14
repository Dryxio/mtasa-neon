local function findTestElement(elementType, logicalModel)
    for _, element in ipairs(getElementsByType(elementType, resourceRoot, true)) do
        if getElementData(element, "serverModel.logical") == logicalModel then
            return element
        end
    end
end

local function describeModel(label, elementType, logicalModel)
    if not logicalModel then
        outputChatBox(label .. ": no active logical model", 255, 190, 80)
        return
    end

    local runtimeModel = engineGetModelRuntimeID(logicalModel)
    local roundTripModel = runtimeModel and engineGetModelServerID(runtimeModel) or false
    local element = findTestElement(elementType, logicalModel)
    local exposedModel = element and getElementModel(element) or false
    local directLod = engineGetModelLODDistance(logicalModel)
    outputChatBox(("%s: server=%s runtime=%s reverse=%s element=%s lod=%s"):format(
        label,
        tostring(logicalModel),
        tostring(runtimeModel),
        tostring(roundTripModel),
        tostring(exposedModel),
        tostring(directLod)
    ), 80, 220, 255)
end

local function reportMappings()
    local vehicleModel = getElementData(resourceRoot, "serverModel.vehicle")
    local objectModel = getElementData(resourceRoot, "serverModel.object")
    local pedModel = getElementData(resourceRoot, "serverModel.ped")
    describeModel("vehicle", "vehicle", vehicleModel)
    describeModel("object", "object", objectModel)
    describeModel("ped", "ped", pedModel)
    describeModel("building", "building", objectModel)
    describeModel("pickup", "pickup", objectModel)

    if objectModel then
        local name = engineGetModelNameFromID(objectModel)
        outputChatBox(("metadata: name=%s idByName=%s parent=%s type=%s"):format(
            tostring(name),
            tostring(name and engineGetModelIDFromName(name) or false),
            tostring(engineGetModelParent(objectModel)),
            tostring(engineGetModelType(objectModel))
        ), 180, 220, 255)
    end
end

-- Report the public IDs emitted by model changes. Runtime IDs here would show
-- that an engine slot leaked through the Lua boundary.
addEventHandler("onClientElementModelChange", root, function(oldModel, newModel)
    if getElementData(source, "serverModel.logical") then
        outputChatBox(("model event: old=%s new=%s exposed=%s"):format(
            tostring(oldModel),
            tostring(newModel),
            tostring(getElementModel(source))
        ), 120, 255, 180)
    end
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    setTimer(reportMappings, 500, 1)
end)

addCommandHandler("servermodelclient", reportMappings)

addCommandHandler("servermodellogicaltest", function()
    local logicalModel = getElementData(resourceRoot, "serverModel.object")
    local object = logicalModel and findTestElement("object", logicalModel)
    if not object then
        outputChatBox("logical test: object not found", 255, 80, 80)
        return
    end

    local originalLod = engineGetModelLODDistance(logicalModel)
    local lodSet = originalLod and engineSetModelLODDistance(logicalModel, originalLod + 1) or false
    local changedLod = engineGetModelLODDistance(logicalModel)
    if lodSet then
        engineSetModelLODDistance(logicalModel, originalLod)
    end

    local parentSet = setElementModel(object, 1337)
    local logicalSet = setElementModel(object, logicalModel)
    outputChatBox(("logical test: parentSet=%s logicalSet=%s exposed=%s runtime=%s lod=%s->%s"):format(
        tostring(parentSet),
        tostring(logicalSet),
        tostring(getElementModel(object)),
        tostring(engineGetModelRuntimeID(logicalModel)),
        tostring(originalLod),
        tostring(changedLod)
    ), 120, 255, 180)
end)

addCommandHandler("servermodellocaltest", function()
    local vehicleModel = getElementData(resourceRoot, "serverModel.vehicle")
    local objectModel = getElementData(resourceRoot, "serverModel.object")
    local pedModel = getElementData(resourceRoot, "serverModel.ped")
    if not (vehicleModel and objectModel and pedModel) then
        outputChatBox("local test: models are not allocated", 255, 80, 80)
        return
    end

    local x, y, z = getElementPosition(localPlayer)
    local position = Vector3(x, y + 5, z)
    local elements = {
        createVehicle(vehicleModel, position),
        createObject(objectModel, position + Vector3(3, 0, 0)),
        createPed(pedModel, position + Vector3(6, 0, 0)),
        createBuilding(objectModel, position.x + 9, position.y, position.z),
        createPickup(position.x + 12, position.y, position.z, 3, objectModel),
    }

    local results = {}
    for index = 1, 5 do
        local element = elements[index]
        results[index] = tostring(isElement(element) and getElementModel(element) or false)
    end
    outputChatBox(("local logical creators: vehicle=%s object=%s ped=%s building=%s pickup=%s"):format(unpack(results)), 120, 255, 180)

    setTimer(function()
        for _, element in ipairs(elements) do
            if isElement(element) then
                destroyElement(element)
            end
        end
    end, 5000, 1)
end)
