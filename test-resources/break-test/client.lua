local tracked = {}
local serial = 0

local function log(kind, name, detail)
    local suffix = detail and (": " .. tostring(detail)) or ""
    local message = ("[BREAKTEST] %s %s%s"):format(kind, name, suffix)
    outputDebugString(message, kind == "FAIL" and 1 or 3)
    outputChatBox(message, kind == "FAIL" and 255 or 100, kind == "FAIL" and 80 or 255, 100)
end

local function check(name, condition, detail)
    log(condition and "PASS" or "FAIL", name, detail)
    return condition
end

local function track(element)
    if isElement(element) then tracked[element] = true end
    return element
end

local function reset()
    serial = serial + 1
    for element in pairs(tracked) do
        if isElement(element) then
            if getElementType(element) == "object" then clearObjectBreakProfile(element) end
            destroyElement(element)
        end
    end
    tracked = {}
    clearBreakEffectCache()
end

local function forwardPosition(distance, side, height)
    local x, y, z = getElementPosition(localPlayer)
    local _, _, rz = getElementRotation(localPlayer)
    local a = math.rad(rz)
    local fx, fy = -math.sin(a), math.cos(a)
    local rx, ry = fy, -fx
    return x + fx * distance + rx * (side or 0), y + fy * distance + ry * (side or 0), z + (height or 0)
end

local function spawnObject(model, distance, side, zOffset, scale)
    local x, y, z = forwardPosition(distance, side, zOffset)
    local object = track(createObject(model, x, y, z))
    if isElement(object) then
        setElementFrozen(object, true)
        if scale then setObjectScale(object, scale) end
    end
    return object
end

local function breakLater(runSerial, object, options, callback)
    setTimer(function()
        if runSerial ~= serial or not isElement(object) then return end
        local effect = track(createObjectBreakEffect(object, options or {}))
        if callback then callback(effect) end
    end, 650, 1)
end

local function basic(runSerial)
    local object = spawnObject(1337, 8, 0, 0.8, 1.2)
    check("basic.object", isElement(object), tostring(object))
    if not isElement(object) then return end

    breakLater(runSerial, object, {fragments = 12, force = 4.5, lifetime = 6000, seed = 101}, function(effect)
        check("basic.create", isElement(effect), tostring(effect))
        if not isElement(effect) then return end
        check("basic.type", getElementType(effect) == "break-effect", getElementType(effect))
        local fragments = getBreakEffectFragmentCount(effect)
        local triangles = getBreakEffectSourceTriangleCount(effect)
        check("basic.fragments", type(fragments) == "number" and fragments >= 1 and fragments <= 12, fragments)
        check("basic.triangles", type(triangles) == "number" and triangles >= fragments, triangles)
        check("basic.pause-set", setBreakEffectPaused(effect, true) and isBreakEffectPaused(effect))
        check("basic.pause-clear", setBreakEffectPaused(effect, false) and not isBreakEffectPaused(effect))
    end)
end

local function cache(runSerial)
    clearBreakEffectCache()
    local first = spawnObject(1337, 10, -2.5, 0.8, 1.0)
    local second = spawnObject(1337, 10, 2.5, 0.8, 1.0)
    check("cache.objects", isElement(first) and isElement(second))
    if not isElement(first) or not isElement(second) then return end

    setTimer(function()
        if runSerial ~= serial then return end
        local options = {fragments = 10, force = 2.5, lifetime = 5000, seed = 4242, hideOriginal = false, disableOriginalCollision = false}
        local effect1 = track(createObjectBreakEffect(first, options))
        check("cache.first", isElement(effect1) and not getBreakEffectCacheHit(effect1), isElement(effect1) and tostring(getBreakEffectCacheHit(effect1)) or "false")
        local before = getBreakEffectCacheSize()
        local effect2 = track(createObjectBreakEffect(second, options))
        check("cache.second", isElement(effect2), tostring(effect2))
        check("cache.hit", isElement(effect2) and getBreakEffectCacheHit(effect2) == true, isElement(effect2) and tostring(getBreakEffectCacheHit(effect2)) or "false")
        check("cache.reuse", getBreakEffectCacheSize() == before, ("before=%s after=%s"):format(tostring(before), tostring(getBreakEffectCacheSize())))
    end, 750, 1)
end

local function profile(runSerial)
    local object = spawnObject(1337, 9, 0, 0.8, 1.0)
    if not check("profile.object", isElement(object), tostring(object)) then return end

    setTimer(function()
        if runSerial ~= serial or not isElement(object) then return end
        local ok = setObjectBreakProfile(object, {
            native = false,
            health = 200,
            fracture = {
                fragments = 10,
                force = 4.0,
                randomness = 1.0,
                lifetime = 5000,
                seed = 777,
            },
        })
        check("profile.set", ok)
        check("profile.health-initial", getObjectBreakHealth(object) == 200, getObjectBreakHealth(object))

        local info = getObjectBreakProfile(object)
        check("profile.get", type(info) == "table" and info.maxHealth == 200 and info.native == false,
            type(info) == "table" and ("health=%s max=%s native=%s"):format(tostring(info.health), tostring(info.maxHealth), tostring(info.native)) or tostring(info))

        check("profile.health-set", setObjectBreakHealth(object, 80) and getObjectBreakHealth(object) == 80, getObjectBreakHealth(object))
        check("profile.health-reset", resetObjectBreakHealth(object) and getObjectBreakHealth(object) == 200, getObjectBreakHealth(object))

        local before = #getElementsByType("break-effect")
        check("profile.zero", setObjectBreakHealth(object, 0))
        setTimer(function()
            if runSerial ~= serial then return end
            local after = #getElementsByType("break-effect")
            check("profile.fracture", after > before, ("before=%d after=%d"):format(before, after))
            check("profile.cleared-after-break", getObjectBreakProfile(object) == false)
        end, 100, 1)
    end, 700, 1)
end

local function invalid(runSerial)
    local object = spawnObject(1337, 7, 3.0, 0.8, 1.0)
    if not check("invalid.object", isElement(object)) then return end
    setTimer(function()
        if runSerial ~= serial or not isElement(object) then return end
        check("invalid.fragments", createObjectBreakEffect(object, {fragments = 65}) == false)
        check("invalid.force", createObjectBreakEffect(object, {force = -1}) == false)
        check("invalid.distance", createObjectBreakEffect(object, {renderDistance = 0}) == false)
        check("invalid.profile-health", setObjectBreakProfile(object, {health = -1}) == false)
        check("invalid.profile-multiplier", setObjectBreakProfile(object, {damageMultiplier = -1}) == false)
        check("invalid.profile-threshold", setObjectBreakProfile(object, {instantBreakThreshold = -1}) == false)
    end, 650, 1)
end

local function stress(runSerial, count)
    count = math.max(1, math.min(tonumber(count) or 8, 16))
    local objects = {}
    for i = 1, count do
        local side = (i - (count + 1) * 0.5) * 2.0
        local object = spawnObject((i % 2 == 0) and 1225 or 1337, 15 + (i % 3) * 2, side, 0.8, 0.85 + (i % 3) * 0.1)
        if isElement(object) then objects[#objects + 1] = object end
    end
    check("stress.objects", #objects == count, ("created=%d requested=%d"):format(#objects, count))

    setTimer(function()
        if runSerial ~= serial then return end
        local effects, fragments = 0, 0
        for i, object in ipairs(objects) do
            if isElement(object) then
                local effect = track(createObjectBreakEffect(object, {
                    fragments = 20,
                    force = 3.5 + (i % 4),
                    randomness = 1.0,
                    lifetime = 4500,
                    seed = 9000 + i,
                }))
                if isElement(effect) then
                    effects = effects + 1
                    fragments = fragments + (getBreakEffectFragmentCount(effect) or 0)
                end
            end
        end
        check("stress.effects", effects == #objects, ("effects=%d objects=%d"):format(effects, #objects))
        check("stress.fragments", fragments > effects and fragments <= 512, fragments)
    end, 800, 1)
end

local function runAll()
    reset()
    local runSerial = serial
    outputChatBox(("[BREAKTEST] RUN serial=%d"):format(runSerial), 255, 200, 80)
    basic(runSerial)
    setTimer(function() if runSerial == serial then cache(runSerial) end end, 1500, 1)
    setTimer(function() if runSerial == serial then profile(runSerial) end end, 3000, 1)
    setTimer(function() if runSerial == serial then invalid(runSerial) end end, 4500, 1)
    setTimer(function() if runSerial == serial then stress(runSerial, 6) end end, 5800, 1)
    setTimer(function()
        if runSerial == serial then
            outputChatBox(("[BREAKTEST] DONE serial=%d -- inspect PASS/FAIL above"):format(runSerial), 255, 200, 80)
        end
    end, 8000, 1)
end

local function parseBool(value)
    value = tostring(value):lower()
    if value == "true" or value == "1" or value == "yes" or value == "on" then return true end
    if value == "false" or value == "0" or value == "no" or value == "off" then return false end
    return nil
end

local function printBreakSpawnHelp()
    outputChatBox("[BREAKSPAWN] /breakspawn <model> [key=value ...]", 255, 200, 80)
    outputChatBox("[BREAKSPAWN] keys: fragments force randomness lifetime gravity bounce drag renderDistance seed", 255, 200, 80)
    outputChatBox("[BREAKSPAWN] spawn: distance side z scale | velocity: vx vy vz | hideOriginal collision", 255, 200, 80)
    outputChatBox("[BREAKSPAWN] example: /breakspawn 1337 fragments=24 force=8 randomness=2 bounce=0.55 lifetime=12000", 255, 200, 80)
end

addCommandHandler("breakspawn", function(_, modelArg, ...)
    local model = tonumber(modelArg)
    if not model then
        printBreakSpawnHelp()
        return
    end

    local spawn = {distance = 8, side = 0, z = 0.8, scale = 1}
    local options = {
        force = 5,
        randomness = 1.5,
        lifetime = 8000,
        gravity = 9.81,
        bounce = 0.35,
        drag = 0.12,
        renderDistance = 350,
        hideOriginal = true,
        disableOriginalCollision = true,
    }
    local velocity = {x = 0, y = 0, z = 1}

    local numericOptionKeys = {
        fragments = true, force = true, randomness = true, lifetime = true,
        gravity = true, bounce = true, drag = true, renderDistance = true, seed = true,
    }

    for _, token in ipairs({...}) do
        local key, raw = tostring(token):match("^([^=]+)=(.+)$")
        if key and raw then
            if numericOptionKeys[key] then
                local value = tonumber(raw)
                if value ~= nil then options[key] = value end
            elseif key == "distance" or key == "side" or key == "z" or key == "scale" then
                local value = tonumber(raw)
                if value ~= nil then spawn[key] = value end
            elseif key == "vx" or key == "vy" or key == "vz" then
                local value = tonumber(raw)
                if value ~= nil then velocity[key:sub(2)] = value end
            elseif key == "hideOriginal" then
                local value = parseBool(raw)
                if value ~= nil then options.hideOriginal = value end
            elseif key == "collision" then
                local value = parseBool(raw)
                if value ~= nil then options.disableOriginalCollision = not value end
            elseif key == "disableOriginalCollision" then
                local value = parseBool(raw)
                if value ~= nil then options.disableOriginalCollision = value end
            else
                outputChatBox(("[BREAKSPAWN] unknown key: %s"):format(key), 255, 120, 80)
            end
        end
    end

    options.velocity = velocity

    local object = spawnObject(model, spawn.distance, spawn.side, spawn.z, spawn.scale)
    if not isElement(object) then
        outputChatBox(("[BREAKSPAWN] failed to create model %s"):format(tostring(model)), 255, 80, 80)
        return
    end

    local runSerial = serial
    outputChatBox(("[BREAKSPAWN] model=%d spawned; fracturing in 650ms"):format(model), 100, 255, 100)
    breakLater(runSerial, object, options, function(effect)
        if not isElement(effect) then
            outputChatBox("[BREAKSPAWN] fracture failed (model may not be streamed/static RenderWare geometry)", 255, 80, 80)
            return
        end
        outputChatBox(("[BREAKSPAWN] effect=%s fragments=%s triangles=%s cacheHit=%s"):format(
            tostring(effect), tostring(getBreakEffectFragmentCount(effect)), tostring(getBreakEffectSourceTriangleCount(effect)),
            tostring(getBreakEffectCacheHit(effect))), 100, 255, 100)
    end)
end)

addCommandHandler("breakspawnhelp", printBreakSpawnHelp)

addCommandHandler("breaktest", function(_, caseName, value)
    caseName = (caseName or "all"):lower()
    if caseName == "all" then
        runAll()
    elseif caseName == "basic" then
        reset(); basic(serial)
    elseif caseName == "cache" then
        reset(); cache(serial)
    elseif caseName == "profile" then
        reset(); profile(serial)
    elseif caseName == "invalid" then
        reset(); invalid(serial)
    elseif caseName == "stress" then
        reset(); stress(serial, value)
    elseif caseName == "status" then
        outputChatBox(("[BREAKTEST] effects=%d cache=%s"):format(#getElementsByType("break-effect"), tostring(getBreakEffectCacheSize())), 255, 200, 80)
    elseif caseName == "reset" then
        reset(); log("PASS", "reset")
    else
        outputChatBox("[BREAKTEST] cases: all, basic, cache, profile, invalid, stress [count], status, reset", 255, 200, 80)
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, reset)