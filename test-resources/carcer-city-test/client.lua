local WORK_BUDGET_MS = 12
local MAX_MODEL_REGISTRATIONS_PER_FRAME = 96
local MAX_PLACEMENTS_PER_FRAME = 500
local MODEL_IMAGE_PATH = "assets/carcer_models.img"
local TEXTURE_IMAGE_PATH = "assets/carcer_textures.img"
local CITY_ID = "carcer"

local modelImage = nil
local textureImage = nil
local loadedModels = {}
local loadedTextures = {}
local createdBuildings = {}
local placementByElement = {}
local probeObjects = {}
local modelOrder = {}
local activeTimers = {}
local loadedRadarTiles = {}

local bootstrapStage = "standby"
local modelCursor = 1
local placementCursor = 1
local bootstrapStartedAt = 0
local loadingFailed = false
local prepareRequest = nil
local residencyRequest = nil
local readyReported = false
local failLoad
local finishPrepare
local finishResidencyRequest

-- The coordinator owns policy and fades, while this resource retains the IMG,
-- model, TXD, collision, and placement lifetimes it knows how to tear down.
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
        for _, tile in ipairs(CARCER_RADAR_TILES) do
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
        outputChatBox("[Carcer radar] API radar Neon indisponible.", 255, 180, 80)
        return false
    end
    if not engineGetRadarMapStats().hooksInstalled then
        outputChatBox("[Carcer radar] Hooks radar inactifs sur cet executable.", 255, 80, 80)
        return false
    end

    for _, tile in ipairs(CARCER_RADAR_TILES) do
        local txd = engineLoadTXD(tile.path)
        if not txd then
            releaseRadar()
            outputChatBox("[Carcer radar] ECHEC engineLoadTXD: " .. tile.path, 255, 80, 80)
            return false
        end
        if not engineSetRadarMapTile(tile.column, tile.row, txd) then
            destroyElement(txd)
            releaseRadar()
            outputChatBox(("[Carcer radar] ECHEC cellule %d,%d"):format(tile.column, tile.row), 255, 80, 80)
            return false
        end
        loadedRadarTiles[tile.path] = txd
    end

    local message = radarStatsMessage(("[Carcer radar] %d tuiles chargees;"):format(#CARCER_RADAR_TILES))
    outputChatBox(message, 80, 220, 255)
    outputDebugString(message)
    return true
end

addCommandHandler("ccradarstats", function()
    local message = radarStatsMessage("[Carcer radar]")
    outputChatBox(message, 80, 220, 255)
    outputDebugString(message)
end)

local function quaternionMatrix(placement)
    -- GTA's IPL loader conjugates the stored quaternion before applying it:
    -- full 3D rotations negate the imaginary vector explicitly, while the
    -- yaw-only path reaches the same result through its signed heading.
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

local function releaseMap()
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
        triggerServerEvent("carcerCityPositionReady", resourceRoot, prepareRequest.token, false, "city residency switched")
    end
    prepareRequest = nil
    local residencyToken = residencyRequest and residencyRequest.token or nil
    residencyRequest = nil

    -- Break every high/low link before destroying either endpoint. Automatic
    -- switching makes teardown frequent, so pairs() order must not decide
    -- whether GTA briefly retains a pointer to an already-destroyed LOD.
    for placementIndex, building in pairs(createdBuildings) do
        local placement = CARCER_CITY_PLACEMENTS[placementIndex]
        if isElement(building) and placement and placement.lod then
            setLowLODElement(building, nil)
        end
    end
    for _, building in pairs(createdBuildings) do
        if isElement(building) then
            destroyElement(building)
        end
    end
    for _, probe in pairs(probeObjects) do
        if isElement(probe) then
            destroyElement(probe)
        end
    end
    createdBuildings = {}
    placementByElement = {}
    probeObjects = {}

    -- Restore IMG links while the dynamic model and TXD slots still exist.
    -- The core drains pending GTA streaming requests before closing an IMG.
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

failLoad = function(reason)
    if loadingFailed then
        return
    end
    local manualRequestFailed = prepareRequest ~= nil
    loadingFailed = true
    bootstrapStage = "failed"
    removeEventHandler("onClientPreRender", root, processBootstrap)
    outputChatBox("[Carcer] ECHEC IMG: " .. reason, 255, 80, 80)
    outputDebugString("[Carcer IMG] " .. reason, 1)
    triggerServerEvent("carcerCityClientReady", resourceRoot, false, reason)
    if prepareRequest then
        triggerServerEvent("carcerCityPositionReady", resourceRoot, prepareRequest.token, false, reason)
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
    local slotName = "ugcc_" .. entryName:gsub("%.[^.]+$", "")
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

    local model = CARCER_CITY_MODELS[sourceId]
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

local function createPlacement(placement, placementIndex)
    if placement.native then
        local object = createObject(placement.model, placement.x, placement.y, placement.z)
        if not object then
            return false, "native object creation failed for " .. tostring(placement.model)
        end
        if not setElementMatrix(object, quaternionMatrix(placement)) then
            destroyElement(object)
            return false, "native matrix assignment failed for " .. tostring(placement.model)
        end
        if placement.isLod then
            setElementCollisionsEnabled(object, false)
        end
        createdBuildings[#createdBuildings + 1] = object
        placementByElement[object] = placementIndex
        return true
    end

    local model = loadedModels[placement.model]
    if not model then
        return false, "missing runtime model " .. tostring(placement.model)
    end

    local building = createBuilding(1337, placement.x, placement.y, placement.z)
    if not building or not setElementModel(building, model.runtimeId) then
        return false, "building creation failed for " .. model.name
    end
    if not setElementMatrix(building, quaternionMatrix(placement)) then
        destroyElement(building)
        return false, "matrix assignment failed for " .. model.name
    end
    -- Keep explicitly low-detail geometry non-colliding even when the source
    -- IPL does not pair it with a high-detail placement.
    if placement.isLod or model.name:lower():find("^lod") or not model.col then
        setElementCollisionsEnabled(building, false)
    end
    createdBuildings[#createdBuildings + 1] = building
    placementByElement[building] = placementIndex
    return true
end

local function placementDescription(placementIndex, distance)
    local placement = CARCER_CITY_PLACEMENTS[placementIndex]
    local model = CARCER_CITY_MODELS[placement.model]
    local name = model and model.name or engineGetModelNameFromID(placement.model) or "native"
    local runtimeId = model and model.runtimeId or placement.model
    local lod = placement.lod and (" -> LOD #" .. tostring(placement.lod)) or ""
    return ("#%d %s source=%d runtime=%s dist=%.2f IPL=%s[%d] xyz=%.3f, %.3f, %.3f%s"):format(
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
        lod
    )
end

local function reportPlacement(placementIndex, distance, chat)
    local description = placementDescription(placementIndex, distance)
    outputConsole("[Carcer inspect] " .. description)
    outputDebugString("[Carcer inspect] " .. description)
    if chat then
        outputChatBox("[Carcer] " .. description, 160, 220, 255)
    end
end

addCommandHandler("ccinspect", function(_, radiusArgument)
    if #createdBuildings == 0 then
        outputChatBox("[Carcer] La carte n'est pas encore chargee.", 255, 180, 80)
        return
    end

    local radius = math.max(1, math.min(200, tonumber(radiusArgument) or 25))
    local x, y, z = getElementPosition(localPlayer)
    local nearby = {}
    for placementIndex, placement in ipairs(CARCER_CITY_PLACEMENTS) do
        local dx, dy, dz = placement.x - x, placement.y - y, placement.z - z
        local distanceSquared = dx * dx + dy * dy + dz * dz
        if distanceSquared <= radius * radius then
            nearby[#nearby + 1] = {index = placementIndex, distance = math.sqrt(distanceSquared)}
        end
    end
    table.sort(nearby, function(left, right)
        return left.distance < right.distance
    end)

    outputChatBox(("[Carcer] %d instances dans %.0fm; les 12 plus proches sont dans F8."):format(#nearby, radius), 80, 255, 160)
    for resultIndex = 1, math.min(12, #nearby) do
        local result = nearby[resultIndex]
        reportPlacement(result.index, result.distance, resultIndex <= 3)
    end
end)

addCommandHandler("ccinspectaim", function()
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
        outputChatBox("[Carcer] Aucun objet Carcer identifie dans la visee.", 255, 180, 80)
        return
    end
    local distance = getDistanceBetweenPoints3D(cameraX, cameraY, cameraZ, hitX, hitY, hitZ)
    reportPlacement(placementIndex, distance, true)
end)

local function finishLoad()
    if readyReported then
        return
    end
    readyReported = true
    bootstrapStage = "ready"
    removeEventHandler("onClientPreRender", root, processBootstrap)

    for index, placement in ipairs(CARCER_CITY_PLACEMENTS) do
        if placement.lod then
            local high = createdBuildings[index]
            local low = createdBuildings[placement.lod]
            if high and low then
                setLowLODElement(high, low)
            end
        end
    end

    local details = ("elapsed=%dms placements=%d models=%d txds=%d img-models=%d img-txds=%d"):format(
        getTickCount() - bootstrapStartedAt,
        countKeys(createdBuildings),
        countKeys(loadedModels),
        countKeys(loadedTextures),
        engineImageGetFilesCount(modelImage),
        engineImageGetFilesCount(textureImage)
    )
    outputChatBox("[Carcer] Ville enregistree. /cctest pour Carcer City.", 80, 255, 160)
    outputDebugString("[Carcer IMG] ready " .. details)
    triggerServerEvent("carcerCityClientReady", resourceRoot, true, details)
    if prepareRequest then
        prepareRequest.timer = rememberTimer(setTimer(finishPrepare, 350, 1))
    elseif residencyRequest then
        residencyRequest.timer = rememberTimer(setTimer(finishResidencyRequest, 350, 1))
    else
        reportResidencyState("ready", nil, details)
    end
end

addCommandHandler("ccstreamstats", function()
    local memory = engineStreamingGetUsedMemory and engineStreamingGetUsedMemory() or 0
    local message = ("stage=%s models=%d/%d txds=%d placements=%d/%d memory=%.1fMiB prepare=%s"):format(
        bootstrapStage,
        countKeys(loadedModels),
        #modelOrder,
        countKeys(loadedTextures),
        countKeys(createdBuildings),
        #CARCER_CITY_PLACEMENTS,
        memory / (1024 * 1024),
        tostring(prepareRequest ~= nil)
    )
    outputChatBox("[Carcer IMG] " .. message, 80, 220, 255)
    outputDebugString("[Carcer IMG] " .. message)
end)

local function setLodDiagnosticMode(mode)
    if mode ~= "normal" and mode ~= "high" and mode ~= "both" then
        outputChatBox("[Carcer] Usage: /cclodmode normal|high|both", 255, 180, 80)
        return
    end

    local links = 0
    for index, placement in ipairs(CARCER_CITY_PLACEMENTS) do
        if placement.lod then
            local high = createdBuildings[index]
            local low = createdBuildings[placement.lod]
            if isElement(high) and isElement(low) then
                if mode == "normal" then
                    setElementAlpha(low, 255)
                    setLowLODElement(high, low)
                else
                    setLowLODElement(high, nil)
                    setElementAlpha(low, mode == "both" and 255 or 0)
                end
                links = links + 1
            end
        end
    end
    engineRestreamWorld()
    outputChatBox(("[Carcer] Mode LOD '%s': %d liens traites."):format(mode, links), 80, 255, 160)
    outputDebugString(("[Carcer] LOD diagnostic mode=%s links=%d"):format(mode, links))
end

addCommandHandler("cclodmode", function(_, mode)
    setLodDiagnosticMode((mode or ""):lower())
end)

local function restoreProbe(placementIndex)
    local probe = probeObjects[placementIndex]
    if isElement(probe) then
        destroyElement(probe)
    end
    probeObjects[placementIndex] = nil

    local placement = CARCER_CITY_PLACEMENTS[placementIndex]
    local original = createdBuildings[placementIndex]
    if not placement or not isElement(original) then
        return false
    end
    setElementAlpha(original, 255)
    if placement.lod then
        local low = createdBuildings[placement.lod]
        if isElement(low) then
            setElementAlpha(low, 255)
            setLowLODElement(original, low)
        end
    end
    return true
end

addCommandHandler("ccprobe", function(_, indexArgument, modeArgument)
    local placementIndex = tonumber(indexArgument)
    local mode = (modeArgument or "status"):lower()
    local placement = placementIndex and CARCER_CITY_PLACEMENTS[placementIndex]
    local original = placementIndex and createdBuildings[placementIndex]
    if not placement or not isElement(original) then
        outputChatBox("[Carcer] Usage: /ccprobe <index> status|object|original", 255, 180, 80)
        return
    end

    if mode == "original" then
        restoreProbe(placementIndex)
        engineRestreamWorld()
        outputChatBox(("[Carcer] Instance #%d restauree comme building."):format(placementIndex), 80, 255, 160)
        return
    end

    if mode == "object" then
        restoreProbe(placementIndex)
        local model = CARCER_CITY_MODELS[placement.model]
        if not model or not model.runtimeId then
            outputChatBox("[Carcer] Cette instance n'a pas de modele custom charge.", 255, 80, 80)
            return
        end
        local probe = createObject(1337, placement.x, placement.y, placement.z)
        if not probe or not setElementModel(probe, model.runtimeId) or not setElementMatrix(probe, quaternionMatrix(placement)) then
            if isElement(probe) then
                destroyElement(probe)
            end
            outputChatBox("[Carcer] Echec de creation de la sonde object.", 255, 80, 80)
            return
        end
        setElementCollisionsEnabled(probe, false)
        -- This probe must bypass the normal object-streamer cycle; otherwise
        -- its immediate status is always "streamed=false" and cannot
        -- distinguish an invisible model from an object that was never put in
        -- GTA's native pool.
        setElementStreamable(probe, false)
        setElementAlpha(original, 0)
        if placement.lod then
            setLowLODElement(original, nil)
            local low = createdBuildings[placement.lod]
            if isElement(low) then
                setElementAlpha(low, 0)
            end
        end
        probeObjects[placementIndex] = probe
        placementByElement[probe] = placementIndex
        engineRestreamWorld()
        outputChatBox(("[Carcer] Instance #%d remplacee temporairement par un object."):format(placementIndex), 80, 255, 160)
        setTimer(function()
            if not isElement(probe) then
                return
            end
            local playerX, playerY, playerZ = getElementPosition(localPlayer)
            local distance = getDistanceBetweenPoints3D(playerX, playerY, playerZ, placement.x, placement.y, placement.z)
            local delayedStatus = ("#%d delayed type=%s streamed=%s onscreen=%s alpha=%d model=%d distance=%.2f"):format(
                placementIndex,
                getElementType(probe),
                tostring(isElementStreamedIn(probe)),
                tostring(isElementOnScreen(probe)),
                getElementAlpha(probe),
                getElementModel(probe),
                distance
            )
            outputChatBox("[Carcer probe] " .. delayedStatus, 160, 220, 255)
            outputDebugString("[Carcer probe] " .. delayedStatus)
        end, 1000, 1)
    elseif mode ~= "status" then
        outputChatBox("[Carcer] Usage: /ccprobe <index> status|object|original", 255, 180, 80)
        return
    end

    local active = probeObjects[placementIndex] or original
    local status = ("#%d type=%s streamed=%s onscreen=%s alpha=%d model=%d"):format(
        placementIndex,
        getElementType(active),
        tostring(isElementStreamedIn(active)),
        tostring(isElementOnScreen(active)),
        getElementAlpha(active),
        getElementModel(active)
    )
    outputChatBox("[Carcer probe] " .. status, 160, 220, 255)
    outputDebugString("[Carcer probe] " .. status)
end)

local function buildIndexes()
    for sourceId in pairs(CARCER_CITY_MODELS) do
        modelOrder[#modelOrder + 1] = sourceId
    end
    table.sort(modelOrder)
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
                    failLoad(reason)
                    return
                end
                modelCursor = modelCursor + 1
                modelRegistrations = modelRegistrations + 1
                if modelRegistrations >= MAX_MODEL_REGISTRATIONS_PER_FRAME then
                    break
                end
            end
        elseif bootstrapStage == "placements" then
            if placementCursor > #CARCER_CITY_PLACEMENTS then
                finishLoad()
                return
            end
            local ok, reason = createPlacement(CARCER_CITY_PLACEMENTS[placementCursor], placementCursor)
            if not ok then
                failLoad(reason)
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
        failLoad(reason)
        return
    end
    bootstrapStage = "models"
    addEventHandler("onClientPreRender", root, processBootstrap)

    local details = ("placements=%d models=%d model-img=%d texture-img=%d"):format(
        #CARCER_CITY_PLACEMENTS,
        #modelOrder,
        engineImageGetFilesCount(modelImage),
        engineImageGetFilesCount(textureImage)
    )
    outputChatBox("[Carcer] Activation locale rapide...", 255, 200, 80)
    outputDebugString("[Carcer IMG] bootstrap " .. details)
end

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
            triggerServerEvent("carcerCityPositionReady", resourceRoot, request.token, false, tostring(result))
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
    outputDebugString("[Carcer IMG] prepare ready " .. details)
    triggerServerEvent("carcerCityPositionReady", resourceRoot, request.token, true, details)
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

addEvent("carcerCityPreparePosition", true)
addEventHandler("carcerCityPreparePosition", resourceRoot, function(x, y, z, token)
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
        outputChatBox("[Carcer] Prechargement natif de Carcer City...", 255, 200, 80)
        return
    end

    -- LC, VC, and Carcer remain active server-side, but the client releases
    -- the remote custom city so GTA's finite dynamic DFF/TXD slots can be
    -- reused without unloading San Andreas.
    triggerEvent("ugWorldDeactivateCities", root, CITY_ID)
    -- Local event dispatch is synchronous, so every previous owner has fully
    -- released its slots before this bootstrap begins.
    beginBootstrap()
end)

addEvent("carcerCityTeleportCommitted", true)
addEventHandler("carcerCityTeleportCommitted", resourceRoot, function(token)
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

addEvent("carcerCityPrepareCancelled", true)
addEventHandler("carcerCityPrepareCancelled", resourceRoot, function(token)
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

addEventHandler("onClientResourceStart", resourceRoot, function()
    buildIndexes()
    loadRadar()
    outputDebugString("[Carcer IMG] standby; activation locale par /cctest")
    triggerServerEvent("carcerCityClientReady", resourceRoot, true, "standby")
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
    triggerServerEvent("carcerCityClientReady", resourceRoot, true, "standby; autre ville residente")
    outputDebugString("[Carcer IMG] released for " .. tostring(nextCity))
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
