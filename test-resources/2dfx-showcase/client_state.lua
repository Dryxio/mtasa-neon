DFXShowcase = DFXShowcase or {}

local S = DFXShowcase

S.FULL_DURATION = 38000
S.RUNWAY_START = { 100.74450, 2501.11401, 16.48438 }
S.RUNWAY_END = { 424.00473, 2503.03442, 16.48438 }
S.RUNWAY_HALF_WIDTH = 17.0
S.PALETTE = {
    tocolor(35, 235, 255, 255),
    tocolor(55, 130, 255, 255),
    tocolor(120, 75, 255, 255),
    tocolor(220, 70, 255, 255),
    tocolor(255, 75, 175, 255),
    tocolor(240, 248, 255, 255),
}

S.state = {
    prepared = false,
    active = false,
    mode = "full",
    shot = nil,
    startedAt = 0,
    dimension = 0,
    allocatedModels = {},
    touchedModels = {},
    objects = {},
    timers = {},
    edgeModels = { left = {}, right = {} },
    edgeEffects = { left = {}, right = {} },
    centerModels = {},
    centerEffects = {},
    thresholdModels = {},
    thresholdEffects = {},
    moonModels = {},
    moonEffects = {},
    particleModels = {},
    roadsignModels = {},
    sunModel = nil,
    vanillaModel = nil,
    vanillaObject = nil,
    vanillaEffect = nil,
    vanillaBaseline = nil,
    lightTemplate = nil,
    cues = {},
    chaseStep = -1,
    moonStep = -1,
    finalStep = -1,
    savedTime = nil,
    savedWeather = nil,
    basis = nil,
}

local state = S.state

function S.log(message, level)
    outputDebugString("[2dfx-showcase] " .. tostring(message), level or 3)
end

function S.chat(message, r, g, b)
    outputChatBox("[2DFX SHOWCASE] " .. tostring(message), r or 100, g or 220, b or 255)
end

function S.clamp(value, minimum, maximum)
    return math.max(minimum, math.min(maximum, value))
end

function S.smoothstep(value)
    value = S.clamp(value, 0, 1)
    return value * value * (3 - 2 * value)
end

function S.lerp(a, b, t)
    return a + (b - a) * t
end

function S.cameraLerp(a, b, t)
    t = S.smoothstep(t)
    setCameraMatrix(
        S.lerp(a[1], b[1], t), S.lerp(a[2], b[2], t), S.lerp(a[3], b[3], t),
        S.lerp(a[4], b[4], t), S.lerp(a[5], b[5], t), S.lerp(a[6], b[6], t),
        0, S.lerp(a[7] or 70, b[7] or 70, t)
    )
end

function S.configureRunway()
    local sx, sy, sz = S.RUNWAY_START[1], S.RUNWAY_START[2], S.RUNWAY_START[3]
    local ex, ey, ez = S.RUNWAY_END[1], S.RUNWAY_END[2], S.RUNWAY_END[3]
    local dx, dy = ex - sx, ey - sy
    local length = math.sqrt(dx * dx + dy * dy)
    local ux, uy = dx / length, dy / length
    local yaw = math.deg(math.atan2(-ux, uy))
    if yaw < 0 then
        yaw = yaw + 360
    end
    state.basis = {
        sx = sx, sy = sy, sz = sz,
        ex = ex, ey = ey, ez = ez,
        ux = ux, uy = uy,
        px = -uy, py = ux,
        length = length,
        yaw = yaw,
    }
end

function S.runwayPoint(t, lateral, height)
    if not state.basis then
        S.configureRunway()
    end
    local b = state.basis
    lateral = lateral or 0
    height = height or 0
    local x = b.sx + (b.ex - b.sx) * t + b.px * lateral
    local y = b.sy + (b.ey - b.sy) * t + b.py * lateral
    local z = b.sz + (b.ez - b.sz) * t + height
    return x, y, z
end

function S.cameraPoint(t, lateral, height, lookT, lookLateral, lookHeight, fov)
    local x, y, z = S.runwayPoint(t, lateral, height)
    local lx, ly, lz = S.runwayPoint(lookT, lookLateral or 0, lookHeight or 0)
    return { x, y, z, lx, ly, lz, fov or 70 }
end

function S.setPresentationUI(visible)
    showPlayerHudComponent("all", visible)
    showChat(visible)
end

function S.rememberTimer(timer)
    state.timers[#state.timers + 1] = timer
    return timer
end

function S.later(delay, callback)
    return S.rememberTimer(setTimer(function()
        if state.prepared then
            callback()
        end
    end, delay, 1))
end

function S.clearTimers()
    for _, timer in ipairs(state.timers) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    state.timers = {}
end

function S.destroyObjects()
    for _, object in ipairs(state.objects) do
        if isElement(object) then
            destroyElement(object)
        end
    end
    state.objects = {}
end

function S.rememberTouchedModel(model)
    state.touchedModels[model] = true
end

function S.resetTouchedModels()
    for model in pairs(state.touchedModels) do
        resetModel2DFXEffects(model)
    end
    state.touchedModels = {}
end

function S.freeRuntimeModels()
    for _, model in ipairs(state.allocatedModels) do
        engineFreeModel(model)
    end
    state.allocatedModels = {}
end

function S.restoreEnvironment()
    if state.savedTime then
        setTime(state.savedTime.hour, state.savedTime.minute)
    end
    if type(state.savedWeather) == "number" then
        setWeather(state.savedWeather)
    end
end

function S.cleanupLocal()
    S.clearTimers()
    state.active = false
    state.prepared = false
    if S.renderShow then
        removeEventHandler("onClientRender", root, S.renderShow)
    end
    S.resetTouchedModels()
    S.destroyObjects()
    S.freeRuntimeModels()
    S.restoreEnvironment()
    setCameraTarget(localPlayer)
    S.setPresentationUI(true)

    state.edgeModels = { left = {}, right = {} }
    state.edgeEffects = { left = {}, right = {} }
    state.centerModels = {}
    state.centerEffects = {}
    state.thresholdModels = {}
    state.thresholdEffects = {}
    state.moonModels = {}
    state.moonEffects = {}
    state.particleModels = {}
    state.roadsignModels = {}
    state.sunModel = nil
    state.vanillaModel = nil
    state.vanillaObject = nil
    state.vanillaEffect = nil
    state.vanillaBaseline = nil
    state.lightTemplate = nil
    state.cues = {}
    state.chaseStep = -1
    state.moonStep = -1
    state.finalStep = -1
    state.savedTime = nil
    state.savedWeather = nil
    state.basis = nil
end

function S.failSetup(reason)
    S.log("setup failed: " .. tostring(reason), 1)
    triggerServerEvent("2dfxShowcase:failed", resourceRoot, tostring(reason))
end

function S.requestObjectModel(parent)
    local model = engineRequestModel("object", parent)
    if not model then
        return false
    end
    state.allocatedModels[#state.allocatedModels + 1] = model
    return model
end

function S.spawnWorld(model, x, y, z, rz, scale, alpha)
    local object = createObject(model, x, y, z, 0, 0, rz or 0)
    if not isElement(object) then
        return false
    end

    setElementDimension(object, state.dimension)
    setElementInterior(object, 0)
    setElementCollisionsEnabled(object, false)
    setElementFrozen(object, true)
    if scale then
        setObjectScale(object, scale)
    end
    if alpha ~= nil then
        setElementAlpha(object, alpha)
    end

    state.objects[#state.objects + 1] = object
    return object
end

function S.spawnOnRunway(model, t, lateral, height, alpha, scale, yawOffset)
    local x, y, z = S.runwayPoint(t, lateral, height)
    local yaw = (state.basis and state.basis.yaw or 270) + (yawOffset or 0)
    return S.spawnWorld(model, x, y, z, yaw, scale, alpha)
end
