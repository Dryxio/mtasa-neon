local RUNWAY_START = {x = 100.74450, y = 2501.11401, z = 16.48438, rz = 272.30032}
local RUNWAY_END = {x = 424.00473, y = 2503.03442, z = 16.48438, rz = 267.65860}
local SHOW_START_HOLD_MS = 1500
local SHOW_TRAVEL_MS = 36000
local SHOW_END_HOLD_MS = 3500

local runwayDX = RUNWAY_END.x - RUNWAY_START.x
local runwayDY = RUNWAY_END.y - RUNWAY_START.y
local runwayLength = math.sqrt(runwayDX * runwayDX + runwayDY * runwayDY)
local runwayDirX, runwayDirY = runwayDX / runwayLength, runwayDY / runwayLength
local runwayRightX, runwayRightY = -runwayDirY, runwayDirX

local show = {
    active = false,
    calibrating = false,
    startedAt = 0,
    dimension = 0,
    objects = {},
    effects = {},
    entries = {},
    calibrationOffsets = {},
    finished = false,
}

local playground = {
    object = nil,
    effect = nil,
    options = nil,
    armed = false,
    objects = {},
    effects = {},
}

local function clamp(v, a, b) return math.max(a, math.min(b, v)) end
local function lerp(a, b, t) return a + (b - a) * t end

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
    show.objects, show.effects, show.entries = {}, {}, {}
    show.active = false
    show.calibrating = false
    show.finished = false
    setCameraTarget(localPlayer)
    presentation(true)
end

local function clearPlayground()
    for _, object in ipairs(playground.objects) do
        if isElement(object) then clearObjectBreakProfile(object) end
    end
    destroyList(playground.effects)
    destroyList(playground.objects)

    playground.object = nil
    playground.effect = nil
    playground.options = nil
    playground.armed = false
    playground.objects = {}
    playground.effects = {}
end

local function raisePlaygroundObject()
    if show.active or show.calibrating or not isElement(playground.object) or isElement(playground.effect) then return end
    if isChatBoxInputActive and isChatBoxInputActive() then return end
    if isConsoleActive and isConsoleActive() then return end

    local x, y, z = getElementPosition(playground.object)
    local step = 0.10
    setElementPosition(playground.object, x, y, z + step)

    if playground.options then
        playground.options.groundOffset = (playground.options.groundOffset or 0) + step
    end

    outputChatBox(("[BREAKSHOW] object raised +%.2fm (groundOffset=%.2f)")
        :format(step, playground.options and playground.options.groundOffset or step), 180, 220, 255)
end

local function getBreakspawnOwners()
    local owners = {}
    for _, entry in ipairs(getCommandHandlers()) do
        if type(entry) == "table" and entry[1] == "breakspawn" and entry[2] then
            local name = getResourceName(entry[2])
            if name then owners[#owners + 1] = name end
        end
    end
    return owners
end

local function warnDuplicateBreakspawnHandlers()
    local owners = getBreakspawnOwners()
    if #owners <= 1 then return end

    outputChatBox(("[BREAKSHOW] WARNING: %d /breakspawn handlers are loaded: %s")
        :format(#owners, table.concat(owners, ", ")), 255, 120, 80)
    outputChatBox("[BREAKSHOW] Restart/resync the stale resource; MTA executes every matching command handler.", 255, 120, 80)
end

local function getGroundedObjectZ(object, x, y, referenceZ, scale, groundOffset, fallbackGroundZ)
    local groundZ = getGroundPosition(x, y, referenceZ + 1000.0)
    if type(groundZ) ~= "number" then
        groundZ = getGroundPosition(x, y, referenceZ + 100.0)
    end
    if type(groundZ) ~= "number" and type(fallbackGroundZ) == "number" then
        groundZ = fallbackGroundZ
    end
    if type(groundZ) ~= "number" then return nil end

    local baseDistance = getElementDistanceFromCentreOfMassToBaseOfModel(object)
    if type(baseDistance) == "number" then
        return groundZ + baseDistance * scale + groundOffset, groundZ
    end

    local _, _, minZ = getElementBoundingBox(object)
    if type(minZ) == "number" then
        return groundZ - minZ * scale + groundOffset, groundZ
    end

    return groundZ + groundOffset, groundZ
end

local function runwayPoint(t, side)
    return RUNWAY_START.x + runwayDX * t + runwayRightX * (side or 0),
           RUNWAY_START.y + runwayDY * t + runwayRightY * (side or 0),
           lerp(RUNWAY_START.z, RUNWAY_END.z, t)
end

-- Focused geometry showcase requested for validating managed fracture on a
-- small set of representative GTA:SA objects. The name field is documentation
-- only; model is the actual createObject ID.
local runwaySpecs = {
    {name = "pool_table",       model = 2964,  t = 0.12, side = -2.5, scale = 1.00, fragments = 28, force = 4.5, randomness = 0.75, vz = 0.45, bounce = 0.16},
    {name = "paypark",          model = 4639,  t = 0.30, side =  2.5, scale = 1.00, fragments = 32, force = 5.0, randomness = 0.85, vz = 0.55, bounce = 0.16},
    {name = "plumber_tower",    model = 16327, t = 0.48, side = -4.0, scale = 1.00, fragments = 56, force = 6.0, randomness = 1.00, vz = 0.75, bounce = 0.18},
    {name = "sm_airstrm_med_",  model = 3175,  t = 0.66, side =  4.0, scale = 1.00, fragments = 48, force = 5.8, randomness = 0.95, vz = 0.70, bounce = 0.18},
    {name = "billboard",        model = 9190,  t = 0.84, side =  0.0, scale = 1.00, fragments = 56, force = 6.2, randomness = 1.05, vz = 0.80, bounce = 0.18},
}

local function spawnRunwayEntry(spec, index)
    local x, y, referenceZ = runwayPoint(spec.t, spec.side)
    local rz = spec.rz or ((lerp(RUNWAY_START.rz, RUNWAY_END.rz, spec.t) + index * 31) % 360)
    local scale = spec.scale or 1.0

    local object = createObject(spec.model, x, y, referenceZ + 50.0, 0, 0, rz)
    if not isElement(object) then return end

    setElementDimension(object, show.dimension)
    setElementInterior(object, 0)
    setElementFrozen(object, true)
    setObjectScale(object, scale)

    local calibrationOffset = show.calibrationOffsets[index] or 0
    local groundOffset = (spec.groundOffset or 0.02) + calibrationOffset
    local objectZ = getGroundedObjectZ(object, x, y, referenceZ, scale, groundOffset, referenceZ)
    if not objectZ then
        destroyElement(object)
        return
    end

    setElementPosition(object, x, y, objectZ)
    show.objects[#show.objects + 1] = object
    show.entries[#show.entries + 1] = {
        object = object,
        spec = spec,
        index = index,
        x = x,
        y = y,
        z = objectZ,
        broken = false,
    }
end

local function buildRunway()
    show.objects, show.effects, show.entries = {}, {}, {}
    for index, spec in ipairs(runwaySpecs) do
        spawnRunwayEntry(spec, index)
    end
end

local function findNearestRunwayEntry(maxDistance)
    local px, py, pz = getElementPosition(localPlayer)
    local nearest, nearestDistanceSq
    local maxDistanceSq = maxDistance * maxDistance

    for _, entry in ipairs(show.entries) do
        if isElement(entry.object) then
            local x, y, z = getElementPosition(entry.object)
            local dx, dy, dz = x - px, y - py, z - pz
            local distanceSq = dx * dx + dy * dy + dz * dz
            if distanceSq <= maxDistanceSq and (not nearestDistanceSq or distanceSq < nearestDistanceSq) then
                nearest = entry
                nearestDistanceSq = distanceSq
            end
        end
    end

    return nearest
end

local function adjustNearestRunwayObject(step)
    local entry = findNearestRunwayEntry(7.0)
    if not entry then
        outputChatBox("[BREAKSHOW] No runway object within 7m.", 255, 180, 80)
        return
    end

    local offset = (show.calibrationOffsets[entry.index] or 0) + step
    show.calibrationOffsets[entry.index] = offset

    local x, y, z = getElementPosition(entry.object)
    entry.z = z + step
    setElementPosition(entry.object, x, y, entry.z)

    outputChatBox(("[BREAKSHOW] #%d %s (%d) offset %+.2fm")
        :format(entry.index, entry.spec.name or "object", entry.spec.model, offset), 180, 220, 255)
end

local function handleRaiseKey()
    if isChatBoxInputActive and isChatBoxInputActive() then return end
    if isConsoleActive and isConsoleActive() then return end

    if show.calibrating then
        local step = (getKeyState("lshift") or getKeyState("rshift")) and -0.10 or 0.10
        adjustNearestRunwayObject(step)
        return
    end

    raisePlaygroundObject()
end

local function fractureRunwayEntry(entry)
    if entry.broken or not isElement(entry.object) then return end
    entry.broken = true

    local spec = entry.spec
    local impactSide = spec.side >= 0 and -1.5 or 1.5
    local effect = createObjectBreakEffect(entry.object, {
        fragments = spec.fragments or 24,
        force = spec.force or 4.0,
        randomness = spec.randomness or 0.8,
        velocity = {0, 0, spec.vz or 0.45},
        impactPosition = {
            entry.x - runwayDirX * 2.2 + runwayRightX * impactSide,
            entry.y - runwayDirY * 2.2 + runwayRightY * impactSide,
            entry.z + 0.55,
        },
        lifetime = spec.lifetime or 7600,
        gravity = 9.81,
        bounce = spec.bounce or 0.18,
        drag = spec.drag or 0.11,
        renderDistance = 360,
        seed = 51000 + entry.index * 997,
        hideOriginal = true,
        disableOriginalCollision = true,
    })

    if isElement(effect) then
        show.effects[#show.effects + 1] = effect
    end
end

local function updateRunwayShow()
    if not show.active then return end

    local elapsed = getTickCount() - show.startedAt
    local travelT = clamp((elapsed - SHOW_START_HOLD_MS) / SHOW_TRAVEL_MS, 0, 1)

    -- A mostly linear travelling shot keeps the destruction rhythm consistent.
    -- Small lateral/height motion keeps it from looking like a debug fly-through.
    local cameraSide = math.sin(travelT * math.pi * 2.4) * 1.15
    local cameraX, cameraY, cameraBaseZ = runwayPoint(travelT, cameraSide)
    local cameraZ = cameraBaseZ + 3.25 + math.sin(travelT * math.pi * 3.0) * 0.18

    local lookT = math.min(travelT + 0.058, 1.03)
    local lookSide = math.sin(lookT * math.pi * 2.4) * 0.35
    local lookX, lookY, lookBaseZ = runwayPoint(lookT, lookSide)
    local lookZ = lookBaseZ + 1.45
    setCameraMatrix(cameraX, cameraY, cameraZ, lookX, lookY, lookZ, 0, 72)

    for _, entry in ipairs(show.entries) do
        local lead = entry.spec.lead or 0.045
        if not entry.broken and travelT >= entry.spec.t - lead then
            fractureRunwayEntry(entry)
        end
    end

    local totalDuration = SHOW_START_HOLD_MS + SHOW_TRAVEL_MS + SHOW_END_HOLD_MS
    if elapsed >= totalDuration and not show.finished then
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
    distance = tonumber,
    groundOffset = tonumber,
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
        distance = 4.0,
        groundOffset = 0.02,
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
    outputChatBox("[BREAKSHOW] fracture: fragments force randomness lifetime gravity bounce drag renderDistance seed vx vy vz", 255, 200, 80)
    outputChatBox("[BREAKSHOW] placement: distance scale groundOffset (auto-grounded by default); R raises latest object +0.10m", 255, 200, 80)
    outputChatBox("[BREAKSHOW] shoot/ram the objects; /breakhp and /breaknow target the latest spawn; /breakclear clears all", 255, 200, 80)
    outputChatBox("[BREAKSHOW] example: /breakspawn 1337 health=250 fragments=24 force=7", 255, 200, 80)
end

local function spawnPlaygroundObject(model, options)
    local px, py, pz = getElementPosition(localPlayer)
    local _, _, rz = getElementRotation(localPlayer)
    local angle = math.rad(rz)
    local x = px - math.sin(angle) * options.distance
    local y = py + math.cos(angle) * options.distance

    local object = createObject(model, x, y, pz + 50.0, 0, 0, rz)
    if not isElement(object) then return nil, "create" end

    setElementDimension(object, getElementDimension(localPlayer))
    setElementInterior(object, getElementInterior(localPlayer))
    setElementFrozen(object, true)
    setObjectScale(object, options.scale)

    local objectZ = getGroundedObjectZ(object, x, y, pz, options.scale, options.groundOffset)
    if not objectZ then
        destroyElement(object)
        return nil, "ground"
    end

    setElementPosition(object, x, y, objectZ)
    return object
end

local function handleBreakspawn(_, modelArg, ...)
    local model = tonumber(modelArg)
    if not model then
        playgroundHelp()
        return
    end

    local options = parsePlaygroundOptions({...})
    local object, errorReason = spawnPlaygroundObject(model, options)
    if not isElement(object) then
        if errorReason == "ground" then
            outputChatBox("[BREAKSHOW] failed to find ground at the spawn point. Move somewhere with loaded ground and retry.", 255, 80, 80)
        else
            outputChatBox("[BREAKSHOW] failed to create model " .. tostring(model), 255, 80, 80)
        end
        return
    end

    playground.objects[#playground.objects + 1] = object
    playground.object = object
    playground.effect = nil
    playground.options = options
    playground.armed = false

    setTimer(function()
        if not isElement(object) then return end

        local armed = setObjectBreakProfile(object, {
            native = options.native,
            health = options.health,
            damageMultiplier = options.damageMultiplier,
            instantBreakThreshold = options.instantBreakThreshold,
            fracture = fractureOptions(options),
        })
        if not armed then
            destroyElement(object)
            if playground.object == object then
                playground.object = nil
                playground.effect = nil
                playground.options = nil
                playground.armed = false
            end
            outputChatBox("[BREAKSHOW] failed to arm managed break profile.", 255, 80, 80)
            return
        end

        if playground.object == object then playground.armed = true end
        outputChatBox(("Object %d spawned and made breakable."):format(model), 100, 255, 100)
    end, 500, 1)
end

addCommandHandler("breakspawn", handleBreakspawn)
addCommandHandler("breakobject", handleBreakspawn)
bindKey("r", "down", handleRaiseKey)

addCommandHandler("breakoffsets", function()
    local count = 0
    for index, spec in ipairs(runwaySpecs) do
        local offset = show.calibrationOffsets[index]
        if offset and math.abs(offset) > 0.001 then
            count = count + 1
            outputChatBox(("[BREAKSHOW] #%d %s (%d): %+.2fm")
                :format(index, spec.name or "object", spec.model, offset), 180, 220, 255)
        end
    end
    if count == 0 then
        outputChatBox("[BREAKSHOW] no runway height adjustments yet.", 180, 220, 255)
    end
end)

addCommandHandler("breakhp", function()
    if not isElement(playground.object) then
        outputChatBox("[BREAKSHOW] no playground object.", 255, 80, 80)
        return
    end
    if not playground.armed then
        outputChatBox("[BREAKSHOW] object is still settling / not armed yet.", 255, 200, 80)
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
    playground.armed = false
    if isElement(playground.effect) then destroyElement(playground.effect) end

    local px, py, pz = getElementPosition(localPlayer)
    local effect = createObjectBreakEffect(object, fractureOptions(p, {px, py, pz + 0.7}))

    if not isElement(effect) then
        outputChatBox("[BREAKSHOW] fracture failed (object may not be streamed / valid static geometry).", 255, 80, 80)
        return
    end

    playground.effect = effect
    playground.effects[#playground.effects + 1] = effect
    outputChatBox(("[BREAKSHOW] fractured: %d fragments, %d source triangles, cacheHit=%s")
        :format(getBreakEffectFragmentCount(effect), getBreakEffectSourceTriangleCount(effect), tostring(getBreakEffectCacheHit(effect))), 100, 255, 100)
end)

addCommandHandler("breakclear", function()
    clearPlayground()
    outputChatBox("[BREAKSHOW] playground cleared.", 100, 255, 100)
end)

addEvent("breakShowcase:calibrate", true)
addEventHandler("breakShowcase:calibrate", resourceRoot, function(dimension)
    stopShow()
    clearPlayground()
    show.dimension = dimension
    show.calibrating = true
    presentation(true)
    setTime(17, 30)
    setWeather(0)
    buildRunway()
    outputChatBox("[BREAKSHOW] Calibration: walk near an object and press R to raise it +0.10m.", 100, 255, 100)
    outputChatBox("[BREAKSHOW] Hold Shift + R to lower it -0.10m. /breakshow starts the demo with these offsets.", 100, 255, 100)
end)

addEvent("breakShowcase:start", true)
addEventHandler("breakShowcase:start", resourceRoot, function(dimension)
    stopShow()
    clearPlayground()
    show.active = true
    show.startedAt = getTickCount()
    show.dimension = dimension
    show.finished = false
    presentation(false)
    setTime(17, 30)
    setWeather(0)
    buildRunway()
end)

addEvent("breakShowcase:stop", true)
addEventHandler("breakShowcase:stop", resourceRoot, stopShow)
addEventHandler("onClientRender", root, updateRunwayShow)
addEventHandler("onClientResourceStart", resourceRoot, warnDuplicateBreakspawnHandlers)
addEventHandler("onClientResourceStop", resourceRoot, function()
    stopShow()
    clearPlayground()
end)
