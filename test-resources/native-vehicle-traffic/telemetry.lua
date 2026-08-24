VehicleTrafficTelemetry = {}

local function finiteNumber(value, limit)
    value = tonumber(value)
    return value and value == value and math.abs(value) <= (limit or 100000) and value or nil
end

function VehicleTrafficTelemetry.newCounterWindow()
    return {events = {}, reasons = {}}
end

function VehicleTrafficTelemetry.record(window, event, reason)
    if type(window) ~= "table" then return end
    event = tostring(event or "unknown")
    window.events[event] = (window.events[event] or 0) + 1
    if reason ~= nil then
        reason = tostring(reason)
        window.reasons[reason] = (window.reasons[reason] or 0) + 1
    end
end

function VehicleTrafficTelemetry.drain(window)
    local snapshot = {events = window.events or {}, reasons = window.reasons or {}}
    window.events = {}
    window.reasons = {}
    return snapshot
end

function VehicleTrafficTelemetry.classifyMotion(sample)
    if type(sample) ~= "table" then return false, "sample-contract" end
    local heading = finiteNumber(sample.rz, 3600)
    local vx = finiteNumber(sample.vx, 3)
    local vy = finiteNumber(sample.vy, 3)
    if not heading or not vx or not vy then return false, "sample-kinematics" end

    local speed = math.sqrt(vx * vx + vy * vy)
    if speed < 0.01 then
        return {state = "stationary", speed = speed, alignment = 0, signedForwardSpeed = 0}
    end

    -- MTA's zero Z rotation faces +Y. This is the same forward basis exposed
    -- by row two of getElementMatrix, expressed using the sampled heading so
    -- the server can diagnose owner reports without trusting another event.
    local radians = math.rad(heading)
    local forwardX, forwardY = -math.sin(radians), math.cos(radians)
    local alignment = (vx * forwardX + vy * forwardY) / speed
    local state = "lateral"
    if alignment >= 0.45 then
        state = "forward"
    elseif alignment <= -0.45 then
        state = "reverse"
    end
    return {state = state, speed = speed, alignment = alignment, signedForwardSpeed = speed * alignment}
end

function VehicleTrafficTelemetry.updateMotion(state, sample, now)
    state = type(state) == "table" and state or {}
    now = tonumber(now) or 0
    local motion, reason = VehicleTrafficTelemetry.classifyMotion(sample)
    if not motion then return state, false, reason end

    state.samples = (state.samples or 0) + 1
    state.counts = state.counts or {forward = 0, reverse = 0, stationary = 0, lateral = 0}
    state.counts[motion.state] = (state.counts[motion.state] or 0) + 1
    state.lastState = motion.state
    state.lastSpeed = motion.speed
    state.lastAlignment = motion.alignment
    state.maximumReverseSpeed = math.max(state.maximumReverseSpeed or 0,
                                         motion.state == "reverse" and math.abs(motion.signedForwardSpeed) or 0)

    local x = finiteNumber(sample.x, 10000)
    local y = finiteNumber(sample.y, 10000)
    if x and y then
        if state.lastX and state.lastY then
            local dx, dy = x - state.lastX, y - state.lastY
            local step = math.sqrt(dx * dx + dy * dy)
            if step <= 25 then state.distance = (state.distance or 0) + step end
        end
        state.lastX, state.lastY = x, y
    end

    local signal = false
    if motion.state == "reverse" then
        state.reverseSince = state.reverseSince or now
        local duration = now - state.reverseSince
        if duration >= 2000 and now - (state.lastAlertAt or -100000) >= 10000 then
            state.lastAlertAt = now
            state.alertActive = true
            signal = {kind = "reverse-sustained", duration = duration, motion = motion}
        end
    else
        if state.alertActive and motion.state == "forward" then
            signal = {kind = "reverse-recovered", duration = now - (state.reverseSince or now), motion = motion}
        end
        state.reverseSince = nil
        state.alertActive = false
    end
    return state, signal
end

return VehicleTrafficTelemetry
