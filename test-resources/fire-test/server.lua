local tracked = {}
local runSerial = 0
local runPlayer = false

local function emit(message, debugLevel, r, g, b)
    outputDebugString(message, debugLevel or 3)
    if isElement(runPlayer) and getElementType(runPlayer) == "player" then
        outputChatBox(message, runPlayer, r or 255, g or 255, b or 255)
    end
end

local function log(kind, name, detail)
    local suffix = detail and (": " .. tostring(detail)) or ""
    local message = ("[FIRETEST] %s %s%s"):format(kind, name, suffix)
    if kind == "FAIL" then
        emit(message, 1, 255, 80, 80)
    else
        emit(message, 3, 100, 255, 100)
    end
end

local function pass(name, detail)
    log("PASS", name, detail)
end

local function fail(name, detail)
    log("FAIL", name, detail)
end

local function check(name, condition, detail)
    if condition then
        pass(name, detail)
    else
        fail(name, detail)
    end
    return condition
end

local function track(element)
    if isElement(element) then
        tracked[element] = true
    end
    return element
end

local function reset()
    for element in pairs(tracked) do
        if isElement(element) then
            destroyElement(element)
        end
    end
    tracked = {}
end

local function spawnPosition(player, offset)
    local x, y, z = getElementPosition(player)
    return x + (offset or 3), y, z
end

local function countManagedFires()
    local count = 0
    for _, element in ipairs(getElementsByType("fire")) do
        if isElement(element) then
            count = count + 1
        end
    end
    return count
end

local function runImmediate(player)
    local x, y, z = spawnPosition(player, 3)
    local fire = track(createFire(x, y, z, {
        duration = 5000,
        strength = 1.0,
        damage = false,
        spread = false,
        source = player,
    }))

    check("basic.create", isElement(fire), tostring(fire))
    if not isElement(fire) then
        return false
    end

    check("basic.type", getElementType(fire) == "fire", getElementType(fire))
    check("duration.get", math.abs((getFireDuration(fire) or -1) - 5000) < 1, getFireDuration(fire))
    check("remaining.initial", (getFireRemainingTime(fire) or -1) > 4500, getFireRemainingTime(fire))
    check("strength.get", math.abs((getFireStrength(fire) or -1) - 1.0) < 0.01, getFireStrength(fire))
    check("damage.disabled", isFireDamageEnabled(fire) == false, tostring(isFireDamageEnabled(fire)))
    check("source.get", getFireSource(fire) == player, tostring(getFireSource(fire)))

    check("strength.set", setFireStrength(fire, 2.5) and math.abs((getFireStrength(fire) or 0) - 2.5) < 0.01, getFireStrength(fire))
    check("duration.set", setFireDuration(fire, 8000) and math.abs((getFireDuration(fire) or 0) - 8000) < 1, getFireDuration(fire))
    check("remaining.set", setFireRemainingTime(fire, 4000) and (getFireRemainingTime(fire) or 0) > 3500, getFireRemainingTime(fire))

    local mask = {players = true, peds = false, vehicles = true, objects = false}
    setFireDamageTargets(fire, mask)
    local got = getFireDamageTargets(fire)
    check("damage.mask", got and got.players and not got.peds and got.vehicles and not got.objects,
        got and ("p=%s ped=%s v=%s o=%s"):format(tostring(got.players), tostring(got.peds), tostring(got.vehicles), tostring(got.objects)) or "false")

    check("spread.set", setFireSpreadEnabled(fire, true) and isFireSpreadEnabled(fire), tostring(isFireSpreadEnabled(fire)))
    check("generations.set", setFireMaxGenerations(fire, 2) and getFireMaxGenerations(fire) == 2, getFireMaxGenerations(fire))
    check("target.set", setFireTarget(fire, player) and getFireTarget(fire) == player, tostring(getFireTarget(fire)))
    check("target.clear", setFireTarget(fire, nil) and getFireTarget(fire) == false, tostring(getFireTarget(fire)))

    -- Keep this control fire stable after testing the live setters so the dedicated
    -- spread scenario owns all propagation assertions.
    setFireSpreadEnabled(fire, false)
    setFireMaxGenerations(fire, 0)

    triggerClientEvent(player, "firetest:inspect", resourceRoot, fire, runSerial)
    return true
end

local function runExpiry(player, serial)
    local x, y, z = spawnPosition(player, 5)
    local fire = track(createFire(x, y, z, {duration = 850, damage = false}))
    check("expiry.created", isElement(fire))
    setTimer(function()
        if serial ~= runSerial then return end
        check("expiry.destroyed", not isElement(fire), isElement(fire) and "still alive" or "expired")
        tracked[fire] = nil
    end, 1400, 1)
end

local function runSpread(player, serial)
    local before = countManagedFires()
    local x, y, z = spawnPosition(player, 7)
    local parent = track(createFire(x, y, z, {
        duration = 7000,
        strength = 1.5,
        damage = false,
        spread = true,
        maxGenerations = 1,
    }))
    check("spread.created", isElement(parent))

    setTimer(function()
        if serial ~= runSerial then return end
        local after = countManagedFires()
        check("spread.child", after >= before + 2, ("before=%d after=%d"):format(before, after))
    end, 2400, 1)
end

local function runBulk(player, serial)
    local x, y, z = spawnPosition(player, 100)
    local fires = {}
    for i = 1, 65 do
        local fire = createFire(x + (i % 10), y + math.floor(i / 10), z, {duration = 3500, damage = false})
        if isElement(fire) then
            setElementDimension(fire, 65000)
            fires[#fires + 1] = fire
            tracked[fire] = true
        end
    end
    check("bulk.over-native-pool", #fires == 65, ("created=%d"):format(#fires))

    setTimer(function()
        if serial ~= runSerial then return end
        local alive = 0
        for _, fire in ipairs(fires) do
            if isElement(fire) then alive = alive + 1 end
        end
        check("bulk.expiry", alive == 0, ("alive=%d"):format(alive))
        for _, fire in ipairs(fires) do tracked[fire] = nil end
    end, 4300, 1)
end

local function runAll(player)
    runSerial = runSerial + 1
    local serial = runSerial
    reset()
    emit(("[FIRETEST] RUN serial=%d player=%s"):format(serial, getPlayerName(player)), 3, 255, 200, 80)

    if not runImmediate(player) then
        fail("run", "basic creation failed; dependent tests skipped")
        return
    end
    runExpiry(player, serial)
    runSpread(player, serial)
    runBulk(player, serial)
    triggerClientEvent(player, "firetest:localPolicy", resourceRoot, serial)

    setTimer(function()
        if serial == runSerial then
            emit(("[FIRETEST] DONE serial=%d -- inspect PASS/FAIL lines above"):format(serial), 3, 255, 200, 80)
        end
    end, 5000, 1)
end

addEvent("firetest:clientResult", true)
addEventHandler("firetest:clientResult", resourceRoot, function(serial, name, ok, detail)
    if not client or serial ~= runSerial then
        return
    end
    check("client." .. tostring(name), ok == true, detail)
end)

addCommandHandler("firetest", function(player, _, caseName)
    if not isElement(player) or getElementType(player) ~= "player" then
        return
    end

    runPlayer = player
    caseName = caseName or "all"
    if caseName == "all" then
        runAll(player)
    elseif caseName == "reset" then
        runSerial = runSerial + 1
        reset()
        pass("reset")
    elseif caseName == "status" then
        outputChatBox(("[FIRETEST] tracked=%d worldFires=%d serial=%d"):format(
            (function() local n = 0 for element in pairs(tracked) do if isElement(element) then n = n + 1 end end return n end)(),
            countManagedFires(), runSerial), player, 255, 200, 80)
    elseif caseName == "basic" then
        runSerial = runSerial + 1
        reset()
        runImmediate(player)
    elseif caseName == "late" then
        runSerial = runSerial + 1
        reset()
        local x, y, z = spawnPosition(player, 6)
        local fire = track(createFire(x, y, z, {duration = 30000, strength = 1.5, damage = false}))
        if isElement(fire) then
            outputChatBox("[FIRETEST] late-join fire created for 30s; join/stream another client and run /firetest status", player, 255, 200, 80)
            pass("late.setup", ("remaining=%.0f"):format(getFireRemainingTime(fire) or -1))
        else
            fail("late.setup", "createFire returned false")
        end
    else
        outputChatBox("[FIRETEST] cases: all, basic, late, status, reset", player, 255, 200, 80)
    end
end)

addEventHandler("onPlayerQuit", root, function()
    if source == runPlayer then
        runPlayer = false
    end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    reset()
    runPlayer = false
end)
