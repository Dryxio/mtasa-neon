local VISUAL_PARENT_MODEL = 980
local INITIAL_WALL_HEIGHT = 1.2
local WALL_THICKNESS = 0.45
local MIN_WALL_LENGTH = 1.5
local MAX_WALL_LENGTH = 40.0
local MIN_WALL_HEIGHT = 0.45
local MAX_WALL_HEIGHT = 6.0
local VISUAL_BASE_LENGTH = 11.8
local VISUAL_BASE_DEPTH = 1.0
local VISUAL_BASE_HEIGHT = 4.5
local COLLISION_MATERIAL = 1
local COLLISION_UPDATE_MS = 33
local LENGTH_EDIT_SPEED = 4.0
local HEIGHT_EDIT_SPEED = 1.8
local RAMP_ANGLE = 18.0
local RAMP_THICKNESS = 0.35

local wallModel = false
local wallCol = false
local visualObject = false
local collisionObject = false

local mode = "idle"
local startPoint = false
local wallState = false
local wireframeEnabled = false
local lastCollisionUpdate = 0
local lastCollisionSignature = false
local lastFrameTick = getTickCount()

local editKeys = {
    extend = false,
    shorten = false,
    raise = false,
    lower = false
}

local screenW, screenH = guiGetScreenSize()

local function chat(message, r, g, b)
end

local function setEditorInput(enabled)
    showCursor(enabled)
    toggleControl("fire", not enabled)
    toggleControl("aim_weapon", not enabled)
end

local function makeCollision(state)
    if state.shape == "ramp" then
        return {
            boxes = {
                {
                    position = { 0, 0, 0 },
                    size = { state.length, WALL_THICKNESS, RAMP_THICKNESS },
                    material = COLLISION_MATERIAL
                }
            }
        }
    end

    return {
        boxes = {
            {
                position = { 0, 0, 0 },
                size = { state.length, WALL_THICKNESS, state.height },
                material = COLLISION_MATERIAL
            }
        }
    }
end

local function collisionSignature(state)
    return string.format("%s:%.3f:%.3f", state.shape, state.length, state.height)
end

local function destroyWallObjects()
    if isElement(visualObject) then
        destroyElement(visualObject)
    end
    visualObject = false

    if isElement(collisionObject) then
        destroyElement(collisionObject)
    end
    collisionObject = false

    if isElement(wallCol) then
        destroyElement(wallCol)
    end
    wallCol = false

    wallState = false
    lastCollisionSignature = false
end

local function ensureModel()
    if wallModel then
        return true
    end

    wallModel = engineRequestModel("object", VISUAL_PARENT_MODEL)
    if not wallModel then
        chat("engineRequestModel failed; no free object model slot.", 255, 80, 80)
        return false
    end

    return true
end

local function getCursorRay()
    local cursorX, cursorY = getCursorPosition()
    if not cursorX then
        return false
    end

    local farX, farY, farZ = getWorldFromScreenPosition(cursorX * screenW, cursorY * screenH, 300)
    if not farX then
        return false
    end

    local cameraX, cameraY, cameraZ = getCameraMatrix()
    return cameraX, cameraY, cameraZ, farX, farY, farZ
end

local function getCursorWorldHit()
    local cameraX, cameraY, cameraZ, farX, farY, farZ = getCursorRay()
    if not cameraX then
        return false
    end

    local hit, hitX, hitY, hitZ = processLineOfSight(
        cameraX, cameraY, cameraZ,
        farX, farY, farZ,
        true, false, false, true, false,
        false, false, false,
        localPlayer
    )

    if not hit then
        return false
    end

    return hitX, hitY, hitZ
end

local function getCursorPointOnPlane(planeZ)
    local cameraX, cameraY, cameraZ, farX, farY, farZ = getCursorRay()
    if not cameraX then
        return false
    end

    local directionZ = farZ - cameraZ
    if math.abs(directionZ) < 0.0001 then
        return false
    end

    local t = (planeZ - cameraZ) / directionZ
    if t <= 0 then
        return false
    end

    return cameraX + (farX - cameraX) * t,
           cameraY + (farY - cameraY) * t,
           planeZ
end

local function clampEndpoint(x, y)
    local dx = x - startPoint.x
    local dy = y - startPoint.y
    local length = math.sqrt(dx * dx + dy * dy)

    if length < 0.0001 then
        return startPoint.x + MIN_WALL_LENGTH, startPoint.y, MIN_WALL_LENGTH
    end

    local clampedLength = math.max(MIN_WALL_LENGTH, math.min(MAX_WALL_LENGTH, length))
    local scale = clampedLength / length
    return startPoint.x + dx * scale, startPoint.y + dy * scale, clampedLength
end

local function updateCollision(force)
    if not isElement(wallCol) or not wallState then
        return false
    end

    local signature = collisionSignature(wallState)
    local now = getTickCount()
    if not force and signature == lastCollisionSignature then
        return true
    end

    if not force and now - lastCollisionUpdate < COLLISION_UPDATE_MS then
        return true
    end

    if not engineSetCOLData(wallCol, makeCollision(wallState)) then
        chat("engineSetCOLData failed while editing the wall.", 255, 80, 80)
        return false
    end

    lastCollisionUpdate = now
    lastCollisionSignature = signature
    return true
end

local function getRampCenterZ(state)
    local pitch = math.rad(RAMP_ANGLE)
    return state.baseZ + math.sin(pitch) * state.length * 0.5 + math.cos(pitch) * RAMP_THICKNESS * 0.5
end

local function applyWallTransform(forceCollision)
    if not wallState or not isElement(visualObject) or not isElement(collisionObject) then
        return false
    end

    local state = wallState
    local middleX = state.startX + state.dirX * state.length * 0.5
    local middleY = state.startY + state.dirY * state.length * 0.5
    local pitch = 0
    local centerZ
    local visualHeight

    if state.shape == "ramp" then
        pitch = -RAMP_ANGLE
        centerZ = getRampCenterZ(state)
        visualHeight = RAMP_THICKNESS
    else
        centerZ = state.baseZ + state.height * 0.5
        visualHeight = state.height
    end

    setElementPosition(collisionObject, middleX, middleY, centerZ)
    setElementRotation(collisionObject, 0, pitch, state.angle)

    setElementPosition(visualObject, middleX, middleY, centerZ)
    setElementRotation(visualObject, 0, pitch, state.angle)
    setObjectScale(
        visualObject,
        math.max(0.04, state.length / VISUAL_BASE_LENGTH),
        math.max(0.08, WALL_THICKNESS / VISUAL_BASE_DEPTH),
        math.max(0.04, visualHeight / VISUAL_BASE_HEIGHT)
    )

    state.middleX = middleX
    state.middleY = middleY
    state.centerZ = centerZ
    state.pitch = pitch

    return updateCollision(forceCollision)
end

local function applyWallEndpoint(endX, endY, forceCollision)
    if not startPoint or not wallState then
        return false
    end

    local clampedX, clampedY, length = clampEndpoint(endX, endY)
    local dx = clampedX - startPoint.x
    local dy = clampedY - startPoint.y
    local inverseLength = 1 / length

    wallState.length = length
    wallState.dirX = dx * inverseLength
    wallState.dirY = dy * inverseLength
    wallState.angle = math.deg(math.atan2(dy, dx))

    return applyWallTransform(forceCollision)
end

local function createWallAt(x, y, z)
    destroyWallObjects()

    startPoint = { x = x, y = y, z = z + 0.03 }
    wallState = {
        startX = startPoint.x,
        startY = startPoint.y,
        baseZ = startPoint.z,
        dirX = 1,
        dirY = 0,
        angle = 0,
        length = MIN_WALL_LENGTH,
        height = INITIAL_WALL_HEIGHT,
        shape = "wall"
    }

    wallCol = engineLoadCOL(makeCollision(wallState))
    if not isElement(wallCol) then
        chat("engineLoadCOL(table) failed.", 255, 80, 80)
        return false
    end

    if not engineReplaceCOL(wallCol, wallModel) then
        chat("engineReplaceCOL failed for the temporary model.", 255, 80, 80)
        destroyWallObjects()
        return false
    end

    collisionObject = createObject(wallModel, x, y, z + 0.03)
    visualObject = createObject(wallModel, x, y, z + INITIAL_WALL_HEIGHT * 0.5)
    if not isElement(collisionObject) or not isElement(visualObject) then
        chat("Could not create wall objects.", 255, 80, 80)
        destroyWallObjects()
        return false
    end

    setElementFrozen(collisionObject, true)
    setElementFrozen(visualObject, true)
    setElementAlpha(collisionObject, 0)
    setElementCollisionsEnabled(collisionObject, true)
    setElementCollisionsEnabled(visualObject, false)
    setElementDoubleSided(visualObject, true)
    setObjectBreakable(collisionObject, false)
    setObjectBreakable(visualObject, false)

    lastCollisionUpdate = 0
    lastCollisionSignature = false
    return applyWallTransform(true)
end

local function resetDemo(keepEditor)
    destroyWallObjects()
    startPoint = false
    mode = keepEditor and "pick-start" or "idle"
    if not keepEditor then
        setEditorInput(false)
    end
end

local function beginWall()
    if not ensureModel() then
        return
    end

    resetDemo(true)
    mode = "pick-start"
    setEditorInput(true)
    chat("Left-click the ground, move the mouse, then left-click again to lock the wall.")
end

local function toggleRamp()
    if mode ~= "locked" or not wallState then
        return
    end

    wallState.shape = wallState.shape == "ramp" and "wall" or "ramp"
    applyWallTransform(true)
end

local function handleEditKey(keyName, state)
    if mode ~= "locked" then
        return
    end

    local down = state == "down"
    if keyName == "e" then
        editKeys.extend = down
    elseif keyName == "a" then
        editKeys.shorten = down
    elseif keyName == "u" then
        editKeys.raise = down
    elseif keyName == "i" then
        editKeys.lower = down
    elseif keyName == "p" and down then
        toggleRamp()
    end
end

for _, keyName in ipairs({ "a", "e", "u", "i", "p" }) do
    bindKey(keyName, "both", handleEditKey)
end

addCommandHandler("wall", beginWall)

addCommandHandler("wallreset", function()
    resetDemo(false)
end)

addCommandHandler("wallwire", function()
    wireframeEnabled = not wireframeEnabled
    chat("Collision wireframe " .. (wireframeEnabled and "enabled." or "disabled."))
end)

addCommandHandler("wallramp", toggleRamp)

addEventHandler("onClientClick", root, function(button, state)
    if button ~= "left" or state ~= "down" then
        return
    end

    if mode == "pick-start" then
        local x, y, z = getCursorWorldHit()
        if not x then
            return
        end

        if createWallAt(x, y, z) then
            mode = "stretch"
        end
        return
    end

    if mode == "stretch" then
        local x, y = getCursorPointOnPlane(startPoint.z)
        if not x then
            return
        end

        applyWallEndpoint(x, y, true)
        mode = "locked"
        setEditorInput(false)
        chat("Wall locked. Hold A/E for length, U/I for height, P to toggle ramp, /wallwire for collision.")
    end
end)

local function updateStretch()
    if mode ~= "stretch" or not startPoint then
        return
    end

    local x, y = getCursorPointOnPlane(startPoint.z)
    if x then
        applyWallEndpoint(x, y, false)
    end
end

local function updateLockedEdit(deltaSeconds)
    if mode ~= "locked" or not wallState then
        return
    end

    local changed = false
    local lengthDirection = (editKeys.extend and 1 or 0) - (editKeys.shorten and 1 or 0)
    if lengthDirection ~= 0 then
        local nextLength = math.max(MIN_WALL_LENGTH, math.min(MAX_WALL_LENGTH, wallState.length + lengthDirection * LENGTH_EDIT_SPEED * deltaSeconds))
        if nextLength ~= wallState.length then
            wallState.length = nextLength
            changed = true
        end
    end

    if wallState.shape == "wall" then
        local heightDirection = (editKeys.raise and 1 or 0) - (editKeys.lower and 1 or 0)
        if heightDirection ~= 0 then
            local nextHeight = math.max(MIN_WALL_HEIGHT, math.min(MAX_WALL_HEIGHT, wallState.height + heightDirection * HEIGHT_EDIT_SPEED * deltaSeconds))
            if nextHeight ~= wallState.height then
                wallState.height = nextHeight
                changed = true
            end
        end
    end

    if changed then
        applyWallTransform(false)
    end
end

local function transformLocalPoint(state, x, y, z)
    local pitch = math.rad(state.pitch or 0)
    local yaw = math.rad(state.angle or 0)

    local pitchX = x * math.cos(pitch) + z * math.sin(pitch)
    local pitchZ = -x * math.sin(pitch) + z * math.cos(pitch)

    local worldX = state.middleX + pitchX * math.cos(yaw) - y * math.sin(yaw)
    local worldY = state.middleY + pitchX * math.sin(yaw) + y * math.cos(yaw)
    local worldZ = state.centerZ + pitchZ
    return worldX, worldY, worldZ
end

local function drawCollisionWireframe()
    if not wireframeEnabled or not wallState or not isElement(collisionObject) then
        return
    end

    local state = wallState
    local halfLength = state.length * 0.5
    local halfDepth = WALL_THICKNESS * 0.5
    local halfHeight = (state.shape == "ramp" and RAMP_THICKNESS or state.height) * 0.5

    local corners = {
        { -halfLength, -halfDepth, -halfHeight },
        {  halfLength, -halfDepth, -halfHeight },
        {  halfLength,  halfDepth, -halfHeight },
        { -halfLength,  halfDepth, -halfHeight },
        { -halfLength, -halfDepth,  halfHeight },
        {  halfLength, -halfDepth,  halfHeight },
        {  halfLength,  halfDepth,  halfHeight },
        { -halfLength,  halfDepth,  halfHeight }
    }

    local world = {}
    for i, corner in ipairs(corners) do
        world[i] = { transformLocalPoint(state, corner[1], corner[2], corner[3]) }
    end

    local edges = {
        {1,2}, {2,3}, {3,4}, {4,1},
        {5,6}, {6,7}, {7,8}, {8,5},
        {1,5}, {2,6}, {3,7}, {4,8}
    }

    for _, edge in ipairs(edges) do
        local a = world[edge[1]]
        local b = world[edge[2]]
        dxDrawLine3D(a[1], a[2], a[3], b[1], b[2], b[3], tocolor(0, 220, 255, 235), 3)
    end
end

addEventHandler("onClientRender", root, function()
    local now = getTickCount()
    local deltaSeconds = math.min(0.1, math.max(0, now - lastFrameTick) / 1000)
    lastFrameTick = now

    updateStretch()
    updateLockedEdit(deltaSeconds)
    drawCollisionWireframe()
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    setEditorInput(false)
    destroyWallObjects()

    if wallModel then
        engineFreeModel(wallModel)
        wallModel = false
    end
end)

chat("Ready: /wall. After locking: hold A/E length, U/I height, P ramp, /wallwire collision.")
