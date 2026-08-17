local pickupRadius = 2.5

local function getClosestDrop(maxDistance)
    local px, py, pz = getElementPosition(localPlayer)
    local closest, closestDistanceSquared
    local limitSquared = maxDistance * maxDistance

    for _, object in ipairs(getElementsByType("object", root, true)) do
        if getElementData(object, "physicalDrop") then
            local ox, oy, oz = getElementPosition(object)
            local dx, dy, dz = px - ox, py - oy, pz - oz
            local distanceSquared = dx * dx + dy * dy + dz * dz
            if distanceSquared <= limitSquared and (not closestDistanceSquared or distanceSquared < closestDistanceSquared) then
                closest = object
                closestDistanceSquared = distanceSquared
            end
        end
    end

    return closest, closestDistanceSquared and math.sqrt(closestDistanceSquared) or nil
end

local function pickupClosestDrop()
    local object = getClosestDrop(pickupRadius)
    if object then
        triggerServerEvent("physicalDrop:pickup", resourceRoot, object)
    end
end

addCommandHandler("physpickup", pickupClosestDrop)
bindKey("e", "down", pickupClosestDrop)

addEventHandler("onClientResourceStart", resourceRoot, function()
    triggerServerEvent("physicalDrop:ready", resourceRoot)
    outputChatBox("[physical-drop] /physdrop [weapon] [ammo] to throw; E or /physpickup to collect.", 160, 220, 255)
end)

addEventHandler("onClientRender", root, function()
    local px, py, pz = getElementPosition(localPlayer)

    for _, object in ipairs(getElementsByType("object", root, true)) do
        if getElementData(object, "physicalDrop") then
            local x, y, z = getElementPosition(object)
            local dx, dy, dz = px - x, py - y, pz - z
            local distanceSquared = dx * dx + dy * dy + dz * dz
            if distanceSquared < 20 * 20 then
                local sx, sy = getScreenFromWorldPosition(x, y, z + 0.35, 0.06)
                if sx then
                    local weapon = getElementData(object, "physicalDrop:weapon") or "?"
                    local ammo = getElementData(object, "physicalDrop:ammo") or "?"
                    local distance = math.sqrt(distanceSquared)
                    local text = ("weapon %s | ammo %s | %.1fm"):format(tostring(weapon), tostring(ammo), distance)
                    dxDrawText(text, sx - 180, sy - 15, sx + 180, sy + 15, tocolor(255, 255, 255, 230), 1, "default-bold", "center", "center")
                end
            end
        end
    end
end)
