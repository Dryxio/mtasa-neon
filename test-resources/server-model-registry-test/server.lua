local TEST_POSITION = Vector3(2492, -1668, 13.35)

local vehicleModel
local objectModel
local testVehicle
local testObject

local function publishModelIds()
    setElementData(resourceRoot, "serverModel.vehicle", vehicleModel or false)
    setElementData(resourceRoot, "serverModel.object", objectModel or false)
end

local function createTestElements()
    vehicleModel = engineRequestModel("vehicle", 411, "registry-test-vehicle")
    objectModel = engineRequestModel("object", 1337, "registry-test-object")

    assert(vehicleModel, "Unable to allocate the test vehicle model")
    assert(objectModel, "Unable to allocate the test object model")
    assert(setModelHandling(vehicleModel, "mass", 2250), "Unable to set custom model handling")

    testVehicle = createVehicle(vehicleModel, TEST_POSITION, Vector3(0, 0, 90))
    testObject = createObject(objectModel, TEST_POSITION + Vector3(4, 0, 0))
    assert(testVehicle and testObject, "Unable to create server-model test elements")

    setElementData(testVehicle, "serverModel.logical", vehicleModel)
    setElementData(testObject, "serverModel.logical", objectModel)
    publishModelIds()

    outputServerLog(("[server-model-test] vehicle=%d parent=%d object=%d parent=%d"):format(
        vehicleModel,
        engineGetModelParent(vehicleModel),
        objectModel,
        engineGetModelParent(objectModel)
    ))
end

addEventHandler("onResourceStart", resourceRoot, createTestElements)

addCommandHandler("servermodeltest", function(player)
    if isElement(player) then
        setElementPosition(player, TEST_POSITION + Vector3(0, -4, 1))
    end

    local mass = vehicleModel and getModelHandling(vehicleModel).mass or false
    outputChatBox(("Server models: vehicle=%s object=%s custom mass=%s"):format(
        tostring(vehicleModel),
        tostring(objectModel),
        tostring(mass)
    ), player, 80, 220, 255)
end)

addCommandHandler("servermodelfree", function(player)
    local vehicleFreed = vehicleModel and engineFreeModel(vehicleModel) or false
    local objectFreed = objectModel and engineFreeModel(objectModel) or false

    outputChatBox(("Freed: vehicle=%s object=%s; surviving elements must now use parents 411/1337"):format(
        tostring(vehicleFreed),
        tostring(objectFreed)
    ), player, 255, 190, 80)

    vehicleModel = false
    objectModel = false
    publishModelIds()
end)
