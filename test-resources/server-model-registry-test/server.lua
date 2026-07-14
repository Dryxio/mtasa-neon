local TEST_POSITION = Vector3(2492, -1668, 13.35)

local vehicleModel
local objectModel
local pedModel
local testVehicle
local testObject
local testPed
local testBuilding
local testPickup

local function publishModelIds()
    setElementData(resourceRoot, "serverModel.vehicle", vehicleModel or false)
    setElementData(resourceRoot, "serverModel.object", objectModel or false)
    setElementData(resourceRoot, "serverModel.ped", pedModel or false)
end

local function createTestElements()
    vehicleModel = engineRequestModel("vehicle", 411, "registry-test-vehicle")
    objectModel = engineRequestModel("object", 1337, "registry-test-object")
    pedModel = engineRequestModel("ped", 7, "registry-test-ped")

    assert(vehicleModel, "Unable to allocate the test vehicle model")
    assert(objectModel, "Unable to allocate the test object model")
    assert(pedModel, "Unable to allocate the test ped model")
    assert(engineGetModelType(vehicleModel) == "vehicle", "Vehicle introspection type mismatch")
    assert(engineGetModelType(objectModel) == "object", "Object introspection type mismatch")
    assert(engineGetModelType(pedModel) == "ped", "Ped introspection type mismatch")
    assert(engineGetModelIDFromName("registry-test-object") == objectModel, "Resource-local name lookup failed")
    assert(engineIsModelAllocated(objectModel), "Allocated model was not reported by introspection")
    assert(setModelHandling(vehicleModel, "mass", 2250), "Unable to set custom model handling")

    testVehicle = createVehicle(vehicleModel, TEST_POSITION, Vector3(0, 0, 90))
    testObject = createObject(objectModel, TEST_POSITION + Vector3(4, 0, 0))
    testPed = createPed(pedModel, TEST_POSITION + Vector3(8, 0, 0), 180)
    testBuilding = createBuilding(objectModel, TEST_POSITION.x + 12, TEST_POSITION.y, TEST_POSITION.z)
    testPickup = createPickup(TEST_POSITION.x + 16, TEST_POSITION.y, TEST_POSITION.z, 3, objectModel)
    assert(testVehicle and testObject and testPed and testBuilding and testPickup, "Unable to create server-model test elements")

    setElementData(testVehicle, "serverModel.logical", vehicleModel)
    setElementData(testObject, "serverModel.logical", objectModel)
    setElementData(testPed, "serverModel.logical", pedModel)
    setElementData(testBuilding, "serverModel.logical", objectModel)
    setElementData(testPickup, "serverModel.logical", objectModel)
    publishModelIds()

    outputServerLog(("[server-model-test] vehicle=%d parent=%d object=%d parent=%d ped=%d parent=%d available=%d"):format(
        vehicleModel,
        engineGetModelParent(vehicleModel),
        objectModel,
        engineGetModelParent(objectModel),
        pedModel,
        engineGetModelParent(pedModel),
        engineGetModelAvailableCount()
    ))
end

addEventHandler("onResourceStart", resourceRoot, createTestElements)

addCommandHandler("servermodeltest", function(player)
    if isElement(player) and pedModel then
        setElementPosition(player, TEST_POSITION + Vector3(0, -4, 1))
        if getElementModel(player) ~= pedModel then
            assert(setElementModel(player, pedModel), "Unable to apply the logical ped model to the player")
        end
    end

    local mass = vehicleModel and getModelHandling(vehicleModel).mass or false
    outputChatBox(("Server models: vehicle=%s object=%s ped=%s building=%s pickup=%s player model=%s custom mass=%s"):format(
        tostring(vehicleModel),
        tostring(objectModel),
        tostring(pedModel),
        tostring(isElement(testBuilding) and getElementModel(testBuilding) or false),
        tostring(isElement(testPickup) and getElementModel(testPickup) or false),
        tostring(isElement(player) and getElementModel(player) or false),
        tostring(mass)
    ), player, 80, 220, 255)
end)

addCommandHandler("servermodelspawn", function(player)
    if not isElement(player) then
        return
    end

    local spawned = pedModel and spawnPlayer(player, TEST_POSITION + Vector3(0, -4, 1), 0, pedModel) or false
    outputChatBox(("Spawned with logical ped=%s result=%s element model=%s"):format(
        tostring(pedModel),
        tostring(spawned),
        tostring(getElementModel(player))
    ), player, 80, 220, 255)
end)

addCommandHandler("servermodelfree", function(player)
    local vehicleFreed = vehicleModel and engineFreeModel(vehicleModel) or false
    local objectFreed = objectModel and engineFreeModel(objectModel) or false
    local pedFreed = pedModel and engineFreeModel(pedModel) or false
    local objectStillAllocated = objectModel and engineIsModelAllocated(objectModel) or false

    outputChatBox(("Freed: vehicle=%s object=%s ped=%s objectStillAllocated=%s; survivors use 411/1337/7"):format(
        tostring(vehicleFreed),
        tostring(objectFreed),
        tostring(pedFreed),
        tostring(objectStillAllocated)
    ), player, 255, 190, 80)

    vehicleModel = false
    objectModel = false
    pedModel = false
    publishModelIds()
end)
