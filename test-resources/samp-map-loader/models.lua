local catalog
local modelImage
local supportImage
local loadedModels = {}
local loadedTextures = {}
local initialized = false
local stats = {
    loadedModels = 0,
    modelReferences = 0,
    loadedTextures = 0,
    failedLoads = 0,
}

local function log(message, level)
    outputDebugString("[SAMP Models] " .. message, level or 3)
end
local function readFile(path)
    local file = fileOpen(path, true)
    if not file then
        return false, "cannot open " .. path
    end
    local size = fileGetSize(file)
    local contents = fileRead(file, size)
    fileClose(file)
    if not contents or #contents ~= size then
        return false, ("short read for %s (%d/%d bytes)"):format(path, contents and #contents or 0, size)
    end
    return contents
end

local function initialize()
    if initialized then
        return true
    end

    local contents, readError = readFile("catalog.json")
    if not contents then
        return false, readError
    end
    catalog = fromJSON(contents)
    if type(catalog) ~= "table" or catalog.format ~= 1 or type(catalog.models) ~= "table" then
        return false, "catalog.json has an unsupported format"
    end

    modelImage = engineLoadIMG("assets/SAMP.img")
    supportImage = engineLoadIMG("assets/support.img")
    if not modelImage or not supportImage then
        return false, "cannot open the SA-MP IMG archives"
    end
    if not engineAddImage(modelImage) or not engineAddImage(supportImage) then
        return false, "cannot register the SA-MP IMG archives"
    end

    initialized = true
    log(("catalog ready: %d definitions from %s"):format(catalog.modelCount or 0, catalog.source or "unknown source"))
    return true
end

local function releaseTexture(name)
    local texture = loadedTextures[name]
    if not texture then
        return
    end
    texture.references = texture.references - 1
    if texture.references > 0 then
        return
    end
    engineFreeTXD(texture.id)
    loadedTextures[name] = nil
    stats.loadedTextures = stats.loadedTextures - 1
end

local function requestTexture(definition)
    if not definition.txdEntry then
        local txdId = engineGetTXDIDFromName(definition.txd)
        if txdId == false then
            return false, ("vanilla TXD '%s' is not registered"):format(definition.txd)
        end
        return txdId
    end

    local key = definition.txd:lower()
    local texture = loadedTextures[key]
    if texture then
        texture.references = texture.references + 1
        return texture.id, key
    end

    local txdId = engineRequestTXD(definition.txd)
    if txdId == false or not engineImageLinkTXD(modelImage, definition.txdEntry, txdId) then
        if txdId ~= false then
            engineFreeTXD(txdId)
        end
        return false, ("cannot link TXD entry '%s'"):format(definition.txdEntry)
    end
    loadedTextures[key] = {id = txdId, references = 1}
    stats.loadedTextures = stats.loadedTextures + 1
    return txdId, key
end

local function rollbackModel(runtimeId, colElement, textureKey)
    if colElement and isElement(colElement) then
        destroyElement(colElement)
    end
    if runtimeId then
        engineFreeModel(runtimeId)
    end
    if textureKey then
        releaseTexture(textureKey)
    end
end

function resolveSAMPModel(sourceModel)
    local ready, initializationError = initialize()
    if not ready then
        stats.failedLoads = stats.failedLoads + 1
        return false, initializationError
    end

    local existing = loadedModels[sourceModel]
    if existing then
        existing.references = existing.references + 1
        stats.modelReferences = stats.modelReferences + 1
        return existing.runtimeId, existing
    end

    local definition = catalog.models[tostring(sourceModel)]
    if not definition then
        stats.failedLoads = stats.failedLoads + 1
        return false, "ID is not defined by SA-MP 0.3.7-R5"
    end
    if not definition.supported then
        stats.failedLoads = stats.failedLoads + 1
        return false, definition.reason or "model is intentionally unsupported"
    end

    local runtimeId = engineRequestModel(definition.type or "object")
    if not runtimeId then
        stats.failedLoads = stats.failedLoads + 1
        return false, "engineRequestModel exhausted the runtime model pool"
    end

    local txdId, textureKeyOrReason = requestTexture(definition)
    if txdId == false then
        rollbackModel(runtimeId)
        stats.failedLoads = stats.failedLoads + 1
        return false, textureKeyOrReason
    end
    local textureKey = definition.txdEntry and textureKeyOrReason or nil

    if not engineSetModelTXDID(runtimeId, txdId) then
        rollbackModel(runtimeId, nil, textureKey)
        stats.failedLoads = stats.failedLoads + 1
        return false, "cannot assign the texture dictionary"
    end
    if not engineImageLinkDFF(modelImage, definition.dff, runtimeId) then
        rollbackModel(runtimeId, nil, textureKey)
        stats.failedLoads = stats.failedLoads + 1
        return false, ("cannot link DFF entry '%s'"):format(definition.dff)
    end

    local colElement
    if definition.collision then
        colElement = engineLoadCOL(definition.collision)
        if not colElement or not engineReplaceCOL(colElement, runtimeId) then
            rollbackModel(runtimeId, colElement, textureKey)
            stats.failedLoads = stats.failedLoads + 1
            return false, ("cannot apply collision '%s'"):format(definition.collision)
        end
    end

    engineSetModelLODDistance(runtimeId, definition.drawDistance, true)
    engineSetModelFlags(runtimeId, definition.flags, true)

    local model = {
        sourceId = sourceModel,
        runtimeId = runtimeId,
        references = 1,
        textureKey = textureKey,
        collision = colElement,
        definition = definition,
    }
    loadedModels[sourceModel] = model
    stats.loadedModels = stats.loadedModels + 1
    stats.modelReferences = stats.modelReferences + 1
    log(("loaded SA-MP model %d (%s) as runtime model %d"):format(sourceModel, definition.name, runtimeId))
    return runtimeId, model
end

function releaseSAMPModel(sourceModel, runtimeModel, handle)
    local model = loadedModels[sourceModel]
    if not model or model ~= handle or model.runtimeId ~= runtimeModel then
        return false
    end

    model.references = model.references - 1
    stats.modelReferences = stats.modelReferences - 1
    if model.references > 0 then
        return true
    end

    if model.collision and isElement(model.collision) then
        destroyElement(model.collision)
    end
    engineFreeModel(model.runtimeId)
    if model.textureKey then
        releaseTexture(model.textureKey)
    end
    loadedModels[sourceModel] = nil
    stats.loadedModels = stats.loadedModels - 1
    return true
end

function getSAMPModelRuntimeID(sourceModel)
    local model = loadedModels[tonumber(sourceModel)]
    return model and model.runtimeId or false
end

function getSAMPMapLoaderStats()
    return {
        loadedModels = stats.loadedModels,
        modelReferences = stats.modelReferences,
        loadedTextures = stats.loadedTextures,
        failedLoads = stats.failedLoads,
        catalogModels = catalog and catalog.modelCount or 0,
    }
end
