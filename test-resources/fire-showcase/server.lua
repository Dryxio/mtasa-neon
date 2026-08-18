local SHOW_DIMENSION = 64990
local SHOW_DURATION = 36000

local LOGO_CENTER_X = 405.0
local LOGO_Y = 2535.0
local LOGO_BASE_Z = 18.5
local LOGO_STEP = 1.35
local LETTER_GAP = 2

local patterns = {
    {
        name = "N",
        rows = {
            "10001",
            "11001",
            "10101",
            "10011",
            "10001",
            "10001",
            "10001",
        },
    },
    {
        name = "E",
        rows = {
            "11111",
            "10000",
            "10000",
            "11110",
            "10000",
            "10000",
            "11111",
        },
    },
    {
        name = "O",
        rows = {
            "01110",
            "10001",
            "10001",
            "10001",
            "10001",
            "10001",
            "01110",
        },
    },
    {
        name = "N",
        rows = {
            "10001",
            "11001",
            "10101",
            "10011",
            "10001",
            "10001",
            "10001",
        },
    },
}

local state = {
    running = false,
    serial = 0,
    player = nil,
    fires = {},
    timers = {},
    vehicle = nil,
    heroFire = nil,
    savedPlayer = nil,
}

local function chat(player, message, r, g, b)
    if isElement(player) then
        outputChatBox("[FIRE SHOWCASE] " .. message, player, r or 255, g or 180, b or 70)
    end
end

local function rememberTimer(timer)
    state.timers[#state.timers + 1] = timer
    return timer
end

local function later(delay, callback)
    local serial = state.serial
    return rememberTimer(setTimer(function()
        if state.running and serial == state.serial then
            callback()
        end
    end, delay, 1))
end

local function clearTimers()
    for _, timer in ipairs(state.timers) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    state.timers = {}
end

local function destroyElementSafe(element)
    if isElement(element) then
        destroyElement(element)
    end
end

local function restorePlayer()
    local player = state.player
    local saved = state.savedPlayer
    if not isElement(player) or not saved then
        return
    end

    setElementInterior(player, saved.interior)
    setElementDimension(player, saved.dimension)
    setElementPosition(player, saved.x, saved.y, saved.z)
    setElementRotation(player, saved.rx, saved.ry, saved.rz)
    setElementAlpha(player, saved.alpha)
    setElementFrozen(player, saved.frozen)
end

local function cleanup(notifyClient)
    if not state.running and #state.fires == 0 and not isElement(state.vehicle) then
        return
    end

    state.running = false
    state.serial = state.serial + 1
    clearTimers()

    if notifyClient ~= false and isElement(state.player) then
        triggerClientEvent(state.player, "fireShowcase:stop", resourceRoot)
    end

    for _, fire in ipairs(state.fires) do
        destroyElementSafe(fire)
    end
    state.fires = {}
    state.heroFire = nil

    destroyElementSafe(state.vehicle)
    state.vehicle = nil

    restorePlayer()
    state.savedPlayer = nil
    state.player = nil
end

local function saveAndStagePlayer(player)
    local x, y, z = getElementPosition(player)
    local rx, ry, rz = getElementRotation(player)
    state.savedPlayer = {
        x = x,
        y = y,
        z = z,
        rx = rx,
        ry = ry,
        rz = rz,
        dimension = getElementDimension(player),
        interior = getElementInterior(player),
        alpha = getElementAlpha(player),
        frozen = isElementFrozen(player),
    }

    setElementInterior(player, 0)
    setElementDimension(player, SHOW_DIMENSION)
    setElementPosition(player, LOGO_CENTER_X, LOGO_Y - 42.0, 16.8)
    setElementRotation(player, 0, 0, 0)
    setElementAlpha(player, 0)
    setElementFrozen(player, true)
end

local function buildLogo(player)
    local letterWidth = 5
    local totalColumns = (#patterns * letterWidth) + ((#patterns - 1) * LETTER_GAP)
    local firstX = LOGO_CENTER_X - ((totalColumns - 1) * LOGO_STEP * 0.5)
    local created = 0
    local hero = nil

    for letterIndex, pattern in ipairs(patterns) do
        local columnBase = (letterIndex - 1) * (letterWidth + LETTER_GAP)
        for rowIndex, row in ipairs(pattern.rows) do
            for columnIndex = 1, #row do
                if row:sub(columnIndex, columnIndex) == "1" then
                    local globalColumn = columnBase + (columnIndex - 1)
                    local x = firstX + globalColumn * LOGO_STEP
                    local z = LOGO_BASE_Z + (#pattern.rows - rowIndex) * LOGO_STEP
                    local fire = createFire(x, LOGO_Y, z, {
                        duration = SHOW_DURATION + 5000,
                        strength = 1.0,
                        damage = false,
                        spread = false,
                        source = player,
                    })

                    if isElement(fire) then
                        setElementDimension(fire, SHOW_DIMENSION)
                        setElementInterior(fire, 0)
                        state.fires[#state.fires + 1] = fire
                        created = created + 1

                        if letterIndex == 3 and rowIndex == 4 and columnIndex == 5 then
                            hero = fire
                        end
                    end
                end
            end
        end
    end

    return created, hero, firstX
end

local function animateStrengthWave(firstX)
    for _, fire in ipairs(state.fires) do
        if isElement(fire) then
            local x = select(1, getElementPosition(fire))
            local column = math.floor(((x - firstX) / LOGO_STEP) + 0.5)
            local delay = 6000 + column * 120

            later(delay, function()
                if isElement(fire) then
                    setFireStrength(fire, 2.8)
                end
            end)

            later(delay + 500, function()
                if isElement(fire) and fire ~= state.heroFire then
                    setFireStrength(fire, 1.0)
                end
            end)
        end
    end
end

local function animateHeroToVehicle()
    local hero = state.heroFire
    local vehicle = state.vehicle
    if not isElement(hero) or not isElement(vehicle) then
        return
    end

    setFireTarget(hero, nil)
    setFireStrength(hero, 3.2)

    local startX, startY, startZ = getElementPosition(hero)
    local steps = 28
    local currentStep = 0
    local serial = state.serial
    local timer

    timer = setTimer(function()
        if not state.running or serial ~= state.serial or not isElement(hero) or not isElement(vehicle) then
            if isTimer(timer) then
                killTimer(timer)
            end
            return
        end

        currentStep = currentStep + 1
        local vehicleX, vehicleY, vehicleZ = getElementPosition(vehicle)
        local t = currentStep / steps
        local eased = t * t * (3 - 2 * t)
        local arc = math.sin(t * math.pi) * 2.2

        setElementPosition(
            hero,
            startX + (vehicleX - startX) * eased,
            startY + (vehicleY - startY) * eased,
            startZ + ((vehicleZ + 1.1) - startZ) * eased + arc
        )

        if currentStep >= steps then
            setFireTarget(hero, vehicle)
            setElementVelocity(vehicle, 0.18, 0.0, 0.0)
        end
    end, 50, steps)

    rememberTimer(timer)
end

local function startShow(player)
    if state.running then
        chat(player, "A showcase is already running. Use /fireshow stop first.", 255, 90, 90)
        return
    end

    state.running = true
    state.serial = state.serial + 1
    state.player = player
    state.fires = {}
    state.timers = {}
    state.vehicle = nil
    state.heroFire = nil

    saveAndStagePlayer(player)

    local vehicle = createVehicle(411, LOGO_CENTER_X - 20.0, LOGO_Y - 13.0, 16.6, 0, 0, 90)
    if not isElement(vehicle) then
        chat(player, "Could not create showcase vehicle.", 255, 90, 90)
        cleanup(true)
        return
    end

    state.vehicle = vehicle
    setElementDimension(vehicle, SHOW_DIMENSION)
    setElementInterior(vehicle, 0)
    setElementFrozen(vehicle, true)

    local count, hero, firstX = buildLogo(player)
    state.heroFire = hero

    if count <= 60 or not isElement(hero) then
        chat(player, ("Showcase setup failed (fires=%d, hero=%s)."):format(count, tostring(isElement(hero))), 255, 90, 90)
        cleanup(true)
        return
    end

    animateStrengthWave(firstX)

    later(14500, function()
        if isElement(state.heroFire) then
            setFireStrength(state.heroFire, 3.2)
        end
        if isElement(state.vehicle) then
            setElementFrozen(state.vehicle, false)
        end
    end)

    later(16000, animateHeroToVehicle)

    later(30000, function()
        if isElement(state.vehicle) then
            setElementVelocity(state.vehicle, 0, 0, 0)
        end
    end)

    -- Wait briefly before switching to the showcase camera so EntityAdd packets and
    -- their FX can start arriving before the first recorded frame.
    later(900, function()
        if isElement(state.player) then
            triggerClientEvent(state.player, "fireShowcase:start", resourceRoot,
                LOGO_CENTER_X, LOGO_Y, LOGO_BASE_Z + 4.05, state.vehicle, state.heroFire)
        end
    end)

    later(SHOW_DURATION, function()
        cleanup(true)
    end)
end

addCommandHandler("fireshow", function(player, _, action)
    action = action and action:lower() or "start"

    if action == "stop" or action == "reset" then
        if state.running and state.player ~= player then
            chat(player, "Another player owns the active showcase.", 255, 90, 90)
            return
        end
        cleanup(true)
        return
    end

    if action ~= "start" then
        chat(player, "Usage: /fireshow [start|stop]", 255, 200, 80)
        return
    end

    startShow(player)
end)

addEventHandler("onPlayerQuit", root, function()
    if source == state.player then
        cleanup(false)
    end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    cleanup(true)
end)
