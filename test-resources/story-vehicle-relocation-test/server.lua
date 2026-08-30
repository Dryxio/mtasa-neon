local run

local function angleError(actual, expected)
    return math.abs((actual - expected + 180) % 360 - 180)
end

local function snapshotVehicle(vehicle, occupants)
    local x, y, z = getElementPosition(vehicle)
    local rx, ry, rz = getElementRotation(vehicle)
    local seats = {}
    for _, occupant in ipairs(occupants) do
        seats[#seats + 1] = {ped = occupant.ped, seat = occupant.seat,
                             interior = getElementInterior(occupant.ped),
                             dimension = getElementDimension(occupant.ped)}
    end
    return {x = x, y = y, z = z, rx = rx, ry = ry, rz = rz,
            interior = getElementInterior(vehicle), dimension = getElementDimension(vehicle),
            frozen = isElementFrozen(vehicle), collisions = getElementCollisionsEnabled(vehicle),
            syncer = getElementSyncer(vehicle), seats = seats}
end

local function verifyVehicleSnapshot(vehicle, expected)
    if not isElement(vehicle) or type(expected) ~= "table" then return false, "rollback snapshot missing" end
    local x, y, z = getElementPosition(vehicle)
    local rx, ry, rz = getElementRotation(vehicle)
    local positionError = math.sqrt((x - expected.x) ^ 2 + (y - expected.y) ^ 2 + (z - expected.z) ^ 2)
    local rxError, ryError, rzError = angleError(rx, expected.rx), angleError(ry, expected.ry),
                                             angleError(rz, expected.rz)
    if positionError > 0.08 or rxError > 0.5 or ryError > 0.5 or rzError > 0.5 then
        return false,
               ("rollback transform expected=(%.4f,%.4f,%.4f;%.3f,%.3f,%.3f) " ..
                   "actual=(%.4f,%.4f,%.4f;%.3f,%.3f,%.3f) error=(%.4f;%.3f,%.3f,%.3f)"):format(
                   expected.x, expected.y, expected.z, expected.rx, expected.ry, expected.rz, x, y, z, rx, ry, rz,
                   positionError, rxError, ryError, rzError)
    end
    if getElementInterior(vehicle) ~= expected.interior or getElementDimension(vehicle) ~= expected.dimension then
        return false, "rollback did not restore interior/dimension"
    end
    if isElementFrozen(vehicle) ~= expected.frozen or
        getElementCollisionsEnabled(vehicle) ~= expected.collisions or getElementSyncer(vehicle) ~= expected.syncer then
        return false, "rollback did not restore physics/syncer policy"
    end
    for _, occupant in ipairs(expected.seats) do
        if not isElement(occupant.ped) or getPedOccupiedVehicle(occupant.ped) ~= vehicle or
            getPedOccupiedVehicleSeat(occupant.ped) ~= occupant.seat or
            getElementInterior(occupant.ped) ~= occupant.interior or
            getElementDimension(occupant.ped) ~= occupant.dimension then
            return false, ("rollback did not restore seat %d"):format(occupant.seat)
        end
    end
    return true
end

local function rollbackRuntimeDiagnostic(snapshot)
    local expected = type(snapshot.rollbackExpected) == "table" and snapshot.rollbackExpected[1] or {}
    local observations = type(snapshot.rollbackObserved) == "table" and snapshot.rollbackObserved or {}
    local server = type(observations.server) == "table" and observations.server[1] or {}
    local clientObserved = {}
    for verifier, values in pairs(observations) do
        if verifier ~= "server" and type(values) == "table" and type(values[1]) == "table" then
            clientObserved = values[1]
            break
        end
    end
    return ("cleanup=%s reason=%s expected=(%s,%s,%s|%s,%s,%s) " ..
               "client=(%s,%s,%s|%s,%s,%s err=%s/%s) server=(%s,%s,%s|%s,%s,%s err=%s/%s)"):format(
                tostring(snapshot.cleanupRestored), tostring(snapshot.reason), tostring(expected.x),
                tostring(expected.y), tostring(expected.z), tostring(expected.rx), tostring(expected.ry),
                tostring(expected.rz), tostring(clientObserved.x), tostring(clientObserved.y),
                tostring(clientObserved.z), tostring(clientObserved.rx), tostring(clientObserved.ry),
                tostring(clientObserved.rz), tostring(clientObserved.positionError),
                tostring(clientObserved.rotationError), tostring(server.x), tostring(server.y), tostring(server.z),
                tostring(server.rx), tostring(server.ry), tostring(server.rz), tostring(server.positionError),
                tostring(server.rotationError))
end

local function createHeadlessLeg(active, phase, request, cancel)
    active.headlessPhase, active.states, active.currentState = phase, {}, nil
    active.cancelExpected, active.cancelRequested, active.cancelIssued = cancel == true, cancel == true, false
    local handle, reason = exports["story-world-runtime"]:createStoryVehicleRelocation({active.player}, active.player,
                                                                                      {request},
                                                                                      {timeout = 20000,
                                                                                       stableSamples = 3})
    if not handle then return false, reason end
    active.handle = handle
    return true
end

local function finish(verdict, details)
    local active = run
    run = nil
    if not active then return end
    local released = isElement(active.handle) and
                         exports["story-world-runtime"]:releaseStoryVehicleRelocation(active.handle)
    if released ~= true or isElement(active.handle) then
        verdict = "FAIL"
        details = ("terminal handle cleanup failed released=%s destroyed=%s"):format(
                      tostring(released), tostring(not isElement(active.handle)))
    end
    if active.headless then
        if isElement(active.player) and isPedInVehicle(active.player) then removePedFromVehicle(active.player) end
        if isElement(active.player) then
            setElementInterior(active.player, active.originalInterior)
            setElementDimension(active.player, active.originalDimension)
            setElementPosition(active.player, active.originalX, active.originalY, active.originalZ)
            setElementRotation(active.player, 0, 0, active.originalHeading)
            if isElement(active.originalVehicle) then
                warpPedIntoVehicle(active.player, active.originalVehicle, active.originalVehicleSeat)
            end
        end
        if isElement(active.vehicle) then destroyElement(active.vehicle) end
        if not isElement(active.player) or isElement(active.vehicle) then
            verdict, details = "FAIL", "headless staging cleanup failed"
        end
    end
    outputServerLog(("[story vehicle relocation test] %s states=%s released=%s details=%s"):format(
                        verdict, table.concat(active.states, ">"), tostring(released), tostring(details or "")))
end

local function validateReady(active, snapshot)
    local expectedStates = {"moving", "verifying", "ready"}
    for index, expectedState in ipairs(expectedStates) do
        if active.states[index] ~= expectedState then
            return false, ("state %d expected %s got %s"):format(index, expectedState,
                                                                  tostring(active.states[index]))
        end
    end
    local vehicle = active.vehicle
    if not isElement(vehicle) or getElementSyncer(vehicle) ~= active.syncer or
        isElementFrozen(vehicle) ~= active.frozen or
        getElementCollisionsEnabled(vehicle) ~= active.collisions then
        return false, "vehicle ownership/physics policy was not restored"
    end
    local expected = snapshot.expected and snapshot.expected[1]
    local finalObserved
    for _, verifierObserved in pairs(snapshot.observed or {}) do
        if verifierObserved[1] then finalObserved = verifierObserved[1] break end
    end
    local x, y, z = getElementPosition(vehicle)
    local rx, ry, rz = getElementRotation(vehicle)
    local vx, vy, vz = getElementVelocity(vehicle)
    local avx, avy, avz = getElementAngularVelocity(vehicle)
    if not expected or not finalObserved or tonumber(finalObserved.sample) < 3 or
        finalObserved.transform ~= true or finalObserved.zeroMotion ~= true or
        (expected.requireGround and (finalObserved.groundHit ~= true or finalObserved.onGround ~= true or
            math.abs(tonumber(finalObserved.bottomClearance) or math.huge) > 0.2)) or
        math.sqrt((x - expected.x) ^ 2 + (y - expected.y) ^ 2 + (z - expected.centerZ) ^ 2) > 0.08 or
        angleError(rx, expected.rx) > 0.5 or angleError(ry, expected.ry) > 0.5 or
        angleError(rz, expected.heading) > 0.5 or getElementInterior(vehicle) ~= expected.interior or
        getElementDimension(vehicle) ~= expected.dimension or
        math.max(math.abs(vx), math.abs(vy), math.abs(vz), math.abs(avx), math.abs(avy), math.abs(avz)) > 0.01 then
        return false, "target transform/ground/three-sample zero-motion proof missing"
    end
    for _, occupant in ipairs(active.occupants) do
        if not isElement(occupant.ped) or getPedOccupiedVehicle(occupant.ped) ~= vehicle or
            getPedOccupiedVehicleSeat(occupant.ped) ~= occupant.seat or
            getElementInterior(occupant.ped) ~= expected.interior or
            getElementDimension(occupant.ped) ~= expected.dimension then
            return false, "declared seat/interior/dimension was not preserved"
        end
    end
    return true
end

addEventHandler("onStoryVehicleRelocationStateChange", root, function(state, snapshot)
    local active = run
    if not active or source ~= active.handle then return end
    active.states[#active.states + 1] = state
    active.currentState = state
    -- `moving` is emitted synchronously immediately before the runtime makes
    -- its first mutation, so this is the same rollback baseline the runtime
    -- owns rather than an earlier, potentially drifting caller snapshot.
    if state == "moving" then active.rollbackSnapshot = snapshotVehicle(active.vehicle, active.occupants) end
    if state == "verifying" and active.cancelRequested and not active.cancelIssued then
        active.cancelIssued = true
        if exports["story-world-runtime"]:releaseStoryVehicleRelocation(active.handle) ~= true then
            return finish("FAIL", "forced cancellation was refused during verifying")
        end
        return
    end
    if state == "failed" then
        if not active.cancelExpected then return finish("FAIL", snapshot.reason) end
        if active.states[1] ~= "moving" or active.states[2] ~= "verifying" or active.states[3] ~= "failed" or
            not active.cancelIssued then
            return finish("FAIL", ("forced cancellation did not traverse moving>verifying>failed: %s"):format(
                              tostring(snapshot.reason)))
        end
        if snapshot.cleanupRestored ~= true then return finish("FAIL", rollbackRuntimeDiagnostic(snapshot)) end
        local restored, restoreReason = verifyVehicleSnapshot(active.vehicle, active.rollbackSnapshot)
        if not restored then
            return finish("FAIL", restoreReason .. "; " .. rollbackRuntimeDiagnostic(snapshot))
        end
        if active.headless and active.headlessPhase == "rollback" then
            active.stagingX, active.stagingY, active.stagingCenterZ = active.rollbackSnapshot.x,
                                                                       active.rollbackSnapshot.y,
                                                                       active.rollbackSnapshot.z
            active.stagingRx, active.stagingRy, active.stagingHeading = active.rollbackSnapshot.rx,
                                                                       active.rollbackSnapshot.ry,
                                                                       active.rollbackSnapshot.rz
            local released = exports["story-world-runtime"]:releaseStoryVehicleRelocation(active.handle)
            if released ~= true or isElement(active.handle) then
                return finish("FAIL", "rollback terminal handle cleanup failed")
            end
            local request = {vehicle = active.vehicle, x = active.target.x, y = active.target.y,
                             scriptZ = active.target.z, heading = active.target.h,
                             occupants = {{ped = active.player, seat = 0}}}
            local created, reason = createHeadlessLeg(active, "outbound", request, false)
            if not created then return finish("FAIL", "outbound relocation create failed: " .. tostring(reason)) end
            return
        end
        return finish("PASS", "forced cancellation rolled back safely")
    end
    if state ~= "ready" then return end
    local valid, validationReason = validateReady(active, snapshot)
    if not valid then return finish("FAIL", validationReason) end
    if active.headless and active.headlessPhase == "outbound" then
        local released = exports["story-world-runtime"]:releaseStoryVehicleRelocation(active.handle)
        if released ~= true or isElement(active.handle) then
            return finish("FAIL", "outbound terminal handle cleanup failed")
        end
        -- A mission commonly parks an occupied vehicle frozen outside the
        -- visible world, then needs to publish it on a road. Exercise that
        -- source state explicitly: the runtime must keep it frozen throughout
        -- ground proof and restore the caller-owned frozen policy at ready.
        setElementFrozen(active.vehicle, true)
        active.frozen = true
        local request = {vehicle = active.vehicle, x = active.frozenTarget.x, y = active.frozenTarget.y,
                         scriptZ = active.frozenTarget.z, heading = active.frozenTarget.h,
                         occupants = {{ped = active.player, seat = 0}}}
        local created, reason = createHeadlessLeg(active, "frozen_source", request, false)
        if not created then
            return finish("FAIL", "frozen-source relocation create failed: " .. tostring(reason))
        end
        return
    elseif active.headless and active.headlessPhase == "frozen_source" then
        local released = exports["story-world-runtime"]:releaseStoryVehicleRelocation(active.handle)
        if released ~= true or isElement(active.handle) then
            return finish("FAIL", "frozen-source terminal handle cleanup failed")
        end
        setElementFrozen(active.vehicle, false)
        active.frozen = false
        local request = {vehicle = active.vehicle, x = active.stagingX, y = active.stagingY,
                         centerZ = active.stagingCenterZ, rx = active.stagingRx, ry = active.stagingRy,
                         heading = active.stagingHeading,
                         occupants = {{ped = active.player, seat = 0}}, requireGround = false}
        local created, reason = createHeadlessLeg(active, "return", request, false)
        if not created then return finish("FAIL", "return relocation create failed: " .. tostring(reason)) end
        return
    elseif active.headless and active.headlessPhase == "return" then
        return finish("PASS", "headless rollback, outbound, frozen-source and return barriers completed")
    end
    finish("PASS", "long-distance occupied relocation stable")
end)

addCommandHandler("storyrelocatetest", function(player, _, x, y, targetZ, heading, zMode)
    if run then return outputServerLog("[story vehicle relocation test] FAIL run already active") end
    if not isElement(player) or not isPedInVehicle(player) then
        return outputServerLog("[story vehicle relocation test] FAIL command player must occupy a vehicle")
    end
    x, y, targetZ, heading = tonumber(x), tonumber(y), tonumber(targetZ), tonumber(heading)
    if not x or not y or not targetZ then
        return outputServerLog("[story vehicle relocation test] usage: storyrelocatetest x y targetZ [heading] [center]")
    end
    local vehicle, occupants = getPedOccupiedVehicle(player), {}
    for seat, ped in pairs(getVehicleOccupants(vehicle) or {}) do
        occupants[#occupants + 1] = {ped = ped, seat = tonumber(seat)}
    end
    local participants = getElementsByType("player")
    local request = {vehicle = vehicle, x = x, y = y, heading = heading, occupants = occupants}
    request[zMode == "center" and "centerZ" or "scriptZ"] = targetZ
    local options = {timeout = 20000, stableSamples = 3}
    local handle, reason = exports["story-world-runtime"]:createStoryVehicleRelocation(participants, player,
                                                                                      {request}, options)
    if not handle then return outputServerLog("[story vehicle relocation test] FAIL create: " .. tostring(reason)) end
    local overlapping = exports["story-world-runtime"]:createStoryVehicleRelocation(participants, player, {request},
                                                                                    options)
    if overlapping then
        exports["story-world-runtime"]:releaseStoryVehicleRelocation(overlapping)
        exports["story-world-runtime"]:releaseStoryVehicleRelocation(handle)
        return outputServerLog("[story vehicle relocation test] FAIL overlapping reservation was accepted")
    end
    local originalX, originalY, originalZ = getElementPosition(vehicle)
    run = {handle = handle, vehicle = vehicle, occupants = occupants, syncer = getElementSyncer(vehicle),
           frozen = isElementFrozen(vehicle), collisions = getElementCollisionsEnabled(vehicle), states = {},
           originalX = originalX, originalY = originalY, originalZ = originalZ}
    outputServerLog(("[story vehicle relocation test] START distance-target=(%.3f,%.3f,%.3f) occupants=%d"):format(
                        x, y, targetZ, #occupants))
end)

addCommandHandler("storyrelocatetestcancel", function()
    if not run or not isElement(run.handle) then
        return outputServerLog("[story vehicle relocation test] FAIL no active relocation to cancel")
    end
    if run.currentState == "ready" or run.currentState == "failed" then
        return outputServerLog("[story vehicle relocation test] FAIL relocation is already terminal")
    end
    run.cancelExpected, run.cancelRequested = true, true
    if run.currentState ~= "verifying" then
        return outputServerLog("[story vehicle relocation test] cancellation queued for verifying barrier")
    end
    run.cancelIssued = true
    if exports["story-world-runtime"]:releaseStoryVehicleRelocation(run.handle) ~= true then
        finish("FAIL", "forced cancellation was refused during verifying")
    end
end)

addCommandHandler("storyrelocatetestheadless", function(player)
    if run then return outputServerLog("[story vehicle relocation test] FAIL run already active") end
    player = isElement(player) and getElementType(player) == "player" and player or getElementsByType("player")[1]
    if not isElement(player) then
        return outputServerLog("[story vehicle relocation test] FAIL no connected player for headless profile")
    end
    local originalVehicle = getPedOccupiedVehicle(player)
    local originalVehicleSeat = isElement(originalVehicle) and getPedOccupiedVehicleSeat(player) or nil
    if isElement(originalVehicle) then removePedFromVehicle(player) end
    local px, py, pz = getElementPosition(player)
    local _, _, heading = getElementRotation(player)
    local interior, dimension = getElementInterior(player), getElementDimension(player)
    local stagingX, stagingY, stagingCenterZ = px + 3, py, pz + 0.6
    local vehicle = createVehicle(481, stagingX, stagingY, stagingCenterZ, 0, 0, heading)
    if not vehicle then return outputServerLog("[story vehicle relocation test] FAIL BMX creation refused") end
    setElementInterior(vehicle, interior)
    setElementDimension(vehicle, dimension)
    setElementSyncer(vehicle, player, true)
    setElementFrozen(vehicle, true)
    setElementCollisionsEnabled(vehicle, false)
    if not warpPedIntoVehicle(player, vehicle, 0) then
        destroyElement(vehicle)
        setElementPosition(player, px, py, pz)
        return outputServerLog("[story vehicle relocation test] FAIL BMX staging warp refused")
    end
    setElementCollisionsEnabled(vehicle, true)
    setElementFrozen(vehicle, false)
    local distanceA = getDistanceBetweenPoints2D(px, py, 970.0873, -1107.7755)
    local target = distanceA >= 1000 and {x = 970.0873, y = -1107.7755, z = 22.8672, h = 82.97} or
                       {x = 2487.3093, y = -1668.3717, z = 12.3438, h = 80}
    local frozenTarget = distanceA >= 1000 and {x = 2487.3093, y = -1668.3717, z = 12.3438, h = 80} or
                             {x = 970.0873, y = -1107.7755, z = 22.8672, h = 82.97}
    run = {headless = true, headlessPhase = "staging", player = player, vehicle = vehicle, states = {},
           originalX = px, originalY = py, originalZ = pz, originalHeading = heading,
           originalVehicle = originalVehicle, originalVehicleSeat = originalVehicleSeat,
           originalInterior = interior, originalDimension = dimension, stagingX = stagingX, stagingY = stagingY,
           stagingCenterZ = stagingCenterZ, stagingHeading = heading, target = target, syncer = player, frozen = false,
           collisions = true, occupants = {{ped = player, seat = 0}}, frozenTarget = frozenTarget}
    setTimer(function()
        if not run or run.vehicle ~= vehicle then return end
        local request = {vehicle = vehicle, x = target.x, y = target.y, scriptZ = target.z, heading = target.h,
                         occupants = {{ped = player, seat = 0}}}
        local created, reason = createHeadlessLeg(run, "rollback", request, true)
        if not created then return finish("FAIL", "rollback probe create failed: " .. tostring(reason)) end
        outputServerLog(("[story vehicle relocation test] HEADLESS START rollback+outbound target=(%.3f,%.3f,%.3f)"):format(
                            target.x, target.y, target.z))
    end, 500, 1)
end)

addEventHandler("onResourceStop", resourceRoot, function()
    if run and isElement(run.handle) then exports["story-world-runtime"]:releaseStoryVehicleRelocation(run.handle) end
    run = nil
end)
