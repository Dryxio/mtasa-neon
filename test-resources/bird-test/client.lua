local tracked = {}
local serial = 0
local shotTarget = nil
local cancelledShotTarget = nil

local function log(kind, name, detail)
    local suffix = detail and (": " .. tostring(detail)) or ""
    local message = ("[BIRDTEST] %s %s%s"):format(kind, name, suffix)
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
        if isElement(element) then destroyElement(element) end
    end
    tracked = {}
    shotTarget = nil
    cancelledShotTarget = nil
end

local function forwardPosition(distance, height)
    local x, y, z = getElementPosition(localPlayer)
    local _, _, rz = getElementRotation(localPlayer)
    local a = math.rad(rz)
    return x - math.sin(a) * distance, y + math.cos(a) * distance, z + (height or 1.4)
end

local function countBirds()
    local count = 0
    for _, element in ipairs(getElementsByType("bird")) do
        if isElement(element) then count = count + 1 end
    end
    return count
end

local function basic()
    local x, y, z = forwardPosition(6, 2)
    local bird = track(createBird(x, y, z, {
        preset = "normal",
        velocity = {0, 0, 0},
        targetVelocity = {2, 1, 0.5},
        size = 0.8,
        renderDistance = 120,
        bodyColor = {220, 80, 80},
        wingColor = {255, 180, 180},
        wingBeatTime = 350,
        curvedFlight = false,
        shootable = true,
    }))

    check("basic.create", isElement(bird), tostring(bird))
    if not isElement(bird) then return false end
    check("basic.type", getElementType(bird) == "bird", getElementType(bird))
    check("size.get", math.abs((getBirdSize(bird) or 0) - 0.8) < 0.01, getBirdSize(bird))
    check("size.set", setBirdSize(bird, 1.2) and math.abs(getBirdSize(bird) - 1.2) < 0.01, getBirdSize(bird))
    check("render-distance", setBirdRenderDistance(bird, 180) and math.abs(getBirdRenderDistance(bird) - 180) < 0.01, getBirdRenderDistance(bird))
    check("wingbeat", setBirdWingBeatTime(bird, 777) and getBirdWingBeatTime(bird) == 777, getBirdWingBeatTime(bird))
    check("curve", setBirdCurvedFlightEnabled(bird, true) and isBirdCurvedFlightEnabled(bird), tostring(isBirdCurvedFlightEnabled(bird)))
    check("shootable", setBirdShootable(bird, false) and not isBirdShootable(bird), tostring(isBirdShootable(bird)))
    check("movement", setBirdMovementEnabled(bird, false) and not isBirdMovementEnabled(bird), tostring(isBirdMovementEnabled(bird)))

    local velocity = getBirdVelocity(bird)
    check("velocity.get", velocity and math.abs(velocity.x) < 0.01 and math.abs(velocity.y) < 0.01, velocity and tostring(velocity) or "false")
    check("velocity.set", setBirdVelocity(bird, Vector3(1, 2, 3)), tostring(getBirdVelocity(bird)))
    local target = getBirdTargetVelocity(bird)
    check("target.get", target and math.abs(target.x - 2) < 0.01 and math.abs(target.y - 1) < 0.01, target and tostring(target) or "false")
    check("target.set", setBirdTargetVelocity(bird, Vector3(-2, 3, 0)), tostring(getBirdTargetVelocity(bird)))

    check("colors.set", setBirdColors(bird, 10, 20, 30, 40, 50, 60))
    local r, g, b, wr, wg, wb = getBirdColors(bird)
    check("colors.get", r == 10 and g == 20 and b == 30 and wr == 40 and wg == 50 and wb == 60,
        ("%s,%s,%s / %s,%s,%s"):format(tostring(r), tostring(g), tostring(b), tostring(wr), tostring(wg), tostring(wb)))

    setBirdMovementEnabled(bird, true)
    setBirdShootable(bird, true)
    setBirdCurvedFlightEnabled(bird, false)
    return true
end

local function movementTest(runSerial)
    local x, y, z = forwardPosition(9, 3)
    local bird = track(createBird(x, y, z, {velocity = {0, 0, 0}, targetVelocity = {5, 0, 0}, renderDistance = 150}))
    check("movement.create", isElement(bird))
    if not isElement(bird) then return end
    local sx = select(1, getElementPosition(bird))
    setTimer(function()
        if runSerial ~= serial or not isElement(bird) then return end
        local nx = select(1, getElementPosition(bird))
        check("movement.position-changed", nx > sx + 0.05, ("delta=%.3f"):format(nx - sx))
        setBirdMovementEnabled(bird, false)
        local frozenX = nx
        setTimer(function()
            if runSerial ~= serial or not isElement(bird) then return end
            local fx = select(1, getElementPosition(bird))
            check("movement.freeze", math.abs(fx - frozenX) < 0.02, ("delta=%.3f"):format(fx - frozenX))
        end, 500, 1)
    end, 700, 1)
end

local function bulkTest(runSerial, count)
    local baseline = countBirds()
    local x, y, z = getElementPosition(localPlayer)
    local created = {}
    for i = 1, count do
        local angle = (i / count) * math.pi * 2
        local bird = createBird(x + math.cos(angle) * 25, y + math.sin(angle) * 25, z + 8 + (i % 7) * 0.6, {
            velocity = {-math.sin(angle) * 3, math.cos(angle) * 3, 0},
            targetVelocity = {-math.sin(angle) * 3, math.cos(angle) * 3, 0},
            curvedFlight = true,
            renderDistance = 180,
            size = 0.65,
        })
        if isElement(bird) then
            track(bird)
            created[#created + 1] = bird
        end
    end
    check("bulk.over-native-pool", #created == count, ("created=%d requested=%d native=6"):format(#created, count))
    setTimer(function()
        if runSerial ~= serial then return end
        for _, bird in ipairs(created) do
            if isElement(bird) then destroyElement(bird) end
            tracked[bird] = nil
        end
        local remaining = countBirds()
        check("bulk.cleanup", remaining == baseline, ("baseline=%d remaining=%d"):format(baseline, remaining))
    end, 1800, 1)
end

local function invalidTest()
    local x, y, z = getElementPosition(localPlayer)
    check("invalid.size", createBird(x, y, z + 3, {size = -1}) == false)
    check("invalid.distance", createBird(x, y, z + 3, {renderDistance = 0}) == false)
    local bird = track(createBird(x, y, z + 3, {}))
    if isElement(bird) then
        check("invalid.setter-size", setBirdSize(bird, -2) == false)
        check("invalid.setter-wingbeat", setBirdWingBeatTime(bird, 0) == false)
    end
end

local function runAll()
    reset()
    local runSerial = serial
    outputChatBox(("[BIRDTEST] RUN serial=%d"):format(runSerial), 255, 200, 80)
    if not basic() then return end
    movementTest(runSerial)
    bulkTest(runSerial, 128)
    invalidTest()
    setTimer(function()
        if runSerial == serial then
            outputChatBox(("[BIRDTEST] DONE serial=%d -- inspect PASS/FAIL above"):format(runSerial), 255, 200, 80)
        end
    end, 2600, 1)
end

local function setupShoot(cancelled)
    reset()
    local x, y, z = forwardPosition(14, 1.8)
    local bird = track(createBird(x, y, z, {
        velocity = {0, 0, 0}, targetVelocity = {0, 0, 0}, movementEnabled = false,
        size = 2.0, bodyColor = cancelled and {80, 160, 255} or {255, 60, 60}, wingColor = {255, 255, 255}, renderDistance = 100,
    }))
    if cancelled then cancelledShotTarget = bird else shotTarget = bird end
    outputChatBox(cancelled and "[BIRDTEST] Shoot the BLUE bird; the event is cancelled so it must survive." or
        "[BIRDTEST] Shoot the RED bird; it should emit onClientBirdShot and disappear.", 255, 210, 80)
end

addEventHandler("onClientBirdShot", root, function(attacker, weapon, x, y, z)
    if source == cancelledShotTarget then
        check("shot.cancel-event", attacker == localPlayer, weapon)
        cancelEvent()
    elseif source == shotTarget then
        check("shot.event", attacker == localPlayer, ("weapon=%s hit=%.1f,%.1f,%.1f"):format(tostring(weapon), x, y, z))
    end
end)

addEventHandler("onClientPlayerWeaponFire", localPlayer, function(weapon, ammo, ammoInClip, hitX, hitY, hitZ, hitElement, startX, startY, startZ)
    if not hitX or not startX then return end
    processBirdGunShot(Vector3(startX, startY, startZ), Vector3(hitX, hitY, hitZ), weapon)
    if shotTarget and not isElement(shotTarget) then
        check("shot.destroyed", true)
        tracked[shotTarget] = nil
        shotTarget = nil
    end
end)

addCommandHandler("birdtest", function(_, caseName, value)
    caseName = (caseName or "all"):lower()
    if caseName == "all" then
        runAll()
    elseif caseName == "basic" then
        reset(); basic()
    elseif caseName == "stress" then
        reset(); bulkTest(serial, math.max(1, math.min(tonumber(value) or 256, 256)))
    elseif caseName == "shoot" then
        setupShoot(false)
    elseif caseName == "shootcancel" then
        setupShoot(true)
    elseif caseName == "status" then
        outputChatBox(("[BIRDTEST] birds=%d tracked=%d"):format(countBirds(), (function() local n=0 for e in pairs(tracked) do if isElement(e) then n=n+1 end end return n end)()), 255, 200, 80)
    elseif caseName == "reset" then
        reset(); log("PASS", "reset")
    else
        outputChatBox("[BIRDTEST] cases: all, basic, stress [count], shoot, shootcancel, status, reset", 255, 200, 80)
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, reset)
