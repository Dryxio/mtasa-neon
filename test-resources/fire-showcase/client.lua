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

    -- Give the managed fire FX plenty of time to stream in and settle. The camera
    -- eases into the logo for the first few seconds, then stays almost completely
    -- still so the full NEON word remains readable on video.
    if elapsed < 14000 then
        local reveal = smoothstep(elapsed / 3000)
        cameraLerp(
            cx + 23, cy - 42, cz + 13,
            cx, cy, cz,
            cx, cy - 34, cz + 5,
            cx, cy, cz,
            reveal,
            76, 62
        )

    -- Move in on the selected fire only after the complete logo has been visible
    -- for a long uninterrupted shot.
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
            cx, cy - 34, cz + 5,
            cx, cy, cz,
            cx + 8, cy - 21, cz + 2,
            lookX, lookY, lookZ,
            t,
            62, 55
        )

    -- Follow the car while the very same fire element is targeting it.
    elseif elapsed < 31000 then
        local camX, camY, camZ, lookX, lookY, lookZ = getVehicleCamera()
        if camX then
            setCameraMatrix(camX, camY, camZ, lookX, lookY, lookZ, 0, 62)
        else
            setCameraMatrix(cx + 8, cy - 21, cz + 2, cx, cy, cz, 0, 60)
        end

    -- Finish on a clean wide gameplay shot with both the logo and car area visible.
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
        setCameraMatrix(cx + 27, cy - 43, cz + 15, targetX, targetY, targetZ, 0, 74)
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
