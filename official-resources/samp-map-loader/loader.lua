local function addDiagnostic(diagnostics, severity, stage, message, line, column, code)
    diagnostics[#diagnostics + 1] = {
        severity = severity,
        stage = stage,
        message = message,
        line = line or 0,
        column = column or 0,
        code = code or stage,
    }
end

local function writeLog(state, message, level)
    local logger = state and state.logger
    if type(logger) == "function" then
        logger(message, level or 3)
    elseif type(outputDebugString) == "function" then
        outputDebugString("[SAMP Loader] " .. message, level or 3)
    end
end

local function requiresSAMPResolver(model)
    return (model >= 11682 and model <= 11753) or (model >= 18631 and model <= 19999)
end

local function readSource(path)
    local file = fileOpen(path, true)
    if not file then
        return false, "cannot open " .. path
    end

    local size = fileGetSize(file)
    local source = fileRead(file, size)
    fileClose(file)
    if not source or #source ~= size then
        return false, ("short read for %s (%d/%d bytes)"):format(path, source and #source or 0, size)
    end
    return source
end

local function appendParserDiagnostics(diagnostics, result)
    for _, diagnostic in ipairs(result.diagnostics) do
        addDiagnostic(
            diagnostics,
            diagnostic.severity,
            "parse",
            diagnostic.message,
            diagnostic.line,
            diagnostic.column,
            "parse"
        )
    end
end

local function sortedModelReferences(modelSet)
    local modelReferences = {}
    for _, reference in pairs(modelSet) do
        modelReferences[#modelReferences + 1] = reference
    end
    table.sort(modelReferences, function(left, right)
        return left.model < right.model
    end)
    return modelReferences
end

local function collectRequiredModels(definition)
    local models = {}
    for _, object in ipairs(definition.objects) do
        if requiresSAMPResolver(object.model) then
            models[object.model] = models[object.model] or {
                model = object.model,
                usage = "object",
                line = object.line,
                column = object.column,
            }
        end
        for _, material in ipairs(object.materials) do
            if material.sourceModel > 0 and requiresSAMPResolver(material.sourceModel) then
                models[material.sourceModel] = models[material.sourceModel] or {
                    model = material.sourceModel,
                    usage = "material",
                    line = material.line,
                    column = material.column,
                }
            end
        end
    end
    return sortedModelReferences(models)
end

local function destroyIfElement(element)
    if element and isElement(element) then
        destroyElement(element)
    end
end

local function releaseObjects(objects)
    for index = #objects, 1, -1 do
        destroyIfElement(objects[index])
    end
end

local function restoreBuildings(removals)
    for index = #removals, 1, -1 do
        local removal = removals[index]
        restoreWorldModel(removal.model, removal.radius, removal.x, removal.y, removal.z, removal.interior)
    end
end

local function releaseResolvedModels(models, releaseModel)
    if not releaseModel then
        return
    end
    for index = #models, 1, -1 do
        local model = models[index]
        pcall(releaseModel, model.sourceModel, model.runtimeModel, model.handle)
    end
end

local function releaseState(state)
    if state.released then
        return
    end

    -- Destroy instances before returning resolver references. Dynamic model
    -- implementations may free native DFF/COL data on the final release.
    releaseObjects(state.objects)
    restoreBuildings(state.removals)
    releaseResolvedModels(state.resolvedModels, state.releaseModel)
    state.objects = {}
    state.removals = {}
    state.resolvedModels = {}
    state.modelRemap = {}
    state.definition = nil
    state.released = true
end

local function fail(state, diagnostics, stage, message, line, column, code)
    addDiagnostic(diagnostics, "error", stage, message, line, column, code)
    writeLog(state, ("%s failed: %s"):format(stage, message), 1)
    releaseState(state)
    return false, diagnostics
end

local function resolveRequiredModels(modelReferences, options, state, diagnostics)
    local resolver = options.resolveModel or resolveSAMPModel
    if type(resolver) ~= "function" then
        return fail(
            state,
            diagnostics,
            "preflight",
            "SA-MP model resolver is unavailable",
            0,
            0,
            "model_resolver_unavailable"
        )
    end

    state.releaseModel = options.releaseModel
    if state.releaseModel == nil and type(releaseSAMPModel) == "function" then
        state.releaseModel = releaseSAMPModel
    end

    for _, reference in ipairs(modelReferences) do
        local sourceModel = reference.model
        local called, runtimeModel, handleOrReason, reason = pcall(resolver, sourceModel)
        if not called then
            return fail(
                state,
                diagnostics,
                "preflight",
                ("resolver raised for SA-MP model %d: %s"):format(sourceModel, tostring(runtimeModel)),
                reference.line,
                reference.column,
                "model_resolver_error"
            )
        end
        if not runtimeModel then
            local message = reason or handleOrReason or "unknown resolver failure"
            return fail(
                state,
                diagnostics,
                "preflight",
                ("cannot resolve SA-MP %s model %d: %s"):format(reference.usage, sourceModel, tostring(message)),
                reference.line,
                reference.column,
                "model_resolve_failed"
            )
        end
        if type(runtimeModel) ~= "number" then
            return fail(
                state,
                diagnostics,
                "preflight",
                ("resolver returned an invalid runtime ID for SA-MP model %d"):format(sourceModel),
                reference.line,
                reference.column,
                "invalid_runtime_model"
            )
        end

        local model = {
            sourceModel = sourceModel,
            runtimeModel = runtimeModel,
            handle = handleOrReason,
        }
        state.resolvedModels[#state.resolvedModels + 1] = model
        state.modelRemap[sourceModel] = runtimeModel
        writeLog(state, ("resolved SA-MP model %d to runtime model %d"):format(sourceModel, runtimeModel))
    end
    return true
end

local function resolveModel(state, sourceModel)
    if requiresSAMPResolver(sourceModel) then
        return state.modelRemap[sourceModel]
    end
    return sourceModel
end

local function createMapObjects(definition, options, state, diagnostics)
    for objectIndex, objectDefinition in ipairs(definition.objects) do
        local runtimeModel = resolveModel(state, objectDefinition.model)
        if not runtimeModel then
            return fail(
                state,
                diagnostics,
                "objects",
                ("no runtime model for SA-MP object model %d"):format(objectDefinition.model),
                objectDefinition.line,
                objectDefinition.column,
                "model_not_resolved"
            )
        end

        local object = createObject(
            runtimeModel,
            objectDefinition.x,
            objectDefinition.y,
            objectDefinition.z,
            objectDefinition.rx,
            objectDefinition.ry,
            objectDefinition.rz
        )
        if not object then
            return fail(
                state,
                diagnostics,
                "objects",
                ("createObject failed for map object %d (source model %d, runtime model %d)"):format(
                    objectIndex,
                    objectDefinition.model,
                    runtimeModel
                ),
                objectDefinition.line,
                objectDefinition.column,
                "object_create_failed"
            )
        end
        state.objects[#state.objects + 1] = object

        local interior = options.interior
        if interior == nil then
            interior = objectDefinition.interior >= 0 and objectDefinition.interior or 0
        end
        local dimension = options.dimension
        if dimension == nil then
            dimension = objectDefinition.world >= 0 and objectDefinition.world or 0
        end
        if not setElementInterior(object, interior) or not setElementDimension(object, dimension) then
            return fail(
                state,
                diagnostics,
                "objects",
                ("failed to assign interior/dimension for map object %d"):format(objectIndex),
                objectDefinition.line,
                objectDefinition.column,
                "object_world_failed"
            )
        end

        if options.onObject then
            local callbackSucceeded, callbackError = pcall(options.onObject, object, objectDefinition, objectIndex, runtimeModel)
            if not callbackSucceeded then
                return fail(
                    state,
                    diagnostics,
                    "objects",
                    ("onObject callback failed for map object %d: %s"):format(objectIndex, tostring(callbackError)),
                    objectDefinition.line,
                    objectDefinition.column,
                    "object_callback_failed"
                )
            end
        end

        for _, material in ipairs(objectDefinition.materials) do
            local sourceModel = resolveModel(state, material.sourceModel)
            if sourceModel == nil then
                return fail(
                    state,
                    diagnostics,
                    "materials",
                    ("no runtime model for material source %d"):format(material.sourceModel),
                    material.line,
                    material.column,
                    "material_model_not_resolved"
                )
            end

            if not setObjectMaterial(object, material.slot, sourceModel, material.txd, material.texture, material.color) then
                return fail(
                    state,
                    diagnostics,
                    "materials",
                    ("setObjectMaterial failed for object %d, slot %d, source model %d"):format(
                        objectIndex,
                        material.slot,
                        material.sourceModel
                    ),
                    material.line,
                    material.column,
                    "material_apply_failed"
                )
            end
        end
    end
    return true
end

local function removeMapBuildings(definition, options, state, diagnostics)
    local removalInterior = options.removalInterior or 0
    for _, removal in ipairs(definition.removedBuildings) do
        if not removeWorldModel(removal.model, removal.radius, removal.x, removal.y, removal.z, removalInterior) then
            return fail(
                state,
                diagnostics,
                "removals",
                ("removeWorldModel failed for model %d"):format(removal.model),
                removal.line,
                removal.column,
                "building_remove_failed"
            )
        end
        state.removals[#state.removals + 1] = {
            model = removal.model,
            radius = removal.radius,
            x = removal.x,
            y = removal.y,
            z = removal.z,
            interior = removalInterior,
        }
    end
    return true
end

function unloadSAMPMap(handle)
    if type(handle) ~= "table" or handle.kind ~= "samp-map" then
        return false
    end
    releaseState(handle)
    return true
end

function loadSAMPMap(path, options)
    options = options or {}
    local diagnostics = {}
    local state = {
        kind = "samp-map",
        sourcePath = path,
        objects = {},
        removals = {},
        resolvedModels = {},
        modelRemap = {},
        released = false,
        logger = options.log,
    }

    local source, readError = readSource(path)
    if not source then
        return fail(state, diagnostics, "read", readError, 0, 0, "map_read_failed")
    end

    local definition = engineParseSAMPMap(source)
    appendParserDiagnostics(diagnostics, definition)
    if not definition.success then
        releaseState(state)
        return false, diagnostics
    end
    writeLog(
        state,
        ("parsed %s: %d objects, %d removals, %d errors"):format(
            path,
            definition.objectCount or #definition.objects,
            definition.removedBuildingCount or #definition.removedBuildings,
            definition.errorCount or 0
        )
    )

    -- Resolve the full dependency set before creating or removing anything in
    -- the GTA world. A bad catalogue entry therefore cannot leave half a map.
    local requiredModels = collectRequiredModels(definition)
    if #requiredModels > 0 and not resolveRequiredModels(requiredModels, options, state, diagnostics) then
        return false, diagnostics
    end

    if not createMapObjects(definition, options, state, diagnostics) then
        return false, diagnostics
    end
    if not removeMapBuildings(definition, options, state, diagnostics) then
        return false, diagnostics
    end

    state.definition = definition
    state.objectCount = #state.objects
    state.removedBuildingCount = #state.removals
    state.remappedModelCount = #state.resolvedModels
    writeLog(
        state,
        ("loaded %s atomically: %d objects, %d removals, %d remapped SA-MP models"):format(
            path,
            state.objectCount,
            state.removedBuildingCount,
            state.remappedModelCount
        )
    )
    return state, diagnostics
end
