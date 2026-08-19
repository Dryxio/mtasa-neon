local activeSerial = 0

local function send(serial, name, ok, detail)
    triggerServerEvent("ropetest:clientResult", resourceRoot, serial, name, ok == true, tostring(detail or ""))
end

local function vectorDetail(value)
    if not value then return "false" end
    return ("%.3f %.3f %.3f"):format(value.x or 0, value.y or 0, value.z or 0)
end

addEvent("ropetest:inspect", true)
addEventHandler("ropetest:inspect", resourceRoot, function(rope, serial)
    activeSerial = serial
    local valid = isElement(rope) and getElementType(rope) == "rope"
    send(serial, "sync.element", valid, valid and "rope element received" or "missing/invalid rope")
    if not valid then return end

    send(serial, "sync.type", getRopeType(rope) == "swat", getRopeType(rope))
    local remaining = getRopeRemainingTime(rope)
    send(serial, "sync.remaining", type(remaining) == "number" and remaining > 2500 and remaining <= 5000, remaining)

    local velocity = getRopeTopVelocity(rope)
    send(serial, "sync.velocity", velocity and math.abs((velocity.x or 0) - 0.002) < 0.0005, vectorDetail(velocity))

    setTimer(function()
        if serial ~= activeSerial or not isElement(rope) then return end
        local active = isRopeActive(rope)
        send(serial, "native.active", active, tostring(active))
        if active then
            local position, speed = getRopePositionAt(rope, 0.5)
            send(serial, "native.sample-position", position ~= false and position ~= nil, vectorDetail(position))
            send(serial, "native.sample-speed", speed ~= false and speed ~= nil, vectorDetail(speed))
        end
    end, 500, 1)
end)

addEvent("ropetest:holderInspect", true)
addEventHandler("ropetest:holderInspect", resourceRoot, function(rope, holder, serial)
    if serial ~= activeSerial or not isElement(rope) or not isElement(holder) then return end

    local active = isRopeActive(rope)
    send(serial, "holder.active", active, tostring(active))
    if not active then return end

    local top = getRopePositionAt(rope, 0.0)
    local hx, hy, hz = getElementPosition(holder)
    local dx = top and (top.x - hx) or 999
    local dy = top and (top.y - hy) or 999
    local dz = top and (top.z - (hz + 1.5)) or 999
    local error = math.sqrt(dx * dx + dy * dy + dz * dz)
    send(serial, "holder.follow", top and error < 1.25, ("error=%.3f top=%s holder=%.3f %.3f %.3f"):format(error, vectorDetail(top), hx, hy, hz))
end)

addEvent("ropetest:pickupInspect", true)
addEventHandler("ropetest:pickupInspect", resourceRoot, function(rope, object, vehicle, serial)
    if serial ~= activeSerial then return end
    local valid = isElement(rope) and isElement(object) and isElement(vehicle)
    send(serial, "pickup.elements", valid, tostring(valid))
    if not valid then return end

    send(serial, "pickup.synced-state", getRopeCarriedElement(rope) == object, tostring(getRopeCarriedElement(rope)))
    setTimer(function()
        if serial ~= activeSerial or not isElement(rope) then return end
        send(serial, "pickup.native-rope-active", isRopeActive(rope), tostring(isRopeActive(rope)))
    end, 800, 1)
end)

addEvent("ropetest:leasingInspect", true)
addEventHandler("ropetest:leasingInspect", resourceRoot, function(ropes, serial)
    if serial ~= activeSerial then return end
    local logical = 0
    local active = 0
    for _, rope in ipairs(ropes or {}) do
        if isElement(rope) and getElementType(rope) == "rope" then
            logical = logical + 1
            if isRopeActive(rope) then active = active + 1 end
        end
    end
    send(serial, "leasing.logical-client", logical == 12, ("logical=%d"):format(logical))
    send(serial, "leasing.native-cap", active > 0 and active <= 8, ("active=%d logical=%d"):format(active, logical))
end)

addEvent("ropetest:local", true)
addEventHandler("ropetest:local", resourceRoot, function(serial)
    activeSerial = serial
    local x, y, z = getElementPosition(localPlayer)
    local rope = createRope(x + 2, y, z + 4, {
        type = "swat",
        duration = 3500,
        physics = false,
        fixedNode = 0,
        topVelocity = {0, 0.001, 0},
    })

    local valid = isElement(rope) and getElementType(rope) == "rope"
    send(serial, "local.create", valid, tostring(rope))
    if not valid then return end

    send(serial, "local.type-set", setRopeType(rope, "swat") and getRopeType(rope) == "swat", getRopeType(rope))
    send(serial, "local.physics-set", setRopePhysicsEnabled(rope, false) and not isRopePhysicsEnabled(rope), tostring(isRopePhysicsEnabled(rope)))
    send(serial, "local.offset-set", setRopeHolderOffset(rope, Vector3(0, 0, -0.5)), vectorDetail(getRopeHolderOffset(rope)))
    send(serial, "local.velocity-set", setRopeTopVelocity(rope, Vector3(0.001, 0, 0)), vectorDetail(getRopeTopVelocity(rope)))

    setTimer(function()
        if serial ~= activeSerial or not isElement(rope) then return end
        local active = isRopeActive(rope)
        send(serial, "local.active", active, tostring(active))
        if active then
            local position = getRopePositionAt(rope, 0.25)
            send(serial, "local.sample", position ~= false and position ~= nil, vectorDetail(position))
        end
        destroyElement(rope)
    end, 650, 1)
end)

addEvent("ropetest:legacy", true)
addEventHandler("ropetest:legacy", resourceRoot, function(serial)
    activeSerial = serial
    local x, y, z = getElementPosition(localPlayer)
    local ok = createSWATRope(x - 2, y, z + 4, 1500)
    send(serial, "legacy.createSWATRope", ok == true, tostring(ok))
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    outputChatBox("[ROPETEST] /ropetest all | basic | holder | pickup | leasing | local | legacy | late | status | reset", 170, 220, 255)
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    activeSerial = activeSerial + 1
end)
