local harness
local harnessSerial = 0

local expectedStages = {
    sweet1a = 1,
    intro = 2,
    enter_car = 3,
    drive_idlewood = 4,
    demo = 5,
    tags_idlewood = 6,
    return_car = 7,
    drive_ballas = 8,
    ballas_departure = 9,
    tags_ballas = 10,
    rooftop = 11,
    return_after_roof = 12,
    drive_home = 13,
    final_scene = 14,
    complete = 15,
}

local stageTimeouts = {
    sweet1a = 90000,
    intro = 120000,
    enter_car = 30000,
    drive_idlewood = 60000,
    demo = 120000,
    tags_idlewood = 120000,
    return_car = 30000,
    drive_ballas = 60000,
    ballas_departure = 90000,
    tags_ballas = 180000,
    rooftop = 150000,
    return_after_roof = 30000,
    drive_home = 60000,
    final_scene = 120000,
    complete = 30000,
}

local requestProfiles = {
    ["tagup-transition-natural-1.request"] = {profile = "solo-natural", expectedPlayers = 1, skipScenes = false},
    ["tagup-transition-natural-2.request"] = {profile = "coop-natural", expectedPlayers = 2, skipScenes = false},
    ["tagup-transition-skip-1.request"] = {profile = "solo-skip", expectedPlayers = 1, skipScenes = true},
    ["tagup-transition-skip-2.request"] = {profile = "coop-skip", expectedPlayers = 2, skipScenes = true},
    ["tagup-transition-native-natural-1.request"] = {
        profile = "solo-native-natural",
        expectedPlayers = 1,
        skipScenes = false,
        nativeTags = true,
    },
    ["tagup-transition-native-natural-2.request"] = {
        profile = "coop-native-natural",
        expectedPlayers = 2,
        skipScenes = false,
        nativeTags = true,
    },
    ["tagup-transition-native-skip-1.request"] = {
        profile = "solo-native-skip",
        expectedPlayers = 1,
        skipScenes = true,
        nativeTags = true,
    },
    ["tagup-transition-native-skip-2.request"] = {
        profile = "coop-native-skip",
        expectedPlayers = 2,
        skipScenes = true,
        nativeTags = true,
    },
}

local NATIVE_TAG_STANDOFF = 1.75
local NATIVE_TAG_GROUND_OFFSET = 1.0
local NATIVE_TAG_TIMEOUT = 45000
local NATIVE_TAG_STABLE_SAMPLES = 3

local function encodeTrace(record)
    return tostring(toJSON(record, true)):gsub("[\r\n]", "")
end

local function trace(event, data)
    local record = {
        event = event,
        run = harness and harness.id or nil,
        profile = harness and harness.profile or nil,
        stage = harness and harness.stage or nil,
        tick = getTickCount(),
    }
    for key, value in pairs(type(data) == "table" and data or {}) do
        if type(value) ~= "userdata" then
            record[key] = value
        end
    end
    outputServerLog("[tagging-up-turf-harness-jsonl] " .. encodeTrace(record))
end

local function rememberHarnessTimer(timer)
    if harness and isTimer(timer) then
        harness.timers[#harness.timers + 1] = timer
    end
    return timer
end

local function clearHarnessTimers()
    if not harness then
        return
    end
    for _, timer in ipairs(harness.timers) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    harness.timers = {}
end

local function snapshotPlayer(player)
    local x, y, z = getElementPosition(player)
    return {
        x = x,
        y = y,
        z = z,
        interior = getElementInterior(player),
        dimension = getElementDimension(player),
        model = getElementModel(player),
        frozen = isElementFrozen(player),
        collisions = getElementCollisionsEnabled(player),
        health = getElementHealth(player),
        armor = getPedArmor(player),
        money = getPlayerMoney(player),
    }
end

local function getMission()
    return type(TAGUP_HARNESS_INTERNAL) == "table" and TAGUP_HARNESS_INTERNAL.getMission() or nil
end

local function abortMission()
    local mission = getMission()
    if mission and mission.running and isElement(mission.leader) then
        TAGUP_HARNESS_INTERNAL.abortMission("Harness aborted: transition validation failed.")
    end
end

local function finishHarness(ok, reason)
    if not harness or harness.terminal then
        return
    end
    harness.terminal = true
    local profile = harness.profile
    local elapsed = getTickCount() - harness.startedAt
    trace(ok and "PASS" or "FAIL", {ok = ok, reason = reason, elapsed = elapsed})
    outputServerLog(("[tagging-up-turf-harness] %s profile=%s elapsed=%dms reason=%s"):format(
                        ok and "PASS" or "FAIL", profile, elapsed, tostring(reason or "-")))
    local mission = getMission()
    if harness.spray and mission and isElement(mission.leader) then
        triggerClientEvent(mission.leader, "tagup:harnessSprayStop", resourceRoot, harness.id, harness.spray.id,
                           harness.spray.tagId, ok and "harness_complete" or "harness_failed")
    end
    if not ok then
        abortMission()
    end
    clearHarnessTimers()
    harness = nil
end

local function failHarness(reason)
    finishHarness(false, reason)
end

local function isHarnessPlayer(player)
    if not harness or not isElement(player) then
        return false
    end
    for playerElement in pairs(harness.snapshots) do
        if playerElement == player then
            return true
        end
    end
    return false
end

local function missionParty()
    local mission = getMission()
    local result = {}
    if not mission then
        return result
    end
    for _, player in ipairs(mission.party or {}) do
        if isElement(player) then
            result[#result + 1] = player
        end
    end
    return result
end

local function allControlsRestored(player)
    for _, control in ipairs({"forwards", "accelerate", "vehicle_left", "vehicle_right", "enter_exit", "aim_weapon", "fire"}) do
        if not isControlEnabled(player, control) then
            return false, control
        end
    end
    return true
end

local function restoredPlayerMatches(player, expected)
    if not isElement(player) or isPedInVehicle(player) or getElementInterior(player) ~= expected.interior or
        getElementDimension(player) ~= expected.dimension or getElementModel(player) ~= expected.model or
        isElementFrozen(player) ~= expected.frozen or getElementCollisionsEnabled(player) ~= expected.collisions then
        return false, "state"
    end
    local x, y, z = getElementPosition(player)
    if getDistanceBetweenPoints3D(x, y, z, expected.x, expected.y, expected.z) > 0.2 then
        return false, "position"
    end
    if math.abs(getElementHealth(player) - expected.health) > 0.1 or math.abs(getPedArmor(player) - expected.armor) > 0.1 then
        return false, "health_or_armor"
    end
    if getPlayerMoney(player) ~= expected.money + 200 then
        return false, "reward"
    end
    return allControlsRestored(player)
end

local function allPlayersReported(probe)
    for _, player in ipairs(probe.players) do
        if not isElement(player) or not probe.reported[player] then
            return false
        end
    end
    return true
end

local function requestInputProbe(control, purpose, callback, playersOverride)
    if not harness or harness.inputProbe then
        return false
    end
    local players = {}
    local seen = {}
    for _, player in ipairs(playersOverride or missionParty()) do
        if not isElement(player) or getElementType(player) ~= "player" or seen[player] then
            failHarness("invalid or duplicate player in input probe " .. tostring(purpose))
            return false
        end
        seen[player] = true
        players[#players + 1] = player
    end
    harness.probeSerial = harness.probeSerial + 1
    local probe = {
        id = harness.probeSerial,
        control = control,
        purpose = purpose,
        players = players,
        reported = {},
        callback = callback,
    }
    if #probe.players ~= harness.expectedPlayers then
        failHarness(("input probe %s expected %d players, got %d"):format(purpose, harness.expectedPlayers, #probe.players))
        return false
    end
    harness.inputProbe = probe
    trace("INPUT_READY", {probeId = probe.id, control = control, purpose = purpose, players = #probe.players})
    for _, player in ipairs(probe.players) do
        triggerClientEvent(player, "tagup:harnessInputProbe", resourceRoot, harness.id, probe.id, control, purpose)
    end
    probe.timeout = rememberHarnessTimer(setTimer(function(runId, probeId)
        if harness and harness.id == runId and harness.inputProbe and harness.inputProbe.id == probeId then
            failHarness(("physical input timeout purpose=%s control=%s"):format(purpose, control))
        end
    end, 45000, 1, harness.id, probe.id))
    return true
end

addEvent("tagup:harnessInputResult", true)
addEventHandler("tagup:harnessInputResult", resourceRoot, function(runId, probeId, control, result, details)
    local player = client
    local probe = harness and harness.inputProbe
    if source ~= resourceRoot or not harness or harness.id ~= tonumber(runId) or not probe or probe.id ~= tonumber(probeId) or
        probe.control ~= control or probe.reported[player] then
        return
    end
    local member = false
    for _, candidate in ipairs(probe.players) do
        if candidate == player then
            member = true
            break
        end
    end
    if not member then
        return
    end
    trace("INPUT_RESULT", {probeId = probe.id, control = control, purpose = probe.purpose,
                            player = getPlayerName(player), result = result, details = tostring(details or "")})
    if result ~= "observed" then
        return failHarness(("physical input refused player=%s purpose=%s result=%s"):format(
                               getPlayerName(player), probe.purpose, tostring(result)))
    end
    probe.reported[player] = true
    if allPlayersReported(probe) then
        if isTimer(probe.timeout) then
            killTimer(probe.timeout)
        end
        harness.inputProbe = nil
        trace("INPUT_PASS", {probeId = probe.id, control = control, purpose = probe.purpose})
        if probe.callback then
            probe.callback()
        end
    end
end)

local function seatPartyInGreenwood()
    local mission = getMission()
    local vehicle = mission and mission.entities and mission.entities.vehicle
    local sweet = mission and mission.entities and mission.entities.sweet
    if not mission or not isElement(vehicle) or not isElement(sweet) or not isElement(mission.leader) then
        return false, "mission vehicle or actors unavailable"
    end
    removePedFromVehicle(sweet)
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            removePedFromVehicle(player)
            setElementFrozen(player, false)
        end
    end
    if not warpPedIntoVehicle(sweet, vehicle, 1) or not warpPedIntoVehicle(mission.leader, vehicle, 0) then
        return false, "driver or Sweet seat refused"
    end
    local seat = 2
    for _, player in ipairs(mission.party) do
        if player ~= mission.leader and isElement(player) then
            if not warpPedIntoVehicle(player, vehicle, seat) then
                return false, "companion seat " .. tostring(seat) .. " refused"
            end
            seat = seat + 1
        end
    end
    setElementSyncer(sweet, mission.leader, true, true)
    setElementSyncer(vehicle, mission.leader, true, true)
    return true
end

local function stageVehicleAt(target, heading, purpose)
    local mission = getMission()
    local vehicle = mission and mission.entities and mission.entities.vehicle
    if not isElement(vehicle) then
        return failHarness("vehicle unavailable for " .. purpose)
    end
    local seated, reason = seatPartyInGreenwood()
    if not seated then
        return failHarness(reason)
    end
    setElementFrozen(vehicle, true)
    setElementVelocity(vehicle, 0, 0, 0)
    setElementAngularVelocity(vehicle, 0, 0, 0)
    setElementPosition(vehicle, target[1], target[2], target[3] + 2.0)
    setElementRotation(vehicle, 8, 0, heading)
    trace("STAGING", {purpose = purpose, x = target[1], y = target[2], z = target[3] + 2.0,
                       contract = "physical 09D0 remains authoritative"})
    rememberHarnessTimer(setTimer(function(runId, stagedVehicle)
        if harness and harness.id == runId and isElement(stagedVehicle) then
            setElementVelocity(stagedVehicle, 0, 0, 0)
            setElementAngularVelocity(stagedVehicle, 0, 0, 0)
            setElementFrozen(stagedVehicle, false)
        end
    end, 750, 1, harness.id, vehicle))
end

local function stagePartyAtBallasTrigger()
    local mission = getMission()
    if not mission or not isElement(mission.leader) then
        return failHarness("leader unavailable for Ballas trigger")
    end
    local positions = {
        {TAGUP.ballasGangScene.trigger.x, TAGUP.ballasGangScene.trigger.y, 23.97},
        {TAGUP.ballasGangScene.trigger.x - 1.5, TAGUP.ballasGangScene.trigger.y - 1.5, 23.97},
        {TAGUP.ballasGangScene.trigger.x - 1.5, TAGUP.ballasGangScene.trigger.y + 1.5, 23.97},
    }
    for index, player in ipairs(mission.party) do
        if isElement(player) then
            removePedFromVehicle(player)
            local position = positions[index]
            setElementPosition(player, position[1], position[2], position[3])
            setElementRotation(player, 0, 0, 90)
            setElementFrozen(player, false)
        end
    end
    trace("STAGING", {purpose = "ballas_native_encounter", x = positions[1][1], y = positions[1][2], z = positions[1][3]})
end

local function stageLeaderAtBallasApproach()
    local mission = getMission()
    if not mission or not isElement(mission.leader) then
        return failHarness("leader unavailable for Ballas approach")
    end
    local approach = TAGUP.ballasGangScene.approach
    setElementPosition(mission.leader, approach.x, approach.y, 23.97)
    setElementRotation(mission.leader, 0, 0, 90)
    setElementFrozen(mission.leader, false)
    trace("STAGING", {purpose = "ballas_5x5_approach", x = approach.x, y = approach.y, z = 23.97})
end

local function getNativeTagPlacement(tag)
    local radians = math.rad(tag.rotation)
    return tag.x - math.cos(radians) * NATIVE_TAG_STANDOFF,
           tag.y - math.sin(radians) * NATIVE_TAG_STANDOFF,
           (tag.rotation + 270) % 360
end

local function beginNativeTag(tagId)
    local mission = getMission()
    local tag = tagupGetTag(tagId)
    local object = mission and mission.entities and mission.entities["tag" .. tostring(tagId)]
    if not harness or not harness.nativeTags or harness.spray or not mission or not isElement(mission.leader) or not tag or not isElement(object) then
        return failHarness("native tag staging unavailable for tag " .. tostring(tagId))
    end

    local active = false
    if mission.stage == "tags_idlewood" then
        active = tag.group == "idlewood"
    elseif mission.stage == "tags_ballas" then
        active = tag.group == "ballas" and not mission.ballasGangScene
    elseif mission.stage == "rooftop" then
        active = tag.group == "rooftop"
    end
    if not active or mission.completedTags[tagId] or tonumber(mission.tagProgress[tagId]) ~= 0 then
        return failHarness(("tag %d is not a fresh objective at stage %s"):format(tagId, tostring(mission.stage)))
    end

    local standX, standY, heading = getNativeTagPlacement(tag)
    harness.spraySerial = harness.spraySerial + 1
    local spray = {
        id = harness.spraySerial,
        tagId = tagId,
        tag = tag,
        object = object,
        stage = mission.stage,
        standX = standX,
        standY = standY,
        heading = heading,
        startedAt = getTickCount(),
        lastProgress = 0,
        lastProgressAt = getTickCount(),
        acceptedAlpha = 0,
        acceptedSteps = 0,
        mirrorReports = {},
    }
    harness.spray = spray

    removePedFromVehicle(mission.leader)
    setElementFrozen(mission.leader, true)
    setElementVelocity(mission.leader, 0, 0, 0)
    setElementPosition(mission.leader, standX, standY, tag.z + 3.0)
    setElementRotation(mission.leader, 0, 0, heading)
    trace("NATIVE_TAG_STAGING", {
        sprayId = spray.id,
        tagId = tagId,
        x = standX,
        y = standY,
        heading = heading,
        targetX = tag.x,
        targetY = tag.y,
        targetZ = tag.z,
        contract = "ground probe then physical RMB/LMB; no synthetic progress",
    })
    triggerClientEvent(mission.leader, "tagup:harnessSprayGroundProbe", resourceRoot, harness.id, spray.id, tagId, object,
                       standX, standY, tag.z, heading)
end

local function beginNextNativeTag(completedTagId)
    if completedTagId == 1 then
        beginNativeTag(2)
    elseif completedTagId == 3 then
        beginNativeTag(4)
    end
end

local function completeNativeTagMirrorBarrier(spray)
    if not harness or harness.spray ~= spray then
        return
    end
    harness.nativeTagsCompleted = harness.nativeTagsCompleted + 1
    trace("NATIVE_TAG_REPLICATION_PASS", {
        sprayId = spray.id,
        tagId = spray.tagId,
        clients = harness.expectedPlayers,
        completed = harness.nativeTagsCompleted,
        total = 5,
        stableSamples = NATIVE_TAG_STABLE_SAMPLES,
    })
    harness.spray = nil
    rememberHarnessTimer(setTimer(function(runId, completedTagId)
        if harness and harness.id == runId then
            beginNextNativeTag(completedTagId)
        end
    end, 250, 1, harness.id, spray.tagId))
end

addEvent("tagup:harnessSprayGroundReady", true)
addEventHandler("tagup:harnessSprayGroundReady", resourceRoot, function(runId, sprayId, tagId, result, groundZ, details, standX, standY)
    local player = client
    local mission = getMission()
    local spray = harness and harness.spray
    if source ~= resourceRoot or not harness or harness.id ~= tonumber(runId) or not spray or spray.id ~= tonumber(sprayId) or
        spray.tagId ~= tonumber(tagId) or not mission or player ~= mission.leader or spray.groundReady then
        return
    end
    local standoff = type(standX) == "number" and type(standY) == "number" and
                         getDistanceBetweenPoints2D(standX, standY, spray.tag.x, spray.tag.y) or nil
    local tagHeight = type(groundZ) == "number" and spray.tag.z - groundZ or nil
    if result ~= "ready" or type(groundZ) ~= "number" or groundZ ~= groundZ or type(standoff) ~= "number" or
        standoff < NATIVE_TAG_STANDOFF - 0.05 or standoff > 5.0 or type(tagHeight) ~= "number" or tagHeight < 0.75 or tagHeight > 3.0 then
        return failHarness(("tag %d ground probe failed: %s (%s)"):format(spray.tagId, tostring(result), tostring(details or groundZ)))
    end

    spray.groundReady = true
    spray.groundZ = groundZ
    spray.standX = standX
    spray.standY = standY
    setElementPosition(player, spray.standX, spray.standY, groundZ + NATIVE_TAG_GROUND_OFFSET)
    setElementRotation(player, 0, 0, spray.heading)
    setElementVelocity(player, 0, 0, 0)
    setPedWeaponSlot(player, getSlotFromWeapon(TAGUP.sprayWeapon))
    setElementFrozen(player, false)
    trace("NATIVE_TAG_GROUND_PASS", {
        sprayId = spray.id,
        tagId = spray.tagId,
        groundZ = groundZ,
        standX = spray.standX,
        standY = spray.standY,
        standoff = standoff,
        tagHeight = tagHeight,
    })
    triggerClientEvent(player, "tagup:harnessSprayArm", resourceRoot, harness.id, spray.id, spray.tagId, spray.object,
                       spray.standX, spray.standY, groundZ + NATIVE_TAG_GROUND_OFFSET, spray.heading,
                       spray.tag.x, spray.tag.y, spray.tag.z)
end)

addEvent("tagup:harnessSprayClientResult", true)
addEventHandler("tagup:harnessSprayClientResult", resourceRoot, function(runId, sprayId, tagId, result, details)
    local player = client
    local mission = getMission()
    local spray = harness and harness.spray
    if source ~= resourceRoot or not harness or harness.id ~= tonumber(runId) or not spray or spray.id ~= tonumber(sprayId) or
        spray.tagId ~= tonumber(tagId) or not mission or player ~= mission.leader then
        return
    end
    trace("NATIVE_TAG_INPUT_RESULT", {
        sprayId = spray.id,
        tagId = spray.tagId,
        player = getPlayerName(player),
        result = tostring(result),
        details = tostring(details or ""),
    })
    if result == "aim_observed" then
        spray.aimObserved = true
    elseif result == "fire_observed" then
        if not spray.aimObserved then
            return failHarness("fire observed before physical aim for tag " .. tostring(spray.tagId))
        end
        spray.fireObserved = true
        spray.fireObservedAt = getTickCount()
        -- Physical vertical alignment can legitimately take several seconds.
        -- The native-progress stall budget begins only once LMB is observed.
        spray.lastProgressAt = spray.fireObservedAt
    else
        failHarness(("tag %d client input failed: %s (%s)"):format(spray.tagId, tostring(result), tostring(details or "")))
    end
end)

addEvent("tagup:harnessTagMirrorResult", true)
addEventHandler("tagup:harnessTagMirrorResult", resourceRoot, function(runId, sprayId, tagId, result, details)
    local player = client
    local spray = harness and harness.spray
    if source ~= resourceRoot or not harness or harness.id ~= tonumber(runId) or not spray or spray.id ~= tonumber(sprayId) or
        spray.tagId ~= tonumber(tagId) or not spray.awaitingMirrors or not isHarnessPlayer(player) or spray.mirrorReports[player] then
        return
    end
    trace("NATIVE_TAG_REPLICATION_RESULT", {
        sprayId = spray.id,
        tagId = spray.tagId,
        player = getPlayerName(player),
        result = tostring(result),
        details = tostring(details or ""),
    })
    if result ~= "stable" then
        return failHarness(("tag %d replication failed on %s: %s"):format(spray.tagId, getPlayerName(player), tostring(details or result)))
    end
    spray.mirrorReports[player] = true
    for playerElement in pairs(harness.snapshots) do
        if not isElement(playerElement) then
            return failHarness("player disconnected during native tag replication barrier")
        end
        if not spray.mirrorReports[playerElement] then
            return
        end
    end
    completeNativeTagMirrorBarrier(spray)
end)

local function isExactNativeAlphaStep(previousAlpha, currentAlpha)
    previousAlpha = tonumber(previousAlpha)
    currentAlpha = tonumber(currentAlpha)
    return previousAlpha and currentAlpha and previousAlpha == math.floor(previousAlpha) and currentAlpha == math.floor(currentAlpha) and
               previousAlpha >= 0 and previousAlpha < 255 and currentAlpha == math.min(previousAlpha + 8, 255)
end

-- This handler runs after server.lua's ordinary validator. It never accepts or
-- mutates progress: it counts an event only when the production mission table
-- already contains that exact native step. A debug skip therefore cannot
-- satisfy the 32-step proof even if it sets the final byte to 255.
addEventHandler("tagup:nativeTagProgress", resourceRoot, function(targetObject, creator, previousAlpha, currentAlpha)
    local player = client
    local mission = getMission()
    local spray = harness and harness.nativeTags and harness.spray
    if source ~= resourceRoot or not spray or not mission or player ~= mission.leader or creator ~= player or
        targetObject ~= spray.object or not isExactNativeAlphaStep(previousAlpha, currentAlpha) then
        return
    end
    previousAlpha = tonumber(previousAlpha)
    currentAlpha = tonumber(currentAlpha)
    if previousAlpha ~= spray.acceptedAlpha or tonumber(mission.tagProgress[spray.tagId]) ~= currentAlpha then
        return
    end
    spray.acceptedAlpha = currentAlpha
    spray.acceptedSteps = spray.acceptedSteps + 1
    trace("NATIVE_TAG_ACCEPTED_STEP", {
        sprayId = spray.id,
        tagId = spray.tagId,
        step = spray.acceptedSteps,
        previous = previousAlpha,
        current = currentAlpha,
        creator = getPlayerName(player),
    })
end, false, "low")

local function updateNativeTag(mission)
    local spray = harness and harness.spray
    if not spray then
        return
    end
    if not isElement(spray.object) or not isElement(mission.leader) then
        return failHarness("native tag object or leader disappeared for tag " .. tostring(spray.tagId))
    end
    if getTickCount() - spray.startedAt > NATIVE_TAG_TIMEOUT then
        return failHarness(("native tag %d timed out at alpha %s"):format(spray.tagId, tostring(mission.tagProgress[spray.tagId])))
    end

    local progress = tonumber(mission.tagProgress[spray.tagId]) or 0
    if progress < spray.lastProgress or progress < 0 or progress > 255 then
        return failHarness(("native tag %d regressed from %d to %s"):format(spray.tagId, spray.lastProgress, tostring(progress)))
    end
    if progress ~= spray.lastProgress then
        trace("NATIVE_TAG_PROGRESS", {
            sprayId = spray.id,
            tagId = spray.tagId,
            previousObserved = spray.lastProgress,
            current = progress,
            note = "server.lua accepted only exact native +8 steps with the final 255 cap",
        })
        spray.lastProgress = progress
        spray.lastProgressAt = getTickCount()
    elseif spray.fireObserved and progress == 0 and getTickCount() - spray.fireObservedAt > 8000 then
        return failHarness("no native alpha step within 8 seconds for tag " .. tostring(spray.tagId))
    elseif spray.fireObserved and progress < 255 and getTickCount() - spray.lastProgressAt > 8000 then
        return failHarness(("native tag %d stalled at alpha %d"):format(spray.tagId, progress))
    end

    if not spray.awaitingMirrors and mission.completedTags[spray.tagId] and progress == 255 then
        if not spray.aimObserved or not spray.fireObserved then
            return failHarness("tag " .. tostring(spray.tagId) .. " completed without observed physical RMB/LMB")
        end
        if spray.acceptedSteps ~= 32 or spray.acceptedAlpha ~= 255 then
            return failHarness(("tag %d reached 255 without the exact accepted chain: steps=%d acceptedAlpha=%d"):format(
                                   spray.tagId, spray.acceptedSteps, spray.acceptedAlpha))
        end
        spray.awaitingMirrors = true
        spray.completedAt = getTickCount()
        trace("NATIVE_TAG_COMPLETE", {
            sprayId = spray.id,
            tagId = spray.tagId,
            alpha = progress,
            acceptedSteps = spray.acceptedSteps,
            creator = getPlayerName(mission.leader),
        })
        triggerClientEvent(mission.leader, "tagup:harnessSprayStop", resourceRoot, harness.id, spray.id, spray.tagId, "authoritative_complete")
        for playerElement in pairs(harness.snapshots) do
            if isElement(playerElement) then
                triggerClientEvent(playerElement, "tagup:harnessTagMirrorProbe", resourceRoot, harness.id, spray.id, spray.tagId,
                                   spray.object, NATIVE_TAG_STABLE_SAMPLES)
            end
        end
    elseif spray.awaitingMirrors and getTickCount() - spray.completedAt > 8000 then
        return failHarness("native tag replication barrier timed out for tag " .. tostring(spray.tagId))
    end
end

local function skipCurrentStage(reason)
    local mission = getMission()
    if not mission or not isElement(mission.leader) or not TAGUP_HARNESS_INTERNAL.skipMissionStage(mission.leader) then
        return failHarness("skip refused: " .. tostring(reason))
    end
    trace("HARNESS_SKIP", {reason = reason})
end

local function beginStageAction(stage)
    if not harness or harness.actions[stage] then
        return
    end
    harness.actions[stage] = true
    if stage == "enter_car" then
        rememberHarnessTimer(setTimer(function(runId)
            if not harness or harness.id ~= runId or harness.stage ~= "enter_car" then
                return
            end
            local seated, reason = seatPartyInGreenwood()
            if not seated then
                failHarness(reason)
            end
        end, 500, 1, harness.id))
    elseif stage == "drive_idlewood" then
        requestInputProbe("accelerate", "drive_idlewood_controls", function()
            stageVehicleAt(TAGUP.idlewoodDestination, 270, "idlewood_arrival")
        end)
    elseif stage == "tags_idlewood" then
        if harness.nativeTags then
            beginNativeTag(1)
        else
            requestInputProbe("fire", "idlewood_native_spray_input", function()
                trace("COVERAGE_GAP", {opcode = "SET_TAG_STATUS_IN_AREA", reason = "A-Z transition harness does not inject native tag alpha; isolated/manual spray remains required"})
                skipCurrentStage("idlewood tags after physical fire probe")
            end)
        end
    elseif stage == "return_car" then
        rememberHarnessTimer(setTimer(function(runId)
            if harness and harness.id == runId and harness.stage == "return_car" then
                local seated, reason = seatPartyInGreenwood()
                if not seated then
                    failHarness(reason)
                end
            end
        end, 500, 1, harness.id))
    elseif stage == "drive_ballas" then
        requestInputProbe("accelerate", "drive_ballas_controls", function()
            stageVehicleAt(TAGUP.ballasDestination, 270, "ballas_arrival")
        end)
    elseif stage == "tags_ballas" then
        stagePartyAtBallasTrigger()
    elseif stage == "rooftop" then
        if harness.nativeTags then
            beginNativeTag(5)
        else
            requestInputProbe("fire", "rooftop_native_spray_input", function()
                trace("COVERAGE_GAP", {opcode = "SET_TAG_STATUS_IN_AREA", reason = "A-Z transition harness does not inject native tag alpha; isolated/manual spray remains required"})
                skipCurrentStage("rooftop tag after physical fire probe")
            end)
        end
    elseif stage == "return_after_roof" then
        rememberHarnessTimer(setTimer(function(runId)
            if harness and harness.id == runId and harness.stage == "return_after_roof" then
                local seated, reason = seatPartyInGreenwood()
                if not seated then
                    failHarness(reason)
                end
            end
        end, 500, 1, harness.id))
    elseif stage == "drive_home" then
        requestInputProbe("accelerate", "drive_home_controls", function()
            stageVehicleAt(TAGUP.homeDestination, 180, "grove_arrival")
        end)
    end
end

local function updateSceneSkips(mission)
    if not harness or not harness.skipScenes then
        return
    end
    if harness.stage == "sweet1a" and mission.fileCutscene and not mission.fileCutscene.skipRequested then
        local started = 0
        for _, player in ipairs(mission.party) do
            if mission.fileCutscene.startedPlayers[player] then
                started = started + 1
            end
        end
        if started == harness.expectedPlayers then
            skipCurrentStage("profile file-cutscene skip")
        end
    elseif harness.stage == "intro" and mission.introScene and mission.introScene.started and
        getTickCount() - harness.stageStartedAt >= 750 then
        skipCurrentStage("profile intro skip")
    elseif harness.stage == "tags_ballas" and mission.ballasGangScene and mission.ballasGangScene.skippable and
        not mission.ballasGangScene.finalCheckRequested then
        skipCurrentStage("profile Ballas scene skip")
    elseif harness.stage == "final_scene" and mission.finalScene and mission.finalScene.skippable and
        not mission.finalScene.releasing then
        skipCurrentStage("profile finale skip")
    end
end

local function updateBallasStage(mission)
    if not harness or harness.stage ~= "tags_ballas" then
        return
    end
    if mission.ballasGangScene then
        return
    end
    local encounter = mission.ballasEncounter
    if not mission.ballasGangSceneCompleted or not encounter then
        return
    end
    if encounter.phase == "awaiting_approach" and not harness.ballasApproachStaged then
        harness.ballasApproachStaged = true
        stageLeaderAtBallasApproach()
    elseif encounter.phase == "attacking" and encounter.attackReady and not harness.ballasTagsRequested then
        harness.ballasTagsRequested = true
        if harness.nativeTags then
            beginNativeTag(3)
        else
            requestInputProbe("fire", "ballas_native_spray_input", function()
                trace("COVERAGE_GAP", {opcode = "SET_TAG_STATUS_IN_AREA", reason = "A-Z transition harness does not inject native tag alpha; isolated/manual spray remains required"})
                skipCurrentStage("Ballas tags after native encounter and physical fire probe")
            end)
        end
    end
end

local function verifyCleanup()
    if not harness then
        return
    end
    if harness.cleanupProbeRequested then
        return
    end
    local players = {}
    for player, expected in pairs(harness.snapshots) do
        local matched, reason = restoredPlayerMatches(player, expected)
        if not matched then
            return failHarness(("cleanup mismatch player=%s field=%s"):format(getPlayerName(player), tostring(reason)))
        end
        players[#players + 1] = player
    end
    harness.cleanupProbeRequested = true
    requestInputProbe("forwards", "post_cleanup_controls", function()
        local nativeTagProof = harness.nativeTags and (", native tags=" .. tostring(harness.nativeTagsCompleted) .. "/5") or ""
        if harness.nativeTags and harness.nativeTagsCompleted ~= 5 then
            return failHarness("mission completed without five native tag replication barriers")
        end
        finishHarness(true, "complete graph, physical control probes, native scene barriers, reward and cleanup passed" .. nativeTagProof)
    end, players)
end

local function updateHarness()
    if not harness then
        return
    end
    local mission = getMission()
    if not mission then
        return failHarness("internal mission surface unavailable")
    end
    if getTickCount() - harness.startedAt > 1200000 then
        return failHarness("global timeout")
    end
    if harness.sawComplete and not mission.running then
        if not harness.cleanupReadyAt then
            harness.cleanupReadyAt = getTickCount() + 750
            trace("CLEANUP_SETTLE", {delay = 750})
            return
        end
        if getTickCount() >= harness.cleanupReadyAt then
            return verifyCleanup()
        end
        return
    end
    if not mission.running then
        return failHarness("mission stopped before complete")
    end
    if mission.stage == "failed" then
        return failHarness("mission entered failed stage")
    end
    if harness.nativeTags then
        updateNativeTag(mission)
        if not harness then
            return
        end
    end
    if mission.stage ~= harness.stage then
        local previousIndex = harness.stage and expectedStages[harness.stage] or 0
        local nextIndex = expectedStages[mission.stage]
        if not nextIndex or nextIndex ~= previousIndex + 1 then
            return failHarness(("unexpected stage transition %s -> %s"):format(tostring(harness.stage), tostring(mission.stage)))
        end
        harness.stage = mission.stage
        harness.stageStartedAt = getTickCount()
        trace("STAGE", {index = nextIndex})
        if mission.stage == "complete" then
            harness.sawComplete = true
        else
            beginStageAction(mission.stage)
        end
    elseif harness.stage and getTickCount() - harness.stageStartedAt > (stageTimeouts[harness.stage] or 60000) then
        return failHarness("stage timeout: " .. harness.stage)
    end
    updateSceneSkips(mission)
    updateBallasStage(mission)
end

local function startHarness(profile, expectedPlayers, skipScenes, preferredLeaderName, nativeTags)
    local function reject(reason, data)
        local record = {
            event = "FAIL",
            profile = profile,
            ok = false,
            reason = reason,
            tick = getTickCount(),
        }
        for key, value in pairs(type(data) == "table" and data or {}) do
            record[key] = value
        end
        outputServerLog("[tagging-up-turf-harness-jsonl] " .. encodeTrace(record))
        outputServerLog(("[tagging-up-turf-harness] FAIL profile=%s reason=%s"):format(profile, reason))
        return false
    end

    if harness then
        return reject("run_already_active")
    end
    if type(TAGUP_HARNESS_INTERNAL) ~= "table" then
        return reject("internal_surface_unavailable")
    end
    local mission = getMission()
    local players = getElementsByType("player")
    if mission.running or #players ~= expectedPlayers then
        return reject(("admission_expected_%d_actual_%d_running_%s"):format(
                          expectedPlayers, #players, tostring(mission.running)), {
            expectedPlayers = expectedPlayers,
            actualPlayers = #players,
            missionRunning = mission.running,
        })
    end
    for _, player in ipairs(players) do
        if isPedDead(player) or isElementFrozen(player) or isPedInVehicle(player) then
            return reject("player_not_admissible", {player = getPlayerName(player)})
        end
    end
    local leader = players[1]
    if type(preferredLeaderName) == "string" and preferredLeaderName ~= "" then
        leader = nil
        for _, player in ipairs(players) do
            if getPlayerName(player) == preferredLeaderName then
                leader = player
                break
            end
        end
        if not leader then
            return reject("preferred_leader_not_connected", {player = preferredLeaderName})
        end
    end
    harnessSerial = harnessSerial + 1
    harness = {
        id = harnessSerial,
        profile = profile,
        expectedPlayers = expectedPlayers,
        skipScenes = skipScenes == true,
        nativeTags = nativeTags == true,
        nativeTagsCompleted = 0,
        spraySerial = 0,
        startedAt = getTickCount(),
        stage = nil,
        stageStartedAt = getTickCount(),
        actions = {},
        snapshots = {},
        timers = {},
        probeSerial = 0,
    }
    for _, player in ipairs(players) do
        harness.snapshots[player] = snapshotPlayer(player)
    end
    trace("START", {expectedPlayers = expectedPlayers, skipScenes = harness.skipScenes, nativeTags = harness.nativeTags,
                     leader = getPlayerName(leader),
                     mainScmSha256 = "601def3baae766ce6a23e2f0b9b48f6b33c9a64e2fc32eb4f22ddea8b868b0fa"})
    trace("TRANSVERSAL_GAP", {opcode = "SET_CHAR_DECISION_MAKER", value = "DM_PED_MISSION_EMPTY", approximation = false})
    trace("TRANSVERSAL_GAP", {opcode = "SET_CHAR_CAN_BE_SHOT_IN_VEHICLE", value = false, approximation = false})
    TAGUP_HARNESS_INTERNAL.startMission(leader)
    rememberHarnessTimer(setTimer(updateHarness, 100, 0))
    return true
end

local function readPreferredLeader(path)
    local handle = fileOpen(path, true)
    if not handle then
        return nil
    end
    local size = fileGetSize(handle)
    local contents = size > 0 and fileRead(handle, size) or ""
    fileClose(handle)
    return type(contents) == "string" and contents:match("leader=([^\r\n]+)") or nil
end

setTimer(function()
    if harness then
        return
    end
    for path, options in pairs(requestProfiles) do
        if fileExists(path) then
            local preferredLeader = readPreferredLeader(path)
            fileDelete(path)
            startHarness(options.profile, options.expectedPlayers, options.skipScenes, preferredLeader, options.nativeTags)
            return
        end
    end
end, 250, 0)

addEventHandler("onResourceStop", resourceRoot, function()
    if harness and not harness.terminal then
        trace("FAIL", {ok = false, reason = "resource stopped during harness"})
        outputServerLog("[tagging-up-turf-harness] FAIL profile=" .. tostring(harness.profile) .. " reason=resource_stopped")
    end
    clearHarnessTimers()
    harness = nil
end)

outputServerLog("[tagging-up-turf-harness] Ready: create tagup-transition-{natural|skip|native-natural|native-skip}-{1|2}.request in the deployed resource.")
