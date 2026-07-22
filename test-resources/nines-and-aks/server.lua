local mission = {
    running = false,
    finishing = false,
    player = nil,
    stage = nil,
    snapshot = nil,
    entities = {},
    timers = {},
    rangeRound = 0,
    rangeHits = {},
    bincoEntered = false,
    bincoExited = false,
}

local function rememberTimer(timer)
    mission.timers[#mission.timers + 1] = timer
    return timer
end

local function clearTimers()
    for _, timer in ipairs(mission.timers) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    mission.timers = {}
end

local function snapshotClothes(player)
    local clothes = {}
    for slot = 0, 17 do
        local texture, model = getPedClothes(player, slot)
        if type(texture) == "string" and type(model) == "string" then
            clothes[slot] = {texture, model}
        end
    end
    return clothes
end

local function snapshotPlayer(player)
    local x, y, z = getElementPosition(player)
    local _, _, rz = getElementRotation(player)
    local weapons = {}
    for slot = 0, 12 do
        local weapon, ammo = getPedWeapon(player, slot), getPedTotalAmmo(player, slot)
        if weapon and weapon > 0 and ammo and ammo > 0 then
            weapons[#weapons + 1] = {weapon, ammo}
        end
    end
    return {
        position = {x, y, z, rz}, interior = getElementInterior(player), dimension = getElementDimension(player),
        model = getElementModel(player), health = getElementHealth(player), armor = getPedArmor(player),
        clothes = snapshotClothes(player), weapons = weapons,
    }
end

local function clearClothes(player)
    for slot = 0, 17 do
        if getPedClothes(player, slot) then
            removePedClothes(player, slot)
        end
    end
end

local function applyCJ(player)
    setElementModel(player, 0)
    clearClothes(player)
    for _, clothing in ipairs(NINES.cj.clothes) do
        addPedClothes(player, clothing[1], clothing[2], clothing[3])
    end
end

local function restorePlayer(player, snapshot, passed)
    if not isElement(player) or not snapshot then
        return
    end
    removePedFromVehicle(player)
    setElementFrozen(player, false)
    toggleAllControls(player, true, true, true)
    setElementModel(player, 0)
    clearClothes(player)
    for slot, clothing in pairs(snapshot.clothes) do
        addPedClothes(player, clothing[1], clothing[2], slot)
    end
    setElementModel(player, snapshot.model)
    takeAllWeapons(player)
    for _, weapon in ipairs(snapshot.weapons) do
        giveWeapon(player, weapon[1], weapon[2], false)
    end
    if passed then
        giveWeapon(player, 22, 60, false)
        setElementInterior(player, 0)
        setElementDimension(player, snapshot.dimension)
        setElementPosition(player, NINES.binco.outsideExit[1], NINES.binco.outsideExit[2], NINES.binco.outsideExit[3])
        setElementRotation(player, 0, 0, NINES.binco.outsideExit[4])
    else
        setElementInterior(player, snapshot.interior)
        setElementDimension(player, snapshot.dimension)
        setElementPosition(player, snapshot.position[1], snapshot.position[2], snapshot.position[3])
        setElementRotation(player, 0, 0, snapshot.position[4])
    end
    setElementHealth(player, math.max(1, snapshot.health))
    setPedArmor(player, snapshot.armor)
end

local function destroyEntities()
    for _, element in pairs(mission.entities) do
        if isElement(element) then
            destroyElement(element)
        end
    end
    mission.entities = {}
end

local function resetMission()
    mission.running = false
    mission.finishing = false
    mission.player = nil
    mission.stage = nil
    mission.snapshot = nil
    mission.rangeRound = 0
    mission.rangeHits = {}
    mission.bincoEntered = false
    mission.bincoExited = false
end

local function cleanup(reason, restore, passed)
    if not mission.running then
        return
    end
    local player, snapshot = mission.player, mission.snapshot
    clearTimers()
    if isElement(player) then
        triggerClientEvent(player, "nines:stop", resourceRoot, reason)
    end
    destroyEntities()
    if restore and isElement(player) then
        restorePlayer(player, snapshot, passed == true)
    end
    outputDebugString(("[nines-and-aks] Cleanup: %s"):format(tostring(reason)))
    resetMission()
end

local function fail(reason, key)
    if not mission.running or mission.finishing then
        return
    end
    mission.finishing = true
    mission.stage = "failed"
    outputDebugString(("[nines-and-aks] Mission failed: %s"):format(reason), 1)
    if isElement(mission.player) then
        triggerClientEvent(mission.player, "nines:failed", resourceRoot, key or "M_FAIL", reason)
    end
    rememberTimer(setTimer(function()
        cleanup(reason, true, false)
    end, 5000, 1))
end

local function configureActor(ped, role)
    setElementDimension(ped, NINES.dimension)
    setElementData(ped, "nines.missionActor", true, true)
    setElementData(ped, "nines.role", role, true)
    setElementSyncer(ped, mission.player, true)
end

local function createOpeningWorld()
    local carProfile, smokeProfile = NINES.glendale, NINES.smoke
    local car = createVehicle(carProfile.model, carProfile.position[1], carProfile.position[2], carProfile.position[3], 0, 0,
                              carProfile.position[4], carProfile.plate)
    setElementDimension(car, NINES.dimension)
    setVehicleColor(car, carProfile.primary[1], carProfile.primary[2], carProfile.primary[3], carProfile.secondary[1],
                    carProfile.secondary[2], carProfile.secondary[3])
    setElementData(car, "nines.role", "glendale", true)
    setElementSyncer(car, mission.player, true)

    local smoke = createPed(smokeProfile.model, smokeProfile.position[1], smokeProfile.position[2], smokeProfile.position[3],
                            smokeProfile.position[4])
    setElementHealth(smoke, smokeProfile.health)
    configureActor(smoke, "smoke")
    mission.entities.glendale, mission.entities.smoke = car, smoke
    return car, smoke
end

local function createRangeWorld()
    local emmetProfile, tampaProfile = NINES.emmet, NINES.tampa
    local emmet = createPed(emmetProfile.model, emmetProfile.position[1], emmetProfile.position[2], emmetProfile.position[3],
                            emmetProfile.position[4])
    setElementHealth(emmet, emmetProfile.health)
    configureActor(emmet, "emmet")
    local tampa = createVehicle(tampaProfile.model, tampaProfile.position[1], tampaProfile.position[2], tampaProfile.position[3], 0, 0,
                                tampaProfile.position[4], tampaProfile.plate)
    setElementDimension(tampa, NINES.dimension)
    setElementData(tampa, "nines.role", "tampa", true)
    setElementSyncer(tampa, mission.player, true)
    setVehicleLocked(tampa, true)
    -- MTA disables the petrol-cap weakpoint on server vehicles by default;
    -- sweet2 relies on the stock one-shot tank explosion once proofs are removed.
    setVehicleFuelTankExplodable(tampa, true)
    setVehicleDamageProof(tampa, true)
    mission.entities.emmet, mission.entities.tampa = emmet, tampa
end

local function stageRangeActors()
    local smoke, emmet, car = mission.entities.smoke, mission.entities.emmet, mission.entities.glendale
    if isElement(smoke) then
        removePedFromVehicle(smoke)
        setElementPosition(smoke, 2450.4653, -1980.5774, 13.5469)
        setElementRotation(smoke, 0, 0, 79.2792)
    end
    if isElement(emmet) then
        setElementPosition(emmet, 2451.8340, -1976.8108, 13.5469)
        setElementRotation(emmet, 0, 0, 75.6292)
    end
    if isElement(car) then
        setElementPosition(car, 2452.6460, -2003.8763, 13.0576026)
        setElementRotation(car, 0, 0, 100.2244)
    end
    setElementPosition(mission.player, 2453.8206, -1978.7771, 13.5469)
    setElementRotation(mission.player, 0, 0, 89.5159)
end

local function stageEmmetDeparture()
    removePedFromVehicle(mission.player)
    setPedWeaponSlot(mission.player, 0)
    setElementPosition(mission.player, 2450.5669, -1975.6414, 13.5469)
    setElementRotation(mission.player, 0, 0, 288.25)
    if isElement(mission.entities.smoke) then
        removePedFromVehicle(mission.entities.smoke)
        setElementPosition(mission.entities.smoke, 2452.7415, -1976.4500, 13.5469)
        setElementRotation(mission.entities.smoke, 0, 0, 345.3302)
    end
    if isElement(mission.entities.emmet) then
        setElementPosition(mission.entities.emmet, 2452.8459, -1975.1003, 13.5469)
        setElementRotation(mission.entities.emmet, 0, 0, 160.25)
    end
    if isElement(mission.entities.glendale) then
        setElementPosition(mission.entities.glendale, 2452.6460, -2003.8763, 13.0576026)
        setElementRotation(mission.entities.glendale, 0, 0, 100.2244)
    end
end

local function stageSmokeGoodbye()
    removePedFromVehicle(mission.player)
    setElementPosition(mission.player, 2071.5537, -1704.3748, 13.5547)
    setElementRotation(mission.player, 0, 0, 12.5)
    if isElement(mission.entities.smoke) then
        removePedFromVehicle(mission.entities.smoke)
        setElementPosition(mission.entities.smoke, 2071.1694, -1703.0597, 13.5547)
        setElementRotation(mission.entities.smoke, 0, 0, 162.35)
    end
end

local function validClient()
    return client and client == mission.player and source == resourceRoot and mission.running and not mission.finishing
end

local function beginMission(player)
    if mission.running then
        outputChatBox("Nines and AK's est deja en cours.", player, 255, 180, 80)
        return
    end
    mission.running, mission.player, mission.stage = true, player, "intro"
    mission.snapshot = snapshotPlayer(player)
    applyCJ(player)
    setElementInterior(player, 0)
    setElementDimension(player, NINES.dimension)
    setElementPosition(player, NINES.cj.position[1], NINES.cj.position[2], NINES.cj.position[3])
    setElementRotation(player, 0, 0, NINES.cj.position[4])
    triggerClientEvent(player, "nines:start", resourceRoot)
    triggerClientEvent(player, "nines:cutscene", resourceRoot, "intro", NINES.cutscenes.intro)
    outputDebugString(("[nines-and-aks] Started for %s"):format(getPlayerName(player)))
end

addCommandHandler(NINES.command, beginMission)
addCommandHandler("ninesandaks", beginMission)

addEvent("nines:cutsceneFinished", true)
addEventHandler("nines:cutsceneFinished", resourceRoot, function(kind, result)
    if not validClient() then
        return
    end
    if result ~= "finished" and result ~= "skipped" then
        return fail(("%s cutscene %s"):format(kind, tostring(result)), "M_FAIL")
    end
    if kind == "intro" and mission.stage == "intro" then
        mission.stage = "drive_emmet"
        local car, smoke = createOpeningWorld()
        triggerClientEvent(client, "nines:drive", resourceRoot, "emmet", car, smoke)
    elseif kind == "emmet" and mission.stage == "emmet_cutscene" then
        mission.stage = "range"
        createRangeWorld()
        stageRangeActors()
        giveWeapon(client, 22, 1, true)
        setWeaponAmmo(client, 22, 30000)
        giveWeapon(mission.entities.smoke, 22, 10000, true)
        triggerClientEvent(client, "nines:range", resourceRoot, mission.entities)
    end
end)

addEvent("nines:arrived", true)
addEventHandler("nines:arrived", resourceRoot, function(destination)
    if not validClient() then
        return
    end
    if destination == "emmet" and mission.stage == "drive_emmet" then
        mission.stage = "emmet_cutscene"
        removePedFromVehicle(mission.entities.smoke)
        removePedFromVehicle(client)
        triggerClientEvent(client, "nines:cutscene", resourceRoot, "emmet", NINES.cutscenes.emmet)
    elseif destination == "smoke" and mission.stage == "drive_smoke" then
        mission.stage = "goodbye"
        stageSmokeGoodbye()
        triggerClientEvent(client, "nines:goodbye", resourceRoot, mission.entities)
    end
end)

addEvent("nines:rangeFinished", true)
addEventHandler("nines:rangeFinished", resourceRoot, function()
    local hitCount = 0
    for _ in pairs(mission.rangeHits) do
        hitCount = hitCount + 1
    end
    if not validClient() or mission.stage ~= "range" or mission.rangeRound ~= 3 or hitCount ~= 5 then
        return
    end
    mission.stage = "gas_tank"
    local tampa, emmet, car = mission.entities.tampa, mission.entities.emmet, mission.entities.glendale
    setElementPosition(tampa, 2446.49, -1966.47, 13.0441911)
    setElementRotation(tampa, 0, 0, 101.85)
    setElementPosition(emmet, 2452.6899, -1980.2181, 13.5469)
    setElementRotation(emmet, 0, 0, 44.7926)
    setElementPosition(car, 2452.6460, -2003.8763, 13.0576026)
    setElementRotation(car, 0, 0, 100.2244)
    setElementPosition(client, 2446.11, -1974.53, 13.54)
    setElementRotation(client, 0, 0, 349.81)
    -- Vanilla keeps the Tampa proofed for both scripted shots and releases it
    -- at the same five-second barrier that restores player control.
    rememberTimer(setTimer(function()
        if mission.running and mission.stage == "gas_tank" and isElement(mission.entities.tampa) then
            setVehicleDamageProof(mission.entities.tampa, false)
        end
    end, 5000, 1))
    triggerClientEvent(client, "nines:gasTank", resourceRoot, mission.entities)
end)

addEvent("nines:stagePlayerRound", true)
addEventHandler("nines:stagePlayerRound", resourceRoot, function(round)
    round = tonumber(round)
    if not validClient() or mission.stage ~= "range" or not round or round < 1 or round > 3 then
        return
    end
    if round ~= mission.rangeRound + 1 then
        return
    end
    if round > 1 then
        local previousCount = 0
        for _ in pairs(mission.rangeHits) do
            previousCount = previousCount + 1
        end
        if previousCount ~= ({1, 3, 5})[round - 1] then
            return
        end
    end
    mission.rangeRound, mission.rangeHits = round, {}
    setElementPosition(client, 2450.7402, -1978.3749, 13.5469)
    setElementRotation(client, 0, 0, 89.5159)
    setWeaponAmmo(client, 22, 30000)
    triggerClientEvent(client, "nines:playerRoundReady", resourceRoot, round)
end)

addEvent("nines:stageDemoRound", true)
addEventHandler("nines:stageDemoRound", resourceRoot, function(round)
    round = tonumber(round)
    if not validClient() or mission.stage ~= "range" or not round or round < 2 or round > 3 or
        round ~= mission.rangeRound + 1 then
        return
    end
    local previousCount = 0
    for _ in pairs(mission.rangeHits) do
        previousCount = previousCount + 1
    end
    if previousCount ~= ({1, 3})[round - 1] then
        return
    end
    if isElement(mission.entities.glendale) then
        setElementPosition(mission.entities.glendale, 2452.6460, -2003.8763, 13.0576026)
        setElementRotation(mission.entities.glendale, 0, 0, 100.2244)
    end
    setElementPosition(client, 2453.8206, -1978.7771, 13.5469)
    setElementRotation(client, 0, 0, 89.5159)
    triggerClientEvent(client, "nines:demoRoundReady", resourceRoot, round)
end)

addEvent("nines:bottleHit", true)
addEventHandler("nines:bottleHit", resourceRoot, function(round, index)
    round, index = tonumber(round), tonumber(index)
    local expected = ({1, 3, 5})[mission.rangeRound]
    if not validClient() or mission.stage ~= "range" or round ~= mission.rangeRound or not index or not expected or
        index < 1 or index > expected or mission.rangeHits[index] then
        return
    end
    mission.rangeHits[index] = true
end)

addEvent("nines:rangePresence", true)
addEventHandler("nines:rangePresence", resourceRoot, function(outside)
    if not validClient() or (mission.stage ~= "range" and mission.stage ~= "gas_tank") then
        return
    end
    setWeaponAmmo(client, 22, outside == true and 10 or 30000)
end)

addEvent("nines:tampaDestroyed", true)
addEventHandler("nines:tampaDestroyed", resourceRoot, function()
    if not validClient() or mission.stage ~= "gas_tank" or not isElement(mission.entities.tampa) or
        not isVehicleBlown(mission.entities.tampa) then
        return
    end
    mission.stage = "emmet_leave"
    stageEmmetDeparture()
    triggerClientEvent(client, "nines:emmetLeave", resourceRoot, mission.entities)
end)

addEvent("nines:leaveEmmetFinished", true)
addEventHandler("nines:leaveEmmetFinished", resourceRoot, function(skipped)
    if not validClient() or mission.stage ~= "emmet_leave" then
        return
    end
    mission.stage = "drive_smoke"
    local oldPistolAmmo = 0
    for _, weapon in ipairs(mission.snapshot.weapons) do
        if weapon[1] == 22 then
            oldPistolAmmo = weapon[2]
            break
        end
    end
    giveWeapon(client, 22, 1, true)
    setWeaponAmmo(client, 22, oldPistolAmmo + 60)
    if isElement(mission.entities.emmet) then
        destroyElement(mission.entities.emmet)
        mission.entities.emmet = nil
    end
    local car, smoke = mission.entities.glendale, mission.entities.smoke
    if isElement(car) then
        setElementPosition(car, 2452.6460, -2003.8763, 13.0576026)
        setElementRotation(car, 0, 0, 100.2244)
        warpPedIntoVehicle(client, car, 0)
        warpPedIntoVehicle(smoke, car, 1)
    end
    triggerClientEvent(client, "nines:drive", resourceRoot, "smoke", car, smoke, skipped == true)
end)

addEvent("nines:departureSeatFallback", true)
addEventHandler("nines:departureSeatFallback", resourceRoot, function()
    if not validClient() or mission.stage ~= "emmet_leave" then
        return
    end
    local car, smoke = mission.entities.glendale, mission.entities.smoke
    if isElement(car) and isElement(smoke) then
        warpPedIntoVehicle(client, car, 0)
        warpPedIntoVehicle(smoke, car, 1)
    end
end)

addEvent("nines:goodbyeFinished", true)
addEventHandler("nines:goodbyeFinished", resourceRoot, function()
    if not validClient() or mission.stage ~= "goodbye" then
        return
    end
    mission.stage = "phone"
    if isElement(mission.entities.smoke) then
        destroyElement(mission.entities.smoke)
        mission.entities.smoke = nil
    end
    triggerClientEvent(client, "nines:phone", resourceRoot)
end)

addEvent("nines:bincoInterior", true)
addEventHandler("nines:bincoInterior", resourceRoot, function(inside)
    if not validClient() or mission.stage ~= "phone" then
        return
    end
    if inside then
        local x, y, z = getElementPosition(client)
        local outside = NINES.binco.outside
        if getElementInterior(client) ~= 0 or getDistanceBetweenPoints3D(x, y, z, outside[1], outside[2], outside[3]) > 10.0 then
            return
        end
        mission.bincoEntered = true
        setElementInterior(client, NINES.binco.insideSpawn[4])
        setElementPosition(client, NINES.binco.insideSpawn[1], NINES.binco.insideSpawn[2], NINES.binco.insideSpawn[3])
        setElementRotation(client, 0, 0, NINES.binco.insideSpawn[5])
        triggerClientEvent(client, "nines:bincoEntered", resourceRoot)
    else
        local x, y, z = getElementPosition(client)
        local exit = NINES.binco.exit
        if not mission.bincoEntered or getElementInterior(client) ~= NINES.binco.insideSpawn[4] or
            getDistanceBetweenPoints3D(x, y, z, exit[1], exit[2], exit[3]) > 6.0 then
            return
        end
        mission.bincoExited = true
        setElementInterior(client, 0)
        setElementPosition(client, NINES.binco.outsideExit[1], NINES.binco.outsideExit[2], NINES.binco.outsideExit[3])
        setElementRotation(client, 0, 0, NINES.binco.outsideExit[4])
        triggerClientEvent(client, "nines:bincoExited", resourceRoot)
    end
end)

addEvent("nines:passed", true)
addEventHandler("nines:passed", resourceRoot, function()
    local x, y, z = getElementPosition(client)
    if not validClient() or mission.stage ~= "phone" or not mission.bincoExited or getElementInterior(client) ~= 0 or
        getDistanceBetweenPoints3D(x, y, z, NINES.binco.outsideExit[1], NINES.binco.outsideExit[2], NINES.binco.outsideExit[3]) > 10.0 then
        return
    end
    mission.finishing, mission.stage = true, "passed"
    triggerClientEvent(client, "nines:passed", resourceRoot)
    rememberTimer(setTimer(function()
        cleanup("passed", true, true)
    end, 5000, 1))
end)

addEventHandler("onPedWasted", root, function()
    if not mission.running or mission.finishing then
        return
    end
    if source == mission.entities.smoke then
        fail("Smoke died", "SWE2_B")
    elseif source == mission.entities.emmet then
        fail("Emmet died", "SWE2_K")
    elseif source == mission.player then
        fail("CJ died", "M_FAIL")
    end
end)

addEventHandler("onVehicleExplode", root, function()
    if not mission.running or mission.finishing then
        return
    end
    if source == mission.entities.glendale and mission.stage ~= "goodbye" and mission.stage ~= "phone" and mission.stage ~= "passed" then
        fail("Smoke's Glendale was destroyed", "SWE2_C")
    end
end)

addEventHandler("onPlayerQuit", root, function()
    if source == mission.player then
        cleanup("player_quit", false, false)
    end
end)

addEventHandler("onPlayerWasted", root, function()
    if source == mission.player and mission.running and not mission.finishing then
        fail("CJ died", "M_FAIL")
    end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    cleanup("resource_stop", true, false)
end)
