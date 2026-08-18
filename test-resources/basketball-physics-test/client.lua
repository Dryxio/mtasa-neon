local BALL_MODEL, BALL_RADIUS = 2114, 0.12
local RIM_RADIUS, RIM_SEGMENTS = 0.23, 20
local ballCol, rimCol, boardCol = false, false, false
local rimModel, boardModel = false, false
local rimObject, boardObject = false, false
local court, trackedBall = false, false
local aiming, charging, chargeStart = false, false, 0
local scoreArmed, previousBallZ, lastScoreSignal = false, false, 0
local screenW, screenH = guiGetScreenSize()

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end

local function configureBall(object)
    if not isElement(object) or getElementType(object) ~= "object" or not getElementData(object, "basketPhysicsTest") then return false end
    setObjectProperty(object, "mass", 0.62)
    setObjectProperty(object, "turn_mass", 0.08)
    setObjectProperty(object, "air_resistance", 0.995)
    setObjectProperty(object, "elasticity", 0.72)
    trackedBall = object
    return true
end

local function findBall()
    if isElement(trackedBall) then return trackedBall end
    for _, object in ipairs(getElementsByType("object", root, true)) do
        if configureBall(object) then return object end
    end
    return false
end

local function installBallCollision()
    ballCol = engineLoadCOL({ spheres = {{ position = {0, 0, 0}, radius = BALL_RADIUS, material = 1 }} })
    return isElement(ballCol) and engineReplaceCOL(ballCol, BALL_MODEL)
end

local function ensureHoopModels()
    if rimModel and boardModel then return true end
    rimModel, boardModel = engineRequestModel("object", 1337), engineRequestModel("object", 1337)
    if not rimModel or not boardModel then return false end

    local spheres = {}
    for i = 0, RIM_SEGMENTS - 1 do
        local a = math.pi * 2 * i / RIM_SEGMENTS
        spheres[#spheres + 1] = { position = {math.cos(a) * RIM_RADIUS, math.sin(a) * RIM_RADIUS, 0}, radius = 0.03, material = 1 }
    end
    rimCol = engineLoadCOL({ spheres = spheres })
    boardCol = engineLoadCOL({ boxes = {{ position = {0, 0, 0}, size = {1.8, 0.08, 1.05}, material = 1 }} })
    return isElement(rimCol) and isElement(boardCol) and engineReplaceCOL(rimCol, rimModel) and engineReplaceCOL(boardCol, boardModel)
end

local function destroyCourtObjects()
    if isElement(rimObject) then destroyElement(rimObject) end
    if isElement(boardObject) then destroyElement(boardObject) end
    rimObject, boardObject = false, false
end

local function readCourt()
    if not getElementData(resourceRoot, "basketPhysics:active") then court = false return false end
    local x = tonumber(getElementData(resourceRoot, "basketPhysics:ringX"))
    local y = tonumber(getElementData(resourceRoot, "basketPhysics:ringY"))
    local z = tonumber(getElementData(resourceRoot, "basketPhysics:ringZ"))
    local h = tonumber(getElementData(resourceRoot, "basketPhysics:heading"))
    if not x or not y or not z or not h then return false end
    court = {x = x, y = y, z = z, heading = h}
    return true
end

local function rebuildCourt()
    destroyCourtObjects()
    if not readCourt() or not ensureHoopModels() then return end
    local r = math.rad(court.heading)
    local fx, fy = -math.sin(r), math.cos(r)
    rimObject = createObject(rimModel, court.x, court.y, court.z)
    boardObject = createObject(boardModel, court.x + fx * 0.30, court.y + fy * 0.30, court.z + 0.38, 0, 0, court.heading)
    for _, object in ipairs({rimObject, boardObject}) do
        if isElement(object) then
            setElementFrozen(object, true)
            setElementAlpha(object, 0)
            setElementCollisionsEnabled(object, true)
            setObjectBreakable(object, false)
        end
    end
    scoreArmed, previousBallZ = false, false
end

local function holding()
    local object = findBall()
    return isElement(object) and getElementData(object, "basketPhysics:holder") == localPlayer
end

local function shoot(power)
    if holding() then
        aiming, charging = false, false
        triggerServerEvent("basketPhysics:shoot", resourceRoot, clamp(power, 0, 1))
    end
end

local function pickup()
    local object = findBall()
    if not isElement(object) or getElementData(object, "basketPhysics:holder") then return end
    local px, py, pz = getElementPosition(localPlayer)
    local bx, by, bz = getElementPosition(object)
    local dx, dy, dz = bx - px, by - py, bz - pz
    if dx * dx + dy * dy + dz * dz <= 3.24 then triggerServerEvent("basketPhysics:pickup", resourceRoot) end
end

bindKey("mouse2", "both", function(_, state)
    aiming = holding() and state == "down"
    if not aiming then charging = false end
end)
bindKey("mouse1", "both", function(_, state)
    if state == "down" and aiming and holding() then charging, chargeStart = true, getTickCount() return end
    if state == "up" and charging and aiming and holding() then
        local c = clamp((getTickCount() - chargeStart) / 1200, 0, 1)
        shoot(c * c * (3 - 2 * c))
    end
    charging = false
end)
bindKey("e", "down", pickup)
addCommandHandler("basketpickup", pickup)
addCommandHandler("basketshot", function(_, value) shoot(clamp(tonumber(value) or 0.65, 0, 1)) end)

local function updateScore(object)
    if not court or not isElement(object) or not getElementData(object, "basketPhysics:free") then scoreArmed, previousBallZ = false, false return end
    local x, y, z = getElementPosition(object)
    local _, _, vz = getElementVelocity(object)
    local dx, dy = x - court.x, y - court.y
    local radial = dx * dx + dy * dy
    local upper, lower = court.z + 0.16, court.z - 0.16
    if previousBallZ and vz < 0 then
        if not scoreArmed and previousBallZ > upper and z <= upper and radial <= (RIM_RADIUS * 0.92) ^ 2 then scoreArmed = true end
        if scoreArmed and previousBallZ > lower and z <= lower and radial <= (RIM_RADIUS * 0.92) ^ 2 then
            local now = getTickCount()
            if now - lastScoreSignal > 1000 then lastScoreSignal = now triggerServerEvent("basketPhysics:score", resourceRoot, object) end
            scoreArmed = false
        end
    end
    if z < court.z - 1 or radial > 4 then scoreArmed = false end
    previousBallZ = z
end

local function drawCourt()
    if not court then return end
    local prev
    for i = 0, RIM_SEGMENTS do
        local a = math.pi * 2 * i / RIM_SEGMENTS
        local x, y = court.x + math.cos(a) * RIM_RADIUS, court.y + math.sin(a) * RIM_RADIUS
        if prev then dxDrawLine3D(prev[1], prev[2], court.z, x, y, court.z, tocolor(255, 145, 40, 240), 3) end
        prev = {x, y}
    end
end

addEventHandler("onClientRender", root, function()
    local object = findBall()
    if isElement(object) then configureBall(object) updateScore(object) end
    drawCourt()
    if isElement(object) then
        local vx, vy, vz = getElementVelocity(object)
        local ax, ay, az = getElementAngularVelocity(object)
        local score = getElementData(localPlayer, "basketPhysics:score") or 0
        dxDrawText(("BASKET PHYSICS | score %d\nvel %.4f %.4f %.4f | spin %.4f %.4f %.4f\nRMB aim | hold/release LMB | E pickup | /basketshot [0..1]"):format(score, vx, vy, vz, ax, ay, az), 24, 24, 700, 115, tocolor(255,255,255,235), 1, "default-bold")
    end
    if aiming and holding() then
        local p = charging and clamp((getTickCount() - chargeStart) / 1200, 0, 1) or 0
        local w, h = 260, 16 local x, y = (screenW - w) * 0.5, screenH * 0.82
        dxDrawRectangle(x, y, w, h, tocolor(0,0,0,170))
        dxDrawRectangle(x + 2, y + 2, (w - 4) * p, h - 4, tocolor(245,245,245,230))
        dxDrawText("THROW", x, y - 24, x + w, y, tocolor(255,255,255,240), 1, "default-bold", "center", "bottom")
    end
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    if not installBallCollision() then outputDebugString("[basket-test] ball collision setup failed", 1) end
    if not ensureHoopModels() then outputDebugString("[basket-test] hoop collision setup failed", 1) end
    if readCourt() then rebuildCourt() end
    for _, object in ipairs(getElementsByType("object", root, true)) do configureBall(object) end
    outputChatBox("[basket-test] /baskettest, RMB aim, hold/release LMB, E pickup, /basketshot [0..1]", 170,230,255)
end)
addEventHandler("onClientElementStreamIn", root, function() configureBall(source) end)
addEventHandler("onClientElementDataChange", root, function(key)
    if source == resourceRoot and key and key:find("^basketPhysics:") then rebuildCourt() else configureBall(source) end
end)
addEventHandler("onClientResourceStop", resourceRoot, function()
    destroyCourtObjects()
    for _, col in ipairs({rimCol, boardCol, ballCol}) do if isElement(col) then destroyElement(col) end end
    if rimModel then engineFreeModel(rimModel) end
    if boardModel then engineFreeModel(boardModel) end
end)