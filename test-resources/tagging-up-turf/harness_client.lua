local activeProbe
local activeSpray
local activeMirrorProbe
local mirroredMissionCompletedTags = {}

local function trace(event, data)
    local record = {event = event, tick = getTickCount()}
    for key, value in pairs(type(data) == "table" and data or {}) do
        record[key] = value
    end
    outputDebugString("[tagging-up-turf-harness-jsonl] " .. tostring(toJSON(record, true)):gsub("[\r\n]", ""))
end

local function clearProbe()
    local probe = activeProbe
    activeProbe = nil
    if not probe then
        return
    end
    for _, timer in ipairs({probe.pollTimer, probe.timeoutTimer}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
end

local function killOwnedTimers(owner)
    if not owner then
        return
    end
    for _, timer in ipairs(owner.timers or {}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    owner.timers = {}
end

local function clearSpray()
    killOwnedTimers(activeSpray)
    activeSpray = nil
end

local function clearMirrorProbe()
    killOwnedTimers(activeMirrorProbe)
    activeMirrorProbe = nil
end

local function sprayAimTelemetry()
    local startX, startY, startZ = getPedTargetStart(localPlayer)
    local endX, endY, endZ = getPedTargetEnd(localPlayer)
    local hitX, hitY, hitZ = getPedTargetCollision(localPlayer)
    return ("targetStart=(%s,%s,%s) targetEnd=(%s,%s,%s) collision=(%s,%s,%s)"):format(
        tostring(startX), tostring(startY), tostring(startZ), tostring(endX), tostring(endY), tostring(endZ),
        tostring(hitX), tostring(hitY), tostring(hitZ))
end

local function distanceFromAimSegmentToPoint(targetX, targetY, targetZ)
    local startX, startY, startZ = getPedTargetStart(localPlayer)
    local endX, endY, endZ = getPedTargetEnd(localPlayer)
    if type(startX) ~= "number" or type(endX) ~= "number" then
        return math.huge
    end
    local segmentX, segmentY, segmentZ = endX - startX, endY - startY, endZ - startZ
    local lengthSquared = segmentX * segmentX + segmentY * segmentY + segmentZ * segmentZ
    if lengthSquared < 0.000001 then
        return math.huge
    end
    local offsetX, offsetY, offsetZ = targetX - startX, targetY - startY, targetZ - startZ
    local fraction = (offsetX * segmentX + offsetY * segmentY + offsetZ * segmentZ) / lengthSquared
    fraction = math.max(0, math.min(1, fraction))
    local closestX = startX + segmentX * fraction
    local closestY = startY + segmentY * fraction
    local closestZ = startZ + segmentZ * fraction
    return getDistanceBetweenPoints3D(closestX, closestY, closestZ, targetX, targetY, targetZ)
end

local function reportSprayFailure(spray, result, details)
    if activeSpray ~= spray or spray.reportedFailure then
        return
    end
    spray.reportedFailure = true
    local diagnostic = tostring(details or "") .. " " .. sprayAimTelemetry()
    trace("SPRAY_INPUT_FAILURE", {
        run = spray.runId,
        sprayId = spray.id,
        tagId = spray.tagId,
        player = getPlayerName(localPlayer),
        result = result,
        details = diagnostic,
    })
    triggerServerEvent("tagup:harnessSprayClientResult", resourceRoot, spray.runId, spray.id, spray.tagId, result, diagnostic)
    clearSpray()
end

-- Observe the production state event without mutating its private client
-- table. This gives the co-op barrier an explicit mission/UI completion proof
-- in addition to the native object and synchronized element-data bytes.
addEventHandler("tagup:state", resourceRoot, function(payload)
    if source == resourceRoot and type(payload) == "table" then
        mirroredMissionCompletedTags = type(payload.completedTags) == "table" and payload.completedTags or {}
    end
end)

addEventHandler("tagup:stop", resourceRoot, function()
    mirroredMissionCompletedTags = {}
end)

addEvent("tagup:harnessSprayGroundProbe", true)
addEventHandler("tagup:harnessSprayGroundProbe", resourceRoot, function(runId, sprayId, tagId, object, standX, standY, targetZ, heading)
    if source ~= resourceRoot or type(runId) ~= "number" or type(sprayId) ~= "number" or type(tagId) ~= "number" or
        not isElement(object) or type(standX) ~= "number" or type(standY) ~= "number" or type(targetZ) ~= "number" or
        type(heading) ~= "number" then
        return
    end
    clearSpray()
    local probe = {runId = runId, id = sprayId, tagId = tagId, object = object, timers = {}, startedAt = getTickCount()}
    activeSpray = probe
    probe.timers[#probe.timers + 1] = setTimer(function()
        if activeSpray ~= probe then
            return
        end
        local objectX, objectY = getElementPosition(object)
        local directionX, directionY = standX - objectX, standY - objectY
        local directionLength = math.sqrt(directionX * directionX + directionY * directionY)
        local best
        if directionLength > 0.001 then
            directionX, directionY = directionX / directionLength, directionY / directionLength
            -- A tag can sit behind an awning, stair or roof collision. The
            -- first floor sample is therefore not necessarily the surface
            -- from which vanilla CJ can spray it. Select a nearby point whose
            -- floor leaves the tag origin at normal torso height.
            for _, distance in ipairs({1.75, 2.25, 2.75, 3.25, 4.0, 4.75}) do
                local candidateX = objectX + directionX * distance
                local candidateY = objectY + directionY * distance
                local candidateZ = getGroundPosition(candidateX, candidateY, targetZ + 10.0)
                local height = type(candidateZ) == "number" and targetZ - candidateZ or nil
                if candidateZ ~= 0 and type(height) == "number" and height >= 0.75 and height <= 3.0 then
                    local score = math.abs(height - 1.75) + distance * 0.01
                    if not best or score < best.score then
                        best = {x = candidateX, y = candidateY, z = candidateZ, distance = distance, height = height, score = score}
                    end
                end
            end
        end
        if best then
            trace("SPRAY_GROUND_READY", {
                run = runId,
                sprayId = sprayId,
                tagId = tagId,
                player = getPlayerName(localPlayer),
                groundZ = best.z,
                standX = best.x,
                standY = best.y,
                standoff = best.distance,
                tagHeight = best.height,
            })
            triggerServerEvent("tagup:harnessSprayGroundReady", resourceRoot, runId, sprayId, tagId, "ready", best.z,
                               ("elapsed=%dms standoff=%.2f tagHeight=%.3f"):format(getTickCount() - probe.startedAt,
                                                                                  best.distance, best.height), best.x, best.y)
            clearSpray()
        elseif getTickCount() - probe.startedAt > 10000 then
            triggerServerEvent("tagup:harnessSprayGroundReady", resourceRoot, runId, sprayId, tagId, "timeout", false,
                               "no sprayable ground sample after 10 seconds")
            clearSpray()
        end
    end, 50, 0)
end)

addEvent("tagup:harnessSprayArm", true)
addEventHandler("tagup:harnessSprayArm", resourceRoot,
                function(runId, sprayId, tagId, object, standX, standY, standZ, heading, targetX, targetY, targetZ)
    if source ~= resourceRoot or type(runId) ~= "number" or type(sprayId) ~= "number" or type(tagId) ~= "number" or
        not isElement(object) or type(standX) ~= "number" or type(standY) ~= "number" or type(standZ) ~= "number" or
        type(heading) ~= "number" or type(targetX) ~= "number" or type(targetY) ~= "number" or type(targetZ) ~= "number" then
        return
    end
    clearSpray()
    local spray = {
        runId = runId,
        id = sprayId,
        tagId = tagId,
        object = object,
        standX = standX,
        standY = standY,
        standZ = standZ,
        heading = heading,
        targetX = targetX,
        targetY = targetY,
        targetZ = targetZ,
        timers = {},
        startedAt = getTickCount(),
    }
    activeSpray = spray
    spray.timers[#spray.timers + 1] = setTimer(function()
        if activeSpray ~= spray then
            return
        end
        local px, py, pz = getElementPosition(localPlayer)
        local distance = getDistanceBetweenPoints3D(px, py, pz, spray.standX, spray.standY, spray.standZ)
        local nativeAlpha = type(getObjectGangTagProgress) == "function" and getObjectGangTagProgress(spray.object) or false
        local mirroredAlpha = tonumber(getElementData(spray.object, "tagup.paintAlpha"))
        local ready = isElementStreamedIn(spray.object) and not isPedInVehicle(localPlayer) and getPedWeapon(localPlayer) == TAGUP.sprayWeapon and
                          distance <= 0.75 and type(nativeAlpha) == "number" and nativeAlpha == 0 and mirroredAlpha == 0 and
                          isControlEnabled("aim_weapon") and isControlEnabled("fire")
        if ready and not spray.aimReady then
            setPedCameraRotation(localPlayer, spray.heading)
            spray.aimReady = true
            trace("SPRAY_AIM_READY", {
                run = spray.runId,
                sprayId = spray.id,
                tagId = spray.tagId,
                player = getPlayerName(localPlayer),
                control = "aim_weapon",
                heading = spray.heading,
                distance = distance,
            })
        elseif not spray.aimReady and getTickCount() - spray.startedAt > 10000 then
            return reportSprayFailure(spray, "arming_timeout",
                                      ("streamed=%s vehicle=%s weapon=%s distance=%.3f native=%s mirrored=%s aimEnabled=%s fireEnabled=%s"):format(
                                          tostring(isElementStreamedIn(spray.object)), tostring(isPedInVehicle(localPlayer)),
                                          tostring(getPedWeapon(localPlayer)), distance, tostring(nativeAlpha), tostring(mirroredAlpha),
                                          tostring(isControlEnabled("aim_weapon")), tostring(isControlEnabled("fire"))))
        end

        if spray.aimReady then
            -- Horizontal camera alignment is deterministic; vertical targeting
            -- remains genuine Win32 mouse input from the runner.
            setPedCameraRotation(localPlayer, spray.heading)
            if not spray.aimObserved and getPedControlState(localPlayer, "aim_weapon") then
                spray.aimObserved = true
                triggerServerEvent("tagup:harnessSprayClientResult", resourceRoot, spray.runId, spray.id, spray.tagId,
                                   "aim_observed", sprayAimTelemetry())
            end
            local aimDistance = distanceFromAimSegmentToPoint(spray.targetX, spray.targetY, spray.targetZ)
            if spray.aimObserved and not spray.fireReady and aimDistance <= 0.45 then
                spray.fireReady = true
                trace("SPRAY_FIRE_READY", {
                    run = spray.runId,
                    sprayId = spray.id,
                    tagId = spray.tagId,
                    player = getPlayerName(localPlayer),
                    control = "fire",
                    aimDistance = aimDistance,
                })
            end
            if spray.fireReady and not spray.fireObserved and getPedControlState(localPlayer, "fire") then
                spray.fireObserved = true
                triggerServerEvent("tagup:harnessSprayClientResult", resourceRoot, spray.runId, spray.id, spray.tagId,
                                   "fire_observed", sprayAimTelemetry())
            end
        end
        if getTickCount() - spray.startedAt > 30000 then
            reportSprayFailure(spray, "input_timeout", "physical aim/fire was not observed within 30 seconds")
        end
    end, 25, 0)
end)

addEvent("tagup:harnessSprayStop", true)
addEventHandler("tagup:harnessSprayStop", resourceRoot, function(runId, sprayId, tagId, reason)
    local spray = activeSpray
    if source == resourceRoot and spray and spray.runId == tonumber(runId) and spray.id == tonumber(sprayId) and
        spray.tagId == tonumber(tagId) then
        trace("SPRAY_STOP", {
            run = spray.runId,
            sprayId = spray.id,
            tagId = spray.tagId,
            player = getPlayerName(localPlayer),
            reason = tostring(reason),
        })
        clearSpray()
    end
end)

local function reportMirrorProbe(probe, result, details)
    if activeMirrorProbe ~= probe or probe.reported then
        return
    end
    probe.reported = true
    triggerServerEvent("tagup:harnessTagMirrorResult", resourceRoot, probe.runId, probe.id, probe.tagId, result, details)
    clearMirrorProbe()
end

addEvent("tagup:harnessTagMirrorProbe", true)
addEventHandler("tagup:harnessTagMirrorProbe", resourceRoot, function(runId, sprayId, tagId, object, requiredStableSamples)
    if source ~= resourceRoot or type(runId) ~= "number" or type(sprayId) ~= "number" or type(tagId) ~= "number" or
        not isElement(object) or type(requiredStableSamples) ~= "number" then
        return
    end
    clearMirrorProbe()
    local probe = {
        runId = runId,
        id = sprayId,
        tagId = tagId,
        object = object,
        requiredStableSamples = math.max(1, math.floor(requiredStableSamples)),
        stableSamples = 0,
        startedAt = getTickCount(),
        timers = {},
    }
    activeMirrorProbe = probe
    probe.timers[#probe.timers + 1] = setTimer(function()
        if activeMirrorProbe ~= probe then
            return
        end
        local mirroredAlpha = tonumber(getElementData(probe.object, "tagup.paintAlpha"))
        local nativeAlpha = type(getObjectGangTagProgress) == "function" and getObjectGangTagProgress(probe.object) or false
        local stable = isElementStreamedIn(probe.object) and tonumber(getElementData(probe.object, "tagup.tagId")) == probe.tagId and
                           mirroredAlpha == 255 and nativeAlpha == 255 and mirroredMissionCompletedTags[probe.tagId] == true
        probe.stableSamples = stable and (probe.stableSamples + 1) or 0
        if probe.stableSamples >= probe.requiredStableSamples then
            trace("TAG_REPLICATION_STABLE", {
                run = probe.runId,
                sprayId = probe.id,
                tagId = probe.tagId,
                player = getPlayerName(localPlayer),
                alpha = nativeAlpha,
                samples = probe.stableSamples,
            })
            reportMirrorProbe(probe, "stable",
                              ("native=255 mirrored=255 samples=%d"):format(probe.stableSamples))
        elseif getTickCount() - probe.startedAt > 7000 then
            reportMirrorProbe(probe, "timeout",
                              ("streamed=%s native=%s mirrored=%s completed=%s stableSamples=%d"):format(
                                  tostring(isElementStreamedIn(probe.object)), tostring(nativeAlpha), tostring(mirroredAlpha),
                                  tostring(mirroredMissionCompletedTags[probe.tagId] == true), probe.stableSamples))
        end
    end, 50, 0)
end)

local function reportProbe(probe, result, details)
    if activeProbe ~= probe or probe.reported then
        return
    end
    probe.reported = true
    trace("INPUT_RESULT", {run = probe.runId, probeId = probe.id, player = getPlayerName(localPlayer),
                            control = probe.control, purpose = probe.purpose, result = result, details = details})
    triggerServerEvent("tagup:harnessInputResult", resourceRoot, probe.runId, probe.id, probe.control, result, details)
    clearProbe()
end

addEvent("tagup:harnessInputProbe", true)
addEventHandler("tagup:harnessInputProbe", resourceRoot, function(runId, probeId, control, purpose)
    if source ~= resourceRoot or type(runId) ~= "number" or type(probeId) ~= "number" or
        type(control) ~= "string" or type(purpose) ~= "string" then
        return
    end
    clearProbe()
    local probe = {runId = runId, id = probeId, control = control, purpose = purpose, startedAt = getTickCount()}
    activeProbe = probe
    local enabled = isControlEnabled(control)
    trace("INPUT_READY", {run = runId, probeId = probeId, player = getPlayerName(localPlayer),
                           control = control, purpose = purpose, enabled = enabled})
    if not enabled then
        return reportProbe(probe, "disabled", "isControlEnabled=false")
    end
    probe.pollTimer = setTimer(function()
        if activeProbe ~= probe then
            return
        end
        if not isControlEnabled(probe.control) then
            return reportProbe(probe, "disabled", "control became disabled while awaiting physical input")
        end
        if getPedControlState(localPlayer, probe.control) then
            local vehicle = getPedOccupiedVehicle(localPlayer)
            local details = ("pressed=true vehicle=%s seat=%s"):format(
                tostring(isElement(vehicle) and getElementModel(vehicle) or false),
                tostring(isElement(vehicle) and getPedOccupiedVehicleSeat(localPlayer) or -1))
            reportProbe(probe, "observed", details)
        end
    end, 25, 0)
    probe.timeoutTimer = setTimer(function()
        if activeProbe == probe then
            reportProbe(probe, "timeout", "physical input not observed within 15 seconds")
        end
    end, 15000, 1)
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    clearProbe()
    clearSpray()
    clearMirrorProbe()
end)
