local observation

local function allOwnedByLocal(state)
    return isElementSyncer(state.driver) and isElementSyncer(state.passenger) and isElementSyncer(state.voodoo)
end

addEvent("cohortHarness:observe", true)
addEventHandler("cohortHarness:observe", resourceRoot, function(id, epoch, owner, driver, passenger, voodoo)
    observation = {
        id = id,
        epoch = epoch,
        owner = owner,
        driver = driver,
        passenger = passenger,
        voodoo = voodoo,
        lastX = nil,
        lastY = nil,
        lastZ = nil,
    }
end)

addEvent("cohortHarness:stop", true)
addEventHandler("cohortHarness:stop", resourceRoot, function(id)
    if observation and observation.id == id then
        observation = nil
    end
end)

setTimer(function()
    local state = observation
    if not state or not isElement(state.owner) or not isElement(state.driver) or not isElement(state.passenger) or
        not isElement(state.voodoo) or not isElementStreamedIn(state.voodoo) then
        return
    end
    local x, y, z = getElementPosition(state.voodoo)
    local moving = state.lastX and getDistanceBetweenPoints3D(x, y, z, state.lastX, state.lastY, state.lastZ) > 0.05 or false
    state.lastX, state.lastY, state.lastZ = x, y, z
    triggerServerEvent("cohortHarness:observerEvidence", resourceRoot, state.id, state.epoch,
                       localPlayer ~= state.owner, allOwnedByLocal(state), moving)
end, 500, 0)
