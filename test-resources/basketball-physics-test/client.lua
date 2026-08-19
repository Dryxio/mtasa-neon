local BALL_MODEL, BALL_RADIUS = 2114, 0.12
local RIM_RADIUS = 0.23
local RIM_PIPE_RADIUS, RIM_EDGE_STEPS = 0.03, 4
local RIM_LOCAL_X, RIM_LOCAL_Y, RIM_LOCAL_Z = 0.07342529296875, 0.5001220703125, 0.9108123779296875
local BOARD_OFFSET_X, BOARD_OFFSET_Y, BOARD_OFFSET_Z = -0.06488037109375, -0.394287109375, 0.5519981384277344
local CALIBRATION_STEP = 0.01
local CALIBRATION_HEADING_STEP = 1.0

-- GTA6-style shot presentation: the reticle itself is the power meter.
-- It starts large and contracts toward the center while E is held.
local CHARGE_DURATION_MS = 1200
local RETICLE_MAX_RADIUS = 42
local RETICLE_MIN_RADIUS = 10
local RETICLE_SEGMENTS = 40

-- Mirror the authoritative server formula only for the temporary tuning HUD.
local SHOT_HORIZONTAL_MIN = 0.055
local SHOT_HORIZONTAL_MAX = 0.165
local SHOT_VERTICAL_MIN = 0.115
local SHOT_VERTICAL_POWER_GAIN = 0.065
local SHOT_VERTICAL_AIM_GAIN = 0.045

-- Centerline of model 947's visible rim, extracted from the supplied
-- bskballhub_lax01.dff and expressed relative to the rim center.
local RIM_PATH = {
    { 0.132629394531, -0.197875976562 },
    { -0.065856933594, -0.222534179688 },
    { -0.225769042969, -0.116821289062 },
    { -0.253601074219, 0.057250976562 },
    { -0.132751464844, 0.197875976562 },
    { 0.065979003906, 0.222534179688 },
    { 0.225891113281, 0.116821289062 },
    { 0.253479003906, -0.057250976562 },
}

local ballCol, rimCol, boardCol = false, false, false
local rimModel, boardModel = false, false
local rimObject, boardObject = false, false
local court, trackedBall = false, false
local aiming, charging, chargeStart = false, false, 0
local calibrating = false
local scoreArmed, previousBallZ, lastScoreSignal = false, false, 0
local screenW, screenH = guiGetScreenSize()

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end

local function rotateLocal(x, y, heading)
    local r = math.rad(heading)
    local c, s = math.cos(r), math.sin(r)
    return x * c - y * s, x * s + y * c
end

local function smoothstep(value)
    value = clamp(value, 0, 1)
    return value * value * (3 - 2 * value)
end

local function getChargePower()
    if not charging then return 0 end
    return smoothstep((getTickCount() - chargeStart) / CHARGE_DURATION_MS)
end

local function getAimVector()
    local cameraX, cameraY, cameraZ, targetX, targetY, targetZ = getCameraMatrix()
    local dx, dy, dz = targetX - cameraX, targetY - cameraY, targetZ - cameraZ
    local length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length < 0.001 then return false end

    dx, dy, dz = dx / length, dy / length, dz / length
    local horizontalLength = math.sqrt(dx * dx + dy * dy)
    if horizontalLength < 0.05 then return false end

    return dx / horizontalLength, dy / horizontalLength, clamp(dz, -0.65, 0.75)
end

local function configureBall(object)
    if not isElement(object) or getElementType(object) ~= "object" or not getElementData(object, "basketPhysicsTest") then return false end
    setObjectProperty(object, "mass", 0.62)
    setObjectProperty(object, "turn_mass", 0.4 * 0.62 * BALL_RADIUS * BALL_RADIUS)
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
    for i = 1, #RIM_PATH do
        local current = RIM_PATH[i]
        local nextPoint = RIM_PATH[i % #RIM_PATH + 1]
        for step = 0, RIM_EDGE_STEPS - 1 do
            local t = step / RIM_EDGE_STEPS
            spheres[#spheres + 1] = {
                position = {
                    current[1] + (nextPoint[1] - current[1]) * t,
                    current[2] + (nextPoint[2] - current[2]) * t,
                    0
                },
                radius = RIM_PIPE_RADIUS,
                material = 1
            }
        end
    end

    rimCol = engineLoadCOL({ spheres = spheres })
    boardCol = engineLoadCOL({ boxes = {{ position = {0, 0, 0}, size = {1.7632, 0.0784, 1.5101}, material = 1 }} })
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

    local boardX, boardY = rotateLocal(BOARD_OFFSET_X, BOARD_OFFSET_Y, court.heading)
    rimObject = createObject(rimModel, court.x, court.y, court.z, 0, 0, court.heading)
    boardObject = createObject(boardModel, court.x + boardX, court.y + boardY, court.z + BOARD_OFFSET_Z, 0, 0, court.heading)
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
    if not holding() then return end

    local dirX, dirY, aimZ = getAimVector()
    if not dirX then
        outputChatBox("[basket-test] Camera aim is too vertical to shoot.", 255, 140, 140)
        return
    end

    power = clamp(power, 0, 1)
    aiming, charging = false, false
    triggerServerEvent("basketPhysics:shoot", resourceRoot, dirX, dirY, aimZ, power)
end

local function pickup()
    local object = findBall()
    if not isElement(object) or getElementData(object, "basketPhysics:holder") then return end
    local px, py, pz = getElementPosition(localPlayer)
    local bx, by, bz = getElementPosition(object)
    local dx, dy, dz = bx - px, by - py, bz - pz
    if dx * dx + dy * dy + dz * dz <= 3.24 then triggerServerEvent("basketPhysics:pickup", resourceRoot) end
end

local function nudgeCourt(dx, dy, dz, dHeading)
    if not calibrating or not court then return end
    triggerServerEvent("basketPhysics:calibrate", resourceRoot, dx, dy, dz, dHeading)
end

local function getCalibratedOrigin()
    if not court then return false end
    local rimOffsetX, rimOffsetY = rotateLocal(RIM_LOCAL_X, RIM_LOCAL_Y, court.heading)
    return court.x - rimOffsetX, court.y - rimOffsetY, court.z - RIM_LOCAL_Z
end

local function copyCalibration()
    if not court then
        outputChatBox("[basket-test] No active court. Run /baskettest first.", 255, 140, 140)
        return
    end

    local objectX, objectY, objectZ = getCalibratedOrigin()
    local text = ("COURT_OBJECT_X=%.6f, COURT_OBJECT_Y=%.6f, COURT_OBJECT_Z=%.6f, COURT_OBJECT_HEADING=%.3f")
        :format(objectX, objectY, objectZ, court.heading)
    setClipboard(text)
    outputChatBox("[basket-test] Calibration coords copied to clipboard:", 170, 230, 255)
    outputChatBox("[basket-test] " .. text, 235, 235, 235)
end

local function drawReticleCircle(centerX, centerY, radius, color)
    local previousX, previousY
    local firstX, firstY
    for segment = 0, RETICLE_SEGMENTS do
        local angle = math.pi * 2 * segment / RETICLE_SEGMENTS
        local x = centerX + math.cos(angle) * radius
        local y = centerY + math.sin(angle) * radius
        if previousX then
            dxDrawLine(previousX, previousY, x, y, color, 2)
        else
            firstX, firstY = x, y
        end
        previousX, previousY = x, y
    end
    if previousX and firstX then
        dxDrawLine(previousX, previousY, firstX, firstY, color, 2)
    end
end

local function drawAimReticle()
    local power = getChargePower()
    local radius = RETICLE_MAX_RADIUS - (RETICLE_MAX_RADIUS - RETICLE_MIN_RADIUS) * power
    local centerX, centerY = screenW * 0.5, screenH * 0.5
    local color = tocolor(245, 245, 245, 235)

    drawReticleCircle(centerX, centerY, radius, color)
    dxDrawRectangle(centerX - 1.5, centerY - 1.5, 3, 3, color)

    local dirX, dirY, aimZ = getAimVector()
    if dirX then
        local horizontalSpeed = SHOT_HORIZONTAL_MIN + (SHOT_HORIZONTAL_MAX - SHOT_HORIZONTAL_MIN) * power
        local verticalSpeed = clamp(SHOT_VERTICAL_MIN + SHOT_VERTICAL_POWER_GAIN * power + SHOT_VERTICAL_AIM_GAIN * aimZ, 0.08, 0.24)
        dxDrawText(("%3.0f%%  aimZ %.2f  h %.3f  v %.3f"):format(power * 100, aimZ, horizontalSpeed, verticalSpeed),
            centerX - 180, centerY + radius + 12, centerX + 180, centerY + radius + 32,
            tocolor(255, 255, 255, 205), 0.9, "default-bold", "center", "top")
    end
end

-- Mac-friendly keyboard controls: A toggles aim, E charges/releases the shot.
bindKey("a", "down", function()
    if calibrating then return end
    if not holding() then
        aiming, charging = false, false
        return
    end
    aiming = not aiming
    if not aiming then charging = false end
end)

bindKey("e", "both", function(_, state)
    if calibrating then return end
    if state == "down" and aiming and holding() then
        charging, chargeStart = true, getTickCount()
        return
    end
    if state == "up" and charging and aiming and holding() then
        shoot(getChargePower())
    end
    charging = false
end)

bindKey("f", "down", function()
    if not calibrating then pickup() end
end)

bindKey("arrow_l", "down", function() nudgeCourt(-CALIBRATION_STEP, 0, 0, 0) end)
bindKey("arrow_r", "down", function() nudgeCourt(CALIBRATION_STEP, 0, 0, 0) end)
bindKey("arrow_u", "down", function() nudgeCourt(0, CALIBRATION_STEP, 0, 0) end)
bindKey("arrow_d", "down", function() nudgeCourt(0, -CALIBRATION_STEP, 0, 0) end)
bindKey("pgup", "down", function() nudgeCourt(0, 0, CALIBRATION_STEP, 0) end)
bindKey("pgdn", "down", function() nudgeCourt(0, 0, -CALIBRATION_STEP, 0) end)
bindKey("z", "down", function() nudgeCourt(0, 0, 0, -CALIBRATION_HEADING_STEP) end)
bindKey("x", "down", function() nudgeCourt(0, 0, 0, CALIBRATION_HEADING_STEP) end)
bindKey("c", "down", function()
    if calibrating then copyCalibration() end
end)

addCommandHandler("basketcal", function()
    if not court then
        outputChatBox("[basket-test] Run /baskettest first.", 255, 140, 140)
        return
    end
    calibrating = not calibrating
    aiming, charging = false, false
    if calibrating then
        outputChatBox("[basket-test] CALIBRATION ON: arrows X/Y, PgUp/PgDn Z, Z/X rotate, C copy.", 170, 230, 255)
    else
        outputChatBox("[basket-test] Calibration off.", 170, 230, 255)
    end
end)
addCommandHandler("basketcopy", copyCalibration)
addCommandHandler("basketpickup", pickup)
addCommandHandler("basketshot", function(_, value)
    shoot(clamp(tonumber(value) or 0.65, 0, 1))
end)

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
    local firstX, firstY
    local prevX, prevY
    for i = 1, #RIM_PATH do
        local ox, oy = rotateLocal(RIM_PATH[i][1], RIM_PATH[i][2], court.heading)
        local x, y = court.x + ox, court.y + oy
        if prevX then dxDrawLine3D(prevX, prevY, court.z, x, y, court.z, tocolor(255, 145, 40, 240), 3) end
        if not firstX then firstX, firstY = x, y end
        prevX, prevY = x, y
    end
    if prevX and firstX then dxDrawLine3D(prevX, prevY, court.z, firstX, firstY, court.z, tocolor(255, 145, 40, 240), 3) end
end

addEventHandler("onClientRender", root, function()
    local object = findBall()
    if isElement(object) then configureBall(object) updateScore(object) end
    drawCourt()
    if isElement(object) then
        local vx, vy, vz = getElementVelocity(object)
        local ax, ay, az = getElementAngularVelocity(object)
        local score = getElementData(localPlayer, "basketPhysics:score") or 0
        dxDrawText(("BASKET PHYSICS | score %d\nvel %.4f %.4f %.4f | spin %.4f %.4f %.4f\nA free aim | hold/release E shoot | F pickup | /basketcal"):format(score, vx, vy, vz, ax, ay, az), 24, 24, 760, 115, tocolor(255,255,255,235), 1, "default-bold")
    end

    if calibrating and court then
        local objectX, objectY, objectZ = getCalibratedOrigin()
        local text = ("HOOP CALIBRATION\nArrows: X/Y  |  PgUp/PgDn: Z  |  Z/X: heading  |  C: copy\nRim %.4f %.4f %.4f  heading %.1f\nObject origin %.4f %.4f %.4f")
            :format(court.x, court.y, court.z, court.heading, objectX, objectY, objectZ)
        dxDrawText(text, 24, 125, 850, 220, tocolor(255, 210, 90, 245), 1, "default-bold", "left", "top", false, false, false)
    end

    if aiming and holding() then
        drawAimReticle()
    end
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    if not installBallCollision() then outputDebugString("[basket-test] ball collision setup failed", 1) end
    if not ensureHoopModels() then outputDebugString("[basket-test] hoop collision setup failed", 1) end
    if readCourt() then rebuildCourt() end
    for _, object in ipairs(getElementsByType("object", root, true)) do configureBall(object) end
    outputChatBox("[basket-test] /baskettest | A free aim | hold/release E shoot | F pickup | /basketcal", 170,230,255)
end)
addEventHandler("onClientElementStreamIn", root, function() configureBall(source) end)
addEventHandler("onClientElementDataChange", root, function(key)
    if source == resourceRoot then
        if key == "basketPhysics:revision" then
            rebuildCourt()
        elseif key == "basketPhysics:active" and not getElementData(resourceRoot, "basketPhysics:active") then
            court = false
            destroyCourtObjects()
        end
    else
        configureBall(source)
    end
end)
addEventHandler("onClientResourceStop", resourceRoot, function()
    destroyCourtObjects()
    for _, col in ipairs({rimCol, boardCol, ballCol}) do if isElement(col) then destroyElement(col) end end
    if rimModel then engineFreeModel(rimModel) end
    if boardModel then engineFreeModel(boardModel) end
end)
