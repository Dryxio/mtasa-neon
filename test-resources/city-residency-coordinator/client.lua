local SAMPLE_INTERVAL_MS = 250
local QUERY_INTERVAL_MS = 10000
local REQUEST_TIMEOUT_MS = 60000
local SWITCH_COOLDOWN_MS = 15000
local SAFE_HOLD_PADDING = 750
local TELEPORT_DISTANCE = 500

-- These are the exact generated placement bounds. They live in this tracked
-- file because each city's generated map_data.lua is intentionally ignored.
local CITIES = {
    vc = {
        name = "Vice City",
        resource = "ug-vc",
        bounds = {minX = 5671.470, maxX = 8462.940, minY = -9466.810, maxY = -5924.260},
    },
    lc = {
        name = "Liberty City",
        resource = "ug-lc",
        bounds = {minX = 5975.040, maxX = 9701.500, minY = 6300.200, maxY = 9362.430},
    },
    bw = {
        name = "Bullworth",
        resource = "ug-bw",
        bounds = {minX = -9040.300, maxX = -7462.800, minY = 7044.400, maxY = 8409.100},
    },
    carcer = {
        name = "Carcer City",
        resource = "carcer-city-test",
        bounds = {minX = 4022.675, maxX = 7390.983, minY = -1756.924, maxY = 2040.721},
    },
}

local POLICY_OPTIONS = {
    approachPadding = 2000,
    criticalPadding = 500,
    predictionPadding = 1000,
    predictionHorizon = 15,
    leavePadding = 2500,
}

local providerStates = {}
for id in pairs(CITIES) do
    providerStates[id] = {stage = "unknown", details = "not reported", lastSeen = 0}
end

local mode = "auto"
local forcedCity = nil
local activeCity = nil
local preparing = nil
local queuedCity = nil
local candidate = nil
local latchedCity = nil
local lastSwitchAt = -SWITCH_COOLDOWN_MS
local manualCommitGraceUntil = 0
local requestSequence = 0
local lastSample = nil
local lastPosition = {x = 0, y = 0, z = 0}
local velocity = {x = 0, y = 0, z = 0}
local suspended = false
local failures = {}
local emergency = nil

local function transition(message, level)
    outputDebugString("[city-residency] " .. message, level or 3)
end

local function cityLabel(id)
    return id and CITIES[id] and CITIES[id].name or "none"
end

local function endEmergency()
    if not emergency then
        return
    end
    if isElement(emergency.element) and not emergency.wasFrozen then
        setElementFrozen(emergency.element, false)
    end
    emergency = nil
    fadeCamera(true, 0.5)
end

local function beginEmergency(cityId, reason)
    if emergency and emergency.city == cityId then
        return
    end
    endEmergency()

    local controlled = getPedOccupiedVehicle(localPlayer) or localPlayer
    emergency = {
        city = cityId,
        element = controlled,
        wasFrozen = isElementFrozen(controlled),
        reason = reason,
    }
    fadeCamera(false, 0.25)
    if not emergency.wasFrozen then
        setElementFrozen(controlled, true)
    end
    transition(("emergency hold for %s (%s)"):format(cityLabel(cityId), reason), 2)
end

local function handoffEmergencyToManual()
    if not emergency then
        return
    end
    if isElement(emergency.element) and not emergency.wasFrozen then
        setElementFrozen(emergency.element, false)
    end
    -- The legacy prepare-before-teleport handshake already owns the black
    -- screen. Leave it faded until that city's commit/cancel token completes.
    emergency = nil
end

local function failureDelay(attempt)
    local delays = {5000, 15000, 30000, 60000}
    return delays[math.min(attempt, #delays)]
end

local function requestCity(cityId, urgent, reason)
    local now = getTickCount()
    local resource = getResourceFromName(CITIES[cityId].resource)
    if not resource or getResourceState(resource) ~= "running" then
        providerStates[cityId].stage = "unavailable"
        providerStates[cityId].details = "resource is not running"
        queuedCity = cityId
        if urgent then
            beginEmergency(cityId, "waiting for city resource restart")
        end
        return false
    end

    local failure = failures[cityId]
    if failure and now < failure.retryAt then
        queuedCity = cityId
        if urgent then
            beginEmergency(cityId, "waiting for failure retry backoff")
        end
        return false
    end

    requestSequence = requestSequence + 1
    local token = ("auto:%d:%s"):format(requestSequence, cityId)
    preparing = {
        city = cityId,
        token = token,
        startedAt = now,
        reason = reason,
    }
    queuedCity = nil
    if urgent then
        beginEmergency(cityId, reason)
    end

    transition(("request %s token=%s reason=%s"):format(cityLabel(cityId), token, reason))
    triggerEvent("ugWorldRequestCityResidency", root, cityId, token, lastPosition.x, lastPosition.y, lastPosition.z)
    return true
end

local function sampleMovement()
    local now = getTickCount()
    local x, y, z = getElementPosition(localPlayer)
    local teleported = false

    if lastSample then
        local elapsed = math.max(1, now - lastSample.tick) / 1000
        local dx, dy, dz = x - lastSample.x, y - lastSample.y, z - lastSample.z
        local distance = math.sqrt(dx * dx + dy * dy + dz * dz)
        teleported = distance > TELEPORT_DISTANCE
        if teleported or elapsed > 2 then
            velocity.x, velocity.y, velocity.z = 0, 0, 0
        else
            velocity.x, velocity.y, velocity.z = dx / elapsed, dy / elapsed, dz / elapsed
        end
    end

    lastSample = {x = x, y = y, z = z, tick = now}
    lastPosition = {x = x, y = y, z = z}
    return now, teleported
end

local function canLeaveActiveCity()
    if not activeCity or not CITIES[activeCity] then
        return true
    end
    return not CityResidencyPolicy.contains(
        lastPosition.x,
        lastPosition.y,
        CITIES[activeCity].bounds,
        SAFE_HOLD_PADDING
    )
end

local function evaluateResidency()
    local now, teleported = sampleMovement()
    if now < manualCommitGraceUntil then
        return
    end
    if suspended or getElementInterior(localPlayer) ~= 0 or getElementDimension(localPlayer) ~= 0 then
        candidate = nil
        queuedCity = nil
        return
    end

    local nextCandidate = CityResidencyPolicy.pickCandidate(CITIES, lastPosition, velocity, POLICY_OPTIONS)
    candidate = CityResidencyPolicy.retainCandidate(CITIES, latchedCity, nextCandidate, lastPosition, velocity, POLICY_OPTIONS)
    latchedCity = candidate and candidate.id or nil
    if forcedCity then
        candidate = {
            id = forcedCity,
            distance = CityResidencyPolicy.distanceToBounds(lastPosition.x, lastPosition.y, CITIES[forcedCity].bounds),
            urgent = CityResidencyPolicy.contains(lastPosition.x, lastPosition.y, CITIES[forcedCity].bounds, POLICY_OPTIONS.criticalPadding),
            predicted = false,
        }
    elseif mode ~= "auto" then
        candidate = nil
    end

    if preparing then
        if preparing.manual then
            queuedCity = candidate and candidate.id ~= preparing.city and candidate.id or nil
        elseif forcedCity and forcedCity ~= preparing.city then
            requestCity(forcedCity, candidate and candidate.urgent or false, "manual override superseded preparation")
        elseif candidate and candidate.id == preparing.city and candidate.urgent then
            beginEmergency(preparing.city, teleported and "teleport into inactive city" or "arrival before preparation completed")
        elseif candidate and candidate.id ~= preparing.city then
            if candidate.urgent then
                requestCity(candidate.id, true, teleported and "urgent teleport superseded preparation" or "urgent destination changed")
            else
                queuedCity = candidate.id
            end
        end

        -- Legacy manual commands have their own server-side timeout and commit
        -- token. Let that owner publish manual-cancelled/manual-committed.
        if preparing and not preparing.manual and now - preparing.startedAt > REQUEST_TIMEOUT_MS then
            local timedOutCity = preparing.city
            transition(("request timed out for %s"):format(cityLabel(timedOutCity)), 1)
            failures[timedOutCity] = {
                attempts = (failures[timedOutCity] and failures[timedOutCity].attempts or 0) + 1,
                reason = "request timeout",
                retryAt = now + 60000,
            }
            preparing = nil
            triggerEvent("ugWorldDeactivateCities", root, nil)
            endEmergency()
        end
        return
    end

    if not candidate or candidate.id == activeCity then
        queuedCity = nil
        return
    end

    local urgent = candidate.urgent or teleported
    local cooldownActive = now - lastSwitchAt < SWITCH_COOLDOWN_MS
    if not forcedCity and not urgent and (not canLeaveActiveCity() or cooldownActive) then
        queuedCity = candidate.id
        return
    end

    local provider = providerStates[candidate.id]
    if not provider or provider.stage == "unknown" then
        queuedCity = candidate.id
        return
    end

    local reason
    if forcedCity then
        reason = "manual override"
    elseif teleported then
        reason = "teleport into approach envelope"
    elseif candidate.predicted then
        reason = ("predicted arrival in %.1fs"):format(candidate.eta or 0)
    else
        reason = ("within %.0fm approach envelope"):format(candidate.distance)
    end
    requestCity(candidate.id, urgent, reason)
end

addEvent("ugWorldCityResidencyState", false)
addEventHandler("ugWorldCityResidencyState", root, function(cityId, stage, requestToken, details)
    if not CITIES[cityId] then
        return
    end

    providerStates[cityId] = {
        stage = stage,
        token = requestToken,
        details = tostring(details or ""),
        lastSeen = getTickCount(),
    }

    if stage == "preparing" and requestToken and tostring(requestToken):find("^manual:") then
        handoffEmergencyToManual()
        preparing = {
            city = cityId,
            token = requestToken,
            startedAt = getTickCount(),
            reason = "manual city command",
            manual = true,
        }
        queuedCity = nil
        transition(("manual preparation owns %s"):format(cityLabel(cityId)))
    elseif stage == "manual-ready" then
        activeCity = cityId
        failures[cityId] = nil
        transition(("%s is preloaded; awaiting server teleport commit"):format(cityLabel(cityId)))
    elseif stage == "manual-committed" then
        activeCity = cityId
        failures[cityId] = nil
        if preparing and preparing.manual and requestToken == preparing.token then
            preparing = nil
            lastSwitchAt = getTickCount()
        end
        -- The client event follows the server-side warp, but position sync can
        -- land on the next pulse. Do not let the old location reclaim slots.
        manualCommitGraceUntil = getTickCount() + 2000
        endEmergency()
        transition(("manual teleport committed for %s"):format(cityLabel(cityId)))
    elseif stage == "manual-cancelled" then
        local ownedCancellation = preparing and preparing.manual and requestToken == preparing.token
        if ownedCancellation then
            preparing = nil
            if not emergency then
                fadeCamera(true, 0.5)
            end
        end
        transition(("manual preparation cancelled for %s (%s)"):format(cityLabel(cityId), tostring(details)))
    elseif stage == "ready" then
        activeCity = cityId
        failures[cityId] = nil
        if preparing and requestToken == preparing.token then
            local manualCompletion = preparing.manual
            transition(("%s resident after %dms (%s)"):format(cityLabel(cityId), getTickCount() - preparing.startedAt, tostring(details)))
            preparing = nil
            lastSwitchAt = getTickCount()
            if not manualCompletion then
                endEmergency()
            end
        elseif preparing and preparing.manual and requestToken == preparing.token then
            transition(("manual activation made %s resident"):format(cityLabel(cityId)))
            preparing = nil
            lastSwitchAt = getTickCount()
            endEmergency()
        end
    elseif stage == "standby" then
        if activeCity == cityId then
            activeCity = nil
        end
        -- A provider restart loses its in-memory request. Replaying the same
        -- generation token resumes the coordinator transition without waiting
        -- for the general timeout or accepting a stale completion.
        if preparing and preparing.city == cityId and tostring(details) == "resource started" then
            triggerEvent(
                "ugWorldRequestCityResidency",
                root,
                cityId,
                preparing.token,
                lastPosition.x,
                lastPosition.y,
                lastPosition.z
            )
        elseif preparing and preparing.city == cityId and preparing.manual then
            preparing = nil
        end
    elseif stage == "releasing" then
        if (activeCity == cityId or (preparing and preparing.city == cityId)) and
            CityResidencyPolicy.contains(lastPosition.x, lastPosition.y, CITIES[cityId].bounds, POLICY_OPTIONS.criticalPadding) then
            beginEmergency(cityId, "resident city resource is restarting")
        end
    elseif stage == "failed" and preparing and preparing.city == cityId and requestToken == preparing.token then
        local attempt = (failures[cityId] and failures[cityId].attempts or 0) + 1
        failures[cityId] = {
            attempts = attempt,
            reason = tostring(details),
            retryAt = getTickCount() + failureDelay(attempt),
        }
        transition(("%s failed: %s"):format(cityLabel(cityId), tostring(details)), 1)
        preparing = nil
        if not candidate or candidate.id ~= cityId or not candidate.urgent then
            endEmergency()
        end
    end
end)

local function queryProviders()
    triggerEvent("ugWorldQueryCityResidency", root)
end

local function formatStats()
    local speed = math.sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z)
    local candidateText = "none"
    if candidate then
        candidateText = ("%s distance=%.0fm eta=%s urgent=%s"):format(
            candidate.id,
            candidate.distance,
            candidate.eta and ("%.1fs"):format(candidate.eta) or "n/a",
            tostring(candidate.urgent)
        )
    end
    return ("mode=%s forced=%s active=%s preparing=%s queued=%s emergency=%s pos=%.0f,%.0f,%.0f velocity=%.1f,%.1f,%.1f speed=%.1fm/s candidate=%s"):format(
        mode,
        tostring(forcedCity),
        tostring(activeCity),
        preparing and (preparing.city .. "/" .. preparing.token) or "none",
        tostring(queuedCity),
        emergency and emergency.city or "none",
        lastPosition.x,
        lastPosition.y,
        lastPosition.z,
        velocity.x,
        velocity.y,
        velocity.z,
        speed,
        candidateText
    )
end

function getCityResidencyStats()
    return {
        mode = mode,
        forcedCity = forcedCity,
        activeCity = activeCity,
        preparingCity = preparing and preparing.city or false,
        queuedCity = queuedCity or false,
        emergencyCity = emergency and emergency.city or false,
        position = {lastPosition.x, lastPosition.y, lastPosition.z},
        velocity = {velocity.x, velocity.y, velocity.z},
        providers = providerStates,
        failures = failures,
    }
end

local function outputStats()
    local message = formatStats()
    outputChatBox("[city-residency] " .. message, 80, 220, 255)
    outputDebugString("[city-residency] " .. message)
    for id, state in pairs(providerStates) do
        outputConsole(("[city-residency] provider=%s stage=%s token=%s details=%s"):format(id, state.stage, tostring(state.token), state.details))
    end
end

addCommandHandler("cityresidencystats", outputStats)
addCommandHandler("cityresidency", function(_, argument)
    local requested = argument and argument:lower() or nil
    if not requested then
        outputStats()
    elseif requested == "auto" then
        mode, forcedCity = "auto", nil
        outputChatBox("[city-residency] Automatic residency enabled.", 80, 255, 160)
    elseif requested == "off" then
        mode, forcedCity, queuedCity = "off", nil, nil
        endEmergency()
        outputChatBox("[city-residency] Automatic residency disabled; the resident city is retained.", 255, 200, 80)
    elseif CITIES[requested] then
        mode, forcedCity = "manual", requested
        outputChatBox(("[city-residency] Manual override: %s. Use /cityresidency auto to release it."):format(cityLabel(requested)), 255, 200, 80)
    else
        outputChatBox("[city-residency] Usage: /cityresidency auto|off|vc|lc|bw|carcer", 255, 180, 80)
    end
    evaluateResidency()
end)

addCommandHandler("cityresidencytest", function()
    local ok, reason, passed, total = runCityResidencyPolicyTests()
    local message = ("policy tests %s: %d/%d (%s)"):format(ok and "PASS" or "FAIL", passed, total, reason)
    outputChatBox("[city-residency] " .. message, ok and 80 or 255, ok and 255 or 80, ok and 160 or 80)
    outputDebugString("[city-residency] " .. message, ok and 3 or 1)
end)

addEventHandler("onClientPlayerWasted", localPlayer, function()
    suspended = true
    queuedCity = nil
    endEmergency()
end)

addEventHandler("onClientPlayerSpawn", localPlayer, function()
    suspended = false
    lastSample = nil
    setTimer(evaluateResidency, 100, 1)
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    transition("coordinator started; querying city providers")
    setTimer(queryProviders, 250, 1)
    setTimer(evaluateResidency, SAMPLE_INTERVAL_MS, 0)
    setTimer(queryProviders, QUERY_INTERVAL_MS, 0)
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    endEmergency()
end)
