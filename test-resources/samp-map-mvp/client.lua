local MAP_FILE = "Ext_DylanCarter3.pwn"

local loadedObjects = {}
local removedBuildings = {}
local savedStreamingLimits

local function log(message, level)
    outputDebugString("[SAMP MVP] " .. message, level or 3)
end

local function readMapSource()
    local file = fileOpen(MAP_FILE, true)
    if not file then
        return false, "cannot open " .. MAP_FILE
    end

    local size = fileGetSize(file)
    local source = fileRead(file, size)
    fileClose(file)
    if not source or #source ~= size then
        return false, ("short read for %s (%d/%d bytes)"):format(MAP_FILE, source and #source or 0, size)
    end
    return source
end

local function unloadMap()
    for _, object in ipairs(loadedObjects) do
        if isElement(object) then
            destroyElement(object)
        end
    end
    loadedObjects = {}

    for _, removal in ipairs(removedBuildings) do
        restoreWorldModel(removal.model, removal.radius, removal.x, removal.y, removal.z, 0)
    end
    removedBuildings = {}

    if savedStreamingLimits then
        engineSetObjectStreamingLimits(savedStreamingLimits.objects, savedStreamingLimits.lowLodObjects)
        log(("restored object streaming limits to %d/%d"):format(savedStreamingLimits.objects, savedStreamingLimits.lowLodObjects))
        savedStreamingLimits = nil
    end
end

local function reportDiagnostics(result)
    for _, diagnostic in ipairs(result.diagnostics) do
        local message = ("%s:%d:%d: %s: %s"):format(
            MAP_FILE,
            diagnostic.line,
            diagnostic.column,
            diagnostic.severity,
            diagnostic.message
        )
        log(message, diagnostic.severity == "error" and 1 or 2)
    end
end

local function loadMap()
    unloadMap()

    local source, readError = readMapSource()
    if not source then
        log(readError, 1)
        return false
    end

    local startedAt = getTickCount()
    local result = engineParseSAMPMap(source)
    local parseDuration = getTickCount() - startedAt
    reportDiagnostics(result)

    log(("native parse: success=%s objects=%d removals=%d errors=%d in %d ms"):format(
        tostring(result.success),
        result.objectCount,
        result.removedBuildingCount,
        result.errorCount,
        parseDuration
    ))
    if not result.success then
        outputChatBox("[SAMP MVP] Parse failed; inspect F8/client.log.", 255, 80, 80)
        return false
    end

    local normalLimit, lowLodLimit, hardMaximum = engineGetObjectStreamingLimits()
    savedStreamingLimits = {objects = normalLimit, lowLodObjects = lowLodLimit}
    engineSetObjectStreamingLimits(hardMaximum, 0)
    log(("object streaming limits set to %d/0 before map creation"):format(hardMaximum))

    for _, removal in ipairs(result.removedBuildings) do
        if removeWorldModel(removal.model, removal.radius, removal.x, removal.y, removal.z, 0) then
            removedBuildings[#removedBuildings + 1] = removal
            log(("removed world model %d from line %d"):format(removal.model, removal.line))
        else
            log(("failed to remove world model %d from line %d"):format(removal.model, removal.line), 2)
        end
    end

    local materialCount = 0
    local materialFailures = 0
    for index, definition in ipairs(result.objects) do
        local object = createObject(
            definition.model,
            definition.x,
            definition.y,
            definition.z,
            definition.rx,
            definition.ry,
            definition.rz
        )

        if not object then
            log(("createObject failed for map object %d, model %d, source line %d"):format(index, definition.model, definition.line), 1)
        else
            local interior = definition.interior >= 0 and definition.interior or 0
            local dimension = definition.world >= 0 and definition.world or 0
            setElementInterior(object, interior)
            setElementDimension(object, dimension)
            loadedObjects[#loadedObjects + 1] = object

            log(("created object %d/%d: model=%d handle=%s line=%d materials=%d"):format(
                index,
                #result.objects,
                definition.model,
                definition.handle,
                definition.line,
                #definition.materials
            ))

            for _, material in ipairs(definition.materials) do
                materialCount = materialCount + 1
                local applied = setObjectMaterial(
                    object,
                    material.slot,
                    material.sourceModel,
                    material.txd,
                    material.texture,
                    material.color
                )
                if applied then
                    log(("applied material: object=%d slot=%d sourceModel=%d txd=%s texture=%s color=0x%08X"):format(
                        index,
                        material.slot,
                        material.sourceModel,
                        material.txd,
                        material.texture,
                        material.color
                    ))
                else
                    materialFailures = materialFailures + 1
                    log(("material failed: object=%d slot=%d sourceModel=%d texture=%s source line=%d"):format(
                        index,
                        material.slot,
                        material.sourceModel,
                        material.texture,
                        material.line
                    ), 1)
                end
            end
        end
    end

    local summary = ("loaded %d/%d objects, %d materials (%d failed), %d removals"):format(
        #loadedObjects,
        #result.objects,
        materialCount,
        materialFailures,
        #removedBuildings
    )
    log(summary, materialFailures == 0 and 3 or 2)
    outputChatBox("[SAMP MVP] " .. summary .. ". /reloadsampmap to retry.", materialFailures == 0 and 80 or 255, materialFailures == 0 and 255 or 180, 160)
    return materialFailures == 0 and #loadedObjects == #result.objects
end

addCommandHandler("loadsampmap", loadMap)
addCommandHandler("reloadsampmap", loadMap)
addCommandHandler("unloadsampmap", unloadMap)

addEventHandler("onClientResourceStart", resourceRoot, function()
    setTimer(loadMap, 1000, 1)
end)

addEventHandler("onClientResourceStop", resourceRoot, unloadMap)
