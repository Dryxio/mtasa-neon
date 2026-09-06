local leases = {}

local function clearTimer(lease, name)
    if isTimer(lease[name]) then
        killTimer(lease[name])
    end
    lease[name] = nil
end

local function clearTimers(lease)
    clearTimer(lease, "fadeTimer")
    clearTimer(lease, "holdTimer")
    clearTimer(lease, "finishTimer")
end

local function restoreView(lease)
    clearTimers(lease)
    lease.busy = false
    if lease.fadeOwned then
        fadeCamera(true, 0.0, 0, 0, 0)
        lease.fadeOwned = false
    end
end

local function rotatedOffset(x, y, trigger)
    local radians = math.rad(-(trigger.rotation or 0.0))
    local dx, dy = x - trigger.x, y - trigger.y
    local cosine, sine = math.cos(radians), math.sin(radians)
    return dx * cosine - dy * sine, dx * sine + dy * cosine
end

local function isWithinTrigger(endpoint)
    if getElementInterior(localPlayer) ~= endpoint.interior then
        return false
    end
    local x, y, z = getElementPosition(localPlayer)
    local dx, dy = rotatedOffset(x, y, endpoint.trigger)
    return math.abs(dx) <= endpoint.trigger.radiusX and math.abs(dy) <= endpoint.trigger.radiusY and
               math.abs(z - endpoint.trigger.z) <= endpoint.trigger.zTolerance
end

local function detectTransitions()
    if isPedDead(localPlayer) or getPedOccupiedVehicle(localPlayer) then
        return
    end
    for id, lease in pairs(leases) do
        if not lease.busy and not lease.disabled and getElementDimension(localPlayer) == lease.dimension then
            local direction
            if isWithinTrigger(lease.definition.outside) then
                direction = "enter"
            elseif isWithinTrigger(lease.definition.inside) then
                direction = "exit"
            end
            if direction then
                lease.busy = true
                triggerServerEvent("storyEntryExitRuntime:request", resourceRoot, id, direction)
                return
            end
        end
    end
end

setTimer(detectTransitions, 50, 0)

addEvent("storyEntryExitRuntime:add", true)
addEventHandler("storyEntryExitRuntime:add", resourceRoot, function(id, definition, dimension, options)
    id = tonumber(id)
    if source ~= resourceRoot or not id or type(definition) ~= "table" or type(options) ~= "table" then
        return
    end
    if leases[id] then
        restoreView(leases[id])
    end
    leases[id] = {
        definition = definition,
        dimension = dimension,
        options = options,
        epoch = 0,
        busy = false,
        fadeOwned = false,
    }
    triggerServerEvent("storyEntryExitRuntime:ack", resourceRoot, id)
end)

addEvent("storyEntryExitRuntime:fadeOut", true)
addEventHandler("storyEntryExitRuntime:fadeOut", resourceRoot, function(id, epoch, duration)
    local lease = leases[tonumber(id)]
    if source ~= resourceRoot or not lease then
        return
    end
    clearTimers(lease)
    lease.epoch = tonumber(epoch)
    lease.busy = true
    lease.fadeOwned = true
    duration = math.max(0.0, tonumber(duration) or 0.5)
    fadeCamera(false, duration, 0, 0, 0)
    lease.fadeTimer = setTimer(function()
        lease.fadeTimer = nil
        if leases[tonumber(id)] == lease and lease.busy then
            local holdDuration = math.max(0.0, tonumber(lease.options.blackHold) or 0.25)
            lease.holdTimer = setTimer(function()
                lease.holdTimer = nil
                if leases[tonumber(id)] == lease and lease.busy then
                    triggerServerEvent("storyEntryExitRuntime:black", resourceRoot, tonumber(id), lease.epoch)
                end
            end, math.max(50, math.ceil(holdDuration * 1000)), 1)
        end
    end, math.max(50, math.ceil(duration * 1000)), 1)
end)

addEvent("storyEntryExitRuntime:rejected", true)
addEventHandler("storyEntryExitRuntime:rejected", resourceRoot, function(id)
    local lease = leases[tonumber(id)]
    if source == resourceRoot and lease then
        lease.busy = false
    end
end)

addEvent("storyEntryExitRuntime:committed", true)
addEventHandler("storyEntryExitRuntime:committed", resourceRoot, function(id, epoch, interior, fadeDuration)
    local lease = leases[tonumber(id)]
    if source ~= resourceRoot or not lease or lease.epoch ~= tonumber(epoch) then
        return
    end
    setCameraInterior(tonumber(interior) or 0)
    fadeDuration = math.max(0.0, tonumber(fadeDuration) or 0.5)
    fadeCamera(true, fadeDuration, 0, 0, 0)
    lease.finishTimer = setTimer(function()
        lease.finishTimer = nil
        if leases[tonumber(id)] ~= lease or not lease.busy then
            return
        end
        lease.fadeOwned = false
        triggerServerEvent("storyEntryExitRuntime:finished", resourceRoot, tonumber(id), lease.epoch)
    end, math.max(50, math.ceil(fadeDuration * 1000)), 1)
end)

addEvent("storyEntryExitRuntime:ready", true)
addEventHandler("storyEntryExitRuntime:ready", resourceRoot, function(id, epoch)
    local lease = leases[tonumber(id)]
    if source == resourceRoot and lease and lease.epoch == tonumber(epoch) then
        clearTimers(lease)
        lease.busy = false
        lease.fadeOwned = false
    end
end)

addEvent("storyEntryExitRuntime:cancel", true)
addEventHandler("storyEntryExitRuntime:cancel", resourceRoot, function(id, epoch)
    local lease = leases[tonumber(id)]
    if source == resourceRoot and lease and lease.epoch == tonumber(epoch) then
        restoreView(lease)
        lease.disabled = true
    end
end)

addEvent("storyEntryExitRuntime:remove", true)
addEventHandler("storyEntryExitRuntime:remove", resourceRoot, function(id)
    local lease = leases[tonumber(id)]
    if source == resourceRoot and lease then
        restoreView(lease)
        leases[tonumber(id)] = nil
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    for _, lease in pairs(leases) do
        restoreView(lease)
    end
    leases = {}
end)
