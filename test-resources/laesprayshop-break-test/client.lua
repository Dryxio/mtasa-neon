local SPRAY = {
    name = "laesprayshop",
    model = 5532,
    lodModel = 5527,
    x = 2057.00,
    y = -1830.50,
    z = 20.60,
    rx = 0.0,
    ry = 0.0,
    rz = 0.0,
    removeRadius = 6.0,
    lodRemoveRadius = 80.0,
}

local SCENE = {
    {name = "smallprosjmt_1", model = 3628, x = 2333.40625, y = -1933.96094, z = 15.21875, rx = 0.0, ry = 0.0, rz = 179.999985},
    {name = "smallprosjmt_2", model = 3628, x = 2333.39844, y = -1892.83594, z = 15.25, rx = 0.0, ry = 0.0, rz = 0.0},
    {name = "smallprosjmt_3", model = 3628, x = 2261.46094, y = -1916.03125, z = 15.1875, rx = 0.0, ry = 0.0, rz = 0.0},
    {name = "smallprosjmt_4", model = 3628, x = 2284.70312, y = -1915.875, z = 15.1796875, rx = 0.0, ry = 0.0, rz = 0.0},
    {name = "smallprosjmt_5", model = 3628, x = 2238.21875, y = -1916.10938, z = 15.1875, rx = 0.0, ry = 0.0, rz = 0.0},

    {name = "las2xref_1", model = 3783, x = 2296.5, y = -1878.30469, z = 15.5234375, rx = 0.0, ry = 0.0, rz = 0.0},
    {name = "las2xref_2", model = 3783, x = 2269.20312, y = -1878.30469, z = 15.5234375, rx = 0.0, ry = 0.0, rz = 0.0},
    {name = "las2xref_3", model = 3783, x = 2241.89844, y = -1878.30469, z = 15.5234375, rx = 0.0, ry = 0.0, rz = 0.0},
}

local sprayReplacement
local sprayRemoved = false
local sprayLodRemoved = false
local sceneReplacements = {}
local sceneRemoved = {}

local function log(message, r, g, b)
    outputDebugString("[BREAKSCENE] " .. message)
    outputChatBox("[BREAKSCENE] " .. message, r or 220, g or 220, b or 220)
end

local function makeProfile(object, seed, fragments, health)
    return setObjectBreakProfile(object, {
        native = false,
        health = health or 350,
        instantBreakThreshold = 320,
        fracture = {
            fragments = fragments or 16,
            force = 4.5,
            randomness = 0.9,
            lifetime = 10000,
            gravity = 9.81,
            bounce = 0.25,
            drag = 0.10,
            renderDistance = 500,
            seed = seed,
            hideOriginal = true,
            disableOriginalCollision = true,
        },
    })
end

local function clearObject(object)
    if isElement(object) then
        clearObjectBreakProfile(object)
        destroyElement(object)
    end
end

local function clearSprayReplacement()
    clearObject(sprayReplacement)
    sprayReplacement = nil
end

local function restoreSpray()
    clearSprayReplacement()
    if sprayRemoved then
        restoreWorldModel(SPRAY.model, SPRAY.removeRadius, SPRAY.x, SPRAY.y, SPRAY.z, 0)
        sprayRemoved = false
    end
    if sprayLodRemoved then
        restoreWorldModel(SPRAY.lodModel, SPRAY.lodRemoveRadius, SPRAY.x, SPRAY.y, SPRAY.z, 0)
        sprayLodRemoved = false
    end
end

local function armSpray()
    clearSprayReplacement()

    if not sprayRemoved then
        sprayRemoved = removeWorldModel(SPRAY.model, SPRAY.removeRadius, SPRAY.x, SPRAY.y, SPRAY.z, 0) == true
    end
    if not sprayLodRemoved then
        sprayLodRemoved = removeWorldModel(SPRAY.lodModel, SPRAY.lodRemoveRadius, SPRAY.x, SPRAY.y, SPRAY.z, 0) == true
    end

    sprayReplacement = createObject(SPRAY.model, SPRAY.x, SPRAY.y, SPRAY.z, SPRAY.rx, SPRAY.ry, SPRAY.rz)
    if not isElement(sprayReplacement) then
        log("createObject failed for spray shop", 255, 80, 80)
        return false
    end

    setElementFrozen(sprayReplacement, true)
    if not makeProfile(sprayReplacement, SPRAY.model, 16, 350) then
        log("setObjectBreakProfile failed for spray shop", 255, 80, 80)
        return false
    end

    log("spray shop armed (HD + LOD removed)", 120, 255, 160)
    return true
end

local function restoreScene()
    for _, object in pairs(sceneReplacements) do
        clearObject(object)
    end
    sceneReplacements = {}

    for index, target in ipairs(SCENE) do
        if sceneRemoved[index] then
            restoreWorldModel(target.model, 2.0, target.x, target.y, target.z, 0)
        end
    end
    sceneRemoved = {}
end

local function armScene()
    restoreScene()

    local created = 0
    for index, target in ipairs(SCENE) do
        local removed = removeWorldModel(target.model, 2.0, target.x, target.y, target.z, 0)
        sceneRemoved[index] = removed == true

        local object = createObject(target.model, target.x, target.y, target.z, target.rx, target.ry, target.rz)
        if not isElement(object) then
            log(("create failed: %s model=%d"):format(target.name, target.model), 255, 80, 80)
        else
            setElementFrozen(object, true)
            local fragments = target.model == 3628 and 12 or 10
            if makeProfile(object, 10000 + index, fragments, 320) then
                sceneReplacements[index] = object
                created = created + 1
            else
                destroyElement(object)
                log(("profile failed: %s"):format(target.name), 255, 80, 80)
            end
        end
    end

    log(("scene armed: %d/%d buildings converted"):format(created, #SCENE), created == #SCENE and 120 or 255, created == #SCENE and 255 or 180, 160)
    log("If a ghost building remains after fracture, it is an associated LOD; tell me which instance and I'll add its LOD model.", 120, 200, 255)
    return created == #SCENE
end

addCommandHandler("spraybreak", armSpray)
addCommandHandler("sprayreset", function()
    restoreSpray()
    log("vanilla spray shop + LOD restored", 120, 255, 160)
end)

addCommandHandler("sprayhealth", function()
    if not isElement(sprayReplacement) then
        log("no spray replacement active; use /spraybreak", 255, 180, 100)
        return
    end
    log("spray health=" .. tostring(getObjectBreakHealth(sprayReplacement)), 120, 220, 255)
end)

addCommandHandler("spraytp", function()
    setElementPosition(localPlayer, SPRAY.x + 20.0, SPRAY.y, SPRAY.z - 7.0)
    log("teleported near laesprayshop", 120, 220, 255)
end)

addCommandHandler("breakscene", armScene)
addCommandHandler("breakscenereset", function()
    restoreScene()
    log("hardcoded scene restored", 120, 255, 160)
end)

addCommandHandler("breakscenetp", function()
    setElementPosition(localPlayer, 2310.0, -1907.0, 13.5)
    log("teleported near hardcoded building scene", 120, 220, 255)
end)

addCommandHandler("breakscenehealth", function()
    for index, target in ipairs(SCENE) do
        local object = sceneReplacements[index]
        if isElement(object) then
            log(("%s model=%d health=%s"):format(target.name, target.model, tostring(getObjectBreakHealth(object))), 120, 220, 255)
        end
    end
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    log("ready: /breakscenetp then /breakscene (or /spraytp + /spraybreak)", 120, 220, 255)
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    restoreScene()
    restoreSpray()
end)
