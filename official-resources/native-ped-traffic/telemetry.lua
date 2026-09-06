PedTrafficTelemetry = {}

local function copyMap(source)
    local result = {}
    for key, value in pairs(source or {}) do result[key] = value end
    return result
end

function PedTrafficTelemetry.deltaMap(current, previous)
    local delta = {}
    for key, value in pairs(current or {}) do
        local difference = value - ((previous and previous[key]) or 0)
        if difference ~= 0 then delta[key] = difference end
    end
    return delta, copyMap(current)
end

function PedTrafficTelemetry.classifyPlayer(input)
    if input.enabled ~= true then return "disabled" end
    if input.eligible ~= true then return "ineligible-world" end
    if input.worldReady ~= true then return "world-converging" end
    if input.profilePresent ~= true then return "profile-missing" end
    if input.profileRevisionOk ~= true then return "profile-revision-mismatch" end
    if input.profileFresh ~= true then return "profile-stale" end
    if input.globalLive >= input.globalCap then return "global-cap" end
    if input.pedPoolLive >= input.pedPoolCap then return "ped-pool-cap" end
    if input.live >= input.target then return "target-met" end
    if input.pendingRequest == true then return "candidate-pending" end
    if input.anyPendingRequest == true then return "request-lane-busy" end
    if input.anyVisibilityCheck == true then return "visibility-lane-busy" end
    return "candidate-ready"
end

return PedTrafficTelemetry
