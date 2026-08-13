local loaderPath = "test-resources/samp-map-loader/loader.lua"

local function expect(condition, message)
    if not condition then
        error(message or "expectation failed", 2)
    end
end

local function expectEqual(actual, expected, message)
    if actual ~= expected then
        error((message or "values differ") .. (": expected %s, got %s"):format(tostring(expected), tostring(actual)), 2)
    end
end

local function makeElement(kind, id)
    return {kind = kind, id = id, valid = true}
end

local function installMocks(configuration)
    configuration = configuration or {}
    local calls = {
        requestedModels = {},
        createdObjects = {},
        materials = {},
        destroyed = {},
        freedModels = {},
        removedBuildings = {},
        restoredBuildings = {},
    }
    local nextRuntimeModel = 21000

    function fileOpen()
        return {valid = true}
    end

    function fileGetSize()
        return 4
    end

    function fileRead()
        return "test"
    end

    function fileClose()
        return true
    end

    function engineParseSAMPMap()
        return configuration.definition
    end

    function resolveSAMPModel(sourceModel)
        if configuration.resolveFails or configuration.resolveFailsFor == sourceModel then
            return false, "mock resolver failure"
        end
        local runtimeModel = nextRuntimeModel
        nextRuntimeModel = nextRuntimeModel + 1
        calls.requestedModels[#calls.requestedModels + 1] = runtimeModel
        return runtimeModel, {sourceModel = sourceModel}
    end

    function releaseSAMPModel(sourceModel, runtimeModel)
        calls.freedModels[#calls.freedModels + 1] = {sourceModel = sourceModel, runtimeModel = runtimeModel}
        return true
    end

    function createObject(model)
        if configuration.objectFails then
            return false
        end
        local object = makeElement("object", model)
        calls.createdObjects[#calls.createdObjects + 1] = object
        return object
    end

    function setElementInterior()
        return true
    end

    function setElementDimension()
        return true
    end

    function setObjectMaterial(object, slot, sourceModel)
        calls.materials[#calls.materials + 1] = {object = object, slot = slot, sourceModel = sourceModel}
        return not configuration.materialFails
    end

    function removeWorldModel(model)
        calls.removedBuildings[#calls.removedBuildings + 1] = model
        return not configuration.removalFails
    end

    function restoreWorldModel(model)
        calls.restoredBuildings[#calls.restoredBuildings + 1] = model
        return true
    end

    function isElement(element)
        return type(element) == "table" and element.valid == true
    end

    function destroyElement(element)
        element.valid = false
        calls.destroyed[#calls.destroyed + 1] = element
        return true
    end

    dofile(loaderPath)
    return calls
end

local function validDefinition(model, materialSource)
    return {
        success = true,
        diagnostics = {},
        objects = {
            {
                model = model,
                x = 1,
                y = 2,
                z = 3,
                rx = 4,
                ry = 5,
                rz = 6,
                world = -1,
                interior = -1,
                line = 10,
                column = 4,
                materials = {
                    {
                        slot = 0,
                        sourceModel = materialSource,
                        txd = "test",
                        texture = "white",
                        color = 0,
                        line = 11,
                        column = 4,
                    },
                },
            },
        },
        removedBuildings = {
            {model = 1307, radius = 0.25, x = 1, y = 2, z = 3, line = 12, column = 4},
        },
    }
end

local tests = {}

function tests.parseFailureCreatesNoRuntimeState()
    local calls = installMocks({
        definition = {
            success = false,
            diagnostics = {{severity = "error", message = "broken", line = 7, column = 9}},
            objects = {},
            removedBuildings = {},
        },
    })

    local handle, diagnostics = loadSAMPMap("broken.pwn")
    expectEqual(handle, false)
    expectEqual(#diagnostics, 1)
    expectEqual(diagnostics[1].stage, "parse")
    expectEqual(#calls.requestedModels, 0)
    expectEqual(#calls.createdObjects, 0)
end

function tests.preflightIsAtomicWhenAnExtendedModelCannotBeResolved()
    local calls = installMocks({definition = validDefinition(19445, 18646), resolveFails = true})

    local handle, diagnostics = loadSAMPMap("missing-assets.pwn")
    expectEqual(handle, false)
    expectEqual(#diagnostics, 1)
    expectEqual(diagnostics[1].stage, "preflight")
    expectEqual(#calls.requestedModels, 0)
    expectEqual(#calls.createdObjects, 0)
end

function tests.partialResolverFailureReleasesEarlierReferencesBeforeWorldMutation()
    local calls = installMocks({definition = validDefinition(19445, 18646), resolveFailsFor = 19445})

    local handle, diagnostics = loadSAMPMap("partial-resolve.pwn")
    expectEqual(handle, false)
    expectEqual(diagnostics[#diagnostics].code, "model_resolve_failed")
    expectEqual(#calls.requestedModels, 1)
    expectEqual(#calls.freedModels, 1)
    expectEqual(calls.freedModels[1].sourceModel, 18646)
    expectEqual(#calls.createdObjects, 0)
    expectEqual(#calls.removedBuildings, 0)
end

function tests.lowSAMPRangeUsesResolverAndAdjacentVanillaModelsStayDirect()
    local lowCalls = installMocks({definition = validDefinition(11682, 11683)})
    local lowHandle = loadSAMPMap("low-range.pwn")
    expect(type(lowHandle) == "table")
    expectEqual(#lowCalls.requestedModels, 2)
    expectEqual(lowCalls.createdObjects[1].id, lowHandle.modelRemap[11682])
    expectEqual(lowCalls.materials[1].sourceModel, lowHandle.modelRemap[11683])
    unloadSAMPMap(lowHandle)

    local vanillaCalls = installMocks({definition = validDefinition(11681, 18630)})
    local vanillaHandle = loadSAMPMap("vanilla.pwn")
    expect(type(vanillaHandle) == "table")
    expectEqual(#vanillaCalls.requestedModels, 0)
    expectEqual(vanillaCalls.createdObjects[1].id, 11681)
    expectEqual(vanillaCalls.materials[1].sourceModel, 18630)
    unloadSAMPMap(vanillaHandle)
end

function tests.extendedObjectAndMaterialModelsAreRemapped()
    local calls = installMocks({definition = validDefinition(19445, 18646)})

    local handle, diagnostics = loadSAMPMap("valid.pwn")
    expect(type(handle) == "table", diagnostics[1] and diagnostics[1].message)
    expectEqual(handle.remappedModelCount, 2)
    expectEqual(calls.createdObjects[1].id, handle.modelRemap[19445])
    expectEqual(calls.materials[1].sourceModel, handle.modelRemap[18646])
    expect(handle.modelRemap[19445] ~= 19445)
    expect(handle.modelRemap[18646] ~= 18646)

    expect(unloadSAMPMap(handle))
    expectEqual(#calls.destroyed, 1)
    expectEqual(#calls.restoredBuildings, 1)
    expectEqual(#calls.freedModels, 2)
    expectEqual(unloadSAMPMap(handle), true)
    expectEqual(#calls.freedModels, 2)
end

function tests.materialFailureRollsBackEverything()
    local calls = installMocks({definition = validDefinition(19445, 1337), materialFails = true})

    local handle, diagnostics = loadSAMPMap("material-failure.pwn")
    expectEqual(handle, false)
    expectEqual(diagnostics[#diagnostics].code, "material_apply_failed")
    expectEqual(#calls.createdObjects, 1)
    expectEqual(calls.createdObjects[1].valid, false)
    expectEqual(#calls.freedModels, 1)
    expectEqual(#calls.removedBuildings, 0)
end

local passed = 0
for name, test in pairs(tests) do
    test()
    passed = passed + 1
    io.write(("ok - %s\n"):format(name))
end
io.write(("%d loader tests passed\n"):format(passed))
