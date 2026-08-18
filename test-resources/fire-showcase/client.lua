local show = {
    active = false,
    startedAt = 0,
    centerX = 0,
    centerY = 0,
    centerZ = 0,
    vehicle = nil,
    heroFire = nil,
}

local function clamp(value, minimum, maximum)
    return math.max(minimum, math.min(maximum, value))
end

local function smoothstep(value)
    value = clamp(value, 0, 1)
    return value * value * (3 - 2 * value)
end

local function lerp(a, b, t)
    return a + (b - a) * t
end

local function cameraLerp(ax, ay, az, alx, aly, alz, bx, by, bz, blx, bly, blz, t, fovA, fovB)
    t = smoothstep(t)
    setCameraMatrix(
        lerp(ax, bx, t),
        lerp(ay, by, t),
        lerp(az, bz, t),
        lerp(alx, blx, t),
        lerp(aly, bly, t),
        lerp(alz, blz, t),
        0,
        lerp(fovA or 65, fovB or 65, t)
    )
end

local function getVehicleCamera()
    if not isElement(show.vehicle) then
        return nil
    end

    local x, y, z = getElementPosition(show.vehicle)
    if type(x) ~= "number" or type(y) ~= "number" or type(z) ~= "number" then
        return nil
    end

    local vx, vy = getElementVelocity(show.vehicle)
    if type(vx) ~= "number" or type(vy) ~= "number" then
        vx, vy = 1, 0
    end

    local speed = math.sqrt(vx * vx + vy * vy)
    local dirX, dirY = 1, 0
    if speed > 0.005 then
        dirX, dirY = vx / speed, vy / speed
    end

    return
        x - dirX * 11.0 - dirY * 3.5,
        y - dirY * 11.0 + dirX * 3.5,
        z + 5.0,
        x + dirX * 2.5,
        y + dirY * 2.5,
        z + 1.0
end

local function setPresentationUI(visible)
    showPlayerHudComponent("all", visible)
    showChat(visible)
end

local function renderShow()
    if not show.active then
        return
    end

    local elapsed = getTickCount() - show.startedAt
    local cx, cy, cz = show.centerX, show.centerY, show.centerZ

    -- Stay close enough for every fire FX to be clearly rendered from the first
    -- recorded frame. Use a wider FOV instead of moving the camera far away just
    -- to fit the whole word in shot.
    if elapsed < 14000 then
        local t = smoothstep(elapsed / 14000)
        cameraLerp(
            cx, cy - 22.0, cz + 3.2,
            cx, cy, cz,
            cx, cy - 19.5, cz + 2.2,
            cx, cy, cz,
            t,
            90, 84
        )

    -- After the long clean logo shot, move much closer to the selected fire as it
    -- leaves the O.
    elseif elapsed < 19000 then
        local t = (elapsed - 14000) / 5000
        local lookX, lookY, lookZ = cx + 5, cy, cz
        if isElement(show.heroFire) then
            local fireX, fireY, fireZ = getElementPosition(show.heroFire)
            if type(fireX) == "number" and type(fireY) == "number" and type(fireZ) == "number" then
                lookX, lookY, lookZ = fireX, fireY, fireZ
            end
        end

        cameraLerp(
            cx, cy - 19.5, cz + 2.2,
            cx, cy, cz,
            cx + 6, cy - 13.5, cz + 1.5,
            lookX, lookY, lookZ,
            t,
            84, 68
        )

    -- Follow the car while the very same fire element is targeting it.
    elseif elapsed < 31000 then
        local camX, camY, camZ, lookX, lookY, lookZ = getVehicleCamera()
        if camX then
            setCameraMatrix(camX, camY, camZ, lookX, lookY, lookZ, 0, 62)
        else
            setCameraMatrix(cx + 6, cy - 13.5, cz + 1.5, cx, cy, cz, 0, 68)
        end

    -- Finish close enough that the remaining logo fires are still clearly visible.
    else
        local vehicleX, vehicleY, vehicleZ = cx + 16, cy - 13, 16.6
        if isElement(show.vehicle) then
            local x, y, z = getElementPosition(show.vehicle)
            if type(x) == "number" and type(y) == "number" and type(z) == "number" then
                vehicleX, vehicleY, vehicleZ = x, y, z
            end
        end

        local targetX = (cx + vehicleX) * 0.5
        local targetY = (cy + vehicleY) * 0.5
        local targetZ = math.max(cz, vehicleZ + 2)
        setCameraMatrix(cx + 18, cy - 24, cz + 9, targetX, targetY, targetZ, 0, 82)
    end
end

local function stopShow()
    if not show.active then
        return
    end

    show.active = false
    removeEventHandler("onClientRender", root, renderShow)
    setCameraTarget(localPlayer)
    setPresentationUI(true)

    show.vehicle = nil
    show.heroFire = nil
end

addEvent("fireShowcase:start", true)
addEventHandler("fireShowcase:start", resourceRoot, function(centerX, centerY, centerZ, vehicle, heroFire)
    stopShow()

    show.active = true
    show.startedAt = getTickCount()
    show.centerX = centerX
    show.centerY = centerY
    show.centerZ = centerZ
    show.vehicle = vehicle
    show.heroFire = heroFire

    setPresentationUI(false)
    addEventHandler("onClientRender", root, renderShow)
end)

addEvent("fireShowcase:stop", true)
addEventHandler("fireShowcase:stop", resourceRoot, stopShow)

addEventHandler("onClientResourceStop", resourceRoot, stopShow)
