local MODEL = 5532
local LOD_MODEL = 5527
local BASE = {x = 2057.00, y = -1830.50, z = 20.60, rx = 0.0, ry = 0.0, rz = 0.0}
-- The associated LOD is centered noticeably away from the HD building origin.
-- A wider removal radius around the HD origin catches that exact LOD instance too.
local REMOVE_RADIUS = 6.0
local LOD_REMOVE_RADIUS = 80.0

local replacement
local removed = false
local lodRemoved = false

local function log(message, r, g, b)
    outputDebugString("[SPRAYBREAK] " .. message)
    outputChatBox("[SPRAYBREAK] " .. message, r or 220, g or 220, b or 220)
end

local function clearReplacement()
    if isElement(replacement) then
        clearObjectBreakProfile(replacement)
        destroyElement(replacement)
    end
    replacement = nil
end

local function restoreVanilla()
    clearReplacement()
    if removed then
        restoreWorldModel(MODEL, REMOVE_RADIUS, BASE.x, BASE.y, BASE.z, 0)
        removed = false
    end
    if lodRemoved then
        restoreWorldModel(LOD_MODEL, LOD_REMOVE_RADIUS, BASE.x, BASE.y, BASE.z, 0)
        lodRemoved = false
    end
end

local function armReplacement()
    clearReplacement()

    if not removed then
        local ok = removeWorldModel(MODEL, REMOVE_RADIUS, BASE.x, BASE.y, BASE.z, 0)
        removed = ok == true
        log("remove HD model 5532 => " .. tostring(ok), ok and 120 or 255, ok and 255 or 120, 160)
    end

    if not lodRemoved then
        local ok = removeWorldModel(LOD_MODEL, LOD_REMOVE_RADIUS, BASE.x, BASE.y, BASE.z, 0)
        lodRemoved = ok == true
        log("remove LOD model 5527 => " .. tostring(ok), ok and 120 or 255, ok and 255 or 120, 160)
    end

    replacement = createObject(MODEL, BASE.x, BASE.y, BASE.z, BASE.rx, BASE.ry, BASE.rz)
    if not isElement(replacement) then
        log("createObject failed for model 5532", 255, 80, 80)
        return false
    end

    setElementFrozen(replacement, true)

    local profileOk = setObjectBreakProfile(replacement, {
        native = false,
        health = 350,
        instantBreakThreshold = 320,
        fracture = {
            fragments = 16,
            force = 4.5,
            randomness = 0.9,
            lifetime = 10000,
            gravity = 9.81,
            bounce = 0.25,
            drag = 0.10,
            renderDistance = 500,
            seed = 5532,
            hideOriginal = true,
            disableOriginalCollision = true,
        },
    })

    if not profileOk then
        log("setObjectBreakProfile failed", 255, 80, 80)
        return false
    end

    log(("replacement armed: model=%d pos=%.2f %.2f %.2f rot=%.1f %.1f %.1f"):format(
        MODEL, BASE.x, BASE.y, BASE.z, BASE.rx, BASE.ry, BASE.rz
    ), 120, 255, 160)
    log("HD + LOD removed. Test with bullets / grenade / rocket. /sprayhealth shows managed health.", 120, 200, 255)
    return true
end

addCommandHandler("spraybreak", armReplacement)

addCommandHandler("sprayreset", function()
    restoreVanilla()
    log("vanilla laesprayshop + LOD restored", 120, 255, 160)
end)

addCommandHandler("sprayhealth", function()
    if not isElement(replacement) then
        log("no replacement active; use /spraybreak", 255, 180, 100)
        return
    end
    log("health=" .. tostring(getObjectBreakHealth(replacement)), 120, 220, 255)
end)

addCommandHandler("sprayrz", function(_, value)
    local rz = tonumber(value)
    if not rz then
        log("usage: /sprayrz <degrees>", 255, 180, 100)
        return
    end
    BASE.rz = rz
    if isElement(replacement) then
        setElementRotation(replacement, BASE.rx, BASE.ry, BASE.rz)
    end
    log("rotation Z => " .. tostring(BASE.rz), 120, 220, 255)
end)

addCommandHandler("spraypos", function(_, x, y, z)
    x, y, z = tonumber(x), tonumber(y), tonumber(z)
    if not x or not y or not z then
        log("usage: /spraypos <x> <y> <z>", 255, 180, 100)
        return
    end
    BASE.x, BASE.y, BASE.z = x, y, z
    if isElement(replacement) then
        setElementPosition(replacement, x, y, z)
    end
    log(("position => %.3f %.3f %.3f"):format(x, y, z), 120, 220, 255)
end)

addCommandHandler("spraytp", function()
    setElementPosition(localPlayer, BASE.x + 20.0, BASE.y, BASE.z - 7.0)
    log("teleported near laesprayshop", 120, 220, 255)
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    log("ready: /spraytp then /spraybreak", 120, 220, 255)
end)

addEventHandler("onClientResourceStop", resourceRoot, restoreVanilla)
