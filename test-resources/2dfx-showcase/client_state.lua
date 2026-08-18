DFXShowcase = DFXShowcase or {}

local S = DFXShowcase

S.FULL_DURATION = 42000
S.PALETTE = {
    tocolor(40, 235, 255, 255),
    tocolor(60, 125, 255, 255),
    tocolor(125, 70, 255, 255),
    tocolor(220, 65, 255, 255),
    tocolor(255, 70, 175, 255),
    tocolor(235, 245, 255, 255),
}

S.state = {
    prepared = false,
    active = false,
    mode = "full",
    shot = nil,
    startedAt = 0,
    cx = 0, cy = 0, cz = 0,
    dimension = 0,
    allocatedModels = {},
    touchedModels = {},
    objects = {},
    timers = {},
    lampModels = {},
    lampEffects = {},
    heroModel = nil,
    heroEffect = nil,
    vanillaModel = nil,
    vanillaObject = nil,
    vanillaEffect = nil,
    vanillaBaseline = nil,
    particleModels = {},
    roadsignModels = {},
    escalatorModels = {},
    sunModel = nil,
    cues = {},
    lastFinalStep = -1,
    savedTime = nil,
    savedWeather = nil,
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

    state.lampModels = {}
    state.lampEffects = {}
    state.heroModel = nil
    state.heroEffect = nil
    state.vanillaModel = nil
    state.vanillaObject = nil
    state.vanillaEffect = nil
    state.vanillaBaseline = nil
    state.particleModels = {}
    state.roadsignModels = {}
    state.escalatorModels = {}
    state.sunModel = nil
    state.cues = {}
    state.lastFinalStep = -1
    state.savedTime = nil
    state.savedWeather = nil
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

function S.spawn(model, ox, oy, oz, rz, scale, alpha)
    local object = createObject(model, state.cx + ox, state.cy + oy, state.cz + oz, 0, 0, rz or 0)
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
    if alpha then
        setElementAlpha(object, alpha)
    end

    state.objects[#state.objects + 1] = object
    return object
end
