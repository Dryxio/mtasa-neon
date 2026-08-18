local BALL_MODEL = 2114
local BALL_RADIUS = 0.12
local BALL_MASS = 0.62
local BALL_TURN_MASS = 0.4 * BALL_MASS * BALL_RADIUS * BALL_RADIUS
local BALL_AIR_RESISTANCE = 0.995
local BALL_ELASTICITY = 0.72

local ballCol = false

local function configureBall(object)
    if not isElement(object) or getElementType(object) ~= "object" or not getElementData(object, "dynamicPhysicsHarness") then
        return
    end

    setObjectProperty(object, "mass", BALL_MASS)
    setObjectProperty(object, "turn_mass", BALL_TURN_MASS)
    setObjectProperty(object, "air_resistance", BALL_AIR_RESISTANCE)
    setObjectProperty(object, "elasticity", BALL_ELASTICITY)
end

local function installCollision()
    ballCol = engineLoadCOL({
        spheres = {
            {
                position = { 0, 0, 0 },
                radius = BALL_RADIUS,
                material = 1
            }
        }
    })

    if not isElement(ballCol) then
        outputDebugString("[dynamic-physics] engineLoadCOL(table) failed", 1)
        return false
    end

    if not engineReplaceCOL(ballCol, BALL_MODEL) then
        outputDebugString("[dynamic-physics] engineReplaceCOL failed for basketball model", 1)
        destroyElement(ballCol)
        ballCol = false
        return false
    end

    return true
end

local function getClosestHarnessBall(maxDistance)
    local px, py, pz = getElementPosition(localPlayer)
    local closest = false
    local closestDistance = maxDistance or 30

    for _, object in ipairs(getElementsByType("object", root, true)) do
        if getElementData(object, "dynamicPhysicsHarness") then
            local x, y, z = getElementPosition(object)
            local dx, dy, dz = x - px, y - py, z - pz
            local distance = math.sqrt(dx * dx + dy * dy + dz * dz)
            if distance < closestDistance then
                closest = object
                closestDistance = distance
            end
        end
    end

    return closest, closestDistance
end

addEventHandler("onClientResourceStart", resourceRoot, function()
    local collisionReady = installCollision()
    for _, object in ipairs(getElementsByType("object", root, true)) do
        configureBall(object)
    end

    if collisionReady then
        triggerServerEvent("dynamicPhysicsHarness:clientReady", resourceRoot)
    else
        outputChatBox("[dynamic-physics] collision setup failed; /dophys is blocked", 255, 100, 100)
    end

    outputChatBox("[dynamic-physics] /dophys spawn, /dothrow [speed] [up], /dostatus, /doreset, /doclear", 170, 220, 255)
end)

addEventHandler("onClientElementStreamIn", root, function()
    configureBall(source)
end)

addEventHandler("onClientElementDataChange", root, function(key)
    if key == "dynamicPhysicsHarness" then
        configureBall(source)
    end
end)

addEventHandler("onClientRender", root, function()
    local object, distance = getClosestHarnessBall(30)
    if not isElement(object) then
        return
    end

    configureBall(object)

    local x, y, z = getElementPosition(object)
    local vx, vy, vz = getElementVelocity(object)
    local avx, avy, avz = getElementAngularVelocity(object)
    local speed = math.sqrt(vx * vx + vy * vy + vz * vz)
    local angular = math.sqrt(avx * avx + avy * avy + avz * avz)

    local text = ("DYNAMIC OBJECT PHYSICS\nmodel=%d dist=%.1fm\npos %.3f %.3f %.3f\nvel %.4f %.4f %.4f | %.4f\nang %.4f %.4f %.4f | %.4f")
        :format(getElementModel(object), distance, x, y, z, vx, vy, vz, speed, avx, avy, avz, angular)
    dxDrawText(text, 24, 190, 620, 340, tocolor(255, 255, 255, 235), 1.0, "default-bold", "left", "top", false, false, false)

    dxDrawLine3D(x - BALL_RADIUS, y, z, x + BALL_RADIUS, y, z, tocolor(255, 100, 100, 220), 2)
    dxDrawLine3D(x, y - BALL_RADIUS, z, x, y + BALL_RADIUS, z, tocolor(100, 255, 100, 220), 2)
    dxDrawLine3D(x, y, z - BALL_RADIUS, x, y, z + BALL_RADIUS, tocolor(100, 170, 255, 220), 2)
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    if isElement(ballCol) then
        destroyElement(ballCol)
    end
    ballCol = false
end)
