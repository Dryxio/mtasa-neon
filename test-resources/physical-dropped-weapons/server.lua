local weaponModels = {
    [22] = 346, -- Colt 45
    [23] = 347, -- Silenced pistol
    [24] = 348, -- Desert Eagle
    [25] = 349, -- Shotgun
    [26] = 350, -- Sawed-off
    [27] = 351, -- Combat shotgun
    [28] = 352, -- Uzi
    [29] = 353, -- MP5
    [30] = 355, -- AK-47
    [31] = 356, -- M4
    [32] = 372, -- Tec-9
    [33] = 357, -- Country rifle
    [34] = 358, -- Sniper rifle
    [35] = 359, -- Rocket launcher
    [36] = 360, -- Heat-seeking RPG
    [37] = 361, -- Flamethrower
    [38] = 362, -- Minigun
    [39] = 363, -- Satchel
    [41] = 365, -- Spraycan
    [42] = 366, -- Fire extinguisher
    [43] = 367, -- Camera
    [46] = 371, -- Parachute
}

local CONTROL_MODEL = 1220 -- cardboardbox2: vanilla dynamic object with native COL/object.dat
local drops = {}
local comparisonObjects = {}

local function message(player, text, r, g, b)
    outputChatBox("[physical-drop] " .. text, player, r or 210, g or 230, b or 255)
end

local function clamp(value, minimum, maximum)
    return math.max(minimum, math.min(maximum, value))
end

local function createPhysicalDrop(player, weapon, ammo)
    local model = weaponModels[weapon]
    if not model then
        message(player, "Unsupported weapon id " .. tostring(weapon), 255, 140, 120)
        return false
    end

    local x, y, z = getElementPosition(player)
    local _, _, heading = getElementRotation(player)
    local radians = math.rad(heading)
    local forwardX, forwardY = -math.sin(radians), math.cos(radians)

    local object = createObject(model, x + forwardX * 1.0, y + forwardY * 1.0, z + 0.85, 0, 0, heading)
    if not object then
        message(player, "createObject failed for model " .. tostring(model), 255, 100, 100)
        return false
    end

    setElementInterior(object, getElementInterior(player))
    setElementDimension(object, getElementDimension(player))
    setElementData(object, "physicalDrop", true)
    setElementData(object, "physicalDrop:weapon", weapon)
    setElementData(object, "physicalDrop:ammo", ammo)

    if not setObjectDynamicPhysics(object, true) then
        destroyElement(object)
        message(player, "setObjectDynamicPhysics failed", 255, 100, 100)
        return false
    end

    -- MTA velocity is expressed in GTA frame units. These values give a short
    -- CS-like toss without turning the weapon into a projectile.
    setElementVelocity(object, forwardX * 0.18, forwardY * 0.18, 0.11)
    setElementAngularVelocity(object, 0.07, 0.11, 0.16)

    drops[object] = {
        weapon = weapon,
        ammo = ammo,
        owner = player,
        createdAt = getTickCount(),
    }

    addEventHandler("onElementDestroy", object, function()
        drops[source] = nil
    end)

    message(player, ("Dropped weapon %d / model %d / ammo %d"):format(weapon, model, ammo), 120, 255, 160)
    return object
end

local function clearComparisonObjects()
    for object in pairs(comparisonObjects) do
        if isElement(object) then
            destroyElement(object)
        end
    end
    comparisonObjects = {}
end

local function createComparisonObject(player, model, label, lateralOffset)
    local x, y, z = getElementPosition(player)
    local _, _, heading = getElementRotation(player)
    local radians = math.rad(heading)
    local forwardX, forwardY = -math.sin(radians), math.cos(radians)
    local rightX, rightY = math.cos(radians), math.sin(radians)

    local object = createObject(
        model,
        x + forwardX * 1.25 + rightX * lateralOffset,
        y + forwardY * 1.25 + rightY * lateralOffset,
        z + 0.85,
        0, 0, heading
    )
    if not object then
        message(player, ("comparison createObject failed for model %d"):format(model), 255, 100, 100)
        return false
    end

    setElementInterior(object, getElementInterior(player))
    setElementDimension(object, getElementDimension(player))
    setElementData(object, "physicalDrop", true)
    -- Reuse the current client label without requiring a client.lua change.
    setElementData(object, "physicalDrop:weapon", label)
    setElementData(object, "physicalDrop:ammo", model)

    if not setObjectDynamicPhysics(object, true) then
        destroyElement(object)
        message(player, ("comparison setObjectDynamicPhysics failed for model %d"):format(model), 255, 100, 100)
        return false
    end

    -- Deliberately identical impulse for the weapon and vanilla control.
    setElementVelocity(object, forwardX * 0.18, forwardY * 0.18, 0.11)
    setElementAngularVelocity(object, 0.07, 0.11, 0.16)

    comparisonObjects[object] = true
    addEventHandler("onElementDestroy", object, function()
        comparisonObjects[source] = nil
    end)
    return object
end

addCommandHandler("physdrop", function(player, _, weaponArgument, ammoArgument)
    if not isElement(player) or getElementType(player) ~= "player" then
        return
    end

    local weapon = tonumber(weaponArgument) or getPedWeapon(player)
    if weapon == 0 or not weaponModels[weapon] then
        weapon = 30
    end

    local ammo = tonumber(ammoArgument)
    if not ammo then
        ammo = weapon == getPedWeapon(player) and getPedTotalAmmo(player) or 90
    end
    ammo = clamp(math.floor(ammo), 1, 9999)

    createPhysicalDrop(player, weapon, ammo)
end)

addCommandHandler("physcompare", function(player, _, controlModelArgument)
    if not isElement(player) or getElementType(player) ~= "player" then
        return
    end

    clearComparisonObjects()

    local controlModel = math.floor(tonumber(controlModelArgument) or CONTROL_MODEL)
    local weaponObject = createComparisonObject(player, 355, "AK-test", -0.65)
    local controlObject = createComparisonObject(player, controlModel, "vanilla-control", 0.65)

    if weaponObject and controlObject then
        message(player, ("Comparison: AK 355 vs vanilla model %d (default 1220 cardboardbox2). Same impulse; watch both + F8."):format(controlModel), 120, 255, 160)
    end
end)

addCommandHandler("physclear", function(player)
    local count = 0
    for object in pairs(drops) do
        if isElement(object) then
            destroyElement(object)
            count = count + 1
        end
    end
    drops = {}

    for object in pairs(comparisonObjects) do
        if isElement(object) then
            destroyElement(object)
            count = count + 1
        end
    end
    comparisonObjects = {}

    if isElement(player) then
        message(player, "Cleared " .. count .. " drops/test objects")
    end
end)

addEvent("physicalDrop:pickup", true)
addEventHandler("physicalDrop:pickup", resourceRoot, function(object)
    local player = client
    local data = object and drops[object]
    if not player or not data or not isElement(object) then
        return
    end

    if getElementDimension(player) ~= getElementDimension(object) or getElementInterior(player) ~= getElementInterior(object) then
        return
    end

    local px, py, pz = getElementPosition(player)
    local ox, oy, oz = getElementPosition(object)
    local dx, dy, dz = px - ox, py - oy, pz - oz
    if dx * dx + dy * dy + dz * dz > 2.5 * 2.5 then
        return
    end

    -- The authoritative table is removed before granting the item so two
    -- pickup requests in the same frame cannot duplicate it.
    drops[object] = nil
    local weapon, ammo = data.weapon, data.ammo
    destroyElement(object)
    giveWeapon(player, weapon, ammo, true)
    message(player, ("Picked up weapon %d with %d ammo"):format(weapon, ammo), 120, 255, 160)
end)

-- A player that joins after a drop was created did not see the original RPC.
-- Replaying the idempotent setter when its client resource is ready gives it
-- the same physics state; existing clients are unaffected.
addEvent("physicalDrop:ready", true)
addEventHandler("physicalDrop:ready", resourceRoot, function()
    if not client then
        return
    end

    for object in pairs(drops) do
        if isElement(object) then
            setObjectDynamicPhysics(object, true)
        end
    end

    for object in pairs(comparisonObjects) do
        if isElement(object) then
            setObjectDynamicPhysics(object, true)
        end
    end
end)
