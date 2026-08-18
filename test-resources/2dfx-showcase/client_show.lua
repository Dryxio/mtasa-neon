local S = DFXShowcase
local state = S.state
local PALETTE = S.PALETTE

function S.cue(key, condition, callback)
    if condition and not state.cues[key] then
        state.cues[key] = true
        callback()
    end
end

function S.updateSceneCues(t)
    S.cue("night", t >= 5000, function()
        setTime(23, 0)
        setWeather(0)
    end)

    for row = 1, 6 do
        S.cue("row" .. row, t >= 5200 + (row - 1) * 900, function()
            S.activateLampRow(row)
        end)
    end

    S.cue("hero_on", t >= 10800, function()
        S.setLight(state.heroModel, state.heroEffect, tocolor(80, 245, 255, 255), 2.5, 30)
    end)

    S.cue("hero_flash", t >= 11900, function()
        if state.heroModel and state.heroEffect then
            setModel2DFXProperty(state.heroModel, state.heroEffect, "showMode", "random_flashing")
        end
    end)

    S.cue("hero_normal", t >= 12900, function()
        if state.heroModel and state.heroEffect then
            setModel2DFXProperty(state.heroModel, state.heroEffect, "showMode", "default")
        end
    end)

    S.cue("vanilla_mutate", t >= 13600, S.mutateVanillaLight)
    S.cue("vanilla_restore", t >= 16500, S.restoreVanillaLight)

    if t >= 35000 then
        local step = math.floor((t - 35000) / 700)
        if step ~= state.lastFinalStep then
            state.lastFinalStep = step
            for row, model in ipairs(state.lampModels) do
                local color = PALETTE[((row + step - 1) % #PALETTE) + 1]
                S.setLight(model, state.lampEffects[row], color, 1.8, 24)
            end
        end
    end
end

function S.setCameraForTimeline(t)
    local cx, cy, cz = state.cx, state.cy, state.cz

    if t < 5000 then
        S.cameraLerp(
            { cx - 18, cy - 34, cz + 8, cx, cy + 35, cz + 8, 76 },
            { cx + 5, cy - 24, cz + 6, cx, cy + 42, cz + 10, 68 },
            t / 5000
        )
    elseif t < 13000 then
        S.cameraLerp(
            { cx - 12, cy - 27, cz + 3.5, cx, cy + 18, cz + 4.5, 78 },
            { cx + 8, cy - 10, cz + 4.5, cx, cy + 22, cz + 5.5, 66 },
            (t - 5000) / 8000
        )
    elseif t < 18000 then
        S.cameraLerp(
            { cx + 19, cy - 4, cz + 3.2, cx + 12, cy + 3, cz + 5.5, 58 },
            { cx + 8, cy + 2, cz + 4.5, cx + 12, cy + 3, cz + 5.5, 50 },
            (t - 13000) / 5000
        )
    elseif t < 23000 then
        S.cameraLerp(
            { cx - 19, cy + 4, cz + 8.5, cx - 11, cy + 14, cz + 5.5, 62 },
            { cx + 18, cy + 8, cz + 9.0, cx + 11, cy + 16, cz + 6.0, 64 },
            (t - 18000) / 5000
        )
    elseif t < 29000 then
        S.cameraLerp(
            { cx, cy + 9, cz + 3.4, cx, cy + 28, cz + 4.0, 58 },
            { cx - 9, cy + 16, cz + 4.8, cx, cy + 28, cz + 4.2, 52 },
            (t - 23000) / 6000
        )
    elseif t < 35000 then
        S.cameraLerp(
            { cx - 16, cy + 24, cz + 2.8, cx, cy + 35, cz + 3.2, 64 },
            { cx + 13, cy + 27, cz + 5.5, cx, cy + 35, cz + 3.5, 60 },
            (t - 29000) / 6000
        )
    else
        S.cameraLerp(
            { cx + 17, cy - 10, cz + 7.0, cx, cy + 20, cz + 5.0, 76 },
            { cx, cy - 39, cz + 14.5, cx, cy + 18, cz + 5.5, 82 },
            S.clamp((t - 35000) / 7000, 0, 1)
        )
    end
end

S.SHOT_WINDOWS = {
    [1] = { offset = 0, duration = 5200 },
    [2] = { offset = 5000, duration = 8200 },
    [3] = { offset = 13000, duration = 5200 },
    [4] = { offset = 18000, duration = 5200 },
    [5] = { offset = 23000, duration = 6200 },
    [6] = { offset = 29000, duration = 6200 },
    [7] = { offset = 35000, duration = 7200 },
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
        S.applyAllLights()
        setTime(23, 0)
        setCameraMatrix(
            state.cx, state.cy - 38, state.cz + 13,
            state.cx, state.cy + 18, state.cz + 5.5,
            0, 82
        )
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
    state.lastFinalStep = -1
    state.startedAt = getTickCount()
    state.active = true

    S.setPresentationUI(state.mode == "setup")
    addEventHandler("onClientRender", root, S.renderShow)

    if state.mode == "setup" or state.mode == "final" then
        S.applyAllLights()
        setTime(23, 0)
    else
        S.setLampRowsOff()
        setTime(18, 30)
        setWeather(0)
    end

    S.log(string.format("started mode=%s shot=%s", tostring(state.mode), tostring(state.shot)))
end

function S.prepareShow(cx, cy, cz, dimension)
    S.cleanupLocal()

    state.prepared = true
    state.cx, state.cy, state.cz = cx, cy, cz
    state.dimension = dimension

    local hour, minute = getTime()
    state.savedTime = { hour = hour, minute = minute }
    local weather = getWeather()
    state.savedWeather = weather

    S.setPresentationUI(false)
    setTime(18, 30)
    setWeather(0)

    local ok, reason = S.buildRuntimeModels()
    if not ok then
        S.failSetup(reason)
        return
    end

    ok, reason = S.buildDecor()
    if not ok then
        S.failSetup(reason)
        return
    end

    S.later(1400, function()
        local effectsOk, effectsReason = S.addEffects()
        if not effectsOk then
            S.failSetup(effectsReason)
            return
        end

        -- Particle, roadsign and escalator additions restream only their own
        -- requested models. Prewarm the native escalator steps while the server
        -- still holds the camera behind a black fade.
        S.later(1500, function()
            setCameraMatrix(
                state.cx - 8, state.cy + 28, state.cz + 4,
                state.cx, state.cy + 35, state.cz + 3.5,
                0, 68
            )
            S.later(1200, function()
                triggerServerEvent("2dfxShowcase:ready", resourceRoot)
            end)
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
