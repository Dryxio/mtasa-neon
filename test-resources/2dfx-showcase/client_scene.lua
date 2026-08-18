local S = DFXShowcase
local state = S.state
local PALETTE = S.PALETTE

local SEGMENTS = 6
local EDGE_STATIONS = 30
local CENTER_STATIONS = 24

function S.lightProperties(color, size, range, showMode)
    return {
        drawDistance = 260,
        lightRange = range or 0,
        coronaSize = size or 0.04,
        shadowSize = 0,
        shadowMultiplier = 0,
        showMode = showMode or "default",
        coronaReflection = false,
        flareType = 0,
        flags = { atNight = true, checkObstacles = false },
        shadowDistance = 0,
        offset = { 0, 0, 0 },
        color = color,
        coronaName = "coronamoon",
        shadowName = "shad_exp",
    }
end

function S.addLight(model, color, size, range, showMode)
    if not addModel2DFX(model, 0, 0, 0.22, "light", S.lightProperties(color, size, range, showMode)) then
        return false
    end
    S.rememberTouchedModel(model)
    return getModel2DFXCount(model, true) - 1
end

function S.setLight(model, effect, color, size, range, showMode)
    if not model or effect == nil then
        return
    end
    if color then
        setModel2DFXProperty(model, effect, "color", color)
    end
    if size then
        setModel2DFXProperty(model, effect, "coronaSize", size)
    end
    if range then
        setModel2DFXProperty(model, effect, "lightRange", range)
    end
    if showMode then
        setModel2DFXProperty(model, effect, "showMode", showMode)
    end
end

function S.setEdgeSegment(side, segment, color, size, range, showMode)
    local model = state.edgeModels[side][segment]
    local effect = state.edgeEffects[side][segment]
    S.setLight(model, effect, color, size, range, showMode)
end

function S.setCenterSegment(segment, color, size, range, showMode)
    S.setLight(state.centerModels[segment], state.centerEffects[segment], color, size, range, showMode)
end

function S.setThreshold(index, color, size, range, showMode)
    S.setLight(state.thresholdModels[index], state.thresholdEffects[index], color, size, range, showMode)
end

function S.allLightsOff()
    for segment = 1, SEGMENTS do
        S.setEdgeSegment("left", segment, PALETTE[1], 0.035, 0, "default")
        S.setEdgeSegment("right", segment, PALETTE[1], 0.035, 0, "default")
        S.setCenterSegment(segment, PALETTE[6], 0.025, 0, "default")
    end
    S.setThreshold(1, tocolor(40, 255, 150, 255), 0.035, 0, "default")
    S.setThreshold(2, tocolor(255, 65, 90, 255), 0.035, 0, "default")
end

function S.applyFinalLights(step)
    step = step or 0
    for segment = 1, SEGMENTS do
        local leftColor = PALETTE[((segment + step - 2) % #PALETTE) + 1]
        local rightColor = PALETTE[((#PALETTE - segment + step) % #PALETTE) + 1]
        S.setEdgeSegment("left", segment, leftColor, 1.55, 4, "default")
        S.setEdgeSegment("right", segment, rightColor, 1.55, 4, "default")
        S.setCenterSegment(segment, PALETTE[6], 0.72, 0, "default")
    end
    S.setThreshold(1, tocolor(45, 255, 150, 255), 1.25, 0, "default")
    S.setThreshold(2, tocolor(255, 55, 85, 255), 1.25, 0, "default")
end

function S.buildRuntimeModels()
    for segment = 1, SEGMENTS do
        state.edgeModels.left[segment] = S.requestObjectModel(1337)
        state.edgeModels.right[segment] = S.requestObjectModel(1337)
        state.centerModels[segment] = S.requestObjectModel(1337)
        if not state.edgeModels.left[segment] or not state.edgeModels.right[segment] or not state.centerModels[segment] then
            return false, "could not allocate runway light model group " .. segment
        end
    end

    for index = 1, 2 do
        state.thresholdModels[index] = S.requestObjectModel(1337)
        state.particleModels[index] = S.requestObjectModel(1337)
        state.roadsignModels[index] = S.requestObjectModel(1337)
        if not state.thresholdModels[index] or not state.particleModels[index] or not state.roadsignModels[index] then
            return false, "could not allocate showcase model group " .. index
        end
    end

    state.sunModel = S.requestObjectModel(1337)
    if not state.sunModel then
        return false, "could not allocate sun-glare model"
    end

    state.vanillaModel = S.requestObjectModel(1226)
    if not state.vanillaModel then
        state.vanillaModel = 1226
    end

    return true
end

local function segmentForStation(index, count)
    return math.min(SEGMENTS, math.floor((index - 1) * SEGMENTS / count) + 1)
end

function S.buildRunwayAnchors()
    for index = 1, EDGE_STATIONS do
        local segment = segmentForStation(index, EDGE_STATIONS)
        local t = 0.045 + ((index - 1) / (EDGE_STATIONS - 1)) * 0.91
        if not S.spawnOnRunway(state.edgeModels.left[segment], t, -S.RUNWAY_HALF_WIDTH, 0.12, 0, 0.08) or
           not S.spawnOnRunway(state.edgeModels.right[segment], t, S.RUNWAY_HALF_WIDTH, 0.12, 0, 0.08) then
            return false, "could not create runway edge anchors"
        end
    end

    for index = 1, CENTER_STATIONS do
        local segment = segmentForStation(index, CENTER_STATIONS)
        local t = 0.07 + ((index - 1) / (CENTER_STATIONS - 1)) * 0.86
        if not S.spawnOnRunway(state.centerModels[segment], t, 0, 0.10, 0, 0.06) then
            return false, "could not create runway center anchors"
        end
    end

    for side = -4, 4 do
        local lateral = side * 4.0
        if not S.spawnOnRunway(state.thresholdModels[1], 0.035, lateral, 0.12, 0, 0.06) or
           not S.spawnOnRunway(state.thresholdModels[2], 0.965, lateral, 0.12, 0, 0.06) then
            return false, "could not create runway threshold anchors"
        end
    end

    for _, lateral in ipairs({ -10, -5, 5, 10 }) do
        if not S.spawnOnRunway(state.particleModels[1], 0.885, lateral, 0.25, 0, 0.05) then
            return false, "could not create smoke-flare anchors"
        end
    end
    for _, lateral in ipairs({ -13, 13 }) do
        if not S.spawnOnRunway(state.particleModels[2], 0.92, lateral, 0.15, 0, 0.05) then
            return false, "could not create fire anchors"
        end
    end

    if not S.spawnOnRunway(state.roadsignModels[1], 0.10, 0, 0.05, 0, 0.05) or
       not S.spawnOnRunway(state.roadsignModels[2], 0.90, 0, 0.05, 0, 0.05) then
        return false, "could not create roadsign anchors"
    end

    if not S.spawnOnRunway(state.sunModel, 0.94, 0, 4.0, 0, 0.05) then
        return false, "could not create sun-glare anchor"
    end

    state.vanillaObject = S.spawnOnRunway(state.vanillaModel, 0.53, -24.0, 0, 255, 1.0)
    if not state.vanillaObject then
        return false, "could not create vanilla lamp"
    end

    return true
end

function S.addRoadsign(model, text, reverse)
    local props = {
        size = { 5.5, 1.9 },
        rotation = { 90, 0, reverse and 270 or 90 },
        flags = { lines = 2, charactersPerLine = 16 },
        color = tocolor(80, 230, 255, 255),
        text1 = text[1] or "",
        text2 = text[2] or "",
        text3 = "",
        text4 = "",
    }
    if not addModel2DFX(model, 0, 0, 3.0, "roadsign", props) then
        return false
    end
    S.rememberTouchedModel(model)
    return true
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
    if type(drawDistance) ~= "number" or type(coronaSize) ~= "number" or type(lightRange) ~= "number" or type(r) ~= "number" then
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
    for segment = 1, SEGMENTS do
        state.edgeEffects.left[segment] = S.addLight(state.edgeModels.left[segment], PALETTE[segment], 0.035, 0)
        state.edgeEffects.right[segment] = S.addLight(state.edgeModels.right[segment], PALETTE[7 - segment], 0.035, 0)
        state.centerEffects[segment] = S.addLight(state.centerModels[segment], PALETTE[6], 0.025, 0)
        if state.edgeEffects.left[segment] == false or state.edgeEffects.right[segment] == false or state.centerEffects[segment] == false then
            return false, "failed to add runway light segment " .. segment
        end
    end

    state.thresholdEffects[1] = S.addLight(state.thresholdModels[1], tocolor(45, 255, 150, 255), 0.035, 0)
    state.thresholdEffects[2] = S.addLight(state.thresholdModels[2], tocolor(255, 55, 85, 255), 0.035, 0)
    if state.thresholdEffects[1] == false or state.thresholdEffects[2] == false then
        return false, "failed to add threshold lights"
    end

    if not addModel2DFX(state.particleModels[1], 0, 0, 0.2, "particle", { name = "smoke_flare" }) then
        return false, "failed to add smoke_flare particle"
    end
    S.rememberTouchedModel(state.particleModels[1])

    if not addModel2DFX(state.particleModels[2], 0, 0, 0.2, "particle", { name = "fire" }) then
        return false, "failed to add fire particle"
    end
    S.rememberTouchedModel(state.particleModels[2])

    if not S.addRoadsign(state.roadsignModels[1], { "NATIVE_GTA_2DFX", "SCRIPTED_IN_LUA" }, false) or
       not S.addRoadsign(state.roadsignModels[2], { "NO_SHADERS", "NO_CUSTOM_ASSETS" }, true) then
        return false, "failed to add runway roadsigns"
    end

    for step = 0, 5 do
        local angle = math.rad(step * 60)
        if not addModel2DFX(state.sunModel, math.cos(angle) * 5.0, math.sin(angle) * 5.0, 3.0 + (step % 2) * 2.0, "sun_glare", {}) then
            return false, "failed to add sun glare"
        end
    end
    S.rememberTouchedModel(state.sunModel)

    if not S.captureVanillaLight() then
        return false, "could not find vanilla model 1226 light 2DFX"
    end

    S.allLightsOff()
    return true
end

function S.mutateVanillaLight()
    if state.vanillaEffect == nil or not state.vanillaBaseline then
        return
    end
    local model = state.vanillaModel
    local index = state.vanillaEffect
    setModel2DFXProperty(model, index, "drawDistance", state.vanillaBaseline.drawDistance + 180)
    setModel2DFXProperty(model, index, "coronaSize", math.max(3.2, state.vanillaBaseline.coronaSize * 3.0))
    setModel2DFXProperty(model, index, "lightRange", state.vanillaBaseline.lightRange + 28)
    setModel2DFXProperty(model, index, "color", tocolor(30, 245, 255, 255))
    setModel2DFXProperty(model, index, "showMode", "warnlight")
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
    resetModel2DFXProperty(model, index, "showMode")
end
