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
    local message = ("[ROPETEST] %s %s%s"):format(kind, name, suffix)
    if kind == "FAIL" then
        emit(message, 1, 255, 80, 80)
    else
        emit(message, 3, 100, 255, 100)
    end
end

local function pass(name, detail) log("PASS", name, detail) end
local function fail(name, detail) log("FAIL", name, detail) end
local function check(name, condition, detail)
    if condition then pass(name, detail) else fail(name, detail) end
    return condition
end

local function track(element)
    if isElement(element) then tracked[element] = true end
    return element
end

local function reset()
    for element in pairs(tracked) do
        if isElement(element) then destroyElement(element) end
    end
    tracked = {}
end

local function spawnPosition(player, forward, side)
    local x, y, z = getElementPosition(player)
    return x + (forward or 4), y + (side or 0), z + 3
end

local function countRopes()
    local count = 0
    for _, rope in ipairs(getElementsByType("rope")) do
        if isElement(rope) then count = count + 1 end
    end
    return count
end

local function runBasic(player, serial)
    local x, y, z = spawnPosition(player, 4)
    local rope = track(createRope(x, y, z, {
        type = "swat",
        duration = 5000,
        physics = false,
        fixedNode = 0,
        sitOnGround = false,
        winchHeight = 0.5,
        topVelocity = {0.001, 0, 0},
    }))

    check("basic.create", isElement(rope), tostring(rope))
    if not isElement(rope) then return false end

    check("basic.element-type", getElementType(rope) == "rope", getElementType(rope))
    check("basic.rope-type", getRopeType(rope) == "swat", tostring(getRopeType(rope)))
    check("basic.duration", math.abs((getRopeDuration(rope) or -1) - 5000) < 1, getRopeDuration(rope))
    check("basic.remaining", (getRopeRemainingTime(rope) or -1) > 4500, getRopeRemainingTime(rope))
    check("basic.physics", isRopePhysicsEnabled(rope) == false, tostring(isRopePhysicsEnabled(rope)))

    check("basic.type-set", setRopeType(rope, "swat") and getRopeType(rope) == "swat", getRopeType(rope))
    check("basic.winch-set", setRopeWinchHeight(rope, 0.65) and math.abs((getRopeWinchHeight(rope) or 0) - 0.65) < 0.01, getRopeWinchHeight(rope))
    check("basic.fixed-set", setRopeFixedNode(rope, 2) and getRopeFixedNode(rope) == 2, getRopeFixedNode(rope))
    check("basic.ground-set", setRopeOnGround(rope, true) and isRopeOnGround(rope), tostring(isRopeOnGround(rope)))
    check("basic.velocity-set", setRopeTopVelocity(rope, Vector3(0.002, 0, 0)), "setter returned true")
    check("basic.offset-set", setRopeHolderOffset(rope, Vector3(0, 0, -1)), "setter returned true")

    triggerClientEvent(player, "ropetest:inspect", resourceRoot, rope, serial)
    return true
end

local function runExpiry(player, serial)
    local x, y, z = spawnPosition(player, 6)
    local rope = track(createRope(x, y, z, {type = "swat", duration = 850, physics = false}))
    check("expiry.created", isElement(rope))
    setTimer(function()
        if serial ~= runSerial then return end
        check("expiry.destroyed", not isElement(rope), isElement(rope) and "still alive" or "expired")
        tracked[rope] = nil
    end, 1450, 1)
end

local function runHolder(player, serial)
    local x, y, z = spawnPosition(player, 9)
    local holder = track(createObject(1337, x, y, z - 2))
    if not isElement(holder) then
        fail("holder.object", "createObject failed")
        return
    end
    setElementDimension(holder, getElementDimension(player))
    setElementInterior(holder, getElementInterior(player))

    local rope = track(createRope(x, y, z, {
        type = "swat",
        holder = holder,
        holderOffset = {0, 0, 1.5},
        duration = 8000,
        physics = false,
    }))
    check("holder.rope", isElement(rope))
    if not isElement(rope) then return end

    setElementPosition(holder, x + 2, y + 1, z - 2)
    setTimer(function()
        if serial ~= runSerial or not isElement(rope) or not isElement(holder) then return end
        triggerClientEvent(player, "ropetest:holderInspect", resourceRoot, rope, holder, serial)
    end, 700, 1)
end

local function runPickupState(player, serial)
    local x, y, z = spawnPosition(player, 12)
    local carriedObject = track(createObject(1337, x, y, z - 4))
    local carrierVehicle = track(createVehicle(411, x + 3, y, z - 4))
    local rope = track(createRope(x, y, z, {
        type = "miniMagnet",
        duration = 10000,
        physics = true,
        carriedElement = carriedObject,
    }))

    check("pickup.object-created", isElement(carriedObject))
    check("pickup.vehicle-created", isElement(carrierVehicle))
    check("pickup.rope-created", isElement(rope))
    if not isElement(rope) or not isElement(carriedObject) then return end

    check("pickup.object-state", getRopeCarriedElement(rope) == carriedObject, tostring(getRopeCarriedElement(rope)))
    check("pickup.detach", detachElementFromRope(rope) and getRopeCarriedElement(rope) == false, tostring(getRopeCarriedElement(rope)))
    check("pickup.vehicle-state", isElement(carrierVehicle) and attachElementToRope(rope, carrierVehicle) and getRopeCarriedElement(rope) == carrierVehicle,
        tostring(getRopeCarriedElement(rope)))
    check("pickup.vehicle-detach", detachElementFromRope(rope) and getRopeCarriedElement(rope) == false, tostring(getRopeCarriedElement(rope)))

    -- Reattach the object for the client authority/native pickup observation.
    attachElementToRope(rope, carriedObject)
    triggerClientEvent(player, "ropetest:pickupInspect", resourceRoot, rope, carriedObject, carrierVehicle, serial)
end

local function runLeasing(player, serial)
    local x, y, z = spawnPosition(player, 18)
    local ropes = {}
    for i = 1, 12 do
        local rope = track(createRope(x + (i - 1) * 0.6, y, z, {
            type = "swat",
            duration = 7000,
            physics = false,
        }))
        if isElement(rope) then ropes[#ropes + 1] = rope end
    end
    check("leasing.logical-count", #ropes == 12, ("created=%d"):format(#ropes))
    setTimer(function()
        if serial ~= runSerial then return end
        triggerClientEvent(player, "ropetest:leasingInspect", resourceRoot, ropes, serial)
    end, 900, 1)
end

local function runLocal(player, serial)
    triggerClientEvent(player, "ropetest:local", resourceRoot, serial)
end

local function runLegacy(player, serial)
    triggerClientEvent(player, "ropetest:legacy", resourceRoot, serial)
end

local function runAll(player)
    runSerial = runSerial + 1
    local serial = runSerial
    reset()
    runPlayer = player
    emit(("[ROPETEST] RUN serial=%d player=%s"):format(serial, getPlayerName(player)), 3, 255, 200, 80)

    if not runBasic(player, serial) then
        fail("run", "basic creation failed; dependent tests skipped")
        return
    end
    runExpiry(player, serial)
    runHolder(player, serial)
    runPickupState(player, serial)
    runLeasing(player, serial)
    runLocal(player, serial)
    runLegacy(player, serial)

    setTimer(function()
        if serial == runSerial then
            emit(("[ROPETEST] DONE serial=%d -- inspect PASS/FAIL lines above"):format(serial), 3, 255, 200, 80)
        end
    end, 4500, 1)
end

addEvent("ropetest:clientResult", true)
addEventHandler("ropetest:clientResult", resourceRoot, function(serial, name, ok, detail)
    if not client or serial ~= runSerial then return end
    check("client." .. tostring(name), ok == true, detail)
end)

addCommandHandler("ropetest", function(player, _, caseName)
    if not isElement(player) or getElementType(player) ~= "player" then return end
    runPlayer = player
    caseName = caseName or "all"

    if caseName == "all" then
        runAll(player)
    elseif caseName == "basic" then
        runSerial = runSerial + 1; reset(); runBasic(player, runSerial)
    elseif caseName == "holder" then
        runSerial = runSerial + 1; reset(); runHolder(player, runSerial)
    elseif caseName == "pickup" then
        runSerial = runSerial + 1; reset(); runPickupState(player, runSerial)
    elseif caseName == "leasing" then
        runSerial = runSerial + 1; reset(); runLeasing(player, runSerial)
    elseif caseName == "local" then
        runSerial = runSerial + 1; reset(); runLocal(player, runSerial)
    elseif caseName == "legacy" then
        runSerial = runSerial + 1; reset(); runLegacy(player, runSerial)
    elseif caseName == "late" then
        runSerial = runSerial + 1; reset()
        local x, y, z = spawnPosition(player, 6)
        local rope = track(createRope(x, y, z, {type = "swat", duration = 30000, physics = false, winchHeight = 0.75}))
        if isElement(rope) then
            pass("late.setup", ("remaining=%.0f"):format(getRopeRemainingTime(rope) or -1))
            outputChatBox("[ROPETEST] 30s rope active: join a second client and inspect remaining time / visibility.", player, 255, 200, 80)
        else
            fail("late.setup", "createRope returned false")
        end
    elseif caseName == "status" then
        outputChatBox(("[ROPETEST] tracked=%d worldRopes=%d serial=%d"):format(
            (function() local n = 0 for element in pairs(tracked) do if isElement(element) then n = n + 1 end end return n end)(),
            countRopes(), runSerial), player, 255, 200, 80)
    elseif caseName == "reset" then
        runSerial = runSerial + 1; reset(); pass("reset")
    else
        outputChatBox("[ROPETEST] cases: all, basic, holder, pickup, leasing, local, legacy, late, status, reset", player, 255, 200, 80)
    end
end)

addEventHandler("onPlayerQuit", root, function()
    if source == runPlayer then runPlayer = false end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    reset()
    runPlayer = false
end)
