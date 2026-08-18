local screenW, screenH = guiGetScreenSize()

local show = {
    active = false,
    startedAt = 0,
    centerX = 0,
    centerY = 0,
    centerZ = 0,
    vehicle = nil,
    heroFire = nil,
    fireCount = 0,
    duration = 0,
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

local function drawCaption(title, subtitle, accent)
    local left = screenW * 0.065
    local bottom = screenH * 0.90
    local width = screenW * 0.64
    local titleHeight = screenH * 0.055

    dxDrawRectangle(left - 18, bottom - 92, width, 82, tocolor(0, 0, 0, 150), false)
    dxDrawRectangle(left - 18, bottom - 92, 5, 82, accent or tocolor(255, 140, 40, 240), false)
    dxDrawText(title, left, bottom - 87, left + width, bottom - 87 + titleHeight,
        tocolor(255, 255, 255, 255), 1.7, "default-bold", "left", "top", false, false, false)
    dxDrawText(subtitle, left, bottom - 48, left + width, bottom,
        tocolor(220, 220, 220, 245), 1.0, "default", "left", "top", false, false, false)
end

local function drawCinematicBars()
    local barHeight = screenH * 0.045
    dxDrawRectangle(0, 0, screenW, barHeight, tocolor(0, 0, 0, 235), false)
    dxDrawRectangle(0, screenH - barHeight, screenW, barHeight, tocolor(0, 0, 0, 235), false)
end

local function renderShow()
    if not show.active then
        return
    end

    local elapsed = getTickCount() - show.startedAt
    local cx, cy, cz = show.centerX, show.centerY, show.centerZ

    -- 0-7s: reveal the full vertical NEON logo from a clean, readable angle.
    if elapsed < 7000 then
        local reveal = smoothstep(elapsed / 1600)
        cameraLerp(
            cx + 23, cy - 42, cz + 13,
            cx, cy, cz,
            cx, cy - 34, cz + 5,
            cx, cy, cz,
            reveal,
            76, 62
        )

    -- 7-11s: push toward the selected flame as it brightens and leaves the O.
    elseif elapsed < 11000 then
        local t = (elapsed - 7000) / 4000
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

    -- 11-21s: chase the vehicle. The same managed fire now follows it as a target.
    elseif elapsed < 21000 then
        local camX, camY, camZ, lookX, lookY, lookZ = getVehicleCamera()
        if camX then
            setCameraMatrix(camX, camY, camZ, lookX, lookY, lookZ, 0, 62)
        else
            setCameraMatrix(cx + 8, cy - 21, cz + 2, cx, cy, cz, 0, 60)
        end

    -- 21s-end: pull back enough to retain the fiery logo while the car rests in frame.
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

    drawCinematicBars()

    if elapsed < 7000 then
        drawCaption(
            ("%d MANAGED FIRES"):format(show.fireCount),
            "One synchronized NEON sign — already beyond GTA's native 60-fire pool."
        )
    elseif elapsed < 11000 then
        drawCaption(
            "EVERY FLAME IS AN ELEMENT",
            "The selected fire changes strength and position live — no extinguish/recreate workaround."
        )
    elseif elapsed < 21000 then
        drawCaption(
            "SAME FIRE. NEW TARGET.",
            "One existing fire leaves the logo, targets a moving vehicle, and follows it."
        )
    else
        drawCaption(
            "SYNCED • SCRIPTABLE • TARGETABLE",
            "Managed fire elements: lifetime, strength, source/target, damage policy and propagation."
        )
    end

    -- Tiny implementation hint for scripters without turning the video into documentation.
    dxDrawText(
        "createFire(...) → element",
        screenW * 0.74, screenH * 0.065, screenW * 0.95, screenH * 0.11,
        tocolor(235, 235, 235, 210), 1.0, "default-bold", "right", "top", false, false, false
    )
end

local function stopShow()
    if not show.active then
        return
    end

    show.active = false
    removeEventHandler("onClientRender", root, renderShow)
    setCameraTarget(localPlayer)

    show.vehicle = nil
    show.heroFire = nil
end

addEvent("fireShowcase:start", true)
addEventHandler("fireShowcase:start", resourceRoot, function(centerX, centerY, centerZ, vehicle, heroFire, fireCount, duration)
    stopShow()

    show.active = true
    show.startedAt = getTickCount()
    show.centerX = centerX
    show.centerY = centerY
    show.centerZ = centerZ
    show.vehicle = vehicle
    show.heroFire = heroFire
    show.fireCount = fireCount
    show.duration = duration

    addEventHandler("onClientRender", root, renderShow)
end)

addEvent("fireShowcase:stop", true)
addEventHandler("fireShowcase:stop", resourceRoot, stopShow)

addEventHandler("onClientResourceStop", resourceRoot, stopShow)
