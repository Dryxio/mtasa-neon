local S = DFXShowcase
local state = S.state
local PALETTE = S.PALETTE

function S.customLightProperties(color)
    return {
        drawDistance = 170,
        lightRange = 0,
        coronaSize = 0.05,
        shadowSize = 8,
        shadowMultiplier = 45,
        showMode = "default",
        coronaReflection = false,
        flareType = 0,
        flags = { atNight = true, checkObstacles = true },
        shadowDistance = 0,
        offset = { 0, 0, 0 },
        color = color,
        coronaName = "coronamoon",
        shadowName = "shad_exp",
    }
end

function S.findFirstLight(model)
    local count = getModel2DFXCount(model, false)
    for index = 0, count - 1 do
        if getModel2DFXType(model, index) == "light" then
            return index
        end
    end
    return nil
end

function S.neutralizeNativeLights(model)
    local count = getModel2DFXCount(model, false)
    local firstPosition = nil
    for index = 0, count - 1 do
        if getModel2DFXType(model, index) == "light" then
            if not firstPosition then
                local x, y, z = getModel2DFXPosition(model, index)
                if type(x) == "number" then
                    firstPosition = { x, y, z }
                end
            end
            setModel2DFXProperty(model, index, "coronaSize", 0.05)
            setModel2DFXProperty(model, index, "lightRange", 0)
            setModel2DFXProperty(model, index, "shadowSize", 0)
            S.rememberTouchedModel(model)
        end
    end
    return firstPosition or { 0, 0, 5.4 }
end

function S.addCustomLight(model, color)
    local position = S.neutralizeNativeLights(model)
    if not addModel2DFX(model, position[1], position[2], position[3], "light", S.customLightProperties(color)) then
        return false
    end
    S.rememberTouchedModel(model)
    return getModel2DFXCount(model, true) - 1
end

function S.setLight(model, effect, color, size, range)
    if not model or effect == nil then
        return
    end
    setModel2DFXProperty(model, effect, "color", color)
    setModel2DFXProperty(model, effect, "coronaSize", size)
    setModel2DFXProperty(model, effect, "lightRange", range)
end

function S.setLampRowsOff()
    for row, model in ipairs(state.lampModels) do
        S.setLight(model, state.lampEffects[row], PALETTE[row], 0.05, 0)
    end
    if state.heroModel and state.heroEffect then
        S.setLight(state.heroModel, state.heroEffect, PALETTE[1], 0.08, 0)
    end
end

function S.activateLampRow(row)
    local model = state.lampModels[row]
    local effect = state.lampEffects[row]
    if model and effect then
        S.setLight(model, effect, PALETTE[row], 1.65, 23)
    end
end

function S.applyAllLights()
    for row = 1, #state.lampModels do
        S.activateLampRow(row)
    end
    if state.heroModel and state.heroEffect then
        S.setLight(state.heroModel, state.heroEffect, tocolor(80, 245, 255, 255), 2.35, 30)
    end
end

function S.buildRuntimeModels()
    for row = 1, 6 do
        local model = S.requestObjectModel(1226)
        if not model then
            return false, "could not allocate lamp model " .. row
        end
        state.lampModels[row] = model
    end

    state.heroModel = S.requestObjectModel(1226)
    if not state.heroModel then
        return false, "could not allocate hero lamp model"
    end

    for i = 1, 2 do
        local model = S.requestObjectModel(1337)
        if not model then
            return false, "could not allocate particle model " .. i
        end
        state.particleModels[i] = model
    end

    for i = 1, 3 do
        local model = S.requestObjectModel(1337)
        if not model then
            return false, "could not allocate roadsign model " .. i
        end
        state.roadsignModels[i] = model
    end

    for i = 1, 2 do
        local model = S.requestObjectModel(1337)
        if not model then
            return false, "could not allocate escalator model " .. i
        end
        state.escalatorModels[i] = model
    end

    state.sunModel = S.requestObjectModel(1337)
    if not state.sunModel then
        return false, "could not allocate sun glare model"
    end

    local vanillaClone = S.requestObjectModel(1226)
    if vanillaClone then
        state.vanillaModel = vanillaClone
    else
        state.vanillaModel = 1226
    end
    return true
end

function S.buildDecor()
    for row, model in ipairs(state.lampModels) do
        local y = -18 + (row - 1) * 7.0
        if not S.spawn(model, -7.0, y, 0, 0) or not S.spawn(model, 7.0, y, 0, 180) then
            return false, "could not create boulevard lamps"
        end
    end

    if not S.spawn(state.heroModel, 0, 6.0, 0, 0) then
        return false, "could not create hero lamp"
    end

    if not S.spawn(state.particleModels[1], -11.0, 14.0, 5.0, 0, 1, 1) or
       not S.spawn(state.particleModels[2], 11.0, 16.0, 5.5, 0, 1, 1) then
        return false, "could not create particle anchors"
    end

    S.spawn(970, 0, 27.8, 1.4, 0, 1.8)
    S.spawn(970, -8.0, 24.8, 1.2, 0, 1.2)
    S.spawn(970, 8.0, 24.8, 1.2, 0, 1.2)
    if not S.spawn(state.roadsignModels[1], 0, 27.5, 0, 0, 1, 1) or
       not S.spawn(state.roadsignModels[2], -8.0, 24.5, 0, 0, 1, 1) or
       not S.spawn(state.roadsignModels[3], 8.0, 24.5, 0, 0, 1, 1) then
        return false, "could not create roadsign anchors"
    end

    S.spawn(980, 0, 41.0, 2.5, 0, 1.35)
    S.spawn(970, -7.2, 34.0, 1.0, 90, 1.4)
    S.spawn(970, 7.2, 34.0, 1.0, 90, 1.4)
    if not S.spawn(state.escalatorModels[1], -5.0, 27.5, 0, 0, 1, 1) or
       not S.spawn(state.escalatorModels[2], 5.0, 27.5, 0, 0, 1, 1) then
        return false, "could not create escalator anchors"
    end

    if not S.spawn(state.sunModel, 0, 48.0, 7.0, 0, 1, 1) then
        return false, "could not create sun glare anchor"
    end

    state.vanillaObject = S.spawn(state.vanillaModel, 12.0, 3.0, 0, 0)
    if not state.vanillaObject then
        return false, "could not create vanilla light object"
    end

    S.spawn(1280, -3.5, -6.0, 0, 90)
    S.spawn(1280, 3.5, -6.0, 0, 270)
    S.spawn(970, -10.0, 3.0, 0.8, 90, 1.4)
    S.spawn(970, 10.0, 3.0, 0.8, 90, 1.4)
    return true
end

function S.addRoadsign(model, position, size, lines, text)
    local props = {
        size = size,
        rotation = { 90, 0, 0 },
        flags = { lines = lines, charactersPerLine = 16 },
        color = tocolor(80, 225, 255, 255),
        text1 = text[1] or "",
        text2 = text[2] or "",
        text3 = text[3] or "",
        text4 = text[4] or "",
    }
    if not addModel2DFX(model, position[1], position[2], position[3], "roadsign", props) then
        return false
    end
    S.rememberTouchedModel(model)
    return true
end

function S.addEscalator(model, direction)
    local props = {
        bottom = { 0, 0, 0.25 },
        top = { 0, 8.5, 4.6 },
        ["end"] = { 0, 12.0, 4.6 },
        direction = direction,
    }
    if not addModel2DFX(model, 0, -3.5, 0.25, "escalator", props) then
        return false
    end
    S.rememberTouchedModel(model)
    return true
end

function S.captureVanillaLight()
    local index = S.findFirstLight(state.vanillaModel)
    if index == nil and state.vanillaModel ~= 1226 then
        state.vanillaModel = 1226
        if isElement(state.vanillaObject) then
            setElementModel(state.vanillaObject, 1226)
        end
        index = S.findFirstLight(1226)
    end
    if index == nil then
        return false
    end

    state.vanillaEffect = index
    local drawDistance = getModel2DFXProperty(state.vanillaModel, index, "drawDistance")
    local coronaSize = getModel2DFXProperty(state.vanillaModel, index, "coronaSize")
    local lightRange = getModel2DFXProperty(state.vanillaModel, index, "lightRange")
    local r, g, b, a = getModel2DFXProperty(state.vanillaModel, index, "color")
    if type(drawDistance) ~= "number" or type(coronaSize) ~= "number" or
       type(lightRange) ~= "number" or type(r) ~= "number" then
        return false
    end

    state.vanillaBaseline = {
        drawDistance = drawDistance,
        coronaSize = coronaSize,
        lightRange = lightRange,
        color = tocolor(r, g, b, a),
    }
    return true
end

function S.addEffects()
    for row, model in ipairs(state.lampModels) do
        local index = S.addCustomLight(model, PALETTE[row])
        if index == false then
            return false, "failed to add custom light row " .. row
        end
        state.lampEffects[row] = index
    end

    state.heroEffect = S.addCustomLight(state.heroModel, PALETTE[1])
    if state.heroEffect == false then
        return false, "failed to add hero light"
    end

    if not addModel2DFX(state.particleModels[1], 0, 0, 0.3, "particle", { name = "fire" }) then
        return false, "failed to add fire particle"
    end
    S.rememberTouchedModel(state.particleModels[1])

    if not addModel2DFX(state.particleModels[2], 0, 0, 0.3, "particle", { name = "smoke_flare" }) then
        return false, "failed to add smoke particle"
    end
    S.rememberTouchedModel(state.particleModels[2])

    if not S.addRoadsign(state.roadsignModels[1], { 0, 0, 3.3 }, { 5.5, 2.8 }, 4,
        { "NEON_BOULEVARD", "NATIVE_GTA_2DFX", "SCRIPTED_IN_LUA", "NO_CUSTOM_ASSETS" }) then
        return false, "failed to add main roadsign"
    end

    if not S.addRoadsign(state.roadsignModels[2], { 0, 0, 2.6 }, { 3.2, 1.0 }, 1,
        { "LIGHTS_FROM_LUA", "", "", "" }) then
        return false, "failed to add left roadsign"
    end

    if not S.addRoadsign(state.roadsignModels[3], { 0, 0, 2.6 }, { 3.2, 1.0 }, 1,
        { "NATIVE_2DFX", "", "", "" }) then
        return false, "failed to add right roadsign"
    end

    if not S.addEscalator(state.escalatorModels[1], 0) or
       not S.addEscalator(state.escalatorModels[2], 1) then
        return false, "failed to add escalators"
    end

    for step = 0, 7 do
        local angle = math.rad(step * 45)
        local x = math.cos(angle) * 12.0
        local y = math.sin(angle) * 12.0
        local z = (step % 2 == 0) and 3.0 or 6.0
        if not addModel2DFX(state.sunModel, x, y, z, "sun_glare", {}) then
            return false, "failed to add sun glare"
        end
    end
    S.rememberTouchedModel(state.sunModel)

    if not S.captureVanillaLight() then
        return false, "could not find vanilla model 1226 light 2DFX"
    end

    S.setLampRowsOff()
    return true
end

function S.mutateVanillaLight()
    if state.vanillaEffect == nil or not state.vanillaBaseline then
        return
    end
    local model = state.vanillaModel
    local index = state.vanillaEffect
    setModel2DFXProperty(model, index, "drawDistance", state.vanillaBaseline.drawDistance + 120)
    setModel2DFXProperty(model, index, "coronaSize", math.max(2.8, state.vanillaBaseline.coronaSize * 2.5))
    setModel2DFXProperty(model, index, "lightRange", state.vanillaBaseline.lightRange + 20)
    setModel2DFXProperty(model, index, "color", tocolor(35, 240, 255, 255))
    S.rememberTouchedModel(model)
end

function S.restoreVanillaLight()
    if state.vanillaEffect == nil then
        return
    end
    local model = state.vanillaModel
    local index = state.vanillaEffect
    resetModel2DFXProperty(model, index, "drawDistance")
    resetModel2DFXProperty(model, index, "coronaSize")
    resetModel2DFXProperty(model, index, "lightRange")
    resetModel2DFXProperty(model, index, "color")
end
