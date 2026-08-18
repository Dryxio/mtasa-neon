local S = DFXShowcase
local state = S.state
local PALETTE = S.PALETTE

local SEGMENTS = 6

function S.cue(key, condition, callback)
    if condition and not state.cues[key] then
        state.cues[key] = true
        callback()
    end
end

function S.updateChase(t)
    if t < 10500 or t >= 17000 then
        return
    end

    local step = math.floor((t - 10500) / 320)
    if step == state.chaseStep then
        return
    end
    state.chaseStep = step

    for segment = 1, SEGMENTS do
        local phase = (segment + step) % #PALETTE
        local leftColor = PALETTE[phase + 1]
        local rightColor = PALETTE[((#PALETTE - phase) % #PALETTE) + 1]
        local pulse = ((segment + step) % SEGMENTS == 0)
        S.setEdgeSegment("left", segment, leftColor, pulse and 2.15 or 1.35, pulse and 10 or 2, "default")
        S.setEdgeSegment("right", segment, rightColor, pulse and 2.15 or 1.35, pulse and 10 or 2, "default")
        S.setCenterSegment(segment, PALETTE[6], pulse and 1.15 or 0.58, 0, "default")
    end
end

function S.updateFinal(t)
    if t < 32000 then
        return
    end

    local step = math.floor((t - 32000) / 430)
    if step == state.finalStep then
        return
    end
    state.finalStep = step
    S.applyFinalLights(step)

    local hot = (step % 2 == 0)
    for segment = 1, SEGMENTS do
        if ((segment + step) % 3) == 0 then
            S.setCenterSegment(segment, PALETTE[6], hot and 1.35 or 0.72, 0, hot and "warnlight" or "default")
        end
    end
end

function S.updateSceneCues(t)
    S.cue("night", t >= 3500, function()
        setTime(23, 0)
        setWeather(0)
    end)

    for order = 1, SEGMENTS do
        local orderCopy = order
        local segment = SEGMENTS - order + 1
        S.cue("edge_segment_" .. segment, t >= 3900 + (orderCopy - 1) * 480, function()
            S.setEdgeSegment("left", segment, PALETTE[segment], 1.55, 3, "default")
            S.setEdgeSegment("right", segment, PALETTE[7 - segment], 1.55, 3, "default")
        end)
    end

    S.cue("thresholds", t >= 6900, function()
        S.setThreshold(1, tocolor(45, 255, 150, 255), 1.25, 0, "default")
        S.setThreshold(2, tocolor(255, 55, 85, 255), 1.25, 0, "default")
    end)

    for segment = 1, SEGMENTS do
        local segmentCopy = segment
        S.cue("center_segment_" .. segmentCopy, t >= 7600 + (segmentCopy - 1) * 330, function()
            S.setCenterSegment(segmentCopy, PALETTE[6], 0.72, 0, "default")
        end)
    end

    S.cue("center_blink", t >= 11200, function()
        for segment = 1, SEGMENTS do
            S.setCenterSegment(segment, PALETTE[6], 0.85, 0, "on_off_at_5")
        end
    end)

    S.cue("center_normal", t >= 15000, function()
        for segment = 1, SEGMENTS do
            S.setCenterSegment(segment, PALETTE[6], 0.72, 0, "default")
        end
    end)

    S.cue("vanilla_mutate", t >= 17300, S.mutateVanillaLight)
    S.cue("vanilla_restore", t >= 20800, S.restoreVanillaLight)

    S.updateChase(t)
    S.updateFinal(t)
end

function S.setCameraForTimeline(t)
    if t < 3500 then
        S.cameraLerp(
            S.cameraPoint(-0.055, -24, 7.5, 0.72, 0, 3.5, 72),
            S.cameraPoint(-0.015, 9, 4.5, 0.88, 0, 4.0, 64),
            t / 3500
        )
    elseif t < 10500 then
        S.cameraLerp(
            S.cameraPoint(-0.035, 0, 2.2, 0.88, 0, 1.4, 78),
            S.cameraPoint(0.17, -12, 4.8, 0.94, 0, 1.5, 68),
            (t - 3500) / 7000
        )
    elseif t < 17000 then
        S.cameraLerp(
            S.cameraPoint(0.20, -30, 9.5, 0.58, 0, 1.4, 72),
            S.cameraPoint(0.60, 29, 8.0, 0.82, 0, 1.2, 70),
            (t - 10500) / 6500
        )
    elseif t < 22000 then
        S.cameraLerp(
            S.cameraPoint(0.49, -31, 4.0, 0.53, -24, 5.5, 55),
            S.cameraPoint(0.56, -28, 6.0, 0.53, -24, 5.5, 48),
            (t - 17000) / 5000
        )
    elseif t < 27000 then
        S.cameraLerp(
            S.cameraPoint(0.70, -24, 7.5, 0.90, 0, 2.5, 66),
            S.cameraPoint(0.82, 19, 5.5, 0.93, 0, 2.0, 60),
            (t - 22000) / 5000
        )
    elseif t < 32000 then
        S.cameraLerp(
            S.cameraPoint(-0.005, -12, 3.4, 0.10, 0, 3.0, 58),
            S.cameraPoint(0.14, 10, 4.8, 0.22, 0, 2.5, 62),
            (t - 27000) / 5000
        )
    else
        S.cameraLerp(
            S.cameraPoint(0.08, 0, 3.2, 0.90, 0, 1.5, 72),
            S.cameraPoint(-0.09, 0, 16.0, 0.62, 0, 1.0, 84),
            S.clamp((t - 32000) / 6000, 0, 1)
        )
    end
end

S.SHOT_WINDOWS = {
    [1] = { offset = 0, duration = 3700 },
    [2] = { offset = 3500, duration = 7200 },
    [3] = { offset = 10500, duration = 6700 },
    [4] = { offset = 17000, duration = 5200 },
    [5] = { offset = 22000, duration = 5200 },
    [6] = { offset = 27000, duration = 5200 },
    [7] = { offset = 32000, duration = 6200 },
}

function S.timelineTime()
    local elapsed = getTickCount() - state.startedAt
    if state.mode == "shot" and S.SHOT_WINDOWS[state.shot] then
        local shot = S.SHOT_WINDOWS[state.shot]
        return elapsed + shot.offset, elapsed, shot.duration
    end
    return elapsed, elapsed, S.FULL_DURATION
end

function S.renderShow()
    if not state.active then
        return
    end

    if state.mode == "setup" or state.mode == "final" then
        S.applyFinalLights(state.finalStep < 0 and 0 or state.finalStep)
        setTime(23, 0)
        local cam = S.cameraPoint(-0.08, 0, 17.0, 0.62, 0, 1.2, 84)
        setCameraMatrix(cam[1], cam[2], cam[3], cam[4], cam[5], cam[6], 0, cam[7])
        return
    end

    local t, elapsed, duration = S.timelineTime()
    S.updateSceneCues(t)
    S.setCameraForTimeline(t)

    if elapsed >= duration then
        state.active = false
        triggerServerEvent("2dfxShowcase:finished", resourceRoot)
    end
end

function S.beginShow(mode, shot)
    if not state.prepared then
        return
    end

    state.mode = mode or "full"
    state.shot = shot
    state.cues = {}
    state.chaseStep = -1
    state.finalStep = -1
    state.startedAt = getTickCount()
    state.active = true

    S.setPresentationUI(state.mode == "setup")
    if not isEventHandlerAdded or not isEventHandlerAdded("onClientRender", root, S.renderShow) then
        addEventHandler("onClientRender", root, S.renderShow)
    end

    if state.mode == "setup" or state.mode == "final" then
        S.applyFinalLights(0)
        setTime(23, 0)
        setWeather(0)
    else
        S.allLightsOff()
        S.restoreVanillaLight()
        setTime(18, 20)
        setWeather(0)
    end

    S.log(string.format("started runway mode=%s shot=%s", tostring(state.mode), tostring(state.shot)))
end

function S.prepareShow(_, _, _, dimension)
    S.cleanupLocal()

    state.prepared = true
    state.dimension = dimension
    S.configureRunway()

    local hour, minute = getTime()
    state.savedTime = { hour = hour, minute = minute }
    state.savedWeather = getWeather()

    S.setPresentationUI(false)
    setTime(18, 20)
    setWeather(0)

    local ok, reason = S.buildRuntimeModels()
    if not ok then
        S.failSetup(reason)
        return
    end

    ok, reason = S.buildRunwayAnchors()
    if not ok then
        S.failSetup(reason)
        return
    end

    S.later(1200, function()
        local effectsOk, effectsReason = S.addEffects()
        if not effectsOk then
            S.failSetup(effectsReason)
            return
        end

        -- Particle and roadsign models perform their targeted restream while the
        -- screen is still black. The recorded timeline itself stays restream-free.
        S.later(2300, function()
            local cam = S.cameraPoint(-0.04, 0, 3.0, 0.8, 0, 1.2, 72)
            setCameraMatrix(cam[1], cam[2], cam[3], cam[4], cam[5], cam[6], 0, cam[7])
            triggerServerEvent("2dfxShowcase:ready", resourceRoot)
        end)
    end)
end

addEvent("2dfxShowcase:prepare", true)
addEventHandler("2dfxShowcase:prepare", resourceRoot, function(cx, cy, cz, dimension)
    S.prepareShow(cx, cy, cz, dimension)
end)

addEvent("2dfxShowcase:begin", true)
addEventHandler("2dfxShowcase:begin", resourceRoot, S.beginShow)

addEvent("2dfxShowcase:stop", true)
addEventHandler("2dfxShowcase:stop", resourceRoot, S.cleanupLocal)

addEventHandler("onClientResourceStop", resourceRoot, S.cleanupLocal)
