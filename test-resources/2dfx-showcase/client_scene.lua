local S = DFXShowcase
local state = S.state
local PALETTE = S.PALETTE

local SEGMENTS = 6
local EDGE_STATIONS = 30
local CENTER_STATIONS = 24
local MOON_COUNT = 7

function S.findFirstLight(model)
    local count = getModel2DFXCount(model, false)
    for index = 0, count - 1 do
        if getModel2DFXType(model, index) == "light" then
            return index
        end
    end
    return nil
end

function S.captureLightTemplate()
    local index = S.findFirstLight(1226)
    if index == nil then
        return false
    end

    local x, y, z = getModel2DFXPosition(1226, index)
    local coronaName = getModel2DFXProperty(1226, index, "coronaName")
    local shadowName = getModel2DFXProperty(1226, index, "shadowName")
    local drawDistance = getModel2DFXProperty(1226, index, "drawDistance")
    local coronaSize = getModel2DFXProperty(1226, index, "coronaSize")
    local lightRange = getModel2DFXProperty(1226, index, "lightRange")

    if type(x) ~= "number" or type(coronaName) ~= "string" or coronaName == "" then
        return false
    end

    state.lightTemplate = {
        position = { x, y, z },
        coronaName = coronaName,
        shadowName = type(shadowName) == "string" and shadowName or "shad_exp",
        drawDistance = type(drawDistance) == "number" and drawDistance or 180,
        coronaSize = type(coronaSize) == "number" and coronaSize or 1.0,
        lightRange = type(lightRange) == "number" and lightRange or 12,
    }
    S.log(string.format("lamp template corona=%s shadow=%s", state.lightTemplate.coronaName, state.lightTemplate.shadowName))
    return true
end

function S.lightProperties(color, size, range, showMode, coronaName, position)
    local template = state.lightTemplate or {}
    position = position or template.position or { 0, 0, 0.22 }
    return {
        drawDistance = math.max(260, template.drawDistance or 0),
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
        coronaName = coronaName or template.coronaName or "coronastar",
        shadowName = template.shadowName or "shad_exp",
    }, position
end

function S.addCustomLight(model, color, size, range, showMode, coronaName, position)
    local props, effectPosition = S.lightProperties(color, size, range, showMode, coronaName, position)
    if not addModel2DFX(model, effectPosition[1], effectPosition[2], effectPosition[3], "light", props) then
        return false
    end
    S.rememberTouchedModel(model)
    return getModel2DFXCount(model, true) - 1
end

function S.addGroundLight(model, color, size, range, showMode)
    return S.addCustomLight(model, color, size, range, showMode or "default", nil, { 0, 0, 0 })
end

function S.ensureLampLight(model, color)
    local index = S.findFirstLight(model)
    if index ~= nil then
        S.rememberTouchedModel(model)
        return index
    end
    return S.addCustomLight(model, color, 0.04, 0, "default")
end

-- Model 1226 is asymmetric: the native 2DFX position tells us where the lamp
-- head actually is in model space. Rotate each physical row so that this vector
-- points toward the runway centre instead of relying on a guessed +/-90 offset.
function S.lampYawForLateral(lateral)
    local template = state.lightTemplate
    local basis = state.basis
    if not template or not basis then
        return basis and basis.yaw or 270
    end

    local headX = template.position[1] or 0
    local headY = template.position[2] or 0
    if math.abs(headX) + math.abs(headY) < 0.001 then
        return basis.yaw
    end

    local targetX, targetY
    if lateral < 0 then
        targetX, targetY = basis.px, basis.py
    else
        targetX, targetY = -basis.px, -basis.py
    end

    local localAngle = math.deg(math.atan2(headY, headX))
    local targetAngle = math.deg(math.atan2(targetY, targetX))
    local yaw = targetAngle - localAngle
    while yaw < 0 do yaw = yaw + 360 end
    while yaw >= 360 do yaw = yaw - 360 end
    return yaw
end

function S.spawnRunwayLamp(model, t, lateral, height)
    local x, y, z = S.runwayPoint(t, lateral, height or 0)
    return S.spawnWorld(model, x, y, z, S.lampYawForLateral(lateral), 1.0, 255)
end

function S.setLight(model, effect, color, size, range, showMode)
    if not model or effect == nil then
        return
    end
    if color then
        setModel2DFXProperty(model, effect, "color", color)
    end
    if size ~= nil then
        setModel2DFXProperty(model, effect, "coronaSize", size)
    end
    if range ~= nil then
        setModel2DFXProperty(model, effect, "lightRange", range)
    end
    if showMode then
        setModel2DFXProperty(model, effect, "showMode", showMode)
    end
end

function S.setEdgeSegment(side, segment, color, size, range, showMode)
    S.setLight(state.edgeModels[side][segment], state.edgeEffects[side][segment], color, size, range, showMode)
end

function S.setCenterSegment(segment, color, size, range, showMode)
    S.setLight(state.centerModels[segment], state.centerEffects[segment], color, size, range, showMode)
end

function S.setThreshold(index, color, size, range, showMode)
    S.setLight(state.thresholdModels[index], state.thresholdEffects[index], color, size, range, showMode)
end

function S.setMoon(index, color, size, showMode)
    S.setLight(state.moonModels[index], state.moonEffects[index], color, size, 0, showMode or "default")
end

function S.allLightsOff()
    for segment = 1, SEGMENTS do
        S.setEdgeSegment("left", segment, PALETTE[1], 0.03, 0, "default")
        S.setEdgeSegment("right", segment, PALETTE[1], 0.03, 0, "default")
        S.setCenterSegment(segment, PALETTE[6], 0.02, 0, "default")
    end
    S.setThreshold(1, tocolor(40, 255, 150, 255), 0.03, 0, "default")
    S.setThreshold(2, tocolor(255, 65, 90, 255), 0.03, 0, "default")
    for index = 1, MOON_COUNT do
        S.setMoon(index, PALETTE[((index - 1) % #PALETTE) + 1], 0.03, "default")
    end
end

function S.applyFinalLights(step)
    step = step or 0
    for segment = 1, SEGMENTS do
        local leftColor = PALETTE[((segment + step - 2) % #PALETTE) + 1]
        local rightColor = PALETTE[((#PALETTE - segment + step) % #PALETTE) + 1]
        S.setEdgeSegment("left", segment, leftColor, 1.55, 18, "default")
        S.setEdgeSegment("right", segment, rightColor, 1.55, 18, "default")
        S.setCenterSegment(segment, PALETTE[6], 0.62, 0, "default")
    end
    S.setThreshold(1, tocolor(45, 255, 150, 255), 1.20, 0, "default")
    S.setThreshold(2, tocolor(255, 55, 85, 255), 1.20, 0, "default")
end

function S.buildRuntimeModels()
    if not S.captureLightTemplate() then
        return false, "could not read model 1226 native light template"
    end

    for segment = 1, SEGMENTS do
        state.edgeModels.left[segment] = S.requestObjectModel(1226)
        state.edgeModels.right[segment] = S.requestObjectModel(1226)
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

    for index = 1, MOON_COUNT do
        state.moonModels[index] = S.requestObjectModel(1337)
        if not state.moonModels[index] then
            return false, "could not allocate moon model " .. index
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
        if not S.spawnRunwayLamp(state.edgeModels.left[segment], t, -S.RUNWAY_HALF_WIDTH - 2.0, 0) or
           not S.spawnRunwayLamp(state.edgeModels.right[segment], t, S.RUNWAY_HALF_WIDTH + 2.0, 0) then
            return false, "could not create runway lamp posts"
        end
    end

    -- Ground carriers sit only a few centimetres above the asphalt. Their 2DFX
    -- position is local zero, so the corona itself is at runway level rather
    -- than inheriting the several-metre-high lamp-head offset from model 1226.
    for index = 1, CENTER_STATIONS do
        local segment = segmentForStation(index, CENTER_STATIONS)
        local t = 0.07 + ((index - 1) / (CENTER_STATIONS - 1)) * 0.86
        if not S.spawnOnRunway(state.centerModels[segment], t, 0, 0.035, 0, 0.06) then
            return false, "could not create runway center anchors"
        end
    end

    for side = -4, 4 do
        local lateral = side * 4.0
        if not S.spawnOnRunway(state.thresholdModels[1], 0.035, lateral, 0.035, 0, 0.06) or
           not S.spawnOnRunway(state.thresholdModels[2], 0.965, lateral, 0.035, 0, 0.06) then
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

    local moonLayout = {
        { 0.62, -58, 24 }, { 0.67, -40, 35 }, { 0.72, -21, 43 },
        { 0.76,   0, 48 }, { 0.80,  21, 43 }, { 0.85,  40, 35 }, { 0.90, 58, 24 },
    }
    for index, moon in ipairs(moonLayout) do
        if not S.spawnOnRunway(state.moonModels[index], moon[1], moon[2], moon[3], 0, 0.05) then
            return false, "could not create moon sky anchor"
        end
    end

    state.vanillaObject = S.spawnRunwayLamp(state.vanillaModel, 0.53, -27.0, 0)
    if not state.vanillaObject then
        return false, "could not create hero vanilla lamp"
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

function S.captureVanillaLight()
    local index = S.findFirstLight(state.vanillaModel)
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
    S.rememberTouchedModel(state.vanillaModel)
    return true
end

function S.addEffects()
    for segment = 1, SEGMENTS do
        state.edgeEffects.left[segment] = S.ensureLampLight(state.edgeModels.left[segment], PALETTE[segment])
        state.edgeEffects.right[segment] = S.ensureLampLight(state.edgeModels.right[segment], PALETTE[7 - segment])
        state.centerEffects[segment] = S.addGroundLight(state.centerModels[segment], PALETTE[6], 0.02, 0)
        if state.edgeEffects.left[segment] == false or state.edgeEffects.right[segment] == false or state.centerEffects[segment] == false then
            return false, "failed to prepare runway light segment " .. segment
        end
    end

    state.thresholdEffects[1] = S.addGroundLight(state.thresholdModels[1], tocolor(45, 255, 150, 255), 0.03, 0)
    state.thresholdEffects[2] = S.addGroundLight(state.thresholdModels[2], tocolor(255, 55, 85, 255), 0.03, 0)
    if state.thresholdEffects[1] == false or state.thresholdEffects[2] == false then
        return false, "failed to add threshold lights"
    end

    for index = 1, MOON_COUNT do
        local color = PALETTE[((index - 1) % #PALETTE) + 1]
        state.moonEffects[index] = S.addCustomLight(state.moonModels[index], color, 0.03, 0, "default", "coronamoon", { 0, 0, 0 })
        if state.moonEffects[index] == false then
            return false, "failed to add moon sprite " .. index
        end
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

    -- A requested 1226 model normally carries the native lamp effect. If this
    -- build does not clone native 2DFX, the hero shot is skipped instead of
    -- touching global model 1226 and affecting unrelated world lamps.
    S.captureVanillaLight()

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