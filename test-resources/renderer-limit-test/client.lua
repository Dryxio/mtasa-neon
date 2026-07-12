local originalFarClip
local originalLodDistances = {}
local densityBuildings = {}
local testActive = false

local function clearDensityTest()
    for _, building in ipairs(densityBuildings) do
        if isElement(building) then
            destroyElement(building)
        end
    end

    if #densityBuildings > 0 then
        outputDebugString(("[Renderer limit] Destroyed %d density-test buildings"):format(#densityBuildings))
    end
    densityBuildings = {}
end

local function clearTest()
    clearDensityTest()

    if not testActive then
        return
    end

    for model, distance in pairs(originalLodDistances) do
        engineSetModelLODDistance(model, distance, true)
    end
    originalLodDistances = {}

    if originalFarClip then
        setFarClipDistance(originalFarClip)
        originalFarClip = nil
    end

    testActive = false
    outputDebugString("[Renderer limit] Original far clip and model LOD distances restored")
end

local function startTest(_, requestedDistance)
    clearTest()

    local distance = math.min(math.max(tonumber(requestedDistance) or 1600, 300), 5000)
    originalFarClip = getFarClipDistance()
    setFarClipDistance(distance)

    local changedModels = 0
    for model = 615, 18631 do
        local originalDistance = engineGetModelLODDistance(model)
        if type(originalDistance) == "number" and originalDistance > 0 and originalDistance < distance then
            originalLodDistances[model] = originalDistance
            if engineSetModelLODDistance(model, distance, true) then
                changedModels = changedModels + 1
            end
        end
    end

    testActive = true
    outputChatBox(
        ("[Renderer limit] Far clip %.0f, %d model LOD distances extended. Use /renderstats for counters."):format(
            distance,
            changedModels
        ),
        80,
        255,
        160
    )
    outputDebugString(("[Renderer limit] far clip %.0f; %d model LOD distances extended"):format(distance, changedModels))
end

local function startDensityTest(_, requestedCount)
    clearDensityTest()

    if not testActive then
        startTest(nil, "1000")
    end

    local count = math.min(math.max(math.floor(tonumber(requestedCount) or 1400), 1), 2000)
    local cameraX, cameraY, _, lookAtX, lookAtY = getCameraMatrix()
    local forwardX, forwardY = lookAtX - cameraX, lookAtY - cameraY
    local length = math.sqrt(forwardX * forwardX + forwardY * forwardY)
    if length < 0.001 then
        forwardX, forwardY, length = 0, 1, 1
    end
    forwardX, forwardY = forwardX / length, forwardY / length

    local _, _, playerZ = getElementPosition(localPlayer)
    local columns = 50
    local created = 0
    for index = 0, count - 1 do
        local row = math.floor(index / columns)
        local column = index % columns
        local angle = -0.55 + (column / (columns - 1)) * 1.1
        local cosAngle, sinAngle = math.cos(angle), math.sin(angle)
        local directionX = forwardX * cosAngle - forwardY * sinAngle
        local directionY = forwardX * sinAngle + forwardY * cosAngle
        local distance = 35 + row * 9
        local building = createBuilding(1337, cameraX + directionX * distance, cameraY + directionY * distance, playerZ - 1)
        if not building then
            break
        end

        setElementCollisionsEnabled(building, false)
        densityBuildings[#densityBuildings + 1] = building
        created = created + 1
    end

    outputChatBox(
        ("[Renderer limit] Density test: %d/%d buildings created in the camera frustum. Use /renderstats."):format(created, count),
        created == count and 80 or 255,
        created == count and 255 or 120,
        160
    )
    outputDebugString(("[Renderer limit] density test created %d/%d buildings"):format(created, count))
end

local function outputRendererStats(_, action)
    if action == "reset" then
        engineResetRendererStats()
    end

    local stats = engineGetRendererStats()
    local message = (
        "[Renderer limits] current/high: entities %d/%d of %d, LODs %d/%d of %d, streaming RwObjects %d/%d of %d"
    ):format(
        stats.visibleEntities,
        stats.visibleEntityHighWater,
        stats.visibleEntityCapacity,
        stats.visibleLods,
        stats.visibleLodHighWater,
        stats.visibleLodCapacity,
        stats.streamingRwObjects,
        stats.streamingRwObjectHighWater,
        stats.streamingRwObjectCapacity
    )
    outputChatBox(message, 80, 220, 255)
    outputDebugString(message)
end

addCommandHandler("renderlimittest", startTest)
addCommandHandler("renderdensitytest", startDensityTest)
addCommandHandler("renderstats", outputRendererStats)
addCommandHandler("renderlimitclear", clearTest)
addEventHandler("onClientResourceStop", resourceRoot, clearTest)

outputChatBox("[Renderer limit] /renderlimittest [300-5000], /renderdensitytest [1-2000], /renderstats, /renderlimitclear", 80, 200, 255)
