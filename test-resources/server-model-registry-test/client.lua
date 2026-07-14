local function describeModel(label, logicalModel)
    if not logicalModel then
        outputChatBox(label .. ": no active logical model", 255, 190, 80)
        return
    end

    local runtimeModel = engineGetModelRuntimeID(logicalModel)
    local roundTripModel = runtimeModel and engineGetModelServerID(runtimeModel) or false
    outputChatBox(("%s: server=%s runtime=%s reverse=%s"):format(
        label,
        tostring(logicalModel),
        tostring(runtimeModel),
        tostring(roundTripModel)
    ), 80, 220, 255)
end

local function reportMappings()
    describeModel("vehicle", getElementData(resourceRoot, "serverModel.vehicle"))
    describeModel("object", getElementData(resourceRoot, "serverModel.object"))
end

addEventHandler("onClientResourceStart", resourceRoot, function()
    setTimer(reportMappings, 500, 1)
end)

addCommandHandler("servermodelclient", reportMappings)
