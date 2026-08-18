local show = {
    active = false,
    startedAt = 0,
    cx = 0, cy = 0, cz = 0,
    dimension = 0,
    objects = {},
    effects = {},
    wave = {},
    finale = {},
    finished = false,
}

local playground = {
    object = nil,
    effect = nil,
    options = nil,
}

local function clamp(v, a, b) return math.max(a, math.min(b, v)) end
local function smoothstep(t) t = clamp(t, 0, 1); return t * t * (3 - 2 * t) end
local function lerp(a, b, t) return a + (b - a) * t end

local function cameraLerp(a, b, t)
    t = smoothstep(t)
    setCameraMatrix(
        lerp(a[1], b[1], t), lerp(a[2], b[2], t), lerp(a[3], b[3], t),
        lerp(a[4], b[4], t), lerp(a[5], b[5], t), lerp(a[6], b[6], t), 0,
        lerp(a[7] or 70, b[7] or 70, t)
    )
end

local function presentation(visible)
    showPlayerHudComponent("all", visible)
    showChat(visible)
end

local function destroyList(list)
    for _, element in ipairs(list) do
        if isElement(element) then destroyElement(element) end
    end
end

local function stopShow()
    destroyList(show.effects)
    destroyList(show.objects)
    show.objects, show.effects, show.wave, show.finale = {}, {}, {}, {}
    show.active = false
    show.finished = false
    setCameraTarget(localPlayer)
    presentation(true)
end

local function clearPlayground()
    if isElement(playground.object) then clearObjectBreakProfile(playground.object) end
    if isElement(playground.effect) then destroyElement(playground.effect) end
    if isElement(playground.object) then destroyElement(playground.object) end
    playground.object = nil
    playground.effect = nil
    playground.options = nil
end

local function spawn(model, x, y, z, scale, rz, bucket)
    local object = createObject(model, x, y, z, 0, 0, rz or 0)
    if not isElement(object) then return nil end
    setElementDimension(object, show.dimension)
    setElementFrozen(object, true)
    if scale then setObjectScale(object, scale) end
    show.objects[#show.objects + 1] = object
    if bucket then bucket[#bucket + 1] = object end
    return object
end

local function buildScene()
    local cx, cy, cz = show.cx, show.cy, show.cz
    local models = {1337, 1225, 1218, 1299, 1429, 1437}

    for i = 1, 14 do
        local row = math.floor((i - 1) / 7)
        local col = (i - 1) % 7
        local x = cx - 10.5 + col * 3.5
        local y = cy + 5 + row * 5.0
        local model = models[((i - 1) % #models) + 1]
        spawn(model, x, y, cz + 0.5, 0.85 + (i % 4) * 0.12, (i * 23) % 360, show.wave)
    end

    for level = 0, 2 do
        for col = -1, 1 do
            spawn((level + col) % 2 == 0 and 1337 or 1218, cx + col * 3.2, cy + 18, cz + 0.7 + level * 2.5,
                  1.05 + level * 0.12, level * 20 + col * 15, show.finale)
        end
    end

    spawn(980, cx - 8, cy + 24, cz + 2.2, 1.15, 0, show.finale)
    spawn(1634, cx + 8, cy + 24, cz + 0.6, 1.05, 180, show.finale)
end

local function fracture(object, index, finale)
    if not isElement(object) then return end
    local x, y, z = getElementPosition(object)
    local side = (index % 2 == 0) and -1 or 1
    local effect = createObjectBreakEffect(object, {
        fragments = finale and math.min(42, 20 + index * 2) or (8 + (index % 5) * 4),
        force = finale and 7.5 or (3.5 + (index % 4) * 0.9),
        randomness = finale and 1.8 or 1.0,
        velocity = {0, 0, finale and 2.3 or 1.2},
        impactPosition = {x - side * 2.5, y - 1.5, z + 0.4},
        lifetime = finale and 9000 or 7500,
        bounce = 0.28 + (index % 3) * 0.08,
        drag = 0.10,
        renderDistance = 300,
        seed = 18000 + index * 311 + (finale and 5000 or 0),
    })
    if isElement(effect) then show.effects[#show.effects + 1] = effect end
end

local function updateShow()
    if not show.active then return end
    local elapsed = getTickCount() - show.startedAt
    local cx, cy, cz = show.cx, show.cy, show.cz

    if elapsed >= 2200 and not show.waveStarted then
        show.waveStarted = true
        for i, object in ipairs(show.wave) do
            setTimer(function()
                if show.active and isElement(object) then fracture(object, i, false) end
            end, (i - 1) * 360, 1)
        end
    end

    if elapsed >= 9300 and not show.finaleStarted then
        show.finaleStarted = true
        for i, object in ipairs(show.finale) do
            setTimer(function()
                if show.active and isElement(object) then fracture(object, i, true) end
            end, (i - 1) * 170, 1)
        end
    end

    if elapsed < 6500 then
        cameraLerp(
            {cx - 19, cy - 14, cz + 5, cx, cy + 8, cz + 2, 74},
            {cx + 17, cy - 8, cz + 7, cx, cy + 10, cz + 2, 68},
            elapsed / 6500
        )
    elseif elapsed < 12000 then
        local t = (elapsed - 6500) / 5500
        cameraLerp(
            {cx + 17, cy - 8, cz + 7, cx, cy + 10, cz + 2, 68},
            {cx + 13, cy + 2, cz + 10, cx, cy + 18, cz + 4, 62},
            t
        )
    elseif elapsed < 17500 then
        local t = (elapsed - 12000) / 5500
        cameraLerp(
            {cx + 13, cy + 2, cz + 10, cx, cy + 18, cz + 4, 62},
            {cx - 13, cy + 3, cz + 8, cx, cy + 20, cz + 3, 66},
            t
        )
    else
        local t = clamp((elapsed - 17500) / 5000, 0, 1)
        cameraLerp(
            {cx - 13, cy + 3, cz + 8, cx, cy + 20, cz + 3, 66},
            {cx, cy - 25, cz + 13, cx, cy + 17, cz + 3, 78},
            t
        )
    end

    if elapsed > 22500 and not show.finished then
        show.finished = true
        triggerServerEvent("breakShowcase:finished", resourceRoot)
        stopShow()
    end
end

local function readBool(v)
    v = tostring(v):lower()
    if v == "1" or v == "true" or v == "yes" or v == "on" then return true end
    if v == "0" or v == "false" or v == "no" or v == "off" then return false end
    return nil
end

local optionReaders = {
    fragments = function(v) return math.floor(tonumber(v) or -1) end,
    force = tonumber,
    randomness = tonumber,
    lifetime = function(v) return math.floor(tonumber(v) or -1) end,
    gravity = tonumber,
    bounce = tonumber,
    drag = tonumber,
    renderDistance = tonumber,
    seed = function(v) return math.floor(tonumber(v) or -1) end,
    vx = tonumber,
    vy = tonumber,
    vz = tonumber,
    scale = tonumber,
    health = tonumber,
    damageMultiplier = tonumber,
    instantBreakThreshold = tonumber,
    native = readBool,
    hideOriginal = readBool,
    disableOriginalCollision = readBool,
}

local function parsePlaygroundOptions(args)
    local values = {
        fragments = 20,
        force = 5.0,
        randomness = 1.5,
        lifetime = 8000,
        gravity = 9.81,
        bounce = 0.35,
        drag = 0.12,
        renderDistance = 350,
        seed = 4242,
        vx = 0,
        vy = 0,
        vz = 1.0,
        scale = 1.0,
        health = 250,
        native = true,
        damageMultiplier = 1.0,
        instantBreakThreshold = 150,
        hideOriginal = true,
        disableOriginalCollision = true,
    }

    for _, arg in ipairs(args) do
        local key, raw = arg:match("^([%w_]+)=(.+)$")
        local reader = key and optionReaders[key]
        if reader then
            local value = reader(raw)
            if value ~= nil then values[key] = value end
        end
    end

    return values
end

local function fractureOptions(p, impact)
    local options = {
        fragments = p.fragments,
        force = p.force,
        randomness = p.randomness,
        lifetime = p.lifetime,
        gravity = p.gravity,
        bounce = p.bounce,
        drag = p.drag,
        renderDistance = p.renderDistance,
        seed = p.seed,
        velocity = {p.vx, p.vy, p.vz},
        hideOriginal = p.hideOriginal,
        disableOriginalCollision = p.disableOriginalCollision,
    }
    if impact then options.impactPosition = impact end
    return options
end

local function playgroundHelp()
    outputChatBox("[BREAKSHOW] /breakspawn <model> [key=value ...]", 255, 200, 80)
    outputChatBox("[BREAKSHOW] durability: health native damageMultiplier instantBreakThreshold", 255, 200, 80)
    outputChatBox("[BREAKSHOW] fracture: fragments force randomness lifetime gravity bounce drag renderDistance seed scale vx vy vz", 255, 200, 80)
    outputChatBox("[BREAKSHOW] shoot/ram the object, /breakhp for health, /breaknow to force, /breakclear", 255, 200, 80)
    outputChatBox("[BREAKSHOW] example: /breakspawn 1337 health=250 fragments=24 force=7", 255, 200, 80)
end

addCommandHandler("breakspawn", function(_, modelArg, ...)
    local model = tonumber(modelArg)
    if not model then
        playgroundHelp()
        return
    end

    clearPlayground()

    local options = parsePlaygroundOptions({...})
    local px, py, pz = getElementPosition(localPlayer)
    local _, _, rz = getElementRotation(localPlayer)
    local angle = math.rad(rz)
    local distance = 4.0
    local x = px - math.sin(angle) * distance
    local y = py + math.cos(angle) * distance
    local z = pz + 0.35

    local object = createObject(model, x, y, z, 0, 0, rz)
    if not isElement(object) then
        outputChatBox("[BREAKSHOW] failed to create model " .. tostring(model), 255, 80, 80)
        return
    end

    setElementDimension(object, getElementDimension(localPlayer))
    setElementInterior(object, getElementInterior(localPlayer))
    setElementFrozen(object, true)
    setObjectScale(object, options.scale)

    local armed = setObjectBreakProfile(object, {
        native = options.native,
        health = options.health,
        damageMultiplier = options.damageMultiplier,
        instantBreakThreshold = options.instantBreakThreshold,
        fracture = fractureOptions(options),
    })
    if not armed then
        destroyElement(object)
        outputChatBox("[BREAKSHOW] failed to arm managed break profile.", 255, 80, 80)
        return
    end

    playground.object = object
    playground.options = options

    outputChatBox(("[BREAKSHOW] model %d armed: %.1f HP, native=%s. Shoot or ram it.")
        :format(model, options.health, tostring(options.native)), 100, 255, 100)
end)

addCommandHandler("breakhp", function()
    if not isElement(playground.object) then
        outputChatBox("[BREAKSHOW] no playground object.", 255, 80, 80)
        return
    end
    local health = getObjectBreakHealth(playground.object)
    outputChatBox(("[BREAKSHOW] managed health: %s"):format(tostring(health)), 180, 220, 255)
end)

addCommandHandler("breaknow", function()
    local object = playground.object
    local p = playground.options
    if not isElement(object) or not p then
        outputChatBox("[BREAKSHOW] no playground object. Use /breakspawn first.", 255, 80, 80)
        return
    end

    clearObjectBreakProfile(object)
    if isElement(playground.effect) then destroyElement(playground.effect) end

    local px, py, pz = getElementPosition(localPlayer)
    local effect = createObjectBreakEffect(object, fractureOptions(p, {px, py, pz + 0.7}))

    if not isElement(effect) then
        outputChatBox("[BREAKSHOW] fracture failed (object may not be streamed / valid static geometry).", 255, 80, 80)
        return
    end

    playground.effect = effect
    outputChatBox(("[BREAKSHOW] fractured: %d fragments, %d source triangles, cacheHit=%s")
        :format(getBreakEffectFragmentCount(effect), getBreakEffectSourceTriangleCount(effect), tostring(getBreakEffectCacheHit(effect))), 100, 255, 100)
end)

addCommandHandler("breakclear", function()
    clearPlayground()
    outputChatBox("[BREAKSHOW] playground cleared.", 100, 255, 100)
end)

addEvent("breakShowcase:start", true)
addEventHandler("breakShowcase:start", resourceRoot, function(cx, cy, cz, dimension)
    stopShow()
    show.active = true
    show.startedAt = getTickCount()
    show.cx, show.cy, show.cz = cx, cy, cz
    show.dimension = dimension
    show.finished = false
    show.waveStarted = false
    show.finaleStarted = false
    presentation(false)
    setTime(18, 20)
    buildScene()
end)

addEvent("breakShowcase:stop", true)
addEventHandler("breakShowcase:stop", resourceRoot, stopShow)
addEventHandler("onClientRender", root, updateShow)
addEventHandler("onClientResourceStop", resourceRoot, function()
    stopShow()
    clearPlayground()
end)