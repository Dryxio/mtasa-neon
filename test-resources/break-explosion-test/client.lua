local tracked = {}
local serial = 0

local ROCKET = 2
local WEAK_ROCKET = 3
local TANK = 10
local MODEL = 1337

local function log(kind, name, detail)
    local suffix = detail and (": " .. tostring(detail)) or ""
    local message = ("[BREAKEXP] %s %s%s"):format(kind, name, suffix)
    outputDebugString(message, kind == "FAIL" and 1 or 3)
    outputChatBox(message, kind == "FAIL" and 255 or 100, kind == "FAIL" and 80 or 255, 100)
end

local function check(name, condition, detail)
    log(condition and "PASS" or "FAIL", name, detail)
    return condition
end

local function destroyTracked()
    for element in pairs(tracked) do
        if isElement(element) then
            if getElementType(element) == "object" then
                clearObjectBreakProfile(element)
            end
            destroyElement(element)
        end
    end
    tracked = {}
end

local function reset()
    serial = serial + 1
    destroyTracked()
end

local function track(element)
    if isElement(element) then
        tracked[element] = true
    end
    return element
end

local function forwardPosition(distance, side, height)
    local x, y, z = getElementPosition(localPlayer)
    local _, _, rz = getElementRotation(localPlayer)
    local a = math.rad(rz)
    local fx, fy = -math.sin(a), math.cos(a)
    local rx, ry = fy, -fx
    return x + fx * distance + rx * (side or 0), y + fy * distance + ry * (side or 0), z + (height or 0)
end

local function spawnTarget(health, withProfile, model)
    local x, y, z = forwardPosition(9, 0, 0.8)
    local object = track(createObject(model or MODEL, x, y, z))
    if not isElement(object) then
        return false
    end

    setElementFrozen(object, true)
    if withProfile then
        local ok = setObjectBreakProfile(object, {
            native = false,
            health = health,
            fracture = {
                fragments = 16,
                force = 6.0,
                randomness = 1.0,
                lifetime = 5000,
                seed = 20260819,
            },
        })
        if not ok then
            destroyElement(object)
            tracked[object] = nil
            return false
        end
    end

    return object, x, y, z
end

local function approximate(value, expected, tolerance)
    return type(value) == "number" and math.abs(value - expected) <= (tolerance or 0.75)
end

local function runDamageCase(runSerial, name, explosionType, distance, damaging, expectedHealth, done)
    destroyTracked()
    local object, x, y, z = spawnTarget(1000, true)
    if not check(name .. ".spawn", isElement(object), tostring(object)) then
        done()
        return
    end

    setTimer(function()
        if runSerial ~= serial or not isElement(object) then return end
        check(name .. ".createExplosion", createExplosion(x + distance, y, z, explosionType, false, 0, damaging) == true)
    end, 650, 1)

    setTimer(function()
        if runSerial ~= serial or not isElement(object) then return end
        local health = getObjectBreakHealth(object)
        check(name .. ".health", approximate(health, expectedHealth), ("expected=%.1f actual=%s"):format(expectedHealth, tostring(health)))
        done()
    end, 800, 1)
end

local function runNoProfileCase(runSerial, done)
    destroyTracked()
    local object, x, y, z = spawnTarget(1000, false)
    if not check("no-profile.spawn", isElement(object), tostring(object)) then
        done()
        return
    end

    local before = #getElementsByType("break-effect")
    setTimer(function()
        if runSerial ~= serial or not isElement(object) then return end
        check("no-profile.createExplosion", createExplosion(x + 1, y, z, ROCKET, false, 0, true) == true)
    end, 650, 1)

    setTimer(function()
        if runSerial ~= serial then return end
        check("no-profile.profile", isElement(object) and getObjectBreakProfile(object) == false)
        check("no-profile.break-effect", #getElementsByType("break-effect") == before,
            ("before=%d after=%d"):format(before, #getElementsByType("break-effect")))
        done()
    end, 800, 1)
end

local function runFractureCase(runSerial, done)
    destroyTracked()
    local object, x, y, z = spawnTarget(200, true)
    if not check("fracture.spawn", isElement(object), tostring(object)) then
        done()
        return
    end

    local before = #getElementsByType("break-effect")
    setTimer(function()
        if runSerial ~= serial or not isElement(object) then return end
        check("fracture.createExplosion", createExplosion(x + 1, y, z, ROCKET, false, 0, true) == true)
    end, 650, 1)

    setTimer(function()
        if runSerial ~= serial then return end
        local after = #getElementsByType("break-effect")
        check("fracture.effect", after > before, ("before=%d after=%d"):format(before, after))
        check("fracture.profile-cleared", not isElement(object) or getObjectBreakProfile(object) == false)
        done()
    end, 850, 1)
end

local function runAll()
    reset()
    local runSerial = serial
    local cases = {
        function(done) runNoProfileCase(runSerial, done) end,
        -- GTA's object explosion branch uses a 300-point radial damage basis.
        -- At <= half radius the falloff is capped at 1, so one rocket should
        -- leave a native=false 1000-health managed profile at exactly 700.
        function(done) runDamageCase(runSerial, "rocket-near-no-double", ROCKET, 1, true, 700, done) end,
        function(done) runDamageCase(runSerial, "rocket-falloff", ROCKET, 8, true, 880, done) end,
        function(done) runDamageCase(runSerial, "rocket-outside", ROCKET, 11, true, 1000, done) end,
        function(done) runDamageCase(runSerial, "weak-rocket", WEAK_ROCKET, 1, true, 940, done) end,
        function(done) runDamageCase(runSerial, "tank", TANK, 1, true, 700, done) end,
        function(done) runDamageCase(runSerial, "no-damage", ROCKET, 1, false, 1000, done) end,
        function(done) runFractureCase(runSerial, done) end,
    }

    local index = 0
    local function nextCase()
        if runSerial ~= serial then return end
        index = index + 1
        if not cases[index] then
            outputChatBox("[BREAKEXP] DONE -- inspect PASS/FAIL above", 255, 200, 80)
            return
        end
        cases[index](function()
            setTimer(nextCase, 250, 1)
        end)
    end

    nextCase()
end

addCommandHandler("breakexptest", runAll)

addCommandHandler("breakexplosiontarget", function(_, modelArg)
    reset()
    local model = tonumber(modelArg) or MODEL
    local object = spawnTarget(250, true, model)
    if not isElement(object) then
        log("FAIL", "manual-target.spawn", model)
        return
    end

    outputChatBox(("[BREAKEXP] target model %d armed with 250 health. Hit it with a rocket or Rhino shell."):format(model), 255, 200, 80)
end)

addCommandHandler("breakexpreset", function()
    reset()
    log("PASS", "reset")
end)

addEventHandler("onClientResourceStop", resourceRoot, reset)