local observation
local mutationAcks = {}

local function healthOf(vehicle)
    if not isElement(vehicle) or not isElementStreamedIn(vehicle) then
        return nil
    end
    return getElementHealth(vehicle)
end

addEvent("vehicleHealthHarness:sample", true)
addEventHandler("vehicleHealthHarness:sample", resourceRoot,
                function(runId, generation, requestId, phase, vehicle, owner)
    if source ~= resourceRoot then
        return
    end

    local streamed = isElement(vehicle) and isElementStreamedIn(vehicle)
    local data = {
        streamed = streamed,
        health = healthOf(vehicle),
        syncer = streamed and isElementSyncer(vehicle) or false,
        occupied = streamed and getVehicleOccupant(vehicle, 0) == owner or false,
        dimension = isElement(vehicle) and getElementDimension(vehicle) or -1,
        interior = isElement(vehicle) and getElementInterior(vehicle) or -1,
    }
    triggerServerEvent("vehicleHealthHarness:sampleAck", resourceRoot, runId, generation, requestId, phase,
                       data)
end)

addEvent("vehicleHealthHarness:mutate", true)
addEventHandler("vehicleHealthHarness:mutate", resourceRoot,
                function(runId, generation, mutationId, phase, vehicle, expectedHealth)
    if source ~= resourceRoot then
        return
    end

    local key = table.concat({tostring(runId), tostring(generation), tostring(mutationId)}, ":")
    local cached = mutationAcks[key]
    if cached then
        triggerServerEvent("vehicleHealthHarness:mutationAck", resourceRoot, runId, generation, mutationId,
                           phase, cached)
        return
    end

    local streamed = isElement(vehicle) and isElementStreamedIn(vehicle)
    local before = healthOf(vehicle)
    local syncer = streamed and isElementSyncer(vehicle) or false
    local accepted = false
    if streamed and syncer then
        -- This must be a local authority mutation. A server-side health RPC
        -- would bypass the puresync packing path that this harness protects.
        accepted = setElementHealth(vehicle, expectedHealth) == true
    end
    local ack = {
        streamed = streamed,
        syncer = syncer,
        accepted = accepted,
        before = before,
        after = healthOf(vehicle),
    }
    mutationAcks[key] = ack
    triggerServerEvent("vehicleHealthHarness:mutationAck", resourceRoot, runId, generation, mutationId, phase,
                       ack)
end)

addEvent("vehicleHealthHarness:begin", true)
addEventHandler("vehicleHealthHarness:begin", resourceRoot, function(runId, generation)
    if source ~= resourceRoot then
        return
    end
    observation = {runId = runId, generation = generation}
    mutationAcks = {}
end)

addEvent("vehicleHealthHarness:stop", true)
addEventHandler("vehicleHealthHarness:stop", resourceRoot, function(runId)
    if source ~= resourceRoot then
        return
    end
    if observation and observation.runId == runId then
        observation = nil
        mutationAcks = {}
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    observation = nil
    mutationAcks = {}
end)
