local WORK_BUDGET_MS = 12
local MAX_MODEL_REGISTRATIONS_PER_FRAME = 96
local MAX_PLACEMENTS_PER_FRAME = 500
local MODEL_IMAGE_PATH = "assets/lc_models.img"
local TEXTURE_IMAGE_PATH = "assets/lc_textures.img"
local CITY_ID = "lc"

-- The city elements stay registered for the resource lifetime. GTA can then
-- stream their RenderWare instances normally instead of racing a second Lua
-- lifecycle that destroys buildings while the native streamer still uses
-- their high/low-detail relationship.
local modelImage = nil
local textureImage = nil
local loadedModels = {}
local loadedTextures = {}
local createdBuildings = {}
local placementByElement = {}
local lodChildren = {}
local modelOrder = {}
local loadedRadarTiles = {}
local activeTimers = {}

local bootstrapStage = "standby"
local modelCursor = 1
local placementCursor = 1
local bootstrapStartedAt = 0
local loadingFailed = false
local prepareRequest = nil
local residencyRequest = nil
local readyReported = false
local failBootstrap
local finishPrepare
local finishResidencyRequest
local releaseMap

-- The residency coordinator is a separate resource, so this event protocol
-- keeps model ownership inside the city resource that allocated the slots.
addEvent("ugWorldCityResidencyState", false)

local function reportResidencyState(state, requestToken, details)
    triggerEvent("ugWorldCityResidencyState", root, CITY_ID, state, requestToken, details)
end

local function countKeys(values)
    local count = 0
    for _ in pairs(values) do
        count = count + 1
    end
    return count
end

local function rememberTimer(timer)
    activeTimers[timer] = true
    return timer
end

local function forgetTimer(timer)
    if timer then
        activeTimers[timer] = nil
    end
end

local function archiveName(path)
    return path and path:match("([^/\\]+)$") or nil
end

local function releaseRadar()
    if engineResetRadarMapTile then
        for _, tile in ipairs(UG_LC_RADAR_TILES) do
            engineResetRadarMapTile(tile.column, tile.row)
        end
    end
    for _, txd in pairs(loadedRadarTiles) do
        if isElement(txd) then
            destroyElement(txd)
        end
    end
    loadedRadarTiles = {}
end

local function radarStatsMessage(prefix)
    if not engineGetRadarMapStats then
        return prefix .. " API indisponible"
    end
    local stats = engineGetRadarMapStats()
    return ("%s hooks=%s registered=%d loaded=%d failed=%d source=%.1f KiB"):format(
        prefix,
        tostring(stats.hooksInstalled),
        stats.registeredTiles,
        stats.loadedTiles,
        stats.failedTiles,
        stats.sourceBytes / 1024
    )
end

local function loadRadar()
    releaseRadar()
    if not engineSetRadarMapTile or not engineGetRadarMapStats then
        outputChatBox("[UG LC radar] API radar Neon indisponible.", 255, 180, 80)
        return false
    end
    if not engineGetRadarMapStats().hooksInstalled then
        outputChatBox("[UG LC radar] Hooks radar inactifs sur cet executable.", 255, 80, 80)
        return false
    end

    for _, tile in ipairs(UG_LC_RADAR_TILES) do
        local txd = engineLoadTXD(tile.path)
        if not txd then
            releaseRadar()
            outputChatBox("[UG LC radar] ECHEC engineLoadTXD: " .. tile.path, 255, 80, 80)
            return false
        end
        if not engineSetRadarMapTile(tile.column, tile.row, txd) then
            destroyElement(txd)
            releaseRadar()
            outputChatBox(("[UG LC radar] ECHEC cellule %d,%d"):format(tile.column, tile.row), 255, 80, 80)
            return false
        end
        loadedRadarTiles[tile.path] = txd
    end

    local message = radarStatsMessage(("[UG LC radar] %d tuiles chargees;"):format(#UG_LC_RADAR_TILES))
    outputChatBox(message, 80, 220, 255)
    outputDebugString(message)
    return true
end

addCommandHandler("lcradarstats", function()
    local message = radarStatsMessage("[UG LC radar]")
    outputChatBox(message, 80, 220, 255)
    outputDebugString(message)
end)

local function quaternionMatrix(placement)
    -- GTA's IPL loader conjugates the stored quaternion before applying it.
    local x, y, z, w = -placement.qx, -placement.qy, -placement.qz, placement.qw
    local xx, yy, zz = x * x, y * y, z * z
    local xy, xz, yz = x * y, x * z, y * z
    local wx, wy, wz = w * x, w * y, w * z

    return {
        {1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy), 0},
        {2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx), 0},
        {2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy), 0},
        {placement.x, placement.y, placement.z, 1},
    }
end

local function buildIndexes()
    for sourceId in pairs(UG_LC_MODELS) do
        modelOrder[#modelOrder + 1] = sourceId
    end
    table.sort(modelOrder)

    for placementIndex, placement in ipairs(UG_LC_PLACEMENTS) do
        if placement.lod then
            lodChildren[placement.lod] = lodChildren[placement.lod] or {}
            lodChildren[placement.lod][#lodChildren[placement.lod] + 1] = placementIndex
        end
    end
end

local function openImages()
    modelImage = engineLoadIMG(MODEL_IMAGE_PATH)
    textureImage = engineLoadIMG(TEXTURE_IMAGE_PATH)
    if not modelImage or not textureImage then
        return false, "engineLoadIMG failed"
    end
    if not engineAddImage(modelImage) then
        return false, "engineAddImage failed for " .. MODEL_IMAGE_PATH
    end
    if not engineAddImage(textureImage) then
        return false, "engineAddImage failed for " .. TEXTURE_IMAGE_PATH
    end
    return true
end

local function requestTexture(path)
    if not path then
        return nil
    end
    if loadedTextures[path] then
        return loadedTextures[path]
    end

    local entryName = archiveName(path)
    local slotName = "uglc_" .. entryName:gsub("%.[^.]+$", "")
    local txdId = engineRequestTXD(slotName)
    if not txdId or txdId == 0 then
        return nil, "engineRequestTXD failed for " .. path
    end
    if not engineImageLinkTXD(textureImage, entryName, txdId) then
        engineFreeTXD(txdId)
        return nil, "engineImageLinkTXD failed for " .. entryName
    end

    loadedTextures[path] = txdId
    return txdId
end

local function registerModel(sourceId)
    if loadedModels[sourceId] then
        return true
    end

    local model = UG_LC_MODELS[sourceId]
    if not model then
        return false, "definition absente pour le modele " .. tostring(sourceId)
    end

    local runtimeId = engineRequestModel(model.modelType)
    if not runtimeId then
        return false, "engineRequestModel failed for " .. model.name
    end
    model.runtimeId = runtimeId

    local textureId, textureReason = requestTexture(model.txd)
    local dffEntry = archiveName(model.dff)
    local reason = textureReason
    if not reason and model.txd and not engineSetModelTXDID(runtimeId, textureId) then
        reason = "engineSetModelTXDID failed for " .. model.name
    elseif not reason and not engineImageLinkDFF(modelImage, dffEntry, runtimeId) then
        reason = "engineImageLinkDFF failed for " .. dffEntry
    end

    if not reason and model.col then
        model.colElement = engineLoadCOL(model.col)
        if not model.colElement then
            reason = "engineLoadCOL failed for " .. model.name
        elseif not engineReplaceCOL(model.colElement, runtimeId) then
            reason = "engineReplaceCOL failed for " .. model.name
        end
    end

    if reason then
        if isElement(model.colElement) then
            destroyElement(model.colElement)
        end
        model.colElement = nil
        engineFreeModel(runtimeId)
        model.runtimeId = nil
        return false, reason
    end

    engineSetModelLODDistance(runtimeId, model.lodDistance, true)
    engineSetModelFlags(runtimeId, model.ideFlags, true)
    if model.timeOn and model.timeOff then
        engineSetModelVisibleTime(tostring(runtimeId), model.timeOn, model.timeOff)
    end
    loadedModels[sourceId] = model
    return true
end

local function linkPlacementLod(placementIndex)
    local placement = UG_LC_PLACEMENTS[placementIndex]
    local element = createdBuildings[placementIndex]
    if placement and placement.lod and isElement(element) then
        local low = createdBuildings[placement.lod]
        if isElement(low) then
            setLowLODElement(element, low)
        end
    end

    for _, highIndex in ipairs(lodChildren[placementIndex] or {}) do
        local high = createdBuildings[highIndex]
        if isElement(high) and isElement(element) then
            setLowLODElement(high, element)
        end
    end
end

local function createPlacement(placementIndex)
    local placement = UG_LC_PLACEMENTS[placementIndex]
    local element
    if placement.native then
        element = createObject(placement.model, placement.x, placement.y, placement.z, 0, 0, 0, placement.isLod)
    else
        local model = loadedModels[placement.model]
        if not model then
            return false, "modele runtime absent " .. tostring(placement.model)
        end
        element = createBuilding(1337, placement.x, placement.y, placement.z)
        if element and not setElementModel(element, model.runtimeId) then
            destroyElement(element)
            element = nil
        end
    end

    if not element then
        return false, "creation impossible pour le placement #" .. tostring(placementIndex)
    end
    if not setElementMatrix(element, quaternionMatrix(placement)) then
        destroyElement(element)
        return false, "matrice impossible pour le placement #" .. tostring(placementIndex)
    end

    local model = UG_LC_MODELS[placement.model]
    if placement.isLod or (model and model.name:lower():find("^lod")) or (model and not model.col) then
        setElementCollisionsEnabled(element, false)
    end

    createdBuildings[placementIndex] = element
    placementByElement[element] = placementIndex
    linkPlacementLod(placementIndex)
    return true
end

local function reportReady()
    if readyReported then
        return
    end
    readyReported = true
    bootstrapStage = "ready"
    removeEventHandler("onClientPreRender", root, processBootstrap)

    local details = ("elapsed=%dms placements=%d models=%d txds=%d img-models=%d img-txds=%d"):format(
        getTickCount() - bootstrapStartedAt,
        countKeys(createdBuildings),
        countKeys(loadedModels),
        countKeys(loadedTextures),
        engineImageGetFilesCount(modelImage),
        engineImageGetFilesCount(textureImage)
    )
    outputChatBox("[UG LC] Ville enregistree. /lctest pour Liberty City.", 80, 255, 160)
    outputDebugString("[UG LC IMG] ready " .. details)
    triggerServerEvent("ugLcClientReady", resourceRoot, true, details)
    if prepareRequest then
        prepareRequest.timer = rememberTimer(setTimer(finishPrepare, 350, 1))
    elseif residencyRequest then
        residencyRequest.timer = rememberTimer(setTimer(finishResidencyRequest, 350, 1))
    else
        reportResidencyState("ready", nil, details)
    end
end

failBootstrap = function(reason)
    if loadingFailed then
        return
    end
    local manualRequestFailed = prepareRequest ~= nil
    loadingFailed = true
    bootstrapStage = "failed"
    removeEventHandler("onClientPreRender", root, processBootstrap)
    outputChatBox("[UG LC] ECHEC IMG: " .. reason, 255, 80, 80)
    outputDebugString("[UG LC IMG] " .. reason, 1)
    triggerServerEvent("ugLcClientReady", resourceRoot, false, reason)
    if prepareRequest then
        triggerServerEvent("ugLcPositionReady", resourceRoot, prepareRequest.token, false, reason)
        reportResidencyState(
            "manual-cancelled",
            "manual:" .. tostring(prepareRequest.token),
            "bootstrap failed: " .. tostring(reason)
        )
        prepareRequest = nil
    end
    if residencyRequest then
        reportResidencyState("failed", residencyRequest.token, reason)
        residencyRequest = nil
    end
    -- Failed partial activation must return every slot before a retry.
    releaseMap()
    if manualRequestFailed then
        fadeCamera(true, 0.5)
    end
end

function processBootstrap()
    if loadingFailed or bootstrapStage == "ready" then
        return
    end

    local startedAt = getTickCount()
    local modelRegistrations = 0
    local placementRegistrations = 0
    while getTickCount() - startedAt < WORK_BUDGET_MS do
        if bootstrapStage == "models" then
            if modelCursor > #modelOrder then
                bootstrapStage = "placements"
            else
                local ok, reason = registerModel(modelOrder[modelCursor])
                if not ok then
                    failBootstrap(reason)
                    return
                end
                modelCursor = modelCursor + 1
                modelRegistrations = modelRegistrations + 1
                if modelRegistrations >= MAX_MODEL_REGISTRATIONS_PER_FRAME then
                    break
                end
            end
        elseif bootstrapStage == "placements" then
            if placementCursor > #UG_LC_PLACEMENTS then
                reportReady()
                return
            end
            local ok, reason = createPlacement(placementCursor)
            if not ok then
                failBootstrap(reason)
                return
            end
            placementCursor = placementCursor + 1
            placementRegistrations = placementRegistrations + 1
            if placementRegistrations >= MAX_PLACEMENTS_PER_FRAME then
                break
            end
        else
            break
        end
    end
end

local function beginBootstrap()
    if bootstrapStage == "ready" or bootstrapStage == "models" or bootstrapStage == "placements" then
        return
    end

    bootstrapStartedAt = getTickCount()
    modelCursor = 1
    placementCursor = 1
    loadingFailed = false
    readyReported = false
    local ok, reason = openImages()
    if not ok then
        failBootstrap(reason)
        return
    end
    bootstrapStage = "models"
    addEventHandler("onClientPreRender", root, processBootstrap)

    local details = ("placements=%d models=%d model-img=%d texture-img=%d"):format(
        #UG_LC_PLACEMENTS,
        #modelOrder,
        engineImageGetFilesCount(modelImage),
        engineImageGetFilesCount(textureImage)
    )
    outputChatBox("[UG LC] Activation locale rapide...", 255, 200, 80)
    outputDebugString("[UG LC IMG] bootstrap " .. details)
end

local function placementDescription(placementIndex, distance)
    local placement = UG_LC_PLACEMENTS[placementIndex]
    local model = UG_LC_MODELS[placement.model]
    local name = model and model.name or engineGetModelNameFromID(placement.model) or "native"
    local runtimeId = model and model.runtimeId or placement.model
    return ("#%d %s UG=%d runtime=%s dist=%.2f IPL=%s[%d] xyz=%.3f, %.3f, %.3f active=%s"):format(
        placementIndex,
        name,
        placement.model,
        tostring(runtimeId),
        distance or 0,
        placement.source or "?",
        placement.sourceIndex or -1,
        placement.x,
        placement.y,
        placement.z,
        tostring(isElement(createdBuildings[placementIndex]))
    )
end

local function reportPlacement(placementIndex, distance, chat)
    local description = placementDescription(placementIndex, distance)
    outputConsole("[UG LC inspect] " .. description)
    outputDebugString("[UG LC inspect] " .. description)
    if chat then
        outputChatBox("[UG LC] " .. description, 160, 220, 255)
    end
end

addCommandHandler("lcinspect", function(_, radiusArgument)
    local radius = math.max(1, math.min(200, tonumber(radiusArgument) or 25))
    local x, y, z = getElementPosition(localPlayer)
    local nearby = {}
    for placementIndex, placement in ipairs(UG_LC_PLACEMENTS) do
        local dx, dy, dz = placement.x - x, placement.y - y, placement.z - z
        local distanceSquared = dx * dx + dy * dy + dz * dz
        if distanceSquared <= radius * radius then
            nearby[#nearby + 1] = {index = placementIndex, distance = math.sqrt(distanceSquared)}
        end
    end
    table.sort(nearby, function(left, right)
        return left.distance < right.distance
    end)

    outputChatBox(("[UG LC] %d instances dans %.0fm; les 12 plus proches sont dans F8."):format(#nearby, radius), 80, 255, 160)
    for resultIndex = 1, math.min(12, #nearby) do
        local result = nearby[resultIndex]
        reportPlacement(result.index, result.distance, resultIndex <= 3)
    end
end)

addCommandHandler("lcinspectaim", function()
    local cameraX, cameraY, cameraZ, lookX, lookY, lookZ = getCameraMatrix()
    local dx, dy, dz = lookX - cameraX, lookY - cameraY, lookZ - cameraZ
    local length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length == 0 then
        return
    end
    local scale = 500 / length
    local hit, hitX, hitY, hitZ, hitElement = processLineOfSight(
        cameraX, cameraY, cameraZ,
        cameraX + dx * scale, cameraY + dy * scale, cameraZ + dz * scale,
        true, false, false, true, true, false, false, false, localPlayer
    )
    local placementIndex = hit and hitElement and placementByElement[hitElement]
    if not placementIndex then
        outputChatBox("[UG LC] Aucun objet LC dans la visee.", 255, 180, 80)
        return
    end
    reportPlacement(placementIndex, getDistanceBetweenPoints3D(cameraX, cameraY, cameraZ, hitX, hitY, hitZ), true)
end)

addCommandHandler("lcstreamstats", function()
    local memory = engineStreamingGetUsedMemory and engineStreamingGetUsedMemory() or 0
    local message = ("stage=%s models=%d/%d txds=%d placements=%d/%d memory=%.1fMiB prepare=%s"):format(
        bootstrapStage,
        countKeys(loadedModels),
        #modelOrder,
        countKeys(loadedTextures),
        countKeys(createdBuildings),
        #UG_LC_PLACEMENTS,
        memory / (1024 * 1024),
        tostring(prepareRequest ~= nil)
    )
    outputChatBox("[UG LC IMG] " .. message, 80, 220, 255)
    outputDebugString("[UG LC IMG] " .. message)
end)

finishPrepare = function()
    local request = prepareRequest
    if not request or request.reported then
        return
    end
    forgetTimer(request.timer)
    request.timer = nil

    if enginePreloadWorldArea then
        local succeeded, result = pcall(enginePreloadWorldArea, request.x, request.y, request.z, "all")
        if not succeeded then
            triggerServerEvent("ugLcPositionReady", resourceRoot, request.token, false, tostring(result))
            reportResidencyState(
                "manual-cancelled",
                "manual:" .. tostring(request.token),
                "native preload failed: " .. tostring(result)
            )
            prepareRequest = nil
            fadeCamera(true, 0.5)
            return
        end
    end

    request.reported = true
    local details = ("elapsed=%dms placements=%d models=%d txds=%d memory=%.1fMiB"):format(
        getTickCount() - request.startedAt,
        countKeys(createdBuildings),
        countKeys(loadedModels),
        countKeys(loadedTextures),
        (engineStreamingGetUsedMemory and engineStreamingGetUsedMemory() or 0) / (1024 * 1024)
    )
    outputDebugString("[UG LC IMG] prepare ready " .. details)
    triggerServerEvent("ugLcPositionReady", resourceRoot, request.token, true, details)
    -- The city is scene-ready, but automatic policy must stay suspended until
    -- the server confirms the matching teleport commit.
    reportResidencyState("manual-ready", "manual:" .. tostring(request.token), details)
end

finishResidencyRequest = function()
    local request = residencyRequest
    if not request then
        return
    end
    forgetTimer(request.timer)
    request.timer = nil

    -- Preparation may begin kilometres before arrival. Preload the current
    -- position at the final barrier so fast aircraft do not receive geometry
    -- and collision for a stale approach point.
    local preloadX, preloadY, preloadZ = getElementPosition(localPlayer)
    if enginePreloadWorldArea then
        local succeeded, result = pcall(enginePreloadWorldArea, preloadX, preloadY, preloadZ, "all")
        if not succeeded then
            reportResidencyState("failed", request.token, tostring(result))
            residencyRequest = nil
            releaseMap()
            return
        end
    end

    local details = ("elapsed=%dms placements=%d models=%d txds=%d memory=%.1fMiB preload=%.1f,%.1f,%.1f"):format(
        getTickCount() - request.startedAt,
        countKeys(createdBuildings),
        countKeys(loadedModels),
        countKeys(loadedTextures),
        (engineStreamingGetUsedMemory and engineStreamingGetUsedMemory() or 0) / (1024 * 1024),
        preloadX,
        preloadY,
        preloadZ
    )
    residencyRequest = nil
    reportResidencyState("ready", request.token, details)
end

addEvent("ugLcPreparePosition", true)
addEventHandler("ugLcPreparePosition", resourceRoot, function(x, y, z, token)
    if residencyRequest then
        if residencyRequest.timer and isTimer(residencyRequest.timer) then
            killTimer(residencyRequest.timer)
        end
        forgetTimer(residencyRequest.timer)
        reportResidencyState("failed", residencyRequest.token, "superseded by manual command")
        residencyRequest = nil
    end
    prepareRequest = {
        x = tonumber(x) or 0,
        y = tonumber(y) or 0,
        z = tonumber(z) or 0,
        token = token,
        startedAt = getTickCount(),
        reported = false,
    }
    reportResidencyState("preparing", "manual:" .. tostring(token), "manual command")
    fadeCamera(false, 0.25)
    if bootstrapStage == "ready" then
        prepareRequest.timer = rememberTimer(setTimer(finishPrepare, 350, 1))
        outputChatBox("[UG LC] Prechargement natif de Portland...", 255, 200, 80)
        return
    end

    -- GTA exposes only 5,136 free DFF slots after San Andreas has loaded. LC
    -- and VC therefore cannot both own every custom model on one client. The
    -- server still hosts both cities simultaneously; each client releases the
    -- remote city before making its local city resident.
    triggerEvent("ugWorldDeactivateCities", root, CITY_ID)
    -- Local event dispatch is synchronous, so every previous owner has fully
    -- released its slots before this bootstrap begins.
    beginBootstrap()
end)

addEvent("ugLcTeleportCommitted", true)
addEventHandler("ugLcTeleportCommitted", resourceRoot, function(token)
    if prepareRequest and prepareRequest.token == token then
        if prepareRequest.timer and isTimer(prepareRequest.timer) then
            killTimer(prepareRequest.timer)
        end
        forgetTimer(prepareRequest.timer)
        prepareRequest = nil
    end
    reportResidencyState("manual-committed", "manual:" .. tostring(token), "server teleport committed")
    rememberTimer(setTimer(function()
        forgetTimer(sourceTimer)
        fadeCamera(true, 0.5)
    end, 100, 1))
end)

addEvent("ugLcPrepareCancelled", true)
addEventHandler("ugLcPrepareCancelled", resourceRoot, function(token)
    -- Only the request that owns the manual fade may reveal the scene. A
    -- delayed cancellation from a city released during a newer transition is
    -- intentionally ignored.
    if prepareRequest and prepareRequest.token == token then
        if prepareRequest and prepareRequest.timer and isTimer(prepareRequest.timer) then
            killTimer(prepareRequest.timer)
        end
        if prepareRequest then
            forgetTimer(prepareRequest.timer)
        end
        prepareRequest = nil
        reportResidencyState("manual-cancelled", "manual:" .. tostring(token), "server cancelled manual preparation")
        fadeCamera(true, 0.5)
    end
end)

releaseMap = function()
    removeEventHandler("onClientPreRender", root, processBootstrap)
    for timer in pairs(activeTimers) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    activeTimers = {}
    if prepareRequest then
        reportResidencyState(
            "manual-cancelled",
            "manual:" .. tostring(prepareRequest.token),
            "city residency switched"
        )
        triggerServerEvent("ugLcPositionReady", resourceRoot, prepareRequest.token, false, "city residency switched")
    end
    prepareRequest = nil
    local residencyToken = residencyRequest and residencyRequest.token or nil
    residencyRequest = nil

    -- Break every high/low link before destroying either endpoint. Automatic
    -- switching makes teardown frequent, so pairs() order must not decide
    -- whether GTA briefly retains a pointer to an already-destroyed LOD.
    for placementIndex, element in pairs(createdBuildings) do
        local placement = UG_LC_PLACEMENTS[placementIndex]
        if isElement(element) and placement and placement.lod then
            setLowLODElement(element, nil)
        end
    end
    for _, element in pairs(createdBuildings) do
        if isElement(element) then
            destroyElement(element)
        end
    end
    createdBuildings = {}
    placementByElement = {}

    -- Restore IMG links while the dynamic slots still exist. This ordering is
    -- important because each archive remembers the previous streaming entry.
    if isElement(modelImage) then
        engineRemoveImage(modelImage)
        destroyElement(modelImage)
    end
    if isElement(textureImage) then
        engineRemoveImage(textureImage)
        destroyElement(textureImage)
    end
    modelImage = nil
    textureImage = nil

    for _, model in pairs(loadedModels) do
        if model.runtimeId then
            if model.colElement then
                engineRestoreCOL(model.runtimeId)
            end
            engineFreeModel(model.runtimeId)
            model.runtimeId = nil
        end
        if isElement(model.colElement) then
            destroyElement(model.colElement)
        end
        model.colElement = nil
    end
    loadedModels = {}
    for _, txdId in pairs(loadedTextures) do
        engineFreeTXD(txdId)
    end
    loadedTextures = {}
    bootstrapStage = "standby"
    modelCursor = 1
    placementCursor = 1
    loadingFailed = false
    readyReported = false
    reportResidencyState("standby", residencyToken, "dynamic slots released")
end

addEventHandler("onClientResourceStart", resourceRoot, function()
    buildIndexes()
    loadRadar()
    outputDebugString("[UG LC IMG] standby; activation locale par /lctest")
    triggerServerEvent("ugLcClientReady", resourceRoot, true, "standby")
    reportResidencyState("standby", nil, "resource started")
end)

addEvent("ugWorldRequestCityResidency", false)
addEventHandler("ugWorldRequestCityResidency", root, function(cityId, requestToken, x, y, z)
    if cityId ~= CITY_ID then
        return
    end
    if prepareRequest then
        reportResidencyState("failed", requestToken, "manual preparation owns this provider")
        return
    end
    if residencyRequest then
        if residencyRequest.timer and isTimer(residencyRequest.timer) then
            killTimer(residencyRequest.timer)
        end
        forgetTimer(residencyRequest.timer)
        if residencyRequest.token ~= requestToken then
            reportResidencyState("failed", residencyRequest.token, "superseded by a newer residency generation")
        end
    end

    if bootstrapStage == "failed" then
        releaseMap()
    end
    residencyRequest = {
        token = requestToken,
        x = tonumber(x) or 0,
        y = tonumber(y) or 0,
        z = tonumber(z) or 0,
        startedAt = getTickCount(),
    }
    reportResidencyState("preparing", requestToken, bootstrapStage)

    if bootstrapStage == "ready" then
        residencyRequest.timer = rememberTimer(setTimer(finishResidencyRequest, 350, 1))
    elseif bootstrapStage == "standby" then
        triggerEvent("ugWorldDeactivateCities", root, CITY_ID)
        beginBootstrap()
    end
end)

addEvent("ugWorldQueryCityResidency", false)
addEventHandler("ugWorldQueryCityResidency", root, function()
    if residencyRequest then
        -- Registration can be complete while the final scene/collision preload
        -- is still pending. Do not let a query publish premature readiness.
        reportResidencyState("preparing", residencyRequest.token, "preloading")
    elseif prepareRequest then
        local state = prepareRequest.reported and "manual-ready" or "preparing"
        reportResidencyState(state, "manual:" .. tostring(prepareRequest.token), "manual preload")
    else
        local state = bootstrapStage == "ready" and "ready" or bootstrapStage
        reportResidencyState(state, nil, "query")
    end
end)

addEvent("ugWorldDeactivateCities", false)
addEventHandler("ugWorldDeactivateCities", root, function(nextCity)
    if nextCity == CITY_ID or bootstrapStage == "standby" then
        return
    end
    releaseMap()
    triggerServerEvent("ugLcClientReady", resourceRoot, true, "standby; autre ville residente")
    outputDebugString("[UG LC IMG] released for " .. tostring(nextCity))
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    reportResidencyState(
        "releasing",
        residencyRequest and residencyRequest.token or (prepareRequest and ("manual:" .. tostring(prepareRequest.token)) or nil),
        "resource stopping"
    )
    releaseMap()
    releaseRadar()
end)
