local target
local lastWorldModel
local targetStreamedIn = false

local showcaseEnabled = true
local targetMarker
local damagePulseUntil = 0
local damagePulseText
local breakPulseUntil = 0
local breakPulsePosition
local breakFlashMarker
local breakFlashUntil = 0

local objective
local objectiveMarker

local function log(message, r, g, b)
    outputChatBox("[world-object] " .. message, r or 220, g or 220, b or 220)
    outputDebugString("[world-object] " .. message)
end

local function getWorldObjects()
    return getElementsByType("worldobject", root, true)
end

local function distanceSquared(x1, y1, z1, x2, y2, z2)
    local dx, dy, dz = x1 - x2, y1 - y2, z1 - z2
    return dx * dx + dy * dy + dz * dz
end

local function destroyLocalElement(element)
    if isElement(element) then
        destroyElement(element)
    end
end

local function clearTargetMarker()
    destroyLocalElement(targetMarker)
    targetMarker = nil
end

local function clearBreakFlash()
    destroyLocalElement(breakFlashMarker)
    breakFlashMarker = nil
    breakFlashUntil = 0
end

local function clearObjective()
    destroyLocalElement(objectiveMarker)
    objectiveMarker = nil
    objective = nil
end

local function ensureTargetMarker()
    if not showcaseEnabled or not isElement(target) or not targetStreamedIn then
        clearTargetMarker()
        return
    end

    local x, y, z = getElementPosition(target)
    if not x then
        clearTargetMarker()
        return
    end

    if not isElement(targetMarker) then
        targetMarker = createMarker(x, y, z + 0.15, "corona", 1.15, 70, 190, 255, 145)
    else
        setElementPosition(targetMarker, x, y, z + 0.15)
    end

    local now = getTickCount()
    if now < breakPulseUntil or now < damagePulseUntil then
        setMarkerColor(targetMarker, 255, 70, 55, 190)
    else
        setMarkerColor(targetMarker, 70, 190, 255, 145)
    end
end

local function draw3DLabel(text, x, y, z, color, scale)
    local sx, sy = getScreenFromWorldPosition(x, y, z, 0.08)
    if not sx then
        return
    end

    local width = 360
    local height = 50
    scale = scale or 1.0

    dxDrawText(text, sx - width / 2 + 2, sy - height / 2 + 2, sx + width / 2 + 2, sy + height / 2 + 2,
        tocolor(0, 0, 0, 210), scale, "default-bold", "center", "center", false, false, false, true)
    dxDrawText(text, sx - width / 2, sy - height / 2, sx + width / 2, sy + height / 2,
        color, scale, "default-bold", "center", "center", false, false, false, true)
end

local function findClosestWorldObject(x, y, z, maxDistance)
    local best, bestDistance = nil, (maxDistance or 5) ^ 2
    for _, element in ipairs(getWorldObjects()) do
        local ex, ey, ez = getElementPosition(element)
        if ex then
            local distance = distanceSquared(x, y, z, ex, ey, ez)
            if distance < bestDistance then
                best, bestDistance = element, distance
            end
        end
    end
    return best, math.sqrt(bestDistance)
end

local function targetFromCamera()
    local cx, cy, cz, lx, ly, lz = getCameraMatrix()
    local dx, dy, dz = lx - cx, ly - cy, lz - cz
    local length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length == 0 then
        return false
    end

    local range = 100
    local ex, ey, ez = cx + dx / length * range, cy + dy / length * range, cz + dz / length * range
    local result = {processLineOfSight(cx, cy, cz, ex, ey, ez, true, true, true, true, true, false, false, false, localPlayer, true)}
    if not result[1] then
        log("LOS: no hit", 255, 180, 120)
        return false
    end

    local hitX, hitY, hitZ = result[2], result[3], result[4]
    lastWorldModel = result[12]
    local candidate, distance = findClosestWorldObject(hitX, hitY, hitZ, 8)
    if not candidate then
        log(("LOS hit %.2f %.2f %.2f, model=%s, but no worldobject proxy nearby"):format(hitX, hitY, hitZ, tostring(lastWorldModel)), 255, 180, 120)
        return false
    end

    clearObjective()
    target = candidate
    targetStreamedIn = true
    damagePulseUntil = 0
    breakPulseUntil = 0
    clearBreakFlash()
    ensureTargetMarker()

    local x, y, z = getElementPosition(target)
    log(("target=%s model(LOS)=%s distance=%.2f pos=%.3f %.3f %.3f"):format(tostring(target), tostring(lastWorldModel), distance, x, y, z), 120, 255, 160)
    return true
end

addCommandHandler("wotarget", targetFromCamera)

addCommandHandler("wolist", function()
    local elements = getWorldObjects()
    log(("%d worldobject proxies"):format(#elements), 120, 200, 255)
    for index, element in ipairs(elements) do
        if index > 12 then
            log(("... %d more"):format(#elements - 12))
            break
        end
        local x, y, z = getElementPosition(element)
        log(("#%d %s @ %.2f %.2f %.2f%s"):format(index, tostring(element), x or 0, y or 0, z or 0, element == target and " [TARGET]" or ""))
    end
end)

addCommandHandler("woshowcase", function()
    showcaseEnabled = not showcaseEnabled
    if not showcaseEnabled then
        clearTargetMarker()
    else
        ensureTargetMarker()
    end
    log("showcase visuals => " .. tostring(showcaseEnabled), 120, 220, 255)
end)

addCommandHandler("woobjective", function()
    if not isElement(target) then
        log("No target. Aim at a native physics object and use /wotarget first.", 255, 180, 120)
        return
    end

    local px, py = getElementPosition(localPlayer)
    local tx, ty, tz = getElementPosition(target)
    if not tx then
        log("Target transform unavailable.", 255, 120, 120)
        return
    end

    local dx, dy = tx - px, ty - py
    local length = math.sqrt(dx * dx + dy * dy)
    if length < 0.1 then
        local _, _, heading = getElementRotation(localPlayer)
        local radians = math.rad(heading)
        dx, dy = -math.sin(radians), math.cos(radians)
        length = 1
    end

    local pushDistance = 5.0
    local goalX = tx + dx / length * pushDistance
    local goalY = ty + dy / length * pushDistance
    local goalZ = tz - 0.65

    clearObjective()
    objectiveMarker = createMarker(goalX, goalY, goalZ, "cylinder", 2.6, 70, 255, 120, 105)
    objective = {
        target = target,
        x = goalX,
        y = goalY,
        z = goalZ,
        radius = 1.55,
        complete = false,
        completedAt = 0,
    }

    log("PHYSICS OBJECTIVE: push the target into the green zone.", 80, 255, 130)
end)

addCommandHandler("wopull", function()
    if not isElement(target) then
        log("No target. Aim at a native physics object and use /wotarget first.", 255, 180, 120)
        return
    end

    local x, y, z = getElementPosition(localPlayer)
    local _, _, heading = getElementRotation(localPlayer)
    local radians = math.rad(heading)
    local ok = setElementPosition(target, x - math.sin(radians) * 2.0, y + math.cos(radians) * 2.0, z + 0.4)
    log("setElementPosition(target) => " .. tostring(ok), ok and 120 or 255, ok and 255 or 120, 160)
end)

addCommandHandler("worotate", function()
    if not isElement(target) then
        log("No target. Use /wotarget first.", 255, 180, 120)
        return
    end

    local matrix = getElementMatrix(target)
    if not matrix then
        log("getElementMatrix(target) failed", 255, 120, 120)
        return
    end

    local radians = math.rad(30)
    local c, s = math.cos(radians), math.sin(radians)
    local right = {matrix[1][1], matrix[1][2], matrix[1][3]}
    local front = {matrix[2][1], matrix[2][2], matrix[2][3]}
    for axis = 1, 3 do
        matrix[1][axis] = right[axis] * c + front[axis] * s
        matrix[2][axis] = front[axis] * c - right[axis] * s
    end

    local ok = setElementMatrix(target, matrix)
    log("setElementMatrix(target, +30deg local Z) => " .. tostring(ok), ok and 120 or 255, ok and 255 or 120, 160)
end)

addCommandHandler("womatrix", function()
    if not isElement(target) then
        log("No target. Use /wotarget first.", 255, 180, 120)
        return
    end

    local matrix = getElementMatrix(target)
    if not matrix then
        log("getElementMatrix(target) failed", 255, 120, 120)
        return
    end

    for row = 1, 4 do
        log(("M%d %.4f %.4f %.4f %.4f"):format(row, matrix[row][1], matrix[row][2], matrix[row][3], matrix[row][4]))
    end
end)

addCommandHandler("wodestroy", function()
    if not isElement(target) then
        log("No target. Use /wotarget first.", 255, 180, 120)
        return
    end

    local before = target
    local ok = destroyElement(target)
    log(("destroyElement(%s) => %s; still valid=%s (expected: false / true)"):format(tostring(before), tostring(ok), tostring(isElement(before))), ok and 255 or 120, ok and 120 or 255, 120)
end)

addCommandHandler("woclear", function()
    clearTargetMarker()
    clearObjective()
    clearBreakFlash()
    target = nil
    lastWorldModel = nil
    targetStreamedIn = false
    log("target/showcase cleared")
end)

addEventHandler("onClientWorldObjectDamage", root, function(loss, attacker, model, x, y, z)
    log(("DAMAGE source=%s model=%s loss=%.3f attacker=%s @ %.2f %.2f %.2f"):format(tostring(source), tostring(model), tonumber(loss) or 0, tostring(attacker), x, y, z), 255, 220, 120)

    if source == target then
        damagePulseUntil = getTickCount() + 700
        damagePulseText = ("DAMAGE %.1f"):format(tonumber(loss) or 0)
    end
end)

addEventHandler("onClientWorldObjectBreak", root, function(attacker, model, x, y, z)
    log(("BREAK source=%s model=%s attacker=%s @ %.2f %.2f %.2f"):format(tostring(source), tostring(model), tostring(attacker), x, y, z), 255, 140, 120)

    if source == target then
        local now = getTickCount()
        breakPulseUntil = now + 1800
        breakPulsePosition = {x, y, z}
        clearBreakFlash()
        breakFlashMarker = createMarker(x, y, z + 0.25, "corona", 2.2, 255, 70, 45, 190)
        breakFlashUntil = now + 500

        if objective and objective.target == source and not objective.complete then
            clearObjective()
        end
    end
end)

addEventHandler("onClientElementStreamIn", root, function()
    if getElementType(source) ~= "worldobject" then
        return
    end

    if source == target then
        targetStreamedIn = true
        ensureTargetMarker()
    end

    log(("STREAM IN %s%s"):format(tostring(source), source == target and " [SAME TARGET]" or ""), 120, 255, 160)
end)

addEventHandler("onClientElementStreamOut", root, function()
    if getElementType(source) ~= "worldobject" then
        return
    end

    if source == target then
        targetStreamedIn = false
        clearTargetMarker()
    end

    log(("STREAM OUT %s%s"):format(tostring(source), source == target and " [TARGET PRESERVED]" or ""), 160, 200, 255)
end)

addEventHandler("onClientRender", root, function()
    local now = getTickCount()

    if isElement(breakFlashMarker) and now >= breakFlashUntil then
        clearBreakFlash()
    end

    if objective then
        if not isElement(objective.target) then
            clearObjective()
        elseif not objective.complete then
            local x, y, z = getElementPosition(objective.target)
            if x then
                local dx, dy = x - objective.x, y - objective.y
                local horizontalDistance = math.sqrt(dx * dx + dy * dy)
                if horizontalDistance <= objective.radius and math.abs(z - objective.z) <= 2.0 then
                    objective.complete = true
                    objective.completedAt = now
                    setMarkerColor(objectiveMarker, 100, 255, 140, 190)
                    log("OBJECT DELIVERED! Native GTA physics drove a Lua gameplay objective.", 100, 255, 140)
                end
            end
        elseif now - objective.completedAt >= 2500 then
            clearObjective()
        end
    end

    ensureTargetMarker()

    if showcaseEnabled and isElement(target) then
        local x, y, z = getElementPosition(target)
        if x then
            if targetStreamedIn then
                dxDrawLine3D(x, y, z + 0.15, x, y, z + 1.25, tocolor(70, 190, 255, 210), 2.0)
            end

            local labelColor = tocolor(90, 210, 255, 245)
            local label = ("PHYSICS OBJECT\npos %.2f  %.2f  %.2f"):format(x, y, z)
            draw3DLabel(label, x, y, z + 1.55, labelColor, 1.0)

            if now < damagePulseUntil then
                draw3DLabel(damagePulseText or "DAMAGE", x, y, z + 2.2, tocolor(255, 85, 65, 255), 1.2)
            end
        end

        if now < breakPulseUntil and breakPulsePosition then
            draw3DLabel("OBJECT BROKEN", breakPulsePosition[1], breakPulsePosition[2], breakPulsePosition[3] + 1.8,
                tocolor(255, 80, 55, 255), 1.25)
        end
    end

    if objective then
        local objectiveColor = objective.complete and tocolor(100, 255, 140, 255) or tocolor(80, 255, 130, 245)
        local objectiveText = objective.complete and "OBJECT DELIVERED!" or "PUSH INTO THE ZONE"
        draw3DLabel(objectiveText, objective.x, objective.y, objective.z + 1.1, objectiveColor, 1.15)
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    clearTargetMarker()
    clearObjective()
    clearBreakFlash()
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    log("showcase ready: /wotarget -> push/shoot | /woobjective -> push into green zone", 120, 255, 160)
    log("tools: /wolist /woshowcase /wopull /worotate /womatrix /wodestroy /woclear", 160, 210, 255)
end)