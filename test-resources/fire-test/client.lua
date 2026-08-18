local activeSerial = 0
local cancelDamage = false

local function send(serial, name, ok, detail)
    triggerServerEvent("firetest:clientResult", resourceRoot, serial, name, ok == true, tostring(detail or ""))
end

addEvent("firetest:inspect", true)
addEventHandler("firetest:inspect", resourceRoot, function(fire, serial)
    activeSerial = serial
    local ok = isElement(fire) and getElementType(fire) == "fire"
    send(serial, "sync.element", ok, ok and "fire element received" or "missing/invalid fire")
    if not ok then return end

    local strength = getFireStrength(fire)
    local remaining = getFireRemainingTime(fire)
    local mask = getFireDamageTargets(fire)
    send(serial, "sync.strength", type(strength) == "number" and math.abs(strength - 2.5) < 0.01, strength)
    send(serial, "sync.remaining", type(remaining) == "number" and remaining > 2500 and remaining <= 4500, remaining)
    send(serial, "sync.damage-mask", type(mask) == "table" and mask.players and not mask.peds and mask.vehicles and not mask.objects,
        type(mask) == "table" and ("p=%s ped=%s v=%s o=%s"):format(tostring(mask.players), tostring(mask.peds), tostring(mask.vehicles), tostring(mask.objects)) or tostring(mask))
end)

addEventHandler("onClientFireDamage", root, function(victim, damage, responsibleElement)
    if cancelDamage then
        cancelEvent()
    end
end)

local function destroyIfElement(element)
    if isElement(element) then
        destroyElement(element)
    end
end

addEvent("firetest:localPolicy", true)
addEventHandler("firetest:localPolicy", resourceRoot, function(serial)
    activeSerial = serial
    local px, py, pz = getElementPosition(localPlayer)
    local ped = createPed(7, px + 4, py, pz)
    if not isElement(ped) then
        send(serial, "local.ped-create", false, "createPed failed")
        return
    end

    setElementDimension(ped, getElementDimension(localPlayer))
    setElementInterior(ped, getElementInterior(localPlayer))

    local fire = createFire(px + 4, py, pz, {
        duration = 4500,
        strength = 1.0,
        damage = true,
        damageTargets = {players = false, peds = false, vehicles = false, objects = false},
        source = localPlayer,
    })

    if not isElement(fire) then
        send(serial, "local.fire-create", false, "managed local createFire failed")
        destroyIfElement(ped)
        return
    end

    setElementDimension(fire, getElementDimension(localPlayer))
    setElementInterior(fire, getElementInterior(localPlayer))
    send(serial, "local.create", getElementType(fire) == "fire", getElementType(fire))

    setTimer(function()
        if serial ~= activeSerial then
            destroyIfElement(fire)
            destroyIfElement(ped)
            return
        end

        send(serial, "damage.mask-block", not isElementOnFire(ped), tostring(isElementOnFire(ped)))
        setFireDamageTargets(fire, {players = false, peds = true, vehicles = false, objects = false})
        cancelDamage = true
        setElementOnFire(ped, false)

        setTimer(function()
            if serial ~= activeSerial then return end
            send(serial, "damage.cancel-event", not isElementOnFire(ped), tostring(isElementOnFire(ped)))
            cancelDamage = false
            setElementOnFire(ped, false)

            setTimer(function()
                if serial ~= activeSerial then return end
                send(serial, "damage.allowed", isElementOnFire(ped), tostring(isElementOnFire(ped)))
                destroyIfElement(fire)
                destroyIfElement(ped)
            end, 1000, 1)
        end, 1000, 1)
    end, 1000, 1)
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    cancelDamage = false
    activeSerial = activeSerial + 1
end)
